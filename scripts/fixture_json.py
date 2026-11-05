"""Compact JSON layout for test fixtures: a container stays on one line unless
it is longer than `limit`, so each record, vector or mutation reads as a row."""
import json


def dumps(value, limit=200, indent=0):
    text = json.dumps(value, ensure_ascii=False, separators=(", ", ": "))
    if (len(text) + 2 * indent <= limit or not isinstance(value, (dict, list)) or not value
            or isinstance(value, list) and not any(isinstance(item, (dict, list)) for item in value)):
        return text
    pad, close = "  " * (indent + 1), "\n" + "  " * indent
    if isinstance(value, dict):
        rows = (pad + json.dumps(key, ensure_ascii=False) + ": " + dumps(item, limit, indent + 1)
                for key, item in value.items())
        return "{\n" + ",\n".join(rows) + close + "}"
    return "[\n" + ",\n".join(pad + dumps(item, limit, indent + 1) for item in value) + close + "]"
