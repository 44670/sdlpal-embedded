#!/usr/bin/env python3
"""Unit tests for display profiles and presentation-stage geometry."""

from __future__ import annotations

import unittest

from pal_ui_layout.profiles import (
    DisplayProfile,
    ProfileRect,
    certified_profiles,
    loading_layout,
    parse_resolution,
)


class DisplayProfileTests(unittest.TestCase):
    def test_certified_stage_rectangles_are_exact(self) -> None:
        cardputer, compact = certified_profiles()
        self.assertEqual(
            cardputer.contain_stage_rect(),
            ProfileRect(12, 0, 216, 135),
        )
        self.assertEqual(
            compact.contain_stage_rect(),
            ProfileRect(0, 14, 160, 100),
        )

    def test_safe_area_changes_stage_and_anchor(self) -> None:
        profile = DisplayProfile(
            240,
            135,
            safe_left=4,
            safe_top=3,
            safe_right=6,
            safe_bottom=2,
        )
        self.assertEqual(profile.safe_rect, ProfileRect(4, 3, 230, 130))
        self.assertEqual(profile.player_anchor, (119, 68))
        self.assertEqual(profile.contain_stage_rect(), ProfileRect(15, 3, 208, 130))

    def test_legacy_player_is_only_a_presentation_projection(self) -> None:
        profile = DisplayProfile(240, 135)
        self.assertEqual(profile.legacy_player_presented(), (120, 76))
        self.assertEqual(profile.player_anchor, (120, 67))

    def test_loading_chrome_is_fully_profile_generated(self) -> None:
        self.assertEqual(
            loading_layout(DisplayProfile(240, 135)),
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
        self.assertEqual(
            loading_layout(DisplayProfile(160, 128)),
            {
                "label_rect": [38, 24, 84, 14],
                "glyph_width": 5,
                "glyph_height": 7,
                "glyph_advance": 6,
                "glyph_scale": 2,
                "glyph_count": 7,
                "bar_rect": [13, 76, 134, 12],
                "bar_border": 1,
                "progress_max": 100,
            },
        )

    def test_resolution_parser(self) -> None:
        self.assertEqual(parse_resolution("160X128"), DisplayProfile(160, 128))
        with self.assertRaises(ValueError):
            parse_resolution("160*128")
        with self.assertRaises(ValueError):
            parse_resolution("0x128")

    def test_invalid_safe_area_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            DisplayProfile(160, 128, safe_left=80, safe_right=80)
        with self.assertRaises(ValueError):
            DisplayProfile(160, 128, safe_top=-1)
        with self.assertRaises(ValueError):
            DisplayProfile(160, 128, font_pixel_size=8)


if __name__ == "__main__":
    unittest.main()
