"""Package the committed source and a tested, relocatable native install."""
import argparse
import json
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from release_common import (ROOT, api_version, committed_source, extract_archive, product_version,
                            run, sha256, write_archive, write_checksums, write_json)


def read_cache(build):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        match = re.match(r"([^/#][^:]*):[^=]*=(.*)$", line)
        if match:
            cache[match[1]] = match[2]
    return cache


def validate_install(prefix, work, cache, build):
    godot_project = prefix / "share/iprop/godot"
    if godot_project.exists() and any((godot_project / "bin").glob("*.debug.*")):
        raise RuntimeError("Use GODOTCPP_TARGET=template_release for the release package")
    run("cmake", f"-DPREFIX={prefix}", f"-DWORK={work}",
        f"-DZLIB_USE_STATIC_LIBS={cache.get('ZLIB_USE_STATIC_LIBS', 'OFF')}",
        "-P", ROOT / "scripts/install_smoke.cmake")
    if godot_project.exists():
        engine = cache.get("IPROP_GODOT_BINARY")
        if not engine or not Path(engine).is_file():
            raise RuntimeError("A Godot package requires its conformance engine")
        run(engine, "--headless", "--path", godot_project,
            "--script", "res://conformance.gd", "--", build / "capi-golden", ROOT / "tests/golden")


def main():
    version = product_version()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?", default=version)
    parser.add_argument("build_directory", nargs="?", type=Path, default=ROOT / "build")
    parser.add_argument("--check", action="store_true", help="validate only; write no archives")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--no-source", action="store_true", help="package only this platform's SDK")
    args = parser.parse_args()
    build = args.build_directory.resolve()
    cache = read_cache(build)
    if args.version != version or cache.get("CMAKE_PROJECT_VERSION") != version:
        raise RuntimeError("Requested, source and built product versions must match")
    if Path(cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != ROOT:
        raise RuntimeError("Build directory belongs to a different source checkout")
    if cache.get("IPROP_BUILD_TESTS") != "ON":
        raise RuntimeError("Configure the release build with IPROP_BUILD_TESTS=ON")
    if cache.get("CMAKE_BUILD_TYPE", "Release") not in ("", "Release"):
        raise RuntimeError("Use a Release build for the native package")
    commit = committed_source() if not args.check else None

    run("ctest", "--test-dir", build, "-C", "Release", "--output-on-failure")
    system = {"Darwin": "macos", "Windows": "windows"}.get(platform.system(), platform.system().lower())
    arch = {"aarch64": "arm64", "AMD64": "x86_64"}.get(platform.machine(), platform.machine())
    name = f"iprop-{version}-{system}-{arch}"
    with tempfile.TemporaryDirectory(prefix="iprop-release-") as temporary:
        temporary = Path(temporary)
        staging = temporary / "staging"
        run("cmake", "--install", build, "--config", "Release", "--prefix", staging)
        # Rename before compiling consumers and running the installed tools.
        # Exported CMake targets and native loaders must resolve from here.
        prefix = temporary / name
        staging.rename(prefix)
        validate_install(prefix, temporary / "smoke", cache, build)
        if args.check:
            print("Release package validation: PASS (no archives created)")
            return

        manifest = {"product_version": version, "c_api_version": api_version(),
                    "source_commit": commit, "system": system, "architecture": arch,
                    "backends": {"capi": True,
                                 "sfml": (prefix / "bin" / ("iprop_sfml.exe" if system == "windows" else "iprop_sfml")).exists(),
                                 "godot": (prefix / "share/iprop/godot").exists()}}
        write_json(prefix / "release.json", manifest)
        dist = args.output_dir.resolve()
        dist.mkdir(parents=True, exist_ok=True)
        native = dist / f"{name}.tar.gz"
        artifacts = [native]
        if not args.no_source:
            source = dist / f"iprop-{version}.tar.gz"
            run("git", "archive", "--format=tar.gz", f"--prefix=iprop-{version}/", "-o", source, "HEAD")
            artifacts.append(source)
        write_archive(prefix, native)
        extracted = temporary / "extracted"
        extract_archive(native, extracted)
        archived_manifest = json.loads((extracted / name / "release.json").read_text(encoding="utf-8"))
        if archived_manifest != manifest:
            raise RuntimeError("The archived SDK manifest differs from the tested install")
        validate_install(extracted / name, temporary / "archive-smoke", cache, build)
        validation = dist / f"{name}-validation.json"
        write_json(validation, {**manifest, "passed": True,
                               "checks": {"ctest": True, "relocated_sdk": True, "extracted_sdk": True},
                               "artifacts": {path.name: sha256(path) for path in artifacts}})
        # Never pick up stale archives from previous invocations in this directory.
        write_checksums(dist / f"iprop-{version}-SHA256SUMS.txt", [*artifacts, validation])
        print("Created and verified " + ", ".join(str(path) for path in artifacts))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"release: {error}", file=sys.stderr)
        sys.exit(1)
