#!/usr/bin/env python3
"""Verify registry bytes against the immutable OCI index and amd64 child digest."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import urllib.request

from reproducible_build import IMAGE


def fetch(url, headers=None):
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers or {}), timeout=60) as response:
        return response.read(), response.headers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    token_bytes, _ = fetch("https://auth.docker.io/token?service=registry.docker.io&scope=repository:espressif/idf:pull")
    token = json.loads(token_bytes)["token"]
    headers = {"Authorization": "Bearer " + token,
               "Accept": "application/vnd.oci.image.index.v1+json,application/vnd.oci.image.manifest.v1+json"}
    expected = IMAGE.split("@", 1)[1]
    data, response_headers = fetch("https://registry-1.docker.io/v2/espressif/idf/manifests/" + expected, headers)
    actual = "sha256:" + hashlib.sha256(data).hexdigest()
    if actual != expected or response_headers.get("Docker-Content-Digest") != expected:
        raise ValueError("OCI index byte digest/header does not match pinned digest")
    index = json.loads(data)
    children = [m for m in index["manifests"] if m.get("platform") == {"architecture": "amd64", "os": "linux"}]
    if len(children) != 1:
        raise ValueError("expected exactly one linux/amd64 child manifest")
    child = children[0]["digest"]
    child_bytes, child_headers = fetch("https://registry-1.docker.io/v2/espressif/idf/manifests/" + child, headers)
    if "sha256:" + hashlib.sha256(child_bytes).hexdigest() != child or child_headers.get("Docker-Content-Digest") != child:
        raise ValueError("linux/amd64 child manifest digest mismatch")
    (args.output / "oci-index.json").write_bytes(data)
    (args.output / "oci-amd64-manifest.json").write_bytes(child_bytes)
    report = {"image": IMAGE, "verified_index_digest": actual, "verified_linux_amd64_digest": child,
              "utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "method": "SHA-256 of exact registry response bytes, fetched by digest; no tag resolution"}
    (args.output / "image-verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
