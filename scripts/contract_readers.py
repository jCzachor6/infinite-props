"""Small, protocol-specific readers; normal builds need only Python's stdlib.

JSON Schema checks in CI are independent of these runtime invariants. There is
no generic schema interpreter here and no catalogue of builtin signatures.
"""
import json
import re
from release_common import read_version_matrix
from pathlib import PurePosixPath

LANGUAGE_SCHEMA_VERSION = 3
DIAGNOSTICS_SCHEMA_VERSION = 1
DOCUMENTATION_SCHEMA_VERSION = 2
VALUE_TYPES = {'i32', 'f32', 'color', 'vec2', 'distribution', 'circle', 'rect', 'void', 'T', 'palette<f32>', 'palette<color>'}
SAMPLER_TYPES = {'sampler<' + item + '>' for item in ('i32', 'f32', 'color', 'vec2')}
LIST_TYPES = {'list<' + item + '>' for item in ({'i32', 'f32', 'color', 'vec2', 'distribution'} | SAMPLER_TYPES)}
VALUE_TYPES |= LIST_TYPES | SAMPLER_TYPES
RNG_EFFECTS = {'none', 'read_current', 'advance_current', 'push_seeded_stream', 'restore_stream',
               'seeded_computation', 'isolated_seeded_stream', 'shared_callee_stream', 'isolated_callee_stream'}
TRAP_CHECKS = {'numeric_range', 'stream_stack_underflow', 'part_render_arguments', 'callee',
              'part_output_missing', 'part_output_context', 'divide_by_zero', 'integer_overflow', 'index_range', 'unassigned_handle'}
EXITS = {'ok': 0, 'invalid_source': 1, 'cli_error': 2, 'internal_error': 3}
STAGES = {'lex', 'parse', 'import', 'typing', 'planning', 'validation', 'io', 'cli', 'internal'}
HASH = re.compile(r'^[0-9a-f]{64}$')
PRODUCT = re.compile(r'^\d+\.\d+\.\d+$')
SOVERSION = re.compile(r'^\d+\.\d+$')


def require(condition, path):
    if not condition:
        raise RuntimeError('Invalid export contract: ' + path)


def parse_json(text):
    def invalid(value):
        raise ValueError('Non-JSON number: ' + value)
    return json.loads(text, parse_constant=invalid)


def obj(value, fields, path):
    require(type(value) is dict and set(fields) <= value.keys(), path + ' required fields')
    return value


def text(value, path, nonempty=False):
    require(type(value) is str and (not nonempty or bool(value)), path + ' string')
    return value


def integer(value, path, minimum=0, maximum=9007199254740991):
    require(type(value) in {int, float} and minimum <= value <= maximum and
            value == int(value), path + ' integer')


def boolean(value, path):
    require(type(value) is bool, path + ' boolean')


def strings(value, path, choices=None, nonempty=False):
    require(type(value) is list and (not nonempty or bool(value)), path + ' array')
    for item in value:
        text(item, path, True)
    require(len(value) == len(set(value)), path + ' unique values')
    require(choices is None or set(value) <= choices, path + ' values')
    return value


def version(value, path):
    obj(value, ['major', 'minor'], path)
    integer(value['major'], path + '.major')
    integer(value['minor'], path + '.minor')


def effects(value, path):
    obj(value, ['rng', 'writes_canvas', 'writes_outputs', 'summary', 'may_trap'], path)
    require(type(value['rng']) is str and value['rng'] in RNG_EFFECTS, path + '.rng')
    require(type(value['summary']) is str and value['summary'] in {'intrinsic', 'conservative'}, path + '.summary')
    boolean(value['writes_canvas'], path + '.writes_canvas')
    boolean(value['writes_outputs'], path + '.writes_outputs')
    strings(value['may_trap'], path + '.may_trap', TRAP_CHECKS)


