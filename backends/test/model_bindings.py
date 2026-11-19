"""Compile the model binding corpus and encode the independent ipgen dumps."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

# Source, seeds, named input sets and levels of detail.
CORPUS = [
    ('backends/html5/test/fixtures/model-primitives.ips', [0, 1, 2, 7, 26, 4294967295], [{}, {'stretch': 1.75, 'sides': 5, 'tint': '#c0392b'}, {'stretch': 0.625, 'sides': 31, 'rounding': 2.5}], [0]),
    ('backends/capi3d/test/crate.ips', [0, 5, 77], [{}, {'wide': 6.5, 'extra': 0}, {'wide': 11.75, 'stain': '#10a0f080'}], [0]),
    ('examples/models/bench.ips', [1, 2], [{}, {'paint': '#7a3b2e'}], [0]),
    ('examples/models/hydrant.ips', [1, 4], [{}, {'paint': '#2f6f4a'}], [0]),
    ('examples/language/model-sculpt.ips', [1], [{}], [0]),
    ('examples/models/sandbags.ips', [1, 3], [{}, {'dye': '#6f7a52'}], [0]),
    ('examples/models/chair.ips', [0, 1, 2, 3, 4, 5, 9, 13], [{}, {'finish': 5, 'fabric': 2}], [0]),
    ('backends/capi3d/test/empty.ips', [0, 9], [{}], [0]),
    ('tests/model/corpus/materials.ips', [1, 3, 6], [{}], [0]),
    ('tests/model/corpus/shapes.ips', [1, 2, 3, 5, 8], [{}], [0]),
    ('examples/language/model-parts.ips', [1, 9], [{}], [0]),
    ("examples/models/boulder.ips", [1, 6], [{}, {"rough": 0.875}], [0]),
    ("examples/language/model-lod.ips", [3], [{}], range(4)),
]


class Writer(bytearray):
    def words(self, *values):
        self.extend(struct.pack(f"<{len(values)}I", *values))

    def wide(self, value):
        self.extend(struct.pack("<Q", value))

    def blob(self, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.words(len(data))
        self.extend(data)

    def array(self, values):
        self.blob(struct.pack(f"<{len(values)}I", *values))


def encode_dump(dst, d):
    dst.words(*d["size"], d["units_per_meter"], *d["bounds"]["min"], *d["bounds"]["max"],
              d["vertices"], d["triangles"])
    for counter in ("fuel", "work"):
        dst.wide(d[counter]["budget"])
        dst.wide(d[counter]["spent"])
    dst.words(len(d["textures"]))
    for t in d["textures"]:
        dst.wide(int(t["key"], 16))
        dst.blob(t["name"])
        dst.words(t["width"], t["height"], *t["asset"], t["repeats"])
        dst.wide(int(t["color"], 16))
        dst.words(len(t["images"]))
        for a in t["images"]:
            dst.words(a["kind"])
            dst.blob(a["name"])
            dst.wide(int(a["pixels"], 16))
    dst.words(len(d["surfaces"]))
    for s in d["surfaces"]:
        dst.words(*(s[k] for k in ("texture", "roughness", "metallic", "emission", "alpha", "cutoff", "two_sided", "crisp")))
        for k in ("positions", "normals", "tangents", "uvs", "indices"):
            dst.array(s[k])
    outputs = d.get("outputs", [])
    dst.words(len(outputs))
    for o in outputs:
        box = o["kind"] == "box3d"
        values = o["boxes"] if box else o["points"]
        dst.blob(o["name"])
        dst.words(box, o["list"], len(values))
        triples = (v for b in values for v in (b["center"], b["size"], *b["axes"])) if box else values
        dst.array([word for triple in triples for word in triple])


def input_word(kind, value):
    if kind == "color":
        rgba = bytes.fromhex(value[1:])
        return 0, int.from_bytes(rgba.ljust(4, b"\xff"), "little")
    if kind == "f32":
        data = struct.pack("<f", value)
        if struct.unpack("<f", data)[0] != value:
            raise ValueError(f"Inexact f32 corpus input {value}")
        return 1, int.from_bytes(data, "little")
    if kind == "u32":
        return 2, value
    raise ValueError(f"Unsupported model input type {kind}")


def prepare(tools, output):
    # CTest can regenerate the fixtures while another backend is reading them.
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output) as staging:
        summary = build_corpus(tools, Path(staging))
        for artifact in Path(staging).iterdir():
            artifact.replace(output / artifact.name)
    return summary


def build_corpus(tools, output):
    root = Path(__file__).resolve().parents[2]
    suffix = ".exe" if os.name == "nt" else ""
    ipc, ipgen = (tools.resolve() / (name + suffix) for name in ("ipc", "ipgen"))
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    def compile_file(source, name, *flags):
        dest = output / name
        subprocess.run([ipc, root / source, *flags, "-o", dest], check=True)
        return dest

    for name, dest, flags in (("crate", "crate-debug.iprop3d", ["--debug", "--no-compress"]),
                              ("tile", "tile.iprop", []), ("outputs", "outputs.iprop3d", [])):
        compile_file("backends/capi3d/test/" + name + ".ips", "model-" + dest, *flags)
    programs = Writer(b"IPMCRP01")
    programs.words(len(CORPUS))
    cases = Writer()
    count = 0
    for index, (source, seeds, overrides, levels) in enumerate(CORPUS):
        name = Path(source).stem.removeprefix("model-")
        file = compile_file(source, "model-" + name + ".iprop3d")
        programs.blob(file.read_bytes())
        with tempfile.TemporaryDirectory(dir=output) as scratch:
            meta = json.loads(subprocess.check_output([ipgen, file, "-o", Path(scratch) / "model.glb"]))
        types = {i["name"]: i["type"] for i in meta["inputs"]}
        for seed in seeds:
            for inputs in overrides:
                for lod in levels:
                    arguments = [f"{k}={v}" for k, v in inputs.items()]
                    cases.blob(" ".join([source, "seed", str(seed), *arguments, "lod", str(lod)]))
                    cases.words(index, seed, lod, len(inputs))
                    for k, v in inputs.items():
                        cases.blob(k)
                        cases.words(*input_word(types[k], v))
                    cmd = [ipgen, file, "--seed", str(seed), "--lod", str(lod), "--dump"]
                    for arg in arguments:
                        cmd.extend(["--uniform", arg])
                    encode_dump(cases, json.loads(subprocess.check_output(cmd)))
                    count += 1
    programs.words(count)
    programs.extend(cases)
    (output / "model-corpus.bin").write_bytes(programs)
    summary = dict(format="IPMCRP01", programs=len(CORPUS), cases=count, bytes=len(programs),
                   sha256=hashlib.sha256(programs).hexdigest())
    (output / "model-corpus.json").write_text(json.dumps(summary, indent=2) + "\n")
    return summary
