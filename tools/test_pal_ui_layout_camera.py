#!/usr/bin/env python3
"""Tests for deterministic small-display geometry and PAL camera policy."""

from __future__ import annotations

from dataclasses import fields, is_dataclass
import unittest

try:
    from pal_ui_layout.camera import (
        BATTLE_CAMERA_C_TEST_VECTORS,
        MAP_CAMERA_C_TEST_VECTORS,
        ONE_TO_ONE,
        BattleCameraPolicy,
        CameraFixedParams,
        Point,
        Rect,
        Size,
        UniformScale,
        battle_camera,
        battle_camera_policy,
        camera_c_test_vectors,
        camera_vectors_for_profile,
        default_battle_camera_policy,
        map_camera,
        map_camera_from_legacy,
        profile_camera_constants,
        stage_scale,
    )
    from pal_ui_layout.geometry import Insets, flow, grid, safe_area
    from pal_ui_layout.emit_c import emit_profile_header
    from pal_ui_layout.profiles import (
        DisplayProfile,
        ProfileRect,
        loading_layout,
    )
except ModuleNotFoundError:
    from tools.pal_ui_layout.camera import (
        BATTLE_CAMERA_C_TEST_VECTORS,
        MAP_CAMERA_C_TEST_VECTORS,
        ONE_TO_ONE,
        BattleCameraPolicy,
        CameraFixedParams,
        Point,
        Rect,
        Size,
        UniformScale,
        battle_camera,
        battle_camera_policy,
        camera_c_test_vectors,
        camera_vectors_for_profile,
        default_battle_camera_policy,
        map_camera,
        map_camera_from_legacy,
        profile_camera_constants,
        stage_scale,
    )
    from tools.pal_ui_layout.geometry import Insets, flow, grid, safe_area
    from tools.pal_ui_layout.emit_c import emit_profile_header
    from tools.pal_ui_layout.profiles import (
        DisplayProfile,
        ProfileRect,
        loading_layout,
    )


class GeometryTests(unittest.TestCase):
    def test_rect_and_profile_rect_are_structurally_compatible(self) -> None:
        profile_rect = ProfileRect(3, 5, 17, 19)
        rect = Rect.from_rect_like(profile_rect)
        self.assertEqual(rect, Rect(3, 5, 17, 19))
        self.assertEqual((rect.width, rect.height), (17, 19))
        self.assertEqual(rect.right, 20)
        self.assertEqual(rect.bottom, 24)
        self.assertTrue(Rect(0, 0, 32, 32).contains_rect(rect))

    def test_safe_area_rejects_consuming_insets(self) -> None:
        self.assertEqual(
            safe_area(240, 135, Insets(2, 3, 4, 5)),
            Rect(2, 3, 234, 127),
        )
        with self.assertRaises(ValueError):
            safe_area(10, 10, Insets(6, 0, 5, 0))

    def test_horizontal_flow_wraps_and_preserves_item_order(self) -> None:
        result = flow(
            Rect(0, 0, 30, 30),
            (Size(12, 5), Size(12, 7), Size(12, 4)),
            gap=2,
            line_gap=3,
            wrap=True,
            align="center",
        )
        self.assertEqual(
            result.rects,
            (
                Rect(0, 1, 12, 5),
                Rect(14, 0, 12, 7),
                Rect(0, 10, 12, 4),
            ),
        )
        self.assertTrue(result.fits)

    def test_flow_space_between_distributes_remainder_left_first(self) -> None:
        result = flow(
            Rect(0, 0, 31, 8),
            (Size(5, 8), Size(5, 8), Size(5, 8)),
            gap=1,
            wrap=False,
            justify="space-between",
        )
        self.assertEqual(
            tuple(rect.x for rect in result.rects),
            (0, 13, 26),
        )
        self.assertTrue(result.fits)

    def test_flow_reports_oversized_items_without_dropping_them(self) -> None:
        result = flow(
            Rect(0, 0, 10, 10),
            (Size(11, 3), Size(4, 12)),
            gap=1,
        )
        self.assertEqual(len(result.rects), 2)
        self.assertEqual(result.overflow_indices, (0, 1))

    def test_grid_distributes_track_remainders_deterministically(self) -> None:
        result = grid(
            Rect(0, 0, 10, 7),
            6,
            columns=3,
            rows=2,
            gap_x=1,
            gap_y=1,
        )
        self.assertEqual(
            result.rects,
            (
                Rect(0, 0, 3, 3),
                Rect(4, 0, 3, 3),
                Rect(8, 0, 2, 3),
                Rect(0, 4, 3, 3),
                Rect(4, 4, 3, 3),
                Rect(8, 4, 2, 3),
            ),
        )
        self.assertTrue(result.fits)

    def test_fixed_cell_grid_can_be_centered(self) -> None:
        result = grid(
            Rect(0, 0, 20, 12),
            4,
            columns=2,
            rows=2,
            gap_x=2,
            gap_y=2,
            cell_size=Size(4, 3),
            horizontal_align="center",
            vertical_align="center",
        )
        self.assertEqual(result.rects[0], Rect(5, 2, 4, 3))
        self.assertEqual(result.rects[-1], Rect(11, 7, 4, 3))
        self.assertTrue(result.fits)


