"""Validate release metadata and require an existing version tag on main."""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from release_common import ROOT, committed_source, versions, write_json


def metadata(root, tag=None, publishing=False):
    release = versions(root)
    version = release["product_version"]
    if tag is not None and tag != "v" + version:
        raise RuntimeError("Tag must match the product version: v" + version)
    for file in ("editor/package.json", "website/package.json", "website/package-lock.json"):
        package = json.loads((root / file).read_text(encoding="utf-8"))
        if package.get("version") != version:
            raise RuntimeError(file + " version differs from CMake")
        if "packages" in package and package["packages"].get("", {}).get("version") != version:
            raise RuntimeError(file + " root package version differs from CMake")
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    match = re.search(r"^## v" + re.escape(version) + r"\s+[^\n]+\n(.*?)(?=^## |\Z)", changelog, re.M | re.S)
    if not match:
        raise RuntimeError("The changelog must contain the current version")
    notes = match.group(0).strip() + "\n"
    if publishing:
        heading = notes.splitlines()[0]
        date = re.fullmatch(r"## v" + re.escape(version) + r" — (\d{4}-\d{2}-\d{2})", heading)
        if not date:
            raise RuntimeError("Date the changelog as '## v" + version + " — YYYY-MM-DD' before tagging")
        datetime.date.fromisoformat(date.group(1))
        # The format is unversioned; each product line freezes the
        # specification of the format it ships.
        line = ".".join(release["product_version"].split(".")[:2])
        name = "spec/iprop-v" + line + ".md"
        try:
            spec = (root / name).read_text(encoding="utf-8")
        except FileNotFoundError:
            raise RuntimeError("Missing the format specification " + name) from None
        title = re.search(r"^# .*\bSpecification v(\d+\.\d+)\s*$", spec, re.M)
        if not title or title.group(1) != line:
            raise RuntimeError(name + " does not specify product line " + line)
        status = re.search(r"^\*\*Status:\*\*\s*(.+)$", spec, re.M)
        if not status or "frozen" not in status.group(1).lower() or re.search(r"candidate|draft", status.group(1), re.I):
            raise RuntimeError("Freeze the format specification status before tagging: " + name)
    return {**release, "tag": "v" + version, "notes": notes}


def validate_tag(root, tag, main_ref):
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=root, text=True, encoding="utf-8").strip()
    commit = git("rev-parse", "HEAD")
    if git("rev-parse", "--verify", "refs/tags/" + tag + "^{commit}") != commit:
        raise RuntimeError("Checkout must be the exact tagged commit")
    subprocess.run(["git", "merge-base", "--is-ancestor", commit, main_ref], cwd=root, check=True)
    return commit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", default=os.environ.get("RELEASE_TAG"))
    parser.add_argument("--main-ref", default="origin/main")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/release-preflight")
    args = parser.parse_args()
    if not args.tag:
        raise RuntimeError("Pass --tag or RELEASE_TAG")
    committed_source()
    result = metadata(ROOT, args.tag, publishing=True)
    result["source_commit"] = validate_tag(ROOT, args.tag, args.main_ref)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "notes.md").write_text(result.pop("notes"), encoding="utf-8")
    write_json(args.output_dir / "preflight.json", result)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write(f"version={result['product_version']}\ncommit={result['source_commit']}\n")
    print(f"Release preflight: PASS {args.tag} at {result['source_commit']} (language {result['language_version']}, "
          f"C API {result['c_api_version']}, SOVERSION {result['c_abi_soversion']})")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print("release preflight: " + str(error), file=sys.stderr)
        sys.exit(1)
