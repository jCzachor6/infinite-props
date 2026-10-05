#!/usr/bin/env python3
"""Run archived actual model sources against a frozen compiler/runtime.

Use --record-kind authored_witness only for harness verification; those records
are explicitly excluded from model measurement claims.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import selectors
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

from binary import preflight
from criteria import image_checks, normalize_outputs, output_checks, verdict
from png import decode

REPO = next((path for path in [*Path(__file__).resolve().parents, Path.cwd()]
             if (path / 'benchmarks/llm/tasks.json').is_file()), Path.cwd())
BASE_COMMIT = '66d34c2831585afb231022da9296528a912e962e'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def now():
    return datetime.now(timezone.utc).isoformat()


def save_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError('duplicate JSON key: ' + key)
        result[key] = value
    return result


def read_json(path):
    return json.loads(path.read_bytes(), object_pairs_hook=unique_object)


def imports(source):
    # Strings and comments are recognized before identifiers. Emoji import is
    # normalized only here; no generated source bytes are changed.
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\n]*|[A-Za-z_][A-Za-z0-9_]*|[^\s]', source)
    tokens = [token for token in tokens if not token.startswith(('/*', '//'))]
    result = []
    for index, token in enumerate(tokens):
        if token in ('import', '🔗') and index + 3 < len(tokens) and tokens[index + 2] == 'from' and tokens[index + 3].startswith('"'):
            result.append(json.loads(tokens[index + 3]))
    return result


def safe_sources(files, task, limits):
    if not isinstance(files, dict) or task['entry_file'] not in files or not 1 <= len(files) <= limits['max_generated_files_per_task']:
        raise ValueError('missing entry or invalid file map/count')
    total, encoded = 0, {}
    for name, source in files.items():
        if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*\.ips', name) or any(part in ('.', '..') for part in name.split('/')):
            raise ValueError('unsafe source path: ' + str(name))
        if not isinstance(source, str) or '\0' in source:
            raise ValueError('source must be UTF-8 text without NUL')
        data = source.encode('utf-8', 'strict')
        if len(data) > limits['max_source_bytes_per_file']:
            raise ValueError('source file exceeds byte ceiling')
        total += len(data)
        encoded[name] = data
    if total > limits['max_total_source_bytes_per_task']:
        raise ValueError('total source bytes exceed ceiling')
    for name, source in files.items():
        targets = imports(source)
        if name != task['entry_file'] and targets:
            raise ValueError('nested imports outside benchmark subset')
        if any(target not in files for target in targets):
            raise ValueError('import must resolve to a safe bundled file')
    return encoded


def process(command, cwd, destination, timeout, limits):
    destination.mkdir(parents=True, exist_ok=True)
    started, began = now(), time.monotonic()
    child = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(child.stdout, selectors.EVENT_READ, 'stdout')
    selector.register(child.stderr, selectors.EVENT_READ, 'stderr')
    buffers = {'stdout': bytearray(), 'stderr': bytearray()}
    failure = None
    while selector.get_map():
        if time.monotonic() - began > timeout:
            failure = 'timeout'; break
        for key, _ in selector.select(timeout=min(0.1, timeout)):
            chunk = os.read(key.fileobj.fileno(), 65536)
            if not chunk:
                selector.unregister(key.fileobj)
                key.fileobj.close()
                continue
            name = key.data
            ceiling = limits[f'max_{name}_bytes_per_process']
            available = ceiling - len(buffers[name])
            buffers[name].extend(chunk[:available])
            if len(chunk) > available:
                failure = 'log_limit'; break
        if failure: break
    if failure:
        try: os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError: pass
    try: child.wait(timeout=max(0.1, timeout - (time.monotonic() - began)))
    except subprocess.TimeoutExpired:
        failure = 'timeout'
        try: os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        child.wait()
    selector.close()
    for name, data in buffers.items():
        (destination / f'{name}.bin').write_bytes(data)
    record = {'command': command, 'cwd': str(cwd), 'started_at_utc': started, 'finished_at_utc': now(), 'wall_time_ms': (time.monotonic() - began) * 1000, 'exit_code': child.returncode, 'failure': failure, 'stdout_path': str(destination / 'stdout.bin'), 'stderr_path': str(destination / 'stderr.bin'), 'stdout_sha256': sha(buffers['stdout']), 'stderr_sha256': sha(buffers['stderr']), 'logs_truncated': failure == 'log_limit'}
    save_json(destination / 'process.json', record)
    return record, bytes(buffers['stdout']), bytes(buffers['stderr'])


def normalize_diagnostics(stderr, stdout, version):
    if version != '0.4':
        try: return json.loads(stdout)
        except (ValueError, UnicodeDecodeError): return {'status': 'invalid_diagnostic_json', 'diagnostics': []}
    result = []
    for line in stderr.decode('utf-8', 'replace').splitlines():
        match = re.match(r'^ipc: (.+):(\d+):(\d+): error: (.*)$', line)
        if match:
            result.append({'file': match[1], 'line': int(match[2]), 'column': int(match[3]), 'severity': 'error', 'message': match[4], 'code': None, 'source_range': None, 'fix_its': None})
    return {'schema_version': None, 'status': 'historical_text_adapter', 'diagnostics': result}


def evaluate(task, files, args, root, protocol):
    limits, directory = protocol['budgets'], root / task['id'] / f'attempt-{args.attempt}'
    directory.mkdir(parents=True)
    record = {'task_id': task['id'], 'task_definition_sha256': task['definition_sha256'], 'sample_index': 0, 'attempt_index': args.attempt, 'record_kind': args.record_kind, 'started_at_utc': now(), 'generation_status': 'source_present' if files is not None else 'missing_task', 'compile_success': False, 'preflight_verdict': 'not_run', 'render_records': [], 'source_feature_verdicts': [{'feature': name, 'verdict': 'pending_manual_review'} for name in task['required_features']], 'human_visual_verdict': 'pending' if task['image_criteria']['human_visual_review_required'] else 'not_required', 'overall_automated_verdict': 'not_run', 'limitations': ['No per-task model inference timing, revision, sampling, tokens or cost exposed.', 'Dynamic output element ceiling is checked after execution; declared output slots are bounded before execution. Frozen API has no shorter host fuel or total-memory override.'], 'failure_stage': None}
    feedback = []
    try:
        if files is None: raise ValueError('no actual generated source for task')
        encoded = safe_sources(files, task, limits)
        workspace = directory / 'sources'
        workspace.mkdir()
        for name, data in encoded.items():
            destination = workspace / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        record.update(actual_generated_files=files, generated_file_sha256={name: sha(data) for name, data in encoded.items()}, source_total_bytes=sum(map(len, encoded.values())))
        output = directory / 'asset.iprop'
        if args.version != '0.4':
            command = [str(args.tools / 'ipc'), task['entry_file'], '--check', '--diagnostics=json']
            checked, stdout, stderr = process(command, workspace, directory / 'check', limits['per_task_compile_wall_timeout_seconds'], limits)
            record['check_process'] = checked
            record['normalized_diagnostics'] = normalize_diagnostics(stderr, stdout, args.version)
            if checked['exit_code'] or checked['failure']:
                record['failure_stage'] = 'check'; feedback.append({'stage': 'check', 'exit_code': checked['exit_code'], 'stdout': stdout.decode('utf-8', 'replace'), 'stderr': stderr.decode('utf-8', 'replace')}); return finish(record, directory, feedback)
        command = [str(args.tools / 'ipc'), task['entry_file'], '-o', str(output)]
        compiled, stdout, stderr = process(command, workspace, directory / 'compile', limits['per_task_compile_wall_timeout_seconds'], limits)
        record['compile_process'] = compiled
        record.setdefault('normalized_diagnostics', normalize_diagnostics(stderr, stdout, args.version))
        if compiled['exit_code'] or compiled['failure'] or not output.is_file():
            record['failure_stage'] = 'compile'; feedback.append({'stage': 'compile', 'exit_code': compiled['exit_code'], 'stdout': stdout.decode('utf-8', 'replace'), 'stderr': stderr.decode('utf-8', 'replace')}); return finish(record, directory, feedback)
        record['compile_success'] = True
        if output.stat().st_size > limits['max_compiled_binary_bytes']: raise ValueError('compiled bytes exceed budget')
        binary = output.read_bytes()
        record['compiled_binary_sha256'] = sha(binary)
        schema = preflight(binary, limits)
        save_json(directory / 'preflight.json', schema)
        record['preflight_verdict'] = 'pass'
        if [schema['width'], schema['height']] != task['image_criteria']['dimensions']:
            feedback.append({'stage': 'preflight', 'criterion': 'dimensions', 'message': 'asset dimensions differ from task 32x32'})
            record['failure_stage'] = 'preflight'; return finish(record, directory, feedback)
        for case in task['render_cases']:
            repetitions = []
            for repetition in range(case['repetitions']):
                destination = directory / 'renders' / case['id'] / str(repetition)
                destination.mkdir(parents=True)
                png_path = destination / 'image.png'
                command = [str(args.tools / 'ipgen'), str(output), '--seed', str(case['seed']), '--variant', str(case['variant']), '--archetype', str(case['archetype']), '--float-bits', '-o', str(png_path)]
                for name, value in case['uniforms'].items(): command += ['--uniform', f'{name}={value}']
                ran, stdout, stderr = process(command, workspace, destination, limits['per_case_render_wall_timeout_seconds'], limits)
                result = {'case_id': case['id'], 'seed': case['seed'], 'variant': case['variant'], 'archetype': case['archetype'], 'uniforms': case['uniforms'], 'repetition': repetition, 'process': ran, 'criterion_verdicts': [], 'preflight_verdict': 'pass', 'trap': None, 'png_path': str(png_path)}
                if ran['exit_code'] or ran['failure'] or not png_path.is_file():
                    result['verdict'] = 'fail'; result['trap'] = stderr.decode('utf-8', 'replace') or ran['failure']
                    feedback.append({'stage': 'render', 'case_id': case['id'], 'repetition': repetition, 'exit_code': ran['exit_code'], 'stderr': stderr.decode('utf-8', 'replace'), 'failure': ran['failure']})
                else:
                    try:
                        raw = json.loads(stdout, object_pairs_hook=unique_object)
                        outputs = normalize_outputs(raw, schema, limits['max_output_elements_per_render'])
                        if raw['fuel']['budget'] != schema['fuel'] or not 0 <= raw['fuel']['spent'] <= schema['fuel']: raise ValueError('runtime fuel differs from preflight/ceiling')
                        if png_path.stat().st_size > limits['max_stdout_bytes_per_process']: raise ValueError('PNG file exceeds budget')
                        png_bytes = png_path.read_bytes()
                        width, height, pixels = decode(png_bytes, limits['max_root_pixels'])
                        (destination / 'rgba.bin').write_bytes(pixels)
                        save_json(destination / 'normalized-outputs.json', outputs)
                        checks = output_checks(task, case, schema, outputs) + image_checks(task, case, schema, outputs, width, height, pixels)
                        result.update(width=width, height=height, png_sha256=sha(png_bytes), decoded_rgba_sha256=sha(pixels), outputs_sha256=sha(json.dumps(outputs, sort_keys=True, separators=(',', ':')).encode()), outputs_raw_path=ran['stdout_path'], normalized_outputs_path=str(destination / 'normalized-outputs.json'), output_owner_observation='observed via independent binary schema/global slot mapping; raw ipgen owner=null', fuel_budget=raw['fuel']['budget'], fuel_used=raw['fuel']['spent'], criterion_verdicts=checks)
                        result['verdict'] = 'fail' if any(item['verdict'] == 'fail' for item in checks) else 'pass'
                        for item in checks:
                            if item['verdict'] == 'fail': feedback.append({'stage': 'criterion', 'case_id': case['id'], 'repetition': repetition, **item})
                    except (ValueError, KeyError, TypeError, OverflowError, OSError) as error:
                        result['verdict'] = 'fail'; result['trap'] = None
                        feedback.append({'stage': 'result_decode', 'case_id': case['id'], 'repetition': repetition, 'message': str(error)})
                save_json(destination / 'record.json', result)
                repetitions.append(result); record['render_records'].append(result)
            good = [result for result in repetitions if result.get('decoded_rgba_sha256')]
            identical = len(good) == case['repetitions'] and len({(result['decoded_rgba_sha256'], result['outputs_sha256'], result['fuel_used']) for result in good}) == 1
            record.setdefault('repeat_verdicts', []).append(verdict('repeat:' + case['id'], identical))
            if not identical: feedback.append({'stage': 'determinism', 'case_id': case['id'], 'message': 'missing or differing repeated RGBA/outputs/fuel'})
        hashes = {result['decoded_rgba_sha256'] for result in record['render_records'] if result.get('decoded_rgba_sha256')}
        for index, rule in enumerate(task['image_criteria']['pixel_rules']):
            if rule['kind'] == 'different_seed_images':
                passed = len(hashes) >= rule['minimum_distinct']
                record.setdefault('cross_case_verdicts', []).append(verdict(f'image:{index}:different_seed_images', passed))
                for result in record['render_records']:
                    for item in result['criterion_verdicts']:
                        if item['criterion'] == f'image:{index}:different_seed_images':
                            item.update(verdict='pass' if passed else 'fail', message=None)
                if not passed: feedback.append({'stage': 'cross_case', 'criterion': 'different_seed_images', 'message': 'insufficient distinct seed images'})
        for result in record['render_records']:
            save_json(Path(result['png_path']).parent / 'record.json', result)
        pending = any(item['verdict'] == 'pending' for result in record['render_records'] for item in result['criterion_verdicts'])
        record['overall_automated_verdict'] = 'fail' if feedback else ('pending' if pending else 'pass')
        if feedback: record['failure_stage'] = 'render_or_criterion'
    except (ValueError, TypeError, KeyError, OSError, UnicodeError) as error:
        record['failure_stage'] = 'source_preflight' if not record['compile_success'] else 'binary_preflight'
        feedback.append({'stage': record['failure_stage'], 'message': str(error)})
    return finish(record, directory, feedback)


def finish(record, directory, feedback):
    if feedback and record['overall_automated_verdict'] == 'not_run': record['overall_automated_verdict'] = 'fail'
    record['finished_at_utc'] = now()
    record['public_failure_feedback'] = feedback
    save_json(directory / 'record.json', record)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--response', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--version', choices=['0.4', '0.5'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tasks', type=Path, default=REPO / 'benchmarks/llm/tasks.json')
    parser.add_argument('--protocol', type=Path, default=REPO / 'benchmarks/llm/protocol.json')
    parser.add_argument('--reference', type=Path, action='append', default=[])
    parser.add_argument('--language-export', type=Path)
    parser.add_argument('--prompt', type=Path)
    parser.add_argument('--commit')
    parser.add_argument('--attempt', type=int, choices=[0, 1], default=0)
    parser.add_argument('--only', nargs='*')
    parser.add_argument('--record-kind', choices=['actual_model_output', 'authored_witness'], default='actual_model_output')
    args = parser.parse_args()
    args.tools, args.output, args.response = args.tools.resolve(), args.output.resolve(), args.response.resolve()
    manifest, protocol = read_json(args.tasks), read_json(args.protocol)
    raw = args.response.read_bytes()
    if len(raw) > protocol['budgets']['max_model_response_bytes_per_version_batch']: raise ValueError('response exceeds byte ceiling')
    response = json.loads(raw, object_pairs_hook=unique_object)
    tasks = response['tasks'] if isinstance(response, dict) else response
    if not isinstance(tasks, list): raise ValueError('response must contain a tasks array')
    generated = {}
    for item in tasks:
        identifier = item.get('task_id')
        if identifier in generated or identifier not in {task['id'] for task in manifest['tasks']}: raise ValueError('duplicate or unknown task ID')
        generated[identifier] = item.get('files')
    if args.output.exists(): raise ValueError('output directory already exists; choose a new immutable run directory')
    args.output.mkdir(parents=True)
    (args.output / 'actual-model-response.json').write_bytes(raw)
    (args.output / 'tasks.json').write_bytes(args.tasks.read_bytes())
    (args.output / 'protocol.json').write_bytes(args.protocol.read_bytes())
    tool_hashes = {}
    for name in ('ipc', 'ipgen', 'ipa'):
        path = args.tools / name
        if not path.is_file(): raise ValueError('missing tool: ' + str(path))
        tool_hashes[name] = sha(path.read_bytes())
    references = []
    for index, source in enumerate(args.reference):
        data = source.read_bytes(); destination = args.output / f'reference-{index}.bin'; destination.write_bytes(data)
        references.append({'source': str(source.resolve()), 'saved': str(destination), 'sha256': sha(data)})
    captured = {}
    for field in ('prompt', 'language_export'):
        source = getattr(args, field)
        if source:
            data = source.read_bytes(); path = args.output / f'{field}.bin'; path.write_bytes(data)
            captured[field] = {'source': str(source.resolve()), 'saved': str(path), 'sha256': sha(data)}
        else: captured[field] = None
    metadata = {'schema_version': 1, 'run_id': args.output.name, 'suite_id': manifest['suite_id'], 'version': args.version, 'record_kind': args.record_kind, 'compiler_source_commit': args.commit or (BASE_COMMIT if args.version == '0.4' else None), 'source_commit_observation': 'caller assertion/frozen baseline identity; binary SHA observed separately', 'tool_binary_sha256': tool_hashes, 'validator_binary_sha256': None, 'validator_evidence': 'ipc includes loadFile/validate; no separate frozen ipval executable provided', 'runtime_platform': platform.platform(), 'python_version': platform.python_version(), 'task_manifest_sha256': sha(args.tasks.read_bytes()), 'protocol_sha256': sha(args.protocol.read_bytes()), 'raw_response_sha256': sha(raw), 'reference_artifacts': references, **captured, 'model_configuration': protocol['model'], 'started_at_utc': now(), 'actual_observable_batch_count': None, 'token_usage': None, 'inference_timing': None, 'actual_generated_task_count': len(generated), 'required_suite_task_count': len(manifest['tasks']), 'definitions_only': False, 'manual_review_pending': True, 'runner_source_sha256': {path.name: sha(path.read_bytes()) for path in Path(__file__).parent.glob('*.py') if path.name != 'validate_contracts.py'}}
    save_json(args.output / 'run.json', metadata)
    selected = [task for task in manifest['tasks'] if args.only is None or task['id'] in args.only]
    records = []
    for task in selected:
        record = evaluate(task, generated.get(task['id']), args, args.output, protocol)
        records.append(record)
        print(task['id'] + ': compile=' + str(record['compile_success']) + ', automatic=' + record['overall_automated_verdict'], flush=True)
    feedback = [{'task_id': record['task_id'], 'files': record.get('actual_generated_files'), 'generated_file_sha256': record.get('generated_file_sha256'), 'feedback': record['public_failure_feedback']} for record in records if record['overall_automated_verdict'] == 'fail']
    save_json(args.output / 'repair-feedback.json', {'version': args.version, 'attempt_index': args.attempt, 'feedback_source': 'actual recorded processes and public predicates, no solved witness sources', 'tasks': feedback})
    summary = {'schema_version': 1, 'version': args.version, 'record_kind': args.record_kind, 'evaluated_task_count': len(records), 'required_suite_task_count': len(manifest['tasks']), 'compile_success_count': sum(record['compile_success'] for record in records), 'automated_success_count': sum(record['overall_automated_verdict'] == 'pass' for record in records), 'fully_reviewed_success_count': None, 'manual_source_review_pending_count': len(records), 'human_visual_pending_count': sum(record['human_visual_verdict'] == 'pending' for record in records), 'token_usage': None, 'model_quality_improvement': None, 'finished_at_utc': now(), 'tasks': [{'task_id': record['task_id'], 'compile_success': record['compile_success'], 'automated_verdict': record['overall_automated_verdict'], 'human_visual_verdict': record['human_visual_verdict'], 'failure_stage': record['failure_stage']} for record in records]}
    save_json(args.output / 'summary.json', summary)
    metadata['finished_at_utc'] = now(); metadata['complete_task_count'] = len(records)
    save_json(args.output / 'run.json', metadata)
    print(json.dumps({key: value for key, value in summary.items() if key != 'tasks'}, indent=2))
    return 0


if __name__ == '__main__':
    try: sys.exit(main())
    except (ValueError, KeyError, TypeError, OSError) as error:
        print('runner: ' + str(error), file=sys.stderr); sys.exit(2)
