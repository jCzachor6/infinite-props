"""Release containers preserve files/links and cannot escape their destination."""
import io
import os
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release_common import extract_archive, sha256, write_archive, write_checksums


class ArchiveTests(unittest.TestCase):
    def test_sdk_archive_retains_contents_executable_permissions_and_relative_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            prefix = directory / "sdk"
            (prefix / "bin").mkdir(parents=True)
            tool = prefix / "bin/tool"
            tool.write_bytes(b"#!/bin/sh\nexit 0\n")
            tool.chmod(0o755)
            if os.name != "nt":
                (prefix / "bin/alias").symlink_to("tool")
            archive = directory / "sdk.tar.gz"
            write_archive(prefix, archive)
            extract_archive(archive, directory / "relocated")
            extracted = directory / "relocated/sdk/bin/tool"
            self.assertEqual(extracted.read_bytes(), tool.read_bytes())
            if os.name != "nt":
                self.assertTrue(extracted.stat().st_mode & 0o111)
                self.assertTrue((extracted.parent / "alias").is_symlink())
                self.assertEqual((extracted.parent / "alias").read_bytes(), tool.read_bytes())

    def test_build_timestamps_do_not_change_the_container_digest(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            prefix = directory / "sdk"
            prefix.mkdir()
            file = prefix / "file"
            file.write_bytes(b"same tested bytes")
            first, second = directory / "first.tar.gz", directory / "second.tar.gz"
            write_archive(prefix, first)
            os.utime(file, (1, 1))
            os.utime(prefix, (2, 2))
            write_archive(prefix, second)
            self.assertEqual(sha256(first), sha256(second))

    def test_archive_cannot_write_outside_extraction_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            archive = directory / "invalid.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                member = tarfile.TarInfo("../escaped")
                member.size = 4
                tar.addfile(member, io.BytesIO(b"oops"))
            with self.assertRaises(tarfile.FilterError):
                extract_archive(archive, directory / "extracted")
            self.assertFalse((directory / "escaped").exists())

    def test_checksum_list_is_limited_to_the_supplied_artifacts(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            current, stale = directory / "current.tar.gz", directory / "stale.tar.gz"
            current.write_bytes(b"current"); stale.write_bytes(b"stale")
            checksums = directory / "SHA256SUMS"
            write_checksums(checksums, [current])
            self.assertEqual(checksums.read_text(), f"{sha256(current)}  current.tar.gz\n")


if __name__ == "__main__":
    unittest.main()
