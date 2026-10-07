"""Session state, exact context, malformed imports and actual one-repair proofs.

Authored fixtures validate the adapter. They are never model measurements.
"""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run
import session
from test_runner import binary_fixture


FIXTURES = Path(__file__).parent / 'fixtures'
BASE_REFERENCE = run.REPO / 'benchmarks/llm/baseline-reference'


def snapshot(root):
    return {str(path.relative_to(root)): session.hash_file(path)
            for path in root.rglob('*') if path.is_file()}


def args_for(root, tools, reference, version='0.4', only=None):
    frozen = version == '0.4'
    return SimpleNamespace(output=root, tools=tools, version=version,
        reference=reference / ('handbook.md' if frozen else 'docs/language-v0.5.md'),
        language_export=reference / ('language.json' if frozen else 'docs/language.json'),
        reference_manifest=reference / ('language-manifest.json' if frozen else 'docs/language-manifest.json'),
        tasks=run.REPO / 'benchmarks/llm/tasks.json', protocol=run.REPO / 'benchmarks/llm/protocol.json',
        record_kind='authored_witness', only=only)


def archived_query(command, _cwd, destination, _timeout, _limits):
    # Unit tests mock only the identity query; malformed inputs then run the
    # real extractor/evaluator and stop before any compiler or VM invocation.
    if command[1:] != ['--describe-language']:
        raise AssertionError('unexpected compiler/VM execution: ' + str(command))
    destination.mkdir(parents=True)
    stdout = (BASE_REFERENCE / 'language.json').read_bytes()
    (destination / 'stdout.bin').write_bytes(stdout)
    (destination / 'stderr.bin').write_bytes(b'')
    result = {'exit_code': 0, 'failure': None}
    run.save_json(destination / 'process.json', result)
    return result, stdout, b''


class ExtractionTests(unittest.TestCase):
    def test_exact_prompt_substitution_does_not_expand_inserted_placeholders(self):
        protocol = {'prompt': {'x': 'A{{reference}}B{{export}}C'}}
        self.assertEqual(session.template(protocol, 'x', {'reference': '{{export}}', 'export': 'raw'}),
                         b'A{{export}}BrawC')

    def test_json_scalar_types_duplicates_and_nonfinite_are_distinct(self):
        for first, second in [(False, 0), (1, 1.0), (True, 1)]:
            self.assertNotEqual(session.canonical_json(first), session.canonical_json(second))
        for raw in [b'{"tasks":[],"tasks":[]}', b'{"tasks":NaN}', b'{"tasks":Infinity}']:
            with self.assertRaises(ValueError): session.parse_json(raw)

    def test_malformed_batch_does_not_keep_a_successful_prefix(self):
        valid = {'task_id': 'asset-01', 'files': {'entry.ips': 'raw\r\n'}}
        for items in [[valid, valid], [valid, {'task_id': 'other'}], [valid, 3]]:
            generated, error = session.extract(session.json_bytes({'tasks': items}), ['asset-01'])
            self.assertEqual(generated, {})
            self.assertIsNotNone(error)
        generated, error = session.extract(session.json_bytes({'tasks': [valid]}), ['asset-01'])
        self.assertIsNone(error)
        self.assertEqual(generated['asset-01']['entry.ips'], 'raw\r\n')
        generated, error = session.extract(b'[' * 2000 + b']' * 2000, ['asset-01'])
        self.assertEqual(generated, {})
        self.assertIsNotNone(error)


class BoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.tools = self.directory / 'tools'; self.tools.mkdir()
        for name in ['ipc', 'ipgen', 'ipa']: (self.tools / name).write_bytes(('unit-' + name).encode())
        self.root = self.directory / 'session'

    def prepare(self, only=None):
        with patch('run.process', archived_query):
            return session.prepare(args_for(self.root, self.tools, BASE_REFERENCE, only=only))

    def response(self, raw):
        path = self.directory / 'response.json'; path.write_bytes(raw)
        return path

    def test_preparation_is_pending_and_context_bytes_are_exact(self):
        state = self.prepare()
        self.assertEqual(state['state'], 'awaiting_initial_response')
        self.assertEqual(state['evaluated_attempts'], 0)
        self.assertIsNone(state['first_pass_compile_success_count'])
        self.assertEqual(len(state['unevaluated_task_ids']), 24)
        self.assertFalse((self.root / 'attempts').exists())
        for name, original in [('handbook.bin', 'handbook.md'), ('language-export.bin', 'language.json')]:
            self.assertEqual((self.root / name).read_bytes(), (BASE_REFERENCE / original).read_bytes())
        protocol = session.parse_json((self.root / 'protocol.json').read_bytes())
        expected = session.template(protocol, 'generation_template', {
            'exact_frozen_reference': (self.root / 'handbook.bin').read_text(),
            'exact_compiler_export': (self.root / 'language-export.bin').read_text(),
            'exact_task_manifest': (self.root / 'tasks.json').read_text()})
        self.assertEqual((self.root / 'prompt-0.txt').read_bytes(), expected)
        request = session.parse_json((self.root / 'request-0.json').read_bytes())
        for key in ['actual_model_call_count', 'token_usage', 'actual_resolved_model', 'model_inference_wall_time_ms']:
            self.assertIsNone(request[key])
        before = snapshot(self.root)
        with self.assertRaisesRegex(ValueError, 'already exists'): self.prepare()
        self.assertEqual(snapshot(self.root), before)

    def test_malformed_actual_import_retains_all_24_missing_rows_verbatim(self):
        self.prepare()
        raw = b' \xff actual malformed response\r\n'
        with patch('run.process', side_effect=AssertionError('no execution for missing source')):
            state = session.import_response(self.root, self.response(raw), 0)
        self.assertEqual(state['state'], 'awaiting_repair_response')
        self.assertEqual(state['initial']['evaluated_task_count'], 24)
        self.assertEqual(state['first_pass_compile_success_count'], 0)
        self.assertEqual((self.root / 'attempts/0/response.json').read_bytes(), raw)
        extraction = session.parse_json((self.root / 'attempts/0/extraction.json').read_bytes())
        self.assertEqual(extraction['status'], 'malformed_response')
        self.assertEqual(len(extraction['missing_task_ids']), 24)
        for task in session.parse_json((self.root / 'tasks.json').read_bytes())['tasks']:
            record = session.parse_json((self.root / f"attempts/0/evaluations/{task['id']}/attempt-0/record.json").read_bytes())
            self.assertEqual(record['generation_status'], 'malformed_response')
            self.assertEqual(record['raw_response_sha256'], run.sha(raw))
            self.assertFalse(record['compile_success'])
        feedback = session.parse_json((self.root / 'repair-feedback.json').read_bytes())
        self.assertEqual(len(feedback['tasks']), 24)
        self.assertTrue(all(item['extraction_error'] for item in feedback['tasks']))

    def test_oversized_unaccepted_response_leaves_preparation_unchanged(self):
        self.prepare(only=['asset-01']); before = snapshot(self.root)
        response = self.response(b'x' * 1048577)
        with self.assertRaisesRegex(ValueError, 'byte ceiling'):
            session.import_response(self.root, response, 0)
        self.assertEqual(snapshot(self.root), before)
        self.assertEqual(session.status(self.root)['state'], 'awaiting_initial_response')
        self.assertEqual(response.stat().st_size, 1048577)

    def test_invalid_surrogate_filename_still_archives_an_explicit_source_failure(self):
        self.prepare(only=['asset-01'])
        raw = session.json_bytes({'tasks': [{'task_id': 'asset-01',
            'files': {'entry.ips': 'safe entry', '\ud800.ips': 'invalid path'}}]})
        with patch('run.process', side_effect=AssertionError('unsafe source must not execute')):
            state = session.import_response(self.root, self.response(raw), 0)
        self.assertEqual(state['state'], 'awaiting_repair_response')
        self.assertEqual(state['initial']['compile_success_count'], 0)
        self.assertEqual((self.root / 'attempts/0/response.json').read_bytes(), raw)
        record = session.parse_json((self.root / 'attempts/0/evaluations/asset-01/attempt-0/record.json').read_bytes())
        self.assertEqual(record['failure_stage'], 'source_preflight')
        self.assertIn('\ud800.ips', record['public_failure_feedback'][0]['message'])

    def test_one_failed_repair_exhausts_budget_and_preserves_existing_attempts(self):
        self.prepare(only=['asset-01'])
        raw = session.json_bytes({'tasks': [{'task_id': 'asset-01', 'files': {'../outside.ips': 'unsafe'}}]})
        response = self.response(raw)
        with patch('run.process', side_effect=AssertionError('unsafe path must stop before compilation')):
            first = session.import_response(self.root, response, 0)
            second = session.import_response(self.root, response, 1)
        self.assertEqual(first['state'], 'awaiting_repair_response')
        self.assertEqual(second['state'], 'repair_budget_exhausted')
        self.assertEqual(second['evaluated_attempts'], 2)
        self.assertFalse((self.directory / 'outside.ips').exists())
        self.assertFalse((self.root / 'request-2.json').exists())
        before = snapshot(self.root)
        for attempt in [0, 1]:
            with self.assertRaisesRegex(ValueError, 'attempt already exists'):
                session.import_response(self.root, response, attempt)
        with self.assertRaisesRegex(ValueError, 'only initial attempt'):
            session.import_response(self.root, response, 2)
        self.assertEqual(snapshot(self.root), before)

    def test_sealed_context_tools_and_attempt_tampering_stop_before_execution(self):
        self.prepare(only=['asset-01'])
        response = self.response(b'{"tasks":[]}')
        for path in [self.root / 'prompt-0.txt', self.tools / 'ipc']:
            original = path.read_bytes(); path.write_bytes(original + b'changed')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                session.import_response(self.root, response, 0)
            self.assertFalse((self.root / 'attempts').exists())
            path.write_bytes(original)
        session.import_response(self.root, response, 0)
        original = (self.root / 'attempts/0/response.json').read_bytes()
        (self.root / 'attempts/0/response.json').write_bytes(original + b' ')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'): session.status(self.root)

    def test_interrupted_evaluation_and_feedback_are_explicit_and_not_replayable(self):
        self.prepare(only=['asset-01'])
        response = self.response(b'{"tasks":[]}')
        real_write = session.write_new
        def fail_seal(path, data):
            if path.name == 'repair-seal.json': raise OSError('injected disk failure')
            real_write(path, data)
        with patch('session.write_new', fail_seal), self.assertRaisesRegex(OSError, 'disk failure'):
            session.import_response(self.root, response, 0)
        self.assertEqual(session.status(self.root)['state'], 'repair_preparation_incomplete')
        with self.assertRaises(OSError): session.replay(self.root, self.directory / 'replay')
        # An interrupted attempt is also distinct from an evaluated failure.
        other = self.directory / 'interrupted'
        with patch('run.process', archived_query): session.prepare(args_for(other, self.tools, BASE_REFERENCE))
        (other / 'attempts/0').mkdir(parents=True)
        self.assertEqual(session.status(other)['state'], 'initial_evaluation_incomplete')
        self.assertIsNone(session.status(other)['first_pass_compile_success_count'])
        with self.assertRaisesRegex(ValueError, 'incomplete'): session.replay(other, self.directory / 'other-replay')

    def test_compact_or_semantically_changed_reference_cannot_replace_full_context(self):
        reference = self.directory / 'reference'; shutil.copytree(BASE_REFERENCE, reference)
        (reference / 'handbook.md').write_bytes(b'compact reference')
        with self.assertRaisesRegex(ValueError, 'full handbook/export'):
            session.prepare(args_for(self.root, self.tools, reference))
        self.assertFalse(self.root.exists())
        shutil.copyfile(BASE_REFERENCE / 'handbook.md', reference / 'handbook.md')
        original = session.parse_json((BASE_REFERENCE / 'language.json').read_bytes())
        def changed_query(*arguments):
            result, stdout, stderr = archived_query(*arguments)
            value = copy.deepcopy(original); value['schema_version'] = 1.0
            return result, session.json_bytes(value), stderr
        with patch('run.process', changed_query), self.assertRaisesRegex(ValueError, 'compiler contract'):
            session.prepare(args_for(self.root, self.tools, reference))

    def test_actual_model_context_requires_clean_committed_reference(self):
        reference = self.directory / 'reference'; shutil.copytree(BASE_REFERENCE, reference)
        manifest_path = reference / 'language-manifest.json'
        manifest = session.parse_json(manifest_path.read_bytes())
        manifest['status'] = 'development / release candidate'
        manifest['tag'] = None
        manifest['working_tree_dirty'] = True
        manifest_path.write_bytes(session.json_bytes(manifest))
        arguments = args_for(self.root, self.tools, reference)
        arguments.record_kind = 'actual_model_output'
        with self.assertRaisesRegex(ValueError, 'committed, clean'):
            session.prepare(arguments)
        self.assertFalse(self.root.exists())

    def test_bad_modern_check_contract_never_falls_back_to_compile_or_vm(self):
        task = session.parse_json((run.REPO / 'benchmarks/llm/tasks.json').read_bytes())['tasks'][0]
        protocol = session.parse_json((run.REPO / 'benchmarks/llm/protocol.json').read_bytes())
        cases = [b'{"status":"ok","diagnostics":[]}',
                 b'{"schema_version":1,"schema_version":1,"status":"ok","diagnostics":[]}']
        for index, raw in enumerate(cases):
            calls = []
            def bad_check(command, *_arguments):
                calls.append(command)
                return {'exit_code': 0, 'failure': None}, raw, b''
            args = SimpleNamespace(version='0.5', tools=self.tools, attempt=0, record_kind='authored_witness',
                                   diagnostic_reader=session.read_diagnostics, diagnostic_parser=session.parse_json)
            with patch('run.process', bad_check):
                record = run.evaluate(task, {'entry.ips': 'asset t {size 32x32 draw{}}'}, args,
                                      self.directory / str(index), protocol)
            self.assertEqual(record['failure_stage'], 'check_protocol')
            self.assertFalse(record['compile_success'])
            self.assertEqual(len(calls), 1)
            self.assertIn('--check', calls[0])

    def test_repair_payload_cannot_include_previously_successful_or_unselected_tasks(self):
        self.prepare(only=['asset-01'])
        session.import_response(self.root, self.response(b'{"tasks":[]}'), 0)
        raw = session.json_bytes({'tasks': [{'task_id': 'asset-02', 'files': {'entry.ips': 'unselected'}}]})
        with patch('run.process', side_effect=AssertionError('bad repair must stop before execution')):
            state = session.import_response(self.root, self.response(raw), 1)
        self.assertEqual(state['state'], 'repair_budget_exhausted')
        self.assertEqual(state['repair']['evaluated_task_count'], 1)
        extraction = session.parse_json((self.root / 'attempts/1/extraction.json').read_bytes())
        self.assertEqual(extraction['status'], 'malformed_response')
        self.assertEqual(extraction['missing_task_ids'], ['asset-01'])

    def test_completed_manifest_rejects_extra_artifact_and_changed_adapter(self):
        self.prepare(only=['asset-01'])
        response = self.response(b'{"tasks":[]}')
        session.import_response(self.root, response, 0)
        extra = self.root / 'attempts/0/unrecorded.txt'; extra.write_bytes(b'not sealed')
        with self.assertRaisesRegex(ValueError, 'artifact manifest'): session.status(self.root)
        extra.unlink()
        original = session.hash_file
        def changed_adapter(path):
            return '0' * 64 if path.name == 'session.py' else original(path)
        with patch('session.hash_file', changed_adapter), self.assertRaisesRegex(ValueError, 'adapter source identity'):
            session.import_response(self.root, response, 1)
        self.assertFalse((self.root / 'attempts/1').exists())

    def test_over_fuel_and_canvas_rejected_before_vm_execution(self):
        task = session.parse_json((run.REPO / 'benchmarks/llm/tasks.json').read_bytes())['tasks'][0]
        protocol = session.parse_json((run.REPO / 'benchmarks/llm/protocol.json').read_bytes())
        for index, binary in enumerate([binary_fixture(fuel=1000001), binary_fixture(width=65), binary_fixture(width=31)]):
            calls = []
            def compile_only(command, *_arguments):
                calls.append(command)
                self.assertEqual(Path(command[0]).name, 'ipc')
                if '-o' in command: Path(command[command.index('-o') + 1]).write_bytes(binary)
                return {'exit_code': 0, 'failure': None}, b'', b''
            args = SimpleNamespace(version='0.4', tools=self.tools, attempt=0, record_kind='authored_witness')
            with patch('run.process', compile_only):
                record = run.evaluate(task, {'entry.ips': 'asset t {size 32x32 draw{}}'}, args,
                                      self.directory / str(index), protocol)
            self.assertEqual(record['overall_automated_verdict'], 'fail')
            self.assertEqual(len(calls), 1)
            self.assertEqual(record['render_records'], [])


