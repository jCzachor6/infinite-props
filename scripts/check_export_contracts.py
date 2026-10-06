#!/usr/bin/env python3
"""Validate real exports, shared mutation fixtures, and all bundle boundaries.

--schema additionally runs the pinned CI JSON Schema implementation. Ordinary
builds and this check's default mode need only the standard library and Node.
"""
import argparse
import copy
import hashlib
import io
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

from contract_readers import (CURRENT, HISTORICAL, LANGUAGE_SCHEMA_VERSION,
    DIAGNOSTICS_SCHEMA_VERSION, DOCUMENTATION_SCHEMA_VERSION, parse_json,
    read_language, read_diagnostics, read_manifest)
import language_docs
from release_common import VERSION_SOURCES

ROOT = Path(__file__).resolve().parent.parent
SCHEMAS = {'language':'language-description', 'diagnostics':'compiler-diagnostics',
           'manifest':'documentation-manifest'}
READERS = {'language':read_language, 'diagnostics':lambda v: read_diagnostics(v,1),
           'manifest':read_manifest}


def mutate(original, case):
    value = copy.deepcopy(original)
    parent = value
    for key in case['path'][:-1]:
        parent = parent[key]
    if case.get('delete'):
        del parent[case['path'][-1]]
    else:
        parent[case['path'][-1]] = case['value']
    return value


def accepts(reader, value):
    try:
        reader(value)
        return True
    except (RuntimeError, ValueError):
        return False


def field_docs(value, schema, full):
    """Coverage of emitted known fields, not validation or a schema engine."""
    if '$ref' in schema:
        target = full
        for key in schema['$ref'][2:].split('/'):
            target = target[key]
        schema = target
    if value is None:
        return
    if 'oneOf' in schema:
        schema = next(s for s in schema['oneOf'] if s.get('type') != 'null')
        return field_docs(value,schema,full)
    if type(value) is dict:
        for key, item in value.items():
            if 'properties' in schema:
                assert key in schema['properties'], 'Undocumented emitted field: '+key
                member = schema['properties'][key]
                assert member.get('description'), 'Missing field description: '+key
            else:
                member = schema['additionalProperties']
            field_docs(item,member,full)
    elif type(value) is list:
        for item in value:
            field_docs(item,schema['items'],full)


