#!/usr/bin/env python3
"""Freeze the released 0.4 compiler/runtime and an independently linked C99 client.

No third-party Python modules are needed. Timing and machine paths are written
only to the local observations file, never to the versioned manifest. Captured
artifacts and the old executable remain under --work-dir for later ABI checks.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib

RELEASE = "66d34c2831585afb231022da9296528a912e962e"
SEEDS = (0, 1, 2, 7, 26)
ROOT = pathlib.Path(__file__).resolve().parent.parent


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()


def run(command, *, env=None):
    return subprocess.run([str(value) for value in command], cwd=ROOT, env=env,
                          capture_output=True, text=True, encoding="utf-8", errors="replace")


def checked(command, *, env=None):
    result = run(command, env=env)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command}\n{result.stderr}")
    return result


def png_rgba(data):
    """Decode exactly the non-interlaced RGBA8 output produced by ipgen.

    Filter predictors always use reconstructed left/up bytes. This avoids the
    false native/WASM differences caused by comparing filtered PNG bytes.
    """
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    offset, compressed, header = 8, bytearray(), None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        if zlib.crc32(kind + body) != crc:
            raise ValueError("PNG CRC mismatch")
        if kind == b"IHDR": header = struct.unpack(">IIBBBBB", body)
        if kind == b"IDAT": compressed.extend(body)
        offset += length + 12
        if kind == b"IEND": break
    if header is None or header[2:] != (8, 6, 0, 0, 0):
        raise ValueError("baseline requires non-interlaced RGBA8 PNGs")
    width, height = header[:2]
    raw, stride = zlib.decompress(compressed), width * 4
    if len(raw) != height * (stride + 1): raise ValueError("invalid PNG data length")
    pixels = bytearray(height * stride)
    for y in range(height):
        start, output = y * (stride + 1), y * stride
        filtering = raw[start]
        if filtering > 4: raise ValueError("unknown PNG filter")
        for x in range(stride):
            left = pixels[output + x - 4] if x >= 4 else 0
            up = pixels[output + x - stride] if y else 0
            corner = pixels[output + x - stride - 4] if y and x >= 4 else 0
            predictor = 0
            if filtering == 1: predictor = left
            elif filtering == 2: predictor = up
            elif filtering == 3: predictor = (left + up) // 2
            elif filtering == 4:
                estimate = left + up - corner
                distances = (abs(estimate-left), abs(estimate-up), abs(estimate-corner))
                predictor = (left, up, corner)[distances.index(min(distances))]
            pixels[output + x] = (raw[start + 1 + x] + predictor) & 255
    return width, height, bytes(pixels)


def source_inventory():
    tree = checked(["git", "ls-tree", "-r", RELEASE]).stdout
    files = {}
    for line in tree.splitlines():
        metadata, name = line.split("\t", 1)
        mode, kind, oid = metadata.split()
        if kind != "blob": continue
        path = ROOT / name
        data = path.read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if actual != oid:
            raise RuntimeError(f"working source differs from released {RELEASE}: {name}")
        files[name] = {"sha256": digest(data), "git_blob": oid, "bytes": len(data)}
    return files


def normalized(message, work):
    return message.replace(str(ROOT), "<repo>").replace(str(work), "<baseline>").replace("\r\n", "\n")


def generation(generator, binary, png, seed, work):
    png.unlink(missing_ok=True)
    result = run([generator, binary, "--seed", seed, "--float-bits", "-o", png])
    if result.returncode:
        return {"status": result.returncode, "stderr": normalized(result.stderr, work)}
    value = json.loads(result.stdout)
    outputs_file = png.with_suffix(".outputs.json")
    outputs_file.write_bytes(encoded(value["outputs"]) + b"\n")
    width, height, pixels = png_rgba(png.read_bytes())
    if value["size"] != {"x": width, "y": height}:
        raise AssertionError("JSON image size differs from PNG")
    return {"status": 0, "png": png.relative_to(work).as_posix(),
            "png_sha256": digest(png.read_bytes()), "rgba_sha256": digest(pixels),
            "outputs_sha256": digest(encoded(value["outputs"])),
            "outputs_json": outputs_file.relative_to(work).as_posix(),
            "outputs": [{"name": output["name"], "kind": output["kind"],
                         "list": output["list"], "count": len(output["values"])}
                        for output in value["outputs"]], "fuel": value["fuel"],
            "size": value["size"], "inputs": value["inputs"]}


def comparable(outcome):
    if outcome["status"]:
        return {"status": outcome["status"], "reason": re.sub(
            r"byte offset [0-9]+", "byte offset <relocated>", outcome["stderr"])}
    return {key: outcome[key] for key in ("rgba_sha256", "outputs_sha256", "size", "inputs")}


def choose_library(build):
    candidates = (build / "backends/capi/libipropc.0.4.0.dylib",
                  build / "backends/capi/libipropc.so.0.4.0",
                  build / "backends/capi/Release/ipropc.dll",
                  build / "backends/capi/ipropc.dll")
    for candidate in candidates:
        if candidate.exists(): return candidate.resolve()
    raise FileNotFoundError("use --library to identify the released shared ipropc")


def legacy_client(library, tools, work):
    target = work / "release-library"; target.mkdir(parents=True, exist_ok=True)
    frozen = target / library.name; shutil.copy2(library, frozen)
    if platform.system() == "Darwin":
        shutil.copy2(library, target / "libipropc.0.4.dylib")
    elif platform.system() != "Windows":
        shutil.copy2(library, target / "libipropc.so.0.4")
    link_library = frozen
    if platform.system() == "Windows":
        imports = library.with_suffix(".lib")
        if not imports.exists(): raise FileNotFoundError("DLL import library missing")
        link_library = target / imports.name; shutil.copy2(imports, link_library)
    build = work / "legacy-client"
    checked(["cmake", "-S", ROOT / "backends/capi/test/legacy/v0.2", "-B", build,
             "-DCMAKE_BUILD_TYPE=Release", f"-DIPROP_LEGACY_LIBRARY={link_library}",
             f"-DCMAKE_BUILD_RPATH={target}"])
    checked(["cmake", "--build", build, "--config", "Release"])
    executable = build / ("iprop_legacy_v02.exe" if os.name == "nt" else "iprop_legacy_v02")
    if not executable.exists(): executable = build / "Release" / executable.name
    env = os.environ.copy()
    env["PATH"] = str(target) + os.pathsep + env.get("PATH", "")
    fixtures = work / "legacy-fixtures"; fixtures.mkdir(exist_ok=True)
    uniforms, owners = fixtures / "uniforms.iprop", fixtures / "output-owners.iprop"
    checked([tools / "ipa", ROOT / "backends/capi/test/uniforms.ipa", "-o", uniforms])
    checked([tools / "ipc", ROOT / "backends/capi/test/output-owners.ips", "-o", owners])
    result = checked([executable, uniforms, owners], env=env)
    return {"header_sha256": digest((ROOT / "backends/capi/test/legacy/v0.2/iprop.h").read_bytes()),
            "target": {"os": platform.system(), "architecture": platform.machine(),
                       "pointer_bits": struct.calcsize("P") * 8, "byte_order": sys.byteorder},
            "executable": executable.relative_to(work).as_posix(),
            "executable_sha256": digest(executable.read_bytes()),
            "library": frozen.relative_to(work).as_posix(), "library_sha256": digest(frozen.read_bytes()),
            "report": json.loads(result.stdout),
            "client_source": {"path": "backends/capi/test/legacy/v0.2/client.c", "sha256": digest(
                (ROOT / "backends/capi/test/legacy/v0.2/client.c").read_bytes())},
            "build_source": {"path": "backends/capi/test/legacy/v0.2/CMakeLists.txt", "sha256": digest(
                (ROOT / "backends/capi/test/legacy/v0.2/CMakeLists.txt").read_bytes())},
            "fixtures": [path.relative_to(work).as_posix() for path in (uniforms, owners)],
            "fixture_sha256": {path.relative_to(work).as_posix(): digest(path.read_bytes())
                               for path in (uniforms, owners)},
            "unobserved": ["OOM requires controlled allocation-failure injection"]}


def capture(args):
    began = time.monotonic()
    if checked(["git", "rev-parse", "v0.4.0^{commit}"]).stdout.strip() != RELEASE:
        raise RuntimeError("released tag no longer matches the pinned source commit")
    files = source_inventory()
    tools, work = args.tools.resolve(), args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if os.name == "nt" else ""
    tool_files = {name: tools / (name + suffix) for name in ("ipc", "ipgen", "ipa")}
    tool_hashes = {name: digest(path.read_bytes()) for name, path in tool_files.items()}
    frozen_tools = work / "release-tools"; frozen_tools.mkdir(exist_ok=True)
    for path in tool_files.values(): shutil.copy2(path, frozen_tools / path.name)
    released_header = checked(["git", "show", f"{RELEASE}:backends/capi/iprop.h"]).stdout.encode()
    if (ROOT / "backends/capi/test/legacy/v0.2/iprop.h").read_bytes() != released_header:
        raise RuntimeError("legacy header is not byte-identical to the released header")
    language = json.loads(checked([tool_files["ipc"], "--describe-language"]).stdout)
    if language["product_version"] != "0.4.0" or language["format_version"] != {"major": 0, "minor": 4}:
        raise RuntimeError("baseline compiler does not report released product/format versions")
    manifest = {"schema_version": 1, "release": {"tag": "v0.4.0", "commit": RELEASE,
                 "product_version": "0.4.0", "format_version": "0.4", "c_api_version": "0.2"},
                "capture": {"seeds": list(SEEDS), "variants": [0], "archetypes": [0],
                 "size": "asset default", "compression": "disabled", "modes": ["debug", "stripped"],
                 "float_outputs": "raw IEEE-754 binary32 bits", "timing": "local observations only"},
                "source_files": files, "tools_sha256": tool_hashes,
                "tool_artifacts": {name: (frozen_tools / path.name).relative_to(work).as_posix()
                                   for name, path in tool_files.items()},
                "language_contract_sha256": digest(encoded(language)), "cases": []}
    for number, source in enumerate(name for name in files if name.endswith(".ips")):
        row = {"source": source, "sha256": files[source]["sha256"], "modes": {}}
        for mode, flag in (("debug", "--debug"), ("stripped", "--strip")):
            pair = {}
            for name, optimize in (("reference", ["--no-opt"]), ("optimized", [])):
                directory = work / "cases" / f"{number:03d}" / mode / name
                directory.mkdir(parents=True, exist_ok=True)
                binary = directory / "asset.iprop"; binary.unlink(missing_ok=True)
                result = run([tool_files["ipc"], ROOT / source, "-o", binary, "--no-compress", flag, *optimize])
                observed = {"status": result.returncode, "stderr": normalized(result.stderr, work)}
                if result.returncode == 0:
                    observed.update({"binary": binary.relative_to(work).as_posix(),
                                     "bytes": binary.stat().st_size, "sha256": digest(binary.read_bytes()),
                                     "generations": [generation(tool_files["ipgen"], binary,
                                         directory / f"seed-{seed}.png", seed, work) for seed in SEEDS]})
                elif binary.exists(): raise AssertionError(f"failed compilation wrote {source}")
                pair[name] = observed
            reference, optimized = pair["reference"], pair["optimized"]
            if reference["status"] != optimized["status"] or reference["stderr"] != optimized["stderr"]:
                raise AssertionError(f"optimized/reference compile mismatch: {source}")
            if reference["status"] == 0:
                if optimized["bytes"] > reference["bytes"]: raise AssertionError(f"artifact grew: {source}")
                for a, b in zip(reference["generations"], optimized["generations"]):
                    if comparable(a) != comparable(b): raise AssertionError(f"execution differs: {source}")
            row["modes"][mode] = pair
        manifest["cases"].append(row)
        if (number + 1) % 20 == 0: print(f"captured {number + 1} source programs", flush=True)
    probe = ROOT / "benchmarks/baselines/v0.4.0/rng-state.ipa"
    binary = work / "rng-state.iprop"
    checked([tool_files["ipa"], probe, "-o", binary])
    manifest["rng_probe"] = {"source": probe.relative_to(ROOT).as_posix(), "sha256": digest(probe.read_bytes()),
                             "generations": []}
    for seed in SEEDS:
        outcome = generation(tool_files["ipgen"], binary, work / f"rng-seed-{seed}.png", seed, work)
        states = {}
        for output in json.loads((work / outcome["outputs_json"]).read_text()):
            point = output["values"][0]
            halves = [int(struct.unpack("<f", struct.pack("<I", point[key]))[0]) for key in ("x", "y")]
            states[output["name"]] = halves[0] | halves[1] << 16
        if states["initial"] != seed or states["advanced"] != states["restored"]:
            raise AssertionError("RNG probe lost or changed raw stream state")
        outcome["states_u32"] = states
        manifest["rng_probe"]["generations"].append(outcome)
    manifest["goldens"] = {}
    for name in files:
        if name.startswith("tests/golden/") and name.endswith(".png"):
            width, height, pixels = png_rgba((ROOT / name).read_bytes())
            manifest["goldens"][name] = {"width": width, "height": height, "rgba_sha256": digest(pixels)}
    library = args.library.resolve() if args.library else choose_library(tools.parent)
    manifest["legacy_client"] = legacy_client(library, tools, work)
    if source_inventory() != files: raise AssertionError("released source changed during capture")
    for name, path in tool_files.items():
        if digest(path.read_bytes()) != tool_hashes[name]: raise AssertionError("tools changed during capture")
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    observations = {"platform": platform.platform(), "machine": platform.machine(),
                    "python": platform.python_version(), "elapsed_seconds": time.monotonic()-began,
                    "work_dir": str(work), "tools": str(tools), "source": RELEASE,
                    "cmake": checked(["cmake", "--version"]).stdout.splitlines()[0]}
    (work / "observations.json").write_text(json.dumps(observations, indent=2) + "\n", encoding="utf-8")
    print(f"captured {len(manifest['cases'])} sources, raw RNG states, {len(manifest['goldens'])} goldens and frozen shared C99 client")


def verify(args):
    """Check preserved evidence after feature work, without recapturing 0.4."""
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    work = args.work_dir.resolve()
    if manifest["release"]["commit"] != RELEASE: raise AssertionError("wrong release identity")
    header = (ROOT / "backends/capi/test/legacy/v0.2/iprop.h").read_bytes()
    if digest(header) != manifest["legacy_client"]["header_sha256"]: raise AssertionError("frozen header changed")
    for name, path in manifest["tool_artifacts"].items():
        if digest((work / path).read_bytes()) != manifest["tools_sha256"][name]:
            raise AssertionError(f"frozen tool changed: {name}")
    count = 0
    for row in manifest["cases"]:
        for pair in row["modes"].values():
            for artifact in pair.values():
                if artifact["status"]: continue
                if digest((work / artifact["binary"]).read_bytes()) != artifact["sha256"]:
                    raise AssertionError("frozen binary changed")
                for outcome in artifact["generations"]:
                    if outcome["status"]: continue
                    if digest((work / outcome["png"]).read_bytes()) != outcome["png_sha256"]:
                        raise AssertionError("frozen PNG changed")
                    outputs = json.loads((work / outcome["outputs_json"]).read_text())
                    if digest(encoded(outputs)) != outcome["outputs_sha256"]:
                        raise AssertionError("frozen raw outputs changed")
                    count += 1
    legacy = manifest["legacy_client"]
    for source in (legacy["client_source"], legacy["build_source"]):
        if digest((ROOT / source["path"]).read_bytes()) != source["sha256"]:
            raise AssertionError("legacy executable source changed")
    for path, expected in legacy["fixture_sha256"].items():
        if digest((work / path).read_bytes()) != expected: raise AssertionError("legacy fixture changed")
    for outcome in manifest["rng_probe"]["generations"]:
        outputs = json.loads((work / outcome["outputs_json"]).read_text())
        if digest(encoded(outputs)) != outcome["outputs_sha256"]:
            raise AssertionError("frozen RNG output changed")
        states = {}
        for output in outputs:
            point = output["values"][0]
            low, high = (int(struct.unpack("<f", struct.pack("<I", point[key]))[0])
                         for key in ("x", "y"))
            states[output["name"]] = low | high << 16
        if states != outcome["states_u32"]: raise AssertionError("raw RNG state changed")
    for path, expected in ((legacy["executable"], legacy["executable_sha256"]),
                           (legacy["library"], legacy["library_sha256"])):
        if digest((work / path).read_bytes()) != expected: raise AssertionError("frozen ABI artifact changed")
    env = os.environ.copy()
    library_dir = work / pathlib.Path(legacy["library"]).parent
    env["PATH"] = str(library_dir) + os.pathsep + env.get("PATH", "")
    result = checked([work / legacy["executable"], *(work / path for path in legacy["fixtures"])], env=env)
    if json.loads(result.stdout) != legacy["report"]: raise AssertionError("legacy client report changed")
    print(f"verified {count} frozen renders, raw outputs, tools and the original shared C99 executable")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=pathlib.Path, default=ROOT / "build/tools")
    parser.add_argument("--library", type=pathlib.Path)
    parser.add_argument("--work-dir", type=pathlib.Path, default=ROOT / "build/baselines/v0.4.0")
    parser.add_argument("--manifest", type=pathlib.Path, default=ROOT / "benchmarks/baselines/v0.4.0/manifest.json")
    parser.add_argument("--verify", action="store_true", help="verify preserved local evidence without recapture")
    args = parser.parse_args()
    if args.verify: verify(args)
    else: capture(args)


if __name__ == "__main__":
    main()
