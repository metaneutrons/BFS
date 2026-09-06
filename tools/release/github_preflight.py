#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Read-only GitHub checks before mutating a BFS release or release pull request."""

import base64
import json
import os
from pathlib import Path
import re
import sys
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen

from release_integrity import digest, package_name, require

REPOSITORY = "metaneutrons/BFS"


def api_get(path, allow_missing=False):
    token = os.environ.get("GH_TOKEN", "")
    # RFC 6750 bearer syntax also accepts the current JWT-shaped installation tokens.
    require(re.fullmatch(r"[A-Za-z0-9._~+/-]+=*", token), "GitHub token is missing or malformed")
    request = Request("https://api.github.com/" + path, headers={
        "Authorization": "Bearer " + token, "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "BFS-release-preflight",
    })
    try:
        with urlopen(request, timeout=30) as response:  # nosec B310 - fixed HTTPS GitHub API
            return json.load(response)
    except HTTPError as error:
        if error.code == 404 and allow_missing:
            return None
        # Never echo a request, headers or API error body that could contain credentials.
        raise ValueError(f"GitHub preflight failed with HTTP {error.code}") from None


def contents_write_access():
    # GET advertises receive-pack only; it neither uploads objects nor changes refs.
    # User-centric repository.permissions is always false for installation tokens.
    credential = base64.b64encode(("x-access-token:" + os.environ["GH_TOKEN"]).encode()).decode()
    request = Request(f"https://github.com/{REPOSITORY}.git/info/refs?service=git-receive-pack",
                      headers={"Authorization": "Basic " + credential})
    try:
        with urlopen(request, timeout=30) as response:  # nosec B310 - fixed HTTPS GitHub endpoint
            require(response.read(31) == b"001f# service=git-receive-pack\n",
                    "repository write-access advertisement is invalid")
    except HTTPError as error:
        raise ValueError(f"Repository write-access preflight failed with HTTP {error.code}") from None


def repository_access(write=True):
    require(os.environ.get("GITHUB_REPOSITORY") == REPOSITORY, "unexpected repository identity")
    repository = api_get(f"repos/{REPOSITORY}")
    require(repository.get("full_name") == REPOSITORY and repository.get("archived") is False,
            "repository identity or archived state differs")
    if write:
        contents_write_access()
    return repository


def tag_commit(tag):
    package_name(tag)
    ref = api_get(f"repos/{REPOSITORY}/git/ref/tags/{quote(tag, safe='')}")
    obj = ref.get("object", {})
    for _ in range(4):
        sha = obj.get("sha", "")
        require(re.fullmatch(r"[0-9a-f]{40}", sha), "invalid tag object identity")
        if obj.get("type") == "commit":
            return sha
        require(obj.get("type") == "tag", "tag does not reference a commit")
        obj = api_get(f"repos/{REPOSITORY}/git/tags/{sha}").get("object", {})
    raise ValueError("tag indirection exceeds limit")


def release_state(release, tag, stage):
    require(stage in ("draft", "upload", "publish", "promote", "public", "stable"), "invalid stage")
    if release is None:
        require(stage == "draft" and "-" in tag, "only qualification tags may lack a draft")
        return False
    require(release.get("tag_name") == tag and type(release.get("id")) is int,
            "release identity differs")
    expected_draft = stage in ("draft", "upload", "publish")
    require(release.get("draft") is expected_draft, "unexpected release draft state")
    if not expected_draft:
        require(release.get("prerelease") is (stage != "stable"), "unexpected release prerelease state")
    if stage in ("promote", "stable"):
        require("-" not in tag, "qualification releases cannot be promoted")
    if stage in ("draft", "upload"):
        require(release.get("assets") == [], "draft already has release assets")
    return True


def asset_identity(release, directory):
    files = {path.name: path for path in directory.iterdir()}
    require(files and all(path.is_file() and not path.is_symlink() for path in files.values()),
            "candidate must contain regular files")
    assets = release.get("assets", [])
    require(len(assets) == len(files) and {asset.get("name") for asset in assets} == set(files),
            "published asset inventory differs")
    for asset in assets:
        path = files[asset["name"]]
        require(asset.get("state") == "uploaded" and asset.get("size") == path.stat().st_size,
                "published asset size or upload state differs")
        require(asset.get("digest") == "sha256:" + digest(path.read_bytes()), "published asset digest differs")


def main():
    require(len(sys.argv) >= 2, "preflight mode is required")
    stage = sys.argv[1]
    repository = repository_access(write=stage != "dispatch")
    if stage == "release-please":
        require(len(sys.argv) == 2 and repository.get("default_branch") == "main", "unexpected release branch")
        installation = api_get("installation/repositories?per_page=100")
        require(installation.get("total_count") == 1 and
                [item.get("full_name") for item in installation.get("repositories", [])] == [REPOSITORY],
                "release app token is not scoped to this repository only")
        return
    require(len(sys.argv) in (3, 4), "usage: github_preflight.py STAGE TAG [CANDIDATE]")
    tag = sys.argv[2]
    commit = tag_commit(tag)
    if stage == "dispatch":
        workflow = api_get(f"repos/{REPOSITORY}/actions/workflows/release.yml")
        require(workflow.get("state") == "active" and workflow.get("path") == ".github/workflows/release.yml",
                "release workflow is absent or disabled")
        content = api_get(f"repos/{REPOSITORY}/contents/.github/workflows/release.yml?ref={quote(tag, safe='')}")
        require(content.get("type") == "file", "release workflow is missing from the tag")
        return
    require(commit == os.environ.get("GITHUB_SHA"), "tag does not identify this release run")
    release = api_get(f"repos/{REPOSITORY}/releases/tags/{quote(tag, safe='')}", allow_missing=stage == "draft")
    exists = release_state(release, tag, stage)
    if stage not in ("draft", "upload"):
        require(len(sys.argv) == 4, "candidate directory is required")
        asset_identity(release, Path(sys.argv[3]))
    print("exists=true" if exists else "exists=false")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        sys.exit(f"ERROR: {error}")
