#!/usr/bin/env python3
"""Replay the entire Native/WASM corpus through the production Python binding."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python/src"))
from infinite_props import Program, Color, DerivedNormal, F32, U32


def check(library, directory):
    data = (directory / "pixel-corpus.bin").read_bytes()
    manifest = json.loads((directory / "pixel-corpus.json").read_text())
    digest = hashlib.sha256(data).hexdigest()
    assert digest == manifest["sha256"], "Corpus SHA-256 mismatch"
    at = 0
    def take(size):
        nonlocal at
        value = data[at:at + size]
        assert len(value) == size, "Truncated corpus"
        at += size
        return value
    def word(): return struct.unpack("<I", take(4))[0]
    def blob(): return take(word())
    assert take(8) == b"IPXCRP02", "Corpus version"
    programs = [blob() for _ in range(word())]
    count = word()
    assert count > 0 and count == manifest["cases"]
    images = 0
    for index in range(count):
        with Program(programs[word()], library=library) as program:
            seed, width, height = word(), word(), word()
            inputs = {}
            for _ in range(word()):
                name = blob().decode("utf-8")
                tag, bits, integer = word(), word(), word()
                if tag == 0:
                    value = Color(*struct.pack("<I", bits))
                else:
                    declared = next(x for x in program.inputs if x.name == name)
                    value = F32.from_bits(bits) if isinstance(declared.default_value, F32) else U32(integer)
                inputs[name] = value
            derive, request = word(), DerivedNormal(*struct.unpack("<3f", take(12)))
            expected_width, expected_height, rgba = word(), word(), blob()
            image = program.generate(seed, width=width, height=height, inputs=inputs, work_limit=1_000_000_000,
                                     derive_normal=request if derive else None)
            label = f"case {index}, seed {seed}"
            assert (image.width, image.height) == (expected_width, expected_height), label + ": size"
            if image.rgba != rgba:
                pixel = next(i for i, (a, b) in enumerate(zip(image.rgba, rgba)) if a != b)
                raise AssertionError(f"{label}: RGBA byte {pixel}: {image.rgba[pixel]} != {rgba[pixel]}")
            assert image.to_pillow().tobytes() == rgba, label + ": Pillow RGBA"
            outputs = image.outputs
            assert len(outputs) == word(), label + ": output count"
            for output in outputs:
                name, owner, kind, is_list, elements = blob(), word(), word(), word(), word()
                bits = tuple(word() for _ in range(word()))
                assert (output.name.encode(), int(output.kind), output.is_list, output.count, output.value_bits) == \
                    (name, kind, bool(is_list), elements, bits), label + ": output bits"
            # Auxiliary images are part of the gate: every byte, name and tag.
            assert len(image.images) == word(), label + ": auxiliary image count"
            for auxiliary in image.images:
                kind, source, name = word(), word(), blob()
                aux_width, aux_height, aux_rgba = word(), word(), blob()
                assert (int(auxiliary.kind), int(auxiliary.source), auxiliary.name.encode(), auxiliary.width, auxiliary.height) == \
                    (kind, source, name, aux_width, aux_height), label + ": auxiliary image"
                if auxiliary.rgba != aux_rgba:
                    pixel = next(i for i, (a, b) in enumerate(zip(auxiliary.rgba, aux_rgba)) if a != b)
                    raise AssertionError(f"{label}: auxiliary RGBA byte {pixel}: {auxiliary.rgba[pixel]} != {aux_rgba[pixel]}")
                assert auxiliary.to_pillow().tobytes() == aux_rgba, label + ": Pillow auxiliary RGBA"
                images += 1
    assert at == len(data), "Unread corpus cases/trailing bytes"
    assert images > 0, "Corpus must exercise auxiliary images"
    print(f"Python + Pillow: {count} pixel-exact cases with {images} auxiliary images; SHA-256 {digest}")
    return count


def verify_rejection(library, directory):
    """Prove the gate rejects one changed transparent RGB byte and truncation."""
    original = (directory / "pixel-corpus.bin").read_bytes()
    at = 8
    def word():
        nonlocal at
        value = struct.unpack_from("<I", original, at)[0]
        at += 4
        return value
    def skip_blob():
        nonlocal at
        size = word()
        at += size
    for _ in range(word()): skip_blob()
    total = word()
    mutated = auxiliary = None
    for _ in range(total):
        for _ in range(4): word()  # program, seed, size overrides
        for _ in range(word()):
            skip_blob()
            for _ in range(3): word()
        for _ in range(4): word()  # derived-normal request
        word(); word()  # actual dimensions
        length = word()
        rgba_at = at
        at += length
        transparent = next((i for i in range(0, length, 4) if original[rgba_at + i + 3] == 0), None)
        if transparent is not None and mutated is None:
            mutated = bytearray(original)
            mutated[rgba_at + transparent] ^= 1
        for _ in range(word()):
            skip_blob()
            for _ in range(4): word()  # reserved, kind, list, element count
            leaves = word()
            at += leaves * 4
        for _ in range(word()):
            word(); word(); skip_blob(); word(); word()  # kind, source, name, dimensions
            length = word()
            if length and auxiliary is None:
                auxiliary = bytearray(original)
                auxiliary[at + length - 2] ^= 1  # the blue byte of the last pixel
            at += length
        if mutated is not None and auxiliary is not None:
            break
    assert mutated is not None, "Corpus must exercise zero alpha"
    assert auxiliary is not None, "Corpus must exercise auxiliary images"
    with tempfile.TemporaryDirectory(prefix="iprop-pixel-gate-") as temporary:
        target = Path(temporary)
        for label, content, expected in [("transparent RGB mutation", mutated, "RGBA byte"),
                                         ("auxiliary image mutation", auxiliary, "auxiliary RGBA byte"),
                                         ("truncated corpus", original[:8], "Truncated corpus")]:
            (target / "pixel-corpus.bin").write_bytes(content)
            manifest = json.loads((directory / "pixel-corpus.json").read_text())
            # Re-sign so the test reaches the byte comparator, not just SHA validation.
            manifest["sha256"] = hashlib.sha256(content).hexdigest()
            (target / "pixel-corpus.json").write_text(json.dumps(manifest))
            try:
                check(library, target)
            except AssertionError as error:
                assert expected in str(error), f"{label}: unexpected failure: {error}"
            else:
                raise AssertionError(f"Gate accepted {label}")
    print("Pixel gate rejects a changed RGB byte under zero alpha, a changed auxiliary image byte and a truncated corpus")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    check(args.library.resolve(), args.corpus)

    if args.self_test:
        verify_rejection(args.library.resolve(), args.corpus)