class ScaleAndProfileTests(unittest.TestCase):
    def test_certified_stage_scales_are_uniform_and_exact(self) -> None:
        profile_240 = DisplayProfile(240, 135)
        profile_160 = DisplayProfile(160, 128)
        self.assertEqual(stage_scale(profile_240), UniformScale(27, 40))
        self.assertEqual(stage_scale(profile_160), UniformScale(1, 2))

        constants_240 = profile_camera_constants(profile_240)
        constants_160 = profile_camera_constants(profile_160)
        self.assertEqual(constants_240.stage_rect, Rect(12, 0, 216, 135))
        self.assertEqual(constants_160.stage_rect, Rect(0, 14, 160, 100))
        self.assertEqual(constants_240.player_anchor, Point(120, 67))
        self.assertEqual(constants_160.player_anchor, Point(80, 64))

    def test_uniform_scale_rejects_axis_distortion(self) -> None:
        self.assertEqual(
            UniformScale.from_axes(2, 4, 3, 6),
            UniformScale(1, 2),
        )
        with self.assertRaisesRegex(ValueError, "non-uniform"):
            UniformScale.from_axes(3, 4, 2, 3)

    def test_fixed_coefficients_match_nearest_center_sampler_contract(self) -> None:
        scale = UniformScale(27, 40)
        self.assertEqual(scale.q16, 44_236)
        self.assertEqual(scale.source_step_q16, 97_090)
        self.assertEqual(scale.source_phase_q16, 48_545)

        fixed = profile_camera_constants(DisplayProfile(240, 135)).stage_fixed
        self.assertEqual(
            fixed,
            CameraFixedParams(
                source=Rect(0, 0, 320, 200),
                destination=Rect(12, 0, 216, 135),
                scale_numerator=27,
                scale_denominator=40,
                scale_q16=44_236,
                source_step_q16=97_090,
                source_phase_q16=48_545,
            ),
        )