def ordered_signature(value, path, typed=True):
    obj(value, ['parameters'], path)
    require(type(value['parameters']) is list, path + '.parameters')
    names = []
    for parameter in value['parameters']:
        obj(parameter, ['name', 'type'] if typed else ['name', 'required', 'positional'], path + '.parameter')
        names.append(text(parameter['name'], path + '.name', True))
        if typed:
            require(type(parameter['type']) is str and parameter['type'] in VALUE_TYPES, path + '.type')
        else:
            boolean(parameter['required'], path + '.required')
            boolean(parameter['positional'], path + '.positional')
            require(parameter['required'] != ('default' in parameter), path + '.default/required')
            if 'default' in parameter:
                default = parameter['default']
                if type(default) in {int, float}:
                    integer(default, path + '.default')
                else:
                    require(default == 'input_declaration', path + '.default')
    require(len(names) == len(set(names)), path + ' unique parameter names')
    order = strings(value.get('evaluation_order'), path + '.evaluation_order')
    require(len(order) == len(names) and set(order) == set(names), path + ' evaluation permutation')
    if typed:
        require(type(value.get('result')) is str and value['result'] in VALUE_TYPES, path + '.result')
        effects(value.get('effects'), path + '.effects')


def read_language(value):
    obj(value, ['schema_version', 'product_version', 'builtins', 'language_version', 'format_version',
                'c_api_version', 'abi_version', 'source_rules', 'effect_rules', 'call_rules'], 'language')
    require(type(value['schema_version']) in {int, float} and value['schema_version'] == LANGUAGE_SCHEMA_VERSION, 'language schema version')
    require(PRODUCT.fullmatch(text(value['product_version'], 'product_version')), 'product_version')
    for key in ['language_version', 'format_version', 'c_api_version', 'abi_version']:
        version(value[key], key)
    if 'c_abi_soversion' in value:
        require(SOVERSION.fullmatch(text(value['c_abi_soversion'], 'c_abi_soversion')) and
                value['c_abi_soversion'] == f"{int(value['abi_version']['major'])}.{int(value['abi_version']['minor'])}",
                'c_abi_soversion matches abi_version')
    require(type(value['builtins']) is list and bool(value['builtins']), 'builtins')
    names = []
    for builtin in value['builtins']:
        obj(builtin, ['name', 'overloads'], 'builtin')
        names.append(text(builtin['name'], 'builtin.name', True))
        require(type(builtin['overloads']) is list and (builtin['overloads'] or 'variadic' in builtin), 'builtin.overloads')
        for signature in builtin['overloads']:
            ordered_signature(signature, 'overload')
            boolean(signature.get('constant_evaluable'), 'constant_evaluable')
            if 'opcode' in signature or 'minimum_format_minor' in signature:
                text(signature.get('opcode'), 'opcode', True)
                integer(signature.get('minimum_format_minor'), 'minimum_format_minor', maximum=255)
        if 'variadic' in builtin:
            signature = builtin['variadic']
            ordered_signature(signature, 'variadic')
            require(signature.get('constant_evaluable') is False, 'variadic constant_evaluable')
            integer(signature.get('minimum_arguments'), 'minimum_arguments', 1)
            require(signature['minimum_arguments'] == len(signature['parameters']), 'variadic minimum template arity')
            require(type(signature.get('repeat')) is str and signature['repeat'] in {'choices', 'coordinate_pairs'}, 'variadic repeat')
            if signature['repeat'] == 'choices':
                strings(signature.get('choice_types'), 'choice_types', VALUE_TYPES, True)
                require(signature.get('choice_evaluation') == 'eager_once_before_selection_rng', 'choice_evaluation')
    require(len(names) == len(set(names)), 'unique builtin names')
    rules = obj(value['source_rules'], ['inputs', 'types', 'locals', 'visibility', 'asset_constants', 'named_calls', 'restrictions', 'effect_order'], 'source_rules')
    constants = rules['asset_constants']
    _strings_and_bools(constants, {'keyword': {'contextual'}, 'scope': {'asset'},
        'syntax': {'const <type> <name> = <expression>'}, 'visibility': {'all_asset_functions'},
        'dependencies': {'forward_acyclic'}, 'evaluation': {'compile_time'}, 'distribution_validation': {'compile_time'}},
        ['read_only', 'check_all_declarations', 'check_effects_in_dead_branches', 'user_helper_calls', 'generation_state'], 'asset_constants')
    constant_types = {'i32', 'f32', 'color', 'vec2', 'distribution'}
    strings(constants.get('types'), 'constant types', constant_types | LIST_TYPES | SAMPLER_TYPES, True)
    require(constants['read_only'] and constants['check_all_declarations'] and constants['check_effects_in_dead_branches'] and
            not constants['user_helper_calls'] and not constants['generation_state'], 'constant policy')
    require(type(rules['inputs']) is list and bool(rules['inputs']), 'source inputs')
    source_types = []
    for item in rules['inputs']:
        obj(item, ['source_type', 'binary_type', 'value_type'], 'input mapping')
        require(all(type(item[key]) is str for key in ['source_type', 'binary_type', 'value_type']) and item['source_type'] in {'i32', 'u32', 'f32', 'color'} and item['binary_type'] in {'u32', 'f32', 'color'} and item['value_type'] in VALUE_TYPES, 'input types')
        require(item['binary_type'] == ('u32' if item['source_type'] == 'i32' else item['source_type']), 'binary input mapping')
        require(item['value_type'] == ('i32' if item['source_type'] in {'i32', 'u32'} else item['source_type']), 'expression input mapping')
        source_types.append(item['source_type'])
    require(len(source_types) == len(set(source_types)), 'unique source input types')
    types = obj(rules['types'], ['storable', 'helper_parameters', 'helper_value_returns', 'output_only_geometry', 'point_value_type', 'implicit_widening', 'local_reassignment'], 'types')
    for key in ['storable', 'helper_parameters', 'helper_value_returns', 'output_only_geometry']:
        strings(types[key], key, {'circle', 'rect'} if key == 'output_only_geometry' else ({'i32', 'f32', 'color', 'vec2', 'distribution'} | LIST_TYPES | SAMPLER_TYPES), True)
    if 'parameterized' in types:
        parameters = types['parameterized']
        require(type(parameters) is list and 1 <= len(parameters) <= 2, 'parameterized type descriptors')
        constructors = set()
        for parameter in parameters:
            _strings_and_bools(parameter, {'constructor': {'list', 'sampler'}, 'representation': {'immutable_arena_handle'}}, ['nested'], 'parameterized type')
            require(parameter['nested'] is False, 'nested list/sampler types')
            allowed = {'i32','f32','color','vec2'} if parameter['constructor'] == 'sampler' else {'i32','f32','color','vec2','distribution'} | SAMPLER_TYPES
            strings(parameter.get('element_types'), 'element types', allowed, True)
            require(parameter['constructor'] not in constructors, 'duplicate type constructor')
            constructors.add(parameter['constructor'])
        require('list' in constructors, 'list type constructor')
    if 'samplers' in rules:
        sampler = obj(rules['samplers'], ['syntax', 'capture_evaluation', 'method_evaluation', 'methods', 'capture_effects'], 'samplers')
        _strings_and_bools(sampler, {
            'syntax': {'sampler(<value> | <lower> to <upper> [with <distribution>])'},
            'intrinsic_capture_rng': {'none'}, 'bounds_validation': {'capture'},
            'probability_mass_validation': {'pick'}, 'method_resolution': {'static_receiver_type_and_arity'},
            'batch_preflight': {'count_handle_allocation_before_first_draw'}}, ['asset_constants','range_palettes'], 'sampler rules')
        require(sampler['asset_constants'] is True and sampler['range_palettes'] is True, 'sampler constants and palettes')
        require(set(constants['types']) == constant_types | LIST_TYPES | SAMPLER_TYPES, 'complete constant types')
        palette = obj(rules.get('palettes'), ['element_types'], 'palettes')
        _strings_and_bools(palette, {
            'syntax': {'palette <name> = [<elements>]'}, 'evaluation': {'compile_time_no_rng'},
            'sampling_trigger': {'outer_range_or_sampler'},
            'constant_entries': {'lift_to_constant_descriptions_in_sampling_palette'},
            'numeric_elements': {'i32_to_f32_widening'}, 'index_type': {'i32'}, 'index_evaluation': {'once'},
            'index_bounds': {'last_element_fallback'}, 'first_class_value': {'immutable_list_with_checked_index'},
            'legacy_length_limit': {'unchanged'}, 'ramp': {'constant_color_or_f32_only'}}, [], 'palette policy')
        palette_types = {'i32','f32','color','vec2'} | SAMPLER_TYPES
        strings(palette['element_types'], 'palette element types', palette_types, True)
        require(set(palette['element_types']) == palette_types, 'complete palette types')
        for key, expected in [('minimum_length',1),('sampling_maximum_length',256),('first_class_maximum_length',256)]:
            integer(palette.get(key), 'palette.' + key)
            require(palette[key] == expected, 'palette.' + key)
        require(sampler['capture_evaluation'] == 'lower_upper_distribution_once' and sampler['method_evaluation'] == 'receiver_once_before_count_once', 'sampler evaluation order')
        effects(sampler['capture_effects'], 'sampler capture')
        require(sampler['capture_effects']['rng'] == 'none', 'sampler capture intrinsic RNG')
        require(sampler.get('minimum_count') == 0 and sampler.get('maximum_count') == 256 and sampler.get('count_type') == 'i32', 'sampler count')
        require(sampler.get('constant_description_draws') == 0 and sampler.get('empty_batch_draws') == 0 and sampler.get('unset_empty_batch') == 'trap', 'sampler zero draws')
        for field in ['minimum_count','maximum_count','constant_description_draws','empty_batch_draws']:
            integer(sampler.get(field), 'samplers.' + field)
        require(type(sampler['methods']) is list and len(sampler['methods']) == 8, 'sampler method signatures')
        signatures = set()
        for method in sampler['methods']:
            obj(method, ['name','receiver','parameters','result','opcode','minimum_format_minor','effects'], 'sampler method')
            require(method['name'] == 'pick' and method['receiver'] in SAMPLER_TYPES, 'sampler receiver')
            require(type(method['parameters']) is list and len(method['parameters']) <= 1, 'sampler parameters')
            item = method['receiver'][8:-1]
            batch = bool(method['parameters'])
            require(method['parameters'] == ([{'name':'n','type':'i32'}] if batch else []), 'sampler count signature')
            require(method['result'] == ('list<' + item + '>' if batch else item), 'sampler result')
            require(method['opcode'] == ('sampler.pick.list' if batch else 'sampler.pick') and method['minimum_format_minor'] == 6, 'sampler opcode')
            effects(method['effects'], 'sampler method effects')
            require(method['effects']['rng'] == 'advance_current', 'sampler method RNG')
            signatures.add((method['receiver'], batch))
        require(len(signatures) == 8, 'unique sampler method signatures')
    if 'point_distributions' in rules:
        points = obj(rules['point_distributions'], ['families', 'operand_types'], 'point distributions')
        _strings_and_bools(points, {
            'syntax': {'<family> { <key>: <value>, ... }'}, 'result': {'distribution'},
            'names': {'contextual_before_brace'}, 'evaluation': {'key_value_pairs_left_to_right_once'},
            'storage': {'immutable_snapshot_original_numeric_types'}, 'intrinsic_rng': {'none'},
            'key_order': {'strictly_increasing'}, 'values': {'finite_nonnegative_positive_total_mass'},
            'bounded_draw': {'one_stream_step_then_truncate_and_select'}, 'numeric_kernel': {'binary64_no_fma'},
            'integer_rounding': {'nearest_half_away_from_zero'}, 'missing_mass': {'trap_after_stream_step'}}, [], 'point distribution policy')
        require(points['operand_types'] == ['i32','f32'], 'point distribution operand types')
        require(points['families'] == [{'name':'weighted','minimum_points':1,'maximum_points':256,
                'value':'discrete_mass','equal_bounds':'positive_mass_at_point'}], 'point distribution families')
        integer(points.get('generation_data_limit_bytes'), 'point distribution data limit')
        require(points['generation_data_limit_bytes'] == 67108864, 'point distribution data limit')
    if 'collections' in rules:
        collection = rules['collections']
        _strings_and_bools(collection, {
            'literal': {'[<elements>]'}, 'empty_literal': {'known_list_type_required'},
            'element_evaluation': {'left_to_right_once'}, 'numeric_elements': {'i32_to_f32_widening'},
            'index_type': {'i32'}, 'index_bounds': {'trap'}, 'helper_transfer': {'share_immutable_data'},
            'legacy_palette_index': {'last_element_fallback'}, 'legacy_palette_length_limit': {'unchanged'}}, ['mutable'], 'collections')
        require(collection['mutable'] is False, 'immutable collections')
        for field, expected in [('minimum_length',0),('maximum_length',256),('palette_as_list_maximum',256),('generation_data_limit_bytes',67108864)]:
            integer(collection.get(field), 'collections.' + field)
            require(collection[field] == expected, 'collections.' + field)
    require(not set(types['storable']) & set(types['output_only_geometry']), 'storable/output-only overlap')
    require(set(types['helper_parameters']) <= set(types['storable']) and set(types['helper_value_returns']) <= set(types['storable']), 'helper types must be storable')
    require(types['point_value_type'] == 'vec2' and 'vec2' in types['storable'], 'point value type')
    require(types['local_reassignment'] == 'exact_type', 'local reassignment')
    # Added in source language 0.6 without changing schema 2. Earlier exports
    # omit this capability list; a present list must still be well formed.
    if 'components' in types:
        require(type(types['components']) is list and bool(types['components']), 'components')
    for item in types.get('components', []):
        obj(item, ['type', 'names', 'result', 'assignable', 'opcodes', 'minimum_format_minor'], 'components')
        require(item['type'] in VALUE_TYPES and item['result'] in VALUE_TYPES and item['assignable'] is False, 'component types')
        strings(item['names'], 'component names', None, True)
        require(type(item['opcodes']) is list and len(item['opcodes']) == len(item['names']) and all(type(op) is str for op in item['opcodes']), 'component opcodes')
        integer(item['minimum_format_minor'], 'minimum_format_minor', maximum=255)
    require(type(types['implicit_widening']) is list, 'implicit widening')
    conversions = []
    for item in types['implicit_widening']:
        obj(item, ['from', 'to', 'contexts'], 'widening')
        require(item['from'] == 'i32' and item['to'] == 'f32' and
                item['from'] in types['storable'] and item['to'] in types['storable'], 'widening types')
        strings(item['contexts'], 'widening contexts', {'numeric_operands', 'call_arguments', 'helper_returns', 'asset_constants'}, True)
        conversions.append((item['from'], item['to']))
    require(len(conversions) == len(set(conversions)), 'unique widening rules')
    _strings_and_bools(rules['locals'], {'declaration': {'first_assignment'}, 'scope': {'function'}, 'type_inference_order': {'source_traversal'}, 'unexecuted_initializer': {'typed_zero'}}, ['definite_assignment_analysis', 'read_requires_prior_assignment'], 'locals')
    visibility = rules['visibility']
    _strings_and_bools(visibility, {'uniforms': {'draw_only'}, 'seed': {'current_rng_state_in_all_functions'}, 'palettes': {'all_asset_functions'}, 'constants': {'all_asset_functions'}}, ['uniforms_read_only', 'helper_parameters_read_only'], 'visibility')
    controls = obj(visibility.get('draw_controls'), ['names', 'type', 'read_only'], 'draw controls')
    strings(controls['names'], 'draw control names', {'v', 'a'}, nonempty=True)
    require(controls['type'] == 'i32', 'draw control type')
    boolean(controls['read_only'], 'draw controls read_only')
    named = rules['named_calls']
    _strings_and_bools(named, {'positional_binding': {'positional_parameter_ordinal'}}, ['mixed_positional_and_named', 'named_arguments_advance_positional_ordinal', 'duplicate_arguments', 'unknown_arguments'], 'named_calls')
    strings(named.get('targets'), 'named call targets', {'builtin', 'helper', 'part'}, True)
    _strings_and_bools(rules['restrictions'], {}, ['recursive_helpers', 'nested_part_imports', 'let_declarations', 'fn_declarations', 'named_geometry_constructors'], 'restrictions')
    _strings_and_bools(rules['effect_order'], {'binary_operands': {'left_to_right'}, 'logical_operators': {'short_circuit_left_to_right'}, 'call_arguments': {'signature_evaluation_order'}, 'part_parent_rng': {'restored_after_child'}}, ['named_arguments_change_evaluation_order'], 'effect_order')
    if 'random_range' in rules:
        ranges = obj(rules['random_range'], ['operator', 'keyword', 'precedence', 'chaining', 'evaluation_order', 'overloads', 'mixed_numeric_bounds', 'traps', 'equal_bounds', 'effects'], 'random_range')
        require(ranges['operator'] == 'to' and ranges['keyword'] == 'contextual' and type(ranges['chaining']) is bool, 'random range operator')
        precedence = obj(ranges['precedence'], ['tighter_than', 'looser_than'], 'random range precedence')
        require(all(type(precedence[key]) is str and precedence[key] for key in precedence), 'random range precedence')
        require(ranges['evaluation_order'] == ['lower', 'upper'], 'random range evaluation order')
        require(type(ranges['overloads']) is list and bool(ranges['overloads']), 'random range overloads')
        for item in ranges['overloads']:
            obj(item, ['lower', 'upper', 'result', 'opcode', 'minimum_format_minor', 'bounds', 'draws'], 'random range overload')
            require(all(item[key] in VALUE_TYPES for key in ['lower', 'upper', 'result']) and type(item['opcode']) is str and item['bounds'] == 'inclusive', 'random range overload types')
            integer(item['minimum_format_minor'], 'minimum_format_minor', maximum=255)
            draws = obj(item['draws'], ['minimum', 'maximum'], 'random range draws')
            integer(draws['minimum'], 'random range draws', 1)
            integer(draws['maximum'], 'random range draws', draws['minimum'])
        require(ranges['mixed_numeric_bounds'] == 'widen_to_f32' and ranges['equal_bounds'] == 'bound_after_one_draw', 'random range rules')
        strings(ranges['traps'], 'random range traps')
        effects(ranges['effects'], 'random range effects')
        # Gradient ends came after the first 0.6 exports; validate when present.
        for item in ranges.get('ends', []):
            obj(item, ['type', 'result', 'coefficient', 'mixed', 'opcode', 'minimum_format_minor', 'effects'], 'random range ends')
            require(item['type'] in {'color', 'vec2'} and item['result'] == item['type'] and item['coefficient'] == 'shared_unit_interval' and type(item['opcode']) is str, 'random range ends')
            strings(item['mixed'], 'random range mixed', None, True)
            integer(item['minimum_format_minor'], 'minimum_format_minor', maximum=255)
            effects(item['effects'], 'random range ends effects')
        # Distributions came after the first 0.6 exports; validate when present.
        if 'with' in ranges:
            shaped = obj(ranges['with'], ['keyword', 'type', 'evaluation_order', 'overloads', 'truncation', 'integer_rounding', 'fuel', 'traps', 'effects'], 'random range with')
            require(shaped['keyword'] == 'with' and shaped['type'] == 'distribution' and shaped['evaluation_order'] == ['lower', 'upper', 'distribution'], 'random range with')
            require(type(shaped['overloads']) is list and bool(shaped['overloads']) and all(type(item) is dict and type(item.get('opcode')) is str for item in shaped['overloads']), 'random range with overloads')
            require(shaped['truncation'] == 'conditioned_on_range' and shaped['integer_rounding'] == 'nearest_half_away_from_zero' and shaped['fuel'] == 'one_plus_kernel_iterations', 'random range with rules')
            strings(shaped['traps'], 'random range with traps')
            effects(shaped['effects'], 'random range with effects')
    call = obj(value['call_rules'], ['helper_evaluation_order', 'part'], 'call_rules')
    require(call['helper_evaluation_order'] == 'declaration', 'helper evaluation order')
    ordered_signature(call['part'], 'part', typed=False)
    effect = obj(value['effect_rules'], ['intrinsic_scope', 'argument_effects', 'resource_traps', 'validation_limits', 'allocation_failure', 'helper', 'part', 'seed_read', 'output_write', 'output_write_policy', 'part_output_read', 'integer_division', 'integer_remainder'], 'effect_rules')
    require(effect['intrinsic_scope'] == 'operation_excluding_arguments_and_resources' and effect['argument_effects'] == 'evaluated_before_operation_in_caller_stream', 'effect scope')
    strings(effect['resource_traps'], 'resource traps', {'fuel_exhausted', 'call_depth_limit', 'generation_data_limit'})
    strings(effect['validation_limits'], 'validation limits', {'operand_stack_limit'})
    boolean(effect['allocation_failure'], 'allocation failure')
    for key in ['helper', 'part', 'seed_read', 'output_write', 'part_output_read', 'integer_division', 'integer_remainder']:
        effects(effect[key], key)
    policy = obj(effect['output_write_policy'], ['singular', 'list'], 'output policy')
    require(policy['singular'] == 'replace' and policy['list'] == 'append', 'output write policy')
    return value


