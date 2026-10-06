"""Exercise release gates and publication failure/retry paths without publishing."""
import copy
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from publish_release import publish
from release_assets import collect, verify_bundle, verify_jobs, verify_source
from release_common import sha256, write_json
from release_preflight import metadata, validate_tag
import language_docs
import release_assets
from contract_readers import HISTORICAL
from functools import partial
from language_fixture import LANGUAGE, VERSION_FILES, fixture as language_fixture


VERSION = "0.4.0"
HEADER = b"#define IPROP_API_VERSION_MAJOR 0\n#define IPROP_API_VERSION_MINOR 2\n"
FROZEN_SPEC = "# `.iprop` Format and Virtual Machine — Specification v{0}\n\n**Status:** Frozen v{0} contract.\n"
REPORTS = ["reports-native-Linux-X64", "reports-native-macOS-ARM64", "reports-native-Windows-X64",
           "reports-wasm", "reports-native-backends", "reports-sanitizers"]


def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root, text=True, encoding="utf-8", stderr=subprocess.DEVNULL).strip()


def repository(root):
    root.mkdir()
    git(root, "init", "-q", "-b", "main")
    # Temporary repositories must not inherit runner-specific checkout filters.
    git(root, "config", "core.autocrlf", "false")
    git(root, "config", "core.eol", "lf")
    for file, text in {
        "CMakeLists.txt": "project(iprop VERSION 0.4.0)\n",
        "editor/package.json": '{"version":"0.4.0"}',
        "website/package.json": '{"version":"0.4.0"}',
        "website/package-lock.json": '{"version":"0.4.0","packages":{"":{"version":"0.4.0"}}}',
        "CHANGELOG.md": "# Changelog\n\n## v0.4.0 — 2026-01-01\n\nRelease notes.\n",
        "spec/iprop-v0.4.md": FROZEN_SPEC.format("0.4"),
        "backends/capi/iprop.h": HEADER.decode(),
        **VERSION_FILES,
    }.items():
        target = root / file
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(text.encode("utf-8"))
    language_fixture(root)
    git(root, "add", ".")
    git(root, "-c", "user.name=CI fixture", "-c", "user.email=ci@example.invalid", "commit", "-qm", "fixture")
    return git(root, "rev-parse", "HEAD")


def archive(path, manifest, wasm=None, documentation=None):
    content = {"release.json": json.dumps(manifest).encode(),
               **({"include/iprop.h": HEADER.replace(b"\n", b"\r\n")
                   if manifest["system"] == "windows" else HEADER} if "system" in manifest else {}),
               **({"iprop.wasm": wasm} if wasm else {})}
    if documentation is not None:
        prefix = "share/iprop/" if "system" in manifest else ""
        content.update({prefix + p.relative_to(documentation).as_posix(): p.read_bytes()
                        for p in documentation.rglob("*") if p.is_file()})
    with tarfile.open(path, "w:gz") as tar:
        for name, data in content.items():
            member = tarfile.TarInfo(path.name[:-7] + "/" + name)
            member.size = len(data)
            tar.addfile(member, io.BytesIO(data))


def inputs(directory, root, commit):
    directory.mkdir()
    documentation = directory / "documentation"
    with patch.object(language_docs, "contracts", return_value=LANGUAGE):
        language_docs.build(Path("unused"), documentation, root, profile=HISTORICAL)
    for system, runner, arch in (("linux", "Linux", "x86_64"), ("macos", "macOS", "arm64"), ("windows", "Windows", "x86_64")):
        sdk = directory / ("sdk-" + runner + "-" + arch)
        sdk.mkdir()
        manifest = {"product_version": VERSION, "source_commit": commit, "system": system, "architecture": arch,
                    "c_api_version": "0.2", "backends": {"capi": True, "sfml": system == "linux", "godot": system == "linux"}}
        file = sdk / f"iprop-{VERSION}-{system}-{arch}.tar.gz"
        archive(file, manifest, documentation=documentation)
        artifacts = {file.name: sha256(file)}
        if system == "linux":
            source = sdk / f"iprop-{VERSION}.tar.gz"
            git(root, "archive", "--format=tar.gz", f"--prefix=iprop-{VERSION}/", "-o", str(source.resolve()), "HEAD")
            artifacts[source.name] = sha256(source)
        write_json(sdk / (file.name[:-7] + "-validation.json"), {**manifest, "passed": True,
                   "checks": {"ctest": True, "relocated_sdk": True, "extracted_sdk": True, "documentation": True}, "artifacts": artifacts})
    web = directory / "web-build"
    web.mkdir()
    wasm = b"fixture module bytes"
    manifest = {"product_version": VERSION, "source_commit": commit, "wasm_sha256": hashlib.sha256(wasm).hexdigest()}
    artifacts = {}
    for kind in ("wasm", "website"):
        file = web / f"iprop-{VERSION}-{kind}.tar.gz"
        archive(file, {**manifest, "kind": kind}, wasm, documentation)
        artifacts[file.name] = sha256(file)
    write_json(web / f"iprop-{VERSION}-web-validation.json", {**manifest, "passed": True,
               "checks": {"extracted_wasm": True, "extracted_website": True, "documentation": True}, "artifacts": artifacts})
    for name in REPORTS:
        report = directory / name
        report.mkdir()
        write_json(report / "summary.json", {"product_version": VERSION, "source_commit": commit, "passed": True,
                   "steps": [{"passed": True, "returncode": 0}], "report_errors": [],
                   "test_reports": [{"tests": 1, "failures": 0, "skipped": 0}],
                   "github": {"run_id": os.environ.get("GITHUB_RUN_ID", "fixture")}})


