"""Reject incomplete, stale or corrupted documentation before packaging."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import language_docs as docs


LANGUAGE = {"schema_version": 1, "product_version": "0.4.0", "builtins": [
    {"name": "unit", "overloads": [{"parameters": [], "result": "f32"}]}],
    "emoji_aliases": [{"word": "asset", "codepoint": 128230}]}


def fixture(root):
    for name, content in {
        "CMakeLists.txt": "project(iprop VERSION 0.4.0)\n",
        "docs/language/book.json": '{"title":"Book","chapters":["index.md"]}',
        "docs/language/index.md": "# Book\n\n## unit\n<!-- builtin: unit -->\n\nOne deterministic stream draw.\n\n<!-- example: ../../examples/language/one.ips -->\n\n<!-- aliases -->\n",
        "examples/language/cases.json": '{"cases":[{"file":"one.ips","expect":"pass"}]}',
        "examples/language/one.ips": "asset A { size 1x1 draw { x = unit() } }\n",
        "examples/recipes/README.md": "# Recipes\n",
    }.items():
        file = root / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(content, encoding="utf-8")


class DocumentationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="iprop-doc-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "source"
        fixture(self.root)
        self.output = Path(self.temporary.name) / "book"

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
        with patch.object(docs, "contracts", return_value=LANGUAGE), self.assertRaisesRegex(RuntimeError, "empty or previously generated"):
            docs.build(Path("unused"), self.output, self.root)
        self.assertEqual(sentinel.read_text(), "keep me")

    def test_verified_book_rejects_tampering_or_a_different_version(self):
        with patch.object(docs, "contracts", return_value=LANGUAGE):
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
        with patch.object(docs, "run", return_value=result), self.assertRaisesRegex(RuntimeError, "another product version"):
            docs.contracts(Path("unused"), self.root)


if __name__ == "__main__":
    unittest.main()