def _strings_and_bools(value, fields, booleans, path):
    obj(value, [*fields, *booleans], path)
    for key, choices in fields.items():
        require(type(value[key]) is str and value[key] in choices, path + '.' + key)
    for key in booleans:
        boolean(value[key], path + '.' + key)


def _range(value, path):
    obj(value, ['start', 'end'], path)
    for point in [value['start'], value['end']]:
        obj(point, ['byte_offset', 'line', 'column'], path)
        integer(point['byte_offset'], path + '.byte_offset')
        integer(point['line'], path + '.line', 1)
        integer(point['column'], path + '.column', 1)
    start, end = value['start'], value['end']
    require(start['byte_offset'] <= end['byte_offset'] and start['line'] <= end['line'] and
            (start['line'] != end['line'] or start['column'] <= end['column']), path + ' ordered range')


def read_diagnostics(value, exit_code=None):
    obj(value, ['schema_version', 'product_version', 'language_version', 'status', 'diagnostics'], 'diagnostics')
    require(type(value['schema_version']) in {int, float} and value['schema_version'] == DIAGNOSTICS_SCHEMA_VERSION, 'diagnostic schema version')
    text(value['product_version'], 'product version')
    version(value['language_version'], 'language version')
    require(type(value['status']) is str and value['status'] in EXITS and (exit_code is None or EXITS[value['status']] == exit_code), 'diagnostic status/exit')
    require(type(value['diagnostics']) is list, 'diagnostic array')
    for item in value['diagnostics']:
        obj(item, ['code', 'stage', 'severity', 'message', 'file', 'location', 'range', 'source_id', 'expected_type', 'actual_type', 'suggestions', 'related_locations', 'fix_its'], 'diagnostic')
        require(re.fullmatch(r'IPS-[A-Z]+-[0-9]{3}', text(item['code'], 'code')), 'diagnostic code')
        require(type(item['stage']) is str and type(item['severity']) is str and item['stage'] in STAGES and item['severity'] in {'error', 'warning', 'note'}, 'diagnostic stage/severity')
        text(item['message'], 'message')
        for key in ['file', 'expected_type', 'actual_type']:
            if item[key] is not None:
                text(item[key], key)
        if item['source_id'] is not None:
            require(re.fullmatch(r'sha256:[0-9a-f]{64}', text(item['source_id'], 'source_id')), 'source_id')
        if item['location'] is not None:
            obj(item['location'], ['line', 'column'], 'location')
            integer(item['location']['line'], 'location line', 1)
            integer(item['location']['column'], 'location column', 1)
        if item['range'] is not None:
            _range(item['range'], 'range')
        require(type(item['suggestions']) is list and all(type(s) is str for s in item['suggestions']), 'suggestions')
        require(type(item['related_locations']) is list and type(item['fix_its']) is list, 'diagnostic arrays')
        for related in item['related_locations']:
            obj(related, ['file', 'message', 'range'], 'related location')
            text(related['file'], 'related file'); text(related['message'], 'related message')
            _range(related['range'], 'related range')
        for fix in item['fix_its']:
            obj(fix, ['range', 'replacement', 'source_id'], 'fix')
            text(fix['replacement'], 'fix replacement')
            require(re.fullmatch(r'sha256:[0-9a-f]{64}', text(fix['source_id'], 'fix source_id')), 'fix source_id')
            _range(fix['range'], 'fix range')
    require(value['status'] != 'ok' or all(d['severity'] != 'error' for d in value['diagnostics']), 'ok status with errors')
    return value


