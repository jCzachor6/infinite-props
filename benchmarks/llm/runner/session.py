#!/usr/bin/env python3
"""Prepare and evaluate imported Codex session responses, with at most one repair.

This adapter does not call a model. The caller supplies the actual recorded
response; request context, raw payloads and every evaluated attempt are retained.
"""
import argparse
import json
import platform
import re
import sys
from pathlib import Path
from types import SimpleNamespace

import run

sys.path.insert(0, str(run.REPO / 'scripts'))
from contract_readers import read_diagnostics, read_language, read_manifest
from release_common import described_versions, handbook_version, match_versions

TASKS_SHA = '4862ffb223b403ffc65a418ed4bc5c1064f9423138c9b25e214ac9540c6f3ee0'
PROTOCOL_SHA = 'c35289e5e472e7bbb1a3c66c5a925c1e60e626bbed9465e3f3828b36766e871f'
FROZEN_BOOK_SHA = '4d77b7682bc08a5986218afb7ca25ece03153937d1c61c963852ea9f9c64d9f3'
FROZEN_EXPORT_SHA = '35e60446d78d1076d2aed03e295a714d7edac42d66bd3187463ae0aaff726643'
CONTEXT_LIMIT = 16 * 1024 * 1024
INPUT_NAMES = {'tasks.json', 'protocol.json', 'handbook.bin', 'language-export.bin',
               'reference-manifest.json', 'compiler-language-export.bin', 'identity.json',
               'prompt-0.txt', 'request-0.json', 'identity-check/process.json',
               'identity-check/stdout.bin', 'identity-check/stderr.bin'}
REPAIR_NAMES = {'repair-feedback.json', 'prompt-1.txt', 'request-1.json', 'completion-0.json'}
ADAPTER_NAMES = {'session.py', 'run.py', 'binary.py', 'png.py', 'criteria.py'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def bounded_bytes(path, limit):
    with path.open('rb') as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, str(path) + ': byte ceiling exceeded')
    return data


def parse_json(data):
    def invalid_constant(value):
        raise ValueError('non-JSON numeric constant: ' + value)
    return json.loads(data, object_pairs_hook=run.unique_object, parse_constant=invalid_constant)


def canonical_json(value):
    # Preserve JSON scalar types: Python False == 0 and 1 == 1.0 must not make
    # different compiler contracts appear equivalent.
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False,
                      allow_nan=False).encode('utf-8')


def json_bytes(value):
    # Escaping also retains malformed model strings containing lone surrogates
    # in feedback, without pretending those strings were extracted as UTF-8.
    return (json.dumps(value, ensure_ascii=True, indent=2, allow_nan=False) + '\n').encode('utf-8')


def write_new(path, data):
    with path.open('xb') as stream:
        stream.write(data)


def hash_file(path):
    import hashlib
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(65536), b''):
            digest.update(block)
    return digest.hexdigest()


def hashes(root, names):
    return {name: hash_file(root / name) for name in sorted(names)}


def verify_hashes(root, expected):
    require(isinstance(expected, dict), 'invalid hash manifest')
    for name, digest in expected.items():
        require(isinstance(name, str) and not Path(name).is_absolute() and
                '..' not in Path(name).parts and re.fullmatch(r'[0-9a-f]{64}', str(digest)),
                'invalid artifact identity')
        require(hash_file(root / name) == digest, 'artifact hash mismatch: ' + name)


def template(protocol, name, values):
    def replace(match):
        require(match[1] in values, 'unknown prompt placeholder: ' + match[1])
        return values[match[1]]
    return re.sub(r'\{\{([^{}]+)\}\}', replace, protocol['prompt'][name]).encode('utf-8')


def tool_identities(tools):
    return {name: {'path': str((tools / name).resolve(strict=True)),
                   'sha256': hash_file(tools / name)} for name in ('ipc', 'ipgen', 'ipa')}


