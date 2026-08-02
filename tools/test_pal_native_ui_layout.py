#!/usr/bin/env python3

import unittest

import pal_native_ui_layout as layout


FONT = {
    "glyph_count": 2600,
    "bytes": 41632,
    "payload_crc32": 0x12345678,
    "metrics": {
        "cell_width": 10,
        "cell_height": 10,
        "ascent": 9,
        "descent": 1,
    },
}


class NativeUiLayoutTests(unittest.TestCase):
    def test_profile_solver_accepts_reusable_intermediate_sizes(self) -> None:
        profile = layout.build_profile(200, 150, FONT)
        self.assertEqual(profile.name, "200x150")
        self.assertEqual(
            (profile.map_view_offset_x, profile.map_view_offset_y), (60, 37)
        )
        for dialog in (profile.upper, profile.lower):
            self.assertGreater(dialog.text.width, 0)
            self.assertLessEqual(
                dialog.text.x + dialog.text.width, profile.display_width
            )
            self.assertLessEqual(
                dialog.text.y + dialog.text.height, profile.display_height
            )
        self.assertEqual(layout.parse_profile_size("200X150"), (200, 150))

    def test_profile_solver_rejects_sizes_outside_native_contract(self) -> None:
        for width, height in (
            (159, 128),
            (160, 127),
            (321, 128),
            (160, 201),
        ):
            with self.subTest(width=width, height=height):
                with self.assertRaises(ValueError):
                    layout.build_profile(width, height, FONT)
        for value in ("200", "200*150", "axb", "200x"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    layout.parse_profile_size(value)

    def test_compatibility_origins_are_deterministic(self) -> None:
        p240 = layout.build_profile(240, 135, FONT)
        p160 = layout.build_profile(160, 128, FONT)
        self.assertEqual(
            (p240.map_view_offset_x, p240.map_view_offset_y), (40, 45)
        )
        self.assertEqual(
            (p160.map_view_offset_x, p160.map_view_offset_y), (80, 48)
        )
        self.assertEqual(
            p240.map_view_offset_x + 120, layout.MAP_VIEW_FOCUS_X
        )
        self.assertEqual(
            p160.map_view_offset_x + 80, layout.MAP_VIEW_FOCUS_X
        )

    def test_dialog_is_bounded_and_keeps_original_asymmetry(self) -> None:
        for width, height in ((240, 135), (160, 128)):
            profile = layout.build_profile(width, height, FONT)
            for dialog in (profile.upper, profile.lower):
                self.assertGreater(dialog.text.width, 0)
                self.assertEqual(dialog.page_lines, 4)
                self.assertLessEqual(dialog.text.x + dialog.text.width, width)
                self.assertLessEqual(dialog.text.y + dialog.text.height, height)
                self.assertLessEqual(
                    dialog.portrait.x + dialog.portrait.width, width
                )
                self.assertLessEqual(
                    dialog.portrait.y + dialog.portrait.height, height
                )
                self.assertEqual(dialog.text_without_portrait.x, 4)
                self.assertEqual(
                    dialog.text_without_portrait.width, width - 8
                )
                self.assertEqual(dialog.title_without_portrait_x, 12)
            self.assertLess(
                profile.upper.portrait.x, profile.upper.text.x
            )
            self.assertLess(
                profile.lower.text.x, profile.lower.portrait.x
            )
            self.assertEqual(profile.center_text.height, 40)

        # 240x135 is the current visual-acceptance profile. Its portrait-side
        # text rectangle must hold one authored 13-cell line at native 10px.
        accepted = layout.build_profile(240, 135, FONT)
        for dialog in (accepted.upper, accepted.lower):
            self.assertGreaterEqual(
                dialog.text.width,
                layout.DIALOG_SOURCE_LINE_CELLS *
                FONT["metrics"]["cell_width"],
            )

    def test_system_menu_fits_the_lcd(self) -> None:
        for width, height in ((240, 135), (160, 128)):
            profile = layout.build_profile(width, height, FONT)
            self.assertEqual(
                (profile.system_menu.x, profile.system_menu.y), (0, 0)
            )
            self.assertGreaterEqual(profile.system_menu.visible_rows, 1)
            self.assertLessEqual(
                profile.system_menu.text_y +
                (profile.system_menu.visible_rows - 1) *
                profile.system_menu.row_height + layout.FONT_LINE_HEIGHT,
                height,
            )

    def test_header_contains_dialog_and_menu_geometry(self) -> None:
        header = layout.emit_header(layout.build_profile(240, 135, FONT))
        self.assertIn("VIRTUAL_WIDTH 320u", header)
        self.assertIn("DISPLAY_WIDTH 240u", header)
        self.assertIn("MAP_VIEW_OFFSET_X 40u", header)
        self.assertNotIn("SAMPLE_", header)
        self.assertIn("DIALOG_SOURCE_LINE_CELLS 13u", header)
        self.assertIn("DIALOG_POPUP_SINGLE_TEXT_INSET_Y 10u", header)
        self.assertNotIn("DIALOG_POPUP_MULTI", header)
        self.assertIn("DIALOG_UPPER_TITLE_NO_PORTRAIT_X 12", header)
        self.assertIn("DIALOG_LOWER_TITLE_NO_PORTRAIT_X 12", header)
        self.assertNotIn("STATUS_", header)
        self.assertIn("SYSTEM_MENU_X 0", header)
        self.assertIn("SYSTEM_MENU_Y 0", header)
        self.assertIn("SYSTEM_MENU_VISIBLE_ROWS 6u", header)
        self.assertNotIn("BATTLE_", header)
        self.assertNotIn("ITEM_USE_FOCUS", header)


if __name__ == "__main__":
    unittest.main()
