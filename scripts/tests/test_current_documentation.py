"""Keep the current specification complete and authored documentation linkable."""
import re
import sys
import unittest
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from markdown_tokens import headings, links


class CurrentDocumentationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = (ROOT / "spec/iprop-v0.7.md").read_text(encoding="utf-8")
        cls.constants = (ROOT / "runtime/iprop/Constants.hpp").read_text(encoding="utf-8")

    def test_every_registered_instruction_has_its_wire_number_and_mnemonic(self):
        enum = self.constants.split("enum class Opcode", 1)[1].split("};", 1)[0]
        numbers = dict(re.findall(r"\b(\w+) = (0x[0-9A-Fa-f]+)", enum))
        registry = (ROOT / "runtime/opcode_spec.cpp").read_text(encoding="utf-8")
        names = dict(re.findall(r'(?:fixed|dynamic)\(Opcode::(\w+), "([^"]+)"', registry))
        self.assertEqual(numbers.keys(), names.keys())
        # Tables also contain paired integer/float instruction columns.
        tables = "\n".join(re.findall(r"^\|.*Mnemonic.*\n(?:^\|.*\n)+", self.spec, re.M))
        documented = set(re.findall(r"\| (0x[0-9A-Fa-f]+) \| `?([a-z][a-z0-9_.]*)(?=[ `|])", tables))
        self.assertEqual(documented, {(number, names[name]) for name, number in numbers.items()})

    def test_all_section_ids_and_storable_wire_types_are_documented(self):
        enum = self.constants.split("enum class SectionId", 1)[1].split("};", 1)[0]
        actual = {(int(number), name.lower()) for name, number in re.findall(r"\b(\w+) = (\d+)", enum)}
        section_table = self.spec.split("| ID | Section | Contract |", 1)[1].split("### 2.3", 1)[0]
        documented = {(int(number), name.replace(" ", "")) for number, name in
                      re.findall(r"\| (\d+) \| ([a-z ]+) \|", section_table)}
        self.assertEqual(documented, actual)
        types = (ROOT / "runtime/iprop/TypeDescriptor.hpp").read_text(encoding="utf-8")
        kinds = dict(re.findall(r"\b(\w+) = (\d+)", types.split("enum class Kind", 1)[1].split("};", 1)[0]))
        scalar_table = self.spec.split("## 3. Types and values", 1)[1].split("## 4.", 1)[0]
        for name in ("I32", "F32", "Color", "Vec2", "Distribution"):
            with self.subTest(type=name):
                self.assertRegex(scalar_table, rf"\| {name.lower()}[^|]*\| {kinds[name]} \|")
        for name in ("List", "Sampler", "Layer", "Mask", "Stepper"):
            with self.subTest(descriptor=name):
                self.assertIn(f"| {kinds[name]} (`{name.lower()}`) |", self.spec)

    def test_specification_is_self_contained_and_numbered_references_resolve(self):
        self.assertIn("**Status:** Development", self.spec)
        self.assertNotRegex(self.spec, r"iprop-v0\.[1-6]\.md")
        defined = set(re.findall(r"^#{2,3} (\d+(?:\.\d+)*)[. ]", self.spec, re.M))
        self.assertLessEqual(set(re.findall(r"§(\d+(?:\.\d+)*)", self.spec)), defined)

    def test_authored_documentation_links_and_fragments_resolve(self):
        paths = [ROOT / "README.md", ROOT / "CHANGELOG.md", *ROOT.glob("docs/**/*.md"),
                 *ROOT.glob("spec/*.md"), *ROOT.glob("backends/*/README.md"),
                 ROOT / "editor/README.md", ROOT / "website/README.md", *ROOT.glob("benchmarks/**/README.md")]
        for source in paths:
            for destination, _ in links(source.read_text(encoding="utf-8")):
                url = urlsplit(destination)
                if url.scheme or url.netloc:
                    continue
                with self.subTest(source=source.relative_to(ROOT), link=destination):
                    target = (source.parent / unquote(url.path)).resolve() if url.path else source
                    self.assertTrue(target.exists(), "Missing documentation target")
                    if url.fragment and target.suffix == ".md":
                        self.assertIn(unquote(url.fragment), {h["anchor"] for h in headings(target.read_text(encoding="utf-8"))})


if __name__ == "__main__":
    unittest.main()