def verify_tools(identity, override=None):
    require(hashes(Path(__file__).parent, ADAPTER_NAMES) == identity['runner_source_sha256'] and
            hash_file(run.REPO / 'scripts/contract_readers.py') == identity['contract_reader_sha256'] and
            hash_file(run.REPO / 'scripts/release_common.py') == identity['version_reader_sha256'],
            'evaluation adapter source identity changed')
    tools = (override or Path(identity['tools_directory'])).resolve()
    observed = tool_identities(tools)
    for name, expected in identity['tools'].items():
        require(observed[name]['sha256'] == expected['sha256'], 'tool binary hash mismatch: ' + name)
        if override is None:
            require(observed[name]['path'] == expected['path'], 'tool executable path changed: ' + name)
    return tools


def verify_inputs(root):
    seal = parse_json(bounded_bytes(root / 'sealed-inputs.json', CONTEXT_LIMIT))
    require(set(seal['files']) == INPUT_NAMES, 'incomplete sealed input manifest')
    verify_hashes(root, seal['files'])
    require(hash_file(root / 'tasks.json') == TASKS_SHA and hash_file(root / 'protocol.json') == PROTOCOL_SHA,
            'frozen task or protocol identity differs')
    return (parse_json((root / 'identity.json').read_bytes()),
            parse_json((root / 'tasks.json').read_bytes()),
            parse_json((root / 'protocol.json').read_bytes()))


def request_record(identity, prompt, attempt, task_ids):
    return {'schema_version': 1, 'backend': 'codex_session_saved_response_import',
            'attempt_index': attempt, 'maximum_repairs': 1, 'task_ids': task_ids,
            'record_kind': identity['record_kind'], 'prompt_sha256': run.sha(prompt),
            'roles': [{'role': 'user', 'content': prompt.decode('utf-8')}],
            'task_manifest_sha256': TASKS_SHA, 'protocol_sha256': PROTOCOL_SHA,
            'model_configuration': identity['model_configuration'],
            'model_call_observation': 'caller imports a recorded response; no online call by this CLI',
            'actual_model_call_count': None, 'actual_resolved_model': None,
            'sampling_parameters': None, 'token_usage': None, 'monetary_cost': None,
            'model_inference_wall_time_ms': None,
            'model_wall_budget_seconds': identity['model_wall_budget_seconds'],
            'model_budget_enforcement': 'external session orchestration; not observable by response import'}


