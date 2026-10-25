from concurrent.futures import ThreadPoolExecutor
import os
import json
import subprocess
import sys
from pathlib import Path
import unittest

from infinite_props import Program, Color, F32, U32, I32, IpropError

FIXTURES = Path(os.environ["IPROP_FIXTURES"])


class RuntimeTests(unittest.TestCase):
    def test_unicode_inputs_and_located_runtime_failure(self):
        with Program(FIXTURES / "binding-unicode.iprop") as program:
            self.assertEqual(program.inputs[0].name, "pł\0suffix")
            self.assertEqual(program.generate(inputs={"pł\0suffix": Color(18, 52, 86)}).rgba, bytes([18, 52, 86, 255]))
            self.assertEqual(program.generate(inputs={"é": F32(1), "e\u0301": F32(2)}).output("anchor").values, (1, 2))
            with self.assertRaises(IpropError) as raised:
                program.generate(inputs={"pł\0suffix\0extra": Color(0, 0, 0)})
            self.assertEqual(raised.exception.reason, 4005)
        with Program(FIXTURES / "binding-trap.iprop") as program:
            with self.assertRaises(IpropError) as raised:
                program.generate()
            error = raised.exception
            self.assertEqual((error.status, error.reason, error.stage), (3, 3008, 4))
            self.assertTrue(error.location_flags & 8)
            self.assertGreater(error.debug_line, 0)
            self.assertEqual(program.generate(inputs={"divisor": I32(1)}).output("value").values, (1, 0))

    def test_pillow_and_cli_export(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Install the images extra to exercise Pillow and CLI export")
        output = FIXTURES / "python-tested.png"
        cli = subprocess.run([sys.executable, "-m", "infinite_props", str(FIXTURES / "bindings.iprop"), str(output),
            "--seed", "4294967295", "--input", "tint=#123456", "--input", "amount=-0.0", "--input", "offset=-7"],
            check=True, capture_output=True, text=True)
        self.assertEqual(json.loads(cli.stdout)["statistics"]["image_byte_count"], 128)
        with Image.open(output) as image:
            self.assertEqual(image.tobytes(), (FIXTURES / "bindings.rgba").read_bytes())

    def test_generation_and_ownership(self):
        with Program(FIXTURES / "bindings.iprop") as program:
            self.assertEqual((program.metadata.width, program.metadata.height, program.metadata.origin_x), (8, 4, 2))
            self.assertEqual([item.name for item in program.inputs], ["tint", "amount", "offset"])
            self.assertEqual(program.inputs[1].default_value, F32(0.5))
            result = program.generate(0xffffffff, inputs={"tint": Color(18, 52, 86), "amount": F32(-0.0), "offset": I32(-7)})
            self.assertEqual(result.rgba, (FIXTURES / "bindings.rgba").read_bytes())
            self.assertEqual(result.output("anchor").value_bits, (0x80000000, 0xc0e00000))
            self.assertEqual(result.output("marks").count, 2)
            self.assertEqual(result.output("unset").count, 0)
            self.assertEqual(result.statistics.fuel_budget - result.statistics.fuel_remaining, result.statistics.fuel_used)
            self.assertEqual(result.statistics.image_byte_count, 128)
            self.assertNotEqual(program.generate().rgba, result.rgba)
            self.assertEqual(len(program.generate(width=16, height=8).rgba), 512)
            for name, value, reason in [("missing", U32(0), 4005), ("tint", F32(1), 4004)]:
                with self.assertRaises(IpropError) as raised:
                    program.generate(inputs={name: value})
                self.assertEqual((raised.exception.reason, raised.exception.stage), (reason, 3))
            expected = program.generate(17).rgba
            with ThreadPoolExecutor(4) as executor:
                self.assertEqual(list(executor.map(lambda _: program.generate(17).rgba, range(20))), [expected] * 20)
            try:
                from PIL import Image
            except ImportError:
                Image = None
            if Image:
                image = result.to_pillow()
                self.assertEqual(image.tobytes(), result.rgba)
        program.close()
        self.assertEqual(len(result.rgba), 128)
        self.assertEqual(result.output("box").values, (1, 2, 3, 4))
        with self.assertRaises(RuntimeError):
            program.generate()

    def test_byte_counted_names_exact_bits_and_empty_slots(self):
        with Program(FIXTURES / "backend-raw-values.iprop") as program:
            result = program.generate()
        self.assertEqual([slot.owner for slot in result.outputs], [0, 0, 0, 2, 2, 2])
        self.assertEqual([slot.name for slot in result.outputs[:3]], ["pł\0suffix", "", "\0c\0\0"])
        self.assertEqual([bits for slot in result.outputs[:3] for bits in slot.value_bits],
            [0x80000000, 1, 0x7fc12345, 0xff800000, 0x7f800000, 0xffc12345, 0x80000001, 0x3f800000, 0x80000000])
        self.assertEqual([slot.count for slot in result.outputs[3:]], [0, 0, 0])
        with Program(FIXTURES / "backend-empty-values.iprop") as program:
            self.assertTrue(all(slot.count == 0 and slot.value_bits == () for slot in program.generate().outputs))

    def test_invalid_input(self):
        with self.assertRaises(IpropError) as raised:
            Program(b"")
        self.assertEqual(raised.exception.status, 2)
        for kind, number in [(U32, -1), (U32, 0x100000000), (I32, 0x80000000), (I32, True)]:
            with self.assertRaises(ValueError):
                kind(number)
        with self.assertRaises(ValueError):
            Color(256, 0, 0)
        with Program(FIXTURES / "bindings.iprop") as program:
            with self.assertRaises(ValueError):
                program.generate(-1)
            with self.assertRaises(TypeError):
                program.generate(inputs={"amount": 0.5})
            self.assertEqual(program.generate().width, 8)


if __name__ == "__main__":
    unittest.main()
