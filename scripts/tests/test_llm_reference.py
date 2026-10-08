"""Check authored excerpt boundaries and exact correction matching."""
import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import llm_reference as llm
from language_fixture import LANGUAGE


class ReferenceSourceTest(unittest.TestCase):
    def test_capability_summaries_are_optional_for_earlier_schema_2_exports(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            config = root / llm.CONFIG
            config.parent.mkdir()
            config.write_text(json.dumps({'sections': [], 'defaults': {
                'sections': [], 'symbols': [], 'max_bytes': 10000}, 'maximum_bytes': 10000}))
            identity = {'source_commit': None, 'status': 'development / release candidate',
                        'working_tree_dirty': False}
            for components, ranges in ((False, False), (True, False), (False, True), (True, True)):
                with self.subTest(components=components, random_range=ranges):
                    language = copy.deepcopy(LANGUAGE)
                    if not components:
                        del language['source_rules']['types']['components']
                    if not ranges:
                        del language['source_rules']['random_range']
                    result, evidence = llm.render(root, language, {}, {}, identity, Path('unused'))
                    self.assertEqual(evidence, [])
                    self.assertEqual('Read-only members:' in result, components)
                    self.assertEqual('Random range:' in result, ranges)
                    if components:
                        self.assertIn('Read-only members: vec2 .x, .y -> f32.', result)

    def test_excerpt_selects_whole_paragraphs_without_literal_code(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            chapter = root / 'docs/chapter.md'
            chapter.parent.mkdir()
            chapter.write_text('# Chapter\n')
            target = root / 'examples/code.ips'
            target.parent.mkdir()
            target.write_text('asset A {}')
            text = '# Chapter\nIntro.\n\n## Form\nSkip this.\n\n<!-- example: ../examples/code.ips -->\n\n'
            text += 'Keep the [registered source](../examples/code.ips).\nStill the same paragraph.\n\n'
            text += '```ips\n# Form\nthis must not become generated code\n```\n\n## Other\nOutside.\n'
            body, link = llm.excerpt(root, chapter, text, 'Form', 1, 1)
            self.assertEqual(body, 'Keep the [registered source](examples/code.ips).\nStill the same paragraph.')
            self.assertEqual(link, 'docs/chapter.md#form')
            with self.assertRaisesRegex(RuntimeError, 'missing or ambiguous'):
                llm.excerpt(root, chapter, text + '\n## Form\nDuplicate.\n', 'Form', 1)
            with self.assertRaisesRegex(RuntimeError, 'invalid paragraph selection'):
                llm.excerpt(root, chapter, text, 'Form', 3)

    def test_correction_refuses_missing_or_ambiguous_source_text(self):
        pair = {'id': 'repair', 'replacements': [{'before': 'x = 1;', 'after': 'x = 1.0;'}]}
        self.assertEqual(llm.correction(pair, 'x = 1; x = 2.5'), 'x = 1.0; x = 2.5')
        for text in ['x = 2;', 'x = 1; x = 1;']:
            with self.assertRaisesRegex(RuntimeError, 'must match once'):
                llm.correction(pair, text)
        pair['replacements'][0]['after'] = 'x = 1;'
        with self.assertRaisesRegex(RuntimeError, 'empty correction'):
            llm.correction(pair, 'x = 1;')


if __name__ == '__main__':
    unittest.main()
