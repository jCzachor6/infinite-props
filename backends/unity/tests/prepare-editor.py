#!/usr/bin/env python3
"""Prepare an isolated real Unity project with the tested UPM native plugin."""
import argparse
import json
import re
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("project", type=Path)
parser.add_argument("--corpus", type=Path, default=Path("build/pixel-corpus"))
parser.add_argument("--fixtures", type=Path, default=Path("build/binding-fixtures"))
parser.add_argument("--color-space", choices=("Gamma", "Linear"), help="Change an already initialized Unity project before restarting the Editor")
parser.add_argument("--version", default="2022.3.62f1")
args = parser.parse_args()
project = args.project.resolve()
package = Path(__file__).resolve().parents[1] / "Packages/dev.infiniteprops.runtime"
if not (package / "Plugins").is_dir():
    raise SystemExit("Build the native Unity plugin with package-native.py first")
(project / "Assets").mkdir(parents=True, exist_ok=True)
(project / "ProjectSettings").mkdir(exist_ok=True)
(project / "Packages").mkdir(exist_ok=True)
shutil.copytree(package, project / "Packages/dev.infiniteprops.runtime", dirs_exist_ok=True)
(project / "Packages/manifest.json").write_text(json.dumps({"dependencies": {
    "dev.infiniteprops.runtime": "file:dev.infiniteprops.runtime", "com.unity.test-framework": "1.1.33",
    "com.unity.modules.imageconversion": "1.0.0", "com.unity.modules.jsonserialize": "1.0.0"
}, "testables": ["dev.infiniteprops.runtime"]}, indent=2) + "\n")
(project / "ProjectSettings/ProjectVersion.txt").write_text("m_EditorVersion: " + args.version + "\n")
for source in (args.corpus, args.fixtures):
    shutil.copytree(source, project / "PixelCorpus", dirs_exist_ok=True)
if args.color_space:
    settings = project / "ProjectSettings/ProjectSettings.asset"
    if not settings.is_file():
        raise SystemExit("Initialize this project in Unity once before selecting a color space")
    value = 1 if args.color_space == "Linear" else 0
    text, count = re.subn(r"(?m)^(  m_ActiveColorSpace:) [01]$", lambda match: match[1] + " " + str(value), settings.read_text())
    if count != 1:
        raise SystemExit("Expected exactly one Unity m_ActiveColorSpace setting")
    settings.write_text(text)
print(project)
