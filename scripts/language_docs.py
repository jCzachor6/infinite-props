"""Build and check the language handbook from Markdown and compiler contracts."""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path, PurePosixPath
from urllib.parse import unquote, urlsplit

from release_common import ROOT


DIRECTIVE = re.compile(r"^<!-- (example|builtin): (\S+) -->$", re.M)
LINK = re.compile(r"\[[^\]\n]+\]\(([^\s)]+)\)")


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


def slug(text):
    # GitHub-style heading anchors, also used by the HTML renderer.
    return re.sub(r"[^\w\s-]", "", text.lower()).strip().replace(" ", "-")


def headings(text):
    result, counts, fence = [], Counter(), False
    for line in text.splitlines():
        if line.startswith("```"):
            fence = not fence
        if not fence and (match := re.match(r"^(#{1,6}) (.+)$", line)):
            title = match[2]
            base = slug(title)
            anchor = base + ("-" + str(counts[base]) if counts[base] else "")
            counts[base] += 1
            result.append({"title": title, "anchor": anchor, "level": len(match[1])})
    return result


def source_path(root, base, name):
    path = (base / unquote(name)).resolve()
    require(path.is_relative_to(root.resolve()) and path.is_file(), "Missing or escaping documentation file: " + name)
    return path


def contracts(compiler, root=ROOT):
    result = run(compiler, "--describe-language")
    require(result.returncode == 0, "Cannot describe the language: " + result.stderr)
    value = json.loads(result.stdout)
    version = re.search(r"\bVERSION\s+(\d+\.\d+\.\d+)", read(root / "CMakeLists.txt"))[1]
    require(value.get("schema_version") == 1 and value.get("product_version") == version,
            "Compiler language description belongs to another product version")
    names = [b["name"] for b in value["builtins"]]
    require(len(names) == len(set(names)), "Duplicate compiler builtin")
    require(value.get("emoji_aliases"), "Missing lexer aliases")
    return value


def identity(root, version):
    def git(*args):
        result = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, encoding="utf-8")
        return result.stdout.strip() if result.returncode == 0 else None
    commit = git("rev-parse", "HEAD") if git("rev-parse", "--show-toplevel") == str(root.resolve()) else None
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
        lines.append(f"| `{signature}` | {overload.get('opcode', 'compiler lowering')} | {'0.' + str(minor) if minor is not None else '—'} |")
    if variadic := builtin.get("variadic"):
        params = ", ".join(p["name"] + ": " + p["type"] for p in variadic["parameters"])
        lines.append(f"| `{builtin['name']}({params}, ...) -> {variadic['result']}` | compiler lowering | — |")
        lines += ["", f"The variadic form requires at least {variadic['minimum_arguments']} arguments."]
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
        for match in LINK.finditer(content):
            url = urlsplit(match[1])
            if url.scheme or url.netloc:
                continue
            target = source_path(root, chapter.parent, url.path) if url.path else chapter
            if chapter == chapters[0] and target in chapters[1:]:
                navigation.append(target)
            if url.fragment and target.suffix == ".md":
                anchors = {h["anchor"] for h in headings(read(target))}
                require(unquote(url.fragment) in anchors, "Missing Markdown anchor: " + match[1])
    require(navigation == chapters[1:], "Handbook contents must link every chapter in book order")
    require(set(referenced) == set(registered), "Every registered example must be included in the handbook")
    expected = {b["name"] for b in language["builtins"]}
    require(set(described) == expected and len(described) == len(expected),
            "Builtin descriptions differ from compiler: missing=" + str(sorted(expected - set(described))) +
            ", extra/duplicate=" + str(sorted(set(described) - expected)))
    require(sum(t.count("<!-- aliases -->") for t in text.values()) == 1, "Require exactly one lexer alias table")
    return book, chapters, text, registered


