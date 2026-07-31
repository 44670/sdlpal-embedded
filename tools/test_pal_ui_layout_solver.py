#!/usr/bin/env python3
"""Pure-standard-library tests for PAL small-screen layout solving."""

from __future__ import annotations

import json
import sys
import unittest
from dataclasses import replace
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from pal_ui_layout.screens import (  # noqa: E402
    battle_hud_screen,
    candidates_for_screen,
    dialog_screen,
    equip_screen,
    item,
    item_screen,
    magic_screen,
    menu_screen,
    solve_screen,
    status_screen,
)
from pal_ui_layout.emit_c import emit_profile_header  # noqa: E402
from pal_ui_layout.font import (  # noqa: E402
    BdfBoundingBox,
    BdfFont,
    BdfGlyph,
    FONT10_BITMAP_BYTES,
    Font10,
    Font10Glyph,
)
from pal_ui_layout.profiles import DisplayProfile, loading_layout  # noqa: E402
from pal_ui_layout.solver import (  # noqa: E402
    AssetPolicy,
    AssetSpace,
    AssetSpec,
    FontMetrics,
    LayoutCandidate,
    LayoutDecision,
    LayoutMode,
    LayoutPage,
    LayoutProblem,
    MIN_READABLE_FONT_PX,
    MissingGlyphError,
    PlacedElement,
    Q16_ONE,
    Rect,
    SamplingPolicy,
    SemanticItem,
    evaluate_candidate,
    measure_text,
    solve_layout,
)


TEST_CHARACTERS = (
    "".join(chr(codepoint) for codepoint in range(0x20, 0x7F))
    + "项令仙剑这是第一段需要在小屏幕上确定性换行的对话"
    + "第二也必须完整保留确定取消李逍遥你好赵灵儿。"
)
TEST_FONT = FontMetrics.from_pairs(
    (
        (codepoint, 5 if codepoint < 0x80 else 10)
        for codepoint in sorted(set(map(ord, TEST_CHARACTERS)))
    ),
    line_height=12,
)


def entries(
    count: int,
    *,
    prefix: str = "entry",
    label_prefix: str = "项",
    role: str = "action",
    value_base: int = 100,
) -> tuple[SemanticItem, ...]:
    return tuple(
        item(
            f"{prefix}_{index}",
            f"{label_prefix}{index}",
            value_base + index,
            role=role,
        )
        for index in range(count)
    )


def semantic_trace(candidate: LayoutCandidate) -> list[tuple[str, int | None]]:
    result: list[tuple[str, int | None]] = []
    for page in candidate.pages:
        semantic = sorted(
            (
                element
                for element in page.elements
                if element.semantic_index is not None
            ),
            key=lambda element: element.reading_order,
        )
        result.extend((element.key, element.return_value) for element in semantic)
    return result