class BattleCameraPolicyTests(unittest.TestCase):
    def test_certified_profile_policy_geometry_is_literal_and_hud_free(self) -> None:
        cases = (
            (
                DisplayProfile(240, 135),
                Rect(4, 4, 232, 30),
                Rect(0, 34, 240, 101),
                Size(240, 101),
                Rect(0, 34, 240, 101),
                UniformScale(1, 2),
                Rect(40, 34, 160, 100),
            ),
            (
                DisplayProfile(160, 128),
                Rect(4, 4, 152, 30),
                Rect(0, 34, 160, 94),
                Size(160, 94),
                Rect(0, 34, 160, 94),
                UniformScale(9, 20),
                Rect(8, 36, 144, 90),
            ),
        )
        for (
            profile,
            expected_hud,
            expected_content,
            expected_focus_source,
            expected_focus_screen,
            expected_fit_scale,
            expected_fit_screen,
        ) in cases:
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                self.assertIsInstance(policy, BattleCameraPolicy)
                self.assertEqual(policy.arena, Rect(0, 0, 320, 200))
                self.assertEqual(policy.hud_rect, expected_hud)
                self.assertEqual(policy.content_rect, expected_content)
                self.assertEqual(policy.focus_source, expected_focus_source)
                self.assertEqual(policy.focus_screen, expected_focus_screen)
                self.assertEqual(policy.fit_scale, expected_fit_scale)
                self.assertEqual(policy.fit_screen, expected_fit_screen)
                self.assertEqual(policy.padding, 4)
                self.assertEqual(policy.max_players, 3)
                self.assertFalse(policy.hud_rect.intersects(policy.content_rect))
                self.assertTrue(
                    policy.content_rect.contains_rect(policy.focus_screen)
                )
                self.assertTrue(
                    policy.content_rect.contains_rect(policy.fit_screen)
                )

                # The public constructor must reproduce the certified default
                # exactly; no hidden profile-specific target calculation is
                # allowed to replace these generated fields.
                self.assertEqual(
                    battle_camera_policy(profile, expected_hud),
                    policy,
                )
                self.assertEqual(
                    profile_camera_constants(profile).battle_policy,
                    policy,
                )

    def test_fit_sample_tables_are_exact_nearest_center_maps(self) -> None:
        for profile in (DisplayProfile(240, 135), DisplayProfile(160, 128)):
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                manifest = policy.as_manifest()
                sample_x = manifest["fit_sample_x"]
                sample_y = manifest["fit_sample_y"]
                self.assertIsInstance(sample_x, list)
                self.assertIsInstance(sample_y, list)
                self.assertEqual(len(sample_x), policy.fit_screen.w)
                self.assertEqual(len(sample_y), policy.fit_screen.h)

                expected_x = [
                    (index * 2 + 1) * policy.arena.w
                    // (policy.fit_screen.w * 2)
                    for index in range(policy.fit_screen.w)
                ]
                expected_y = [
                    (index * 2 + 1) * policy.arena.h
                    // (policy.fit_screen.h * 2)
                    for index in range(policy.fit_screen.h)
                ]
                rational_x = [
                    (index * 2 + 1) * policy.fit_scale.denominator
                    // (policy.fit_scale.numerator * 2)
                    for index in range(policy.fit_screen.w)
                ]
                rational_y = [
                    (index * 2 + 1) * policy.fit_scale.denominator
                    // (policy.fit_scale.numerator * 2)
                    for index in range(policy.fit_screen.h)
                ]
                self.assertEqual(sample_x, expected_x)
                self.assertEqual(sample_y, expected_y)
                self.assertEqual(sample_x, rational_x)
                self.assertEqual(sample_y, rational_y)
                self.assertEqual(sample_x, sorted(sample_x))
                self.assertEqual(sample_y, sorted(sample_y))
                self.assertGreaterEqual(sample_x[0], 0)
                self.assertGreaterEqual(sample_y[0], 0)
                self.assertLess(sample_x[-1], policy.arena.w)
                self.assertLess(sample_y[-1], policy.arena.h)


