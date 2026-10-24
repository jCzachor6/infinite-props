#!/usr/bin/env python3
"""Build a relocatable CIprop XCFramework from the same runtime sources."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root, help="Complete runtime source checkout")
    parser.add_argument("--build", type=Path, default=root / "build/apple-native")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "Artifacts/CIprop.xcframework")
    args = parser.parse_args()
    root = args.source.resolve()
    if not (root / "CMakeLists.txt").is_file():
        parser.error("Provide --source pointing to a complete Infinite Props source checkout")
    build, output = args.build.resolve(), args.output.resolve()
    env = dict(os.environ)
    if "DEVELOPER_DIR" not in env and Path("/Applications/Xcode.app/Contents/Developer").is_dir():
        env["DEVELOPER_DIR"] = "/Applications/Xcode.app/Contents/Developer"

    def run(*cmd):
        subprocess.run([str(x) for x in cmd], check=True, env=env)

    if output.exists():
        parser.error(f"Output already exists: {output}; choose another --output or move it first")
    libraries = []
    for sdk, system, architectures, minimum in [
        ("macosx", "Darwin", "arm64;x86_64", "12.0"),
        ("iphoneos", "iOS", "arm64", "15.0"),
        ("iphonesimulator", "iOS", "arm64;x86_64", "15.0"),
    ]:
        directory = build / sdk
        headers = directory / "Headers"
        headers.mkdir(parents=True, exist_ok=True)
        run("cmake", "-S", root, "-B", directory, "-DCMAKE_BUILD_TYPE=Release",
            "-DIPROP_RUNTIME_ONLY=ON", "-DBUILD_SHARED_LIBS=OFF",
            f"-DCMAKE_SYSTEM_NAME={system}", f"-DCMAKE_OSX_SYSROOT={sdk}",
            f"-DCMAKE_OSX_ARCHITECTURES={architectures}", f"-DCMAKE_OSX_DEPLOYMENT_TARGET={minimum}")
        run("cmake", "--build", directory, "--target", "iprop_capi_static", "--parallel", "4")
        (headers / "iprop.h").write_bytes((root / "backends/capi/iprop.h").read_bytes())
        (headers / "module.modulemap").write_text('module CIprop { header "iprop.h" export * }\n')
        library = directory / "libCIprop.a"
        run("xcrun", "libtool", "-static", "-o", library,
            directory / "backends/capi/libipropc.a", directory / "runtime/libiprop_runtime.a")
        libraries.extend(["-library", library, "-headers", headers])
    output.parent.mkdir(parents=True, exist_ok=True)
    run("xcodebuild", "-create-xcframework", *libraries, "-output", output)


if __name__ == "__main__":
    main()
