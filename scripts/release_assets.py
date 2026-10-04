"""Collect same-commit CI artifacts into one verified release asset set."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

from release_common import ROOT, api_version, committed_source, sha256, write_checksums, write_json
from release_preflight import metadata


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def identity(value, version, commit):
    require(value.get("product_version") == version and value.get("source_commit") == commit,
            "Artifact version/source commit differs from the release checkout")


def archive_manifest(file):
    with tarfile.open(file, "r:gz") as archive:
        member = archive.getmember(file.name[:-7] + "/release.json")
        require(member.isfile() and member.size < 65536, "Invalid archive manifest")
        return json.load(archive.extractfile(member))


def verify_source(file, version, commit, root=ROOT):
    # Compare every Git blob, rather than assuming gzip implementations on
    # different runners necessarily produce the same container bytes.
    raw = subprocess.check_output(["git", "ls-tree", "-r", "-z", commit], cwd=root)
    expected = {}
    for entry in raw.split(b"\0"):
        if not entry:
            continue
        info, name = entry.split(b"\t", 1)
        mode, kind, digest = info.decode("ascii").split()
        require(kind == "blob", "Source archives do not support Git submodules")
        expected[name.decode("utf-8")] = (mode, digest)
    with tarfile.open(file, "r:gz") as archive:
        require(archive.pax_headers.get("comment") == commit, "Source archive is not from the tagged commit")
        found = set()
        prefix = f"iprop-{version}/"
        for member in archive:
            if member.isdir():
                continue
            require(member.name.startswith(prefix), "Source archive has an unexpected root")
            name = member.name[len(prefix):]
            require(name in expected and name not in found, "Unexpected or duplicate source member: " + name)
            mode, digest = expected[name]
            if mode == "120000":
                require(member.issym(), "Source symlink changed: " + name)
                data = member.linkname.encode("utf-8")
            else:
                require(member.isfile(), "Source file type changed: " + name)
                require(bool(member.mode & 0o111) == (mode == "100755"), "Source executable bit changed: " + name)
                data = archive.extractfile(member).read()
            actual = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
            require(actual == digest, "Source file differs from Git: " + name)
            found.add(name)
        require(found == set(expected), "Source archive omits tracked files")


def verify_jobs(jobs, version, commit, run_id=None):
    patterns = [r"reports-native-Linux-.+", r"reports-native-macOS-.+", r"reports-native-Windows-.+",
                r"reports-wasm", r"reports-native-backends", r"reports-sanitizers"]
    require(len(jobs) == 6, "Require exactly six CI job reports")
    for pattern in patterns:
        require(sum(re.fullmatch(pattern, name) is not None for name in jobs) == 1,
                "Missing or duplicate required CI job: " + pattern)
    for name, report in jobs.items():
        identity(report, version, commit)
        require(report.get("passed") is True and report.get("steps") and
                all(step.get("passed") is True and step.get("returncode") == 0 for step in report["steps"]),
                "A CI job did not pass: " + name)
        require(not report.get("report_errors") and report.get("test_reports") and
                sum(test["tests"] - test.get("skipped", 0) for test in report["test_reports"]) > 0 and
                all(test["failures"] == 0 for test in report["test_reports"]),
                "Missing or failed JUnit tests: " + name)
        if run_id is not None:
            require(report.get("github", {}).get("run_id") == str(run_id), "Report belongs to a different CI run")


def verify_packages(directory, version, commit, root=ROOT):
    prefix = "iprop-" + version
    source = directory / (prefix + ".tar.gz")
    require(source.is_file(), "Missing source archive")
    verify_source(source, version, commit, root)
    natives = {}
    for file in directory.glob(prefix + "-*.tar.gz"):
        match = re.fullmatch(re.escape(prefix) + r"-(linux|macos|windows)-([a-zA-Z0-9_]+)\.tar\.gz", file.name)
        if not match:
            continue
        system, architecture = match.groups()
        require(system not in natives, "Duplicate native platform: " + system)
        natives[system] = file
        manifest = archive_manifest(file)
        identity(manifest, version, commit)
        require(manifest.get("system") == system and manifest.get("architecture") == architecture,
                "SDK filename and manifest platform differ")
        require(manifest.get("backends", {}).get("capi") is True, "SDK omits the C API")
        require(manifest.get("c_api_version") == api_version(root), "SDK contains a different C API version")
        with tarfile.open(file, "r:gz") as archive:
            header = archive.extractfile(file.name[:-7] + "/include/iprop.h").read()
            require(header.replace(b"\r\n", b"\n") == (root / "backends/capi/iprop.h").read_bytes().replace(b"\r\n", b"\n"),
                    "Archived C API header differs from the source")
        if system == "linux":
            require(manifest["backends"].get("sfml") is True and manifest["backends"].get("godot") is True,
                    "Linux SDK must include the tested SFML/Godot integrations")
        validation = json.loads((directory / (file.name[:-7] + "-validation.json")).read_text(encoding="utf-8"))
        identity(validation, version, commit)
        require(validation.get("passed") is True and
                all(validation.get("checks", {}).get(key) is True for key in ("ctest", "relocated_sdk", "extracted_sdk")),
                "SDK archive did not pass extraction validation")
        require(validation.get("backends") == manifest["backends"] and
                validation.get("c_api_version") == manifest["c_api_version"], "SDK validation describes a different API or backends")
        for name, digest in validation.get("artifacts", {}).items():
            require(name in (file.name, source.name) and sha256(directory / name) == digest,
                    "SDK validation digest mismatch: " + name)
        require(file.name in validation.get("artifacts", {}), "SDK validation omits its archive")
    require(set(natives) == {"linux", "macos", "windows"}, "Require Linux, macOS and Windows SDKs")
    web = json.loads((directory / (prefix + "-web-validation.json")).read_text(encoding="utf-8"))
    identity(web, version, commit)
    require(web.get("passed") is True and all(web.get("checks", {}).get(key) is True
                                            for key in ("extracted_wasm", "extracted_website")),
            "WASM/website did not pass extraction validation")
    for kind in ("wasm", "website"):
        file = directory / (prefix + "-" + kind + ".tar.gz")
        require(sha256(file) == web.get("artifacts", {}).get(file.name), "Web validation digest mismatch")
        manifest = archive_manifest(file)
        identity(manifest, version, commit)
        require(manifest.get("kind") == kind and manifest.get("wasm_sha256") == web.get("wasm_sha256"),
                "Web archive and validation manifest differ")
        with tarfile.open(file, "r:gz") as archive:
            wasm = archive.extractfile(file.name[:-7] + "/iprop.wasm").read()
            require(hashlib.sha256(wasm).hexdigest() == web["wasm_sha256"], "Archived WASM module differs")


def collect(inputs, destination, version, commit, root=ROOT):
    require(not destination.exists() or not any(destination.iterdir()), "Use an empty release output directory")
    destination.mkdir(parents=True, exist_ok=True)
    jobs = {}
    for directory in sorted(inputs.iterdir()):
        if directory.is_dir() and (directory.name in ("reports-wasm", "reports-native-backends", "reports-sanitizers") or
                                  re.fullmatch(r"reports-native-(Linux|macOS|Windows)-.+", directory.name)):
            summaries = list(directory.rglob("summary.json"))
            require(len(summaries) == 1, "Require one summary per job: " + directory.name)
            jobs[directory.name] = json.loads(summaries[0].read_text(encoding="utf-8"))
        elif directory.is_dir() and (directory.name.startswith("sdk-") or directory.name == "web-build"):
            for file in sorted(directory.rglob("iprop-*")):
                if not file.is_file() or file.name.endswith("SHA256SUMS.txt"):
                    continue
                require(file.name.startswith("iprop-" + version + "-") or file.name == "iprop-" + version + ".tar.gz",
                        "Artifact contains a different version: " + file.name)
                require(not (destination / file.name).exists(), "Duplicate release asset: " + file.name)
                shutil.copy2(file, destination / file.name)
    verify_jobs(jobs, version, commit, os.environ.get("GITHUB_RUN_ID"))
    verify_packages(destination, version, commit, root)
    ci = destination / f"iprop-{version}-ci-validation.json"
    write_json(ci, {"product_version": version, "source_commit": commit, "passed": True, "jobs": jobs})
    files = sorted(destination.iterdir())
    expected = 6 + 4 + 1  # source + three SDKs + two web archives, four validation reports, CI report
    require(len(files) == expected, "Unexpected release asset count")
    release = destination / f"iprop-{version}-release.json"
    write_json(release, {"product_version": version, "source_commit": commit, "passed": True,
                         "artifacts": {file.name: {"sha256": sha256(file), "bytes": file.stat().st_size} for file in files}})
    write_checksums(destination / f"iprop-{version}-SHA256SUMS.txt", [*files, release])
    verify_bundle(destination, version, commit, root)


def verify_bundle(directory, version, commit, root=ROOT):
    prefix = "iprop-" + version
    release = directory / (prefix + "-release.json")
    ledger = json.loads(release.read_text(encoding="utf-8"))
    identity(ledger, version, commit)
    require(ledger.get("passed") is True and len(ledger.get("artifacts", {})) == 11, "Incomplete release manifest")
    files = []
    for name, record in ledger["artifacts"].items():
        require(re.fullmatch(r"[a-zA-Z0-9_.-]+", name) is not None and name.startswith(prefix), "Invalid release asset name")
        file = directory / name
        require(file.is_file() and file.stat().st_size == record["bytes"] and sha256(file) == record["sha256"],
                "Release asset changed: " + name)
        files.append(file)
    checksums = directory / (prefix + "-SHA256SUMS.txt")
    require({file.name for file in directory.iterdir()} == {file.name for file in files} | {release.name, checksums.name},
            "Unlisted or missing release assets")
    expected = "".join(f"{sha256(file)}  {file.name}\n" for file in sorted([*files, release]))
    require(checksums.read_text(encoding="utf-8") == expected, "Release SHA256SUMS differs")
    ci = json.loads((directory / (prefix + "-ci-validation.json")).read_text(encoding="utf-8"))
    identity(ci, version, commit)
    require(ci.get("passed") is True, "CI validation failed")
    verify_jobs(ci["jobs"], version, commit, os.environ.get("GITHUB_RUN_ID"))
    verify_packages(directory, version, commit, root)
    return [*files, release, checksums]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("collect", "verify"))
    parser.add_argument("--input-dir", type=Path, default=ROOT / "build/downloads")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/release")
    args = parser.parse_args()
    commit = committed_source()
    version = metadata(ROOT)["product_version"]
    if args.action == "collect":
        collect(args.input_dir, args.output_dir, version, commit)
    else:
        verify_bundle(args.output_dir, version, commit)
    print("Release assets: PASS " + version + " at " + commit)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, tarfile.TarError, subprocess.CalledProcessError) as error:
        print("release assets: " + str(error), file=sys.stderr)
        sys.exit(1)