class MapCameraTests(unittest.TestCase):
    def test_shared_map_vectors(self) -> None:
        for vector in MAP_CAMERA_C_TEST_VECTORS:
            with self.subTest(vector=vector.name):
                view = map_camera_from_legacy(
                    DisplayProfile(vector.profile_width, vector.profile_height),
                    vector.world_bounds,
                    vector.canonical_viewport,
                    vector.party_offset,
                    extra_script_offset=vector.extra_script_offset,
                    downsample_stage=vector.downsample_stage,
                )
                self.assertEqual(view.world_rect, vector.expected_world)
                self.assertEqual(view.screen_rect, vector.expected_screen)
                self.assertEqual(view.scale, vector.expected_scale)
                self.assertEqual(
                    view.desired_origin, vector.expected_desired_origin
                )
                self.assertEqual(
                    view.was_clamped, vector.expected_was_clamped
                )
                manifest = vector.as_c_manifest()
                self.assertEqual(
                    view.world_to_screen(tuple(manifest["focus"])),
                    vector.expected_projected_focus,
                )
                self.assertEqual(
                    manifest["projected_focus"],
                    [
                        vector.expected_projected_focus.x,
                        vector.expected_projected_focus.y,
                    ],
                )

    def test_legacy_script_counter_motion_keeps_world_state_immutable(self) -> None:
        profile = DisplayProfile(240, 135)
        bounds = Rect(0, 0, 2048, 2048)
        before = map_camera_from_legacy(
            profile,
            bounds,
            canonical_viewport=Point(500, 400),
            party_offset=Point(160, 112),
        )
        during_pan = map_camera_from_legacy(
            profile,
            bounds,
            canonical_viewport=Point(510, 400),
            party_offset=Point(150, 112),
        )

        canonical_player_before = Point(500 + 160, 400 + 112)
        canonical_player_during = Point(510 + 150, 400 + 112)
        self.assertEqual(canonical_player_before, canonical_player_during)
        self.assertEqual(
            during_pan.world_rect.x - before.world_rect.x,
            10,
        )
        self.assertEqual(
            during_pan.world_to_screen(canonical_player_during).x
            - before.world_to_screen(canonical_player_before).x,
            -10,
        )

    def test_player_center_and_edge_clamp(self) -> None:
        profile = DisplayProfile(160, 128)
        centered = map_camera(
            profile,
            Rect(0, 0, 1000, 1000),
            Point(400, 300),
        )
        self.assertEqual(centered.world_to_screen(Point(400, 300)), Point(80, 64))
        self.assertFalse(centered.was_clamped)

        edge = map_camera(
            profile,
            Rect(0, 0, 640, 400),
            Point(20, 20),
        )
        self.assertEqual(edge.world_rect, Rect(0, 0, 160, 128))
        self.assertEqual(edge.world_to_screen(Point(20, 20)), Point(20, 20))
        self.assertTrue(edge.was_clamped)

    def test_stage_downsample_keeps_source_assets_in_canonical_space(self) -> None:
        view = map_camera(
            DisplayProfile(240, 135),
            Rect(0, 0, 2048, 2048),
            Point(660, 512),
            downsample_stage=True,
        )
        self.assertEqual(view.mode, "map_stage")
        self.assertEqual(view.world_rect.size, Size(320, 200))
        self.assertEqual(view.scale, UniformScale(27, 40))
        self.assertEqual(view.screen_rect, Rect(12, 0, 216, 135))
        self.assertEqual(view.world_to_screen(Point(660, 512)), Point(120, 67))


