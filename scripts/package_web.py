"""Archive the current WASM and website, then test their extracted copies."""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from release_common import (ROOT, committed_source, extract_archive, product_version,
                            run, sha256, versions, write_archive, write_checksums, write_json)
from language_docs import build as build_documentation, verify as verify_documentation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wasm-dir", type=Path, default=ROOT / "build/wasm")
    parser.add_argument("--website-dir", type=Path, default=ROOT / "website/dist")
    parser.add_argument("--tools-dir", type=Path, default=ROOT / "build/tools")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    version, commit = product_version(), committed_source()
    wasm, website, tools = args.wasm_dir.resolve(), args.website_dir.resolve(), args.tools_dir.resolve()
    for name in ("iprop.js", "iprop.wasm"):
        if (wasm / name).read_bytes() != (website / name).read_bytes():
            raise RuntimeError("The website must contain the current WASM build: " + name)
    destination = args.output_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    # Documentation verification below proves that the compiler exports these versions.
    manifest = {**versions(ROOT), "source_commit": commit, "wasm_sha256": sha256(wasm / "iprop.wasm")}
    artifacts = []
    with tempfile.TemporaryDirectory(prefix="iprop-web-release-") as temporary:
        temporary = Path(temporary)
        documentation = temporary / "language-docs"
        compiler = tools / ("ipc.exe" if sys.platform == "win32" else "ipc")
        build_documentation(compiler, documentation)
        for kind in ("wasm", "website"):
            prefix = temporary / f"iprop-{version}-{kind}"
            if kind == "website":
                shutil.copytree(website, prefix)
            else:
                prefix.mkdir()
                for name in ("iprop.js", "iprop.wasm"):
                    shutil.copy2(wasm / name, prefix / name)
                shutil.copy2(ROOT / "backends/html5/js/api.js", prefix / "api.js")
                shutil.copy2(ROOT / "backends/html5/README.md", prefix / "README.md")
                shutil.copytree(documentation, prefix, dirs_exist_ok=True)
            verify_documentation(prefix, version, commit, root=ROOT, compiler=compiler, clean=True)
            shutil.copy2(ROOT / "LICENSE", prefix / "LICENSE")
            shutil.copytree(ROOT / "licenses", prefix / "licenses", dirs_exist_ok=True)
            (prefix / "licenses").mkdir(exist_ok=True)
            shutil.copy2(ROOT / "third_party/zlib-LICENSE.txt", prefix / "licenses/zlib-LICENSE.txt")
            write_json(prefix / "release.json", {**manifest, "kind": kind})
            archive = destination / (prefix.name + ".tar.gz")
            write_archive(prefix, archive)
            extracted = temporary / (kind + "-extracted")
            extract_archive(archive, extracted)
            verify_documentation(extracted / prefix.name, version, commit, root=ROOT, compiler=compiler, clean=True)
            if kind == "website":
                run("node", ROOT / "website/test/release-smoke.js", extracted / prefix.name, tools)
            else:
                run("node", ROOT / "backends/html5/test/run-tests.js", extracted / prefix.name, tools)
            artifacts.append(archive)
    validation = destination / f"iprop-{version}-web-validation.json"
    write_json(validation, {**manifest, "passed": True,
                           "checks": {"extracted_wasm": True, "extracted_website": True,
                                      "documentation": True},
                           "artifacts": {path.name: sha256(path) for path in artifacts}})
    write_checksums(destination / f"iprop-{version}-web-SHA256SUMS.txt", [*artifacts, validation])
    print("Created and verified " + ", ".join(path.name for path in artifacts))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print("web package: " + str(error), file=sys.stderr)
        sys.exit(1)
