#!/usr/bin/env python3
"""Tests for the generated-only C/Python parity fixture pipeline."""

from __future__ import annotations

import json
import unittest

from pal_ui_layout.emit_c import emit_profile_header
from pal_ui_layout.parity import build_parity_profile
from pal_ui_layout.profiles import DisplayProfile


class LayoutParityProfileTests(unittest.TestCase):
    def test_certified_profiles_are_byte_deterministic_and_complete(self) -> None:
        for width, height, camera_count, sampling_count in (
            (240, 135, 4, 17),
            (160, 128, 4, 16),
        ):
            with self.subTest(profile=(width, height)):
                profile = DisplayProfile(width, height)
                first = build_parity_profile(profile)
                second = build_parity_profile(profile)
                self.assertEqual(
                    json.dumps(
                        first,
                        sort_keys=True,
                        separators=(",", ":"),
                    ),
                    json.dumps(
                        second,
                        sort_keys=True,
                        separators=(",", ":"),
                    ),
                )
                self.assertEqual(
                    [screen["name"] for screen in first["screens"]],
                    [
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
                    ],
                )
                self.assertEqual(
                    first["screens"][0]["variant"],
                    "paged",
                )
                self.assertGreater(
                    first["screens"][0]["initial_page"],
                    0,
                )
                self.assertEqual(
                    len(first["sampling"]), sampling_count
                )
                self.assertIn("battle_camera_policy", first)
                self.assertEqual(
                    len(first["camera_vectors"]),
                    camera_count,
                )
                for vector in first["camera_vectors"]:
                    self.assertIn("scale", vector)
                    self.assertIn("screen", vector)
                    self.assertIn("desired", vector)
                    self.assertIn("projected_focus", vector)
                    self.assertIn("source_step_q16", vector)
                    self.assertIn("source_phase_q16", vector)

    def test_emitted_header_contains_runtime_abi_and_stage_coefficients(self) -> None:
        header = emit_profile_header(
            build_parity_profile(DisplayProfile(240, 135))
        )
        self.assertIn(
            "typedef struct PalUiGeneratedCameraVector",
            header,
        )
        self.assertIn("PalUiGeneratedRect screen;", header)
        self.assertIn("uint16_t scale_numerator;", header)
        self.assertIn("uint32_t source_step_q16;", header)
        self.assertIn("int16_t projected_focus_x;", header)
        self.assertIn("uint8_t initial_page;", header)
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SCALE_NUMERATOR 27u",
            header,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SCALE_DENOMINATOR 40u",
            header,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_STEP_X_Q16 97090u",
            header,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_CAMERA_VECTOR_COUNT 4u",
            header,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT",
            header,
        )
        self.assertIn(
            "pal_ui_generated_battle_camera_policy",
            header,
        )

    def test_uncertified_profile_has_no_implicit_vectors(self) -> None:
        with self.assertRaisesRegex(ValueError, "no certified"):
            build_parity_profile(DisplayProfile(200, 120))


if __name__ == "__main__":
    unittest.main()