def build(compiler, destination, root=ROOT):
    root = root.resolve()
    language = contracts(compiler, root)
    _, chapters, authored, cases = sources(root, language)
    version = language["product_version"]
    minor = ".".join(version.split(".")[:2])
    info = identity(root, version)
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
        aliases = ["| Word | Alias | Unicode scalar |", "| --- | --- | --- |"]
        aliases += [f"| `{a['word']}` | {chr(a['codepoint'])} | U+{a['codepoint']:04X} |" for a in language["emoji_aliases"]]
        content = content.replace("<!-- aliases -->", "\n".join(aliases))
        expanded[chapter] = content
        output[chapter.relative_to(root).as_posix()] = content

    def flatten(chapter, content):
        def link(match):
            url = urlsplit(match[1])
            if url.scheme or url.netloc or not url.path:
                return match[0]
            target = source_path(root, chapter.parent, url.path)
            if target in expanded:
                href = "#" + (url.fragment or headings(expanded[target])[0]["anchor"])
            else:
                href = Path(os.path.relpath(target, root / "docs")).as_posix()
                if url.fragment:
                    href += "#" + url.fragment
            return match[0].replace(match[1], href)
        content = LINK.sub(link, content)
        # Shift chapter headings without touching literal example code.
        lines, fence = [], False
        for line in content.splitlines():
            if line.startswith("```"):
                fence = not fence
            if not fence and chapter != chapters[0] and line.startswith("#"):
                line = "#" + line
            lines.append(line)
        return "\n".join(lines)

    combined = "\n\n".join(flatten(p, expanded[p]) for p in chapters) + "\n"
    first, rest = combined.split("\n", 1)
    combined = first + "\n\n" + f"Version: **{version}** · Status: **{info['status']}** · Source: `{info['source_commit'] or 'source archive'}`\n" + rest
    output[f"docs/language-v{minor}.md"] = combined
    output["docs/language.json"] = json.dumps(language, ensure_ascii=False, indent=2) + "\n"
    # Ship the linked standalone sources and frozen numerical contracts offline.
    resources = [root / "docs/language/book.json", root / "examples/language/cases.json"]
    resources += list((root / "examples/language").rglob("*.ips"))
    resources += list((root / "examples/recipes").glob("*.ips")) + [root / "examples/recipes/README.md"]
    resources += list((root / "spec").glob("*.md"))
    resources += list((root / "docs").glob("*optim*.md"))
    for file in resources:
        output[record(file)] = read(file)
    info.update({"schema_version": 1, "builtin_count": len(builtins), "example_count": len(cases),
                 "headings": headings(combined),
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
    verify(destination, version, info["source_commit"])
    return info


def verify(directory, version=None, commit=None):
    def path(name):
        parts = PurePosixPath(name).parts
        require(parts and not name.startswith("/") and all(p not in (".", "..") for p in parts), "Invalid documentation member")
        result = directory.joinpath(*parts)
        require(result.resolve().is_relative_to(directory.resolve()), "Documentation member escapes bundle")
        return result
    manifest = json.loads(read(path("docs/language-manifest.json")))
    require(manifest.get("schema_version") == 1 and manifest.get("files") and manifest.get("source_files"), "Incomplete documentation manifest")
    if version is not None:
        require(manifest.get("product_version") == version, "Documentation version differs")
    if commit is not None:
        require(manifest.get("source_commit") == commit, "Documentation source commit differs")
    for name, expected in manifest["files"].items():
        file = path(name)
        require(file.is_file() and digest(file.read_bytes()) == expected, "Documentation file changed: " + name)
    language = json.loads(read(path("docs/language.json")))
    require(language["product_version"] == manifest["product_version"] and len(language["builtins"]) == manifest["builtin_count"], "Documentation catalog differs")
    return manifest


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
    args = parser.parse_args()
    if args.action == "verify":
        info = verify(args.output_dir.resolve())
    elif args.action == "build":
        info = build(args.compiler.resolve(), args.output_dir.resolve())
    else:
        language = contracts(args.compiler.resolve())
        _, _, _, cases = sources(ROOT, language)
        results = check_examples(args.compiler.resolve(), language, cases)
        with tempfile.TemporaryDirectory(prefix="iprop-doc-check-") as temporary:
            info = build(args.compiler.resolve(), Path(temporary) / "book")
        if args.report_dir:
            write_json(args.report_dir / "language-docs.json", {**identity(ROOT, language["product_version"]),
                       "passed": True, "builtin_count": len(language["builtins"]), "examples": results})
    print(f"Language documentation: PASS {info['product_version']} ({info['builtin_count']} builtins, {info['example_count']} examples)")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print("language documentation: " + str(error), file=sys.stderr)
        sys.exit(1)
