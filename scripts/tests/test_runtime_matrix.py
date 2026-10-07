"""Regression controls for bit-exact matrix comparisons and compressed bodies."""
import json
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_runtime_matrix import comparable, expanded, sha, summarize
from capture_v04_baseline import encoded


class RuntimeMatrixTest(unittest.TestCase):
    def outcome(self, bits):
        return {'status': 0, 'rgba_sha256': 'a' * 64, 'outputs_sha256': sha(encoded([
                    {'name': 'same', 'kind': 'point', 'list': False, 'values': [{'x': bits, 'y': 0}]}])),
                'size': {'x': 1, 'y': 1}, 'inputs': []}

    def test_signed_zero_nan_payload_and_output_order_are_observable(self):
        for first, second in ((0, 0x80000000), (0x7fc00000, 0x7fc00001), (0x7fc00000, 0xffc00000)):
            self.assertNotEqual(comparable(self.outcome(first)), comparable(self.outcome(second)))
        a = self.outcome(0)
        b = {**a, 'rgba_sha256': 'b' * 64}
        self.assertNotEqual(comparable(a), comparable(b))
        b = {**a, 'inputs': [{'name': 'amount', 'default': False}]}
        c = {**a, 'inputs': [{'name': 'amount', 'default': 0}]}
        self.assertNotEqual(comparable(b), comparable(c))

    def test_only_trap_byte_offset_is_relocatable(self):
        a = {'status': 1, 'stderr': 'ipgen: generation failed at byte offset 20: divide by zero\n'}
        b = {**a, 'stderr': a['stderr'].replace('20', '200')}
        self.assertEqual(comparable(a), comparable(b))
        self.assertNotEqual(comparable(a), comparable({**b, 'stderr': b['stderr'].replace('divide by zero', 'fuel exhausted')}))
        self.assertNotEqual(comparable(a), comparable({**a, 'status': 2}))

    def test_expansion_rejects_concatenated_truncated_and_false_lengths(self):
        body = b'IPRP' + struct.pack('<HHIII', 0, 4, 0, 1, 0)
        header = b'IPRP' + struct.pack('<HHIII', 0, 4, 2, 1, 0)
        stream = zlib.compress(body)
        envelope = header + struct.pack('<I', len(body)) + stream
        self.assertEqual(expanded(body), body)
        self.assertEqual(expanded(envelope), body)
        for bad in (envelope[:-1], envelope + stream,
                    header + struct.pack('<I', len(body) - 1) + stream,
                    header + struct.pack('<I', 64 * 1024 * 1024 + 1) + stream):
            with self.assertRaises(RuntimeError): expanded(bad)

    def test_summary_retains_failed_cases_without_fabricating_historical_sizes(self):
        manifest = {'cases': [{'source': 'one.ips', 'sha256': 'a' * 64,
                    'modes': {mode: {name: {'status': 0, 'bytes': 20} for name in ('reference', 'optimized')}
                              for mode in ('debug', 'stripped')}}]}
        rows = {'current_sources': [{'source': 'one.ips', 'artifacts': {}, 'errors': ['compiler timed out']}]}
        value = summarize(rows, manifest)['current_sources']
        self.assertEqual(value['historical_size']['matched_plain_artifacts'], 0)
        self.assertEqual(value['compile_attempts'], 0)
        self.assertEqual(rows['current_sources'][0]['errors'], ['compiler timed out'])


if __name__ == '__main__':
    unittest.main()
