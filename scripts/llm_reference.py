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


def excerpt(root, chapter, text, heading, paragraphs, skip=0, fences=(), optional=False):
    # Method signatures are already generated in compiler_rules for every
    # selection. Their handbook placeholder is not prose or a second table.
    text = text.replace('<!-- sampler-methods -->', '').replace('<!-- stepper-methods -->', '').replace('<!-- surface-methods -->', '')
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
    # A paragraph under `<!-- llm: skip -->` stays in the book only; other
    # comment lines (example and builtin directives) are dropped.
    blocks = [re.sub(r'^<!--.*?-->\s*$', '', block, flags=re.M).strip()
              for block in re.split(r'\n\s*\n', body) if not block.strip().startswith(SKIP)]
    blocks = [block for block in blocks if block]
    if paragraphs == 'all':
        paragraphs = len(blocks) - skip
        if paragraphs == 0 and optional:
            return '', chapter.relative_to(root).as_posix() + '#' + matches[0]['anchor']
    require(type(paragraphs) is int and type(skip) is int and skip >= 0 and
            0 < paragraphs and skip + paragraphs <= len(blocks), 'invalid paragraph selection for ' + heading)
    body = rewrite_links('\n\n'.join(blocks[skip:skip + paragraphs]), resolver(root, chapter))
    # Selected fenced blocks (such as the EBNF grammar) are kept verbatim.
    section = '\n'.join(lines[begin:end])
    for info, code in re.findall(r'^```(\w+)\n(.*?)^```', section, flags=re.M | re.S):
        if info in fences:
            body += '\n\n```' + info + '\n' + code.rstrip() + '\n```'
    return body, chapter.relative_to(root).as_posix() + '#' + matches[0]['anchor']


def resolver(root, chapter):
    # Handbook links become links relative to the documentation root.
    def link(destination):
        url = urlsplit(destination)
        if url.scheme or url.netloc:
            return None
        target = (chapter.parent / unquote(url.path)).resolve() if url.path else chapter
        require(target.is_relative_to(root) and target.is_file(), 'missing handbook link: ' + destination)
        return target.relative_to(root).as_posix() + ('#' + url.fragment if url.fragment else '')
    return link


BUILTIN = re.compile(r'^<!-- builtin: (\S+) -->$', re.M)
SKIP = '<!-- llm: skip -->'
RNG_LABELS = {'none': None, 'advance_current': 'draws from the stream'}


def signature_lines(name, builtin):
    lines = []
    for signature in [*builtin['overloads'], *([builtin['variadic']] if 'variadic' in builtin else [])]:
        params = ', '.join(p['name'] + ': ' + p['type'] for p in signature['parameters'])
        if 'minimum_arguments' in signature:
            params += ', ...'
        rng = signature['effects']['rng']
        label = RNG_LABELS.get(rng, rng.replace('_', ' '))
        lines.append(f"`{name}({params}) -> {signature['result']}`" + (f' ({label})' if label else ''))
    return lines


def builtin_catalog(root, language, authored, hints):
    # Every builtin once, in handbook order: its exported signatures and the
    # first paragraph of the handbook text that follows its directive.
    builtins = {builtin['name']: builtin for builtin in language['builtins']}
    require(set(hints) <= set(builtins), 'hint for an unknown builtin: ' + ', '.join(sorted(set(hints) - set(builtins))))
    lines, seen = ['## Builtins', '',
        'Every builtin of this compiler, grouped by handbook chapter: exported signatures, '
        'then the first paragraph of its handbook description.', ''], set()
    for chapter, text in authored.items():
        found = BUILTIN.findall(text)
        if not found:
            continue
        lines += ['### ' + headings(text)[0]['title'], '']
        code = code_lines(text)
        for match in BUILTIN.finditer(text):
            name = match.group(1)
            require(name in builtins and name not in seen, 'unknown or repeated builtin description: ' + name)
            seen.add(name)
            start = text.count('\n', 0, match.end()) + 1
            following = []
            for number, line in enumerate(text.split('\n')[start:], start):
                if line.startswith('#') or line.startswith('<!--'):
                    break
                following.append('' if number in code else line)
            blocks = [block.strip() for block in re.split(r'\n\s*\n', '\n'.join(following)) if block.strip()]
            require(blocks, 'missing builtin explanation: ' + name)
            lines += signature_lines(name, builtins[name])
            if name in hints:
                lines.append('Style: ' + hints[name])
            lines += [rewrite_links(blocks[0], resolver(root, chapter)), '']
    require(seen == set(builtins), 'builtins missing from the handbook: ' + ', '.join(sorted(set(builtins) - seen)))
    return lines


