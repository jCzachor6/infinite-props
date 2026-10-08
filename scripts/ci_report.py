"""Run a CI command without hiding its failure, retaining logs and JSON reports."""
import argparse
import datetime
import json
import os
import platform
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from release_common import match_versions, versions


ROOT = Path(__file__).resolve().parent.parent


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def output(command):
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=15, check=False)
        return result.stdout.strip() if result.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def environment(directory):
    path = directory / "environment.json"
    if not path.exists():
        write_json(path, {
            "source_commit": output(["git", "rev-parse", "HEAD"]),
            **versions(ROOT),
            "system": platform.system(), "architecture": platform.machine(),
            "python": platform.python_version(),
            "tools": {name: output([name, "--version"]) for name in ("cmake", "node", "c++")},
            "github": {key: os.environ.get("GITHUB_" + key.upper())
                       for key in ("run_id", "run_attempt", "job", "ref", "sha")},
        })
    value = json.loads(path.read_text(encoding="utf-8"))
    match_versions(value, versions(ROOT))
    return value


def run_command(directory, name, command):
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", name):
        raise ValueError("Use a simple, unique step name")
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        raise ValueError("Pass a command after the step name")
    record = {"name": name, "command": command,
              "started_at": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    started = time.monotonic()
    with (directory / (name + ".log")).open("w", encoding="utf-8") as log:
        try:
            with subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True,
                                  encoding="utf-8", errors="replace", bufsize=1) as process:
                for line in process.stdout:
                    sys.stdout.write(line)
                    sys.stdout.flush()
                    log.write(line)
                    log.flush()
                status = process.wait()
        except OSError as error:
            log.write(str(error) + "\n")
            print(error, file=sys.stderr)
            status = 127
    record.update(returncode=status, passed=status == 0,
                  duration_seconds=round(time.monotonic() - started, 3))
    write_json(directory / (name + ".step.json"), record)
    return status if status >= 0 else 128 - status


def summarize(directory):
    steps = [json.loads(path.read_text(encoding="utf-8"))
             for path in sorted(directory.glob("*.step.json"))]
    tests, errors = [], []
    for path in sorted(directory.glob("*.xml")):
        try:
            tree = ET.parse(path)
            cases = list(tree.iter("testcase"))
            tests.append({"file": path.name, "tests": len(cases),
                          "failures": sum(case.find("failure") is not None or
                                          case.find("error") is not None for case in cases),
                          "skipped": sum(case.find("skipped") is not None for case in cases)})
        except (OSError, ET.ParseError) as error:
            errors.append(path.name + ": " + str(error))
    result = {**environment(directory), "steps": steps, "test_reports": tests,
              "report_errors": errors,
              "passed": bool(steps) and all(step["passed"] for step in steps)
                        and not errors and all(report["failures"] == 0 for report in tests)}
    write_json(directory / "summary.json", result)
    lines = ["### CI report", "", "Commit: `" + str(result["source_commit"]) + "`", "",
             "| Step | Result | Seconds |", "| --- | --- | ---: |"]
    lines.extend(f"| {step['name']} | {'PASS' if step['passed'] else 'FAIL'} | {step['duration_seconds']} |"
                 for step in steps)
    lines.extend(["", f"JUnit: {sum(report['tests'] for report in tests)} tests, "
                  f"{sum(report['failures'] for report in tests)} failures.",
                  "Logs, JUnit and JSON are retained in this job's reports artifact."])
    lines.extend("Report error: " + error for error in errors)
    markdown = "\n".join(lines) + "\n"
    (directory / "summary.md").write_text(markdown, encoding="utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
            summary.write(markdown)
    print(markdown)
    return 0 if result["passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report-dir", type=Path, default=ROOT / "build/ci-reports")
    actions = parser.add_subparsers(dest="action", required=True)
    runner = actions.add_parser("run")
    runner.add_argument("name")
    runner.add_argument("command", nargs=argparse.REMAINDER)
    actions.add_parser("summary")
    args = parser.parse_args()
    args.report_dir.mkdir(parents=True, exist_ok=True)
    environment(args.report_dir)
    return run_command(args.report_dir, args.name, args.command) if args.action == "run" else summarize(args.report_dir)


if __name__ == "__main__":
    sys.exit(main())
