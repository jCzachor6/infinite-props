import ctypes
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
from threading import Event
import unittest
from unittest.mock import patch

from infinite_props import (Model, Program, Color, F32, U32, TextureCache, KnownTexture,
                            IpropError, model_library, _native as n)


def fnv(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = (value ^ byte) * 0x100000001b3 & 0xffffffffffffffff
    return value


class Reader(io.BytesIO):
    def words(self, count):
        return struct.unpack(f"<{count}I", self.read(4 * count))

    def word(self):
        return self.words(1)[0]

    def wide(self):
        return struct.unpack("<Q", self.read(8))[0]

    def blob(self):
        return self.read(self.word())


@unittest.skipUnless(os.environ.get("IPROP3D_LIBRARY"), "model library not selected")
class Models(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = Path(os.environ["IPROP_FIXTURES"])

    def model(self, name):
        model = Model(self.dir / ("model-" + name + ".iprop3d"))
        self.addCleanup(model.close)
        return model

    def test_layout(self):
        if ctypes.sizeof(ctypes.c_void_p) != 8:
            self.skipTest("64-bit layout")
        pins = (Path(__file__).resolve().parents[2] / "capi3d/test/models.c").read_text()
        def descriptor(name):
            return getattr(n, "LibraryInfo3D" if name == "library_info" else "".join(k.title() for k in name.split("_")))
        for name, size in re.findall(r"LAYOUT\(iprop3d_(\w+), (\d+)\)", pins):
            self.assertEqual(ctypes.sizeof(descriptor(name)), int(size), name)
        for name, field, offset in re.findall(r"OFFSET\(iprop3d_(\w+), (\w+), (\d+)\)", pins):
            self.assertEqual(getattr(descriptor(name), field).offset, int(offset), (name, field))

    def test_corpus(self):
        data = (self.dir / "model-corpus.bin").read_bytes()
        summary = json.loads((self.dir / "model-corpus.json").read_bytes())
        self.assertEqual(hashlib.sha256(data).hexdigest(), summary["sha256"])
        r = Reader(data)
        self.assertEqual(r.read(8), b"IPMCRP01")
        models = [Model(r.blob()) for _ in range(r.word())]
        for model in models:
            self.addCleanup(model.close)
        cases = r.word()
        self.assertEqual(cases, summary["cases"])
        for _ in range(cases):
            label = r.blob().decode()
            index, seed, lod, count = r.words(4)
            inputs = {}
            for _ in range(count):
                key = r.blob().decode()
                kind, value = r.words(2)
                inputs[key] = (lambda: Color(*value.to_bytes(4, "little")), lambda: F32.from_bits(value), lambda: U32(value))[kind]()
            model = models[index]
            result = model.generate(seed, inputs=inputs, lod=lod)
            self.assertEqual(result, model.generate(seed, inputs=inputs, lod=lod), label)
            floats = (*result.size, result.units_per_meter, *result.bounds_min, *result.bounds_max)
            self.assertEqual(struct.pack("<10f", *floats), r.read(40), label)
            s = result.statistics
            self.assertEqual((s.vertex_count, s.triangle_count), r.words(2), label)
            self.assertEqual((s.fuel_budget, s.fuel_used, s.work_budget, s.work_used), tuple(r.wide() for _ in range(4)), label)
            self.assertEqual(len(result.textures), r.word(), label)
            for t in result.textures:
                self.assertEqual((t.key, t.name.encode()), (r.wide(), r.blob()), label)
                self.assertEqual((t.width, t.height, t.asset_width, t.asset_height, t.repeats), r.words(5), label)
                self.assertEqual(fnv(t.rgba), r.wide(), label)
                self.assertEqual(len(t.images), r.word(), label)
                for a in t.images:
                    self.assertEqual((a.kind, a.name.encode(), fnv(a.rgba)), (r.word(), r.blob(), r.wide()), label)
            self.assertEqual(len(result.surfaces), r.word(), label)
            for s in result.surfaces:
                self.assertEqual(struct.pack("<IfffIfII", s.texture_index, s.roughness, s.metallic, s.emission,
                                             s.alpha_mode, s.alpha_cutoff, s.two_sided, s.crisp), r.read(32), label)
                for array in (s.positions, s.normals, s.tangents, s.uvs, s.indices):
                    self.assertEqual(array, r.blob(), label)
            self.assertEqual(len(result.outputs), r.word(), label)
            for o in result.outputs:
                self.assertEqual((o.name.encode(), o.kind, o.is_list, o.value_count), (r.blob(), *r.words(3)), label)
                self.assertEqual(struct.pack(f"<{len(o.value_bits)}I", *o.value_bits), r.blob(), label)
        self.assertEqual(r.read(), b"")

    def test_description_and_skin(self):
        lib = model_library()
        self.assertEqual((lib.api, lib.max_vertices, lib.max_triangles, lib.max_textures, lib.max_texture_side,
                          lib.max_texels, lib.max_lod), ((0, 3), 65536, 131072, 32, 2048, 4194304, 7))
        self.assertTrue(lib.texture_stamp)
        m = self.model("crate")
        self.assertEqual((m.metadata.size, m.metadata.units_per_meter), ((12, 10, 8), 50))
        self.assertEqual([(i.name, i.default_value) for i in m.inputs],
                         [("wide", F32(8)), ("stain", Color(51, 102, 153)), ("extra", U32(1)), ("rounding", F32(.5))])
        skin, = m.skin_programs
        self.assertEqual((skin.name, skin.width, skin.height), ("tile", 16, 8))
        t = m.generate(5).textures[0]
        with Program(self.dir / "model-tile.iprop") as tile:
            image = tile.generate(77, width=32, height=16, inputs={"tint": Color(51, 102, 153)})
            self.assertEqual((t.rgba, t.images), (image.rgba, image.images))
        try:
            import PIL
        except ImportError:
            return
        self.assertEqual(t.to_pillow().tobytes(), t.rgba)

    def test_errors(self):
        for data in (b"", b"abc", (self.dir / "model-tile.iprop").read_bytes()):
            with self.assertRaises(IpropError) as e:
                Model(data)
            self.assertEqual(e.exception.status, 2)
        m = self.model("crate-debug")
        token = m.cancellation_token()
        token.cancel()
        for kwargs, reason in ((dict(work_limit=1), 3015), (dict(work_limit=200), 3015),
                               (dict(cancellation=token), 3016), (dict(inputs={"bad": F32(1)}), 4005),
                               (dict(inputs={"wide": U32(1)}), 4004), (dict(inputs={"wide": F32(0)}), 3010)):
            with self.assertRaises(IpropError) as error:
                m.generate(**kwargs)
            self.assertEqual(error.exception.reason, reason)
            if kwargs == dict(work_limit=200):
                self.assertIn("skin 'tile'", str(error.exception))
            if reason == 3010:
                self.assertGreater(error.exception.debug_line, 0)
                self.assertEqual(error.exception.stage, 4)
            m.generate()
        with self.assertRaisesRegex(IpropError, "skin 'tile'"):
            m.generate(inputs={"rounding": F32(100)})
        for kwargs in (dict(lod=8), dict(known=[KnownTexture(0, 1, 1, 1)])):
            with self.assertRaises(IpropError) as error:
                m.generate(**kwargs)
            self.assertEqual(error.exception.status, 4)

    def test_cache_and_threads(self):
        m = self.model("lod")
        cache = TextureCache()
        cold = m.generate(3, textures=cache)
        warm = m.generate(3, textures=cache)
        self.assertEqual((cold.surfaces, cold.statistics), (warm.surfaces, warm.statistics))
        for a, b in zip(cold.textures, warm.textures):
            self.assertTrue(b.reused)
            self.assertIs(a.rgba, b.rgba)
            self.assertIs(a.images, b.images)
        known = cache.known()
        for t in m.generate(3, known=known).textures:
            self.assertTrue(t.reused)
            self.assertEqual((t.rgba, t.images), (b"", ()))
        cache.clear()
        with self.assertRaises(KeyError):
            m.generate(3, known=known, textures=cache)
        def run(i, cache):
            result = m.generate(100 + i, lod=i % 3, textures=cache)
            return (result.surfaces, result.outputs, result.statistics, tuple(t.rgba for t in result.textures))
        expected = [run(i, None) for i in range(8)]
        with ThreadPoolExecutor(8) as pool:
            for shared in (None, cache):
                self.assertEqual(list(pool.map(lambda i: run(i, shared), list(range(8))*6)), expected * 6)

    def test_close_during_call(self):
        m = self.model("crate")
        entered, resume = Event(), Event()
        call = m._native.model_api_generate
        def paused(*args):
            entered.set()
            if not resume.wait(10):
                raise TimeoutError()
            return call(*args)
        with patch.object(m._native, "model_api_generate", paused), ThreadPoolExecutor(1) as pool:
            future = pool.submit(m.generate)
            self.assertTrue(entered.wait(10))
            m.close()
            m.close()
            resume.set()
            self.assertTrue(future.result().textures[0].rgba)
        with self.assertRaisesRegex(RuntimeError, "closed"):
            m.generate()
