"""Handbook Markdown structure, parsed exactly as the website renders it.

The website uses markdown-it (JavaScript) with raw HTML, linkify and the
typographer disabled. This module uses its Python port with the same preset,
vendored with mdurl in third_party/python, so builds need no pip install.
Line numbers are zero-based, counted on the text split at "\\n".
"""
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "third_party" / "python"))
from markdown_it import MarkdownIt  # noqa: E402  (vendored, after the path entry)

PARSER = MarkdownIt("js-default")


def slug(text):
    # GitHub-style heading anchors, also used by the HTML renderer.
    return re.sub(r"[^\w\s-]", "", text.lower()).strip().replace(" ", "-")


def headings(text):
    """Every heading: inline source title, level, unique anchor and its line.

    `atx` is false for setext headings, which span two lines and cannot be
    shifted by prefixing `#`.
    """
    tokens = PARSER.parse(text)
    result, counts = [], Counter()
    for index, token in enumerate(tokens):
        if token.type != "heading_open":
            continue
        title = tokens[index + 1].content
        base = slug(title)
        result.append({"title": title, "anchor": base + ("-" + str(counts[base]) if counts[base] else ""),
                       "level": int(token.tag[1]), "line": token.map[0], "atx": token.markup.startswith("#")})
        counts[base] += 1
    return result


def manifest_headings(text):
    """The published heading fields of the documentation manifest."""
    return [{"title": h["title"], "anchor": h["anchor"], "level": h["level"]} for h in headings(text)]


def links(text):
    """Link and image destinations, in document order, with their source lines.

    Destinations inside code, code spans or escaped text are not links. Values
    are markdown-it's normalized (percent-encoded) destinations.
    """
    found = []
    for token in PARSER.parse(text):
        if token.type != "inline":
            continue
        line = token.map[0]
        for child in token.children or []:
            if child.type in ("softbreak", "hardbreak"):
                line += 1
            elif child.type == "link_open":
                found.append((child.attrGet("href"), line))
            elif child.type == "image":
                found.append((child.attrGet("src"), line))
    return found


def normalize_link(destination):
    """The destination form that `links` reports for raw Markdown text."""
    return PARSER.normalizeLink(destination)


# Inline `[label](destination)` spelling, without a title or angle brackets.
_INLINE_LINK = re.compile(r"\[([^\]\n]+)\]\(([^\s)]+)\)")


def rewrite_links(text, replace):
    """Replace inline link destinations with replace(destination).

    Only spellings that markdown-it parses as links on that line change; links
    with titles or angle brackets, code and escaped text stay literal. A None
    result keeps the link. Line structure and all other bytes are preserved.
    """
    real = {}
    for destination, line in links(text):
        real.setdefault(line, set()).add(destination)
    lines = text.split("\n")

    def substitute(match, destinations):
        if normalize_link(match[2]) not in destinations:
            return match[0]
        new = replace(match[2])
        return match[0] if new is None else "[" + match[1] + "](" + new + ")"

    for number, destinations in real.items():
        lines[number] = _INLINE_LINK.sub(lambda match: substitute(match, destinations), lines[number])
    return "\n".join(lines)


def code_lines(text):
    """Lines of fenced and indented code blocks, including fence markers."""
    lines = set()
    for token in PARSER.parse(text):
        if token.type in ("fence", "code_block"):
            lines.update(range(*token.map))
    return lines
