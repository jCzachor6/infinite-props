"""Reject incomplete, stale or corrupted documentation before packaging."""
import copy
import json
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import language_docs as docs
from language_fixture import LANGUAGE, fixture, reference_stub


class DocumentationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="iprop-doc-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "source"
        fixture(self.root)
        self.output = Path(self.temporary.name) / "book"
        reference = reference_stub()
        reference.start()
        self.addCleanup(reference.stop)

    def test_missing_or_duplicate_builtin_explanations_are_rejected(self):
        file = self.root / "docs/language/index.md"
        original = file.read_text()
        for content in (original.replace("<!-- builtin: unit -->", ""), original + "\n<!-- builtin: unit -->\nAnother description.\n"):
            file.write_text(content)
            with self.assertRaisesRegex(RuntimeError, "Builtin descriptions differ"):
                docs.sources(self.root, LANGUAGE)

    def test_broken_fragment_is_rejected(self):
        file = self.root / "docs/language/index.md"
        file.write_text(file.read_text() + "\n[bad](index.md#missing)\n")
        with self.assertRaisesRegex(RuntimeError, "Missing Markdown anchor"):
            docs.sources(self.root, LANGUAGE)

    def test_unshipped_contract_link_cannot_replace_a_generated_bundle(self):
        with patch.object(docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        before = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        (self.root / "docs/contract.md").write_text("# Contract\n\n[missing](../unshipped.md)\n")
        with patch.object(docs, "contracts", return_value=LANGUAGE), self.assertRaisesRegex(RuntimeError, "Missing offline documentation link"):
            docs.build(Path("unused"), self.output, self.root)
        after = {p.relative_to(self.output): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_offline_directory_link_requires_shipped_children(self):
        payload = {"docs/guide.md": b"# Guide\n\n[Examples](../examples/language)\n",
                   "examples/language/one.ips": b"asset A { size 1x1 draw {} }"}
        docs.verify_links(payload.__getitem__, payload.keys())
        del payload["examples/language/one.ips"]
        with self.assertRaisesRegex(RuntimeError, "Missing offline documentation link"):
            docs.verify_links(payload.__getitem__, payload.keys())

    def test_unregistered_example_is_rejected(self):
        (self.root / "examples/language/extra.ips").write_text("asset A { size 1x1 draw {} }")
        with self.assertRaisesRegex(RuntimeError, "Register every language example"):
            docs.sources(self.root, LANGUAGE)

    def test_new_chapter_cannot_be_missing_from_contents(self):
        (self.root / "docs/language/other.md").write_text("# Other\n")
        (self.root / "docs/language/book.json").write_text('{"title":"Book","chapters":["index.md","other.md"]}')
        with self.assertRaisesRegex(RuntimeError, "contents must link every chapter"):
            docs.sources(self.root, LANGUAGE)

    def test_generation_preserves_unrelated_existing_output(self):
        self.output.mkdir()
        sentinel = self.output / "user-file.txt"
        sentinel.write_text("keep me")
        with patch.object(docs, "contracts", return_value=LANGUAGE), self.assertRaisesRegex(RuntimeError, "empty or previously generated"):
            docs.build(Path("unused"), self.output, self.root)
        self.assertEqual(sentinel.read_text(), "keep me")

    def test_documentation_cannot_silently_omit_the_llm_reference(self):
        with patch.object(docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        before = {p.relative_to(self.output): p.read_bytes()
                  for p in self.output.rglob("*") if p.is_file()}
        (self.root / "docs/llm-reference.json").unlink()
        with patch.object(docs, "contracts", return_value=LANGUAGE), self.assertRaisesRegex(RuntimeError, "requires docs/llm-reference.json"):
            docs.build(Path("unused"), self.output, self.root)
        after = {p.relative_to(self.output): p.read_bytes()
                 for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_verified_book_rejects_tampering_or_a_different_version(self):
        with patch.object(docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        docs.verify(self.output, "0.4.0")
        with self.assertRaisesRegex(RuntimeError, "version differs"):
            docs.verify(self.output, "0.3.0")
        book = self.output / "docs/language-v0.5.md"
        book.write_text(book.read_text() + "changed")
        with self.assertRaisesRegex(RuntimeError, "Documentation file changed"):
            docs.verify(self.output, "0.4.0")

    def test_sdk_install_preserves_generated_documentation_from_crlf_sources(self):
        # Exercise the real install rules with lightweight interface targets;
        # documentation installation needs no compiler or native build.
        source = Path(__file__).resolve().parents[2]
        files = {
            "backends/ResultQueries.hpp": "// fixture\n",
            "runtime/iprop/Fixture.hpp": "// fixture\n",
            "examples/showcase/readme-collection.png": "fixture image",
            "examples/showcase/LICENSE.md": "# License\n",
            "editor/README.md": "# Editor\n",
            "website/README.md": "# Website\n",
            "website/public/favicon.svg": "<svg/>\n",
            "licenses/fixture.txt": "License\n",
            "third_party/README.md": "# Third party\n",
            "third_party/zlib-LICENSE.txt": "License\n",
            "LICENSE": "License\n",
            "README.md": "# SDK\n",
            "spec/iprop-v0.4.md": "# Format\n",
            "CHANGELOG.md": "# Changelog\n\n## v0.4.0\n\nRelease notes.\n",
            "cmake/ipropConfig.cmake.in": "@PACKAGE_INIT@\n",
            "cmake/ipropc.pc.in": "Name: iprop\n",
        }
        for backend in ("capi", "html5", "sfml", "godot", "apple", "unity", "android", "python"):
            files[f"backends/{backend}/README.md"] = "# Backend\n"
        for name in ("bindings.ips", "binding-trap.ips", "binding-unicode.ips", "prepare-bindings.py",
                     "result_fixture.py", "check-python.py", "check-csharp.py", "python-asan.supp"):
            files[f"backends/test/{name}"] = "\n"
        for name, content in files.items():
            file = self.root / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(content.replace("\n", "\r\n").encode("utf-8"))
        # Distinguish checkout files from generated files even on file
        # systems where CMake's copy timestamp comparison is coarse.
        for file in self.root.rglob("*"):
            if file.is_file():
                os.utime(file, (1, 1))
        with patch.object(docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        (self.root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.24)\n"
            "project(iprop VERSION 0.4.0 LANGUAGES NONE)\n"
            "set(CMAKE_INSTALL_LIBDIR lib)\n"
            "include(GNUInstallDirs)\n"
            f'include("{source.as_posix()}/cmake/CAbi.cmake")\n'
            "foreach(target iprop_runtime iprop_capi iprop_capi_static ipc ipa ipgen ipval ipdis ipsheet)\n"
            "  add_library(${target} INTERFACE)\n"
            "endforeach()\n"
            "set(Python3_Interpreter_FOUND TRUE)\n"
            f'set(IPROP_LANGUAGE_DOCS_DIR "{self.output.as_posix()}")\n'
            f'include("{source.as_posix()}/cmake/Install.cmake")\n',
            encoding="utf-8", newline="\n")
        build, prefix = self.output.parent / "build", self.output.parent / "installed"
        for command in (["cmake", "-S", str(self.root), "-B", str(build)],
                        ["cmake", "--install", str(build), "--prefix", str(prefix)]):
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        installed = prefix / "share/iprop"
        docs.verify(installed, "0.4.0")
        self.assertEqual((installed / "CHANGELOG.md").read_bytes(),
                         (self.output / "CHANGELOG.md").read_bytes())

    def test_compiler_description_cannot_use_another_product_version(self):
        value = {**LANGUAGE, "product_version": "0.3.0"}
        result = type("Result", (), {"returncode": 0, "stdout": json.dumps(value), "stderr": ""})()
        with patch.object(docs, "run", return_value=result), self.assertRaisesRegex(RuntimeError, "another product version"):
            docs.contracts(Path("unused"), self.root)

    def test_schema_2_bundle_without_new_capabilities_can_be_verified(self):
        earlier = copy.deepcopy(LANGUAGE)
        del earlier['source_rules']['types']['components']
        del earlier['source_rules']['random_range']
        with patch.object(docs, "contracts", return_value=earlier):
            docs.build(Path("unused"), self.output, self.root)
        docs.verify(self.output, "0.4.0", root=self.root)
        self.assertEqual(json.loads((self.output / "docs/language.json").read_bytes()), earlier)

    def test_current_compiler_versions_must_match_their_independent_sources(self):
        def describe(**changes):
            result = type("Result", (), {"returncode": 0, "stdout": json.dumps({**LANGUAGE, **changes}), "stderr": ""})()
            # Only the version cross-check is under test; read_language has its own corpus.
            with patch.object(docs, "run", return_value=result), \
                    patch.object(docs, "read_language", lambda language: language):
                return docs.contracts(Path("unused"), self.root)

        self.assertEqual(describe()["c_abi_soversion"], "0.4")
        for key, changed, message in (("language_version", {"major": 0, "minor": 6}, "language_version 0.6 != 0.5"),
                                      ("c_api_version", {"major": 0, "minor": 2}, "c_api_version 0.2 != 0.3")):
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError, "differs from the source versions: " + message):
                describe(**{key: changed})
        with self.assertRaisesRegex(RuntimeError, "ABI alias differs"):
            describe(c_abi_soversion="0.5")

    def test_archived_documentation_rejects_tampering_missing_files_and_dirty_sources(self):
        with patch.object(docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        original = {p.relative_to(self.output).as_posix(): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        file = self.output.parent / "book.tar.gz"
        for change in ("none", "tamper", "missing", "dirty", "foreign"):
            payload = dict(original)
            expected_commit = None
            if change == "tamper":
                payload["docs/language-v0.5.md"] += b"changed"
            if change == "missing":
                del payload["docs/language/index.md"]
            if change in ("dirty", "foreign"):
                manifest = json.loads(payload["docs/language-manifest.json"])
                if change == "dirty":
                    manifest["working_tree_dirty"] = True
                else:
                    manifest["source_commit"] = "b" * 40
                    expected_commit = "a" * 40
                payload["docs/language-manifest.json"] = json.dumps(manifest).encode()
            with tarfile.open(file, "w:gz") as archive:
                for name, data in payload.items():
                    member = tarfile.TarInfo("book/" + name)
                    member.size = len(data)
                    archive.addfile(member, io.BytesIO(data))
            with self.subTest(change=change):
                if change == "none":
                    docs.verify_archive(file, "book", "0.4.0", None, self.root)
                else:
                    with self.assertRaises(RuntimeError):
                        docs.verify_archive(file, "book", "0.4.0", expected_commit, self.root)

    def test_changed_inputs_cannot_be_verified_as_current_documentation(self):
        with patch.object(docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        chapter = self.root / "docs/language/index.md"
        chapter.write_text(chapter.read_text() + "\nNew behavior.\n")
        with self.assertRaisesRegex(RuntimeError, "inputs differ"):
            docs.verify(self.output, "0.4.0", root=self.root)


if __name__ == "__main__":
    unittest.main()
