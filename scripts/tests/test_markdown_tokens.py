"""Handbook Markdown follows the CommonMark parser the website renders with."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import language_docs
import llm_reference as llm
import markdown_tokens as md
from contract_readers import HISTORICAL
from language_fixture import LANGUAGE, fixture

TRICKY = """# Book ##

Intro [a](one.md#x) and `[b](two.md)`, \\[c](three.md) escaped.
Second line [d](dé.md).

~~~
# fenced, not a heading
[e](four.md)
~~~

    # indented code
    [f](five.md)

Setext title
------------

## Book

![image](pic.png) [g][ref]

[ref]: six.md
"""


class MarkdownTokensTest(unittest.TestCase):
    def test_headings_use_inline_titles_unique_anchors_and_lines(self):
        self.assertEqual(md.headings(TRICKY), [
            {"title": "Book", "anchor": "book", "level": 1, "line": 0, "atx": True},
            {"title": "Setext title", "anchor": "setext-title", "level": 2, "line": 13, "atx": False},
            {"title": "Book", "anchor": "book-1", "level": 2, "line": 16, "atx": True}])
        self.assertEqual(md.manifest_headings("## `code` *em*\n")[0],
                         {"title": "`code` *em*", "anchor": "code-em", "level": 2})

    def test_links_exclude_code_and_escapes_and_count_lines(self):
        self.assertEqual(md.links(TRICKY), [("one.md#x", 2), ("d%C3%A9.md", 3), ("pic.png", 18), ("six.md", 18)])
        self.assertEqual(md.normalize_link("dé.md"), "d%C3%A9.md")

    def test_code_lines_cover_fences_and_indented_blocks(self):
        self.assertEqual(sorted(md.code_lines(TRICKY)), [5, 6, 7, 8, 10, 11])

    def test_rewrite_changes_only_real_inline_links(self):
        self.assertEqual(md.rewrite_links(TRICKY, lambda destination: None), TRICKY)
        rewritten = md.rewrite_links(TRICKY, lambda destination: "new/" + destination)
        self.assertIn("Intro [a](new/one.md#x) and `[b](two.md)`, \\[c](three.md) escaped.", rewritten)
        self.assertIn("Second line [d](new/dé.md).", rewritten)
        self.assertIn("[e](four.md)", rewritten)
        self.assertIn("    [f](five.md)", rewritten)
        # The label keeps its text even when it spells the destination.
        self.assertEqual(md.rewrite_links("[x.md](x.md) [y](<y.md>)", lambda d: "z/" + d),
                         "[x.md](z/x.md) [y](<y.md>)")


class HandbookAssemblyTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="iprop-markdown-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "source"
        fixture(self.root)
        chapters = self.root / "docs/language"
        (chapters / "book.json").write_text('{"title":"Book","chapters":["index.md","more.md"]}')
        index = (chapters / "index.md").read_text()
        (chapters / "index.md").write_text(index + "\nContinue with [More](more.md).\n")
        self.more = chapters / "more.md"

    def build(self):
        output = Path(self.temporary.name) / "book"
        with patch.object(language_docs, "contracts", return_value=LANGUAGE):
            language_docs.build(Path("unused"), output, self.root, profile=HISTORICAL)
        return next((output / "docs").glob("language-v*.md")).read_text()

    def test_book_nests_only_real_headings_and_rewrites_only_real_links(self):
        self.more.write_text("# More\n\nUse `[fake](missing.md)` literally.\n\n~~~\n# fenced\n[also](missing.md)\n~~~\n\n"
                             "## Section ##\n\nBack to [unit](index.md#unit).\n")
        book = self.build()
        self.assertIn("\n## More\n", book)
        self.assertIn("\n### Section ##\n", book)
        self.assertIn("~~~\n# fenced\n[also](missing.md)\n~~~", book)
        self.assertIn("Use `[fake](missing.md)` literally.", book)
        self.assertIn("Back to [unit](#unit).", book)

    def test_setext_chapter_headings_are_rejected_before_assembly(self):
        self.more.write_text("More\n====\n\nText.\n")
        with self.assertRaisesRegex(RuntimeError, "ATX headings"):
            self.build()

    def test_excerpt_ends_at_real_headings_and_skips_tilde_fences(self):
        chapter = self.root / "docs/language/more.md"
        text = "# More\n\n## Form\n\nKeep [unit](index.md#unit).\n\n~~~\n## Other\n[x](missing.md)\n~~~\n\nAfter.\n\n## Other\nOutside.\n"
        body, link = llm.excerpt(self.root.resolve(), chapter.resolve(), text, "Form", 2)
        self.assertEqual(body, "Keep [unit](docs/language/index.md#unit).\n\nAfter.")
        self.assertEqual(link, "docs/language/more.md#form")


if __name__ == "__main__":
    unittest.main()
