"""Exercise bounded selection, executable repairs and generated-file ownership."""
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import unquote, urlsplit

import language_docs as docs
import llm_reference as llm
from release_common import VERSION_SOURCES


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def snapshot(directory):
    return {p.relative_to(directory).as_posix(): p.read_bytes()
            for p in directory.rglob('*') if p.is_file()}


def main():
    compiler = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[1]
    language = docs.contracts(compiler, root)
    _, _, authored, cases = docs.sources(root, language)
    config = json.loads((root / llm.CONFIG).read_text(encoding='utf-8'))
    with tempfile.TemporaryDirectory(prefix='iprop-llm-check-') as temporary:
        directory = Path(temporary).resolve()
        output = directory / 'book'
        info = docs.build(compiler, output, root)
        reference = (output / 'LLM_REFERENCE.md').read_text(encoding='utf-8')
        reference_bytes = len(reference.encode('utf-8'))
        require(reference_bytes <= config['defaults']['max_bytes'], 'default reference exceeds its budget')
        require('Commit: `' + info['source_commit'] + '`' in reference, 'reference lost its source identity')
        for key, label in [('language_version', 'source language'), ('format_version', 'format'),
                           ('c_api_version', 'C API'), ('abi_version', 'ABI')]:
            version = language[key]
            require(f"{label} {version['major']}.{version['minor']}" in reference, 'independent version missing: ' + key)
        require('Product ' + language['product_version'] in reference, 'product version missing')
        checks = info['llm_reference_checks']
        examples = {name for entry in config['sections'] if entry['id'] in config['defaults']['sections']
                    for name in entry.get('examples', []) + entry.get('demos', [])}
        pairs = len(config['diagnostic_pairs']) + len(config['style_pairs'])
        require(len([c for c in checks if c.get('execution') == 'pass']) == len(examples), 'selected examples were not executed')
        require(len([c for c in checks if c.get('correction_execution') == 'pass']) == pairs, 'repair pairs were not executed')
        require(reference.count('```ips') == len(checks) + pairs, 'unregistered/unexecuted code block in reference')
        # The complete reference: the idioms first, every builtin once.
        require(reference.index('## Write idiomatic props') < reference.index('## Before and after') <
                reference.index('## Compiler rules') < reference.index('## Assets, inputs and outputs'), 'idioms do not lead')
        require('llm: skip' not in reference, 'a skip marker leaked into the reference')
        for builtin in language['builtins']:
            require('`' + builtin['name'] + '(' in reference, 'builtin missing from the reference: ' + builtin['name'])
        for method in language['source_rules']['samplers']['methods']:
            args = ', '.join(parameter['name'] + ': ' + parameter['type'] for parameter in method['parameters'])
            require(f"| {method['receiver']} | .{method['name']}({args}) | {method['result']} |" in reference,
                    'sampler method missing from the reference')
        require('<!-- sampler-methods -->' not in reference, 'method placeholder leaked into reference')
        for method in language['source_rules']['surfaces']['methods']:
            require(f"| {method['receiver']} | .blur(sigma: f32) | {method['result']} |" in reference,
                    'surface method missing from the reference')
        require('<!-- surface-methods -->' not in reference, 'surface method placeholder leaked into reference')
        # All artifact links must resolve inside the installed documentation tree.
        for target in re.findall(r'\[[^\]\n]+\]\(([^\s)]+)\)', reference):
            url = urlsplit(target)
            require(not url.scheme and not url.netloc, 'reference requires an online source')
            path = (output / unquote(url.path)).resolve()
            require(path.is_relative_to(output) and path.is_file(), 'missing offline link: ' + target)
            if url.fragment:
                require(url.fragment in {h['anchor'] for h in docs.headings(path.read_text(encoding='utf-8'))},
                        'missing offline anchor: ' + target)
        docs.verify(output, language['product_version'], info['source_commit'], root, compiler)
        original = snapshot(output)
        # Real command-line selections, including an overload and a dotted name.
        command = [sys.executable, str(root / 'scripts/language_docs.py'), 'build', '--compiler', str(compiler),
                   '--output-dir', str(directory / 'selected'), '--llm-sections', 'symbols,asset',
                   '--llm-symbols', 'color.hsv,pick', '--llm-max-bytes', '8192']
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        require(result.returncode == 0, 'selection CLI failed: ' + result.stderr)
        selected = (directory / 'selected/LLM_REFERENCE.md').read_text(encoding='utf-8')
        require(selected.index('## Selected symbols') < selected.index('## Minimal asset'), 'section order changed')
        require('`color.hsv(' in selected and '`pick(' in selected and '`rect(' not in selected,
                'symbol selection did not use current exported names')
        require('## Rejection and repair' not in selected and '## Part' not in selected, 'unselected content leaked')
        require(len(selected.encode('utf-8')) <= 8192, 'selected budget failed')
        # Every current builtin is selectable without a parallel symbol list.
        signatures = 0
        for builtin in language['builtins']:
            text, evidence = llm.render(root, language, authored, cases, info, compiler,
                                        sections='symbols', symbols=builtin['name'])
            count = len(builtin['overloads']) + ('variadic' in builtin)
            require(text.count('`' + builtin['name'] + '(') == count and not evidence,
                    'exported signatures missing from selected reference: ' + builtin['name'])
            signatures += count
        # Errors must fail closed before replacing a previously generated tree.
        failures = [({'llm_sections': 'asset,asset'}, 'duplicate section'),
                    ({'llm_sections': 'future'}, 'unknown section'),
                    ({'llm_symbols': 'pixel,pixel'}, 'duplicate symbol'),
                    ({'llm_symbols': 'future'}, 'unknown symbol'),
                    ({'llm_max_bytes': 1}, 'exceeds byte budget'),
                    ({'llm_max_bytes': config['maximum_bytes'] + 1}, 'byte budget must be')]
        for options, expected in failures:
            try:
                docs.build(compiler, output, root, **options)
                raise RuntimeError('invalid selection accepted: ' + str(options))
            except RuntimeError as error:
                require(expected in str(error), 'unexpected selection failure: ' + str(error))
            require(snapshot(output) == original, 'failed build changed existing documentation')
        minimal, evidence = llm.render(root, language, authored, cases, info, compiler, sections='', symbols='')
        require('## Compiler rules' in minimal and '## Minimal asset' not in minimal and not evidence,
                'explicit empty selection is not respected')
        # Mutate only a private copy of the authored inputs. A changed compiler
        # diagnostic or invalid repair must never overwrite a known good book.
        copied = directory / 'source'
        for folder in ['docs', 'spec']:
            shutil.copytree(root / folder, copied / folder)
        for folder in ['language', 'recipes']:
            shutil.copytree(root / 'examples' / folder, copied / 'examples' / folder)
        for name, _ in VERSION_SOURCES.values():
            (copied / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / name, copied / name)
        for resource in docs.resources(root):
            target = copied / resource.relative_to(root)
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(resource, target)
        source_before = snapshot(root / 'examples/language')
        broken = json.loads(json.dumps(config))
        broken['diagnostic_pairs'][0]['diagnostic_code'] = 'IPS-TYPE-999'
        broken_cases = [(broken, 'stable diagnostic changed')]
        broken = json.loads(json.dumps(config))
        broken['diagnostic_pairs'][0]['replacements'][0]['after'] = 'x = true;'
        broken_cases.append((broken, 'unexpected compiler acceptance'))
        broken = json.loads(json.dumps(config))
        next(entry for entry in broken['sections'] if isinstance(entry['parts'], list) and
             entry['id'] in config['defaults']['sections'])['parts'][0]['heading'] = 'Stale handbook heading'
        broken_cases.append((broken, 'missing or ambiguous handbook heading'))
        for broken, expected in broken_cases:
            (copied / llm.CONFIG).write_text(json.dumps(broken), encoding='utf-8')
            try:
                docs.build(compiler, output, copied)
                raise RuntimeError('stale authored selection accepted')
            except RuntimeError as error:
                require(expected in str(error), 'unexpected authored failure: ' + str(error))
            require(snapshot(output) == original, 'invalid correction changed existing documentation')
        require(snapshot(root / 'examples/language') == source_before, 'acceptance checks mutated registered sources')
        # The existing manifest owns the generated reference as well as inputs.
        (output / 'LLM_REFERENCE.md').write_text(reference + '\nchanged', encoding='utf-8')
        try:
            docs.verify(output)
            raise RuntimeError('tampered reference accepted')
        except RuntimeError as error:
            require('Documentation file changed: LLM_REFERENCE.md' in str(error), 'tampered reference escaped ownership')
        print(f'LLM reference: PASS {reference_bytes} bytes, {len(examples)} executable forms, {pairs} checked before/after pairs, '
              f'{len(language["builtins"])} selectable builtins/{signatures} signatures')


if __name__ == '__main__':
    main()
