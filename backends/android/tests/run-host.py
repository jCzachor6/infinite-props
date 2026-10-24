#!/usr/bin/env python3
"""Exercise the real JNI bridge under a host JVM with CheckJNI enabled."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kotlinc", default=shutil.which("kotlinc"))
    parser.add_argument("--java-home", default=os.environ.get("JAVA_HOME"))
    parser.add_argument("--build", type=Path, default=root / "build/android-host")
    parser.add_argument("--fixtures", type=Path, default=root / "build/binding-fixtures")
    args = parser.parse_args()
    if not args.kotlinc or not args.java_home:
        parser.error("Provide --kotlinc and --java-home (or JAVA_HOME)")
    build = args.build.resolve()
    env = dict(os.environ, JAVA_HOME=args.java_home)
    def run(*command):
        subprocess.run([str(x) for x in command], check=True, env=env)
    source = root / "backends/android/iprop/src/main"
    run("cmake", "-S", source / "cpp", "-B", build, "-DCMAKE_BUILD_TYPE=Release")
    run("cmake", "--build", build, "--target", "iprop_jni", "--parallel", "4")
    run(args.kotlinc, source / "java/dev/infiniteprops/Program.kt",
        root / "backends/android/tests/shared/Conformance.kt", root / "backends/android/tests/Main.kt",
        "-jvm-target", "17", "-include-runtime", "-d", build / "conformance.jar")
    run(Path(args.java_home) / "bin/java", "-Xcheck:jni", f"-Djava.library.path={build}",
        "-jar", build / "conformance.jar", args.fixtures.resolve())


if __name__ == "__main__":
    main()
