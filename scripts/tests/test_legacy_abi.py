"""Small falsification checks for the loader/identity oracle, not runtime mocks."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_legacy_abi as gate


class LoaderOracleTests(unittest.TestCase):
    def test_linux_uses_loaded_init_not_library_search_candidates(self):
        self.assertEqual(gate.observed_library("Linux", """
  12: trying file=/old/libipropc.so.0.4
  12: calling init: /replacement with spaces/libipropc.so.0.4
"""), ["/replacement with spaces/libipropc.so.0.4"])
        self.assertEqual(gate.observed_library("Linux", "trying file=/old/libipropc.so.0.4"), [])

    def test_macos_accepts_real_dyld_records_without_search_noise(self):
        self.assertEqual(gate.observed_library("Darwin", """
dyld[54]: <ABCD-1234> /private/tmp/new folder/libipropc.0.4.dylib
configured DYLD_LIBRARY_PATH=/old/libipropc.0.4.dylib
"""), ["/private/tmp/new folder/libipropc.0.4.dylib"])

    def test_successful_client_report_cannot_replace_loader_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            exe, library = root / "client", root / "libipropc.so.0.4"
            exe.write_bytes(b"same frozen binary")
            library.write_bytes(b"new library")
            with self.assertRaises(gate.GateFailure) as error:
                gate.validate_observation(exe, gate.digest(exe), [], gate.identity(library),
                                          0, json.dumps(gate.EXPECTED_CLIENT))
            self.assertEqual(error.exception.code, "loader_evidence")

    def test_wrong_path_and_same_path_wrong_hash_are_both_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            exe = root / "client"
            exe.write_bytes(b"same frozen binary")
            old, new = root / "old/libipropc.so.0.4", root / "new/libipropc.so.0.4"
            old.parent.mkdir(); new.parent.mkdir()
            # Even identical library bytes at a different path must not silently
            # masquerade as the replacement requested by this run.
            old.write_bytes(b"library"); new.write_bytes(b"library")
            expected = gate.identity(new)
            report = json.dumps(gate.EXPECTED_CLIENT)
            with self.assertRaises(gate.GateFailure) as error:
                gate.validate_observation(exe, gate.digest(exe), [str(old)], expected, 0, report)
            self.assertEqual(error.exception.code, "library_path")
            new.write_bytes(b"changed library")
            with self.assertRaises(gate.GateFailure) as error:
                gate.validate_observation(exe, gate.digest(exe), [str(new)], expected, 0, report)
            self.assertEqual(error.exception.code, "library_identity")

    def test_tampered_client_is_rejected_before_process_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "client"
            exe.write_bytes(b"original")
            sha = gate.digest(exe)
            exe.write_bytes(b"tampered")
            # Invalid following arguments prove no loader/process path is reached.
            with self.assertRaises(gate.GateFailure) as error:
                gate.observe(exe, sha, None, None, None, None, None)
            self.assertEqual(error.exception.code, "client_identity")

    def test_negative_control_does_not_accept_an_unrelated_failure(self):
        def unrelated():
            raise gate.GateFailure("client_exit", "crash")
        with self.assertRaises(gate.GateFailure) as error:
            gate.reject_control("library_path", unrelated)
        self.assertEqual(error.exception.code, "negative_control")


if __name__ == "__main__":
    unittest.main()