def prepare(args):
    root, tools = args.output.resolve(), args.tools.resolve()
    require(not root.exists(), 'output already exists; choose a new session directory')
    sources = {'tasks.json': bounded_bytes(args.tasks, CONTEXT_LIMIT),
               'protocol.json': bounded_bytes(args.protocol, CONTEXT_LIMIT),
               'handbook.bin': bounded_bytes(args.reference, CONTEXT_LIMIT),
               'language-export.bin': bounded_bytes(args.language_export, CONTEXT_LIMIT),
               'reference-manifest.json': bounded_bytes(args.reference_manifest, CONTEXT_LIMIT)}
    require(run.sha(sources['tasks.json']) == TASKS_SHA and run.sha(sources['protocol.json']) == PROTOCOL_SHA,
            'frozen task or protocol bytes differ')
    tasks, protocol = parse_json(sources['tasks.json']), parse_json(sources['protocol.json'])
    catalog = parse_json(sources['language-export.bin'])
    profile = 'historical-0.4' if args.version == '0.4' else 'current'
    manifest = read_manifest(parse_json(sources['reference-manifest.json']), profile)
    read_language(catalog, profile=profile)
    product = catalog['product_version']
    book_name = 'docs/language-v' + handbook_version(catalog, profile) + '.md'
    if profile == 'current':
        match_versions(manifest, described_versions(catalog))
    require(manifest['product_version'] == product and
            manifest['files'].get(book_name) == run.sha(sources['handbook.bin']) and
            manifest['files'].get('docs/language.json') == run.sha(sources['language-export.bin']),
            'full handbook/export do not match the supplied generated manifest')
    if args.record_kind == 'actual_model_output':
        require(isinstance(manifest.get('source_commit'), str) and
                re.fullmatch(r'[0-9a-f]{40}', manifest['source_commit']) and
                manifest.get('working_tree_dirty') is False,
                'model measurement context requires a committed, clean generated reference')
    if args.version == '0.4':
        require(manifest.get('source_commit') == run.BASE_COMMIT and
                run.sha(sources['handbook.bin']) == FROZEN_BOOK_SHA and
                run.sha(sources['language-export.bin']) == FROZEN_EXPORT_SHA,
                'historical reference differs from the frozen 0.4 baseline')
    else:
        require(canonical_json(catalog['language_version']) == canonical_json({'major': 0, 'minor': 5}),
                'candidate adapter requires the 0.5 language contract')
    all_ids = [task['id'] for task in tasks['tasks']]
    selected = all_ids if args.only is None else [identifier for identifier in all_ids if identifier in args.only]
    require(selected and (args.only is None or set(args.only) <= set(all_ids)), 'empty or unknown task selection')
    pins = tool_identities(tools)
    root.mkdir(parents=True)
    for name, data in sources.items():
        write_new(root / name, data)
    query, stdout, stderr = run.process([str(tools / 'ipc'), '--describe-language'], tools,
                                       root / 'identity-check', protocol['budgets']['per_task_compile_wall_timeout_seconds'],
                                       protocol['budgets'])
    require(not query['failure'] and query['exit_code'] == 0 and not stderr, 'compiler language query failed')
    observed = parse_json(stdout)
    require(canonical_json(observed) == canonical_json(catalog), 'supplied export differs from the compiler contract')
    require(tool_identities(tools) == pins, 'tool executable changed during prepare')
    write_new(root / 'compiler-language-export.bin', stdout)
    identity = {'schema_version': 1, 'version': args.version, 'record_kind': args.record_kind,
                'suite_id': tasks['suite_id'], 'tools_directory': str(tools), 'tools': pins,
                'compiler_source_commit': manifest.get('source_commit'),
                'source_commit_observation': 'supplied generated manifest; tool binary hashes observed separately',
                'product_version_observed': product, 'language_version_observed': catalog.get('language_version'),
                'format_version_observed': catalog['format_version'],
                'model_configuration': protocol['model'],
                'model_wall_budget_seconds': protocol['budgets']['model_wall_timeout_per_version_batch_seconds'],
                'selected_task_ids': selected, 'required_suite_task_count': len(all_ids),
                'runtime_platform': platform.platform(), 'python_version': platform.python_version(),
                'prepared_at_utc': run.now(), 'maximum_repairs': 1,
                'runner_source_sha256': hashes(Path(__file__).parent, ADAPTER_NAMES),
                'contract_reader_sha256': hash_file(run.REPO / 'scripts/contract_readers.py'),
                'version_reader_sha256': hash_file(run.REPO / 'scripts/release_common.py'),
                'reference_serialization': 'supplied SDK bytes preserved; full typed JSON value matches raw compiler stdout'}
    prompt = template(protocol, 'generation_template', {
        'exact_frozen_reference': sources['handbook.bin'].decode('utf-8'),
        'exact_compiler_export': sources['language-export.bin'].decode('utf-8'),
        'exact_task_manifest': sources['tasks.json'].decode('utf-8')})
    write_new(root / 'identity.json', json_bytes(identity))
    write_new(root / 'prompt-0.txt', prompt)
    write_new(root / 'request-0.json', json_bytes(request_record(identity, prompt, 0, selected)))
    write_new(root / 'sealed-inputs.json', json_bytes({'schema_version': 1, 'files': hashes(root, INPUT_NAMES)}))
    return status(root)


def completion(root, attempt):
    path = root / f'completion-{attempt}.json'
    if not path.exists():
        return None
    value = parse_json(bounded_bytes(path, CONTEXT_LIMIT))
    directory = root / 'attempts' / str(attempt)
    actual_files = {str(item.relative_to(root)) for item in directory.rglob('*') if item.is_file()}
    require(set(value['files']) == actual_files and value['summary']['attempt_index'] == attempt,
            'incomplete attempt artifact manifest')
    verify_hashes(root, value['files'])
    require(hash_file(directory / 'response.json') == value['response_sha256'], 'response identity differs')
    return value


