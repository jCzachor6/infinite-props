"""Render a bounded reference from handbook sections and compiler contracts."""
import json
from contract_readers import parse_json, read_diagnostics
from markdown_tokens import code_lines, headings, rewrite_links
import re
import subprocess
import tempfile
from pathlib import Path
from urllib.parse import unquote, urlsplit


CONFIG = 'docs/llm-reference.json'


def require(condition, message):
    if not condition:
        raise RuntimeError('LLM reference: ' + message)


def select(value, default, known, label):
    selected = default if value is None else [item.strip() for item in value.split(',') if item.strip()]
    require(len(selected) == len(set(selected)), 'duplicate ' + label)
    require(set(selected) <= set(known), 'unknown ' + label + ': ' + ', '.join(sorted(set(selected) - set(known))))
    return selected


def excerpt(root, chapter, text, heading, paragraphs, skip=0):
    found = headings(text)
    matches = [h for h in found if h['title'] == heading]
    require(len(matches) == 1, 'missing or ambiguous handbook heading: ' + heading)
    index = found.index(matches[0])
    lines = text.split('\n')
    begin = matches[0]['line'] + 1
    end = found[index + 1]['line'] if index + 1 < len(found) else len(lines)
    # Code blocks become blank lines, which still separate the prose around them.
    code = code_lines(text)
    body = '\n'.join('' if number in code else line for number, line in enumerate(lines[begin:end], begin))
    body = re.sub(r'^<!--.*?-->\s*$', '', body, flags=re.M)
    blocks = [block.strip() for block in re.split(r'\n\s*\n', body) if block.strip()]
    require(type(paragraphs) is int and type(skip) is int and skip >= 0 and
            0 < paragraphs and skip + paragraphs <= len(blocks), 'invalid paragraph selection for ' + heading)

    def link(destination):
        url = urlsplit(destination)
        if url.scheme or url.netloc:
            return None
        target = (chapter.parent / unquote(url.path)).resolve() if url.path else chapter
        require(target.is_relative_to(root) and target.is_file(), 'missing handbook link: ' + destination)
        return target.relative_to(root).as_posix() + ('#' + url.fragment if url.fragment else '')

    body = rewrite_links('\n\n'.join(blocks[skip:skip + paragraphs]), link)
    return body, chapter.relative_to(root).as_posix() + '#' + matches[0]['anchor']


def correction(pair, original):
    result = original
    for edit in pair['replacements']:
        before, after = edit['before'], edit['after']
        require(before and result.count(before) == 1, 'correction must match once: ' + pair['id'])
        result = result.replace(before, after, 1)
    require(result != original, 'empty correction: ' + pair['id'])
    return result


def check_program(compiler, source, accepted):
    process = subprocess.run([str(compiler), str(source), '--check', '--diagnostics=json'],
                             capture_output=True, text=True, encoding='utf-8', timeout=60)
    require(not process.stderr, 'check wrote non-JSON errors: ' + str(source))
    report = read_diagnostics(parse_json(process.stdout), process.returncode)
    require(process.returncode == (0 if accepted else 1) and
            report['status'] == ('ok' if accepted else 'invalid_source'),
            'unexpected compiler acceptance: ' + str(source) + '\n' + process.stdout)
    require(bool(report['diagnostics']) != accepted, 'unexpected diagnostics: ' + str(source))
    return report


def execute(compiler, source, directory, name):
    binary, image = directory / (name + '.iprop'), directory / (name + '.png')
    compiled = subprocess.run([str(compiler), str(source), '-o', str(binary)], capture_output=True,
                              text=True, encoding='utf-8', timeout=60)
    require(compiled.returncode == 0 and binary.is_file(), 'example compilation failed: ' + str(source) + '\n' + compiled.stderr)
    generator = compiler.parent / ('ipgen.exe' if compiler.suffix == '.exe' else 'ipgen')
    generated = subprocess.run([str(generator), str(binary), '--seed', '7', '-o', str(image)],
                               capture_output=True, text=True, encoding='utf-8', timeout=60)
    require(generated.returncode == 0 and image.is_file(), 'example execution failed: ' + str(source) + '\n' + generated.stderr)


