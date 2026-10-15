"""Dependency-free protocol rejection; actual exports are checked by CTest."""
import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from contract_readers import (parse_json,read_diagnostics,read_language,read_manifest)
from language_fixture import LANGUAGE
from release_common import described_versions, read_version_matrix


class ContractReaderTest(unittest.TestCase):
    def test_manifest_requires_schema_2_and_its_version_matrix(self):
        current = {'schema_version':3,'product_version':'0.4.0','language_version':'0.5',
                   'c_api_version':'0.3','c_abi_soversion':'0.4','source_commit':None,'working_tree_dirty':False,
                   'status':'development / release candidate','tag':None,'builtin_count':1,'example_count':1,
                   'headings':[{'title':'Book','anchor':'book','level':1}],
                   'source_files':{'docs/language/index.md':'a'*64},'files':{'docs/language.json':'b'*64}}
        self.assertIs(read_manifest(current),current)
        with self.assertRaisesRegex(RuntimeError,'manifest schema version'):
            read_manifest({**current,'schema_version':2})
        for key in ('language_version','c_api_version','c_abi_soversion'):
            for value in (None,False,4,{'major':0,'minor':4},'0.4.0'):
                with self.subTest(key=key,value=value), self.assertRaises(RuntimeError):
                    read_manifest({**current,key:value})
            missing = dict(current); del missing[key]
            with self.assertRaises(RuntimeError): read_manifest(missing)
        self.assertEqual(read_version_matrix({**current,'future':123})['language_version'],'0.5')

    def test_existing_abi_pair_normalizes_without_optional_alias(self):
        catalog = {'product_version':'0.5.0','language_version':{'major':0,'minor':5},
                   'c_api_version':{'major':0,'minor':3},
                   'abi_version':{'major':0,'minor':4}}
        self.assertEqual(described_versions(catalog),{'product_version':'0.5.0','language_version':'0.5',
                         'c_api_version':'0.3','c_abi_soversion':'0.4'})
        with self.assertRaises(RuntimeError): described_versions({**catalog,'c_abi_soversion':'0.5'})
        for value in (True,0.5,None):
            with self.assertRaises(RuntimeError): described_versions({**catalog,'abi_version':{'major':0,'minor':value}})

    def test_language_requires_the_current_schema_and_rules(self):
        self.assertIs(read_language(LANGUAGE),LANGUAGE)
        with self.assertRaisesRegex(RuntimeError,'language schema version'):
            read_language({**LANGUAGE,'schema_version':1})
        with self.assertRaisesRegex(RuntimeError,'language schema version'):
            read_language({**LANGUAGE,'schema_version':2})
        for key in ('source_rules','effect_rules','call_rules','language_version'):
            missing = dict(LANGUAGE); del missing[key]
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError,'language required fields'):
                read_language(missing)

    def test_malformed_top_level_and_unsupported_versions_are_rejected(self):
        for value in (None,False,[],{}, {'schema_version':2}):
            for reader in (read_language,read_diagnostics,read_manifest):
                with self.subTest(reader=reader.__name__,value=value):
                    with self.assertRaises(RuntimeError):
                        reader(value)

    def test_schema_3_accepts_exports_without_optional_capabilities(self):
        for components, ranges in ((False, False), (True, False), (False, True), (True, True)):
            with self.subTest(components=components, random_range=ranges):
                earlier = copy.deepcopy(LANGUAGE)
                if not components:
                    del earlier['source_rules']['types']['components']
                if not ranges:
                    del earlier['source_rules']['random_range']
                original = copy.deepcopy(earlier)
                self.assertIs(read_language(earlier), earlier)
                self.assertEqual(earlier, original)

    def test_optional_capabilities_still_reject_malformed_values(self):
        missing_result = copy.deepcopy(LANGUAGE['source_rules']['types']['components'])
        del missing_result[0]['result']
        for components in (None, False, {}, [], 'vec2', missing_result):
            with self.subTest(components=components):
                value = copy.deepcopy(LANGUAGE)
                value['source_rules']['types']['components'] = components
                with self.assertRaisesRegex(RuntimeError, 'components'):
                    read_language(value)
        missing_effects = copy.deepcopy(LANGUAGE['source_rules']['random_range'])
        del missing_effects['effects']
        for ranges in (None, False, {}, [], 'to', missing_effects):
            with self.subTest(random_range=ranges):
                value = copy.deepcopy(LANGUAGE)
                value['source_rules']['random_range'] = ranges
                with self.assertRaisesRegex(RuntimeError, 'random_range'):
                    read_language(value)

    def test_non_json_numbers_do_not_enter_python_only_consumers(self):
        for token in ('NaN','Infinity','-Infinity'):
            with self.assertRaises(ValueError):
                parse_json('{"value":'+token+'}')

    def test_additive_header_allowed_but_status_must_match_exit(self):
        report={'schema_version':1,'product_version':'0.4.0','language_version':{'major':0,'minor':5},
                'status':'ok','diagnostics':[],'future_field':{'arbitrary':[None,True]}}
        self.assertIs(read_diagnostics(report,0),report)
        report["schema_version"] = 1.0
        self.assertIs(read_diagnostics(report,0),report)
        with self.assertRaisesRegex(RuntimeError,'status/exit'):
            read_diagnostics(report,1)
        report['language_version']['major']=True
        with self.assertRaises(RuntimeError):
            read_diagnostics(report,0)

    def test_manifest_paths_and_known_types_do_not_accept_json_coercion(self):
        value={'schema_version':3,'product_version':'0.4.0','source_commit':None,
               'language_version':'0.5','c_api_version':'0.3','c_abi_soversion':'0.4',
               'working_tree_dirty':False,'status':'development / release candidate','tag':None,
               'builtin_count':1,'example_count':1,'headings':[{'level':1,'title':'Book','anchor':'book'}],
               'source_files':{'docs/book.md':'a'*64},'files':{'docs/book.md':'a'*64}}
        self.assertIs(read_manifest(value),value)
        for name in ('/abs','C:/escape','../escape','.','a/../escape','a//b','a\\b','nul\0name'):
            with self.subTest(name=name):
                broken={**value,'files':{name:'a'*64}}
                with self.assertRaises(RuntimeError):
                    read_manifest(broken)
        with self.assertRaises(RuntimeError):
            read_manifest({**value,'working_tree_dirty':'false'})


if __name__=='__main__':
    unittest.main()
