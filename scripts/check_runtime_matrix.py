#!/usr/bin/env python3
"""Execute the current runtime and full compiler matrix against old/new sources.

The frozen manifest/artifacts are read only. Preservation means executing their
actual binaries with the current ipgen, not merely verifying archived hashes.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys
import time
import zlib

from capture_v04_baseline import RELEASE, encoded, png_rgba
from release_common import VERSION_SOURCES, described_versions, versions

ROOT = Path(__file__).resolve().parent.parent
MODES = ('debug', 'stripped')
OPTIMIZATIONS = ('reference', 'optimized')
CONTAINERS = ('plain', 'compress')
# These are implemented source-language repairs, not rewritten historical pins.
TRANSITIONS = {
    'examples/language/errors/negative-default.ips': '05-C.2 signed declaration constants',
    'examples/language/errors/signed-input.ips': '05-C.2 i32 source input mapped to existing U32 tag',
    'examples/language/nested-import.ips': '0.6 nested part imports',
}
# Released sources whose recompiled meaning changes on purpose. Each differing
# result is recorded, and an entry that no longer differs is an error.
SEMANTIC_CHANGES = {
    'examples/language/composition.ips': '0.6 part outputs in caller coordinates: the anchor read and the pixel drawn there move by the placement',
    'backends/html5/test/fixtures/part-transforms.ips': '0.6 part outputs in caller coordinates: read and host values include the blit origin',
}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_record(path):
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path.read_bytes())}


def source_identity(current_files):
    result = {directory: {p.relative_to(ROOT).as_posix(): sha(p.read_bytes())
                          for p in (ROOT / directory).rglob('*') if p.is_file()}
              for directory in ('runtime', 'compiler', 'language', 'tools')}
    inputs = set(current_files) | {source[0] for source in VERSION_SOURCES.values()} | {
        'examples/language/cases.json', 'cmake/RuntimeSources.cmake', 'cmake/CompilerWarnings.cmake'}
    result['corpus_and_build_inputs'] = {name: sha((ROOT / name).read_bytes()) for name in sorted(inputs)}
    return result


def command(argv, timeout):
    return subprocess.run([str(a) for a in argv], capture_output=True, text=True,
                          encoding='utf-8', errors='replace', timeout=timeout)


def normalized(text):
    # Byte offsets may change after optimization/debug stripping. Reasons may not.
    return re.sub(r'byte offset [0-9]+', 'byte offset <relocated>', text.replace('\r\n', '\n'))


def comparable(outcome):
    if outcome['status']:
        return encoded({'status': outcome['status'], 'reason': normalized(outcome['stderr'])})
    return encoded({key: outcome[key] for key in ('rgba_sha256', 'outputs_sha256', 'size', 'inputs')})


def expanded(data):
    require(len(data) >= 20 and data[:4] == b'IPRP', 'invalid compiler artifact header')
    if not struct.unpack_from('<I', data, 8)[0] & 2:
        return data
    require(len(data) >= 24, 'short compressed envelope')
    size = struct.unpack_from('<I', data, 20)[0]
    require(20 <= size <= 64 * 1024 * 1024, 'expanded size outside format bound')
    decoder = zlib.decompressobj()
    body = decoder.decompress(data[24:], size + 1)
    require(decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail and len(body) == size,
            'compressed artifact is not one exact bounded zlib stream')
    require(len(body) >= 20 and body[:4] == b'IPRP' and not struct.unpack_from('<I', body, 8)[0] & 2,
            'invalid expanded artifact')
    return body


def generate(generator, binary, directory, seeds, timeout):
    directory.mkdir(parents=True, exist_ok=True)
    outcomes = []
    for seed in seeds:
        png = directory / f'seed-{seed}.png'
        result = command([generator, binary, '--seed', seed, '--float-bits', '-o', png], timeout)
        outcome = {'seed': seed, 'status': result.returncode, 'stderr': result.stderr}
        if result.returncode:
            require(not png.exists(), 'failed generation published a PNG: ' + str(binary))
        else:
            value = json.loads(result.stdout)
            width, height, rgba = png_rgba(png.read_bytes())
            require(value['size'] == {'x': width, 'y': height}, 'JSON/PNG dimensions differ')
            output_file = directory / f'seed-{seed}.json'
            output_file.write_text(result.stdout, encoding='utf-8')
            outcome.update({'png': file_record(png), 'json': file_record(output_file),
                            'rgba_sha256': sha(rgba), 'outputs_sha256': sha(encoded(value['outputs'])),
                            'size': value['size'], 'inputs': value['inputs'], 'fuel': value['fuel']})
        outcomes.append(outcome)
    return outcomes


def compile_case(compiler, generator, corpus, index, source, source_root, expected, old, work, seeds, timeout):
    released = corpus == 'released_sources'
    row = {'source': source, 'source_sha256': sha((source_root / source).read_bytes()),
           'historical_acceptance_transition': TRANSITIONS.get(source) if released else None,
           'semantic_change': SEMANTIC_CHANGES.get(source) if released else None, 'semantic_differences': [],
           'artifacts': {}, 'errors': [], 'container_growth': [], 'historical_size_deltas': []}
    directory = work / corpus / f'{index:03d}'
    for mode in MODES:
        for container in CONTAINERS:
            for optimization in OPTIMIZATIONS:
                key = '/'.join((mode, container, optimization))
                target = directory / key / 'asset.iprop'
                target.parent.mkdir(parents=True, exist_ok=True)
                flags = ['--debug' if mode == 'debug' else '--strip',
                         '--no-compress' if container == 'plain' else '--compress']
                if optimization == 'reference':
                    flags.append('--no-opt')
                argv = [compiler, source_root / source, '-o', target, *flags]
                result = command(argv, timeout)
                artifact = {'command': [str(a) for a in argv], 'status': result.returncode,
                            'stderr': result.stderr}
                row['artifacts'][key] = artifact
                rejection = expected.get('expect') == 'compile-error'
                if rejection:
                    if result.returncode == 0 or expected['message'] not in result.stderr or target.exists():
                        row['errors'].append(key + ': expected compile rejection ' + expected['message'])
                    continue
                if result.returncode:
                    row['errors'].append(key + ': unexpected compile rejection')
                    require(not target.exists(), 'failed compile published an artifact')
                    continue
                artifact.update(file_record(target))
                body = expanded(target.read_bytes())
                artifact.update({'expanded_bytes': len(body), 'expanded_sha256': sha(body),
                                 'container_is_compressed': bool(struct.unpack_from('<I', target.read_bytes(), 8)[0] & 2),
                                 'generations': generate(generator, target, target.parent, seeds, timeout)})
                for actual in artifact['generations']:
                    if expected.get('expect') == 'pass' and actual['status']:
                        row['errors'].append(key + ': expected successful generation')
                    if expected.get('expect') == 'trap' and (not actual['status'] or expected['message'] not in actual['stderr']):
                        row['errors'].append(key + ': expected runtime trap ' + expected['message'])
                if corpus == 'released_sources' and old['modes'][mode][optimization]['status'] == 0:
                    before = old['modes'][mode][optimization]
                    for actual, frozen in zip(artifact['generations'], before['generations']):
                        if comparable(actual) != comparable(frozen):
                            difference = key + f": differs from released semantics at seed {actual['seed']}"
                            row['semantic_differences' if row['semantic_change'] else 'errors'].append(difference)
                    if container == 'plain':
                        row['historical_size_deltas'].append({'mode': mode, 'optimization': optimization,
                            'released_bytes': before['bytes'], 'current_bytes': artifact['bytes'],
                            'delta': artifact['bytes'] - before['bytes']})
            a, b = (row['artifacts']['/'.join((mode, container, name))] for name in OPTIMIZATIONS)
            if a['status'] == b['status'] == 0 and not rejection:
                if b['expanded_bytes'] > a['expanded_bytes']:
                    row['errors'].append(mode + '/' + container + ': expanded optimized artifact grew')
                if b['bytes'] > a['bytes']:
                    row['container_growth'].append({'mode': mode, 'container': container,
                                                    'reference_bytes': a['bytes'], 'optimized_bytes': b['bytes']})
                for first, second in zip(a['generations'], b['generations']):
                    if comparable(first) != comparable(second):
                        row['errors'].append(mode + '/' + container + ': optimized/reference execution differs')
        for name in OPTIMIZATIONS:
            plain, packed = (row['artifacts']['/'.join((mode, c, name))] for c in CONTAINERS)
            if plain['status'] == packed['status'] == 0 and not rejection:
                if plain['sha256'] != packed['expanded_sha256']:
                    row['errors'].append(mode + '/' + name + ': compression changed expanded bytes')
                if packed['bytes'] > plain['bytes']:
                    row['errors'].append(mode + '/' + name + ': automatic compression grew the container')
    if row['semantic_change'] and not row['semantic_differences']:
        row['errors'].append('registered semantic change no longer differs from the released result')
    # Every successful configuration must preserve images, inputs and output bits.
    successful = [a for a in row['artifacts'].values() if a['status'] == 0 and 'generations' in a]
    if successful:
        canonical = [comparable(o) for o in successful[0]['generations']]
        for artifact in successful[1:]:
            if [comparable(o) for o in artifact['generations']] != canonical:
                row['errors'].append('debug/strip/compression mode changed execution')
    return row


def frozen_runtime(manifest, frozen, generator, work, timeout):
    records, errors = [], []
    for index, row in enumerate(manifest['cases']):
        for mode in MODES:
            for optimization in OPTIMIZATIONS:
                artifact = row['modes'][mode][optimization]
                if artifact['status']:
                    continue
                binary = frozen / artifact['binary']
                require(sha(binary.read_bytes()) == artifact['sha256'], 'frozen binary changed')
                outcomes = generate(generator, binary, work / 'frozen-runtime' / str(index) / mode / optimization,
                                    manifest['capture']['seeds'], timeout)
                for actual, expected in zip(outcomes, artifact['generations']):
                    if comparable(actual) != comparable(expected) or (not actual['status'] and actual['fuel'] != expected['fuel']):
                        errors.append(row['source'] + '/' + mode + '/' + optimization + ': current runtime differs from frozen result')
                records.append({'source': row['source'], 'mode': mode, 'optimization': optimization,
                                'binary': file_record(binary), 'generations': outcomes})
        if (index + 1) % 20 == 0:
            print('frozen binaries executed:', index + 1, flush=True)
    probe = frozen / 'rng-state.iprop'
    outcomes = generate(generator, probe, work / 'rng', manifest['capture']['seeds'], timeout)
    for actual, expected in zip(outcomes, manifest['rng_probe']['generations']):
        require(comparable(actual) == comparable(expected), 'raw RNG output bits changed')
        values = json.loads(Path(actual['json']['path']).read_bytes())['outputs']
        states = {}
        for slot in values:
            point = slot['values'][0]
            halves = [int(struct.unpack('<f', struct.pack('<I', point[key]))[0]) for key in ('x', 'y')]
            states[slot['name']] = halves[0] | halves[1] << 16
        require(states == expected['states_u32'], 'initial/advanced/restored RNG states changed')
        actual['states_u32'] = states
    records.append({'source': manifest['rng_probe']['source'], 'binary': file_record(probe), 'generations': outcomes})
    return {'passed': not errors, 'errors': errors, 'records': records,
            'successful_generations': sum(not o['status'] for r in records for o in r['generations']),
            'traps': sum(bool(o['status']) for r in records for o in r['generations'])}


def summarize(rows, manifest):
    old_by_source = {row['source']: row for row in manifest['cases']}
    summaries = {}
    for corpus, records in rows.items():
        artifacts = [a for row in records for a in row['artifacts'].values()]
        outcomes = [o for a in artifacts for o in a.get('generations', [])]
        if corpus == 'current_sources':
            for row in records:
                old = old_by_source.get(row['source'])
                if not old:
                    continue
                row['historical_source_same_bytes'] = row.get('source_sha256') == old['sha256']
                for mode in MODES:
                    for name in OPTIMIZATIONS:
                        before, now = old['modes'][mode][name], row['artifacts'].get(mode + '/plain/' + name)
                        if now and before['status'] == now['status'] == 0 and 'bytes' in now:
                            row.setdefault('historical_size_deltas', []).append({'mode': mode, 'optimization': name,
                                'released_bytes': before['bytes'], 'current_bytes': now['bytes'],
                                'delta': now['bytes'] - before['bytes'],
                                'same_source_bytes': row['historical_source_same_bytes']})
        totals = {}
        for mode in MODES:
            for container in CONTAINERS:
                for name in OPTIMIZATIONS:
                    key = '/'.join((mode, container, name))
                    values = [row['artifacts'][key] for row in records if key in row['artifacts'] and 'bytes' in row['artifacts'][key]]
                    totals[key] = {'bytes': sum(a['bytes'] for a in values),
                                   'expanded_bytes': sum(a['expanded_bytes'] for a in values)}
        changes = [d for row in records for d in row.get('historical_size_deltas', [])]
        summaries[corpus] = {'sources': len(records), 'compile_attempts': len(artifacts),
            'compile_rejections': sum(a['status'] != 0 for a in artifacts),
            'successful_generations': sum(not o['status'] for o in outcomes),
            'traps': sum(bool(o['status']) for o in outcomes), 'artifact_bytes': totals,
            'semantic_changes': {row['source']: {'reason': row['semantic_change'],
                                                 'differing_results': len(row['semantic_differences'])}
                                 for row in records if row.get('semantic_change')},
            'expanded_optimized_no_growth_passed': not any('expanded optimized artifact grew' in e for row in records for e in row['errors']),
            'compressed_optimized_larger_pairs': sum(len(row.get('container_growth', [])) for row in records),
            'historical_size': {'matched_plain_artifacts': len(changes),
                'released_bytes': sum(d['released_bytes'] for d in changes),
                'current_bytes': sum(d['current_bytes'] for d in changes),
                'delta_bytes': sum(d['delta'] for d in changes),
                'grown_artifacts': sum(d['delta'] > 0 for d in changes),
                'shrunk_artifacts': sum(d['delta'] < 0 for d in changes),
                'new_source_paths_without_historical_size': [row['source'] for row in records if row['source'] not in old_by_source]}}
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tools', type=Path)
    parser.add_argument('--frozen-dir', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, default=ROOT / 'benchmarks/baselines/v0.4.0/manifest.json')
    parser.add_argument('--work-dir', type=Path, required=True, help='new directory; frozen evidence is never modified')
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--timeout', type=int, default=30, help='seconds per compiler/generator operation')
    args = parser.parse_args()
    require(1 <= args.workers <= 8 and args.timeout > 0, 'invalid bounded worker/timeout setting')
    work, frozen = args.work_dir.resolve(), args.frozen_dir.resolve()
    require(not work.is_relative_to(frozen) and not work.is_relative_to(ROOT), 'work directory must be outside source and frozen evidence')
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    require(manifest['release']['commit'] == RELEASE and manifest['capture']['seeds'] == [0, 1, 2, 7, 26], 'wrong frozen identity/scope')
    tools = args.tools.resolve()
    suffix = '.exe' if (tools / 'ipc.exe').is_file() else ''
    tool_paths = {name: tools / (name + suffix) for name in ('ipc', 'ipgen', 'ipa')}
    identities = {name: file_record(p) for name, p in tool_paths.items()}
    described = command([tool_paths['ipc'], '--describe-language'], args.timeout)
    require(described.returncode == 0 and described_versions(json.loads(described.stdout)) == versions(ROOT), 'compiler/source version mismatch')
    current_files = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard',
                                             '--', '*.ips'], cwd=ROOT, text=True).splitlines()
    require(current_files, 'empty current source corpus')
    source_snapshot = source_identity(current_files)
    work.mkdir(parents=True, exist_ok=False)
    old_root = work / 'released-source'
    old_root.mkdir()
    for name, value in manifest['source_files'].items():
        if not name.endswith('.ips'):
            continue
        data = subprocess.check_output(['git', 'show', value['git_blob']], cwd=ROOT)
        require(sha(data) == value['sha256'], 'released Git blob does not match manifest: ' + name)
        target = old_root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    current_cases = json.loads((ROOT / 'examples/language/cases.json').read_bytes())['cases']
    expectations = {'examples/language/' + c['file']: c for c in current_cases}
    jobs = []
    for index, old in enumerate(manifest['cases']):
        source = old['source']
        expected = expectations.get(source, {})
        if source in TRANSITIONS:
            expected = {'expect': 'pass'}
        elif old['modes']['debug']['reference']['status']:
            require(expected.get('expect') == 'compile-error', 'historical rejection lacks an explicit current expectation: ' + source)
        jobs.append(('released_sources', index, source, old_root, expected, old))
    jobs += [('current_sources', i, name, ROOT, expectations.get(name, {}), None) for i, name in enumerate(sorted(current_files))]
    began = time.monotonic()
    preserved = frozen_runtime(manifest, frozen, tool_paths['ipgen'], work, args.timeout)
    rows = {'released_sources': [], 'current_sources': []}
    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {executor.submit(compile_case, tool_paths['ipc'], tool_paths['ipgen'], *job,
                                  work, manifest['capture']['seeds'], args.timeout): job for job in jobs}
        for count, future in enumerate(as_completed(futures), 1):
            job = futures[future]
            try:
                row = future.result()
            except Exception as error:
                row = {'source': job[2], 'errors': [str(error)], 'artifacts': {}}
            rows[job[0]].append(row)
            if count % 20 == 0:
                print('matrix sources completed:', count, '/', len(jobs), flush=True)
    for values in rows.values():
        values.sort(key=lambda row: row['source'])
    summaries = summarize(rows, manifest)
    errors = preserved['errors'] + [f"{corpus}/{r['source']}: {e}" for corpus, values in rows.items() for r in values for e in r['errors']]
    unchanged_tools = all(file_record(p) == identities[name] for name, p in tool_paths.items())
    require(unchanged_tools and args.manifest.read_bytes() == manifest_bytes and source_identity(current_files) == source_snapshot,
            'tools/source inputs/frozen manifest changed during run')
    result = {'schema_version': 1, 'passed': not errors, 'errors': errors,
        'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'source_identity': source_snapshot,
        'versions': versions(ROOT), 'tools': identities, 'harness_sha256': sha(Path(__file__).read_bytes()),
        'frozen_manifest': file_record(args.manifest.resolve()), 'frozen_release': RELEASE,
        'scope': {'seeds': manifest['capture']['seeds'], 'variants': [0], 'archetypes': [0], 'size': 'asset default',
                  'optimizations': list(OPTIMIZATIONS), 'debug_modes': list(MODES), 'containers': list(CONTAINERS),
                  'optimized_size_gate': 'expanded bytes; compressed entropy deltas reported separately',
                  'historical_size_gate': False, 'workers': args.workers, 'per_command_timeout_seconds': args.timeout},
        'frozen_runtime': preserved, 'corpora': rows, 'summary': summaries, 'elapsed_seconds': time.monotonic() - began,
        'platform': platform.platform(), 'limitations': ['Default variant/archetype/size only; wider controls are covered by separate conformance.',
            'C API/SFML/Godot require separate pinned backend reports.', 'Historical compiler rejections are retained and intentional language acceptance repairs are recorded.',
            'Registered semantic changes of released sources are recorded per differing result, not compared.']}
    (work / 'report.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print('Runtime matrix:', 'PASS' if result['passed'] else 'FAIL', 'report:', work / 'report.json')
    for error in errors[:20]:
        print(error, file=sys.stderr)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print('Runtime matrix: ' + str(error), file=sys.stderr)
        sys.exit(1)
