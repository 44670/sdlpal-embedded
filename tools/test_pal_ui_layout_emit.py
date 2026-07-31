#!/usr/bin/env python3
"""Tests for deterministic target header and dependency-free previews."""

from __future__ import annotations

import copy
import hashlib
import tempfile
import unittest
from pathlib import Path

from pal_ui_layout.emit_c import EmitError, emit_profile_header
from pal_ui_layout.font import FONT10_BITMAP_BYTES, Font10, Font10Glyph
from pal_ui_layout.preview import (
    PreviewError,
    decode_pal_rle,
    render_loading,
    render_screen,
    write_profile_previews,
)


def sample_profile() -> dict[str, object]:
    return {
        "schema_version": 1,
        "public_abi_version": 1,
        "name": "240x135",
        "display": {"width": 240, "height": 135},
        "safe_rect": {"x": 0, "y": 0, "w": 240, "h": 135},
        "stage_rect": {"x": 12, "y": 0, "w": 216, "h": 135},
        "stage_source": [320, 200],
        "player_anchor": [120, 67],
        "font": {
            "cell_width": 10,
            "cell_height": 10,
            "ascent": 9,
            "descent": 1,
            "line_height": 10,
        },
        "loading": {
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
        "sampling": [
            {
                "name": "stage",
                "filter": "nearest_center",
                "numerator": 27,
                "denominator": 40,
                "source": [320, 200],
                "destination_size": [216, 135],
                "step_q16": [97090, 97090],
                "phase_q16": [48545, 48545],
                "min_width": 216,
                "min_height": 135,
                "max_width": 216,
                "max_height": 135,
            },
            {
                "name": "portrait",
                "filter": "nearest_center",
                "numerator": 1,
                "denominator": 2,
                "source": [64, 64],
                "destination_size": [32, 32],
                "step_q16": [131072, 131072],
                "phase_q16": [65536, 65536],
                "screen": "menu",
                "destination": [180, 8, 32, 32],
                "min_width": 20,
                "min_height": 20,
                "max_width": 48,
                "max_height": 48,
            },
        ],
        "screens": [
            {
                "name": "menu",
                "variant": "single_column",
                "rows": 2,
                "columns": 1,
                "page_count": 1,
                "page_capacity": 2,
                "focus_order": ["item_0", "item_1"],
                "elements": [
                    {
                        "name": "item_0",
                        "kind": "action",
                        "rect": [10, 10, 100, 12],
                        "page": 0,
                        "priority": 255,
                        "visible": True,
                        "selectable": True,
                        "critical": True,
                        "return_value": 7,
                    },
                    {
                        "name": "item_1",
                        "kind": "action",
                        "rect": {"x": 10, "y": 24, "w": 100, "h": 12},
                        "page": 0,
                        "priority": 255,
                        "visible": True,
                        "selectable": True,
                        "critical": True,
                        "return_value": 9,
                    },
                ],
            }
        ],
        "camera_vectors": [
            {
                "kind": "map",
                "mode": "follow",
                "bounds": {"x": 0, "y": 0, "w": 320, "h": 200},
                "focus": [160, 112],
                "camera": [0, 0],
                "scale_q16": 44236,
            },
            {
                "kind": "battle",
                "mode": "actor_target",
                "bounds": [40, 30, 240, 135],
                "focus": [160, 100],
                "camera": [40, 30],
                "scale_q16": 65536,
            },
        ],
    }


def solid_font(*characters: str) -> Font10:
    return Font10(
        ascent=9,
        descent=1,
        glyphs=tuple(
            Font10Glyph(
                ord(character),
                10,
                bytes([0xFF] * FONT10_BITMAP_BYTES),
            )
            for character in sorted(set(characters))
        ),
        payload_crc32=0,
    )


class EmitTests(unittest.TestCase):
    def test_header_is_deterministic_and_contains_generated_coefficients(self) -> None:
        profile = sample_profile()
        first = emit_profile_header(profile)
        second = emit_profile_header(profile)
        self.assertEqual(first, second)
        self.assertIn("#define PAL_UI_GENERATED_STAGE_WIDTH 216u", first)
        self.assertIn(
            "#define PAL_UI_GENERATED_COEFFICIENTS_ONLY 1u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SOURCE_WIDTH 320u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH 216u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT 135u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SCALE_NUMERATOR 27u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SCALE_DENOMINATOR 40u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SCALE_Q16 44236u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_STEP_X_Q16 97090u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_PHASE_X_Q16 48545u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT 216u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT 135u",
            first,
        )
        self.assertIn(
            "static const uint16_t pal_ui_generated_stage_sample_x[]",
            first,
        )
        self.assertIn(
            "static const uint16_t pal_ui_generated_stage_sample_y[]",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT 216u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT 135u",
            first,
        )
        self.assertIn(
            "pal_ui_generated_battle_camera_policy",
            first,
        )
        self.assertIn(
            "pal_ui_generated_battle_fit_sample_x[]",
            first,
        )
        self.assertIn(
            "pal_ui_generated_battle_fit_sample_y[]",
            first,
        )
        self.assertIn("0u, 2u, 3u, 5u", first)
        self.assertIn(
            "#define PAL_UI_GENERATED_FONT_GLYPH_COUNT 0u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_FONT_IMAGE_BYTES 0u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_FONT_PAYLOAD_CRC32 0x00000000u",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_LOADING_LABEL_X 57",
            first,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_LOADING_BAR_WIDTH 200u",
            first,
        )
        self.assertIn("44236u, 97090u, 97090u", first)
        self.assertIn("PAL_UI_SAMPLE_PORTRAIT", first)
        self.assertIn("PAL_UI_SCREEN_MENU", first)
        self.assertIn("return_value", first)
        self.assertEqual(
            hashlib.sha256(first.encode("utf-8")).hexdigest(),
            hashlib.sha256(second.encode("utf-8")).hexdigest(),
        )

    def test_unsupported_public_abi_version_fails_closed(self) -> None:
        profile = sample_profile()
        profile["public_abi_version"] = 0
        with self.assertRaisesRegex(
            EmitError, "public_abi_version must be >= 1"
        ):
            emit_profile_header(profile)

    def test_missing_focus_target_fails_closed(self) -> None:
        profile = sample_profile()
        screens = profile["screens"]
        assert isinstance(screens, list)
        screens[0]["focus_order"] = ["absent"]  # type: ignore[index]
        with self.assertRaisesRegex(EmitError, "focus element"):
            emit_profile_header(profile)

    def test_stage_source_must_be_explicit_python_output(self) -> None:
        profile = sample_profile()
        del profile["stage_source"]
        with self.assertRaisesRegex(EmitError, "stage_source must be an array"):
            emit_profile_header(profile)

    def test_explicit_battle_policy_requires_exact_sample_maps(self) -> None:
        profile = sample_profile()
        policy = {
            "arena": [0, 0, 320, 200],
            "hud_rect": [4, 4, 232, 30],
            "content_rect": [0, 34, 240, 101],
            "focus_source": [240, 101],
            "focus_screen": [0, 34, 240, 101],
            "fit_screen": [40, 34, 160, 100],
            "fit_scale": [1, 2],
            "fit_scale_q16": 32768,
            "fit_step_q16": 131072,
            "fit_phase_q16": 65536,
            "fit_sample_x": [
                (index * 2 + 1) * 320 // (160 * 2)
                for index in range(160)
            ],
            "fit_sample_y": [
                (index * 2 + 1) * 200 // (100 * 2)
                for index in range(100)
            ],
            "padding": 4,
            "max_players": 3,
        }
        profile["battle_camera_policy"] = policy
        header = emit_profile_header(profile)
        self.assertIn(
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCREEN_Y 34",
            header,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_BATTLE_FIT_SCALE_NUMERATOR 1u",
            header,
        )

        bad_players = copy.deepcopy(profile)
        bad_players["battle_camera_policy"]["max_players"] = 2
        with self.assertRaisesRegex(
            EmitError, "max_players must equal runtime capacity 3"
        ):
            emit_profile_header(bad_players)

        bad_hud = copy.deepcopy(profile)
        bad_hud["battle_camera_policy"]["hud_rect"] = [-1, 4, 232, 30]
        with self.assertRaisesRegex(
            EmitError, "hud_rect must be zero or inside safe_rect"
        ):
            emit_profile_header(bad_hud)

        fractional_fit = copy.deepcopy(profile)
        fractional_policy = fractional_fit["battle_camera_policy"]
        fractional_policy.update(
            {
                "fit_screen": [51, 42, 137, 85],
                "fit_scale": [3, 7],
                "fit_scale_q16": (3 << 16) // 7,
                "fit_step_q16": (7 << 16) // 3,
                "fit_phase_q16": ((7 << 16) // 3) // 2,
                "fit_sample_x": [
                    (index * 2 + 1) * 320 // (137 * 2)
                    for index in range(137)
                ],
                "fit_sample_y": [
                    (index * 2 + 1) * 200 // (85 * 2)
                    for index in range(85)
                ],
            }
        )
        with self.assertRaisesRegex(
            EmitError, "fit_scale must exactly divide both arena axes"
        ):
            emit_profile_header(fractional_fit)

        policy["fit_sample_x"][0] = 99
        with self.assertRaisesRegex(EmitError, "fit_sample_x is not exact"):
            emit_profile_header(profile)

    def test_font_identity_and_exact_camera_coefficients_fail_closed(self) -> None:
        profile = sample_profile()
        font = profile["font"]
        assert isinstance(font, dict)
        font.update(
            {
                "glyph_count": 2,
                "image_bytes": 64,
                "payload_crc32": 0x12345678,
            }
        )
        header = emit_profile_header(profile)
        self.assertIn(
            "#define PAL_UI_GENERATED_FONT_GLYPH_COUNT 2u",
            header,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_FONT_IMAGE_BYTES 64u",
            header,
        )
        self.assertIn(
            "#define PAL_UI_GENERATED_FONT_PAYLOAD_CRC32 0x12345678u",
            header,
        )

        font["image_bytes"] = 63
        with self.assertRaisesRegex(EmitError, "FONT10 glyph_count"):
            emit_profile_header(profile)

        profile = sample_profile()
        vectors = profile["camera_vectors"]
        assert isinstance(vectors, list)
        vectors[0]["scale"] = [27, 40]  # type: ignore[index]
        vectors[0]["screen"] = [12, 0, 216, 135]  # type: ignore[index]
        vectors[0]["projected_focus"] = [121, 75]  # type: ignore[index]
        with self.assertRaisesRegex(EmitError, "projected_focus"):
            emit_profile_header(profile)

    def test_ppm_preview_is_bounded_and_deterministic(self) -> None:
        profile = sample_profile()
        screens = profile["screens"]
        assert isinstance(screens, list)
        canvas = render_screen(profile, screens[0])  # type: ignore[arg-type]
        first = canvas.ppm_bytes()
        second = render_screen(
            profile, screens[0]  # type: ignore[arg-type]
        ).ppm_bytes()
        self.assertEqual(first, second)
        self.assertTrue(first.startswith(b"P6\n240 135\n255\n"))
        self.assertEqual(
            len(first),
            len(b"P6\n240 135\n255\n") + 240 * 135 * 3,
        )

        with tempfile.TemporaryDirectory(prefix="pal-layout-preview-") as tmp:
            output = Path(tmp)
            written = write_profile_previews(profile, output)
            self.assertEqual([path.name for path in written], ["menu-p1.ppm"])
            self.assertEqual(written[0].read_bytes(), first)

    def test_preview_omits_elements_from_other_pages(self) -> None:
        baseline_profile = sample_profile()
        baseline_screen = baseline_profile["screens"][0]  # type: ignore[index]
        baseline = render_screen(
            baseline_profile,
            baseline_screen,  # type: ignore[arg-type]
            page=0,
        ).ppm_bytes()

        profile = copy.deepcopy(baseline_profile)
        screen = profile["screens"][0]  # type: ignore[index]
        screen["page_count"] = 2  # type: ignore[index]
        screen["elements"].append(  # type: ignore[index]
            {
                "name": "page_1_overlap",
                "kind": "action",
                "rect": [10, 10, 100, 12],
                "page": 1,
                "priority": 255,
                "visible": True,
                "selectable": True,
                "critical": True,
                "return_value": 11,
            }
        )
        page_zero = render_screen(
            profile,
            screen,  # type: ignore[arg-type]
            page=0,
        ).ppm_bytes()
        page_one = render_screen(
            profile,
            screen,  # type: ignore[arg-type]
            page=1,
        ).ppm_bytes()
        self.assertEqual(page_zero, baseline)
        self.assertNotEqual(page_one, page_zero)

        paged = sample_profile()
        paged_screen = paged["screens"][0]  # type: ignore[index]
        paged_screen["page_count"] = 2  # type: ignore[index]
        paged_screen["selected_key"] = "item_0"  # type: ignore[index]
        paged_elements = paged_screen["elements"]  # type: ignore[index]
        paged_elements[1]["page"] = 1  # type: ignore[index]
        paged_elements[0]["text"] = "A"  # type: ignore[index]
        paged_elements[1]["text"] = "A"  # type: ignore[index]
        font = solid_font("A")
        fallback = render_screen(
            paged,
            paged_screen,  # type: ignore[arg-type]
            page=1,
            font=font,
        ).ppm_bytes()
        paged_screen["selected_key"] = "item_1"  # type: ignore[index]
        explicit = render_screen(
            paged,
            paged_screen,  # type: ignore[arg-type]
            page=1,
            font=font,
        ).ppm_bytes()
        self.assertEqual(fallback, explicit)

    def test_preview_renders_real_font_pixels_and_rejects_clipping(self) -> None:
        profile = sample_profile()
        screen = profile["screens"][0]  # type: ignore[index]
        element = screen["elements"][0]  # type: ignore[index]
        element["text"] = "AA"  # type: ignore[index]
        element["rect"] = [10, 10, 20, 10]  # type: ignore[index]
        screen["selected_key"] = "item_0"  # type: ignore[index]
        font = solid_font("A")

        wireframe = render_screen(
            profile,
            screen,  # type: ignore[arg-type]
        ).ppm_bytes()
        rendered = render_screen(
            profile,
            screen,  # type: ignore[arg-type]
            font=font,
        ).ppm_bytes()
        self.assertNotEqual(rendered, wireframe)

        element["rect"] = [10, 10, 19, 10]  # type: ignore[index]
        with self.assertRaisesRegex(PreviewError, "needs 20x10"):
            render_screen(
                profile,
                screen,  # type: ignore[arg-type]
                font=font,
            )

        element["text"] = ""  # type: ignore[index]
        element["rect"] = [239, 134, 2, 2]  # type: ignore[index]
        with self.assertRaisesRegex(PreviewError, "exceeds 240x135"):
            render_screen(
                profile,
                screen,  # type: ignore[arg-type]
            )

    def test_preview_strictly_decodes_pal_rle_and_loading(self) -> None:
        image = decode_pal_rle(
            b"\x02\0\0\0"
            b"\x02\0\x02\0"
            b"\x82"
            b"\x02\x03\x04",
            source="fixture",
        )
        self.assertEqual((image.width, image.height), (2, 2))
        self.assertEqual(image.indices, b"\0\0\x03\x04")
        self.assertEqual(image.opaque, b"\0\0\x01\x01")
        with self.assertRaisesRegex(PreviewError, "truncated"):
            decode_pal_rle(b"\x01\0\x01\0", source="bad")

        loading = render_loading(sample_profile(), percent=50)
        self.assertEqual(
            len(loading.ppm_bytes()),
            len(b"P6\n240 135\n255\n") + 240 * 135 * 3,
        )


if __name__ == "__main__":
    unittest.main()
