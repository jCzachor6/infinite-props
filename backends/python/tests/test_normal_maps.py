"""Auxiliary images through ctypes against the CLI-rendered shared fixtures."""
import json
import os
import subprocess
import sys
from pathlib import Path
import unittest

from infinite_props import (Program, AuxiliaryImage, DeclaredImage, DerivedNormal, ImageKind, ImageSource,
                            IpropError)

FIXTURES = Path(os.environ["IPROP_FIXTURES"])
FLAT = bytes([128, 128, 255, 255])


class NormalMapTests(unittest.TestCase):
    def test_authored_normal_map_matches_the_cli(self):
        with Program(FIXTURES / "binding-normal.iprop") as program:
            self.assertEqual(program.images, (DeclaredImage(0, ImageKind.NORMAL, "relief"),))
            result = program.generate(5)
            self.assertEqual(result.rgba, (FIXTURES / "binding-normal.rgba").read_bytes())
            self.assertEqual(len(result.images), 1)
            normal = result.normal
            self.assertIs(normal, result.images[0])
            self.assertIsInstance(normal, AuxiliaryImage)
            self.assertEqual((normal.index, normal.kind, normal.source, normal.name), (0, ImageKind.NORMAL, ImageSource.AUTHORED, "relief"))
            self.assertEqual((normal.width, normal.height, normal.row_stride), (8, 4, 32))
            self.assertEqual((normal.pixel_format, normal.alpha_mode, normal.storage_origin), (result.pixel_format, 2, result.storage_origin))
            self.assertEqual(normal.rgba, (FIXTURES / "binding-normal.normal.rgba").read_bytes())
            self.assertEqual(set(normal.rgba[3::4]), {255})
            # The seed moves one painted direction; a size override scales the image.
            self.assertNotEqual(program.generate(6).normal.rgba, normal.rgba)
            scaled = program.generate(5, width=16, height=8).normal
            self.assertEqual((scaled.width, scaled.height, len(scaled.rgba)), (16, 8, 512))
            # An authored image wins over a derive request and stays identical.
            self.assertEqual(program.generate(5, derive_normal=True).images, result.images)
            self.assertEqual(program.generate(5, derive_normal=DerivedNormal(depth=9.0)).normal, normal)
        # The result owns its copies.
        self.assertEqual(normal.rgba[:4], FLAT)

    def test_derived_normal_map_is_opt_in(self):
        with Program(FIXTURES / "bindings.iprop") as program:
            self.assertEqual(program.images, ())
            plain = program.generate(5)
            self.assertEqual(plain.images, ())
            self.assertIsNone(plain.normal)
            self.assertIsNone(program.generate(5, derive_normal=False).normal)
            for request, name in [(True, "bindings-derived"), (DerivedNormal(), "bindings-derived"),
                                  (DerivedNormal(depth=2.0, rounding=0.5, detail=0.25), "bindings-derived-custom")]:
                with self.subTest(request=request):
                    result = program.generate(5, derive_normal=request)
                    normal = result.normal
                    self.assertEqual((normal.kind, normal.source, normal.name), (ImageKind.NORMAL, ImageSource.DERIVED, "normal"))
                    self.assertEqual(normal.rgba, (FIXTURES / (name + ".normal.rgba")).read_bytes())
                    self.assertEqual((normal.width, normal.height, normal.alpha_mode), (8, 4, 2))
                    # The request leaves the main image, outputs and fuel alone and spends work.
                    self.assertEqual((result.rgba, result.outputs), (plain.rgba, plain.outputs))
                    self.assertEqual(result.statistics.fuel_used, plain.statistics.fuel_used)
                    self.assertGreater(result.statistics.work_used, plain.statistics.work_used)
            # The derivation runs under the generation's work budget.
            used = program.generate(5, derive_normal=True).statistics.work_used
            self.assertEqual(program.generate(5, derive_normal=True, work_limit=used).statistics.work_remaining, 0)
            with self.assertRaises(IpropError) as raised:
                program.generate(5, derive_normal=True, work_limit=used - 1)
            self.assertEqual((raised.exception.reason, raised.exception.stage), (3015, 4))

    def test_invalid_requests_fail_as_arguments(self):
        with Program(FIXTURES / "bindings.iprop") as program:
            for request in [DerivedNormal(depth=-1.0), DerivedNormal(depth=float("inf")), DerivedNormal(rounding=64.5),
                            DerivedNormal(rounding=-0.5), DerivedNormal(detail=1.5), DerivedNormal(detail=float("nan"))]:
                with self.subTest(request=request), self.assertRaises(IpropError) as raised:
                    program.generate(5, derive_normal=request)
                self.assertEqual((raised.exception.status, raised.exception.stage), (4, 3))
                self.assertIn("out of range", str(raised.exception))
            for request in ["yes", 1, {"depth": 1.0}, (1.0, 2.0, 0.5)]:
                with self.subTest(request=request), self.assertRaises(TypeError):
                    program.generate(5, derive_normal=request)
            self.assertIsNone(program.generate(5).normal)

    def test_cli_writes_the_normal_map(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Install the images extra to exercise Pillow and CLI export")
        image, normal = FIXTURES / "python-normal-main.png", FIXTURES / "python-normal.png"
        run = lambda *arguments: subprocess.run([sys.executable, "-m", "infinite_props", *map(str, arguments)],
                                                capture_output=True, text=True)
        authored = run(FIXTURES / "binding-normal.iprop", image, "--seed", "5", "--normal-output", normal)
        self.assertEqual(authored.returncode, 0, authored.stderr)
        self.assertEqual(json.loads(authored.stdout)["images"], [{"kind": "normal", "source": "authored", "name": "relief"}])
        with Image.open(normal) as saved:
            self.assertEqual(saved.tobytes(), (FIXTURES / "binding-normal.normal.rgba").read_bytes())
        derived = run(FIXTURES / "bindings.iprop", image, "--seed", "5", "--derive-normal", "--normal-output", normal)
        self.assertEqual(json.loads(derived.stdout)["images"], [{"kind": "normal", "source": "derived", "name": "normal"}])
        with Image.open(normal) as saved:
            self.assertEqual(saved.tobytes(), (FIXTURES / "bindings-derived.normal.rgba").read_bytes())
        self.assertNotIn("images", json.loads(run(FIXTURES / "bindings.iprop", image, "--seed", "5").stdout))
        refused = run(FIXTURES / "bindings.iprop", image, "--seed", "5", "--normal-output", normal)
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn("publishes no normal map", refused.stderr)


if __name__ == "__main__":
    unittest.main()
