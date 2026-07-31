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
            (profile.world_origin_x, profile.world_origin_y), (60, 37)
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
                self.assertEqual(dialog.title_without_portrait_x, 12)
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
        self.assertIn("DIALOG_POPUP_SINGLE_TEXT_INSET_Y 10u", header)
        self.assertIn("DIALOG_POPUP_MULTI_TEXT_INSET_Y 12u", header)
        self.assertIn("DIALOG_UPPER_TITLE_NO_PORTRAIT_X 12", header)
        self.assertIn("DIALOG_LOWER_TITLE_NO_PORTRAIT_X 12", header)

    def test_battle_keeps_original_hud_shape_inside_player_view(self) -> None:
        for width, height, info_x, battle_y in (
            (240, 135, 91, (75, 90, 90, 105, 100)),
            (160, 128, 86, (68, 83, 83, 98, 93)),
        ):
            profile = layout.build_profile(width, height, FONT)
            battle = profile.battle
            self.assertEqual(
                (
                    (battle.attack.x, battle.attack.y),
                    (battle.magic.x, battle.magic.y),
                    (battle.coop_magic.x, battle.coop_magic.y),
                    (battle.misc.x, battle.misc.y),
                ),
                (
                    (27, battle_y[0]),
                    (0, battle_y[1]),
                    (54, battle_y[2]),
                    (27, battle_y[3]),
                ),
            )
            self.assertLess(battle.coop_magic.x, width)
            self.assertEqual(battle.info_local_x, info_x)
            self.assertEqual(battle.info_y, battle_y[4])

            result_left = max(0, battle.result_focus.x - width // 2)
            result_top = max(0, battle.result_focus.y - height // 2)
            self.assertLessEqual(result_left, 77)
            self.assertGreaterEqual(result_left + width, 230)
            self.assertLessEqual(result_top, 60)
            self.assertGreaterEqual(result_top + height, 140)
            self.assertEqual(
                (battle.result_focus.x, battle.result_focus.y), (153, 110)
            )
            self.assertEqual(battle.level_up_focus.x, 170)
            self.assertEqual(
                max(0, battle.level_up_focus.y - height // 2), 0
            )

            header = layout.emit_header(profile)
            self.assertIn("BATTLE_ATTACK_LOCAL_X 27", header)
            self.assertIn(
                f"BATTLE_INFO_LOCAL_X {info_x}", header
            )
            self.assertIn(
                f"BATTLE_ATTACK_LOCAL_Y {battle_y[0]}", header
            )
            self.assertIn("BATTLE_RESULT_FOCUS_X 153", header)
            self.assertIn("BATTLE_RESULT_FOCUS_Y 110", header)
            self.assertIn("BATTLE_LEVEL_UP_FOCUS_X 170", header)
            self.assertIn(
                f"BATTLE_LEVEL_UP_FOCUS_Y {height // 2}", header
            )
            self.assertNotIn("BATTLE_INFO_STRIDE", header)

        # The solver returns to PAL's original x=91 as soon as the 75px
        # frame fits without touching the 84px action group.
        self.assertEqual(layout._battle_info_local_x(161), 86)
        self.assertEqual(layout._battle_info_local_x(165), 90)
        self.assertEqual(layout._battle_info_local_x(166), 91)


if __name__ == "__main__":
    unittest.main()
