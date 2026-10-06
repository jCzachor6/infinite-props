"""Release containers preserve files/links and cannot escape their destination."""
import io
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release_common import (extract_archive, require_no_source_copies, sha256,
                            source_conflict_copies, write_archive, write_checksums)


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

    def test_repeated_extraction_never_merges_or_overwrites_existing_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            prefix = directory / "sdk"
            prefix.mkdir()
            (prefix / "source.md").write_bytes(b"packaged bytes")
            archive = directory / "sdk.tar.gz"
            write_archive(prefix, archive)
            destination = directory / "extracted"
            extract_archive(archive, destination)
            source = destination / "sdk/source.md"
            source.write_bytes(b"local edits")
            with self.assertRaisesRegex(RuntimeError, "new or empty directory"):
                extract_archive(archive, destination)
            self.assertEqual(source.read_bytes(), b"local edits")
            self.assertEqual(sorted(p.relative_to(destination).as_posix()
                                    for p in destination.rglob("*") if p.is_file()), ["sdk/source.md"])

    def test_extraction_rejects_files_and_symlink_destinations(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            prefix = directory / "sdk"
            prefix.mkdir()
            (prefix / "file").write_bytes(b"package")
            archive = directory / "sdk.tar.gz"
            write_archive(prefix, archive)
            destination = directory / "existing"
            destination.write_bytes(b"preserve")
            with self.assertRaisesRegex(RuntimeError, "new or empty directory"):
                extract_archive(archive, destination)
            self.assertEqual(destination.read_bytes(), b"preserve")
            if os.name != "nt":
                empty = directory / "empty"
                empty.mkdir()
                link = directory / "link"
                link.symlink_to(empty, target_is_directory=True)
                with self.assertRaisesRegex(RuntimeError, "new or empty directory"):
                    extract_archive(archive, link)
                self.assertEqual(list(empty.iterdir()), [])

    def test_numbered_source_copies_are_reported_without_ignoring_or_deleting_edits(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            (root / "docs").mkdir()
            for name in ("composition.md", "chapter 2.md", "LICENSE"):
                (root / "docs" / name).write_bytes(b"tracked")
            subprocess.run(["git", "add", "docs"], cwd=root, check=True)
            copies = ["docs/composition 2.md", "docs/composition 12.md", "docs/LICENSE 2"]
            for name in [*copies, "docs/unrelated 2.md", "docs/composition 1.md"]:
                (root / name).write_bytes(b"separate edits")
            self.assertEqual(source_conflict_copies(root), sorted(copies))
            with self.assertRaisesRegex(RuntimeError, "Numbered source copies detected"):
                require_no_source_copies(root)
            self.assertTrue(all((root / name).read_bytes() == b"separate edits" for name in copies))
            self.assertTrue((root / "docs/chapter 2.md").exists())
            for name in copies:
                (root / name).unlink()
            self.assertEqual(source_conflict_copies(root), [])
            require_no_source_copies(root)

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
