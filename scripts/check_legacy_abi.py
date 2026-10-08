#!/usr/bin/env python3
"""Build an API 0.2 client once, then observe it with old and new shared libraries.

This is a native CI gate, not a recompile-with-old-header test. The baseline
comes from a pinned release archive; the client executable is never rebuilt
between runs. Loader events, file identities and negative controls are retained
under build/ci-reports/legacy-abi. No third-party Python packages are required.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import struct
import subprocess
import tarfile


ROOT = Path(__file__).resolve().parent.parent
RELEASE = "66d34c2831585afb231022da9296528a912e962e"
FROZEN = {
    "iprop.h": "d601e43d0c1da04b439dc0dadd58a5e505e625810cde67f90ee619044a7e8155",
    "client.c": "7bf7b3ab0c69e7b7f09979631c4d3ec4dea1a188b63ce3f74ab8fccc918f1b81",
    "CMakeLists.txt": "4702fcc003511901c96e863db27f2d2cab6285f27df9802062d4c879c946e260",
}
EXPECTED_CLIENT = {"api_major": 0, "api_minor": 2,
                   "sizes": {"uniform": 24, "options": 40, "meta": 32,
                             "output": 40, "image": 32},
                   "status": "pass", "exports_used": 6}


class GateFailure(RuntimeError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identity(path):
    path = Path(path).resolve(strict=True)
    return {"path": str(path), "sha256": digest(path)}


def require_client(executable, expected_sha):
    if digest(executable) != expected_sha:
        raise GateFailure("client_identity", "the frozen executable changed")


def observed_library(system, stderr):
    paths = []
    for line in stderr.splitlines():
        if system == "Darwin":
            match = re.match(r"^dyld\[\d+\]:\s+(?:<[^>]+>\s+)?(/.*libipropc[^/]*\.dylib)$", line)
        else:
            # Searching/trying a file is not evidence that it was loaded.
            match = re.search(r"calling init:\s+(/.*libipropc\.so(?:\.[0-9.]+)?)\s*$", line)
        if match:
            paths.append(match.group(1))
    return paths


def validate_observation(executable, executable_sha, loaded_paths, expected_library,
                         returncode, stdout):
    require_client(executable, executable_sha)
    actual = {Path(path).resolve(strict=True) for path in loaded_paths
              if re.fullmatch(r"(?:lib)?ipropc(?:\.so(?:\.[0-9.]+)?|(?:\.[0-9.]+)?\.dylib|\.dll)",
                              Path(path).name, re.IGNORECASE)}
    if len(actual) != 1:
        raise GateFailure("loader_evidence", f"expected one loaded ipropc, observed {sorted(map(str, actual))}")
    loaded = identity(actual.pop())
    if os.path.normcase(loaded["path"]) != os.path.normcase(expected_library["path"]):
        raise GateFailure("library_path", "the OS loaded a different ipropc path")
    if loaded["sha256"] != expected_library["sha256"]:
        raise GateFailure("library_identity", "the loaded ipropc file changed")
    if returncode != 0:
        raise GateFailure("client_exit", f"legacy client exited {returncode}")
    try:
        report = json.loads(stdout)
    except ValueError as error:
        raise GateFailure("client_report", "legacy client did not return its JSON report") from error
    if report != EXPECTED_CLIENT:
        raise GateFailure("client_report", f"unexpected legacy layouts/export report: {report}")
    return {"loaded_library": loaded, "client_report": report, "returncode": returncode}


def command(args, cwd, report_dir, label):
    result = subprocess.run([str(arg) for arg in args], cwd=cwd,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    (report_dir / (label + ".log")).write_text(
        json.dumps([str(arg) for arg in args]) + "\n" + result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise GateFailure("build", f"{label} failed ({result.returncode}); see {report_dir / (label + '.log')}")
    return result.stdout.strip()


def find_library(build, system):
    directory = build / "backends/capi"
    if system == "Windows":
        candidates = list(directory.glob("**/ipropc.dll"))
    elif system == "Darwin":
        candidates = [p for p in directory.glob("libipropc.*.dylib") if not p.is_symlink()]
    else:
        candidates = [p for p in directory.glob("libipropc.so.*") if not p.is_symlink()]
    if len(candidates) != 1:
        raise GateFailure("library_artifact", f"expected one shared ipropc artifact under {directory}")
    library = candidates[0].resolve(strict=True)
    alias = ("ipropc.dll" if system == "Windows" else
             "libipropc.0.4.dylib" if system == "Darwin" else "libipropc.so.0.4")
    loader_file = library.parent / alias
    if not loader_file.exists() or loader_file.resolve() != library:
        raise GateFailure("loader_identity", f"the shared library does not preserve ABI loader name {alias}")
    return library, loader_file


def stage_library(build, destination, system):
    library, loader_file = find_library(build, system)
    destination.mkdir()
    shutil.copy2(library, destination / library.name)
    if loader_file.name != library.name:
        shutil.copy2(library, destination / loader_file.name)
    staged = destination / loader_file.name
    return identity(library), identity(staged)


def observe(executable, executable_sha, fixtures, library, expected_library, report_dir, label):
    require_client(executable, executable_sha)  # before creating the process
    env = os.environ.copy()
    for name in ("LD_PRELOAD", "LD_AUDIT", "LD_DEBUG", "LD_DEBUG_OUTPUT",
                 "DYLD_INSERT_LIBRARIES", "DYLD_LIBRARY_PATH", "DYLD_PRINT_LIBRARIES"):
        env.pop(name, None)
    system = platform.system()
    directory = str(Path(library["path"]).parent)
    args = [executable, *fixtures]
    stdout_path, stderr_path = report_dir / (label + ".stdout"), report_dir / (label + ".loader.log")
    if system == "Windows":
        # No DLL is copied beside the executable. The observer verifies which
        # path Windows chose, so an accidental higher-priority DLL cannot pass.
        env["PATH"] = directory + os.pathsep + env.get("PATH", "")
        from windows_loader import run_observed
        code, loaded = run_observed(args, cwd=Path(executable).parent, env=env,
                                   stdout=stdout_path, stderr=stderr_path)
        (report_dir / (label + ".dll-events.json")).write_text(json.dumps(loaded, indent=2), encoding="utf-8")
    else:
        if system == "Darwin":
            env["DYLD_LIBRARY_PATH"] = directory
            env["DYLD_PRINT_LIBRARIES"] = "1"
        else:
            env["LD_LIBRARY_PATH"] = directory
            env["LD_DEBUG"] = "libs"
        result = subprocess.run([str(arg) for arg in args], cwd=Path(executable).parent,
                                env=env, capture_output=True, timeout=90)
        stdout_path.write_bytes(result.stdout)
        stderr_path.write_bytes(result.stderr)
        code = result.returncode
        loaded = observed_library(system, result.stderr.decode("utf-8", errors="replace"))
    return validate_observation(executable, executable_sha, loaded, expected_library,
                                code, stdout_path.read_text(encoding="utf-8"))


def reject_control(code, action):
    try:
        action()
    except GateFailure as error:
        if error.code != code:
            raise GateFailure("negative_control", f"expected {code}, observed {error.code}") from error
        return {"status": "rejected", "reason": code}
    raise GateFailure("negative_control", f"control unexpectedly passed: {code}")


def gate(args, report):
    system = platform.system()
    if system not in ("Linux", "Darwin", "Windows") or struct.calcsize("P") != 8:
        raise GateFailure("platform", "ABI acceptance is defined for native Linux/macOS/Windows 64-bit hosts")
    work, build, repository, report_dir = args.work.resolve(), args.build.resolve(), args.repository.resolve(), args.report.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise GateFailure("work_directory", "use an empty private work directory; do not reuse an old build/client")
    frozen = ROOT / "backends/capi/test/legacy/v0.2"
    for name, expected in FROZEN.items():
        if digest(frozen / name) != expected:
            raise GateFailure("frozen_source", f"frozen {name} changed")
    peeled = command(["git", "rev-parse", "v0.4.0^{commit}"], repository, report_dir, "release-pin")
    if peeled != RELEASE:
        raise GateFailure("release_pin", "v0.4.0 does not resolve to the pinned released commit")
    tag = command(["git", "rev-parse", "v0.4.0"], repository, report_dir, "release-tag")
    # Repository bytes on every host: git archive otherwise applies core.autocrlf (on by default on Windows).
    archive = subprocess.run(["git", "-c", "core.autocrlf=false", "archive", RELEASE], cwd=repository,
                             capture_output=True, check=True).stdout
    old_source = work / "released-source"
    old_source.mkdir()
    with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
        tree.extractall(old_source, filter="data")
    if (old_source / "backends/capi/iprop.h").read_bytes() != (frozen / "iprop.h").read_bytes():
        raise GateFailure("released_header", "frozen client header differs from the released API 0.2 header")
    report.update({"platform": {"system": system, "machine": platform.machine(), "pointer_bits": 64},
                   "release": {"commit": RELEASE, "tag_object": tag, "archive_sha256": hashlib.sha256(archive).hexdigest()},
                   "frozen_source_sha256": FROZEN})
    old_build = work / "released-build"
    configure = ["cmake", *(["-G", args.generator] if args.generator else []), "-S", old_source, "-B", old_build,
                 "-DCMAKE_BUILD_TYPE=Release", "-DIPROP_BUILD_TESTS=OFF", "-DIPROP_FETCH_DEPS=OFF",
                 "-DIPROP_SFML=OFF", "-DIPROP_GODOT=OFF", f"-DZLIB_USE_STATIC_LIBS={args.static_zlib}"]
    if system == "Windows":
        configure.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
    command(configure, ROOT, report_dir, "old-library-configure")
    command(["cmake", "--build", old_build, "--config", "Release", "--target", "iprop_capi", "--parallel", "4"], ROOT, report_dir, "old-library-build")
    old_original, old_library = stage_library(old_build, work / "released-library", system)
    new_original, new_library = stage_library(build, work / "replacement-library", system)
    if old_original["sha256"] == new_original["sha256"]:
        raise GateFailure("replacement_identity", "the requested replacement is byte-identical to the released library")
    report["libraries"] = {"released_build": old_original, "released_loader_file": old_library,
                           "current_build": new_original, "replacement_loader_file": new_library}
    # Reuse the current C99 full-offset/size and capability contract checks.
    # They complement the old binary without maintaining a second catalogue.
    command(["cmake", "--build", build, "--config", "Release", "--target",
             "capi_library_shared", "capi_library_static", "--parallel", "4"],
            ROOT, report_dir, "current-layout-build")
    command(["ctest", "--test-dir", build, "-C", "Release", "--output-on-failure",
             "--no-tests=error", "-R", "^capi_library_(shared|static)$"],
            ROOT, report_dir, "current-layout-contracts")
    report["current_layout_checks"] = ["capi_library_shared", "capi_library_static"]
    client_build = work / "client-build"
    link_library = Path(old_library["path"])
    if system == "Windows":
        link_library = Path(old_original["path"]).with_suffix(".lib")
        if not link_library.is_file():
            raise GateFailure("import_library", "released DLL import library ipropc.lib is missing")
        report["released_import_library"] = identity(link_library)
    configure = ["cmake", *(["-G", args.generator] if args.generator else []), "-S", frozen, "-B", client_build,
                 "-DCMAKE_BUILD_TYPE=Release", f"-DIPROP_LEGACY_LIBRARY={link_library}",
                 f"-DCMAKE_BUILD_RPATH={Path(old_library['path']).parent}"]
    if system == "Windows":
        # Release /MD matches the existing static-zlib/shared-CRT CI setup.
        # Leave frozen source/CMake bytes intact despite /WX secure-CRT warnings.
        configure += ["-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
                      "-DCMAKE_C_FLAGS=/D_CRT_SECURE_NO_WARNINGS /utf-8"]
    command(configure, ROOT, report_dir, "client-configure")
    command(["cmake", "--build", client_build, "--config", "Release"], ROOT, report_dir, "client-build")
    executable = client_build / ("iprop_legacy_v02.exe" if system == "Windows" else "iprop_legacy_v02")
    if not executable.exists():
        executable = client_build / "Release" / executable.name
    client = identity(executable)
    report["client"] = client
    # Keep the once-built binary in CI artifacts, independently of the private
    # build tree. The actual old/new runs below use the original exact path.
    shutil.copy2(executable, report_dir / executable.name)
    command(["cmake", "--build", build, "--config", "Release", "--target", "capi_legacy_fixtures", "--parallel", "4"], ROOT, report_dir, "current-fixtures")
    fixtures = [build / "capi-golden/uniforms.iprop", build / "capi-golden/legacy-output-owners.iprop"]
    report["fixtures"] = [identity(path) for path in fixtures]
    sha = client["sha256"]
    report["released_run"] = observe(executable, sha, fixtures, old_library, old_library, report_dir, "released-run")
    report["replacement_run"] = observe(executable, sha, fixtures, new_library, new_library, report_dir, "replacement-run")
    # The old library can return the very same six-export report. Actual loader
    # evidence must still reject it when the requested replacement was not used.
    controls = {"wrong_library": reject_control("library_path", lambda:
        observe(executable, sha, fixtures, old_library, new_library, report_dir, "wrong-library-control"))}
    tampered = work / ("tampered-client.exe" if system == "Windows" else "tampered-client")
    shutil.copy2(executable, tampered)
    with tampered.open("ab") as stream:
        stream.write(b"ABI negative control\n")
    controls["client_tamper"] = reject_control("client_identity", lambda:
        observe(tampered, sha, fixtures, new_library, new_library, report_dir, "tamper-control"))
    report["negative_controls"] = controls
    require_client(executable, sha)
    for fixture in report["fixtures"]:
        if digest(fixture["path"]) != fixture["sha256"]:
            raise GateFailure("fixture_identity", "a fixture changed between old/new client runs")
    report["client_after_sha256"] = digest(executable)
    report["status"] = "pass"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--repository", type=Path, default=ROOT)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--generator", default="Ninja" if shutil.which("ninja") else None)
    parser.add_argument("--static-zlib", choices=("ON", "OFF"), default="OFF")
    args = parser.parse_args()
    args.work = args.work or args.build / "legacy-abi"
    args.report = args.report or args.build / "ci-reports/legacy-abi"
    args.report.mkdir(parents=True, exist_ok=True)
    report = {"schema_version": 1, "status": "failed"}
    try:
        gate(args, report)
    except Exception as error:
        report["error"] = {"code": getattr(error, "code", "unexpected"), "message": str(error)}
        raise
    finally:
        (args.report / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Frozen API 0.2 client/new shared library: PASS (actual loader path/hash, layouts, six exports, negative controls)")


if __name__ == "__main__":
    main()