class SolverContractTests(unittest.TestCase):
    def test_rect_text_and_font_rules_are_integer_only(self) -> None:
        self.assertTrue(Rect(0, 0, 20, 20).contains(Rect(2, 3, 4, 5)))
        self.assertFalse(Rect(0, 0, 5, 5).overlaps(Rect(5, 0, 3, 3)))
        variable_font = FontMetrics.from_pairs(
            ((ord("A"), 4), (ord("B"), 7), (ord("仙"), 9)),
            line_height=13,
        )
        self.assertEqual(measure_text("AB", variable_font), 11)
        self.assertEqual(measure_text("仙", variable_font), 9)
        with self.assertRaises(MissingGlyphError):
            measure_text("剑", variable_font)

        problem = LayoutProblem("font", 80, 40, TEST_FONT)
        too_small = LayoutCandidate(
            "font:text_only",
            LayoutMode.TEXT_ONLY,
            MIN_READABLE_FONT_PX - 1,
            (LayoutPage(0, ()),),
        )
        evaluation = evaluate_candidate(problem, too_small)
        self.assertFalse(evaluation.valid)
        self.assertTrue(
            any("not fixed FONT10" in violation for violation in evaluation.violations)
        )

    def test_metrics_are_losslessly_frozen_from_parsed_font10(self) -> None:
        parsed = Font10(
            ascent=8,
            descent=2,
            glyphs=(
                Font10Glyph(ord("A"), 4, bytes(FONT10_BITMAP_BYTES)),
                Font10Glyph(ord("B"), 7, bytes(FONT10_BITMAP_BYTES)),
            ),
            payload_crc32=0,
        )
        metrics = FontMetrics.from_font10(parsed)
        self.assertEqual(metrics.advances, ((ord("A"), 4), (ord("B"), 7)))
        self.assertEqual(metrics.line_height, 10)
        self.assertEqual(metrics.ascent, 8)
        self.assertEqual(metrics.descent, 2)
        self.assertEqual(measure_text("AB", metrics), 11)
        self.assertEqual(
            metrics.to_manifest(),
            {
                "family": "fusion-pixel-10",
                "pixel_height": 10,
                "cell_width": 10,
                "cell_height": 10,
                "ascent": 8,
                "descent": 2,
                "line_height": 10,
                "glyph_count": 2,
            },
        )

    def test_metrics_are_losslessly_frozen_from_parsed_bdf(self) -> None:
        bounds = BdfBoundingBox(10, 10, 0, -2)
        glyph_a = BdfGlyph(
            "A",
            ord("A"),
            4,
            0,
            bounds,
            (0,) * 10,
        )
        glyph_mark = BdfGlyph(
            "mark",
            0x0301,
            0,
            0,
            bounds,
            (0,) * 10,
        )
        parsed = BdfFont(
            version="2.1",
            name="synthetic",
            size=(10, 75, 75),
            bbox=bounds,
            ascent=8,
            descent=2,
            properties={},
            glyphs={ord("A"): glyph_a, 0x0301: glyph_mark},
        )
        metrics = FontMetrics.from_bdf(parsed, (0x0301, ord("A")))
        self.assertEqual(metrics.advances, ((ord("A"), 4), (0x0301, 0)))
        self.assertEqual(measure_text("A\u0301", metrics), 4)
        with self.assertRaises(MissingGlyphError):
            FontMetrics.from_bdf(parsed, (ord("B"),))

    def test_out_of_bounds_overlap_and_focus_occlusion_are_rejected(self) -> None:
        focus = Rect(35, 25, 20, 20)
        problem = LayoutProblem(
            "geometry",
            100,
            70,
            TEST_FONT,
            focus=focus,
            protect_focus=True,
        )
        candidate = LayoutCandidate(
            "geometry:bad",
            LayoutMode.FULL,
            10,
            (
                LayoutPage(
                    0,
                    (
                        PlacedElement(
                            "one",
                            "panel",
                            Rect(0, 0, 60, 50),
                            blocks_focus=True,
                        ),
                        PlacedElement(
                            "two",
                            "panel",
                            Rect(50, 10, 60, 40),
                        ),
                    ),
                ),
            ),
        )
        evaluation = evaluate_candidate(problem, candidate)
        self.assertFalse(evaluation.valid)
        self.assertTrue(
            any("out of bounds" in violation for violation in evaluation.violations)
        )
        self.assertTrue(
            any("illegal overlap" in violation for violation in evaluation.violations)
        )
        self.assertIn("protected focus is obscured", evaluation.violations)

    def test_fixed_scoring_is_independent_of_candidate_input_order(self) -> None:
        spec = menu_screen(
            240,
            135,
            entries(4),
            font_metrics=TEST_FONT,
            selected_key="entry_2",
        )
        problem = spec.problem()
        candidates = candidates_for_screen(spec)
        forward = solve_layout(problem, candidates)
        reverse = solve_layout(problem, reversed(candidates))
        self.assertEqual(forward.candidate.candidate_id, "menu:full")
        self.assertEqual(forward.candidate.candidate_id, reverse.candidate.candidate_id)
        self.assertEqual(forward.score, reverse.score)
        self.assertEqual(
            json.dumps(forward.to_manifest(), sort_keys=True),
            json.dumps(reverse.to_manifest(), sort_keys=True),
        )

    def test_missing_font10_glyph_fails_before_candidate_generation(self) -> None:
        digits_only = FontMetrics.from_pairs(
            (
                (ord(character), 5)
                for character in "0123456789/"
            ),
            line_height=12,
        )
        with self.assertRaises(MissingGlyphError):
            menu_screen(
                160,
                128,
                (item("sword", "剑", 1),),
                font_metrics=digits_only,
                title="",
            )

    def test_changed_return_value_or_item_order_is_rejected(self) -> None:
        original_items = entries(3)
        spec = menu_screen(
            160, 128, original_items, font_metrics=TEST_FONT
        )
        problem = spec.problem()
        candidate = candidates_for_screen(spec)[0]
        page = candidate.pages[0]
        changed_elements = list(page.elements)
        semantic_position = next(
            index
            for index, element in enumerate(changed_elements)
            if element.semantic_index == 1
        )
        changed_elements[semantic_position] = replace(
            changed_elements[semantic_position],
            return_value=0x7FFF,
        )
        changed = replace(
            candidate,
            pages=(LayoutPage(0, tuple(changed_elements)),),
        )
        evaluation = evaluate_candidate(problem, changed)
        self.assertFalse(evaluation.valid)
        self.assertTrue(
            any("functional item" in violation for violation in evaluation.violations)
        )

        swapped = list(changed_elements)
        first = next(i for i, value in enumerate(swapped) if value.semantic_index == 0)
        second = next(i for i, value in enumerate(swapped) if value.semantic_index == 1)
        swapped[first] = replace(swapped[first], reading_order=1)
        swapped[second] = replace(swapped[second], reading_order=0)
        evaluation = evaluate_candidate(
            problem,
            replace(candidate, pages=(LayoutPage(0, tuple(swapped)),)),
        )
        self.assertFalse(evaluation.valid)
        self.assertIn(
            "functional item order/coverage changed",
            evaluation.violations,
        )


