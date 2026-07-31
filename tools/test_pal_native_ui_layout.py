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
    def test_world_view_is_one_to_one_and_player_centered(self) -> None:
        p240 = layout.build_profile(240, 135, FONT)
        p160 = layout.build_profile(160, 128, FONT)
        self.assertEqual((p240.world_origin_x, p240.world_origin_y), (40, 45))
        self.assertEqual((p160.world_origin_x, p160.world_origin_y), (80, 48))
        self.assertEqual(p240.world_origin_x + 120, layout.WORLD_FOCUS_X)
        self.assertEqual(p160.world_origin_x + 80, layout.WORLD_FOCUS_X)

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
            self.assertLess(
                profile.upper.portrait.x, profile.upper.text.x
            )
            self.assertLess(
                profile.lower.text.x, profile.lower.portrait.x
            )
            self.assertEqual(profile.center_text.height, 40)

    def test_header_forbids_whole_frame_scaling_contract(self) -> None:
        header = layout.emit_header(layout.build_profile(240, 135, FONT))
        self.assertIn("LOGICAL_WIDTH 320u", header)
        self.assertIn("DISPLAY_WIDTH 240u", header)
        self.assertIn("WORLD_ORIGIN_X 40u", header)
        self.assertNotIn("SCALE_", header)
        self.assertNotIn("SAMPLE_", header)


if __name__ == "__main__":
    unittest.main()
