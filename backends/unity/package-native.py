#!/usr/bin/env python3
"""Build platform-specific native plugins into a Unity UPM package."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid


def meta(path, platform, cpu):
    editor = platform in ("OSXUniversal", "Win64", "Linux64")
    os_name = {"OSXUniversal": "OSX", "Win64": "Windows", "Linux64": "Linux"}.get(platform, "AnyOS")
    # Explicit import settings prevent a library for a different target from
    # being loaded in the editor. GUIDs remain stable when the plugin rebuilds.
    identity = platform + "/" + cpu + ("/models" if "iprop3dc" in path.name else "")
    data = f"""fileFormatVersion: 2
guid: {uuid.uuid5(uuid.NAMESPACE_URL, 'https://infiniteprops.dev/unity/' + identity).hex}
PluginImporter:
  externalObjects: {{}}
  serializedVersion: 2
  iconMap: {{}}
  executionOrder: {{}}
  defineConstraints: []
  isPreloaded: 0
  isOverridable: 0
  isExplicitlyReferenced: 0
  validateReferences: 1
  platformData:
  - first:
      Any:
    second:
      enabled: 0
      settings: {{}}
  - first:
      Editor: Editor
    second:
      enabled: {int(editor)}
      settings:
        CPU: {cpu if platform != 'OSXUniversal' else 'AnyCPU'}
        OS: {os_name}
  - first:
      {'Standalone' if editor else platform}: {'iOS' if platform == 'iPhone' else platform}
    second:
      enabled: 1
      settings:
        CPU: {cpu}
  userData:
  assetBundleName:
  assetBundleVariant:
"""
    Path(str(path) + ".meta").write_text(data)


def main():
    root = Path(__file__).resolve().parents[2]
    default = "macos" if sys.platform == "darwin" else "windows" if sys.platform == "win32" else "linux"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", choices=["macos", "windows", "linux", "android", "ios"], default=default)
    parser.add_argument("--source", type=Path, default=root)
    parser.add_argument("--build", type=Path, default=root / "build/unity-native")
    parser.add_argument("--package", type=Path, default=Path(__file__).resolve().parent / "Packages/dev.infiniteprops.runtime")
    parser.add_argument("--ndk", type=Path, help="Android NDK root (required for Android)")
    parser.add_argument("--models", action="store_true", help="Include the model API 0.3 plugin")
    args = parser.parse_args()
    env = dict(os.environ)
    if sys.platform == "darwin" and "DEVELOPER_DIR" not in env and Path("/Applications/Xcode.app/Contents/Developer").is_dir():
        env["DEVELOPER_DIR"] = "/Applications/Xcode.app/Contents/Developer"
    def run(*cmd):
        subprocess.run([str(x) for x in cmd], check=True, env=env)
    def build(name, *options, target="iprop_capi"):
        directory = args.build.resolve() / name
        run("cmake", "-S", args.source.resolve(), "-B", directory, "-DCMAKE_BUILD_TYPE=Release",
            "-DIPROP_RUNTIME_ONLY=ON", f"-DIPROP_RUNTIME_MODELS={'ON' if args.models else 'OFF'}", "-DBUILD_SHARED_LIBS=OFF", *options)
        targets = [target]
        if args.models:
            targets.append("iprop_capi3d" if name == "ios" else "iprop_capi3d_shared")
        run("cmake", "--build", directory, "--config", "Release", "--target", *targets, "--parallel", "4")
        return directory
    def publish(directory, source, relative, platform, cpu):
        destination = args.package.resolve() / "Plugins" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        meta(destination, platform, cpu)
        print("Packaged", destination)
        if args.models and platform != "iPhone":
            name = source.name.replace("ipropc-0.5", "ipropc").replace("ipropc", "iprop3dc")
            plugin = directory / "backends/capi3d" / ("Release" if source.parent.name == "Release" else "") / name
            companion = destination.with_name(name)
            shutil.copyfile(plugin, companion)
            meta(companion, platform, cpu)
    if args.platform == "macos":
        d = build("macos", "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0")
        publish(d, d / "backends/capi/libipropc.dylib", "macOS/libipropc.dylib", "OSXUniversal", "AnyCPU")
    elif args.platform == "windows":
        d = build("windows", "-A", "x64")
        dll = d / "backends/capi/Release/ipropc-0.5.dll"
        if not dll.exists():
            dll = d / "backends/capi/ipropc-0.5.dll"
        publish(d, dll, "x86_64/ipropc-0.5.dll", "Win64", "x86_64")
        # zlib is a runtime dependency when the selected SDK uses shared zlib.
        # Use a static-zlib toolchain, or package its DLL beside this plugin.
    elif args.platform == "linux":
        d = build("linux")
        publish(d, d / "backends/capi/libipropc.so", "x86_64/libipropc.so", "Linux64", "x86_64")
    elif args.platform == "android":
        if not args.ndk:
            parser.error("--ndk is required for Android")
        for abi, cpu in [("arm64-v8a", "ARM64"), ("armeabi-v7a", "ARMv7"), ("x86_64", "x86_64")]:
            d = build(abi, f"-DCMAKE_TOOLCHAIN_FILE={args.ndk.resolve()}/build/cmake/android.toolchain.cmake",
                f"-DANDROID_ABI={abi}", "-DANDROID_PLATFORM=android-23", "-DANDROID_STL=c++_static",
                "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384")
            publish(d, d / "backends/capi/libipropc.so", f"Android/{abi}/libipropc.so", "Android", cpu)
    else:
        d = build("ios", "-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_OSX_SYSROOT=iphoneos",
            "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0", target="iprop_capi_static")
        combined = d / "libipropc.a"
        run("xcrun", "libtool", "-static", "-o", combined,
            d / "backends/capi/libipropc.a", d / "runtime/libiprop_runtime.a",
            *([d / "backends/capi3d/libiprop3dc.a", d / "model/libiprop_model.a"] if args.models else []))
        publish(d, combined, "iOS/libipropc.a", "iPhone", "ARM64")


if __name__ == "__main__":
    main()
