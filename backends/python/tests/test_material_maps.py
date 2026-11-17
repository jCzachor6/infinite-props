"""Material maps (spec material maps v1.2) through ctypes against the CLI-rendered shared fixture."""
import json
import os
import subprocess
import sys
from pathlib import Path
import unittest

from infinite_props import Program, DeclaredImage, ImageKind, ImageSource

FIXTURES = Path(os.environ["IPROP_FIXTURES"])
KINDS = (ImageKind.NORMAL, ImageKind.ROUGHNESS, ImageKind.METALLIC, ImageKind.AO, ImageKind.EMISSION)
NAMES = ("relief", "gloss", "metal", "shade", "glow")


class MaterialMapTests(unittest.TestCase):
    def test_every_map_matches_the_cli_in_kind_order(self):
        self.assertEqual([int(kind) for kind in KINDS], [1, 2, 3, 4, 5])
        with Program(FIXTURES / "binding-maps.iprop") as program:
            # Declared out of order in the source; listed by kind.
            self.assertEqual(program.images, tuple(DeclaredImage(i, kind, name) for i, (kind, name) in enumerate(zip(KINDS, NAMES))))
            result = program.generate(5)
            self.assertEqual(result.rgba, (FIXTURES / "binding-maps.rgba").read_bytes())
            self.assertEqual([(image.index, image.kind, image.source, image.name) for image in result.images],
                             [(i, kind, ImageSource.AUTHORED, name) for i, (kind, name) in enumerate(zip(KINDS, NAMES))])
            for image in result.images:
                with self.subTest(kind=image.kind):
                    self.assertIs(result.image_of(image.kind), image)
                    self.assertEqual((image.width, image.height, image.row_stride, image.alpha_mode), (8, 4, 32, 2))
                    self.assertEqual(image.rgba, (FIXTURES / ("binding-maps." + image.kind.name.lower() + ".rgba")).read_bytes())
                    self.assertEqual(set(image.rgba[3::4]), {255})
            # A scalar map holds one value in red, green and blue.
            for kind in KINDS[1:4]:
                pixels = result.image_of(kind).rgba
                self.assertEqual(pixels[0::4], pixels[1::4])
                self.assertEqual(pixels[0::4], pixels[2::4])
            self.assertIs(result.normal, result.images[0])
            # The seed moves one roughness pixel and nothing else.
            moved = program.generate(6)
            self.assertNotEqual(moved.image_of(ImageKind.ROUGHNESS).rgba, result.image_of(ImageKind.ROUGHNESS).rgba)
            self.assertEqual(moved.image_of(ImageKind.EMISSION).rgba, result.image_of(ImageKind.EMISSION).rgba)
            # An authored normal map wins over a derive request; the maps stay.
            self.assertEqual(program.generate(5, derive_normal=True).images, result.images)

    def test_a_derived_normal_map_comes_first(self):
        with Program(FIXTURES / "bindings.iprop") as program:
            self.assertIsNone(program.generate(5).image_of(ImageKind.ROUGHNESS))

    def test_cli_writes_every_map(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Install the images extra to exercise Pillow and CLI export")
        run = lambda *arguments: subprocess.run([sys.executable, "-m", "infinite_props", *map(str, arguments)],
                                                capture_output=True, text=True)
        paths = {kind: FIXTURES / ("python-maps-" + kind.name.lower() + ".png") for kind in KINDS[1:]}
        flags = [value for kind, path in paths.items() for value in ("--" + kind.name.lower() + "-output", path)]
        written = run(FIXTURES / "binding-maps.iprop", FIXTURES / "python-maps.png", "--seed", "5", *flags)
        self.assertEqual(written.returncode, 0, written.stderr)
        self.assertEqual(json.loads(written.stdout)["images"],
                         [{"kind": kind.name.lower(), "source": "authored", "name": name} for kind, name in zip(KINDS, NAMES)])
        for kind, path in paths.items():
            with Image.open(path) as saved:
                self.assertEqual(saved.tobytes(), (FIXTURES / ("binding-maps." + kind.name.lower() + ".rgba")).read_bytes())
        refused = run(FIXTURES / "bindings.iprop", FIXTURES / "python-maps.png", "--seed", "5", "--emission-output", paths[ImageKind.EMISSION])
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn("publishes no emission map", refused.stderr)


if __name__ == "__main__":
    unittest.main()