class ScreenSemanticsTests(unittest.TestCase):
    def test_all_five_candidate_families_exist_in_fixed_order(self) -> None:
        spec = menu_screen(
            240, 135, entries(3), font_metrics=TEST_FONT
        )
        self.assertEqual(
            tuple(candidate.mode for candidate in candidates_for_screen(spec)),
            (
                LayoutMode.FULL,
                LayoutMode.COMPACT,
                LayoutMode.SINGLE_COLUMN,
                LayoutMode.PAGED,
                LayoutMode.TEXT_ONLY,
            ),
        )
        self.assertEqual(
            {candidate.font_px for candidate in candidates_for_screen(spec)},
            {10},
        )

    def test_paged_menu_preserves_order_values_and_selected_page(self) -> None:
        original = entries(18, value_base=0x200)
        spec = menu_screen(
            160,
            128,
            original,
            font_metrics=TEST_FONT,
            selected_key="entry_17",
        )
        decision = solve_screen(spec)
        self.assertIsInstance(decision, LayoutDecision)
        self.assertEqual(decision.candidate.mode, LayoutMode.PAGED)
        self.assertGreater(len(decision.candidate.pages), 1)
        self.assertEqual(
            semantic_trace(decision.candidate),
            [(entry.key, entry.return_value) for entry in original],
        )
        initial_keys = {
            element.key
            for element in decision.candidate.pages[
                decision.candidate.initial_page
            ].elements
        }
        self.assertIn("entry_17", initial_keys)
        self.assertEqual(decision.candidate.font_px, 10)

    def test_dialog_wraps_pages_without_covering_player_focus(self) -> None:
        source_lines = (
            "这是第一段需要在小屏幕上确定性换行的对话。",
            "第二段也必须完整保留。",
        )
        choices = (
            item("yes", "确定", 7),
            item("no", "取消", 9),
        )
        focus = Rect(68, 50, 24, 28)
        spec = dialog_screen(
            160,
            128,
            source_lines,
            choices,
            font_metrics=TEST_FONT,
            title="李逍遥",
            focus=focus,
            selected_key="no",
        )
        decision = solve_screen(spec)
        self.assertIn(
            decision.candidate.mode,
            (LayoutMode.PAGED, LayoutMode.TEXT_ONLY),
        )
        self.assertGreater(len(decision.candidate.pages), 1)
        self.assertEqual(
            semantic_trace(decision.candidate),
            [("yes", 7), ("no", 9)],
        )
        for page in decision.candidate.pages:
            for element in page.elements:
                if element.blocks_focus:
                    self.assertFalse(element.rect.overlaps(focus))

    def test_item_magic_status_and_equip_have_small_screen_paging(self) -> None:
        constructors = (
            item_screen,
            magic_screen,
            status_screen,
            equip_screen,
        )
        roles = ("item", "magic", "stat", "equip")
        for constructor, role in zip(constructors, roles):
            with self.subTest(screen=constructor.__name__):
                original = entries(50, prefix=role, role=role)
                decision = solve_screen(
                    constructor(
                        160,
                        128,
                        original,
                        font_metrics=TEST_FONT,
                        selected_key=f"{role}_49",
                    )
                )
                self.assertIn(
                    decision.candidate.mode,
                    (LayoutMode.PAGED, LayoutMode.TEXT_ONLY),
                )
                self.assertGreater(len(decision.candidate.pages), 1)
                self.assertEqual(
                    semantic_trace(decision.candidate),
                    [(entry.key, entry.return_value) for entry in original],
                )
                self.assertEqual(decision.candidate.font_px, 10)

    def test_status_fields_are_not_focusable_and_have_value_slots(self) -> None:
        fields = (
            item("party", "李逍遥", None, role="party_selector"),
            item(
                "hp",
                "项",
                None,
                role="stat_label",
                selectable=False,
                value_sample="999/999",
            ),
        )
        decision = solve_screen(
            status_screen(
                160,
                128,
                fields,
                font_metrics=TEST_FONT,
                selected_key="party",
            )
        )
        manifest = decision.to_manifest()
        self.assertEqual(manifest["focus_order"], ["party"])
        self.assertEqual(
            [
                row["key"]
                for row in manifest["return_values"]
            ],
            ["party"],
        )
        elements = {
            element["name"]: element
            for element in manifest["elements"]
        }
        self.assertFalse(elements["hp"]["selectable"])
        self.assertEqual(elements["hp:value"]["text"], "999/999")
        self.assertFalse(elements["hp:value"]["selectable"])
        self.assertFalse(
            Rect(*elements["hp"]["rect"]).overlaps(
                Rect(*elements["hp:value"]["rect"])
            )
        )

    def test_simple_menu_keeps_richer_variant_when_it_fits(self) -> None:
        decision = solve_screen(
            menu_screen(
                240, 135, entries(6), font_metrics=TEST_FONT
            )
        )
        self.assertEqual(decision.candidate.mode, LayoutMode.FULL)
        self.assertEqual(len(decision.candidate.pages), 1)


