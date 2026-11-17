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
    assert rules['materials'] == {
        'keyword': 'contextual', 'definition_scope': 'separate_file_without_asset',
        'qualification': 'import_alias', 'private_declarations': ['palette', 'const'],
        'required_field': 'base', 'required_field_type': 'color',
        'field_types': ['color', 'i32', 'f32', 'vec2'], 'field_dependencies': 'earlier_fields',
        'construction': 'assignment_only', 'overrides': 'named_exact_type',
        'evaluation_order': 'definition_order', 'replacement': 'evaluate_then_commit',
        'helper_parameters': 'nominal_by_value', 'copy_rng': 'none', 'mutable_fields': False,
        'host_inputs': False, 'helper_returns': False, 'collections': False,
        'asset_constants': False, 'part_arguments': False, 'standalone_check': True,
        'standalone_format': True, 'lowering': 'scalar_fields', 'format_change': False}
    scalar_types = ['i32', 'f32', 'color', 'vec2', 'distribution']
    constant_types = scalar_types + ['list<' + name + '>' for name in scalar_types]
    sampler_types = ['sampler<' + name + '>' for name in scalar_types[:-1]]
    stepper_types = ['stepper<' + name + '>' for name in scalar_types[:-1]]
    storable_types = constant_types + sampler_types + ['list<' + name + '>' for name in sampler_types] + stepper_types + ['list<' + name + '>' for name in stepper_types]
    constant_types = storable_types.copy()
    storable_types += ['layer', 'mask']
    geometry_types = ['diagram2d', 'contours2d', 'regions2d', 'part2d', 'parts2d']
    storable_types += geometry_types
    storable_types += ['seed']  # a source type carried in i32 slots (named seeds v1.1)
    assert rules['inputs'] == [
        {'source_type': 'color', 'binary_type': 'color', 'value_type': 'color'},
        {'source_type': 'f32', 'binary_type': 'f32', 'value_type': 'f32'},
        {'source_type': 'u32', 'binary_type': 'u32', 'value_type': 'i32'},
        {'source_type': 'i32', 'binary_type': 'u32', 'value_type': 'i32'}]
    for field in ('storable', 'helper_parameters', 'helper_value_returns'):
        assert rules['types'][field] == storable_types
    assert rules['types']['output_only_geometry'] == ['circle', 'rect']
    assert rules['types']['point_value_type'] == 'vec2'
    assert rules['geometry_handles']['types'] == geometry_types
    assert rules['geometry_handles']['descriptor_codes'] == dict(zip(geometry_types, range(15, 20)))
    assert not set(geometry_types) & set(rules['reserved_keywords'])
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
        not m['constant_evaluable'] and
        m['effects']['rng'] == 'none' and not m['effects']['writes_canvas'] for m in surface_methods)
    assert rules['surfaces']['blur']['maximum_sigma'] == 64
    assert rules['surfaces']['blur']['clip'] == 'paint_only'
    named = rules['named_seeds']
    assert not {'seed', 'fork', 'initial', 'key'} & set(rules['reserved_keywords'])
    assert named['text_tag'] == 0x80d7e5dc and named['index_tag'] == 0x986d7397
    assert named['initial_opcode'] == 'seed.initial' and named['fork_lowering'] == 'existing_instructions'
    assert 'seed.initial' in {b['name'] for b in description['builtins']}
    assert all(p.get('role') != 'seed' or p['type'] == 'i32'
               for b in description['builtins'] for o in b['overloads'] for p in o['parameters'])
    normal = rules['normal_maps']
    assert not {'normal', 'encoding', 'depth', 'smoothing'} & set(rules['reserved_keywords'])
    assert normal['maximum_smoothing'] == 64 and normal['outputs_per_asset'] == 1 and normal['capture_mode_code'] == 3
    assert len(normal['methods']) == 4 and {(m['receiver'], len(m['parameters'])) for m in normal['methods']} == {
        ('layer', 1), ('layer', 2), ('mask', 1), ('mask', 2)}
    assert all(m['name'] == 'normal' and m['result'] == 'layer' and m['opcode'] == normal['opcode'] and
        not m['constant_evaluable'] and m['effects']['rng'] == 'none' and not m['effects']['writes_canvas']
        for m in normal['methods'])
    assert all(b['name'] != 'normal' for b in description['builtins'])
    maps = rules['material_maps']
    assert [k['name'] for k in maps['kinds']] == ['roughness', 'metallic', 'ao', 'emission']
    assert [k['image_code'] for k in maps['kinds']] == [2, 3, 4, 5] and maps['publication_order'][0] == 'normal'
    assert not {'roughness', 'metallic', 'ao', 'emission'} & set(rules['reserved_keywords'])
    stored = [b for b in description['builtins'] if b['name'] == normal['color_builtin']]
    assert len(stored) == 1 and stored[0]['overloads'][0]['constant_evaluable'] and 'opcode' not in stored[0]['overloads'][0]
    assert rules['masks']['maximum_radius'] == 64
    assert rules['masks']['part_clip'] == 'parent_applies_to_blit_once'
    cases = [
        *[(name+' helper and contextual local', 'keep(v:'+name+')->'+name+'{'+name+' copy=v return choose(seed,copy,copy)}draw{'+name+'=1}', True) for name in geometry_types],
        *[(name+' list rejected', 'keep(v:list<'+name+'>){}draw{}', False) for name in geometry_types],
        *[(name+' input rejected', 'input '+name+' v=0 draw{}', False) for name in geometry_types],
        *[(name+' constant rejected', 'const '+name+' v=0 draw{}', False) for name in geometry_types],
        ('layer blur and helper chain', 'echo(x:layer)->layer{return x.blur(0)} draw{layer image{}surface.paint(echo(image).blur(sigma=2))}', True),
        ('mask blur and widening', 'draw{layer image{}m=mask.from(image).blur(2);surface.paint(m,#fff)}', True),
        ('blur ordinary helper name', 'blur(x:i32)->i32{return x} draw{blur=blur(2)}', True),
        ('blur scalar rejected', 'draw{(1).blur(2)}', False),
        ('blur sigma type rejected', 'draw{layer image{}image.blur(#fff)}', False),
        ('blur missing sigma rejected', 'draw{layer image{}image.blur()}', False),
        ('blur out of range rejected', 'draw{layer image{}image.blur(65)}', False),
        ('normal output from a direction capture', 'output normal relief draw{layer n(encoding=surface.normal){pixel(0,0,color.normal(0,0,1))}relief=n}', True),
        ('normal from mask and layer with names and widening', 'output normal relief draw{layer body{}relief=mask.from(body).normal(2,smoothing=1);x=body.normal(depth=2)}', True),
        ('normal helper transfer', 'bump(m:mask,d:f32)->layer{return m.normal(d)} output normal relief draw{layer body{}relief=bump(mask.from(body),3)}', True),
        ('normal words stay ordinary names', 'input f32 normal=1 encoding(depth:f32)->f32{return depth} draw{smoothing=encoding(normal)}', True),
        ('normal constant direction', 'const color up=color.normal(0,-1,1) draw{pixel(0,0,up)}', True),
        ('second normal output rejected', 'output normal a output normal b draw{}', False),
        ('list normal output rejected', 'output list normal relief draw{}', False),
        ('normal output value type rejected', 'output normal relief draw{relief=#fff}', False),
        ('normal scalar receiver rejected', 'draw{x=(1).normal(2)}', False),
        ('normal missing depth rejected', 'draw{layer image{}x=image.normal()}', False),
        ('normal depth out of range rejected', 'draw{layer image{}x=image.normal(-1)}', False),
        ('normal smoothing out of range rejected', 'draw{layer image{}x=image.normal(1,65)}', False),
        ('normal zero direction rejected', 'draw{x=color.normal(0,0,0)}', False),
        ('normal direction facing away rejected', 'draw{x=color.normal(0,0,-1)}', False),
        ('normal encoding with alpha rejected', 'draw{layer n(alpha=surface.legacy,encoding=surface.normal){}}', False),
        ('normal encoding of another kind rejected', 'draw{layer n(encoding=surface.legacy){}}', False),
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
        # Named seeds (v1.1).
        ('named seeds through helpers and seed arguments', 'draw{s=seed.initial().fork("a");t=wrap(s,2);'
         'x=noise.value(1.0,2.0,t);y=roll(t,0,1);z=choose(t,1,2);stream.push(t);stream.pop();same=s==t} '
         'wrap(b:seed,i:i32)->seed{return b.fork(i)}', True),
        ('fork from an i32 snapshot with a named key', 'draw{start=seed;s=start.fork(key="rim");n=int(s)+1}', True),
        ('fork and initial stay ordinary names', 'input i32 fork=1 draw{initial=fork} initial(x:i32)->i32{return x}', True),
        ('seed arithmetic rejected', 'draw{x=seed.initial()+1}', False),
        ('seed to i32 parameter rejected', 'draw{x=f(seed.initial())} f(v:i32)->i32{return v}', False),
        ('i32 to seed parameter rejected', 'draw{x=f(1)} f(v:seed)->i32{return int(v)}', False),
        ('f32 fork key rejected', 'draw{x=seed.fork(1.5)}', False),
        ('initial of a stored seed rejected', 'draw{s=seed.initial();x=s.initial()}', False),
        ('seed constant rejected', 'const seed s=1 draw{}', False),
        ('seed list rejected', 'draw{x=[seed.initial()]}', False),
        ('seed choices rejected', 'draw{x=pick(seed.initial(),seed.initial())}', False),
        ('string outside fork rejected', 'draw{x="leaves"}', False),
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

        library = 'const i32 K=3\nmaterial Bark {base=#0f0;width=K} material Leaf {base=#fff}'
        (root / 'materials.ips').write_text(library, encoding='utf-8')
        material_entry = ('import forest from "materials.ips"; asset t {size 8x8 '
                          'draw{m=forest.Bark();paint(m)} paint(m:forest.Bark){pixel(0,0,m.base)}}')
        check('material nominal helper and private declarations', material_entry, True)
        check('material nominal type mismatch', material_entry.replace('paint(m)}', 'n=forest.Leaf();paint(n)}'), False)
        check('material private declaration', material_entry.replace('m=forest.Bark()', 'm=forest.Bark(width=K)'), False)
        check('material unknown field', material_entry.replace('m.base', 'm.missing'), False)
        check('material field type override', material_entry.replace('forest.Bark()', 'forest.Bark(width=#fff)'), False)
        check('standalone material library', library, True)
        check('standalone material semantic error', 'material Bark {base=1}', False)
        check('material declarations cannot share an asset file', library + ' asset t {size 8x8 draw{}}', False)

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

        # Models (v1.2, a preview): the rule set against what the compiler
        # accepts in an asset with render { } and rejects in one with draw { }.
        models = rules['models']
        assert models['status'] == 'preview' and models['entry'] == 'render' and models['file_magic'] == 'IP3D'
        assert not {'render', 'place', 'scale', 'vec3', 'skin'} & set(rules['reserved_keywords'])
        flat = {b['name'] for b in description['builtins']}
        added = {b['name']: b for b in models['builtins']}
        assert set(added) == {'box', 'cylinder', 'cone', 'band', 'sphere', 'lathe', 'extrude', 'quad', 'tri',
            'tube', 'sweep',
            'vec3.len', 'vec3.dot', 'vec3.cross', 'vec3.normalize', 'skin.density', 'skin.fit', 'skin.shift',
            'skin.roughness', 'skin.metallic', 'skin.emission', 'skin.cutout', 'skin.two_sided', 'skin.crisp',
            'skin.blend', 'skin.region'}
        assert [b['name'] for b in models['extended_builtins']] == ['lerp', 'list.len']
        assert {'rect', 'pixel', 'poly', 'surface.paint', 'mask.from', 'noise.mask', 'part.draw', 'voronoi'} <= \
            set(models['unavailable_builtins']) < flat
        assert not {'sin', 'lerp', 'roll', 'choose', 'hash', 'noise.fbm', 'color.shade', 'seed.initial', 'list.len'} & \
            set(models['unavailable_builtins'])
        assert [a['name'] for a in models['place']['arguments']] == models['place']['evaluation_order']
        assert [t['name'] for t in models['types']] == ['vec3', 'skin']

        (root / 'tile.ips').write_text('asset tile {size 8x8 input color tint=#fff input f32 gap=1 draw{rect(0,0,8,gap,tint)}}')
        (root / 'solid.ips').write_text('asset solid {size 8x8x8 render{}}')
        model_cases = 0

        def model(name, body, accepted, declarations=''):
            nonlocal model_cases
            model_cases += 1
            return check(name, 'import tile from "tile.ips" asset t {size 8x8x8 ' + declarations + ' ' + body + '}', accepted)

        def flat_asset(name, body, accepted):
            nonlocal model_cases
            model_cases += 1
            return check(name, 'asset t {size 8x8 ' + body + '}', accepted)

        model('empty render block', 'render{}', True)
        # Every exported signature binds: one call per overload, arguments by type.
        values = {'vec3': '{1,2,3}', 'vec2': '{1,2}', 'f32': '0.5', 'i32': '8', 'skin': 's',
                  'list<vec2>': '[{2,0},{2,2},{0,2}]', 'list<vec3>': '[{0,0,0},{0,4,0},{3,4,0}]'}
        for builtin in [*models['builtins'], *models['extended_builtins']]:
            for number, overload in enumerate(builtin['overloads']):
                call = builtin['name'] + '(' + ','.join(values[p['type']] for p in overload['parameters']) + ')'
                named = builtin['name'] + '(' + ','.join(p['name'] + '=' + values[p['type']]
                                                         for p in reversed(overload['parameters'])) + ')'
                for form in (call, named):
                    model(f"{builtin['name']} overload {number}", 'render{s=tile.draw(1);' +
                          (form if overload['result'] == 'void' else 'x=' + form) + '}', True)
        for name in models['unavailable_builtins']:
            report = model(name + ' needs a canvas', 'render{' + name + '()}', False)
            assert 'render asset' in report['diagnostics'][0]['message'], report
        for name in added:
            report = flat_asset(name + ' needs a render asset', 'draw{' + name + '()}', False)
            assert 'render asset' in report['diagnostics'][0]['message'], report
        for type_name in ('vec3', 'skin'):
            flat_asset(type_name + ' is no type of a draw asset', 'draw{} f(p:' + type_name + '){}', False)
            flat_asset(type_name + ' stays an ordinary name in a draw asset', 'draw{' + type_name + '=1}', True)
        flat_asset('render and place stay helper names in a draw asset',
                   'draw{render(1);place(2)} render(n:i32){pixel(n,n,#fff)} place(n:i32){pixel(n,n,#fff)}', True)
        flat_asset('three numbers are a circle in a draw asset', 'output circle c draw{c={1,2,3}}', True)
        model('three numbers are a vec3 in a render asset', 'render{p={1,2,3};z=p.z+p.x+p.y}', True)
        model('vec3 arithmetic and range', 'render{p={1,2,3};q=(p+p-p)*p/p*2/2;r=2*q;n=-r;t=p to q}', True)
        model('vec3 has three components', 'render{p={1,2,3};w=p.w}', False)
        model('vec3 components are read-only', 'render{p={1,2,3};p.x=2.0}', False)
        model('vec3 and vec2 do not mix', 'render{p={1,2,3}+{1,2}}', False)
        model('vec3 constants, parameters and results', 'render{p=move(up)} move(p:vec3)->vec3{return p+up}', True,
              'const vec3 up=vec3.normalize({0,2,0})')
        model('skin parameters and results', 'render{box({0,0,0},{1,1,1},dull(tile.draw(seed)))} '
              'dull(s:skin)->skin{return skin.roughness(s,1)}', True)
        model('skin constants rejected', 'render{}', False, 'const skin s=tile.draw(1)')
        model('lists of vec3 accepted', 'render{n=list.len(f([{0,0,0}]))} f(ps:list<vec3>)->list<vec3>{return ps}', True)
        model('samplers of vec3 rejected', 'render{} f(ps:sampler<vec3>){}', False)
        flat_asset('lists of vec3 belong to render assets', 'draw{} f(ps:list<vec3>){}', False)
        # Shaping blocks, 3D outputs and parts (M3).
        for block in models['shaping_blocks']['blocks']:
            arguments = ','.join('seed.initial()' if 'seed' in p['types'] else '2' if p['types'] == ['i32'] else '1.5'
                                 for p in block['parameters'])
            model(block['name'] + ' block', 'render{' + block['name'] + '(' + arguments + '){}}', True)
            flat_asset(block['name'] + ' needs a render asset', 'draw{' + block['name'] + '(' + arguments + '){}}', False)
        for kind in models['outputs']['kinds']:
            value = '{1,2,3}' if kind['name'] == 'point3d' else '{{0,0,0},{1,1,1}}'
            model(kind['name'] + ' output', 'render{o=' + value + '}', True, 'output ' + kind['name'] + ' o')
            model(kind['name'] + ' list output', 'render{o=' + value + '}', True, 'output list ' + kind['name'] + ' o')
            flat_asset(kind['name'] + ' output needs a render asset', 'output ' + kind['name'] + ' o draw{}', False)
        model('2D outputs rejected in a render asset', 'render{}', False, 'output point p')
        model('a box cannot bend', 'render{bend(30){o={{0,0,0},{1,1,1}}}}', False, 'output box3d o')
        (root / 'peg.ips').write_text('asset peg {size 4x4x4 output point3d tip render{tip={0,4,0}}}')
        report = check('a part is called and read', 'import peg from "peg.ips" asset t {size 8x8x8 output point3d o '
                       'render{place(at={1,0,0}){peg.render(1);o=peg.outputs.tip}}}', True)
        report = check('a part has no placement arguments', 'import peg from "peg.ips" asset t {size 8x8x8 '
                       'render{peg.render(1,at={1,0,0})}}', False)
        model('collections of skins rejected', 'render{} f(ss:list<skin>){}', False)
        model('host inputs of vec3 rejected', 'render{}', False, 'input vec3 at={0,0,0}')
        for argument in models['place']['arguments']:
            for type_name in argument['types']:
                value = {'vec3': '{1,2,3}', 'f32': '1.5', 'i32': '1'}[type_name]
                model(f"place {argument['name']} takes {type_name}", 'render{place(' + argument['name'] + '=' + value + '){}}', True)
        model('place order is free', 'render{place(mirror_x=1,scale=2,roll=1,pitch=2,yaw=3,at={0,1,0}){}}', True)
        model('place needs an argument', 'render{place(){}}', False)
        model('place arguments are named', 'render{place({0,1,0}){}}', False)
        model('place rejects unknown arguments', 'render{place(tilt=1){}}', False)
        model('place rejects duplicates', 'render{place(yaw=1,yaw=2){}}', False)
        model('place at is a vec3', 'render{place(at={1,2}){}}', False)
        model('place mirror is an integer', 'render{place(mirror_x=1.0){}}', False)
        model('place nests and leaves by return', 'render{build()} build(){place(yaw=90){place(at={0,1,0}){return;}}}', True)
        flat_asset('place needs a render asset', 'draw{place(yaw=1){}}', False)
        model('skin call with inputs and size', 'render{s=tile.draw(seed.initial().fork("a"),gap=2,tint=#000,size={16,16})}', True)
        model('skin seed is required', 'render{s=tile.draw(size={8,8})}', False)
        model('skin seed is the only positional argument', 'render{s=tile.draw(1,#000)}', False)
        model('skin size is a vec2', 'render{s=tile.draw(1,size=16)}', False)
        model('skin input type', 'render{s=tile.draw(1,tint=1)}', False)
        model('skin has no placement', 'render{s=tile.draw(1,at={0,0})}', False)
        model('skin is no method receiver', 'render{s=tile.draw(1).fit()}', False)
        model('skin is not a number', 'render{s=tile.draw(1)+1}', False)
        check('a render asset is no skin', 'import solid from "solid.ips" asset t {size 8x8x8 render{s=solid.draw(1)}}', False)
        check('a render asset is a part', 'import solid from "solid.ips" asset t {size 8x8x8 render{solid.render(1)}}', True)
        check('a render asset is no part', 'import solid from "solid.ips" asset t {size 8x8 draw{}}', False)
        model('inputs are visible in render', 'render{x=tall+1.0}', True, 'input f32 tall=2')
        model('inputs are not visible in helpers', 'render{f()} f(){x=tall}', False, 'input f32 tall=2')
        model('palettes, constants and ramp work in render', 'render{c=ramp(tones,0.5);d=tones[k]}', True,
              'palette tones=[#000,#fff] const i32 k=1')
        model('scalar builtins work in render', 'render{a=sin(1.0)+noise.fbm(1.0,2.0,3,4)+roll(seed.initial(),0.0,1.0);'
              'b=hash(1)+choose(1,2,3)+list.len([1,2]);c=color.shade(#888,10);d=vec2.len({3,4})}', True)
        model('outputs rejected', 'render{}', False, 'output point top')
        model('origin rejected', 'render{}', False, 'origin 1,1')
        model('layers rejected', 'render{layer image{}}', False)
        model('clips rejected', 'render{clip(m){}}', False)
        model('both entries rejected', 'draw{} render{}', False)
        model('entry is no helper', 'render{render()}', False)
        check('render needs a depth', 'asset t {size 8x8 render{}}', False)
        check('draw has no depth', 'asset t {size 8x8x8 draw{}}', False)
        check('scale is a positive number', 'asset t {size 8x8x8 scale 0 render{}}', False)
        check('scale belongs to render', 'asset t {size 8x8 scale 32 draw{}}', False)
        check('scale in units per metre', 'asset t {size 8x8x8 scale 32.5 render{}}', True)
        model_cases += 7

    print(f'Language contract: versions, profile and {len(cases) + 4 + model_cases} acceptance cases passed')


if __name__ == '__main__':
    main()