class BattleCameraTests(unittest.TestCase):
    def assert_subjects_visible(
        self,
        policy: BattleCameraPolicy,
        view: object,
        subjects: tuple[Rect, ...],
    ) -> None:
        camera = view
        for subject in subjects:
            visible = subject.intersection(policy.arena)
            self.assertFalse(visible.is_empty)
            self.assertTrue(camera.world_rect.contains_rect(visible))
            projected = camera.project_rect(visible)
            self.assertTrue(camera.screen_rect.contains_rect(projected))
            self.assertTrue(policy.content_rect.contains_rect(projected))
            self.assertFalse(policy.hud_rect.intersects(projected))

    def test_shared_battle_vectors(self) -> None:
        for vector in BATTLE_CAMERA_C_TEST_VECTORS:
            with self.subTest(vector=vector.name):
                view = battle_camera(
                    DisplayProfile(vector.profile_width, vector.profile_height),
                    vector.arena,
                    players=(vector.player,),
                    actor=vector.actor,
                    target=vector.target,
                    padding=vector.padding,
                )
                self.assertEqual(view.mode, vector.expected_mode)
                self.assertEqual(view.world_rect, vector.expected_world)
                self.assertEqual(view.screen_rect, vector.expected_screen)
                self.assertEqual(view.scale, vector.expected_scale)
                self.assertEqual(
                    view.desired_origin, vector.expected_desired_origin
                )
                self.assertEqual(
                    view.was_clamped, vector.expected_was_clamped
                )
                manifest = vector.as_c_manifest()
                self.assertEqual(
                    view.world_to_screen(tuple(manifest["focus"])),
                    vector.expected_projected_focus,
                )
                self.assertEqual(
                    manifest["projected_focus"],
                    [
                        vector.expected_projected_focus.x,
                        vector.expected_projected_focus.y,
                    ],
                )

    def test_close_live_subjects_use_hud_free_focus_crop_on_both_profiles(
        self,
    ) -> None:
        players = (Rect(180, 140, 20, 20),)
        actor = Rect(180, 140, 20, 20)
        target = Rect(90, 95, 24, 24)
        for profile in (DisplayProfile(240, 135), DisplayProfile(160, 128)):
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                view = battle_camera(
                    profile,
                    players=players,
                    actor=actor,
                    target=target,
                    policy=policy,
                )
                self.assertEqual(view.mode, "battle_focus")
                self.assertEqual(view.scale, ONE_TO_ONE)
                self.assertEqual(view.screen_rect, policy.focus_screen)
                self.assert_subjects_visible(
                    policy,
                    view,
                    (*players, actor, target),
                )

    def test_far_live_subjects_use_hud_free_fit_all_on_both_profiles(
        self,
    ) -> None:
        players = (Rect(230, 145, 30, 30),)
        actor = players[0]
        target = Rect(25, 90, 30, 30)
        for profile in (DisplayProfile(240, 135), DisplayProfile(160, 128)):
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                view = battle_camera(
                    profile,
                    players=players,
                    actor=actor,
                    target=target,
                    policy=policy,
                )
                self.assertEqual(view.mode, "battle_fit_all")
                self.assertEqual(view.world_rect, policy.arena)
                self.assertEqual(view.screen_rect, policy.fit_screen)
                self.assertEqual(view.scale, policy.fit_scale)
                self.assert_subjects_visible(
                    policy,
                    view,
                    (*players, actor, target),
                )

    def test_focus_extent_boundary_is_inclusive_then_falls_back(self) -> None:
        for profile in (DisplayProfile(240, 135), DisplayProfile(160, 128)):
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                exact = Rect(
                    40,
                    50,
                    policy.focus_source.w - policy.padding * 2,
                    policy.focus_source.h - policy.padding * 2,
                )
                exact_view = battle_camera(
                    profile,
                    players=(exact,),
                    policy=policy,
                )
                self.assertEqual(exact_view.mode, "battle_focus")
                self.assertTrue(
                    exact_view.world_rect.contains_rect(
                        exact.expanded(policy.padding)
                    )
                )

                one_pixel_too_wide = Rect(
                    exact.x,
                    exact.y,
                    exact.w + 1,
                    exact.h,
                )
                fallback = battle_camera(
                    profile,
                    players=(one_pixel_too_wide,),
                    policy=policy,
                )
                self.assertEqual(fallback.mode, "battle_fit_all")
                self.assertEqual(fallback.screen_rect, policy.fit_screen)

    def test_team_focus_is_order_invariant_and_primary_focus_is_explicit(
        self,
    ) -> None:
        players = (
            Rect(140, 100, 20, 20),
            Rect(180, 120, 20, 20),
        )
        team_center = Rect(140, 100, 60, 40).center
        for profile in (DisplayProfile(240, 135), DisplayProfile(160, 128)):
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                team = battle_camera(
                    profile,
                    players=players,
                    primary_player=None,
                    policy=policy,
                )
                reversed_team = battle_camera(
                    profile,
                    players=tuple(reversed(players)),
                    primary_player=None,
                    policy=policy,
                )
                self.assertEqual(team, reversed_team)
                self.assertEqual(
                    team.world_to_screen(team_center),
                    policy.focus_screen.center,
                )

                primary = battle_camera(
                    profile,
                    players=players,
                    primary_player=0,
                    policy=policy,
                )
                self.assertEqual(primary.mode, "battle_focus")
                self.assertEqual(
                    primary.world_to_screen(players[0].center),
                    policy.focus_screen.center,
                )
                self.assertNotEqual(primary.world_rect, team.world_rect)
                self.assert_subjects_visible(policy, primary, players)

    def test_partial_arena_edge_is_clipped_and_outside_subject_is_rejected(
        self,
    ) -> None:
        partial = Rect(-10, -8, 24, 20)
        for profile in (DisplayProfile(240, 135), DisplayProfile(160, 128)):
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                view = battle_camera(
                    profile,
                    players=(partial,),
                    policy=policy,
                )
                self.assertEqual(view.mode, "battle_focus")
                self.assertEqual(view.world_rect.x, 0)
                self.assertEqual(view.world_rect.y, 0)
                self.assert_subjects_visible(policy, view, (partial,))

                with self.assertRaisesRegex(ValueError, "outside"):
                    battle_camera(
                        profile,
                        players=(Rect(-30, -30, 10, 10),),
                        policy=policy,
                    )

    def test_force_fit_all_bypasses_an_otherwise_valid_focus_crop(self) -> None:
        players = (Rect(180, 100, 20, 20),)
        target = Rect(130, 90, 20, 20)
        for profile in (DisplayProfile(240, 135), DisplayProfile(160, 128)):
            with self.subTest(profile=profile.name):
                policy = default_battle_camera_policy(profile)
                ordinary = battle_camera(
                    profile,
                    players=players,
                    target=target,
                    policy=policy,
                )
                forced = battle_camera(
                    profile,
                    players=players,
                    target=target,
                    force_fit_all=True,
                    policy=policy,
                )
                self.assertEqual(ordinary.mode, "battle_focus")
                self.assertEqual(forced.mode, "battle_fit_all")
                self.assertEqual(forced.world_rect, policy.arena)
                self.assertEqual(forced.screen_rect, policy.fit_screen)
                self.assert_subjects_visible(
                    policy,
                    forced,
                    (*players, target),
                )

    def test_focus_crop_contains_players_actor_and_target(self) -> None:
        profile = DisplayProfile(240, 135)
        players = (
            Rect(180, 140, 20, 24),
            Rect(205, 130, 20, 24),
            Rect(225, 120, 20, 24),
        )
        actor = players[0]
        target = Rect(90, 95, 24, 24)
        view = battle_camera(
            profile,
            players=players,
            actor=actor,
            target=target,
            padding=4,
        )
        self.assertEqual(view.mode, "battle_focus")
        required = target.expanded(4)
        for rect in (*players, actor, target, required):
            self.assertTrue(view.world_rect.contains_rect(rect))
        self.assertEqual(view.scale, ONE_TO_ONE)

    def test_wide_actor_target_pair_falls_back_to_full_uniform_stage(self) -> None:
        for profile, expected_screen, expected_scale in (
            (
                DisplayProfile(240, 135),
                Rect(40, 34, 160, 100),
                UniformScale(1, 2),
            ),
            (
                DisplayProfile(160, 128),
                Rect(8, 36, 144, 90),
                UniformScale(9, 20),
            ),
        ):
            with self.subTest(profile=profile.name):
                view = battle_camera(
                    profile,
                    players=(Rect(230, 145, 30, 30),),
                    actor=Rect(230, 145, 30, 30),
                    target=Rect(25, 90, 30, 30),
                )
                self.assertEqual(view.mode, "battle_fit_all")
                self.assertEqual(view.world_rect, Rect(0, 0, 320, 200))
                self.assertEqual(view.screen_rect, expected_screen)
                self.assertEqual(view.scale, expected_scale)
                self.assertTrue(
                    view.screen_rect.contains_rect(view.project_rect(view.world_rect))
                )

    def test_player_group_is_primary_focus_when_no_action_pair(self) -> None:
        players = (
            Rect(170, 130, 20, 20),
            Rect(210, 150, 20, 20),
        )
        view = battle_camera(
            DisplayProfile(240, 135),
            players=players,
        )
        self.assertEqual(view.mode, "battle_focus")
        for player in players:
            self.assertTrue(view.world_rect.contains_rect(player))

    def test_actor_and_target_may_be_canonical_points(self) -> None:
        view = battle_camera(
            DisplayProfile(240, 135),
            players=(Point(210, 155),),
            actor=Point(210, 155),
            target=Point(100, 110),
        )
        self.assertEqual(view.mode, "battle_focus")
        self.assertTrue(view.world_rect.contains_point(Point(210, 155)))
        self.assertTrue(view.world_rect.contains_point(Point(100, 110)))


