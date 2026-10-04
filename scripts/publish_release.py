"""Upload verified release assets to a draft, then publish after readback."""
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from release_assets import require, verify_bundle
from release_common import ROOT, committed_source, sha256
from release_preflight import metadata, validate_tag


class GitHub:
    def __init__(self, repository, token):
        require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository) is not None, "Invalid repository")
        self.base = os.environ.get("GITHUB_API_URL", "https://api.github.com")
        self.prefix = "/repos/" + repository
        self.token = token

    def request(self, method, path, data=None, binary=False):
        url = path if path.startswith("https://") else self.base + self.prefix + path
        headers = {"Authorization": "Bearer " + self.token,
                   "Accept": "application/vnd.github+json", "User-Agent": "iprop-release",
                   "X-GitHub-Api-Version": "2022-11-28"}
        if data is not None:
            headers["Content-Type"] = "application/octet-stream" if binary else "application/json"
            if not binary:
                data = json.dumps(data).encode("utf-8")
        with urllib.request.urlopen(urllib.request.Request(url, method=method, data=data, headers=headers), timeout=90) as response:
            return json.load(response) if response.status != 204 else None

    def tag_commit(self, tag):
        object_ = self.request("GET", "/git/ref/tags/" + urllib.parse.quote(tag, safe=""))["object"]
        for _ in range(10):
            if object_["type"] == "commit":
                return object_["sha"]
            require(object_["type"] == "tag", "Release tag must resolve to a commit")
            object_ = self.request("GET", "/git/tags/" + object_["sha"])["object"]
        raise RuntimeError("Too many nested annotated tags")

    def release(self, tag):
        try:
            return self.request("GET", "/releases/tags/" + urllib.parse.quote(tag, safe=""))
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
            # A draft may not appear in the by-tag endpoint. Find it explicitly.
            for page in range(1, 101):
                releases = self.request("GET", f"/releases?per_page=100&page={page}")
                for release in releases:
                    if release["tag_name"] == tag:
                        return release
                if len(releases) < 100:
                    return None
            raise RuntimeError("Too many releases to locate an existing draft")

    def assets(self, release):
        result = []
        for page in range(1, 101):
            assets = self.request("GET", f"/releases/{release['id']}/assets?per_page=100&page={page}")
            result.extend(assets)
            if len(assets) < 100:
                return result
        raise RuntimeError("Too many release assets")

    def create(self, tag, commit, notes):
        return self.request("POST", "/releases", {"tag_name": tag, "target_commitish": commit,
                                                  "name": tag, "body": notes, "draft": True, "prerelease": False})

    def delete_asset(self, asset):
        self.request("DELETE", "/releases/assets/" + str(asset["id"]))

    def upload(self, release, file):
        url = release["upload_url"].split("{", 1)[0] + "?" + urllib.parse.urlencode({"name": file.name})
        return self.request("POST", url, file.read_bytes(), binary=True)

    def finish(self, release, tag, notes):
        return self.request("PATCH", "/releases/" + str(release["id"]),
                            {"draft": False, "prerelease": False, "name": tag, "body": notes, "make_latest": "true"})


def publish(api, tag, commit, notes, files):
    expected = {file.name: ("sha256:" + sha256(file), file.stat().st_size) for file in files}
    require(api.tag_commit(tag) == commit, "Remote release tag moved or identifies a different commit")
    release = api.release(tag)
    if release is None:
        release = api.create(tag, commit, notes)
    existing = {asset["name"]: asset for asset in api.assets(release)}
    require(set(existing) <= set(expected), "Existing release contains unverified assets")

    def matches(asset, name):
        return asset.get("state") == "uploaded" and (asset.get("digest"), asset.get("size")) == expected[name]

    if not release["draft"]:
        require(set(existing) == set(expected) and all(matches(asset, name) for name, asset in existing.items()),
                "Published release differs; published assets will not be replaced")
        return release
    for file in files:
        asset = existing.get(file.name)
        if asset is not None and matches(asset, file.name):
            continue
        if asset is not None:
            api.delete_asset(asset)
        uploaded = api.upload(release, file)
        require(matches(uploaded, file.name), "GitHub upload digest/size differs: " + file.name)
    uploaded = {asset["name"]: asset for asset in api.assets(release)}
    require(set(uploaded) == set(expected) and all(matches(asset, name) for name, asset in uploaded.items()),
            "Release asset readback is incomplete or changed")
    require(api.tag_commit(tag) == commit, "Remote release tag moved during upload")
    api.finish(release, tag, notes)
    published = api.release(tag)
    require(published is not None and published["draft"] is False, "Release publication did not complete")
    published_assets = {asset["name"]: asset for asset in api.assets(published)}
    require(set(published_assets) == set(expected) and
            all(matches(asset, name) for name, asset in published_assets.items()),
            "Published release asset readback differs")
    return published


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", default=os.environ.get("RELEASE_TAG"))
    parser.add_argument("--commit", default=os.environ.get("RELEASE_COMMIT"))
    parser.add_argument("--assets", type=Path, default=ROOT / "build/release")
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    commit = committed_source()
    require(args.tag is not None and args.commit == commit, "Require the exact release tag and source commit")
    release = metadata(ROOT, args.tag, publishing=True)
    validate_tag(ROOT, args.tag, "origin/main")
    files = verify_bundle(args.assets, release["product_version"], commit)
    if args.dry_run:
        print("Verified publication plan: " + args.tag + " with " + str(len(files)) + " assets")
        return
    token = os.environ.get("GITHUB_TOKEN")
    require(bool(token) and bool(args.repository), "Set GITHUB_TOKEN and GITHUB_REPOSITORY")
    result = publish(GitHub(args.repository, token), args.tag, commit, release["notes"], files)
    print("Published and verified " + result["html_url"])


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print("release publication: " + str(error), file=sys.stderr)
        sys.exit(1)