def run(compiler, *arguments, env=None):
    return subprocess.run([str(compiler), *map(str,arguments)], capture_output=True,
                          text=True,encoding='utf-8',timeout=60,env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler',type=Path)
    parser.add_argument('--probe',type=Path)
    parser.add_argument('--schema',action='store_true')
    parser.add_argument('--report',type=Path)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file() and compiler.with_suffix('.exe').is_file():
        compiler = compiler.with_suffix('.exe')
    schemas = {k:parse_json((ROOT/'docs'/f'{v}.schema.json').read_bytes())
               for k,v in SCHEMAS.items()}
    assert [schemas[k]['properties']['schema_version']['const'] for k in SCHEMAS] == [
        LANGUAGE_SCHEMA_VERSION, DIAGNOSTICS_SCHEMA_VERSION, DOCUMENTATION_SCHEMA_VERSION]
    validators = None
    if args.schema:
        from jsonschema import Draft202012Validator
        for schema in schemas.values():
            Draft202012Validator.check_schema(schema)
        validators = {k:Draft202012Validator(v) for k,v in schemas.items()}
    process = run(compiler,'--describe-language')
    assert process.returncode == 0 and not process.stderr
    language = read_language(parse_json(process.stdout))
    frozen = parse_json((ROOT/'benchmarks/llm/baseline-reference/language.json').read_bytes())
    assert accepts(read_language,frozen) is False
    read_language(frozen,HISTORICAL)
    assert accepts(lambda v:read_language(v,HISTORICAL),language) is False
    malformed_frozen = copy.deepcopy(frozen)
    malformed_frozen['builtins'][0]['overloads'][0]['effects'] = {'rng':'unknown'}
    assert not accepts(lambda v:read_language(v,HISTORICAL),malformed_frozen)
    if validators:
        historical = {**schemas['language'], **schemas['language']['$defs']['historical04']}
        Draft202012Validator(historical).validate(frozen)
        assert not Draft202012Validator(historical).is_valid(malformed_frozen)
    corpus = parse_json((ROOT/'tests/fixtures/export-contract-mutations.json').read_bytes())
    results, diagnostic_cases = [], []
    with tempfile.TemporaryDirectory(prefix='iprop-export-contract-') as temporary:
        temp = Path(temporary)
        source = temp/'source.ips'
        source.write_text('asset A { size 1x1 draw { x=1; x=#fff } }\n',encoding='utf-8')
        failure = run(compiler,source,'--check','--diagnostics=json')
        diagnostics = read_diagnostics(parse_json(failure.stdout),failure.returncode)
        assert failure.returncode == 1 and diagnostics['diagnostics'][0]['range']
        # All registered acceptance and rejection cases exercise the actual CLI.
        examples = parse_json((ROOT/'examples/language/cases.json').read_bytes())['cases']
        for case in examples:
            process = run(compiler,ROOT/'examples/language'/case['file'],'--check','--diagnostics=json')
            document = read_diagnostics(parse_json(process.stdout),process.returncode)
            assert process.returncode == (1 if case['expect']=='compile-error' else 0), case
            if validators:
                validators['diagnostics'].validate(document)
            field_docs(document,schemas['diagnostics'],schemas['diagnostics'])
        cli = run(compiler,'--not-an-option','--diagnostics=json')
        read_diagnostics(parse_json(cli.stdout),2)
        if validators:
            validators['diagnostics'].validate(parse_json(cli.stdout))
        if args.probe:
            probe = args.probe.resolve()
            if not probe.is_file(): probe=probe.with_suffix('.exe')
            internal = run(probe,source,'--check','--diagnostics=json',
                env={**os.environ,'IPROP_DIAGNOSTIC_FAIL_AT':'0','IPROP_DIAGNOSTIC_FAIL_PERSIST':'1'})
            read_diagnostics(parse_json(internal.stdout),3)
            if validators:
                validators['diagnostics'].validate(parse_json(internal.stdout))
        # A complete current export cannot silently use the old fixture path
        # when the default compact-reference configuration is absent.
        shadow = temp/'source-checkout'
        shadow.mkdir()
        for name, _ in VERSION_SOURCES.values():
            (shadow/name).parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/name,shadow/name)
        for directory in ('docs','examples','spec','plans'):
            shutil.copytree(ROOT/directory,shadow/directory)
        (shadow/'docs/llm-reference.json').unlink()
        try:
            language_docs.build(compiler,temp/'missing-config-output',root=shadow)
            raise AssertionError('Missing current configuration was accepted')
        except RuntimeError as error:
            assert 'Current compiler rules require' in str(error), error
        assert not (temp/'missing-config-output').exists()
        # The real export cannot describe a checkout whose emitted format differs.
        constants = shadow/'runtime/iprop/Constants.hpp'
        constants.write_text(re.sub(r'(\bkVersionMinor\s*=\s*)(\d+)', lambda m: m[1]+str(int(m[2])+1),
                                    constants.read_text(encoding='utf-8'), count=1), encoding='utf-8')
        try:
            language_docs.contracts(compiler,shadow)
            raise AssertionError('A compiler emitting another format was accepted')
        except RuntimeError as error:
            assert 'differs from the source versions: format_version' in str(error), error
        bundle = temp/'bundle'
        language_docs.build(compiler,bundle)
        manifest = read_manifest(parse_json((bundle/'docs/language-manifest.json').read_bytes()))
        bases = {'language':language,'diagnostics':diagnostics,'manifest':manifest}
        for kind, base in bases.items():
            field_docs(base,schemas[kind],schemas[kind])
            if validators: validators[kind].validate(base)
            for case in corpus[kind]:
                value = mutate(base,case)
                reader = (lambda v:read_diagnostics(v,case.get('exit',1))) if kind=='diagnostics' else READERS[kind]
                assert accepts(reader,value) == case['accept'], (kind,case['id'])
                if validators:
                    assert validators[kind].is_valid(value) == case['schema'], (kind,case['id'],'schema')
                results.append({'kind':kind,'case':case['id'],'passed':True})
                if kind=='diagnostics':
                    diagnostic_cases.append({'document':value,'exit':case.get('exit',1),'accept':case['accept'],'id':case['id']})
        # A complete current archive is accepted before semantic corruption.
        manifest['working_tree_dirty']=False  # explicit clean fixture identity, not a release claim
        (bundle/'docs/language-manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
        valid_archive=temp/'valid-bundle.tar.gz'
        with tarfile.open(valid_archive,'w:gz') as tar:
            tar.add(bundle,arcname='bundle')
        language_docs.verify_archive(valid_archive,'bundle',manifest['product_version'],None,root=None)
        # Rehashing a malformed catalog must not bypass package or site validation.
        catalog = bundle/'docs/language.json'
        original_catalog = catalog.read_bytes()
        catalog.write_text(json.dumps(mutate(language,next(case for case in corpus['language'] if case['id']=='wrong evaluation permutation'))),encoding='utf-8')
        manifest['files']['docs/language.json']=hashlib.sha256(catalog.read_bytes()).hexdigest()
        (bundle/'docs/language-manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
        assert not accepts(lambda _:language_docs.verify(bundle),None)
        manifest['working_tree_dirty']=False
        (bundle/'docs/language-manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
        archive=temp/'bundle.tar.gz'
        with tarfile.open(archive,'w:gz') as tar:
            tar.add(bundle,arcname='bundle')
        assert not accepts(lambda _:language_docs.verify_archive(archive,'bundle',manifest['product_version'],None,root=None),None)
        # A false schema-constant type cannot pass by rehashing that schema.
        catalog.write_bytes(original_catalog)
        manifest['files']['docs/language.json']=hashlib.sha256(original_catalog).hexdigest()
        schema_path=bundle/'docs/compiler-diagnostics.schema.json'
        broken_schema=parse_json(schema_path.read_bytes())
        broken_schema['properties']['schema_version']['const']=True
        schema_path.write_text(json.dumps(broken_schema),encoding='utf-8')
        manifest['files']['docs/compiler-diagnostics.schema.json']=hashlib.sha256(schema_path.read_bytes()).hexdigest()
        (bundle/'docs/language-manifest.json').write_text(json.dumps(manifest),encoding='utf-8')
        assert not accepts(lambda _:language_docs.verify(bundle),None)
        fixture = temp/'diagnostics.json'
        fixture.write_text(json.dumps(diagnostic_cases),encoding='utf-8')
        node = shutil.which('node')
        assert node, 'Node is required to check editor consumer agreement'
        subprocess.run([node,str(ROOT/'editor/test/contract-corpus.js'),str(fixture)],check=True,timeout=30)
    report = {'passed':True,'schema_validation':args.schema,'profile':'current',
              'builtin_count':len(language['builtins']),'registered_examples':len(examples),
              'missing_current_config_rejected':True,'source_version_mismatch_rejected':True,'rehashed_catalog_rejected':True,
              'rehashed_schema_rejected':True,'valid_current_archive_accepted':True,
              'mutations':results,'protocol_versions':{'language':1,'diagnostics':1,'manifest':1}}
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f"Export contracts: PASS ({len(results)} mutations, {len(examples)} real CLI cases)")


if __name__=='__main__':
    main()