def verify_repair_request(root):
    seal = parse_json(bounded_bytes(root / 'repair-seal.json', CONTEXT_LIMIT))
    require(set(seal['files']) == REPAIR_NAMES, 'incomplete repair request seal')
    verify_hashes(root, seal['files'])


def status(root):
    identity, _tasks, _protocol = verify_inputs(root)
    first, second = completion(root, 0), completion(root, 1)
    state = 'awaiting_initial_response'
    if (root / 'attempts/0').exists() and first is None:
        state = 'initial_evaluation_incomplete'
    elif first:
        if (root / 'attempts/1').exists() and second is None:
            state = 'repair_evaluation_incomplete'
        elif second:
            state = 'repair_budget_exhausted' if second['failed_task_ids'] else 'completed_after_one_repair'
        elif first['failed_task_ids']:
            if (root / 'repair-seal.json').exists():
                verify_repair_request(root)
                state = 'awaiting_repair_response'
            else:
                state = 'repair_preparation_incomplete'
        else:
            state = 'completed_without_repair'
    latest = {row['task_id']: row for value in [first, second] if value for row in value['summary']['tasks']}
    return {'state': state, 'version': identity['version'], 'record_kind': identity['record_kind'],
            'selected_task_count': len(identity['selected_task_ids']),
            'required_suite_task_count': identity['required_suite_task_count'],
            'evaluated_attempts': int(first is not None) + int(second is not None),
            'maximum_repairs': 1, 'initial': first['summary'] if first else None,
            'repair': second['summary'] if second else None,
            'first_pass_compile_success_count': first['summary']['compile_success_count'] if first else None,
            'first_pass_automated_success_count': first['summary']['automated_success_count'] if first else None,
            'latest_compile_success_count': sum(row['compile_success'] for row in latest.values()) if first else None,
            'latest_automated_success_count': sum(row['automated_verdict'] == 'pass' for row in latest.values()) if first else None,
            'unevaluated_task_ids': [task['id'] for task in _tasks['tasks'] if task['id'] not in latest],
            'fully_reviewed_success_count': None,
            'model_call_count': None, 'token_usage': None, 'model_inference_wall_time_ms': None}


def extract(raw, allowed):
    generated, error = {}, None
    try:
        payload = parse_json(raw)
        items = payload['tasks'] if isinstance(payload, dict) else payload
        require(isinstance(items, list), 'response must contain a tasks array')
        for item in items:
            require(isinstance(item, dict), 'task response must be an object')
            identifier = item.get('task_id')
            require(isinstance(identifier, str) and identifier in allowed, 'unknown task ID in response')
            require(identifier not in generated, 'duplicate task ID in response')
            generated[identifier] = item.get('files')
    except (ValueError, TypeError, KeyError, UnicodeError, RecursionError) as failure:
        generated, error = {}, str(failure)
    return generated, error


