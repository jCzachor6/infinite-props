#!/usr/bin/env python3
"""Check the language profile against the actual compiler acceptance boundary."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from contract_readers import LANGUAGE_SCHEMA_VERSION
from release_common import described_versions, versions


def main():
    compiler = Path(sys.argv[1]).resolve()
    source = Path(__file__).resolve().parent.parent
    process = subprocess.run([str(compiler), '--describe-language'], capture_output=True,
                             text=True, timeout=30, check=True)
    assert not process.stderr, process.stderr
    description = json.loads(process.stdout)
    assert description['schema_version'] == LANGUAGE_SCHEMA_VERSION

    # Each version comes from its own source; none is derived from another.
    expected = versions(source)
    assert described_versions(description) == expected, (described_versions(description), expected)
    major, minor = expected['c_abi_soversion'].split('.')
    assert description['abi_version'] == {'major': int(major), 'minor': int(minor)}

    rules = description['source_rules']
    assert rules['inputs'] == [
        {'source_type': 'color', 'binary_type': 'color', 'value_type': 'color'},
        {'source_type': 'f32', 'binary_type': 'f32', 'value_type': 'f32'},
        {'source_type': 'u32', 'binary_type': 'u32', 'value_type': 'i32'},
        {'source_type': 'i32', 'binary_type': 'u32', 'value_type': 'i32'}]
    for field in ('storable', 'helper_parameters', 'helper_value_returns'):
        assert rules['types'][field] == ['i32', 'f32', 'color', 'vec2', 'distribution']
    assert rules['types']['output_only_geometry'] == ['circle', 'rect']
    assert rules['types']['point_value_type'] == 'vec2'
    assert rules['types']['implicit_widening'] == [{'from': 'i32', 'to': 'f32',
        'contexts': ['numeric_operands', 'call_arguments', 'helper_returns']}]
    assert rules['types']['local_reassignment'] == 'exact_type'
    assert rules['locals'] == {'declaration': 'first_assignment', 'scope': 'function',
        'type_inference_order': 'source_traversal', 'definite_assignment_analysis': False,
        'unexecuted_initializer': 'typed_zero', 'read_requires_prior_assignment': True}
    assert rules['visibility'] == {'uniforms': 'draw_only', 'uniforms_read_only': True,
        'helper_parameters_read_only': True,
        'draw_controls': {'names': ['v', 'a'], 'type': 'i32', 'read_only': True},
        'seed': 'current_rng_state_in_all_functions', 'palettes': 'all_asset_functions'}
    assert rules['named_calls'] == {'targets': ['builtin', 'helper', 'part'],
        'mixed_positional_and_named': True, 'positional_binding': 'positional_parameter_ordinal',
        'named_arguments_advance_positional_ordinal': False,
        'duplicate_arguments': False, 'unknown_arguments': False}
    assert rules['restrictions'] == {'recursive_helpers': False, 'nested_part_imports': True,
        'let_declarations': False, 'fn_declarations': False, 'named_geometry_constructors': False}
    assert rules['effect_order'] == {'binary_operands': 'left_to_right',
        'logical_operators': 'short_circuit_left_to_right', 'call_arguments': 'signature_evaluation_order',
        'named_arguments_change_evaluation_order': False, 'part_parent_rng': 'restored_after_child'}

    # These are language acceptance cases, not JSON snapshots alone. Each
    # advertised rule is exercised by a valid source and/or a rejection.
    cases = [
        ('input types', 'input color c=#abc input f32 f=7 input u32 u=4294967295 '
         'input i32 i=-7 draw { n=i+u; x=f+n; pixel(0,0,c) }', True),
        ('signed default bounds', 'input i32 i=2147483648 draw{}', False),
        ('unsigned negative', 'input u32 u=-7 draw{}', False),
        ('helper types and widening', 'draw{ x=widen(7); p=point({1,2}); paint(1,2,#abc) } '
         'widen(x:f32)->f32{return 7} point(p:vec2)->vec2{return p} '
         'paint(x:f32,y:f32,c:color){rect(x,y,1,1,c)}', True),
        ('no narrowing', 'draw{paint(1.5)} paint(x:i32){}', False),
        ('uniform-only u32 type', 'draw{} helper(x:u32){}', False),
        ('output geometry', 'output circle c output rect r draw{c={1,2,3};r={1,2,3,4}}', True),
        ('circle cannot be local', 'draw{x={1,2,3}}', False),
        ('rect cannot be local', 'draw{x={1,2,3,4}}', False),
        ('geometry helper parameter', 'draw{} helper(x:circle){}', False),
        ('legacy branch local', 'draw{if(false){x=1} pixel(x,0,#fff)}', True),
        ('legacy loop local', 'draw{while(false){x=1} pixel(x,0,#fff)}', True),
        ('local read before assignment', 'draw{pixel(x,0,#fff);x=1}', False),
        ('local exact reassignment', 'draw{x=1.0;x=1}', False),
        ('uniform explicitly passed', 'input i32 n=1 draw{paint(n)} paint(x:i32){pixel(x,0,#fff)}', True),
        ('uniform hidden from helper', 'input i32 n=1 draw{paint()} paint(){pixel(n,0,#fff)}', False),
        ('uniform read only', 'input i32 n=1 draw{n=2}', False),
        ('parameter read only', 'draw{} helper(x:i32){x=2}', False),
        ('draw control read only', 'draw{v=2}', False),
        ('seed and palettes in helper', 'palette p=[#abc,#fff] draw{paint()} '
         'paint(){pixel(seed&1,0,p[0])}', True),
        ('seed read only', 'draw{seed=2}', False),
        ('named builtin', 'draw{rect(color=#fff,h=1,w=1,y=2,x=1)}', True),
        ('mixed helper named before positional', 'draw{paint(y=2,1)} '
         'paint(x:f32,y:f32){rect(x,y,1,1,#fff)}', True),
        ('named does not consume positional slot', 'draw{paint(x=2,1)} paint(x:i32,y:i32){}', False),
        ('duplicate named', 'draw{paint(x=2,x=1)} paint(x:i32){}', False),
        ('unknown named', 'draw{paint(z=2)} paint(x:i32){}', False),
        ('forward helpers', 'draw{first()} first(){second()} second(){}', True),
        ('direct recursion', 'draw{first()} first(){first()}', False),
        ('indirect recursion', 'draw{first()} first(){second()} second(){first()}', False),
        ('future let syntax', 'draw{let x=1}', False),
        ('future fn syntax', 'draw{} fn helper(){}', False),
        ('future vec2 constructor', 'draw{x=vec2(1,2)}', False),
        ('future circle constructor', 'draw{x=circle(1,2,3)}', False),
        ('rect drawing is not a value constructor', 'draw{x=rect(1,2,3,4,#fff)}', False),
    ]
    with tempfile.TemporaryDirectory(prefix='iprop-language-contract-') as directory:
        root = Path(directory)

        def check(name, text, accepted):
            entry = root / 'entry.ips'
            entry.write_text(text, encoding='utf-8')
            result = subprocess.run([str(compiler), str(entry), '--check', '--diagnostics=json'],
                                    capture_output=True, text=True, timeout=30)
            assert not result.stderr, (name, result.stderr)
            report = json.loads(result.stdout)
            assert report['language_version'] == description['language_version'], (name, report)
            assert report['product_version'] == description['product_version'], (name, report)
            assert result.returncode == (0 if accepted else 1), (name, report)
            assert report['status'] == ('ok' if accepted else 'invalid_source'), (name, report)
            assert bool(report['diagnostics']) != accepted, (name, report)
            assert not list(root.glob('*.iprop')), name
            return report

        for name, body, accepted in cases:
            check(name, 'asset t {size 8x8 ' + body + '}', accepted)

        (root / 'child.ips').write_text('asset child {size 8x8 input i32 n=-7 draw{}}')
        entry = ('import child from "child.ips" asset t {size 8x8 '
                 'draw{child.draw(a=0,at={0,0},v=0,n=1,seed=seed)}}')
        check('named part controls and uniform', entry, True)
        check('part placement is named only', entry.replace('at={0,0}', '{0,0}'), False)
        # Nested imports resolve beside the importing file, not the entry.
        (root / 'parts').mkdir()
        (root / 'parts' / 'grandchild.ips').write_text('asset grandchild {size 8x8 draw{}}')
        (root / 'child.ips').write_text('import grandchild from "parts/grandchild.ips" '
            'asset child {size 8x8 input i32 n=-7 draw{grandchild.draw(seed,v,a,at={0,0})}}')
        check('nested part import', entry, True)
        (root / 'parts' / 'grandchild.ips').write_text('import back from "../child.ips" asset grandchild {size 8x8 draw{}}')
        report = check('nested import cycle', entry, False)
        assert report['diagnostics'][0]['message'] == \
            'import cycle: child.ips -> parts/grandchild.ips -> child.ips', report

    print(f'Language contract: versions, profile and {len(cases) + 4} acceptance cases passed')


if __name__ == '__main__':
    main()
