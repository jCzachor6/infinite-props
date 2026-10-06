"""Dependency-free protocol rejection; actual exports are checked by CTest."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from contract_readers import (CURRENT,HISTORICAL,parse_json,read_diagnostics,read_language,read_manifest)
from language_fixture import LANGUAGE


class ContractReaderTest(unittest.TestCase):
    def test_legacy_requires_explicit_profile_even_for_product_0_4(self):
        with self.assertRaisesRegex(RuntimeError,'current language required fields'):
            read_language(LANGUAGE)
        self.assertIs(read_language(LANGUAGE,HISTORICAL),LANGUAGE)
        for profile in ('unknown',None,[]):
            with self.assertRaises(RuntimeError):
                read_language(LANGUAGE,profile)

    def test_malformed_top_level_and_unsupported_versions_are_rejected(self):
        for value in (None,False,[],{}, {'schema_version':2}):
            for reader in (read_language,read_diagnostics,read_manifest):
                with self.subTest(reader=reader.__name__,value=value):
                    with self.assertRaises(RuntimeError):
                        reader(value)

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
        value={'schema_version':1,'product_version':'0.4.0','source_commit':None,
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
