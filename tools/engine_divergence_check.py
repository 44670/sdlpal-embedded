#!/usr/bin/env python3
"""Check that protected SDLPAL engine files have not drifted silently."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict) or not isinstance(data.get("files"), list):
        raise ValueError("manifest must be an object with a files array")
    return data


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    root = args.root.resolve()
    manifest_path = args.manifest.resolve()
    errors: list[str] = []
    manifest = load_manifest(manifest_path)

    for entry in manifest["files"]:
        if not isinstance(entry, dict):
            errors.append("manifest file entry is not an object")
            continue
        rel = entry.get("path")
        expected = entry.get("sha256")
        if not isinstance(rel, str) or not isinstance(expected, str):
            errors.append(f"bad manifest entry: {entry!r}")
            continue
        path = root / rel
        if not path.is_file():
            errors.append(f"{rel}: missing protected file")
            continue
        actual = sha256_file(path)
        if actual != expected:
            policy = entry.get("policy", "protected")
            reason = entry.get("reason", "")
            suffix = f" ({reason})" if reason else ""
            errors.append(f"{rel}: {policy} hash changed: expected {expected}, got {actual}{suffix}")

    print("# Engine Divergence Check")
    print(f"root: {root}")
    print(f"manifest: {manifest_path}")
    print(f"protected files: {len(manifest['files'])}")

    if errors:
        print("\n## FAIL")
        for error in errors:
            print(error)
        return 1

    print("\n## PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
