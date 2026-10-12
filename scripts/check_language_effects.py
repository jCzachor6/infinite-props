#!/usr/bin/env python3
"""Validate effect coverage on every exported shared-registry signature."""
import json
from pathlib import Path
import subprocess
import sys


def main():
    compiler = Path(sys.argv[1]).resolve()
    result = subprocess.run([str(compiler), '--describe-language'], check=True,
                            capture_output=True, text=True, timeout=30)
    assert not result.stderr, result.stderr
    description = json.loads(result.stdout)
    rng = {'none', 'read_current', 'advance_current', 'push_seeded_stream', 'restore_stream',
           'seeded_computation', 'isolated_seeded_stream', 'shared_callee_stream', 'isolated_callee_stream'}
    checks = {'numeric_range', 'stream_stack_underflow', 'part_render_arguments', 'callee',
              'part_output_missing', 'divide_by_zero', 'integer_overflow', 'part_output_context'}

    def valid(effects):
        assert set(effects) == {'rng', 'writes_canvas', 'writes_outputs', 'summary', 'may_trap'}, effects
        assert effects['rng'] in rng, effects
        assert type(effects['writes_canvas']) is bool and type(effects['writes_outputs']) is bool, effects
        assert effects['summary'] in {'intrinsic', 'conservative'}, effects
        assert isinstance(effects['may_trap'], list), effects
        assert len(effects['may_trap']) == len(set(effects['may_trap'])), effects
        assert set(effects['may_trap']) <= checks, effects

    # Enumerate the export, rather than maintaining another signature catalogue.
    # The C++ registry test checks the same metadata against every real overload
    # and executes every current-stream/seeded and numeric-check overload.
    opcode_effects = {}
    signatures = 0
    for builtin in description['builtins']:
        entries = [*builtin['overloads']]
        if 'variadic' in builtin:
            entries.append(builtin['variadic'])
        assert entries, builtin
        for entry in entries:
            signatures += 1
            effects = entry['effects']
            valid(effects)
            assert effects['summary'] == 'intrinsic', builtin
            if 'opcode' in entry:
                previous = opcode_effects.setdefault(entry['opcode'], effects)
                assert previous == effects, builtin
            if entry['result'] == 'void':
                assert effects['writes_canvas'] or effects['writes_outputs'] or effects['rng'] != 'none', builtin
        if 'variadic' in builtin and builtin['variadic']['repeat'] == 'choices':
            assert builtin['variadic']['choice_evaluation'] == 'eager_once_before_selection_rng', builtin
            assert builtin['variadic']['effects']['rng'] in {'advance_current', 'isolated_seeded_stream'}, builtin

    rules = description['effect_rules']
    assert rules['intrinsic_scope'] == 'operation_excluding_arguments_and_resources'
    assert rules['argument_effects'] == 'evaluated_before_operation_in_caller_stream'
    assert rules['resource_traps'] == ['fuel_exhausted', 'call_depth_limit']
    assert rules['validation_limits'] == ['operand_stack_limit']
    assert rules['allocation_failure'] is True
    assert rules['output_write_policy'] == {'singular': 'replace', 'list': 'append'}
    for name in ('helper', 'part', 'seed_read', 'output_write', 'part_output_read', 'integer_division', 'integer_remainder'):
        valid(rules[name])
    for name in ('helper', 'part'):
        assert rules[name]['summary'] == 'conservative'
        assert rules[name]['writes_canvas'] and rules[name]['writes_outputs']
        assert 'callee' in rules[name]['may_trap']
    assert rules['helper']['rng'] == 'shared_callee_stream'
    assert rules['part']['rng'] == 'isolated_callee_stream'
    assert 'part_render_arguments' in rules['part']['may_trap']
    assert rules['seed_read']['rng'] == 'read_current'
    assert rules['output_write']['writes_outputs'] and not rules['output_write']['writes_canvas']
    assert rules['part_output_read']['may_trap'] == ['part_output_missing', 'part_output_context']
    assert rules['integer_division']['may_trap'] == ['divide_by_zero', 'integer_overflow']
    assert rules['integer_remainder']['may_trap'] == ['divide_by_zero']

    # Selected observable boundaries are regression examples, not signatures.
    by_name = {builtin['name']: builtin for builtin in description['builtins']}
    for name in ('unit', 'index', 'range', 'chance'):
        assert by_name[name]['overloads'][0]['effects']['rng'] == 'advance_current', name
    for name in ('roll', 'choose'):
        signature = by_name[name]['overloads'][0] if by_name[name]['overloads'] else by_name[name]['variadic']
        assert signature['effects']['rng'] == 'isolated_seeded_stream', name
    assert by_name['pick']['variadic']['effects']['rng'] == 'advance_current'
    assert by_name['weather']['overloads'][0]['effects']['rng'] == 'seeded_computation'
    assert by_name['weather']['overloads'][0]['effects']['writes_canvas']
    assert by_name['abs']['overloads'][0]['effects']['may_trap'] == []
    assert by_name['round']['overloads'][0]['effects']['may_trap'] == []
    for name in ('min', 'max', 'clamp'):
        assert {entry['result'] for entry in by_name[name]['overloads']} == {'i32', 'f32'}
        for entry in by_name[name]['overloads']:
            assert entry['effects']['rng'] == 'none' and not entry['effects']['may_trap']
    normalize = by_name['vec2.normalize']['overloads'][0]
    assert normalize['minimum_format_minor'] == 6 and normalize['result'] == 'vec2'
    assert normalize['effects']['rng'] == 'none' and normalize['effects']['may_trap'] == ['numeric_range']
    assert by_name['stream_pop']['overloads'][0]['effects']['may_trap'] == ['stream_stack_underflow']
    print(f'Language effects: {len(by_name)} builtins, {signatures} signatures and shared effect rules passed')


if __name__ == '__main__':
    main()
