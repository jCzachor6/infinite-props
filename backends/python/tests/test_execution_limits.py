"""Exercise actual controls through ctypes, including concurrent cancellation."""
import gc
import json
import os
from pathlib import Path
from threading import Timer
import unittest
import weakref

from infinite_props import Program, I32, IpropError
from test_part_outputs import observe

FIXTURES = Path(os.environ['IPROP_FIXTURES'])


class ExecutionLimitTests(unittest.TestCase):
    def test_all_typed_fixtures_keep_pixels_rng_and_exact_native_work(self):
        for case in json.loads((FIXTURES / 'part-bindings.json').read_text()):
            with self.subTest(case=case['name']), Program(FIXTURES / case['directory'] / 'root.iprop') as program:
                args = dict(seed=case['seed'], width=case['size'][0], height=case['size'][1])
                reference = program.generate(**args)
                used = reference.statistics.work_used
                self.assertGreater(used, 0)
                self.assertEqual(reference.statistics.work_budget, 100_000_000)
                exact = program.generate(**args, work_limit=used, timeout_ns=60_000_000_000)
                self.assertEqual(exact.statistics.work_remaining, 0)
                self.assertEqual(exact.statistics.fuel_used, reference.statistics.fuel_used)
                self.assertEqual(observe(exact), observe(reference))
                for controls, reason in [(dict(work_limit=used - 1), 3015), (dict(timeout_ns=1), 3017)]:
                    with self.assertRaises(IpropError) as error:
                        program.generate(**args, **controls)
                    self.assertEqual((error.exception.reason, error.exception.stage), (reason, 4))
                self.assertEqual(observe(program.generate(**args)), observe(reference))

    def test_token_is_independent_of_program_lock_and_releases_after_use(self):
        with Program(FIXTURES / 'execution-slow.iprop') as program:
            token = program.cancellation_token()
            self.assertFalse(token.is_cancelled)
            timer = Timer(0.01, token.cancel)
            timer.start()
            try:
                with self.assertRaises(IpropError) as error:
                    program.generate(work_limit=1_000_000_000, cancellation=token)
                self.assertEqual((error.exception.reason, error.exception.stage), (3016, 4))
                self.assertTrue(error.exception.location_flags & 4, 'cancelled inside the active scan')
            finally:
                timer.join()
            self.assertTrue(token.is_cancelled)
            token.cancel()
            for _ in range(32):
                with self.assertRaises(IpropError) as error:
                    program.generate(inputs={'enabled': I32(0)}, cancellation=token)
                self.assertEqual(error.exception.reason, 3016)
            reference = program.generate(inputs={'enabled': I32(0)})
            self.assertEqual(reference.width, 1)
            pointer = weakref.ref(token)
            del token, timer
            gc.collect()
            self.assertIsNone(pointer())
            self.assertEqual(program.generate(inputs={'enabled': I32(0)}).rgba, reference.rgba)

    def test_counter_types_do_not_wrap_or_coerce(self):
        with Program(FIXTURES / 'bindings.iprop') as program:
            for value in [-1, 1 << 64, 0.5, True, '1']:
                for name in ['work_limit', 'timeout_ns']:
                    with self.assertRaises(ValueError):
                        program.generate(**{name: value})
            with self.assertRaises(ValueError):
                program.generate(cancellation=object())
