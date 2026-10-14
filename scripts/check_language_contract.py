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
    scalar_types = ['i32', 'f32', 'color', 'vec2', 'distribution']
    constant_types = scalar_types + ['list<' + name + '>' for name in scalar_types]
    sampler_types = ['sampler<' + name + '>' for name in scalar_types[:-1]]
    stepper_types = ['stepper<' + name + '>' for name in scalar_types[:-1]]
    storable_types = constant_types + sampler_types + ['list<' + name + '>' for name in sampler_types] + stepper_types + ['list<' + name + '>' for name in stepper_types]
    constant_types = storable_types.copy()
    storable_types += ['layer', 'mask']
    assert rules['inputs'] == [
        {'source_type': 'color', 'binary_type': 'color', 'value_type': 'color'},
        {'source_type': 'f32', 'binary_type': 'f32', 'value_type': 'f32'},
        {'source_type': 'u32', 'binary_type': 'u32', 'value_type': 'i32'},
        {'source_type': 'i32', 'binary_type': 'u32', 'value_type': 'i32'}]
    for field in ('storable', 'helper_parameters', 'helper_value_returns'):
        assert rules['types'][field] == storable_types
    assert rules['types']['output_only_geometry'] == ['circle', 'rect']
    assert rules['types']['point_value_type'] == 'vec2'
    assert rules['resource_exports']['types'] == ['mask', 'layer']
    assert rules['resource_exports']['table'] == 'separate_from_host_geometry'
    assert rules['resource_exports']['buffer_release'] == 'last_handle'
    assert rules['types']['implicit_widening'] == [{'from': 'i32', 'to': 'f32',
        'contexts': ['numeric_operands', 'call_arguments', 'helper_returns', 'asset_constants']}]
    assert rules['types']['local_reassignment'] == 'exact_type'
    assert rules['locals'] == {'declaration': 'first_assignment', 'scope': 'function',
        'type_inference_order': 'source_traversal', 'definite_assignment_analysis': False,
        'unexecuted_initializer': 'typed_zero', 'read_requires_prior_assignment': True}
    assert rules['visibility'] == {'uniforms': 'draw_only', 'uniforms_read_only': True,
        'helper_parameters_read_only': True,
        'seed': 'current_rng_state_in_all_functions', 'palettes': 'all_asset_functions',
        'constants': 'all_asset_functions'}
    assert rules['asset_constants'] == {'keyword': 'reserved',
        'syntax': 'const <type> <name> = <expression>', 'types': constant_types,
        'scope': 'asset', 'visibility': 'all_asset_functions', 'read_only': True,
        'dependencies': 'forward_acyclic', 'evaluation': 'compile_time',
        'check_all_declarations': True, 'check_effects_in_dead_branches': True,
        'user_helper_calls': False, 'generation_state': False, 'distribution_validation': 'compile_time'}
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
    assert rules['types']['parameterized'] == [{'constructor': 'list', 'element_types': scalar_types + sampler_types + stepper_types,
        'nested': False, 'representation': 'immutable_arena_handle'}, {'constructor': 'sampler',
        'element_types': scalar_types[:-1], 'nested': False, 'representation': 'immutable_arena_handle'}, {'constructor': 'stepper',
        'element_types': scalar_types[:-1], 'nested': False, 'representation': 'immutable_arena_handle'}]
    assert rules['collections']['index_bounds'] == 'trap'
    assert rules['collections']['maximum_length'] == 256
    assert rules['collections']['generation_data_limit_bytes'] == 67108864
    methods = rules['collections']['methods']
    assert len(methods) == 17
    assert {(m['name'],m['receiver']) for m in methods} == (
        {('distinct','list<' + t + '>') for t in scalar_types + sampler_types + stepper_types} |
        {('sort','list<' + t + '>') for t in scalar_types[:-1]})
    assert all(m['result'] == m['receiver'] and m['parameters'] == [] and
               m['constant_evaluable'] and m['effects']['rng'] == 'none' for m in methods)
    assert rules['point_distributions']['families'] == [{'name':'weighted','minimum_points':1,'maximum_points':256,'value':'discrete_mass','equal_bounds':'positive_mass_at_point'}, {'name':'density','minimum_points':2,'maximum_points':256,'value':'piecewise_linear_density','equal_bounds':'closed_natural_support'}]
    assert rules['surfaces']['maximum_active_scopes'] == 32
    assert rules['surfaces']['scope_cleanup'] == ['end','return','break','continue']
    assert 'blur' not in rules['reserved_keywords']
    assert all(b['name'] != 'blur' for b in description['builtins'])
    surface_methods = rules['surfaces']['methods']
    assert len(surface_methods) == 2 and {m['receiver'] for m in surface_methods} == {'layer','mask'}
    assert all(m['name'] == 'blur' and m['parameters'] == [{'name':'sigma','type':'f32'}] and
        m['result'] == m['receiver'] and m['opcode'] == m['receiver'] + '.blur' and
        m['minimum_format_minor'] == 7 and not m['constant_evaluable'] and
        m['effects']['rng'] == 'none' and not m['effects']['writes_canvas'] for m in surface_methods)
    assert rules['surfaces']['blur']['maximum_sigma'] == 64
    assert rules['surfaces']['blur']['clip'] == 'paint_only'
    assert rules['masks']['maximum_radius'] == 64
    assert rules['masks']['part_clip'] == 'parent_applies_to_blit_once'
    cases = [
        ('layer blur and helper chain', 'echo(x:layer)->layer{return x.blur(0)} draw{layer image{}surface.paint(echo(image).blur(sigma=2))}', True),
        ('mask blur and widening', 'draw{layer image{}m=mask.from(image).blur(2);surface.paint(m,#fff)}', True),
        ('blur ordinary helper name', 'blur(x:i32)->i32{return x} draw{blur=blur(2)}', True),
        ('blur scalar rejected', 'draw{(1).blur(2)}', False),
        ('blur sigma type rejected', 'draw{layer image{}image.blur(#fff)}', False),
        ('blur missing sigma rejected', 'draw{layer image{}image.blur()}', False),
        ('blur out of range rejected', 'draw{layer image{}image.blur(65)}', False),
        ('list transforms and constant empty chain', 'const list<i32> empty=[].sort().distinct() draw{items=[3,1,3].distinct().sort()}', True),
        ('list method names remain identifiers', 'sort(x:i32)->i32{return x} distinct(x:i32)->i32{return x} draw{sort=sort(distinct(1))}', True),
        ('distribution sort rejected', 'draw{items=[gaussian(0,1)].sort()}', False),
        ('sampler sort rejected', 'draw{items=[sampler(1)].sort()}', False),
        ('list method arguments rejected', 'draw{items=[1].distinct(1)}', False),
        ('list method scalar receiver rejected', 'draw{items=(1).sort()}', False),
        ('layer snapshot', 'echo(image:layer)->layer{return image} draw{layer image{pixel(0,0,#fff)} copy=echo(image);surface.paint(copy,blend=surface.legacy)}', True),
        ('layer reserved helper', 'layer(x:i32)->i32{return x} draw{x=layer(2);layer image{}}', False),
        ('layer typed copy', 'draw{layer image{} layer copy=image;surface.paint(copy)}', True),
        ('layer current result not yet bound', 'draw{layer image{surface.paint(image)}}', False),
        ('layer const rejected', 'const layer image=1 draw{}', False),
        ('layer lists rejected', 'echo(images:list<layer>){return;} draw{}', False),
        ('layer sampler rejected', 'echo(images:sampler<layer>){return;} draw{}', False),
        ('layer host input rejected', 'input layer image draw{}', False),
        ('layer resource export', 'output layer image draw{}', True),
        ('mask resource export', 'output mask region draw{}', True),
        ('list resource export rejected', 'output list layer image draw{}', False),
        ('layer arithmetic rejected', 'draw{layer image{}x=image+image}', False),
        ('layer comparison rejected', 'draw{layer image{}x=image==image}', False),
        ('layer target type conflict', 'draw{image=1;layer image{}}', False),
        ('surface mode static', 'draw{layer image{}surface.paint(image,blend=1)}', False),
        ('surface source_over', 'draw{layer image(alpha=surface.source_over){}}', True),
        ('density snapshot', 'const distribution d=density{0:0,1:1,2:0} draw{s=sampler(0.0 to 2.0 with d);xs=s.pick(3)}', True),
        ('density reserved helper', 'density(x:i32)->i32{return x} draw{x=density(2);d=density{0:1,1:1}}', False),
        ('density minimum', 'draw{d=density{0:1}}', False),
        ('density area', 'draw{d=density{0:0,1:0}}', False),
        ('density order', 'draw{d=density{1:1,0:1}}', False),
        ('density negative with runtime key', 'draw{d=density{v:(-1),v+1:1}}', False),
        ('weighted snapshot', 'const distribution d=weighted{16777217:50,16777218:20} draw{s=sampler(16777217 to 16777218 with d);xs=s.pick(3)}', True),
        ('weighted reserved helper', 'weighted(x:i32)->i32{return x} draw{x=weighted(2);d=weighted{1:1}}', False),
        ('weighted order', 'draw{d=weighted{2:1,1:2}}', False),
        ('weighted mass', 'draw{d=weighted{1:0}}', False),
        ('weighted negative weight with runtime key', 'draw{d=weighted{v:(-1)}}', False),
        ('weighted nonnumeric', 'draw{d=weighted{#fff:1}}', False),
        ('sampler helper and batch', 'echo(s:sampler<i32>)->sampler<i32>{return s} draw{s=echo(sampler(10 to 15));items=s.pick(3);x=items[0]}', True),
        ('sampler list', 'draw{items=[sampler(1),sampler(2 to 4)];x=items[0].pick();n=list.len(items)}', True),
        ('sampler constants', 'const sampler<i32> width=sampler(1 to 2) draw{x=width.pick()}', True),
        ('constant sampler list', 'const list<sampler<i32>> specs=[sampler(1),sampler(2 to 4)] const i32 n=list.len(specs) draw{x=specs[0].pick(n)}', True),
        ('range palette', 'palette widths=[10 to 15,20 to 30 with gaussian(25,2),40] draw{s=widths[-1];x=s.pick(3)}', True),
        ('palette descriptions through helper', 'const sampler<i32> fixed=sampler(40) palette widths=[1 to 2,fixed] echo(s:list<sampler<i32>>)->sampler<i32>{return s[0]} draw{x=echo(widths).pick()}', True),
        ('vector palette', 'palette points=[{1,2},{3,4}] draw{x=points[-1]}', True),
        ('sampling ramp rejected', 'palette tones=[#123 to #456,#789] draw{x=ramp(tones,0.5)}', False),
        ('constant sampler RNG rejected', 'const sampler<i32> bad=sampler(index(3)) draw{}', False),
        ('constant pick rejected', 'const sampler<i32> s=sampler(1) const i32 bad=s.pick() draw{}', False),
        ('sampler invalid count', 'draw{s=sampler(1 to 2);x=s.pick(257)}', False),
        ('sampler count type', 'draw{s=sampler(1 to 2);x=s.pick(1.0)}', False),
        ('sampler capture bounds', 'draw{s=sampler(4 to 2)}', False),
        ('sampler capture collision', 'sampler(x:i32)->i32{return x} draw{x=sampler(1 to 2)}', False),
        ('sampler reserved helper', 'sampler(x:i32)->i32{return x} draw{x=sampler(1)}', False),
        ('lists through helpers', 'draw{items=echo([1,2]);n=list.len(items)} echo(items:list<i32>)->list<i32>{return items}', True),
        ('empty context', 'const list<vec2> empty=[] draw{points=empty}', True),
        ('empty without context', 'draw{items=[]}', False),
        ('mixed element rejection', 'draw{items=[1,#abc]}', False),
        ('list numeric widening', 'draw{items=[1,2.0];x=items[0]}', True),
        ('nested list rejection', 'draw{items=[[1]]}', False),
        ('list polygon', 'draw{poly([{0,0},{3,0},{0,3}],#abc)}', True),
        ('first-class palette', 'palette tones=[#abc,#def] draw{items=tones;n=list.len(items)}', True),
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
        ('integer bounds result', 'draw{x=min(1,2);x=max(1,2);x=clamp(1,2,0);take(x)} take(n:i32){}', True),
        ('integer bounds source migration', 'draw{x=1.0;x=min(1,2)}', False),
        ('mixed float bounds', 'draw{x=1.0;x=min(1,2.0);x=max(1.0,2);x=clamp(1,0,2.0)}', True),
        ('normalize vector', 'draw{p=vec2.normalize(v={3,4})}', True),
        ('normalize needs vector', 'draw{p=vec2.normalize(1)}', False),
        ('asset constants and forward references', 'const i32 n=next+1 const i32 next=2 '
         'const f32 width=n const vec2 direction=vec2.normalize({width,4}) '
         'const color iron=rgb(146,156,168) const distribution d=gaussian(0.5,0.18) '
         'paint(){pixel(n,0,iron)} draw{paint();x=0.0 to 1.0 with d}', True),
        ('const reserved helper', 'const(x:i32)->i32{return x} draw{x=const(1)}', False),
        ('const cycle', 'const i32 x=y const i32 y=x draw{}', False),
        ('const input effect', 'input i32 n=1 const i32 x=n draw{}', False),
        ('const seed effect', 'const i32 x=seed draw{}', False),
        ('const RNG in dead branch', 'const i32 x=false && index(1) draw{}', False),
        ('const helper call', 'const i32 x=helper() helper()->i32{return 1} draw{}', False),
        ('const invalid unused distribution', 'const distribution d=beta(0,1) draw{}', False),
        ('uniform explicitly passed', 'input i32 n=1 draw{paint(n)} paint(x:i32){pixel(x,0,#fff)}', True),
        ('uniform hidden from helper', 'input i32 n=1 draw{paint()} paint(){pixel(n,0,#fff)}', False),
        ('uniform read only', 'input i32 n=1 draw{n=2}', False),
        ('parameter read only', 'draw{} helper(x:i32){x=2}', False),
        ('ordinary control names', 'draw{v=2;a=3;variants=4;archetypes=5}', True),
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
                 'draw{child.draw(at={0,0},n=1,seed=seed)}}')
        check('named part controls and uniform', entry, True)
        check('part placement is named only', entry.replace('at={0,0}', '{0,0}'), False)
        # Nested imports resolve beside the importing file, not the entry.
        (root / 'parts').mkdir()
        (root / 'parts' / 'grandchild.ips').write_text('asset grandchild {size 8x8 draw{}}')
        (root / 'child.ips').write_text('import grandchild from "parts/grandchild.ips" '
            'asset child {size 8x8 input i32 n=-7 draw{grandchild.draw(seed,at={0,0})}}')
        check('nested part import', entry, True)
        (root / 'parts' / 'grandchild.ips').write_text('import back from "../child.ips" asset grandchild {size 8x8 draw{}}')
        report = check('nested import cycle', entry, False)
        assert report['diagnostics'][0]['message'] == \
            'import cycle: child.ips -> parts/grandchild.ips -> child.ips', report

    print(f'Language contract: versions, profile and {len(cases) + 4} acceptance cases passed')


if __name__ == '__main__':
    main()
