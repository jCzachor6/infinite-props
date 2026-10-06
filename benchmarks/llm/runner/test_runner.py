"""Meaningful decoder, budget, file-boundary and predicate regressions."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

from binary import PreflightError, preflight
from criteria import bits, image_checks, normalize_outputs, output_checks
from png import decode
from run import evaluate, format_candidate, read_json, safe_sources, sha, unique_object


LIMITS = {'max_compiled_binary_bytes': 2097152, 'max_root_width': 64, 'max_root_height': 64, 'max_root_pixels': 4096, 'max_part_width': 64, 'max_part_height': 64, 'max_part_count': 4, 'max_declared_fuel': 1000000, 'max_output_elements_per_render': 256, 'max_generated_files_per_task': 8, 'max_source_bytes_per_file': 65536, 'max_total_source_bytes_per_task': 262144}


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def png_fixture(width, height, channels):
    rows = [bytes((x * 37 + y * 91 + x * y * 3) & 255 for x in range(width * channels)) for y in range(height)]
    encoded = bytearray()
    previous = bytes(width * channels)
    for y, row in enumerate(rows):
        kind = y % 5
        encoded.append(kind)
        for index, value in enumerate(row):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if kind == 0: predictor = 0
            elif kind == 1: predictor = left
            elif kind == 2: predictor = up
            elif kind == 3: predictor = (left + up) // 2
            else:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if pa <= pb and pa <= pc else up if pb <= pc else upper_left
            encoded.append((value - predictor) & 255)
        previous = row
    header = struct.pack('>IIBBBBB', width, height, 8, 6 if channels == 4 else 2, 0, 0, 0)
    file = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(encoded)) + chunk(b'IEND', b'')
    expected = b''.join(rows) if channels == 4 else b''.join(row[index:index + 3] + b'\xff' for row in rows for index in range(0, len(row), 3))
    return file, expected


def binary_fixture(width=32, fuel=1000000):
    # A structural parser fixture, not proof of opcode/VM validation.
    meta = struct.pack('<IIffIIQH6xHH', width, 32, 0, 0, 1, 1, fuel, 0, 0, 0)
    constants = struct.pack('<II', 1, 4) + b'draw' + struct.pack('<III', 0, 0, 0)
    code = struct.pack('<IIIIH', 1, 0, 0, 4, 2) + b'\0\0' + struct.pack('<HH', 0, 0) + b'\0' * 4 + struct.pack('<I', 0)
    sections = [meta, constants, code]
    header = b'IPRP' + struct.pack('<HHIII', 0, 4, 0, 3, 0)
    offset, table = 56, bytearray()
    for index, data in enumerate(sections, 1):
        table.extend(struct.pack('<III', index, offset, len(data)))
        offset += len(data)
    return header + table + b''.join(sections)


class DecoderTests(unittest.TestCase):
    def test_all_png_filters_reconstruct_previous_bytes_rgb_and_rgba(self):
        for channels in (3, 4):
            data, expected = png_fixture(7, 10, channels)
            self.assertEqual(decode(data), (7, 10, expected))

    def test_png_crc_truncation_trailing_and_dimensions_rejected(self):
        data, _ = png_fixture(7, 10, 4)
        for bad in (data[:-1], data + b'extra', data[:45] + bytes([data[45] ^ 1]) + data[46:]):
            with self.assertRaises(ValueError): decode(bad)
        with self.assertRaises(ValueError): decode(data, 69)


class BudgetTests(unittest.TestCase):
    def test_plain_and_compressed_metadata_and_schema(self):
        plain = binary_fixture()
        parsed = preflight(plain, LIMITS)
        self.assertEqual((parsed['width'], parsed['height'], parsed['fuel']), (32, 32, 1000000))
        envelope = b'IPRP' + struct.pack('<HHIII', 0, 4, 2, 3, 0) + struct.pack('<I', len(plain)) + zlib.compress(plain)
        self.assertTrue(preflight(envelope, LIMITS)['compressed'])
        for bad in (envelope + b'extra', envelope[:-1], envelope[:20] + struct.pack('<I', 3000000) + envelope[24:]):
            with self.assertRaises((PreflightError, zlib.error)): preflight(bad, LIMITS)

    def test_root_size_fuel_and_section_overlap_rejected(self):
        for bad in (binary_fixture(width=65), binary_fixture(fuel=1000001)):
            with self.assertRaises(PreflightError): preflight(bad, LIMITS)
        data = bytearray(binary_fixture())
        struct.pack_into('<I', data, 36, 56)
        with self.assertRaises(PreflightError): preflight(data, LIMITS)

    def test_safe_bundled_sources_and_no_external_imports(self):
        task = {'entry_file': 'entry.ips'}
        good = {'entry.ips': 'import t from "part.v1.ips"\nasset a {}', 'part.v1.ips': 'asset p {}'}
        self.assertEqual(set(safe_sources(good, task, LIMITS)), set(good))
        for bad in ({'entry.ips': 'import t from "../secret.ips" asset a {}'}, {'entry.ips': '🔗 t from "/etc/secret.ips" asset a {}'}, {'entry.ips': 'asset a {}', '../outside.ips': ''}, {'entry.ips': 'x' * 65537}, {'entry.ips': 'import t from "part.ips" asset a {}', 'part.ips': 'import t from "entry.ips" asset p {}'}):
            with self.assertRaises(ValueError): safe_sources(bad, task, LIMITS)
        with self.assertRaises(ValueError): json.loads('{"files":{},"files":{}}', object_pairs_hook=unique_object)


class PredicateTests(unittest.TestCase):
    def test_owner_duplicate_names_and_binary32_values(self):
        schema = {'inputs': [], 'output_slots': [{'slot': 0, 'owner': 0, 'name': 'anchor', 'kind': 'point', 'is_list': False}, {'slot': 1, 'owner': 1, 'name': 'anchor', 'kind': 'point', 'is_list': False}]}
        raw = {'outputs': [{'name': 'anchor', 'kind': 'point', 'list': False, 'values': [{'x': bits(12), 'y': bits(15)}]}, {'name': 'anchor', 'kind': 'point', 'list': False, 'values': [{'x': bits(2), 'y': bits(3)}]}]}
        outputs = normalize_outputs(raw, schema, 256)
        task = {'output_criteria': [{'owner': 0, 'name': 'anchor', 'kind': 'point', 'is_list': False, 'count': 1, 'values': [12, 15]}, {'owner': 1, 'name': 'anchor', 'kind': 'point', 'is_list': False, 'count': 1, 'values': [2, 3]}]}
        case = {'id': 's0', 'seed': 0, 'variant': 0, 'archetype': 0, 'uniforms': {}}
        self.assertTrue(all(value['verdict'] == 'pass' for value in output_checks(task, case, schema, outputs)))
        raw['outputs'][1]['values'][0]['y'] = bits(15)
        outputs = normalize_outputs(raw, schema, 256)
        self.assertEqual(output_checks(task, case, schema, outputs)[1]['verdict'], 'fail')
        with self.assertRaises(ValueError): normalize_outputs(raw, schema, 1)
        raw['outputs'][0]['values'][0]['x'] = True
        with self.assertRaises(ValueError): normalize_outputs(raw, schema, 256)

    def test_compositor_whole_image_detects_unchecked_pixel_changes(self):
        task = {'image_criteria': {'dimensions': [2, 2], 'pixel_rules': [{'kind': 'frozen_compositor_rect', 'x': 0, 'y': 0, 'width': 1, 'height': 1, 'destination_rgba_hex': '204060ff', 'source_rgba_hex': 'ff000080'}]}}
        case, schema = {'seed': 0, 'variant': 0, 'archetype': 0, 'uniforms': {}}, {'inputs': []}
        mixed = bytes([(255 * 128 + 32 * 127) // 255, 64 * 127 // 255, 96 * 127 // 255, 255])
        pixels = mixed + bytes.fromhex('204060ff') * 3
        self.assertTrue(all(value['verdict'] == 'pass' for value in image_checks(task, case, schema, [], 2, 2, pixels)))
        corrupted = pixels[:-1] + b'\0'
        self.assertEqual(image_checks(task, case, schema, [], 2, 2, corrupted)[1]['verdict'], 'fail')


class FormattingTests(unittest.TestCase):
    def test_format_failure_keeps_raw_compile_success_and_first_pass_feedback(self):
        task = {'id': 'test', 'definition_sha256': 'authored-harness', 'entry_file': 'entry.ips',
                'required_features': [], 'render_cases': [],
                'image_criteria': {'dimensions': [32, 32], 'pixel_rules': [], 'human_visual_review_required': False}}
        budgets = {**LIMITS, 'per_task_compile_wall_timeout_seconds': 15}
        files = {'entry.ips': '// exact raw\r\nasset t { size 32x32 draw{} }\r\n'}
        for version in ['0.4', '0.5']:
            commands = []

            def fake_process(command, _cwd, _destination, _timeout, _limits):
                commands.append(command)
                exit_code = 2 if '--format-only' in command else 0
                if '-o' in command:
                    Path(command[command.index('-o') + 1]).write_bytes(binary_fixture())
                return {'exit_code': exit_code, 'failure': None}, b'{"status":"ok","diagnostics":[]}', b'unsupported format' if exit_code else b''

            with tempfile.TemporaryDirectory() as directory, patch('run.process', fake_process):
                root = Path(directory)
                args = SimpleNamespace(version=version, tools=root / 'tools', attempt=0, record_kind='authored_witness')
                record = evaluate(task, files, args, root, {'budgets': budgets})
                self.assertTrue(record['compile_success'])
                self.assertEqual(record['overall_automated_verdict'], 'pass')
                self.assertIsNone(record['failure_stage'])
                self.assertEqual(record['public_failure_feedback'], [])
                self.assertEqual(record['actual_generated_files'], files)
                raw = files['entry.ips'].encode('utf-8')
                self.assertEqual(record['generated_file_sha256'], {'entry.ips': sha(raw)})
                self.assertEqual((root / 'test/attempt-0/sources/entry.ips').read_bytes(), raw)
                if version == '0.4':
                    self.assertNotIn('formatting', record)
                    self.assertEqual(len(commands), 1)
                    self.assertNotIn('--check', commands[0])
                else:
                    self.assertEqual(record['formatting']['verdict'], 'fail')
                    self.assertEqual(record['formatting']['failure_stage'], 'format')
                    self.assertEqual(len(commands), 3)

    def test_raw_compile_failure_does_not_invoke_formatting(self):
        task = {'id': 'test', 'definition_sha256': 'authored-harness', 'entry_file': 'entry.ips',
                'required_features': [], 'image_criteria': {'human_visual_review_required': False}}
        commands = []
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = SimpleNamespace(version='0.5', tools=root / 'tools', attempt=0, record_kind='authored_witness')

            def fail(command, *_args):
                commands.append(command)
                return {'exit_code': 1, 'failure': None}, b'{"status":"invalid_source","diagnostics":[]}', b'actual compile error'

            with patch('run.process', fail):
                record = evaluate(task, {'entry.ips': 'invalid source'}, args, root,
                                  {'budgets': {**LIMITS, 'per_task_compile_wall_timeout_seconds': 15}})
            self.assertFalse(record['compile_success'])
            self.assertNotIn('formatting', record)
            self.assertEqual(len(commands), 1)

    def test_canonical_binary_difference_and_non_idempotence_are_reported_separately(self):
        for failure in ['binary', 'idempotence', 'source_ceiling']:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                workspace = root / 'sources'; workspace.mkdir()
                (workspace / 'entry.ips').write_bytes(b'raw source\r\n')

                def fake_process(command, _cwd, _destination, _timeout, _limits):
                    if '--format-only' in command:
                        destination = Path(command[command.index('--format') + 1])
                        data = b'canonical\n'
                        if failure == 'idempotence' and destination.name == 'entry-again.ips': data += b'changed'
                        if failure == 'source_ceiling': data = b'x' * (LIMITS['max_source_bytes_per_file'] + 1)
                        destination.write_bytes(data)
                    else:
                        Path(command[command.index('-o') + 1]).write_bytes(binary_fixture(width=31 if failure == 'binary' else 32))
                    return {'exit_code': 0, 'failure': None}, b'', b''

                with patch('run.process', fake_process):
                    stage = format_candidate({'entry_file': 'entry.ips'}, workspace, root, root / 'tools',
                                             binary_fixture(), {**LIMITS, 'per_task_compile_wall_timeout_seconds': 15})
                self.assertEqual(stage['verdict'], 'fail')
                self.assertEqual(stage['failure_stage'], {'binary': 'binary_equivalence', 'idempotence': 'idempotence', 'source_ceiling': 'format'}[failure])
                self.assertEqual((workspace / 'entry.ips').read_bytes(), b'raw source\r\n')

    @unittest.skipUnless(os.name == 'posix' and os.environ.get('IPROP_COMPILER'),
                         'actual runner process adapter requires POSIX and IPROP_COMPILER')
    def test_real_candidate_formats_imports_comments_signed_defaults_without_changing_raw_artifacts(self):
        compiler = Path(os.environ['IPROP_COMPILER']).resolve()
        budgets = read_json(Path(__file__).resolve().parents[1] / 'protocol.json')['budgets']
        files = {'entry.ips': ('import first from "part.ips" /* first alias */\r\n'
                              'import second from /* second path */ "part.ips"\r\n'
                              'asset t {size 32x32 input i32 n=-7 input f32 zero=-0.0 draw {\r\n'
                              'first.draw(a=a,/* before seed */seed=seed,v=v,at={0,0});\r\n'
                              'second.draw(seed,v,a,at={1,1});}}\r\n'),
                 'part.ips': 'asset p {size 2x2 input f32 zero=-0.0 draw {pixel(0,0,#f00)}}\n'}
        with tempfile.TemporaryDirectory(prefix='iprop-runner-format-') as directory:
            root = Path(directory); workspace = root / 'źródła'; workspace.mkdir()
            for name, source in files.items(): (workspace / name).write_bytes(source.encode('utf-8'))
            output = root / 'raw.iprop'
            result = subprocess.run([str(compiler), 'entry.ips', '-o', str(output)], cwd=workspace,
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            binary = output.read_bytes()
            stage = format_candidate({'entry_file': 'entry.ips'}, workspace, root, compiler.parent, binary, budgets)
            self.assertEqual(stage['verdict'], 'pass', stage)
            self.assertTrue(stage['stripped_binary_equal'])
            self.assertTrue(stage['idempotent'])
            canonical = Path(stage['canonical_source_path']).read_text(encoding='utf-8')
            self.assertEqual(canonical.count('/* before seed */'), 1)
            self.assertIn('../źródła/part.ips', canonical)
            self.assertEqual(Path(stage['canonical_binary_path']).read_bytes(), binary)
            self.assertEqual(output.read_bytes(), binary)
            for name, source in files.items(): self.assertEqual((workspace / name).read_bytes(), source.encode('utf-8'))


if __name__ == '__main__':
    unittest.main()