class AssetPolicyTests(unittest.TestCase):
    def test_portrait_downsampling_is_visible_and_manifested(self) -> None:
        portrait = AssetSpec(
            "face_72",
            "portrait",
            72,
            72,
            min_scale_q16=Q16_ONE // 2,
        )
        spec = dialog_screen(
            240,
            135,
            ("你好。",),
            font_metrics=TEST_FONT,
            title="赵灵儿",
            focus=Rect(100, 70, 40, 30),
            portrait=portrait,
        )
        decision = solve_screen(spec)
        policy = decision.candidate.asset_policies[0]
        self.assertTrue(policy.visible)
        self.assertEqual(policy.space, AssetSpace.UI)
        self.assertEqual(policy.scale_q16, Q16_ONE // 2)
        self.assertEqual(policy.sampling, SamplingPolicy.NEAREST_CENTER)
        self.assertEqual(policy.destination_width, 36)
        self.assertEqual(policy.destination_height, 36)
        self.assertEqual(policy.step_x_q16, Q16_ONE * 2)
        self.assertEqual(policy.phase_x_q16, Q16_ONE)

        manifest = decision.to_manifest()
        self.assertEqual(manifest["screen_id"], "dialog")
        self.assertEqual(manifest["variant"], "full")
        self.assertEqual(manifest["candidate_id"], "dialog:full")
        self.assertIsInstance(manifest["reasons"], list)
        emitted = manifest["asset_scale_policies"][0]
        self.assertEqual(emitted["scale_q16"], Q16_ONE // 2)
        self.assertEqual(emitted["sampling"], "nearest_center")
        self.assertEqual(emitted["filter"], "nearest_center")
        self.assertEqual((emitted["numerator"], emitted["denominator"]), (1, 2))
        self.assertEqual(emitted["source"], [72, 72])
        self.assertEqual(emitted["destination_size"], [36, 36])

    def test_battle_sprite_and_effect_share_stage_scale_per_screen(self) -> None:
        stage_assets = (
            AssetSpec(
                "hero",
                "battle_sprite",
                64,
                80,
                critical=True,
                min_scale_q16=Q16_ONE // 4,
                scale_group="battle_stage",
            ),
            AssetSpec(
                "fire",
                "battle_effect",
                96,
                96,
                critical=True,
                min_scale_q16=Q16_ONE // 4,
                scale_group="battle_stage",
            ),
        )
        commands = entries(
            8,
            prefix="battle",
            label_prefix="令",
            value_base=0x400,
        )
        cases = (
            (240, 135, Rect(150, 78, 45, 45), 44_236),
            (160, 128, Rect(95, 72, 45, 45), 32_768),
        )
        for width, height, focus, expected_scale in cases:
            with self.subTest(viewport=(width, height)):
                decision = solve_screen(
                    battle_hud_screen(
                        width,
                        height,
                        commands,
                        font_metrics=TEST_FONT,
                        player_focus=focus,
                        selected_key="battle_7",
                        stage_assets=stage_assets,
                    )
                )
                policies = decision.candidate.asset_policies
                self.assertEqual(
                    [policy.scale_q16 for policy in policies],
                    [expected_scale, expected_scale],
                )
                self.assertTrue(all(policy.visible for policy in policies))
                self.assertTrue(
                    all(
                        policy.space is AssetSpace.LEGACY_STAGE
                        for policy in policies
                    )
                )
                self.assertTrue(
                    all(
                        policy.sampling is SamplingPolicy.NEAREST_CENTER
                        for policy in policies
                    )
                )
                for page in decision.candidate.pages:
                    for element in page.elements:
                        if element.blocks_focus:
                            self.assertFalse(element.rect.overlaps(focus))

    def test_optional_art_has_explicit_omitted_policy_in_text_only(self) -> None:
        preview = AssetSpec("item_preview", "item_preview", 64, 64)
        spec = item_screen(
            160,
            128,
            entries(20),
            font_metrics=TEST_FONT,
            preview=preview,
        )
        text_only = candidates_for_screen(spec)[-1]
        self.assertEqual(text_only.mode, LayoutMode.TEXT_ONLY)
        self.assertEqual(len(text_only.asset_policies), 1)
        policy = text_only.asset_policies[0]
        self.assertFalse(policy.visible)
        self.assertEqual(policy.scale_q16, 0)
        self.assertEqual(policy.sampling, SamplingPolicy.NONE)
        self.assertEqual(policy.to_manifest()["space"], "omitted")

    def test_tampered_fixed_point_asset_parameters_are_rejected(self) -> None:
        asset = AssetSpec(
            "hero",
            "battle_sprite",
            64,
            80,
            critical=True,
            min_scale_q16=Q16_ONE // 4,
            scale_group="battle_stage",
        )
        spec = battle_hud_screen(
            240,
            135,
            entries(2),
            font_metrics=TEST_FONT,
            player_focus=Rect(150, 78, 40, 40),
            stage_assets=(asset,),
        )
        problem = spec.problem()
        candidate = candidates_for_screen(spec)[0]
        policy = candidate.asset_policies[0]
        changed_policy = replace(policy, phase_x_q16=policy.phase_x_q16 + 1)
        changed = replace(candidate, asset_policies=(changed_policy,))
        evaluation = evaluate_candidate(problem, changed)
        self.assertFalse(evaluation.valid)
        self.assertTrue(
            any(
                "fixed-point sampling parameters changed" in violation
                for violation in evaluation.violations
            )
        )


class ManifestShapeTests(unittest.TestCase):
    def test_manifest_contains_stable_integration_fields(self) -> None:
        original = entries(12, value_base=500)
        decision = solve_screen(
            menu_screen(
                160,
                128,
                original,
                font_metrics=TEST_FONT,
                screen_id="system_menu",
                selected_key="entry_8",
            )
        )
        manifest = decision.to_manifest()
        self.assertEqual(
            set(
                (
                    "screen_id",
                    "variant",
                    "reasons",
                    "elements",
                    "pages",
                    "focus",
                    "return_values",
                    "asset_scale_policies",
                )
            )
            - set(manifest),
            set(),
        )
        self.assertEqual(
            [
                (entry["key"], entry["return_value"])
                for entry in manifest["return_values"]
            ],
            [(entry.key, entry.return_value) for entry in original],
        )
        self.assertEqual(manifest["name"], "system_menu")
        self.assertEqual(
            manifest["focus_order"],
            [entry.key for entry in original],
        )
        self.assertTrue(
            all(
                "name" in element
                and "kind" in element
                and "selectable" in element
                for element in manifest["elements"]
            )
        )
        encoded_once = json.dumps(
            manifest,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        encoded_twice = json.dumps(
            solve_screen(
                menu_screen(
                    160,
                    128,
                    original,
                    font_metrics=TEST_FONT,
                    screen_id="system_menu",
                    selected_key="entry_8",
                )
            ).to_manifest(),
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        self.assertEqual(encoded_once, encoded_twice)

    def test_decision_manifest_plugs_into_generated_header(self) -> None:
        portrait = AssetSpec(
            "face",
            "portrait",
            64,
            64,
            min_scale_q16=Q16_ONE // 2,
        )
        decision = solve_screen(
            dialog_screen(
                240,
                135,
                ("你好。",),
                (item("ok", "确定", 7),),
                font_metrics=TEST_FONT,
                title="赵灵儿",
                portrait=portrait,
            )
        )
        screen = decision.to_manifest()
        profile = {
            "schema_version": 1,
            "name": "solver-integration",
            "display": {"width": 240, "height": 135},
            "safe_rect": [0, 0, 240, 135],
            "stage_rect": [12, 0, 216, 135],
            "stage_source": [320, 200],
            "player_anchor": [120, 67],
            "font": screen["font_metrics"],
            "loading": loading_layout(DisplayProfile(240, 135)),
            "sampling": screen["asset_scale_policies"],
            "screens": [screen],
            "camera_vectors": [],
        }
        header = emit_profile_header(profile)
        self.assertIn("PAL_UI_SCREEN_DIALOG", header)
        self.assertIn("PAL_UI_SAMPLE_DIALOG_FACE", header)
        self.assertIn("PAL_UI_VARIANT_FULL", header)


if __name__ == "__main__":
    unittest.main()