def import_response(root, response, attempt, tools_override=None):
    require(type(attempt) is int and attempt in (0, 1), 'only initial attempt 0 and one repair attempt 1 are allowed')
    identity, tasks, protocol = verify_inputs(root)
    tools = verify_tools(identity, tools_override)
    first = completion(root, 0)
    if attempt:
        require(first is not None and first['failed_task_ids'], 'there are no evaluated failures eligible for repair')
        verify_repair_request(root)
        selected = first['failed_task_ids']
    else:
        selected = identity['selected_task_ids']
    directory = root / 'attempts' / str(attempt)
    require(not directory.exists(), 'attempt already exists; maximum one initial response and one repair')
    # Oversized inputs are refused before accepting an import. The caller's
    # original response is untouched, and a prepared session remains awaiting.
    raw = bounded_bytes(response, protocol['budgets']['max_model_response_bytes_per_version_batch'])
    directory.mkdir(parents=True)
    write_new(directory / 'response.json', raw)
    all_ids = [task['id'] for task in tasks['tasks']]
    generated, error = extract(raw, selected if attempt else all_ids)
    missing = [identifier for identifier in selected if identifier not in generated]
    write_new(directory / 'extraction.json', json_bytes({'response_sha256': run.sha(raw),
        'request_sha256': hash_file(root / f'request-{attempt}.json'),
        'status': 'malformed_response' if error else 'parsed', 'message': error,
        'missing_task_ids': missing, 'model_origin_observation': 'caller assertion/record-kind; no model call made by this adapter'}))
    args = SimpleNamespace(attempt=attempt, version=identity['version'], tools=tools,
                           record_kind=identity['record_kind'], diagnostic_reader=read_diagnostics,
                           diagnostic_parser=parse_json)
    records = []
    selected_tasks = [task for task in tasks['tasks'] if task['id'] in selected]
    for task in selected_tasks:
        record = run.evaluate(task, generated.get(task['id']), args, directory / 'evaluations', protocol)
        if error:
            record['generation_status'] = 'malformed_response'
            record['extraction_error'] = error
        record.update(batch_id=f'attempt-{attempt}', generator_agent_id=None,
            exact_raw_model_response_path=f'attempts/{attempt}/response.json', raw_response_sha256=run.sha(raw),
            prompt_messages_path=f'request-{attempt}.json', combined_prompt_path=f'prompt-{attempt}.txt',
            combined_prompt_sha256=hash_file(root / f'prompt-{attempt}.txt'),
            feedback_source_sha256=hash_file(root / 'repair-feedback.json') if attempt else None,
            model_configuration=identity['model_configuration'], model_call_count=None,
            token_usage=None, model_inference_wall_time_ms=None,
            repair_requested=not attempt and record['overall_automated_verdict'] == 'fail')
        run.save_json(directory / 'evaluations' / task['id'] / f'attempt-{attempt}' / 'record.json', record)
        records.append(record)
    verify_tools(identity, tools_override)
    failed = [record['task_id'] for record in records if record['overall_automated_verdict'] == 'fail']
    summary = {'attempt_index': attempt, 'record_kind': identity['record_kind'], 'evaluated_task_count': len(records),
               'required_suite_task_count': len(all_ids),
               'compile_success_count': sum(record['compile_success'] for record in records),
               'automated_success_count': sum(record['overall_automated_verdict'] == 'pass' for record in records),
               'fully_reviewed_success_count': None, 'source_feature_review': 'pending_manual_review',
               'human_visual_pending_count': sum(record['human_visual_verdict'] == 'pending' for record in records),
               'unevaluated_task_ids': [identifier for identifier in all_ids if identifier not in selected],
               'tasks': [{'task_id': record['task_id'], 'compile_success': record['compile_success'],
                          'automated_verdict': record['overall_automated_verdict'],
                          'failure_stage': record['failure_stage'],
                          'human_visual_verdict': record['human_visual_verdict']} for record in records],
               'model_call_count': None, 'token_usage': None, 'model_inference_wall_time_ms': None}
    files = {str(path.relative_to(root)): hash_file(path) for path in sorted(directory.rglob('*')) if path.is_file()}
    write_new(root / f'completion-{attempt}.json', json_bytes({'schema_version': 1, 'summary': summary,
        'failed_task_ids': failed, 'response_sha256': run.sha(raw), 'files': files,
        'completed_at_utc': run.now()}))
    if not attempt and failed:
        feedback = [{'task_id': record['task_id'], 'task_definition': task,
                     'files': record.get('actual_generated_files', generated.get(record['task_id'])),
                     'generated_file_sha256': record.get('generated_file_sha256'),
                     'feedback': record['public_failure_feedback'], 'extraction_error': error}
                    for task, record in zip(selected_tasks, records) if record['task_id'] in failed]
        feedback_data = json_bytes({'version': identity['version'], 'attempt_index': 0,
                                    'feedback_source': 'actual imported sources, compiler processes and public predicates',
                                    'tasks': feedback})
        write_new(root / 'repair-feedback.json', feedback_data)
        prompt = template(protocol, 'repair_template', {
            'exact_frozen_reference': (root / 'handbook.bin').read_bytes().decode('utf-8'),
            'exact_compiler_export': (root / 'language-export.bin').read_bytes().decode('utf-8'),
            'exact_failed_records': feedback_data.decode('utf-8')})
        write_new(root / 'prompt-1.txt', prompt)
        write_new(root / 'request-1.json', json_bytes(request_record(identity, prompt, 1, failed)))
        write_new(root / 'repair-seal.json', json_bytes({'schema_version': 1, 'files': hashes(root, REPAIR_NAMES)}))
    return status(root)


