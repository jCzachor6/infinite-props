"""Build and check the language handbook from Markdown and compiler contracts."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path, PurePosixPath
from urllib.parse import unquote, urlsplit

from release_common import ROOT, described_versions, handbook_version, match_versions, product_version, versions
from contract_readers import (CURRENT, DOCUMENTATION_SCHEMA_VERSION, LANGUAGE_SCHEMA_VERSION, parse_json,
                              read_language, read_manifest)
from markdown_tokens import headings, links, manifest_headings, rewrite_links


DIRECTIVE = re.compile(r"^<!-- (example|builtin): (\S+) -->$", re.M)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    return path.read_text(encoding="utf-8")


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def run(*args):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True,
                          encoding="utf-8", timeout=60)


def source_path(root, base, name):
    path = (base / unquote(name)).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(), "Missing or escaping documentation file: " + name)
    return path


def contracts(compiler, root=ROOT, profile=CURRENT):
    result = run(compiler, "--describe-language")
    require(result.returncode == 0, "Cannot describe the language: " + result.stderr)
    value = parse_json(result.stdout)
    require(type(value) is dict and value.get("product_version") == product_version(root),
            "Compiler language description belongs to another product version")
    language = read_language(value, profile)
    if profile == CURRENT:
        # Language, format, C API and SOVERSION change independently of the product.
        described, expected = described_versions(language), versions(root)
        require(described == expected, "Compiler language description differs from the source versions: " +
                ", ".join(f"{key} {described[key]} != {expected[key]}" for key in expected if described[key] != expected[key]))
    return language


def identity(root, version):
    def git(*args):
        try:
            result = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, encoding="utf-8")
        except FileNotFoundError:
            return None  # The installed source archive does not require Git.
        return result.stdout.strip() if result.returncode == 0 else None
    toplevel = git("rev-parse", "--show-toplevel")
    commit = git("rev-parse", "HEAD") if toplevel and Path(toplevel).resolve() == root.resolve() else None
    dirty = bool(git("status", "--porcelain=v1")) if commit else False
    tag = "v" + version
    tagged = commit is not None and git("rev-parse", "refs/tags/" + tag + "^{commit}") == commit
    # A tag alone must not label candidate metadata as a finished release.
    from release_preflight import metadata
    try:
        metadata(root, tag, publishing=True)
        final = True
    except (OSError, RuntimeError, ValueError):
        final = False
    return {"product_version": version, "source_commit": commit, "working_tree_dirty": dirty,
            "status": "released" if tagged and final and not dirty else "development / release candidate",
            "tag": tag if tagged and final and not dirty else None}


def signatures(builtin):
    lines = ["| Signature | VM instruction | Minimum VM format |", "| --- | --- | --- |"]
    for overload in builtin["overloads"]:
        params = ", ".join(p["name"] + ": " + p["type"] for p in overload["parameters"])
        signature = builtin["name"] + "(" + params + ") -> " + overload["result"]
        minor = overload.get("minimum_format_minor")
        lines.append(f"| `{signature}` | {overload.get('opcode', 'compiler lowering')} | {'0.' + str(int(minor)) if minor is not None else '—'} |")
    if variadic := builtin.get("variadic"):
        params = ", ".join(p["name"] + ": " + p["type"] for p in variadic["parameters"])
        lines.append(f"| `{builtin['name']}({params}, ...) -> {variadic['result']}` | compiler lowering | — |")
        lines += ["", f"The variadic form requires at least {int(variadic['minimum_arguments'])} arguments."]
        if variadic["repeat"] == "choices":
            lines.append("`T` is a shared storable type; mixed numeric choices widen to `f32`.")
        else:
            lines.append("Additional coordinates repeat as `xN, yN` pairs; positional color comes first.")
    return "\n".join(lines)


def sources(root, language):
    root = root.resolve()
    directory = root / "docs/language"
    book = json.loads(read(directory / "book.json"))
    chapters = [source_path(root, directory, p) for p in book["chapters"]]
    require(len(set(chapters)) == len(chapters) and set(chapters) == set(directory.glob("*.md")),
            "Book must list every chapter exactly once")
    text = {p: read(p) for p in chapters}
    require(all(headings(t) and headings(t)[0]["level"] == 1 for t in text.values()), "Each chapter needs a title")
    # Book assembly nests chapters one level by prefixing `#`, as in `## Title`.
    require(all(h["atx"] and h["level"] < 6 for t in text.values() for h in headings(t)),
            "Handbook chapters need ATX headings of level 1-5")
    require(headings(text[chapters[0]])[0]["title"] == book["title"], "Book title differs from its first chapter")
    examples = root / "examples/language"
    cases = json.loads(read(examples / "cases.json"))["cases"]
    registered = {source_path(root, examples, c["file"]): c for c in cases}
    require(len(registered) == len(cases) and set(registered) == set(examples.rglob("*.ips")),
            "Register every language example exactly once")
    require(all(c["expect"] in ("pass", "compile-error", "trap") and
                (c["expect"] == "pass" or c.get("message")) for c in cases), "Invalid example expectations")
    described, referenced, navigation = [], set(), []
    for chapter, content in text.items():
        for match in DIRECTIVE.finditer(content):
            kind, name = match.groups()
            if kind == "builtin":
                described.append(name)
                following = content[match.end():].split("\n#", 1)[0].strip()
                require(bool(following) and not following.startswith("<!--"), "Missing builtin explanation: " + name)
            else:
                referenced.add(source_path(root, chapter.parent, name))
        # Validate authored links, including fragments in other Markdown files.
        for destination, _ in links(content):
            url = urlsplit(destination)
            if url.scheme or url.netloc:
                continue
            target = source_path(root, chapter.parent, url.path) if url.path else chapter
            if chapter == chapters[0] and target in chapters[1:]:
                navigation.append(target)
            if url.fragment and target.suffix == ".md":
                anchors = {h["anchor"] for h in headings(read(target))}
                require(unquote(url.fragment) in anchors, "Missing Markdown anchor: " + destination)
    require(navigation == chapters[1:], "Handbook contents must link every chapter in book order")
    require(set(referenced) == set(registered), "Every registered example must be included in the handbook")
    expected = {b["name"] for b in language["builtins"]}
    require(set(described) == expected and len(described) == len(expected),
            "Builtin descriptions differ from compiler: missing=" + str(sorted(expected - set(described))) +
            ", extra/duplicate=" + str(sorted(set(described) - expected)))
    return book, chapters, text, registered


def resources(root):
    files = [root / "docs/language/book.json", root / "examples/language/cases.json"]
    files += list((root / "examples/language").rglob("*.ips"))
    files += list((root / "examples/recipes").glob("*.ips")) + [root / "examples/recipes/README.md"]
    files += list((root / "spec").glob("*.md"))
    files += [p for p in (root / "docs").glob("*.md") if not p.name.startswith("language-v")]
    files += list((root / "docs").glob("*.schema.json"))
    files += list((root / "plans").glob("*.md"))
    if (root / "docs/llm-reference.json").is_file():
        files += [root / "docs/llm-reference.json"]
    return files


def build(compiler, destination, root=ROOT, llm_sections=None, llm_symbols=None, llm_max_bytes=None, profile=CURRENT):
    root = root.resolve()
    language = contracts(compiler, root, profile)
    _, chapters, authored, cases = sources(root, language)
    version = language["product_version"]
    minor = handbook_version(language, profile)
    info = identity(root, version)
    if profile == CURRENT:
        info.update(match_versions(described_versions(language), versions(root)))
    builtins = {b["name"]: b for b in language["builtins"]}
    output, inputs = {}, {}

    def record(path):
        relative = path.relative_to(root).as_posix()
        inputs[relative] = digest(read(path).encode("utf-8"))
        return relative

    expanded = {}
    for chapter in chapters:
        record(chapter)
        def replace(match):
            kind, name = match.groups()
            if kind == "builtin":
                return signatures(builtins[name])
            file = source_path(root, chapter.parent, name)
            record(file)
            case = cases[file]
            label = "Expected result: " + case["expect"]
            if case.get("message"):
                label += " (`" + case["message"] + "`)"
            return label + ".\n\n```ips\n" + read(file).rstrip() + "\n```"
        content = DIRECTIVE.sub(replace, authored[chapter])
        expanded[chapter] = content
        output[chapter.relative_to(root).as_posix()] = content

    def flatten(chapter, content):
        def link(destination):
            url = urlsplit(destination)
            if url.scheme or url.netloc or not url.path:
                return None
            target = source_path(root, chapter.parent, url.path)
            if target in expanded:
                return "#" + (url.fragment or headings(expanded[target])[0]["anchor"])
            href = Path(os.path.relpath(target, root / "docs")).as_posix()
            return href + "#" + url.fragment if url.fragment else href
        content = rewrite_links(content, link)
        # Nest chapter headings by one level; code and other text stay literal.
        lines = content.split("\n")
        if chapter != chapters[0]:
            for heading in headings(content):
                lines[heading["line"]] = "#" + lines[heading["line"]]
        return "\n".join(lines).removesuffix("\n")

    combined = "\n\n".join(flatten(p, expanded[p]) for p in chapters) + "\n"
    first, rest = combined.split("\n", 1)
    matrix_text = (f"Product: **{version}** · Source language: **{info['language_version']}** · "
                   f"Binary format: **{info['format_version']}** · C API: **{info['c_api_version']}** · "
                   f"C ABI: **{info['c_abi_soversion']}**" if profile == CURRENT else f"Version: **{version}**")
    combined = first + "\n\n" + matrix_text + f" · Status: **{info['status']}** · Source: `{info['source_commit'] or 'source archive'}`\n" + rest
    output[f"docs/language-v{minor}.md"] = combined
    output["docs/language.json"] = json.dumps(language, ensure_ascii=False, indent=2) + "\n"
    llm_checks = []
    has_llm_config = (root / "docs/llm-reference.json").is_file()
    require(has_llm_config or "source_rules" not in language,
            "Current compiler rules require docs/llm-reference.json")
    require(has_llm_config or all(value is None for value in (llm_sections, llm_symbols, llm_max_bytes)),
            "LLM selections require docs/llm-reference.json")
    if has_llm_config:
        from llm_reference import render
        output["LLM_REFERENCE.md"], llm_checks = render(root, language, authored, cases, info, compiler,
                                                     llm_sections, llm_symbols, llm_max_bytes)
    # Ship the linked standalone sources and frozen numerical contracts offline.
    for file in resources(root):
        output[record(file)] = read(file)
    info.update({"schema_version": DOCUMENTATION_SCHEMA_VERSION if profile == CURRENT else 1, "builtin_count": len(builtins), "example_count": len(cases),
                 "headings": manifest_headings(combined),
                 "source_files": dict(sorted(inputs.items())),
                 "files": {name: digest(text.encode("utf-8")) for name, text in sorted(output.items())}})
    require(destination.resolve() != root.resolve() and not root.resolve().is_relative_to(destination.resolve()),
            "Documentation output must not replace the source checkout")
    require(not any((root / name).resolve().is_relative_to(destination.resolve()) for name in inputs),
            "Documentation output must not contain authored input files")
    # Validate everything first; then replace only the designated generated tree.
    if destination.exists() and any(destination.iterdir()):
        marker = destination / "docs/language-manifest.json"
        require(marker.is_file(), "Use an empty or previously generated documentation directory")
        previous = json.loads(read(marker))
        known = set(previous.get("files", {})) | {"docs/language-manifest.json"}
        existing = {p.relative_to(destination).as_posix() for p in destination.rglob("*") if p.is_file()}
        require(existing <= known, "Documentation output contains files outside its generated manifest")
        shutil.rmtree(destination)
    for name, content in output.items():
        file = destination / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(content, encoding="utf-8", newline="\n")
    write_json(destination / "docs/language-manifest.json", info)
    verify(destination, version, info["source_commit"], root=root, profile=profile)
    # Execution evidence belongs to the check report, not a new manifest schema.
    if llm_checks:
        info["llm_reference_checks"] = llm_checks
    return info


def member_name(name):
    require(isinstance(name, str) and "\\" not in name and "\0" not in name,
            "Invalid documentation member")
    path = PurePosixPath(name)
    require(path.parts and not path.is_absolute() and path.as_posix() == name and
            all(p not in (".", "..") for p in path.parts), "Invalid documentation member")
    return name


def verify_contents(load, version=None, commit=None, root=None, clean=False, profile=CURRENT):
    manifest = read_manifest(parse_json(load("docs/language-manifest.json")), profile)
    if version is not None:
        require(manifest.get("product_version") == version, "Documentation version differs")
    if commit is not None:
        require(manifest.get("source_commit") == commit, "Documentation source commit differs")
    if clean:
        require(manifest.get("working_tree_dirty") is False, "Documentation was generated from an uncommitted checkout")
    for name, expected in manifest["files"].items():
        require(digest(load(member_name(name))) == expected, "Documentation file changed: " + name)
    language = read_language(parse_json(load("docs/language.json")), profile)
    require(language["product_version"] == manifest["product_version"] and
            len(language["builtins"]) == manifest["builtin_count"], "Documentation catalog differs")
    if profile == CURRENT:
        match_versions(manifest, described_versions(language))
        required_contracts = {"LLM_REFERENCE.md", "docs/llm-reference.json"}
        for name, schema_version in (("language-description", LANGUAGE_SCHEMA_VERSION), ("compiler-diagnostics", 1),
                                     ("documentation-manifest", DOCUMENTATION_SCHEMA_VERSION)):
            path = "docs/" + name + ".schema.json"
            required_contracts.add(path)
            schema = parse_json(load(path))
            require(type(schema) is dict and schema.get("$schema") == "https://json-schema.org/draft/2020-12/schema" and
                    type(schema.get("properties")) is dict and
                    type(schema["properties"].get("schema_version")) is dict and
                    schema["properties"]["schema_version"].get("type") == "integer" and
                    type(schema["properties"]["schema_version"].get("const")) in {int, float} and
                    schema["properties"]["schema_version"].get("const") == schema_version,
                    "Documentation schema contract differs: " + path)
        require(required_contracts <= manifest["files"].keys(), "Current documentation omits export contracts")
    cases = json.loads(load("examples/language/cases.json"))["cases"]
    require(len(cases) == manifest["example_count"], "Documentation example count differs")
    minor = handbook_version(language, profile)
    required = set(manifest["source_files"]) | {"docs/language.json", f"docs/language-v{minor}.md"}
    if "docs/llm-reference.json" in manifest["source_files"]:
        required.add("LLM_REFERENCE.md")
    require(set(manifest["files"]) == required, "Documentation manifest omits or adds payload files")
    if root is not None:
        root = root.resolve()
        if profile == CURRENT:
            match_versions(manifest, versions(root))
        _, chapters, _, _ = sources(root, language)
        expected = {p.relative_to(root).as_posix(): digest(read(p).encode("utf-8")) for p in [*chapters, *resources(root)]}
        require(manifest["source_files"] == expected, "Documentation inputs differ from the source checkout")
        info = identity(root, manifest["product_version"])
        require(manifest.get("source_commit") == info["source_commit"], "Documentation source commit differs from the source checkout")
        require(manifest.get("status") == info["status"] and manifest.get("tag") == info["tag"],
                "Documentation release status differs from the source checkout")
    return manifest


def verify(directory, version=None, commit=None, root=None, compiler=None, clean=False, profile=CURRENT):
    directory = directory.resolve()
    def load(name):
        file = directory.joinpath(*PurePosixPath(member_name(name)).parts)
        require(file.resolve().is_relative_to(directory) and file.is_file(), "Missing or escaping documentation member: " + name)
        return file.read_bytes()
    manifest = verify_contents(load, version, commit, root, clean, profile)
    if compiler is not None:
        require(json.loads(load("docs/language.json")) == contracts(compiler, root or ROOT, profile),
                "Documentation signatures differ from the installed compiler")
    return manifest


def verify_archive(file, prefix, version, commit, root=ROOT, profile=CURRENT):
    with tarfile.open(file, "r:gz") as archive:
        names = [m.name for m in archive]
        require(len(names) == len(set(names)), "Duplicate archive member")
        def load(name):
            name = prefix.rstrip("/") + "/" + member_name(name)
            require(name in names, "Missing archived documentation: " + name)
            member = archive.getmember(name)
            require(member.isfile(), "Archived documentation is not a file: " + name)
            return archive.extractfile(member).read()
        return verify_contents(load, version, commit, root, clean=True, profile=profile)


def check_examples(compiler, language, cases, root=ROOT):
    generator = compiler.parent / ("ipgen.exe" if compiler.suffix == ".exe" else "ipgen")
    covered, results = set(), []
    names = {b["name"] for b in language["builtins"]}
    with tempfile.TemporaryDirectory(prefix="iprop-language-examples-") as temporary:
        for number, (source, case) in enumerate(cases.items()):
            binary = Path(temporary) / f"{number}.iprop"
            compiled = run(compiler, source, "-o", binary)
            if case["expect"] == "compile-error":
                require(compiled.returncode != 0 and case["message"] in compiled.stderr and not binary.exists(),
                        "Expected compile rejection: " + str(source) + "\n" + compiled.stderr)
            else:
                require(compiled.returncode == 0, "Example did not compile: " + str(source) + "\n" + compiled.stderr)
                image = Path(temporary) / f"{number}.png"
                generated = run(generator, binary, "--seed", 7, "-o", image)
                if case["expect"] == "trap":
                    require(generated.returncode != 0 and case["message"] in generated.stdout + generated.stderr,
                            "Expected runtime trap: " + str(source) + "\n" + generated.stdout + generated.stderr)
                else:
                    require(generated.returncode == 0 and image.is_file(), "Example generation failed: " + str(source) + "\n" + generated.stderr)
                    code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', " ", read(source), flags=re.S)
                    covered |= {name for name in names if re.search(r"(?<![\w.])" + re.escape(name) + r"\s*\(", code)}
            results.append({"file": source.relative_to(root).as_posix(), "expect": case["expect"], "passed": True})
    require(covered == names, "Builtins without a successful executable example: " + str(sorted(names - covered)))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "check", "verify"))
    parser.add_argument("--compiler", type=Path, default=ROOT / ("build/tools/ipc.exe" if os.name == "nt" else "build/tools/ipc"))
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/language-docs")
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--version", help="required product version when verifying")
    parser.add_argument("--commit", help="required source commit when verifying")
    parser.add_argument("--against-source", action="store_true", help="verify inputs and signatures against this checkout/compiler")
    parser.add_argument("--clean", action="store_true", help="reject documentation generated from uncommitted changes")
    parser.add_argument("--llm-sections", help="comma-separated LLM section ids; empty selects none")
    parser.add_argument("--llm-symbols", help="comma-separated compiler builtin names; empty selects none")
    parser.add_argument("--llm-max-bytes", type=int, help="UTF-8 byte budget for LLM_REFERENCE.md (maximum 16384)")
    args = parser.parse_args()
    if args.action == "verify" and any(value is not None for value in (args.llm_sections, args.llm_symbols, args.llm_max_bytes)):
        parser.error("LLM selections apply to build/check, not verify")
    if args.action == "verify":
        info = verify(args.output_dir.resolve(), args.version, args.commit,
                      root=ROOT if args.against_source else None,
                      compiler=args.compiler.resolve() if args.against_source else None, clean=args.clean)
    elif args.action == "build":
        info = build(args.compiler.resolve(), args.output_dir.resolve(), llm_sections=args.llm_sections,
                     llm_symbols=args.llm_symbols, llm_max_bytes=args.llm_max_bytes)
    else:
        language = contracts(args.compiler.resolve())
        _, _, _, cases = sources(ROOT, language)
        results = check_examples(args.compiler.resolve(), language, cases)
        with tempfile.TemporaryDirectory(prefix="iprop-doc-check-") as temporary:
            info = build(args.compiler.resolve(), Path(temporary) / "book", llm_sections=args.llm_sections,
                         llm_symbols=args.llm_symbols, llm_max_bytes=args.llm_max_bytes)
        if args.report_dir:
            write_json(args.report_dir / "language-docs.json", {**identity(ROOT, language["product_version"]),
                       "passed": True, "builtin_count": len(language["builtins"]), "examples": results,
                       "llm_reference_checks": info.get("llm_reference_checks", [])})
    print(f"Language documentation: PASS {info['product_version']} ({info['builtin_count']} builtins, {info['example_count']} examples)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError, tarfile.TarError) as error:
        print("language documentation: " + str(error), file=sys.stderr)
        sys.exit(1)
