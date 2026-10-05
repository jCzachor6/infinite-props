"""Shared version, archive and checksum operations for release packages."""
import gzip
import hashlib
import json
import re
import subprocess
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def run(*args, capture=False):
    result = subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True,
                            text=True, encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def product_version():
    return re.search(r"\bVERSION\s+(\d+\.\d+\.\d+)",
                     (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")).group(1)


def api_version(root=ROOT):
    header = (root / "backends/capi/iprop.h").read_text(encoding="utf-8")
    return ".".join(re.search(rf"#define IPROP_API_VERSION_{part}\s+(\d+)", header).group(1)
                    for part in ("MAJOR", "MINOR"))


def committed_source():
    if run("git", "status", "--porcelain", capture=True):
        raise RuntimeError("Commit the source changes before packaging the HEAD source archive")
    return run("git", "rev-parse", "HEAD", capture=True)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_archive(directory, destination):
    # Normalize container metadata; this does not promise that different
    # compilers or platforms produce byte-identical executable files.
    epoch = int(run("git", "show", "-s", "--format=%ct", "HEAD", capture=True))

    def normalize(member):
        member.uid = member.gid = 0
        member.uname = member.gname = ""
        member.mtime = epoch
        member.pax_headers = {}
        return member

    with destination.open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                archive.add(directory, arcname=directory.name, filter=normalize)


def extract_archive(archive_path, destination):
    with tarfile.open(archive_path, "r:gz") as archive:
        archive.extractall(destination, filter="data")


def write_checksums(destination, artifacts):
    destination.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in sorted(artifacts)),
                           encoding="utf-8")
