#!/usr/bin/env python3
"""Generate the compact audited Cardputer ADV selection plan used by the web builder."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PACK_DIR = ROOT / "esp32s3" / "TF_datapak"
DEFAULT_OUTPUT = ROOT / "tools" / "pal_web_cardputer_plan.json"


def build_plan(pack_dir: Path) -> dict[str, object]:
    manifest = json.loads((pack_dir / "chapter_manifest.json").read_text())
    closure = manifest["closure_audit"]
    return {
        "schema": "sdlpal-web-cardputer-plan",
        "version": 2,
        "core_full_archives": [
            "DATA",
            "SSS",
            "TEXT",
            "PAT",
            "MUS",
            "BALL",
            "RGM",
            "F",
            "FIRE",
        ],
        "core_mgo_ids": sorted(
            set(closure["core_global_mgo_ids"])
            | set(closure["core_startup_mgo_ids"])
        ),
        "scene_bundles": manifest["scene_partition"]["bundles"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack-dir", type=Path, default=DEFAULT_PACK_DIR)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    rendered = json.dumps(build_plan(args.pack_dir), indent=2, sort_keys=True) + "\n"
    if args.check:
        if not args.output.is_file() or args.output.read_text() != rendered:
            raise SystemExit(
                f"{args.output}: stale Cardputer web plan; regenerate it"
            )
        print(f"{args.output}: Cardputer web plan is current")
        return 0

    args.output.write_text(rendered)
    print(f"{args.output}: generated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
