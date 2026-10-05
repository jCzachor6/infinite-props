"""Failure paths must remain failures when CI diagnostics are collected."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "ci_report.py"


class ReportsTest(unittest.TestCase):
    def invoke(self, directory, *args):
        return subprocess.run([sys.executable, SCRIPT, "--report-dir", directory, *args],
                              capture_output=True, text=True, encoding="utf-8", check=False)

    def test_failure_exit_code_output_and_summary_survive(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = self.invoke(directory, "run", "failure", sys.executable, "-c",
                                 "import sys; print('zażółć'); print('stderr', file=sys.stderr); sys.exit(17)")
            self.assertEqual(result.returncode, 17)
            log = (directory / "failure.log").read_text(encoding="utf-8")
            self.assertIn("zażółć", log)
            self.assertIn("stderr", log)
            self.assertEqual(self.invoke(directory, "summary").returncode, 1)
            summary = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
            self.assertFalse(summary["passed"])
            self.assertEqual(summary["steps"][0]["returncode"], 17)
            self.assertRegex(summary["source_commit"], r"^[0-9a-f]{40}$")

    def test_missing_command_has_a_retained_failure_record(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.assertEqual(self.invoke(directory, "run", "missing", "iprop-missing-ci-command").returncode, 127)
            self.assertEqual(self.invoke(directory, "summary").returncode, 1)
            self.assertTrue((directory / "missing.step.json").is_file())

    def test_failed_and_malformed_junit_cannot_produce_a_pass(self):
        for xml in ("<testsuite><testcase><failure>oops</failure></testcase></testsuite>", "<broken"):
            with self.subTest(xml=xml), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                self.assertEqual(self.invoke(directory, "run", "success", sys.executable, "-c", "pass").returncode, 0)
                (directory / "test.xml").write_text(xml, encoding="utf-8")
                self.assertEqual(self.invoke(directory, "summary").returncode, 1)


if __name__ == "__main__":
    unittest.main()
