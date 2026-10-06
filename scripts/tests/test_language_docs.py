"""Reject incomplete, stale or corrupted documentation before packaging."""
import json
import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import language_docs
from contract_readers import HISTORICAL
from functools import partial
from types import SimpleNamespace
# These authored unit fixtures represent the frozen 0.4 profile explicitly.
docs = SimpleNamespace(**vars(language_docs))
for name in ("build", "verify", "verify_archive"):
    setattr(docs, name, partial(getattr(language_docs, name), profile=HISTORICAL))
from language_fixture import LANGUAGE, VERSION_FILES, fixture


class DocumentationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="iprop-doc-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "source"
        fixture(self.root)
        self.output = Path(self.temporary.name) / "book"

    def test_integral_json_scalar_renders_as_a_unicode_codepoint(self):
        from copy import deepcopy
        numeric = deepcopy(LANGUAGE)
        numeric["emoji_aliases"][0]["codepoint"] = 128230.0
        with patch.object(language_docs, "contracts", return_value=numeric):
            docs.build(Path("unused"), self.output, self.root)
        self.assertIn("U+1F4E6", (self.output / "docs/language-v0.4.md").read_text())

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
        with patch.object(language_docs, "contracts", return_value=LANGUAGE), self.assertRaisesRegex(RuntimeError, "empty or previously generated"):
            docs.build(Path("unused"), self.output, self.root)
        self.assertEqual(sentinel.read_text(), "keep me")

    def test_explicit_llm_selection_requires_registered_configuration(self):
        with patch.object(language_docs, "contracts", return_value=LANGUAGE), self.assertRaisesRegex(RuntimeError, "LLM selections require"):
            docs.build(Path("unused"), self.output, self.root, llm_sections="asset")
        self.assertFalse(self.output.exists())

    def test_current_compiler_cannot_silently_omit_default_reference(self):
        with patch.object(language_docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        before = {p.relative_to(self.output): p.read_bytes()
                  for p in self.output.rglob("*") if p.is_file()}
        modern = {**LANGUAGE, "source_rules": {}}
        with patch.object(language_docs, "contracts", return_value=modern), self.assertRaisesRegex(RuntimeError, "Current compiler rules require"):
            docs.build(Path("unused"), self.output, self.root)
        after = {p.relative_to(self.output): p.read_bytes()
                 for p in self.output.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_verified_book_rejects_tampering_or_a_different_version(self):
        with patch.object(language_docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        docs.verify(self.output, "0.4.0")
        with self.assertRaisesRegex(RuntimeError, "version differs"):
            docs.verify(self.output, "0.3.0")
        book = self.output / "docs/language-v0.4.md"
        book.write_text(book.read_text() + "changed")
        with self.assertRaisesRegex(RuntimeError, "Documentation file changed"):
            docs.verify(self.output, "0.4.0")

    def test_compiler_description_cannot_use_another_product_version(self):
        value = {**LANGUAGE, "product_version": "0.3.0"}
        result = type("Result", (), {"returncode": 0, "stdout": json.dumps(value), "stderr": ""})()
        with patch.object(language_docs, "run", return_value=result), self.assertRaisesRegex(RuntimeError, "another product version"):
            docs.contracts(Path("unused"), self.root)

    def test_current_compiler_versions_must_match_their_independent_sources(self):
        for name, text in {**VERSION_FILES, "backends/capi/iprop.h":
                           "#define IPROP_API_VERSION_MAJOR 0\n#define IPROP_API_VERSION_MINOR 3\n"}.items():
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text, encoding="utf-8")
        value = {**LANGUAGE, "language_version": {"major": 0, "minor": 4}, "format_version": {"major": 0, "minor": 4},
                 "c_api_version": {"major": 0, "minor": 3}, "abi_version": {"major": 0, "minor": 4},
                 "c_abi_soversion": "0.4"}

        def describe(**changes):
            result = type("Result", (), {"returncode": 0, "stdout": json.dumps({**value, **changes}), "stderr": ""})()
            # Only the version cross-check is under test; read_language has its own corpus.
            with patch.object(language_docs, "run", return_value=result), \
                    patch.object(language_docs, "read_language", lambda language, profile: language):
                return language_docs.contracts(Path("unused"), self.root)

        self.assertEqual(describe()["c_abi_soversion"], "0.4")
        for key, changed, message in (("format_version", {"major": 0, "minor": 5}, "format_version 0.5 != 0.4"),
                                      ("language_version", {"major": 0, "minor": 5}, "language_version 0.5 != 0.4"),
                                      ("c_api_version", {"major": 0, "minor": 2}, "c_api_version 0.2 != 0.3"),
                                      ("c_abi_soversion", "0.5", "c_abi_soversion 0.5 != 0.4")):
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError, "differs from the source versions: " + message):
                describe(**{key: changed})

    def test_archived_documentation_rejects_tampering_missing_files_and_dirty_sources(self):
        with patch.object(language_docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        original = {p.relative_to(self.output).as_posix(): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        file = self.output.parent / "book.tar.gz"
        for change in ("none", "tamper", "missing", "dirty", "foreign"):
            payload = dict(original)
            expected_commit = None
            if change == "tamper":
                payload["docs/language-v0.4.md"] += b"changed"
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
        with patch.object(language_docs, "contracts", return_value=LANGUAGE):
            docs.build(Path("unused"), self.output, self.root)
        chapter = self.root / "docs/language/index.md"
        chapter.write_text(chapter.read_text() + "\nNew behavior.\n")
        with self.assertRaisesRegex(RuntimeError, "inputs differ"):
            docs.verify(self.output, "0.4.0", root=self.root)


if __name__ == "__main__":
    unittest.main()
