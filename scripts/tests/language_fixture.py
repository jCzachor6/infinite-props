"""Small authored handbook for documentation and release gate tests."""

LANGUAGE = {"schema_version": 1, "product_version": "0.4.0", "builtins": [
    {"name": "unit", "overloads": [{"parameters": [], "result": "f32", "evaluation_order": []}]}],
    "call_rules": {"helper_evaluation_order": "declaration", "part": {
        "parameters": [{"name": "seed", "required": True, "positional": True},
                       {"name": "v", "required": True, "positional": True},
                       {"name": "a", "required": True, "positional": True},
                       {"name": "at", "required": True, "positional": False},
                       {"name": "rot", "required": False, "positional": False, "default": 0},
                       {"name": "mirror_x", "required": False, "positional": False, "default": 0},
                       {"name": "mirror_y", "required": False, "positional": False, "default": 0},
                       {"name": "$uniform", "required": False, "positional": False, "default": "input_declaration"}],
        "evaluation_order": ["$uniform", "seed", "v", "a", "at", "rot", "mirror_x", "mirror_y"]}},
    "emoji_aliases": [{"word": "asset", "codepoint": 128230}]}

# Each release version lives in its own source and changes independently.
VERSION_FILES = {
    "language/ips/Version.hpp": "inline constexpr unsigned kLanguageVersionMajor=0;\n"
                                "inline constexpr unsigned kLanguageVersionMinor=4;\n",
    "runtime/iprop/Constants.hpp": "inline constexpr u16 kVersionMajor = 0;\ninline constexpr u16 kVersionMinor = 4;\n",
    "cmake/CAbi.cmake": "set(IPROP_C_ABI_VERSION_MAJOR 0)\nset(IPROP_C_ABI_VERSION_MINOR 4)\n",
}


def fixture(root):
    for name, content in {
        "CMakeLists.txt": "project(iprop VERSION 0.4.0)\n",
        "docs/language/book.json": '{"title":"Book","chapters":["index.md"]}',
        "docs/language/index.md": "# Book\n\n## unit\n<!-- builtin: unit -->\n\nOne deterministic stream draw.\n\n<!-- example: ../../examples/language/one.ips -->\n",
        "examples/language/cases.json": '{"cases":[{"file":"one.ips","expect":"pass"}]}',
        "examples/language/one.ips": "asset A { size 1x1 draw { x = unit() } }\n",
        "examples/recipes/README.md": "# Recipes\n",
    }.items():
        file = root / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(content.encode("utf-8"))