def phrase(value, phrases):
    # A known export value as words; an unknown one stays visible verbatim.
    return phrases.get(value, str(value).replace('_', ' '))


def compiler_rules(language):
    """The compiler's source rules in plain sentences, from --describe-language."""
    rules = language['source_rules']
    types = rules['types']
    lines = ['## Compiler rules', '',
        f"Values you can store, pass and return: {', '.join(types['storable'])}. Helper parameters take "
        f"{', '.join(types['helper_parameters'])}. {' and '.join('`' + kind + '`' for kind in types['output_only_geometry'])} exist only as output kinds."]
    widening = ', '.join(item['from'] + ' to ' + item['to'] for item in types['implicit_widening']) or 'nothing'
    lines.append(f"Only {widening} converts implicitly; {phrase(types['local_reassignment'], {'exact_type': 'a local keeps the exact type of its first assignment'})}.")
    if 'collections' in rules:
        collection = rules['collections']
        lines.append(f"`list<T>` is immutable and holds {collection['minimum_length']} to {collection['maximum_length']} elements; "
                     "empty literals need a known element type. Elements evaluate once left to right; list indices are i32 and trap "
                     "outside the length. Helpers share immutable data. `list.len` gives i32; `poly` "
                     "accepts list<vec2> and color, evaluating points first. Direct legacy palette indexing keeps its "
                     "last-element fallback; a palette passed as a list uses checked indexing and the 256-element limit.")
    if 'samplers' in rules:
        from language_docs import sampler_signatures
        lines += ['Captured samplers evaluate lower, upper and optional distribution once, without an intrinsic draw. '
                  'Copies/helpers share the immutable description. Receivers evaluate once before the count; '
                  'batch pick checks count, handle and allocation before results. Constants and empty batches consume no RNG. '
                  'Const samplers and range palettes store compile-time descriptions. Range palettes lift scalar entries '
                  'to constant descriptions; indexing selects without RNG and keeps the last-element fallback. '
                  'Pass them as list<sampler<T>> for checked indexing. Ramp accepts only constant color/f32 palettes.', '', sampler_signatures(language)]
        lines.append('')
    if 'steppers' in rules:
        from language_docs import method_signatures
        lines += ['`stepper<T>` captures the same four endpoint types as a sampler, but `.pick(n)` requires an i32 count '
                  'in 0-256 and returns deterministic list<T> quantiles without RNG. No cursor or implicit sampler conversion exists. '
                  'Uniform positions are i/(n-1), with exact endpoints; n=0 is empty and n=1 returns the first step. '
                  'Integer interpolation uses binary64 and rounds halves away from zero. Gaussian, triangular, beta, weighted and density '
                  'use the truncated distribution support, with one shared coefficient for all color RGBA channels or vec2 components. '
                  'Weighted steps may repeat. Captured operands still run eagerly in lower, upper, distribution order. '
                  'Const descriptions and lists of steppers are supported; picks require generation, distinct compares descriptions, '
                  'and sort rejects them. Empty picks still validate their handle but do not check probability mass.', '',
                  method_signatures(rules['steppers']['methods']), '']
    if 'methods' in rules.get('surfaces', {}):
        from language_docs import method_signatures
        lines += ['Layer/mask `.blur(sigma)` requires finite f32 0-64 (i32 widens), in input pixels. '
                  'Evaluate receiver then sigma once. Immutable results keep geometry/D4, never paint/use RNG. '
                  'Zero validates/shares without allocation. Otherwise: Gaussian r=ceil(3*sigma), K=2*r+1, '
                  'normalized pinnedExp weights, binary64 horizontal then vertical; repeat nearest edges. '
                  'Premultiply stored RGBA, unpremultiply after both passes, round nearest ties up, zero-alpha RGB=0. '
                  'Masks return A8 matching alpha. Clip applies on paint. Invalid dynamic sigma traps. '
                  'Fuel 1+K+W*H*(1+2*K); output and 8*W*min(H,K)*C scratch share 64 MiB (C=4 layer,1 mask). '
                  'Always release scratch; failures publish nothing.', '',
                  method_signatures(rules['surfaces']['methods']), '']
    if 'random_range' in rules:
        ranged = rules['random_range']
        lines.append('`lower to upper` evaluates the lower bound, then the upper one, then draws once: ' +
            '; '.join(f"{item['lower']} bounds give {item['result']}" for item in ranged['overloads']) +
            f"; mixed numbers {phrase(ranged['mixed_numeric_bounds'], {'widen_to_f32': 'widen to f32'})}. It binds tighter than "
            f"`{ranged['precedence']['tighter_than']}` and looser than `{ranged['precedence']['looser_than']}`" +
            ('.' if ranged['chaining'] else ' and cannot be chained.'))
        if 'ends' in ranged:
            lines.append('Between two ' + ' or two '.join(item['type'] + ' values' for item in ranged['ends']) +
                         ', `to` draws one shared position, so ' +
                         ' or '.join(', '.join(item['mixed']) for item in ranged['ends']) + ' move together.')
        if 'with' in ranged:
            shaped = ranged['with']
            lines.append(f"`lower to upper with d` draws from `d` {phrase(shaped['truncation'], {'conditioned_on_range': 'restricted to the range, never clamped'})}; "
                         f"it evaluates {', '.join(shaped['evaluation_order'])} in that order, and i32 ranges round "
                         f"{phrase(shaped['integer_rounding'], {'nearest_half_away_from_zero': 'to the nearest integer, halves away from zero'})}.")
    if 'point_distributions' in rules:
        points = rules['point_distributions']
        lines.append('Point distribution literals: ' + '; '.join(
            '`' + family['name'] + ' {key: value, ...}` (' + str(family['minimum_points']) + '–256 points)'
            for family in points['families']) + '. Numeric pairs evaluate once left to right; keys keep exact i32/f32 types, '
            'increase strictly, and values are finite/nonnegative with positive total mass. Descriptions snapshot without RNG; '
            'use `to ... with d` or capture a sampler. Weighted values are discrete masses; clipped draws consume one stream step, '
            'and missing mass traps. Density values are heights interpolated linearly, zero outside their support; '
            'draws integrate clipped trapezoids and invert the selected integral. Equal density bounds need only lie in the closed support. '
            'Color/vec2 use positions on 0–1.')
    if 'components' in types:
        lines.append('Read-only members: ' + '; '.join(item['type'] + ' ' + ', '.join('.' + name for name in item['names']) +
                     ' give ' + item['result'] for item in types['components']) + '.')
    local = rules['locals']
    lines.append(f"A local is declared by its {phrase(local['declaration'], {'first_assignment': 'first assignment'})} and is visible in the whole "
                 f"{local['scope']}; if that assignment never runs, it reads as "
                 f"{phrase(local['unexecuted_initializer'], {'typed_zero': 'zero of its type'})}" +
                 ('.' if local['definite_assignment_analysis'] else ' (there is no definite-assignment check).'))
    visibility = rules['visibility']
    lines.append(f"Inputs are visible {phrase(visibility['uniforms'], {'draw_only': 'only in `draw`'})}" +
                 (' and are read-only' if visibility['uniforms_read_only'] else '') + '; pass them to helpers as parameters.')
    named = rules['effect_order']['named_arguments_change_evaluation_order']
    lines.append(f"Helper arguments evaluate in {phrase(language['call_rules']['helper_evaluation_order'], {'declaration': 'parameter declaration order'})}" +
                 ('; naming them can change that order.' if named else '; naming them does not change that order.'))
    unavailable = ', '.join(key.replace('_', ' ') for key, allowed in rules['restrictions'].items() if not allowed)
    lines += ['Not available: ' + (unavailable or 'nothing') + '.', '']
    return lines


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
    reserved = {'symbols', 'builtins', 'style', 'diagnostics'}
    require(len(entries) == len(config['sections']) and not reserved & entries.keys(), 'duplicate/reserved section id')
    builtins = {builtin['name']: builtin for builtin in language['builtins']}
    sections = select(sections, config['defaults']['sections'], [*entries, *sorted(reserved)], 'section')
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
        'Write .ips the way the first section shows: draw values with `lower to upper`, shape them with '
        '`with` and a distribution, draw colors as gradients (`dark to light with d`), name fixed sets in '
        'palettes, and make every seed alone a different prop. The later sections cover the whole language.', '',
        f"Product {language['product_version']} · source language {version('language_version')} · format {version('format_version')} · C API {version('c_api_version')} · ABI {version('abi_version')}",
        f"Commit: `{identity['source_commit'] or 'source archive'}` · {identity['status']} · dirty: {str(identity['working_tree_dirty']).lower()}",
        f"Budget: {budget} UTF-8 bytes (no truncation). Sections: {', '.join(sections) or 'none'}. Symbols: {', '.join(symbols) if 'symbols' in sections and symbols else 'none'}.", '',
        f'[Full handbook](docs/language-v{minor}.md) · [Compiler catalog](docs/language.json)', '']
    rules_lines = compiler_rules(language)
    example_paths, repairs = [], []

    style = []

    def example(path, demo=False):
        require(path in cases and cases[path]['expect'] == 'pass', 'selected example must be registered as pass: ' + str(path))
        example_paths.append(path)
        caption = (', an API demo executed with seed 7: it shows the calls of this section on a tiny canvas, '
                   'not the style of a prop.' if demo else ', executed with seed 7.')
        return ['[' + path.name + '](' + path.relative_to(root).as_posix() + ')' + caption, '',
                '```ips', path.read_text(encoding='utf-8').rstrip(), '```', '']

    # The compiler rules follow the idioms and their before/after pairs, which lead whenever selected.
    rules_pending = True

    for selected in sections:
        if rules_pending and selected not in ('idioms', 'style'):
            lines += rules_lines
            rules_pending = False
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
        elif selected == 'builtins':
            lines += builtin_catalog(root, language, authored, config.get('hints', {}))
        elif selected == 'style':
            lines += ['## Before and after', '',
                      'Each program below compiles and runs but breaks one rule of the idioms; write the corrected form.', '']
            for pair in config['style_pairs']:
                path = (root / 'examples/language' / pair['file']).resolve()
                require(path in cases and cases[path]['expect'] == 'pass', 'style source must be a registered passing example')
                avoid = path.read_text(encoding='utf-8')
                fixed = correction(pair, avoid)
                style.append((pair, path, fixed))
                lines += ['### ' + pair['title'], '', '[' + path.name + '](' + path.relative_to(root).as_posix() + ')', '',
                          'Avoid:', '```ips', avoid.rstrip(), '```', pair['explanation'],
                          'Write:', '```ips', fixed.rstrip(), '```', '']
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
                    f"Diagnostic `{pair['diagnostic_code']}`: `{cases[path]['message']}`.", '',
                    pair['explanation'], 'Corrected:', '```ips', fixed.rstrip(), '```', '']
        else:
            entry = entries[selected]
            chapter = (root / 'docs/language' / entry['chapter']).resolve()
            require(chapter in authored, 'section must come from the registered handbook')
            text = authored[chapter]
            title = headings(text)[0]['title']
            parts = entry['parts']
            whole = parts == 'chapter'
            if whole:
                # The whole chapter: its introduction and every subsection with prose.
                parts = [{'heading': h['title']} for h in headings(text)]
            lines += ['## ' + entry['title'], '']
            for number, part in enumerate(parts):
                body, link = excerpt(root, chapter, text, part['heading'], part.get('paragraphs', 'all'),
                                     part.get('skip', 0), part.get('fences', ()), optional=whole)
                if number == 0:
                    lines += ['[Handbook section](' + link + ')', '']
                if not body:
                    continue
                if part['heading'] != title:
                    lines += ['### ' + part['heading'], '']
                lines += [body, '']
            for name in entry.get('examples', []):
                lines += example((root / 'examples/language' / name).resolve())
            for name in entry.get('demos', []):
                lines += example((root / 'examples/language' / name).resolve(), demo=True)
            if selected == 'part':
                part = language['call_rules']['part']
                params = [p['name'] + (' positional' if p['positional'] else ' named-only') +
                          (' required' if p['required'] else ' default=' + str(p['default'])) for p in part['parameters']]
                lines += ['Part slots: ' + '; '.join(params) + '.',
                          'Part evaluation order: ' + ', '.join(part['evaluation_order']) + '.', '']
    if rules_pending:
        lines += rules_lines
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
        for number, (pair, path, fixed) in enumerate(style):
            owning_copy = directory / path.relative_to(root)
            check_program(compiler, owning_copy, True)
            execute(compiler, owning_copy, directory, 'avoid-' + str(number))
            owning_copy.write_text(fixed, encoding='utf-8')
            check_program(compiler, owning_copy, True)
            execute(compiler, owning_copy, directory, 'style-' + str(number))
            owning_copy.write_text(path.read_text(encoding='utf-8'), encoding='utf-8')
            evidence.append({'file': path.relative_to(root).as_posix(), 'style': 'pass',
                             'correction_check': 'pass', 'correction_execution': 'pass'})
    return result, evidence
