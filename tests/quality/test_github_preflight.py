# SPDX-License-Identifier: MPL-2.0
import io
import json
from pathlib import Path
import secrets
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/release"))
import github_preflight as preflight


class GithubPreflightTests(unittest.TestCase):
    class JsonResponse(io.BytesIO):
        def __init__(self, payload, headers=None):
            super().__init__(json.dumps(payload).encode())
            self.headers = headers or {}

    def test_write_access_advertisement_and_denial(self):
        with patch.dict("os.environ", {"GH_TOKEN": secrets.token_hex(16)}):
            for payload in (b"001f# service=git-receive-pack\n", b"not a Git advertisement"):
                with patch.object(preflight, "urlopen", return_value=io.BytesIO(payload)):
                    if payload.startswith(b"001f"):
                        preflight.contents_write_access()
                    else:
                        with self.assertRaisesRegex(ValueError, "advertisement is invalid"):
                            preflight.contents_write_access()
            for code in (401, 403, 404, 500):
                with patch.object(preflight, "urlopen", side_effect=HTTPError("fixture", code, "", {}, None)):
                    with self.assertRaisesRegex(ValueError, f"HTTP {code}"):
                        preflight.contents_write_access()

    def test_bearer_syntax_without_header_injection(self):
        for token in ("ghs_fixture", "header.payload.signature", "base64+/value=="):
            with patch.object(preflight.os, "environ", {"GH_TOKEN": token}), \
                    patch.object(preflight, "urlopen", return_value=io.BytesIO(b'{}')):
                self.assertEqual(preflight.api_get("fixture"), {})
        for token in ("", "token\nInjected: header", "token\rvalue", "two tokens", "token\x00"):
            with patch.object(preflight.os, "environ", {"GH_TOKEN": token}), \
                    patch.object(preflight, "urlopen") as request:
                with self.assertRaisesRegex(ValueError, "missing or malformed"):
                    preflight.api_get("fixture")
                request.assert_not_called()

    def test_only_explicit_404_means_absent(self):
        for code in (401, 403, 404, 429, 500):
            error = HTTPError("https://api.github.com/", code, "fixture", {}, None)
            with patch.dict("os.environ", {"GH_TOKEN": secrets.token_hex(16)}), \
                    patch.object(preflight, "urlopen", side_effect=error):
                if code == 404:
                    self.assertIsNone(preflight.api_get("fixture", allow_missing=True))
                else:
                    with self.assertRaisesRegex(ValueError, f"HTTP {code}"):
                        preflight.api_get("fixture", allow_missing=True)
                with self.assertRaises(ValueError):
                    preflight.api_get("fixture")

    def test_valid_json_and_network_failure(self):
        with patch.dict("os.environ", {"GH_TOKEN": secrets.token_hex(16)}):
            with patch.object(preflight, "urlopen", return_value=io.BytesIO(b'{"id": 1}')):
                self.assertEqual(preflight.api_get("fixture"), {"id": 1})
            with patch.object(preflight, "urlopen", side_effect=URLError("network unavailable")):
                with self.assertRaises(URLError):
                    preflight.api_get("fixture", allow_missing=True)

    def test_draft_and_promotion_states(self):
        draft = {"id": 1, "tag_name": "v1.0.0", "draft": True, "prerelease": False, "assets": []}
        self.assertTrue(preflight.release_state(draft, "v1.0.0", "draft"))
        self.assertFalse(preflight.release_state(None, "v1.0.0-test", "draft"))
        for release, tag, stage in ((None, "v1.0.0", "draft"), (None, "v1.0.0-test", "upload"),
                                    ({**draft, "draft": False}, "v1.0.0", "draft"),
                                    ({**draft, "assets": [1]}, "v1.0.0", "upload"),
                                    (draft, "v1.0.1", "draft"), (draft, "v1.0.0", "promote")):
            with self.subTest(stage=stage), self.assertRaises(ValueError):
                preflight.release_state(release, tag, stage)
        public = {**draft, "draft": False, "prerelease": True}
        self.assertTrue(preflight.release_state(public, "v1.0.0", "promote"))
        public["tag_name"] = "v1.0.0-test"
        with self.assertRaisesRegex(ValueError, "cannot be promoted"):
            preflight.release_state(public, "v1.0.0-test", "promote")

    def test_exact_asset_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "asset").write_bytes(b"payload")
            asset = {"name": "asset", "size": 7, "state": "uploaded",
                     "digest": "sha256:" + preflight.digest(b"payload")}
            preflight.asset_identity({"assets": [asset]}, root)
            for field, value in (("name", "wrong"), ("size", 8), ("state", "new"), ("digest", "wrong")):
                with self.subTest(field=field), self.assertRaises(ValueError):
                    preflight.asset_identity({"assets": [{**asset, field: value}]}, root)

    def test_draft_release_lookup_falls_back_to_paginated_list(self):
        tag = "v1.0.0-test"
        release = {"id": 1, "tag_name": tag, "draft": True, "prerelease": True, "assets": []}
        first_page = [{"id": 2, "tag_name": "v0.9.0-test"}]
        second_page = [release]
        not_found = HTTPError("fixture", 404, "draft releases are omitted", {}, None)
        with patch.dict("os.environ", {"GH_TOKEN": secrets.token_hex(16)}), \
                patch.object(preflight, "urlopen", side_effect=[
                    not_found,
                    self.JsonResponse(first_page, {"Link": '<https://api.github.com/repos/metaneutrons/BFS/releases?page=2>; rel="next"'}),
                    self.JsonResponse(second_page),
                ]):
            self.assertEqual(preflight.release_for_tag(tag), release)

    def test_release_lookup_rejects_missing_non_draft_release(self):
        not_found = HTTPError("fixture", 404, "missing", {}, None)
        with patch.dict("os.environ", {"GH_TOKEN": secrets.token_hex(16)}), \
                patch.object(preflight, "urlopen", side_effect=[not_found, self.JsonResponse([])]):
            with self.assertRaisesRegex(ValueError, "release for tag .* is missing"):
                preflight.release_for_tag("v1.0.0-test")


if __name__ == "__main__":
    unittest.main()
