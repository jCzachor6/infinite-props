#!/usr/bin/env python3
"""Check shipped ABI slices, ELF page alignment and JNI packaging."""
import argparse
import io
import struct
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("aar")
args = parser.parse_args()
expected = {"arm64-v8a": 183, "armeabi-v7a": 40, "x86_64": 62}
with zipfile.ZipFile(args.aar) as archive:
    libraries = {name for name in archive.namelist() if name.endswith(".so")}
    assert libraries == {f"jni/{abi}/libiprop_jni.so" for abi in expected}, libraries
    with zipfile.ZipFile(io.BytesIO(archive.read("classes.jar"))) as classes:
        for name in ("Program", "GeneratedImage", "Native", "AndroidImagesKt", "IpropException"):
            assert f"dev/infiniteprops/{name}.class" in classes.namelist(), name
    rules = archive.read("proguard.txt")
    assert b"dev.infiniteprops.Native" in rules and b"dev.infiniteprops.IpropException" in rules
    for abi, machine in expected.items():
        data = archive.read(f"jni/{abi}/libiprop_jni.so")
        assert data[:4] == b"\x7fELF" and data[5] == 1, abi
        assert struct.unpack_from("<HH", data, 16) == (3, machine), abi
        wide = data[4] == 2
        offset = struct.unpack_from("<Q" if wide else "<I", data, 32 if wide else 28)[0]
        stride, count = struct.unpack_from("<HH", data, 54 if wide else 42)
        alignments = []
        for index in range(count):
            start = offset + index * stride
            if struct.unpack_from("<I", data, start)[0] == 1:
                alignment = struct.unpack_from("<Q" if wide else "<I", data, start + (48 if wide else 28))[0]
                assert alignment >= 16384, (abi, alignment)
                alignments.append(alignment)
        assert alignments, abi
        print(f"{abi}: ELF machine {machine}, {len(alignments)} load segments aligned for 16 KiB pages")
print("Android AAR packaging passed")
