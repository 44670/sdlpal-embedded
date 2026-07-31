#!/usr/bin/env python3
"""Generate deterministic C/Python layout-camera parity profiles.

These profiles are contract fixtures, not target-side defaults.  They pass
the complete public screen ABI through the Python compiler using a small
synthetic corpus; :mod:`emit_c` then freezes every result into a C header.
The embedded runtime consumes only that header and never imports or
reimplements the solver.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Sequence

from .check import (
    PalCorpus,
    compile_profile,
    default_asset_inventory,
)
from .emit_c import write_profile_header
from .profiles import (
    DisplayProfile,
    certified_profiles,
    parse_resolution,
)
from .solver import FontMetrics


_LABEL_KEYS = (
    "all",
    "attack",
    "battle",
    "cancel",
    "confirm",
    "coop_magic",
    "defend",
    "equipment",
    "exit",
    "flee",
    "items",
    "load",
    "magic",
    "misc",
    "save",
    "status",
    "system",
    "use",
)

_PUBLIC_ELEMENT_KINDS = frozenset(
    (
        "action",
        "dialog_text",
        "equipment_label",
        "equipment_label_value",
        "equipment_preview",
        "page_indicator",
        "panel",
        "party_selector",
        "portrait",
        "stat_label",
        "stat_label_value",
        "title",
    )
)


def _fixture_font() -> FontMetrics:
    """Return frozen synthetic FONT10 metrics sufficient for fixture labels."""

    characters = (
        "".join(chr(codepoint) for codepoint in range(0x20, 0x7F))
        + "頭部披掛身體手持腳佩戴"
    )
    return FontMetrics.from_pairs(
        (
            (codepoint, 5 if codepoint == ord(" ") else 10)
            for codepoint in sorted({ord(character) for character in characters})
        ),
        line_height=10,
        ascent=9,
        descent=1,
    )


def _fixture_corpus() -> PalCorpus:
    words = ["A"] * 700
    for index in range(24):
        words[100 + index] = f"LONG{index:02d}XX"
    messages = ("AA", "BB", "CC", "DD")
    return PalCorpus(tuple(words), messages, messages, ())


def _fixture_labels() -> dict[str, str]:
    return {
        key: chr(ord("A") + index)
        for index, key in enumerate(_LABEL_KEYS)
    }


def build_parity_profile(profile: DisplayProfile) -> dict[str, object]:
    """Build one fully resolved profile for Python/C contract testing."""

    certified = {(item.width, item.height) for item in certified_profiles()}
    if (profile.width, profile.height) not in certified:
        raise ValueError(
            f"{profile.name} has no certified shared camera vectors"
        )

    manifest = compile_profile(
        profile,
        _fixture_font(),
        _fixture_corpus(),
        _fixture_labels(),
        default_asset_inventory(),
    )
    manifest["name"] = f"{profile.name}-parity"

    screens = manifest.get("screens")
    if not isinstance(screens, list):
        raise ValueError("parity compiler returned no screens")
    present_kinds = {
        str(element.get("kind"))
        for screen in screens
        if isinstance(screen, dict)
        for element in screen.get("elements", ())
        if isinstance(element, dict)
    }
    unexpected = present_kinds - _PUBLIC_ELEMENT_KINDS
    if unexpected:
        raise ValueError(
            f"parity compiler emitted non-public element kinds: "
            f"{sorted(unexpected)}"
        )

    # A synthetic corpus can make a decorative element disappear at one
    # resolution.  Keep one invisible, bounded sentinel for every public kind
    # so this C fixture always exercises the complete stable enum ABI.
    equip = next(
        (
            screen
            for screen in screens
            if isinstance(screen, dict) and screen.get("name") == "equip"
        ),
        None,
    )
    if not isinstance(equip, dict) or not isinstance(
        equip.get("elements"), list
    ):
        raise ValueError("parity compiler returned no equipment screen")
    for kind in sorted(_PUBLIC_ELEMENT_KINDS - present_kinds):
        equip["elements"].append(
            {
                "name": f"abi_kind_{kind}",
                "kind": kind,
                "rect": [0, 0, 1, 1],
                "page": 0,
                "priority": 0,
                "visible": False,
                "selectable": False,
                "critical": False,
                "return_value": -1,
            }
        )
    return manifest


def write_parity_header(profile: DisplayProfile, output: Path) -> None:
    write_profile_header(build_parity_profile(profile), output)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--profile",
        required=True,
        help="certified display resolution (240x135 or 160x128)",
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    write_parity_header(parse_resolution(args.profile), args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
