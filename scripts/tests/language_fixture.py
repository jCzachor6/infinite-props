"""Small authored handbook for documentation and release gate tests."""
import json
from pathlib import Path
from unittest.mock import patch

# A current compiler export, reduced to the one builtin the handbook describes.
LANGUAGE = json.loads(Path(__file__).with_name("language.json").read_text(encoding="utf-8"))

# Each release version lives in its own source and changes independently.
VERSION_FILES = {
    "language/ips/Version.hpp": "inline constexpr unsigned kLanguageVersionMajor=0;\n"
                                "inline constexpr unsigned kLanguageVersionMinor=5;\n",
    "runtime/iprop/Constants.hpp": "inline constexpr u16 kVersionMajor = 0;\ninline constexpr u16 kVersionMinor = 4;\n",
    "backends/capi/iprop.h": "#define IPROP_API_VERSION_MAJOR 0\n#define IPROP_API_VERSION_MINOR 3\n",
    "cmake/CAbi.cmake": "set(IPROP_C_ABI_VERSION_MAJOR 0)\nset(IPROP_C_ABI_VERSION_MINOR 4)\n",
}


def schema(version):
    return json.dumps({"$schema": "https://json-schema.org/draft/2020-12/schema",
                       "properties": {"schema_version": {"type": "integer", "const": version}}})


def reference_stub():
    """The real LLM reference renderer runs the compiler; handbook tests need none."""
    return patch("llm_reference.render", return_value=("# LLM reference\n", []))


def fixture(root):
    for name, content in {
        "CMakeLists.txt": "project(iprop VERSION 0.4.0)\n",
        **VERSION_FILES,
        "docs/language/book.json": '{"title":"Book","chapters":["index.md"]}',
        "docs/language/index.md": "# Book\n\n## unit\n<!-- builtin: unit -->\n\nOne deterministic stream draw.\n\n<!-- example: ../../examples/language/one.ips -->\n",
        "docs/language-description.schema.json": schema(2),
        "docs/compiler-diagnostics.schema.json": schema(1),
        "docs/documentation-manifest.schema.json": schema(2),
        "docs/llm-reference.json": "{}\n",
        "examples/language/cases.json": '{"cases":[{"file":"one.ips","expect":"pass"}]}',
        "examples/language/one.ips": "asset A { size 1x1 draw { x = unit() } }\n",
        "examples/recipes/README.md": "# Recipes\n",
    }.items():
        file = root / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(content.encode("utf-8"))