def read_manifest(value):
    obj(value, ['schema_version', 'product_version', 'source_commit', 'working_tree_dirty', 'status', 'tag', 'builtin_count', 'example_count', 'headings', 'source_files', 'files'], 'documentation manifest')
    require(type(value['schema_version']) in {int, float} and
            value['schema_version'] == DOCUMENTATION_SCHEMA_VERSION, 'manifest schema version')
    require(PRODUCT.fullmatch(text(value['product_version'], 'manifest product')), 'manifest product')
    try:
        read_version_matrix(value)
    except RuntimeError as error:
        require(False, str(error))
    require(value['source_commit'] is None or re.fullmatch(r'[0-9a-f]{40}', text(value['source_commit'], 'source commit')), 'source commit')
    boolean(value['working_tree_dirty'], 'dirty')
    require(type(value['status']) is str and value['status'] in {'released', 'development / release candidate'}, 'manifest status')
    require(value['tag'] is None or type(value['tag']) is str, 'manifest tag')
    if value['status'] == 'released':
        require(value['working_tree_dirty'] is False and value['source_commit'] is not None and
                value['tag'] == 'v' + value['product_version'], 'released manifest identity')
    else:
        require(value['tag'] is None, 'candidate manifest tag')
    for key in ['builtin_count', 'example_count']:
        integer(value[key], key, 1)
    require(type(value['headings']) is list and bool(value['headings']), 'manifest headings')
    anchors = []
    for heading in value['headings']:
        obj(heading, ['level', 'title', 'anchor'], 'heading')
        integer(heading['level'], 'heading level', 1, 6)
        text(heading['title'], 'heading title', True)
        anchors.append(text(heading['anchor'], 'heading anchor', True))
    require(len(anchors) == len(set(anchors)), 'unique heading anchors')
    for key in ['files', 'source_files']:
        require(type(value[key]) is dict and bool(value[key]), key)
        for name, digest in value[key].items():
            text(name, 'file name', True)
            path = PurePosixPath(name)
            require(path.parts and not re.match(r'^[A-Za-z]:', name) and not path.is_absolute() and path.as_posix() == name and '\\' not in name and '\0' not in name and
                    all(part not in {'.', '..'} for part in path.parts), 'safe manifest path')
            require(HASH.fullmatch(text(digest, 'file digest')), 'file digest')
    return value
