#!/usr/bin/env python3
"""Tests for the end-to-end native UI layout compiler."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from pal_ui_layout.check import (
    PalCorpus,
    _assert_reproducible,
    _dialog_visible_text,
    compile_bundle,
    compile_profile,
    parse_profile_spec,
)
from pal_ui_layout.profiles import DisplayProfile
from pal_ui_layout.solver import FontMetrics


PAL_DATA_DIR = Path("/mnt/hgfs/deb13/PAL")
FONT_ARCHIVE = Path(
    "/tmp/fusion-pixel-font-10px-monospaced-bdf-v2026.07.20.zip"
)


def small_inputs() -> tuple[PalCorpus, FontMetrics, dict[str, str]]:
    words = ["甲"] * 607
    messages = ("$06甲乙~70", "甲甲甲")
    labels = {
        "status": "甲",
        "magic": "甲",
        "items": "甲",
        "system": "甲",
        "save": "甲",
        "load": "甲",
        "exit": "甲",
        "battle": "甲",
        "cancel": "甲",
        "confirm": "甲",
        "equipment": "甲",
        "attack": "甲",
        "coop_magic": "甲",
        "misc": "甲",
        "defend": "甲",
        "all": "甲",
        "flee": "甲",
        "life": "甲",
        "mana": "甲",
        "role": "甲",
        "time_meter": "甲",
        "use": "甲",
    }
    characters = set(
        "甲乙頭部披掛身體手持腳佩戴0123456789/"
    )
    metrics = FontMetrics.from_pairs(
        ((ord(character), 10) for character in characters),
        line_height=10,
        ascent=9,
        descent=1,
    )
    return (
        PalCorpus(
            words=tuple(words),
            messages=messages,
            rendered_messages=tuple(
                _dialog_visible_text(message) for message in messages
            ),
            source_files=(),
        ),
        metrics,
        labels,
    )


class LayoutCompilerTests(unittest.TestCase):
    def test_profile_parser_supports_explicit_safe_insets(self) -> None:
        self.assertEqual(
            parse_profile_spec("240x135@4,3,6,2"),
            DisplayProfile(
                240,
                135,
                safe_left=4,
                safe_top=3,
                safe_right=6,
                safe_bottom=2,
            ),
        )
        with self.assertRaisesRegex(ValueError, "expected WIDTHxHEIGHT"):
            parse_profile_spec("240*135")

    def test_dialog_measurement_removes_legacy_commands(self) -> None:
        self.assertEqual(
            _dialog_visible_text("$06甲'乙'~70丙"),
            "甲乙",
        )
        self.assertEqual(_dialog_visible_text("\\(甲"), "(甲")

    def test_profile_contains_only_python_generated_coefficients(self) -> None:
        corpus, metrics, labels = small_inputs()
        profile = compile_profile(
            DisplayProfile(240, 135),
            metrics,
            corpus,
            labels,
        )
        self.assertEqual(
            profile["coefficient_origin"],
            "python_generated_only",
        )
        self.assertEqual(profile["stage_source"], [320, 200])
        self.assertEqual(
            profile["loading"],
            {
                "label_rect": [57, 25, 126, 21],
                "glyph_width": 5,
                "glyph_height": 7,
                "glyph_advance": 6,
                "glyph_scale": 3,
                "glyph_count": 7,
                "bar_rect": [20, 81, 200, 18],
                "bar_border": 2,
                "progress_max": 100,
            },
        )
        stage = profile["sampling"][0]  # type: ignore[index]
        self.assertEqual(
            (stage["numerator"], stage["denominator"]),
            (27, 40),
        )
        self.assertEqual(stage["step_q16"], [97090, 97090])
        self.assertEqual(profile["sampling_catalog_count"], 7)
        catalog = [
            policy
            for policy in profile["sampling"]  # type: ignore[index]
            if policy.get("catalog")
        ]
        self.assertEqual(
            {policy["asset_class"] for policy in catalog},
            {
                "portrait",
                "item_preview",
                "battle_player",
                "battle_enemy",
                "battle_fire",
                "ui_sprite",
                "battle_effect",
            },
        )
        self.assertEqual(
            profile["text_audit"]["clipped_text_count"],  # type: ignore[index]
            0,
        )
        screen_names = {
            screen["name"] for screen in profile["screens"]  # type: ignore[index]
        }
        self.assertEqual(
            screen_names,
            {
                "dialog",
                "opening_menu",
                "game_menu",
                "system_menu",
                "save_slots",
                "confirmation",
                "item",
                "magic",
                "status",
                "equip",
                "battle_hud",
                "battle_misc",
                "battle_item_action",
            },
        )

    @unittest.skipUnless(
        PAL_DATA_DIR.is_dir() and FONT_ARCHIVE.is_file(),
        "real PAL corpus or pinned Fusion Pixel archive unavailable",
    )
    def test_real_bundle_is_byte_reproducible(self) -> None:
        first = compile_bundle(
            data_dir=PAL_DATA_DIR,
            font10_archive=FONT_ARCHIVE,
        )
        second = compile_bundle(
            data_dir=PAL_DATA_DIR,
            font10_archive=FONT_ARCHIVE,
        )
        _assert_reproducible(first, second)
        self.assertEqual(len(first.target_headers), 2)
        self.assertEqual(len(first.artifacts["font/font10.bin"]), 42240)
        for profile in first.profiles:
            self.assertEqual(
                profile["text_audit"]["clipped_text_count"],  # type: ignore[index]
                0,
            )
            self.assertEqual(profile["sampling_catalog_count"], 369)
            inventory = profile["asset_inventory"]  # type: ignore[index]
            self.assertEqual(
                inventory["portrait"]["source_max"],  # type: ignore[index]
                [135, 131],
            )
            self.assertEqual(
                inventory["item_preview"]["source_max"],  # type: ignore[index]
                [48, 47],
            )
            self.assertEqual(
                inventory["battle_player"]["source_max"],  # type: ignore[index]
                [228, 179],
            )
            self.assertEqual(
                inventory["battle_enemy"]["source_max"],  # type: ignore[index]
                [320, 200],
            )
            self.assertEqual(
                inventory["battle_fire"]["source_max"],  # type: ignore[index]
                [320, 200],
            )
            self.assertEqual(
                inventory["ui_sprite"]["source_max"],  # type: ignore[index]
                [75, 64],
            )
            self.assertEqual(
                inventory["battle_effect"]["source_max"],  # type: ignore[index]
                [90, 90],
            )


if __name__ == "__main__":
    unittest.main()