def render(root, language, authored, cases, identity, compiler, sections=None, symbols=None, max_bytes=None):
    root = root.resolve()
    config = json.loads((root / CONFIG).read_text(encoding='utf-8'))
    entries = {entry['id']: entry for entry in config['sections']}
    require(len(entries) == len(config['sections']) and not {'symbols', 'diagnostics'} & entries.keys(), 'duplicate/reserved section id')
    builtins = {builtin['name']: builtin for builtin in language['builtins']}
    sections = select(sections, config['defaults']['sections'], [*entries, 'symbols', 'diagnostics'], 'section')
    symbols = select(symbols, config['defaults']['symbols'], builtins, 'symbol')
    budget = config['defaults']['max_bytes'] if max_bytes is None else max_bytes
    require(type(budget) is int and 0 < budget <= config['maximum_bytes'], 'byte budget must be in [1, ' + str(config['maximum_bytes']) + ']')
    from release_common import handbook_version
    minor = handbook_version(language)

    def version(key):
        value = language[key]
        return str(int(value['major'])) + '.' + str(int(value['minor']))

    lines = ['# .ips LLM reference', '',
        'Generated from the handbook, compiler export and registered examples. Do not edit this artifact.', '',
        f"Product {language['product_version']} · source language {version('language_version')} · format {version('format_version')} · C API {version('c_api_version')} · ABI {version('abi_version')}",
        f"Commit: `{identity['source_commit'] or 'source archive'}` · {identity['status']} · dirty: {str(identity['working_tree_dirty']).lower()}",
        f"Budget: {budget} UTF-8 bytes (no truncation). Sections: {', '.join(sections) or 'none'}. Symbols: {', '.join(symbols) if 'symbols' in sections and symbols else 'none'}.", '',
        f'[Full handbook](docs/language-v{minor}.md) · [Compiler catalog](docs/language.json)', '']
    rules = language['source_rules']
    widening = ', '.join(item['from'] + '→' + item['to'] for item in rules['types']['implicit_widening']) or 'none'
    unavailable = ', '.join(key.replace('_', ' ') for key, allowed in rules['restrictions'].items() if not allowed)
    lines += ['## Compiler rules', '',
        'Storable types: ' + ', '.join(rules['types']['storable']) +
        '; helper parameter types: ' + ', '.join(rules['types']['helper_parameters']) +
        '. Output-only geometry: ' + ', '.join(rules['types']['output_only_geometry']) + '.',
        f"Implicit widening: {widening}; local reassignment: {rules['types']['local_reassignment']}.",
        f"Locals: {rules['locals']['declaration']}, {rules['locals']['scope']} scope, {rules['locals']['unexecuted_initializer']} if skipped; definite-assignment analysis: {str(rules['locals']['definite_assignment_analysis']).lower()}.",
        f"Uniform visibility: {rules['visibility']['uniforms']}; read-only: {str(rules['visibility']['uniforms_read_only']).lower()}.",
        f"Helper argument order: {language['call_rules']['helper_evaluation_order']}. Named arguments change execution order: {str(rules['effect_order']['named_arguments_change_evaluation_order']).lower()}.",
        'Unavailable: ' + unavailable + '.', '']
    example_paths, repairs = [], []

    def example(path):
        require(path in cases and cases[path]['expect'] == 'pass', 'selected example must be registered as pass: ' + str(path))
        example_paths.append(path)
        return ['[' + path.name + '](' + path.relative_to(root).as_posix() + ') — executed with seed 7.', '',
                '```ips', path.read_text(encoding='utf-8').rstrip(), '```', '']

    for selected in sections:
        if selected == 'symbols':
            lines += ['## Selected symbols', '']
            for name in symbols:
                builtin = builtins[name]
                for signature in [*builtin['overloads'], *([builtin['variadic']] if 'variadic' in builtin else [])]:
                    params = ', '.join(p['name'] + ': ' + p['type'] for p in signature['parameters'])
                    if 'minimum_arguments' in signature:
                        params += ', ...'
                    effects = signature['effects']
                    lines += [f"`{name}({params}) -> {signature['result']}`",
                        'Order: ' + (', '.join(signature['evaluation_order']) or 'no arguments') +
                        f"; RNG: {effects['rng']}; canvas/output writes: {str(effects['writes_canvas']).lower()}/{str(effects['writes_outputs']).lower()}; intrinsic traps: {', '.join(effects['may_trap']) or 'none'}."]
                    if 'minimum_arguments' in signature:
                        lines += [f"Minimum arguments: {int(signature['minimum_arguments'])}; repeat: {signature['repeat']}." +
                                  (' Choices: ' + signature['choice_evaluation'] + '.' if 'choice_evaluation' in signature else '')]
                lines.append('')
            lines += ['Argument effects and execution-wide resource failures remain separate from intrinsic summaries.', '']
        elif selected == 'diagnostics':
            lines += ['## Rejection and repair', '']
            for pair in config['diagnostic_pairs']:
                path = (root / 'examples/language' / pair['file']).resolve()
                require(path in cases and cases[path]['expect'] == 'compile-error', 'repair source must be a registered rejection')
                wrong = path.read_text(encoding='utf-8')
                fixed = correction(pair, wrong)
                repairs.append((pair, path, fixed))
                lines += ['### ' + pair['title'], '', '[' + path.name + '](' + path.relative_to(root).as_posix() + ')', '',
                    'Wrong:', '```ips', wrong.rstrip(), '```',
                    f"Diagnostic: `{pair['diagnostic_code']}` — `{cases[path]['message']}`.", '',
                    pair['explanation'], 'Corrected:', '```ips', fixed.rstrip(), '```', '']
        else:
            entry = entries[selected]
            chapter = (root / 'docs/language' / entry['chapter']).resolve()
            require(chapter in authored, 'section must come from the registered handbook')
            body, link = excerpt(root, chapter, authored[chapter], entry['heading'], entry['paragraphs'],
                                 entry.get('skip_paragraphs', 0))
            lines += ['## ' + entry['title'], '', '[Handbook section](' + link + ')', '', body, '']
            for name in entry.get('examples', []):
                lines += example((root / 'examples/language' / name).resolve())
            if selected == 'part':
                part = language['call_rules']['part']
                params = [p['name'] + (' positional' if p['positional'] else ' named-only') +
                          (' required' if p['required'] else ' default=' + str(p['default'])) for p in part['parameters']]
                lines += ['Part slots: ' + '; '.join(params) + '.',
                          'Part evaluation order: ' + ', '.join(part['evaluation_order']) + '.', '']
    result = '\n'.join(lines).rstrip() + '\n'
    require(len(result.encode('utf-8')) <= budget,
            f'generated reference exceeds byte budget ({len(result.encode("utf-8"))} > {budget}); select fewer sections/symbols')

    # Validate before output replacement. Corrections retain the example layout
    # so current and future registered imports resolve from an owning snapshot.
    evidence = []
    with tempfile.TemporaryDirectory(prefix='iprop-llm-reference-') as temporary:
        directory = Path(temporary)
        for path in cases:
            target = directory / path.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
        for number, path in enumerate(dict.fromkeys(example_paths)):
            owning_copy = directory / path.relative_to(root)
            check_program(compiler, owning_copy, True)
            execute(compiler, owning_copy, directory, 'example-' + str(number))
            evidence.append({'file': path.relative_to(root).as_posix(), 'check': 'pass', 'execution': 'pass'})
        for number, (pair, path, fixed) in enumerate(repairs):
            owning_copy = directory / path.relative_to(root)
            report = check_program(compiler, owning_copy, False)
            require(any(d['code'] == pair['diagnostic_code'] and cases[path]['message'] in d['message']
                        for d in report['diagnostics']), 'stable diagnostic changed: ' + pair['id'])
            owning_copy.write_text(fixed, encoding='utf-8')
            check_program(compiler, owning_copy, True)
            execute(compiler, owning_copy, directory, 'repair-' + str(number))
            evidence.append({'file': path.relative_to(root).as_posix(), 'diagnostic_code': pair['diagnostic_code'],
                             'rejection': 'pass', 'correction_check': 'pass', 'correction_execution': 'pass'})
    return result, evidence
