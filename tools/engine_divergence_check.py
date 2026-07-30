#!/usr/bin/env python3
"""Check that protected SDLPAL engine files have not drifted silently."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
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
    if data.get("version") != 2:
        raise ValueError("manifest version must be 2")
    if not isinstance(data.get("baseline_id"), str) or not data["baseline_id"]:
        raise ValueError("manifest must name a baseline_id")
    reviewed = data.get("reviewed_against_commit")
    if not isinstance(reviewed, str) or re.fullmatch(r"[0-9a-f]{40}", reviewed) is None:
        raise ValueError("reviewed_against_commit must be a full Git object ID")
    verification = data.get("verification")
    if (
        not isinstance(verification, list)
        or not verification
        or not all(isinstance(item, str) and item for item in verification)
    ):
        raise ValueError("manifest verification must be a non-empty string array")
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
    seen_paths: set[str] = set()

    for entry in manifest["files"]:
        if not isinstance(entry, dict):
            errors.append("manifest file entry is not an object")
            continue
        rel = entry.get("path")
        expected = entry.get("sha256")
        policy = entry.get("policy")
        reason = entry.get("reason")
        if (
            not isinstance(rel, str)
            or not isinstance(expected, str)
            or re.fullmatch(r"[0-9a-f]{64}", expected) is None
            or policy not in ("frozen-upstream", "frozen-reviewed-extreme")
            or not isinstance(reason, str)
            or not reason
        ):
            errors.append(f"bad manifest entry: {entry!r}")
            continue
        if rel in seen_paths:
            errors.append(f"{rel}: duplicate protected file")
            continue
        seen_paths.add(rel)
        path = root / rel
        if not path.is_file():
            errors.append(f"{rel}: missing protected file")
            continue
        actual = sha256_file(path)
        if actual != expected:
            errors.append(
                f"{rel}: {policy} hash changed: expected {expected}, "
                f"got {actual} ({reason})"
            )

    print("# Engine Divergence Check")
    print(f"root: {root}")
    print(f"manifest: {manifest_path}")
    print(f"baseline: {manifest['baseline_id']}")
    print(f"reviewed against: {manifest['reviewed_against_commit']}")
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