class ReleaseGatesTest(unittest.TestCase):
    def setUp(self):
        # Only these explicitly frozen fixture archives use the legacy profile.
        reader = patch.object(release_assets, "verify_documentation",
                              partial(language_docs.verify_archive, profile=HISTORICAL))
        reader.start()
        self.addCleanup(reader.stop)

    def test_matching_versions_and_frozen_dated_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repo"
            repository(root)
            self.assertIn("Release notes.", metadata(root, "v0.4.0", publishing=True)["notes"])
            with self.assertRaisesRegex(RuntimeError, "Tag must match"):
                metadata(root, "v0.5.0", publishing=True)
            (root / "website/package-lock.json").write_text('{"version":"0.4.0","packages":{"":{"version":"0.3.0"}}}')
            with self.assertRaisesRegex(RuntimeError, "root package version"):
                metadata(root, "v0.4.0", publishing=True)

    def test_candidate_changelog_or_specification_cannot_be_published(self):
        for file, text, message in (("CHANGELOG.md", "## v0.4.0 — release candidate\n\nNotes.\n", "Date the changelog"),
                                    ("spec/iprop-v0.4.md", FROZEN_SPEC.format("0.4").replace("Frozen v0.4 contract", "release candidate; semantics frozen"),
                                     "Freeze the format")):
            with self.subTest(file=file), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "repo"
                repository(root)
                (root / file).write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, message):
                    metadata(root, "v0.4.0", publishing=True)

    def test_release_checks_the_emitted_frozen_format_not_the_product_number(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repo"
            repository(root)
            for file, text in {
                "CMakeLists.txt": "project(iprop VERSION 0.5.0)\n",
                "editor/package.json": '{"version":"0.5.0"}',
                "website/package.json": '{"version":"0.5.0"}',
                "website/package-lock.json": '{"version":"0.5.0","packages":{"":{"version":"0.5.0"}}}',
                "CHANGELOG.md": "# Changelog\n\n## v0.5.0 — 2026-10-06\n\nRelease notes.\n",
                "language/ips/Version.hpp": VERSION_FILES["language/ips/Version.hpp"].replace("Minor=4", "Minor=5"),
                "backends/capi/iprop.h": "#define IPROP_API_VERSION_MAJOR 0\n#define IPROP_API_VERSION_MINOR 3\n",
            }.items():
                (root / file).write_text(text, encoding="utf-8")
            self.assertFalse((root / "spec/iprop-v0.5.md").exists())
            release = metadata(root, "v0.5.0", publishing=True)
            self.assertEqual({key: release[key] for key in ("product_version", "language_version", "format_version",
                                                            "c_api_version", "c_abi_soversion", "tag")},
                             {"product_version": "0.5.0", "language_version": "0.5", "format_version": "0.4",
                              "c_api_version": "0.3", "c_abi_soversion": "0.4", "tag": "v0.5.0"})
            # A frozen-looking specification named after the product cannot
            # stand in for the format the release actually emits.
            (root / "spec/iprop-v0.5.md").write_text(FROZEN_SPEC.format("0.5"), encoding="utf-8")
            (root / "spec/iprop-v0.4.md").unlink()
            with self.assertRaisesRegex(RuntimeError, "Missing the emitted format specification spec/iprop-v0.4.md"):
                metadata(root, "v0.5.0", publishing=True)
            (root / "spec/iprop-v0.4.md").write_text(FROZEN_SPEC.format("0.5"), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "does not specify format 0.4"):
                metadata(root, "v0.5.0", publishing=True)
            # A new emitted format requires its own frozen specification.
            (root / "runtime/iprop/Constants.hpp").write_text(
                VERSION_FILES["runtime/iprop/Constants.hpp"].replace("Minor = 4", "Minor = 5"), encoding="utf-8")
            self.assertEqual(metadata(root, "v0.5.0", publishing=True)["format_version"], "0.5")

    def test_unreadable_version_source_is_a_release_error(self):
        for file, text in (("cmake/CAbi.cmake", "set(IPROP_C_ABI_SOVERSION 0.4)\n"),
                           ("language/ips/Version.hpp", "inline constexpr unsigned kLanguageVersionMajor=0;\n"),
                           ("backends/capi/iprop.h", "/* IPROP_API_VERSION_MAJOR 0 */\n")):
            with self.subTest(file=file), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "repo"
                repository(root)
                (root / file).write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "Cannot read .* from " + file):
                    metadata(root)

    def test_tag_must_identify_the_checkout_and_belong_to_main(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repo"
            commit = repository(root)
            git(root, "tag", "v0.4.0")
            self.assertEqual(validate_tag(root, "v0.4.0", "main"), commit)
            git(root, "checkout", "-qb", "develop")
            (root / "feature").write_text("new")
            git(root, "add", ".")
            git(root, "-c", "user.name=CI fixture", "-c", "user.email=ci@example.invalid", "commit", "-qm", "feature")
            with self.assertRaisesRegex(RuntimeError, "exact tagged commit"):
                validate_tag(root, "v0.4.0", "main")
            git(root, "tag", "v0.4.1")
            with self.assertRaises(subprocess.CalledProcessError):
                validate_tag(root, "v0.4.1", "main")

    def test_missing_failed_foreign_or_all_skipped_job_cannot_pass(self):
        jobs = {name: {"product_version": VERSION, "source_commit": "a" * 40, "passed": True,
                       "steps": [{"passed": True, "returncode": 0}], "report_errors": [],
                       "test_reports": [{"tests": 1, "failures": 0, "skipped": 0}]} for name in REPORTS}
        verify_jobs(jobs, VERSION, "a" * 40)
        bad = copy.deepcopy(jobs); del bad[REPORTS[-1]]
        with self.assertRaises(RuntimeError):
            verify_jobs(bad, VERSION, "a" * 40)
        for field, value in (("passed", False), ("source_commit", "b" * 40),
                             ("steps", [{"passed": False, "returncode": 1}]),
                             ("test_reports", [{"tests": 1, "failures": 0, "skipped": 1}])):
            bad = copy.deepcopy(jobs); bad[REPORTS[0]][field] = value
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                verify_jobs(bad, VERSION, "a" * 40)

    def test_complete_bundle_and_tampered_asset(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "repo"
            commit = repository(root)
            source = directory / "inputs"
            inputs(source, root, commit)
            # Retry leftovers are not part of the six required check reports.
            (source / "reports-publication").mkdir()
            output = directory / "release"
            collect(source, output, VERSION, commit, root)
            self.assertEqual(len(verify_bundle(output, VERSION, commit, root)), 13)
            with (output / "iprop-0.4.0-wasm.tar.gz").open("ab") as file:
                file.write(b"changed")
            with self.assertRaisesRegex(RuntimeError, "Release asset changed"):
                verify_bundle(output, VERSION, commit, root)

    def test_source_archive_must_contain_every_git_blob(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "repo"
            commit = repository(root)
            file = directory / "source.tar.gz"
            git(root, "archive", "--format=tar.gz", "--prefix=iprop-0.4.0/", "-o", str(file), "HEAD")
            verify_source(file, VERSION, commit, root)
            invalid = directory / "incomplete.tar.gz"
            with tarfile.open(invalid, "w:gz", format=tarfile.PAX_FORMAT, pax_headers={"comment": commit}) as tar:
                member = tarfile.TarInfo("iprop-0.4.0/CMakeLists.txt")
                data = subprocess.check_output(["git", "show", commit + ":CMakeLists.txt"], cwd=root)
                member.size = len(data)
                tar.addfile(member, io.BytesIO(data))
            with self.assertRaisesRegex(RuntimeError, "omits tracked files"):
                verify_source(invalid, VERSION, commit, root)

    def test_valid_sdk_checksum_cannot_hide_missing_language_documentation(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / "repo"
            commit = repository(root)
            source = directory / "inputs"
            inputs(source, root, commit)
            sdk = next(source.glob("sdk-macOS-*"))
            file = next(sdk.glob("*.tar.gz"))
            validation_file = next(sdk.glob("*-validation.json"))
            validation = json.loads(validation_file.read_text())
            manifest = {key: value for key, value in validation.items() if key not in ("checks", "artifacts", "passed")}
            archive(file, manifest)
            validation["artifacts"][file.name] = sha256(file)
            write_json(validation_file, validation)
            with self.assertRaisesRegex(RuntimeError, "Missing archived documentation"):
                collect(source, directory / "release", VERSION, commit, root)


class FakeGitHub:
    def __init__(self, commit):
        self.commit = commit
        self.current = None
        self.files = {}
        self.calls = []
        self.corrupt = False
        self.move_tag = False

    def tag_commit(self, tag):
        return "b" * 40 if self.move_tag and self.files else self.commit

    def release(self, tag):
        return self.current

    def assets(self, release):
        return list(self.files.values())

    def create(self, tag, commit, notes):
        self.calls.append("create-draft")
        self.current = {"id": 1, "draft": True, "html_url": "https://example.invalid/release"}
        return self.current

    def delete_asset(self, asset):
        self.calls.append("delete-" + asset["name"])
        del self.files[asset["name"]]

    def upload(self, release, file):
        self.calls.append("upload-" + file.name)
        asset = {"id": file.name, "name": file.name, "state": "uploaded", "size": file.stat().st_size,
                 "digest": "sha256:" + ("0" * 64 if self.corrupt else sha256(file))}
        self.files[file.name] = asset
        return asset

    def finish(self, release, tag, notes):
        self.calls.append("publish")
        self.current["draft"] = False


class PublicationTest(unittest.TestCase):
    def setUp(self):
        # Only these explicitly frozen fixture archives use the legacy profile.
        reader = patch.object(release_assets, "verify_documentation",
                              partial(language_docs.verify_archive, profile=HISTORICAL))
        reader.start()
        self.addCleanup(reader.stop)

    def test_upload_readback_precedes_publication_and_same_release_is_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            file = Path(temporary) / "sdk.tar.gz"; file.write_bytes(b"tested SDK")
            api = FakeGitHub("a" * 40)
            publish(api, "v0.4.0", "a" * 40, "notes", [file])
            self.assertEqual(api.calls, ["create-draft", "upload-sdk.tar.gz", "publish"])
            api.calls.clear()
            publish(api, "v0.4.0", "a" * 40, "notes", [file])
            self.assertEqual(api.calls, [])
            file.write_bytes(b"different SDK")
            with self.assertRaisesRegex(RuntimeError, "Published release differs"):
                publish(api, "v0.4.0", "a" * 40, "notes", [file])
            self.assertEqual(api.calls, [])

    def test_bad_upload_digest_and_tag_move_leave_an_unpublished_draft(self):
        for failure in ("corrupt", "move_tag"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                file = Path(temporary) / "sdk.tar.gz"; file.write_bytes(b"tested SDK")
                api = FakeGitHub("a" * 40); setattr(api, failure, True)
                with self.assertRaises(RuntimeError):
                    publish(api, "v0.4.0", "a" * 40, "notes", [file])
                self.assertTrue(api.current["draft"])
                self.assertNotIn("publish", api.calls)

    def test_partial_draft_can_be_retried_and_unverified_extras_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            file = Path(temporary) / "sdk.tar.gz"; file.write_bytes(b"tested SDK")
            api = FakeGitHub("a" * 40); api.corrupt = True
            with self.assertRaises(RuntimeError):
                publish(api, "v0.4.0", "a" * 40, "notes", [file])
            api.corrupt = False
            publish(api, "v0.4.0", "a" * 40, "notes", [file])
            self.assertFalse(api.current["draft"])
            self.assertIn("delete-sdk.tar.gz", api.calls)
            api.files["unknown.exe"] = {"name": "unknown.exe"}
            with self.assertRaisesRegex(RuntimeError, "unverified assets"):
                publish(api, "v0.4.0", "a" * 40, "notes", [file])


if __name__ == "__main__":
    unittest.main()