class SharedVectorContractTests(unittest.TestCase):
    def test_vector_accessor_returns_literal_shared_sets(self) -> None:
        maps, battles = camera_c_test_vectors()
        self.assertIs(maps, MAP_CAMERA_C_TEST_VECTORS)
        self.assertIs(battles, BATTLE_CAMERA_C_TEST_VECTORS)
        self.assertEqual(
            {vector.profile_width for vector in (*maps, *battles)},
            {160, 240},
        )

    def test_fixed_and_vector_payloads_contain_no_float(self) -> None:
        def walk(value: object) -> None:
            self.assertNotIsInstance(value, float)
            if is_dataclass(value):
                for field in fields(value):
                    walk(getattr(value, field.name))
            elif isinstance(value, (tuple, list)):
                for item in value:
                    walk(item)
            elif isinstance(value, dict):
                for item in value.values():
                    walk(item)

        maps, battles = camera_c_test_vectors()
        walk(maps)
        walk(battles)
        walk(profile_camera_constants(DisplayProfile(240, 135)))
        walk(profile_camera_constants(DisplayProfile(160, 128)))

    def test_vector_manifests_are_accepted_by_c_header_emitter(self) -> None:
        profile = DisplayProfile(240, 135)
        constants = profile_camera_constants(profile)
        manifest = {
            "schema_version": 1,
            "name": profile.name,
            "display": {"width": profile.width, "height": profile.height},
            "safe_rect": [
                constants.safe_rect.x,
                constants.safe_rect.y,
                constants.safe_rect.w,
                constants.safe_rect.h,
            ],
            "stage_rect": [
                constants.stage_rect.x,
                constants.stage_rect.y,
                constants.stage_rect.w,
                constants.stage_rect.h,
            ],
            "stage_source": [320, 200],
            "player_anchor": [
                constants.player_anchor.x,
                constants.player_anchor.y,
            ],
            "font": {
                "cell_width": 10,
                "cell_height": 10,
                "ascent": 9,
                "descent": 1,
                "line_height": 10,
            },
            "loading": loading_layout(profile),
            "sampling": [],
            "screens": [],
            "camera_vectors": list(camera_vectors_for_profile(profile)),
        }
        header = emit_profile_header(manifest)
        self.assertIn("PAL_UI_GENERATED_CAMERA_VECTOR_COUNT 4u", header)
        self.assertIn("/* 0:map:follow */", header)
        self.assertIn("/* 3:battle:fit_all */", header)


if __name__ == "__main__":
    unittest.main()
