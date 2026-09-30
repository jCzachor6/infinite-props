#!/usr/bin/env python3
"""Make Godot 4.5's universal macOS template usable for a single-arch export."""

import argparse
import copy
from pathlib import Path
import subprocess
import tempfile
import zipfile


def prepare(source: Path, architecture: str, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        tmp = Path(temporary)
        archive = tmp / "template.zip"
        found = set()
        with zipfile.ZipFile(source) as original, zipfile.ZipFile(archive, "w") as target:
            for info in original.infolist():
                entry = copy.copy(info)
                data = original.read(info)
                for flavor in ("debug", "release"):
                    binary = f"godot_macos_{flavor}"
                    if info.filename.endswith(binary + ".universal"):
                        universal = tmp / "universal"
                        thin = tmp / "thin"
                        universal.write_bytes(data)
                        subprocess.run(["lipo", str(universal), "-thin", architecture,
                                        "-output", str(thin)], check=True)
                        data = thin.read_bytes()
                        entry.filename = info.filename.removesuffix("universal") + architecture
                        found.add(flavor)
                    elif info.filename.endswith(binary + "." + architecture):
                        found.add(flavor)
                target.writestr(entry, data)
        if "release" not in found:
            raise ValueError(f"template has no release binary for {architecture}")
        archive.replace(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("architecture", choices=("arm64", "x86_64"))
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    prepare(arguments.source, arguments.architecture, arguments.output)
