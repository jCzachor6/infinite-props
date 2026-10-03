"""Package the committed source and a tested, relocatable native install."""
import argparse
import hashlib
import json
import platform
import re
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def run(*args, capture=False):
    result = subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def read_cache(build):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        match = re.match(r"([^/#][^:]*):[^=]*=(.*)$", line)
        if match:
            cache[match[1]] = match[2]
    return cache


def main():
    version = re.search(r"\bVERSION\s+(\d+\.\d+\.\d+)",
                        (ROOT / "CMakeLists.txt").read_text()).group(1)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?", default=version)
    parser.add_argument("build_directory", nargs="?", type=Path, default=ROOT / "build")
    parser.add_argument("--check", action="store_true", help="validate only; write no archives")
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
    if not args.check and run("git", "status", "--porcelain", capture=True):
        raise RuntimeError("Commit the source changes before packaging the HEAD source archive")

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
        godot_project = prefix / "share/iprop/godot"
        if godot_project.exists() and any((godot_project / "bin").glob("*.debug.*")):
            raise RuntimeError("Use GODOTCPP_TARGET=template_release for the release package")
        run("cmake", f"-DPREFIX={prefix}", f"-DWORK={temporary / 'smoke'}",
            f"-DZLIB_USE_STATIC_LIBS={cache.get('ZLIB_USE_STATIC_LIBS', 'OFF')}",
            "-P", ROOT / "scripts/install_smoke.cmake")
        if godot_project.exists():
            engine = cache.get("IPROP_GODOT_BINARY")
            if not engine or not Path(engine).is_file():
                raise RuntimeError("A Godot package requires its conformance engine")
            run(engine, "--headless", "--path", godot_project,
                "--script", "res://conformance.gd", "--", build / "capi-golden", ROOT / "tests/golden")
        if args.check:
            print("Release package validation: PASS (no archives created)")
            return

        commit = run("git", "rev-parse", "HEAD", capture=True)
        api = (ROOT / "backends/capi/iprop.h").read_text()
        api_version = ".".join(re.search(rf"#define IPROP_API_VERSION_{part}\s+(\d+)", api).group(1)
                               for part in ("MAJOR", "MINOR"))
        manifest = {"product_version": version, "c_api_version": api_version,
                    "source_commit": commit, "system": system, "architecture": arch,
                    "backends": {"capi": True,
                                 "sfml": (prefix / "bin" / ("iprop_sfml.exe" if system == "windows" else "iprop_sfml")).exists(),
                                 "godot": godot_project.exists()}}
        (prefix / "release.json").write_text(json.dumps(manifest, indent=2) + "\n")
        dist = ROOT / "dist"
        dist.mkdir(exist_ok=True)
        source = dist / f"iprop-{version}.tar.gz"
        native = dist / f"{name}.tar.gz"
        run("git", "archive", "--format=tar.gz", f"--prefix=iprop-{version}/", "-o", source, "HEAD")
        with tarfile.open(native, "w:gz") as archive:
            archive.add(prefix, arcname=name)
        checksums = []
        for artifact in sorted(dist.glob(f"iprop-{version}*.tar.gz")):
            checksums.append(f"{hashlib.sha256(artifact.read_bytes()).hexdigest()}  {artifact.name}\n")
        (dist / f"iprop-{version}-SHA256SUMS.txt").write_text("".join(checksums))
        print(f"Created {source.relative_to(ROOT)} and {native.relative_to(ROOT)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"release: {error}", file=sys.stderr)
        sys.exit(1)