def replay(source, output, tools_override=None):
    identity, _tasks, _protocol = verify_inputs(source)
    verify_tools(identity, tools_override)
    first, second = completion(source, 0), completion(source, 1)
    require(not (source / 'attempts/0').exists() or first is not None, 'initial evaluation is incomplete and not sealed for replay')
    require(not (source / 'attempts/1').exists() or second is not None, 'repair evaluation is incomplete and not sealed for replay')
    if first and first['failed_task_ids']:
        verify_repair_request(source)
    require(not output.exists(), 'replay output already exists; choose a new directory')
    output.mkdir(parents=True)
    for name in sorted(INPUT_NAMES | {'sealed-inputs.json'}):
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        write_new(target, (source / name).read_bytes())
    if first:
        import_response(output, source / 'attempts/0/response.json', 0, tools_override)
    if second:
        require(completion(output, 0)['failed_task_ids'] == first['failed_task_ids'],
                'replayed first-pass failures differ from the saved repair request')
        for name in ['repair-feedback.json', 'prompt-1.txt', 'request-1.json']:
            require((output / name).read_bytes() == (source / name).read_bytes(),
                    'replay repair context differs: ' + name)
        import_response(output, source / 'attempts/1/response.json', 1, tools_override)
    write_new(output / 'replay-observation.json', json_bytes({'schema_version': 1,
        'source_directory': str(source), 'source_seal_sha256': hash_file(source / 'sealed-inputs.json'),
        'source_completion_sha256': {str(attempt): hash_file(source / f'completion-{attempt}.json')
                                     for attempt, value in enumerate([first, second]) if value},
        'policy': 'saved response replay, no model call, source records unchanged; CI integration remains separate'}))
    return status(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prepare_parser = commands.add_parser('prepare')
    prepare_parser.add_argument('--output', type=Path, required=True)
    prepare_parser.add_argument('--tools', type=Path, required=True)
    prepare_parser.add_argument('--version', choices=['0.4', '0.5'], required=True)
    prepare_parser.add_argument('--reference', type=Path, required=True)
    prepare_parser.add_argument('--reference-manifest', type=Path, required=True)
    prepare_parser.add_argument('--language-export', type=Path, required=True)
    prepare_parser.add_argument('--tasks', type=Path, default=run.REPO / 'benchmarks/llm/tasks.json')
    prepare_parser.add_argument('--protocol', type=Path, default=run.REPO / 'benchmarks/llm/protocol.json')
    prepare_parser.add_argument('--record-kind', choices=['actual_model_output', 'authored_witness'], default='actual_model_output')
    prepare_parser.add_argument('--only', nargs='+')
    for name in ['import-response', 'import-repair']:
        child = commands.add_parser(name)
        child.add_argument('--run', type=Path, required=True)
        child.add_argument('--response', type=Path, required=True)
    child = commands.add_parser('status')
    child.add_argument('--run', type=Path, required=True)
    child = commands.add_parser('replay')
    child.add_argument('--run', type=Path, required=True)
    child.add_argument('--output', type=Path, required=True)
    child.add_argument('--tools', type=Path)
    args = parser.parse_args()
    if args.command == 'prepare':
        result = prepare(args)
    elif args.command == 'status':
        result = status(args.run.resolve())
    elif args.command == 'replay':
        result = replay(args.run.resolve(), args.output.resolve(), args.tools)
    else:
        result = import_response(args.run.resolve(), args.response, int(args.command == 'import-repair'))
    print(json.dumps(result, ensure_ascii=True, indent=2, allow_nan=False))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, KeyError, TypeError, OSError, UnicodeError, RuntimeError) as error:
        print('session: ' + str(error), file=sys.stderr)
        sys.exit(2)