@unittest.skipUnless(os.name == 'posix' and os.environ.get('IPROP_COMPILER'),
                     'actual POSIX orchestration requires IPROP_COMPILER')
class ActualCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.tools = Path(os.environ['IPROP_COMPILER']).resolve().parent
        cls.reference = cls.directory / 'reference'
        result = subprocess.run([os.sys.executable, str(run.REPO / 'scripts/language_docs.py'), 'build',
            '--compiler', str(cls.tools / 'ipc'), '--output-dir', str(cls.reference),
            '--report-dir', str(cls.directory / 'doc-check')], capture_output=True, text=True)
        if result.returncode: raise RuntimeError(result.stdout + result.stderr)

    def setUp(self):
        self.case = Path(tempfile.mkdtemp(dir=self.directory))
        self.root = self.case / 'session'

    def prepare(self, version='0.5'):
        tools = self.tools if version == '0.5' else Path(os.environ['IPROP_FROZEN_TOOLS'])
        reference = self.reference if version == '0.5' else BASE_REFERENCE
        return session.prepare(args_for(self.root, tools, reference, version, only=['asset-01']))

    def record(self, attempt, root=None):
        return session.parse_json(((root or self.root) /
            f'attempts/{attempt}/evaluations/asset-01/attempt-{attempt}/record.json').read_bytes())

    def assert_successful_render(self, record):
        self.assertEqual(record['overall_automated_verdict'], 'pass')
        self.assertEqual(len(record['render_records']), 8)
        self.assertTrue(all(item['verdict'] == 'pass' for item in record['render_records']))
        self.assertTrue(all(item['verdict'] == 'pass' for item in record['repeat_verdicts']))
        for render in record['render_records']:
            outputs = session.parse_json(Path(render['normalized_outputs_path']).read_bytes())
            self.assertEqual(outputs[0]['values_bits'], [[1098907648, 1098907648]])
            self.assertEqual((render['width'], render['height']), (32, 32))

    def test_real_reference_manifest_contract_before_output_or_tool_query(self):
        versions = ['0.5']
        if os.environ.get('IPROP_FROZEN_TOOLS'):
            versions.append('0.4')
        for version in versions:
            tools = self.tools if version == '0.5' else Path(os.environ['IPROP_FROZEN_TOOLS'])
            original = self.reference if version == '0.5' else BASE_REFERENCE
            reference = self.case / ('manifest-reference-' + version)
            shutil.copytree(original, reference)
            manifest_path = reference / ('docs/language-manifest.json' if version == '0.5' else 'language-manifest.json')
            manifest = session.parse_json(manifest_path.read_bytes())
            for case in ['unknown_schema', 'missing_required', 'wrong_known_type']:
                broken = copy.deepcopy(manifest)
                if case == 'unknown_schema':
                    broken['schema_version'] = manifest['schema_version'] + 1
                elif case == 'missing_required':
                    del broken['headings']
                else:
                    broken['builtin_count'] = str(broken['builtin_count'])
                manifest_path.write_bytes(session.json_bytes(broken))
                output = self.case / (version + '-' + case)
                arguments = args_for(output, tools, reference, version, only=['asset-01'])
                with self.subTest(version=version, case=case):
                    with patch('run.process', side_effect=AssertionError('invalid manifest must stop before tools')):
                        with self.assertRaisesRegex(RuntimeError, 'Invalid export contract'):
                            session.prepare(arguments)
                    self.assertFalse(output.exists())
            extended = {**manifest, 'future_extension': {'values': [None, True, 'unknown']}}
            exact = session.json_bytes(extended)
            manifest_path.write_bytes(exact)
            output = self.case / (version + '-additive')
            with self.subTest(version=version, case='additive_extension'):
                state = session.prepare(args_for(output, tools, reference, version, only=['asset-01']))
                self.assertEqual(state['state'], 'awaiting_initial_response')
                self.assertEqual(state['evaluated_attempts'], 0)
                self.assertEqual((output / 'reference-manifest.json').read_bytes(), exact)

    def test_real_failure_one_corrected_attempt_and_checked_offline_replay(self):
        self.prepare()
        initial = FIXTURES / 'initial-failure.json'; repair = FIXTURES / 'one-repair.json'
        first = session.import_response(self.root, initial, 0)
        self.assertEqual(first['state'], 'awaiting_repair_response')
        failed = self.record(0)
        self.assertEqual(failed['failure_stage'], 'check')
        self.assertTrue(failed['normalized_diagnostics']['diagnostics'])
        self.assertEqual((self.root / 'attempts/0/response.json').read_bytes(), initial.read_bytes())
        before_repair = {name: session.hash_file(self.root / name) for name in session.INPUT_NAMES}
        final = session.import_response(self.root, repair, 1)
        self.assertEqual(final['state'], 'completed_after_one_repair')
        self.assertEqual(final['first_pass_compile_success_count'], 0)
        self.assertEqual(final['latest_compile_success_count'], 1)
        self.assertEqual(final['required_suite_task_count'], 24)
        self.assertEqual(len(final['unevaluated_task_ids']), 23)
        record = self.record(1); self.assert_successful_render(record)
        source = session.parse_json(repair.read_bytes())['tasks'][0]['files']['entry.ips'].encode('utf-8')
        self.assertEqual(record['generated_file_sha256']['entry.ips'], run.sha(source))
        self.assertEqual((self.root / 'attempts/1/evaluations/asset-01/attempt-1/sources/entry.ips').read_bytes(), source)
        self.assertEqual(before_repair, {name: session.hash_file(self.root / name) for name in session.INPUT_NAMES})
        before = snapshot(self.root); destination = self.case / 'replay'
        replayed = session.replay(self.root, destination)
        self.assertEqual(replayed['state'], final['state'])
        self.assertEqual(snapshot(self.root), before)
        replay_record = self.record(1, destination); self.assert_successful_render(replay_record)
        for original, replay in zip(record['render_records'], replay_record['render_records']):
            for field in ['decoded_rgba_sha256', 'outputs_sha256', 'fuel_used', 'criterion_verdicts']:
                self.assertEqual(original[field], replay[field])
        self.assertEqual((destination / 'attempts/1/response.json').read_bytes(), repair.read_bytes())
        self.assertFalse((self.root / 'request-2.json').exists())

    def test_real_success_needs_no_repair_and_cannot_be_overwritten(self):
        self.prepare()
        final = session.import_response(self.root, FIXTURES / 'one-repair.json', 0)
        self.assertEqual(final['state'], 'completed_without_repair')
        self.assertEqual(final['first_pass_automated_success_count'], 1)
        self.assert_successful_render(self.record(0))
        self.assertFalse((self.root / 'request-1.json').exists())
        before = snapshot(self.root)
        with self.assertRaisesRegex(ValueError, 'no evaluated failures'):
            session.import_response(self.root, FIXTURES / 'initial-failure.json', 1)
        self.assertEqual(snapshot(self.root), before)

    @unittest.skipUnless(os.environ.get('IPROP_FROZEN_TOOLS'), 'actual frozen released tools not supplied')
    def test_actual_frozen_04_retains_legacy_compile_and_diagnostic_contract(self):
        self.prepare('0.4')
        first = session.import_response(self.root, FIXTURES / 'initial-failure.json', 0)
        self.assertEqual(first['initial']['compile_success_count'], 0)
        failed = self.record(0)
        self.assertEqual(failed['failure_stage'], 'compile')
        self.assertNotIn('check_process', failed)
        self.assertTrue(failed['normalized_diagnostics']['diagnostics'])
        self.assertTrue(all(item['code'] is None for item in failed['normalized_diagnostics']['diagnostics']))
        final = session.import_response(self.root, FIXTURES / 'one-repair.json', 1)
        self.assertEqual(final['state'], 'completed_after_one_repair')
        self.assert_successful_render(self.record(1))
        self.assertNotIn('formatting', self.record(1))
        self.assertNotIn('check_process', self.record(1))
        self.assertEqual((self.root / 'language-export.bin').read_bytes(), (BASE_REFERENCE / 'language.json').read_bytes())
        self.assertNotEqual(session.hash_file(self.root / 'compiler-language-export.bin'),
                            session.hash_file(self.root / 'language-export.bin'))


if __name__ == '__main__':
    unittest.main()
