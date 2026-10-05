#!/usr/bin/env python3
"""Complete archival identity from existing exact context, without rerendering.

Per-task source, process, diagnostic, PNG/RGBA and render records are untouched.
The earlier derived run.json is retained before its context fields are completed.
"""
import argparse
import hashlib
import json
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--reference', type=Path, action='append', default=[])
    parser.add_argument('--language-export', type=Path)
    parser.add_argument('--prompt', type=Path)
    parser.add_argument('--request', type=Path)
    args = parser.parse_args()
    root = args.run.resolve()
    data = (root / 'run.json').read_bytes()
    record = json.loads(data)
    original = root / 'run-before-context.json'
    if not original.exists(): original.write_bytes(data)
    captured = {'source_run_metadata_sha256': sha(data), 'references': []}
    for index, source in enumerate(args.reference):
        raw = source.read_bytes()
        destination = root / f'reference-{index}.bin'
        if destination.exists() and destination.read_bytes() != raw:
            raise ValueError('refusing to replace a different archived reference')
        destination.write_bytes(raw)
        captured['references'].append({'source': str(source.resolve()), 'saved': str(destination), 'sha256': sha(raw)})
    if captured['references']: record['reference_artifacts'] = captured['references']
    for field in ('language_export', 'prompt', 'request'):
        source = getattr(args, field)
        if not source: continue
        raw = source.read_bytes()
        destination = root / (field + '.bin')
        if destination.exists() and destination.read_bytes() != raw:
            raise ValueError('refusing to replace a different archived ' + field)
        destination.write_bytes(raw)
        captured[field] = {'source': str(source.resolve()), 'saved': str(destination), 'sha256': sha(raw)}
        record[field] = captured[field]
    if args.request:
        request = json.loads(args.request.read_bytes())
        if (record.get('prompt') or {}).get('sha256') != request.get('prompt_sha256'):
            raise ValueError('exact prompt hash differs from saved request')
        if record['task_manifest_sha256'] != request['task_manifest_sha256']:
            raise ValueError('task definitions differ from saved request')
        record['batch_id'] = request.get('batch_id')
        record['prompt_roles'] = request.get('roles')
    captured['policy'] = 'Existing exact context copied; no raw task/render/source records modified or measurements regenerated.'
    save(root / 'captured-context.json', captured)
    save(root / 'run.json', record)
    print(json.dumps(captured, indent=2))


if __name__ == '__main__':
    main()
