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


# Independent release versions, each read from the source that defines it. The
# C ABI line names the runtime library's SOVERSION; it is configured separately
# and is neither the product, the C API nor the format version.
VERSION_SOURCES = {
    "product_version": ("CMakeLists.txt", (r"\bVERSION\s+(\d+\.\d+\.\d+)",)),
    "language_version": ("language/ips/Version.hpp", (r"\bkLanguageVersionMajor\s*=\s*(\d+)\s*;",
                                                      r"\bkLanguageVersionMinor\s*=\s*(\d+)\s*;")),
    "format_version": ("runtime/iprop/Constants.hpp", (r"\bkVersionMajor\s*=\s*(\d+)\s*;",
                                                       r"\bkVersionMinor\s*=\s*(\d+)\s*;")),
    "c_api_version": ("backends/capi/iprop.h", (r"^#define IPROP_API_VERSION_MAJOR\s+(\d+)\b",
                                                r"^#define IPROP_API_VERSION_MINOR\s+(\d+)\b")),
    "c_abi_soversion": ("cmake/CAbi.cmake", (r"^set\(IPROP_C_ABI_VERSION_MAJOR\s+(\d+)\)",
                                             r"^set\(IPROP_C_ABI_VERSION_MINOR\s+(\d+)\)")),
}


def _source_version(root, key):
    file, patterns = VERSION_SOURCES[key]
    text = (root / file).read_text(encoding="utf-8")
    parts = [re.search(pattern, text, re.M) for pattern in patterns]
    if not all(parts):
        raise RuntimeError("Cannot read " + key + " from " + file)
    return ".".join(part.group(1) for part in parts)


def product_version(root=ROOT):
    return _source_version(root, "product_version")


def api_version(root=ROOT):
    return _source_version(root, "c_api_version")


def versions(root=ROOT):
    return {key: _source_version(root, key) for key in VERSION_SOURCES}


def read_version_matrix(value):
    """Typed flat version fields; unknown additive metadata remains untouched."""
    if type(value) is not dict:
        raise RuntimeError("Invalid version matrix: expected an object")
    result = {}
    for key in VERSION_SOURCES:
        pattern = r"[0-9]+\.[0-9]+\.[0-9]+" if key == "product_version" else r"[0-9]+\.[0-9]+"
        if type(value.get(key)) is not str or not re.fullmatch(pattern, value[key]):
            raise RuntimeError("Invalid version matrix: missing or malformed " + key)
        result[key] = value[key]
    return result


def match_versions(actual, expected):
    actual, expected = read_version_matrix(actual), read_version_matrix(expected)
    if actual != expected:
        raise RuntimeError("Version matrix differs: " + repr(actual) + " != " + repr(expected))
    return actual


def described_versions(language):
    """Release version names for a current `ipc --describe-language` export."""
    if type(language) is not dict:
        raise RuntimeError("Invalid compiled version matrix: expected an object")
    def pair(key):
        value = language.get(key)
        if type(value) is not dict or any(type(value.get(part)) not in {int, float} or
                not 0 <= value[part] <= 9007199254740991 or int(value[part]) != value[part]
                for part in ("major", "minor")):
            raise RuntimeError("Invalid version pair: " + key)
        return f"{int(value['major'])}.{int(value['minor'])}"
    abi = pair("abi_version")
    if "c_abi_soversion" in language and (type(language['c_abi_soversion']) is not str or language['c_abi_soversion'] != abi):
        raise RuntimeError("C ABI alias differs from abi_version")
    return read_version_matrix({"product_version": language.get("product_version"), "language_version": pair("language_version"),
            "format_version": pair("format_version"), "c_api_version": pair("c_api_version"),
            "c_abi_soversion": abi})


def handbook_version(language):
    return described_versions(language)["language_version"]


def source_conflict_copies(root=ROOT):
    """Identify untracked numbered siblings of tracked files, without deleting them."""
    def names(*options):
        output = subprocess.check_output(["git", "ls-files", "-z", *options], cwd=root)
        return {name for name in output.decode("utf-8", errors="surrogateescape").split("\0") if name}

    tracked = names()
    copies = []
    for name in names("--others", "--exclude-standard"):
        path = Path(name)
        match = re.fullmatch(r"(.+) ([2-9]|[1-9][0-9]+)(\.[^.]*)?", path.name)
        if match:
            canonical = path.with_name(match[1] + (match[3] or "")).as_posix()
            if canonical in tracked:
                copies.append(name)
    return sorted(copies)


def require_no_source_copies(root=ROOT):
    copies = source_conflict_copies(root)
    if copies:
        raise RuntimeError("Numbered source copies detected: " + ", ".join(copies) +
                           ". Preserve or remove these untracked copies outside the checkout "
                           "before release validation; packaging never deletes source files.")


def committed_source():
    require_no_source_copies()
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
    destination = Path(destination)
    if destination.is_symlink() or (destination.exists() and
            (not destination.is_dir() or any(destination.iterdir()))):
        raise RuntimeError("Archive extraction requires a new or empty directory: " + str(destination))
    with tarfile.open(archive_path, "r:gz") as archive:
        archive.extractall(destination, filter="data")


def write_checksums(destination, artifacts):
    destination.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in sorted(artifacts)),
                           encoding="utf-8")
