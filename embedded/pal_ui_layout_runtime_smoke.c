#include "pal_ui_layout_runtime.h"

#include <stdio.h>

static int fail(const char *message)
{
    fprintf(stderr, "pal UI layout runtime: %s\n", message);
    return 1;
}

static bool same_rect(PalUiLayoutRect first, PalUiLayoutRect second)
{
    return first.x == second.x &&
           first.y == second.y &&
           first.width == second.width &&
           first.height == second.height;
}

static bool rect_contains(
    PalUiLayoutRect outer,
    PalUiLayoutRect inner)
{
    return inner.x >= outer.x &&
           inner.y >= outer.y &&
           (int32_t)inner.x + inner.width <=
               (int32_t)outer.x + outer.width &&
           (int32_t)inner.y + inner.height <=
               (int32_t)outer.y + outer.height;
}

static bool rect_intersects(
    PalUiLayoutRect first,
    PalUiLayoutRect second)
{
    return first.width != 0 && first.height != 0 &&
           second.width != 0 && second.height != 0 &&
           first.x < (int32_t)second.x + second.width &&
           second.x < (int32_t)first.x + first.width &&
           first.y < (int32_t)second.y + second.height &&
           second.y < (int32_t)first.y + first.height;
}

static bool same_camera(
    const PalUiLayoutCameraVector *first,
    const PalUiLayoutCameraVector *second)
{
    return same_rect(first->source, second->source) &&
           same_rect(first->screen, second->screen) &&
           first->focus_x == second->focus_x &&
           first->focus_y == second->focus_y &&
           first->camera_x == second->camera_x &&
           first->camera_y == second->camera_y &&
           first->desired_x == second->desired_x &&
           first->desired_y == second->desired_y &&
           first->projected_focus_x == second->projected_focus_x &&
           first->projected_focus_y == second->projected_focus_y &&
           first->scale_numerator == second->scale_numerator &&
           first->scale_denominator == second->scale_denominator &&
           first->fixed_q_shift == second->fixed_q_shift &&
           first->scale_q16 == second->scale_q16 &&
           first->source_step_q16 == second->source_step_q16 &&
           first->source_phase_q16 == second->source_phase_q16 &&
           first->kind == second->kind &&
           first->mode == second->mode &&
           first->flags == second->flags;
}

static bool find_camera(
    const PalUiLayoutProfile *profile,
    uint8_t kind,
    uint8_t mode,
    PalUiLayoutCameraVector *out)
{
    uint16_t index;
    for (index = 0; index < profile->camera_vector_count; index++) {
        PalUiLayoutCameraVector candidate;
        if (!PalUiLayout_GetCameraVector(index, &candidate)) {
            return false;
        }
        if (candidate.kind == kind && candidate.mode == mode) {
            *out = candidate;
            return true;
        }
    }
    return false;
}

static bool subject_is_hud_free(
    const PalUiLayoutBattleCameraPolicy *policy,
    const PalUiLayoutCameraVector *camera,
    PalUiLayoutRect subject)
{
    PalUiLayoutRect projected;
    return PalUiLayout_ProjectCameraRect(
               camera, subject, &projected) &&
           rect_contains(policy->content_rect, projected) &&
           !rect_intersects(policy->hud_rect, projected);
}

static bool check_live_battle_camera(
    const PalUiLayoutProfile *profile)
{
    PalUiLayoutBattleCameraPolicy policy;
    PalUiLayoutBattleCameraInput close = {0};
    PalUiLayoutBattleCameraInput far = {0};
    PalUiLayoutBattleCameraInput outside = {0};
    PalUiLayoutCameraVector close_camera;
    PalUiLayoutCameraVector far_camera;
    PalUiLayoutCameraVector expected_close;
    PalUiLayoutCameraVector expected_far;
    PalUiLayoutCameraVector forced;
    uint16_t source;

    if (!PalUiLayout_GetBattleCameraPolicy(&policy) ||
        policy.max_players != PAL_UI_LAYOUT_BATTLE_MAX_PLAYERS ||
        !rect_contains(profile->safe_rect, policy.content_rect) ||
        rect_intersects(policy.hud_rect, policy.content_rect) ||
        !PalUiLayout_BattleFitSourceX(0, &source) ||
        source >= policy.arena.width ||
        !PalUiLayout_BattleFitSourceY(0, &source) ||
        source >= policy.arena.height ||
        PalUiLayout_BattleFitSourceX(
            policy.fit_screen.width, &source) ||
        PalUiLayout_BattleFitSourceY(
            policy.fit_screen.height, &source)) {
        return false;
    }

    close.player_count = 1;
    close.primary_player = 0;
    close.flags =
        PAL_UI_LAYOUT_BATTLE_HAS_ACTOR |
        PAL_UI_LAYOUT_BATTLE_HAS_TARGET;
    close.players[0] =
        (PalUiLayoutRect){180, 140, 32, 28};
    close.actor = close.players[0];
    close.target =
        (PalUiLayoutRect){80, 100, 24, 24};
    if (!PalUiLayout_ResolveBattleCamera(&close, &close_camera) ||
        !find_camera(
            profile,
            PAL_UI_LAYOUT_CAMERA_BATTLE,
            PAL_UI_LAYOUT_CAMERA_ACTOR_TARGET,
            &expected_close) ||
        !same_camera(&close_camera, &expected_close) ||
        !subject_is_hud_free(
            &policy, &close_camera, close.players[0]) ||
        !subject_is_hud_free(
            &policy, &close_camera, close.actor) ||
        !subject_is_hud_free(
            &policy, &close_camera, close.target)) {
        return false;
    }

    far.player_count = 1;
    far.primary_player = 0;
    far.flags =
        PAL_UI_LAYOUT_BATTLE_HAS_ACTOR |
        PAL_UI_LAYOUT_BATTLE_HAS_TARGET;
    far.players[0] =
        (PalUiLayoutRect){230, 145, 30, 30};
    far.actor = far.players[0];
    far.target =
        (PalUiLayoutRect){25, 90, 30, 30};
    if (!PalUiLayout_ResolveBattleCamera(&far, &far_camera) ||
        !find_camera(
            profile,
            PAL_UI_LAYOUT_CAMERA_BATTLE,
            PAL_UI_LAYOUT_CAMERA_FIT_ALL,
            &expected_far) ||
        !same_camera(&far_camera, &expected_far) ||
        !subject_is_hud_free(
            &policy, &far_camera, far.players[0]) ||
        !subject_is_hud_free(
            &policy, &far_camera, far.actor) ||
        !subject_is_hud_free(
            &policy, &far_camera, far.target)) {
        return false;
    }

    close.flags |= PAL_UI_LAYOUT_BATTLE_FORCE_FIT_ALL;
    if (!PalUiLayout_ResolveBattleCamera(&close, &forced) ||
        forced.mode != PAL_UI_LAYOUT_CAMERA_FIT_ALL ||
        !same_rect(forced.source, policy.arena) ||
        !same_rect(forced.screen, policy.fit_screen)) {
        return false;
    }

    outside.player_count = 1;
    outside.primary_player = 0;
    outside.players[0] =
        (PalUiLayoutRect){-100, -100, 10, 10};
    if (PalUiLayout_ResolveBattleCamera(&outside, &forced)) {
        return false;
    }
    return true;
}

int main(void)
{
    PalUiLayoutProfile profile;
    uint32_t selectable = 0;
    uint32_t visible_sampling = 0;
    uint32_t catalog_sampling = 0;
    uint16_t screen_id;
    uint16_t sampling_id;
    uint16_t vector_id;

    if (!PalUiLayout_ValidateGenerated()) {
        return fail("generated profile validation failed");
    }
    if (!PalUiLayout_GetProfile(&profile) ||
        profile.screen_count == 0 ||
        profile.element_count == 0 ||
        profile.focus_count == 0 ||
        profile.sampling_count == 0 ||
        profile.camera_vector_count == 0) {
        return fail("generated profile counts are incomplete");
    }
    if (profile.font_image_bytes == 0 &&
        PalUiLayout_Font10IdentityMatches(0, 0, 0, 0, 0, 0, 0)) {
        return fail("empty FONT10 identity matched");
    }
    if (profile.font_image_bytes != 0 &&
        !PalUiLayout_Font10IdentityMatches(
            profile.font_glyph_count,
            profile.font_image_bytes,
            profile.font_payload_crc32,
            profile.font_cell_width,
            profile.font_cell_height,
            profile.font_ascent,
            profile.font_descent)) {
        return fail("FONT10 identity did not round-trip");
    }
    if (profile.font_image_bytes != 0 &&
        PalUiLayout_Font10IdentityMatches(
            profile.font_glyph_count,
            profile.font_image_bytes,
            profile.font_payload_crc32,
            profile.font_cell_width,
            profile.font_cell_height,
            (int8_t)(profile.font_ascent == 127
                ? 126
                : profile.font_ascent + 1),
            profile.font_descent)) {
        return fail("FONT10 metric mismatch was accepted");
    }
    if (PalUiLayout_GetScreen(profile.screen_count, NULL) ||
        PalUiLayout_GetElement(profile.element_count, NULL) ||
        PalUiLayout_GetSampling(profile.sampling_count, NULL) ||
        PalUiLayout_GetCameraVector(
            profile.camera_vector_count, NULL)) {
        return fail("out-of-range accessor succeeded");
    }

    for (screen_id = 0; screen_id < profile.screen_count; screen_id++) {
        PalUiLayoutScreen screen;
        uint16_t local_id;
        uint16_t focus_index;
        bool initial_page_has_focus = false;
        if (!PalUiLayout_GetScreen(screen_id, &screen)) {
            return fail("screen lookup failed");
        }
        for (local_id = 0; local_id < screen.element_count; local_id++) {
            PalUiLayoutElement element;
            if (!PalUiLayout_GetScreenElement(
                    screen_id, local_id, &element)) {
                return fail("screen element lookup failed");
            }
        }
        for (focus_index = 0;
             focus_index < screen.focus_count;
             focus_index++) {
            PalUiLayoutElement element;
            if (!PalUiLayout_GetFocusElement(
                    screen_id, focus_index, &element)) {
                return fail("focus lookup failed");
            }
            if ((element.flags &
                 PAL_UI_LAYOUT_ELEMENT_SELECTABLE) == 0) {
                return fail("focus order contains non-selectable element");
            }
            selectable++;
            if (element.page == screen.initial_page) {
                initial_page_has_focus = true;
            }
        }
        if (screen.focus_count != 0 && !initial_page_has_focus) {
            return fail("initial page has no focus target");
        }
    }
    if (selectable != profile.focus_count) {
        return fail("focus count changed through C accessors");
    }
    if (profile.screen_count == PAL_UI_LAYOUT_SCREEN_COUNT) {
        PalUiLayoutListTemplate item_list;
        PalUiLayoutListTemplate magic_list;
        PalUiLayoutElement slot;
        uint16_t pages;
        if (!PalUiLayout_GetListTemplate(
                PAL_UI_LAYOUT_SCREEN_ITEM, &item_list) ||
            !PalUiLayout_GetListTemplate(
                PAL_UI_LAYOUT_SCREEN_MAGIC, &magic_list) ||
            item_list.maximum_items !=
                PAL_UI_LAYOUT_ITEM_MAX_ITEMS ||
            magic_list.maximum_items !=
                PAL_UI_LAYOUT_MAGIC_MAX_ITEMS ||
            !PalUiLayout_GetListPageCount(
                &item_list, item_list.maximum_items, &pages) ||
            pages != item_list.maximum_page_count ||
            !PalUiLayout_GetListSlot(
                PAL_UI_LAYOUT_SCREEN_ITEM, 0, &slot) ||
            slot.page != 0 ||
            PalUiLayout_GetListSlot(
                PAL_UI_LAYOUT_SCREEN_ITEM,
                item_list.slot_count,
                &slot)) {
            return fail("live list template contract failed");
        }
    }
    if (!check_live_battle_camera(&profile)) {
        return fail("live battle camera disagrees with Python vectors");
    }

    for (sampling_id = 0;
         sampling_id < profile.sampling_count;
         sampling_id++) {
        PalUiLayoutSampling sampling;
        uint16_t source_x;
        uint16_t source_y;
        if (!PalUiLayout_GetSampling(sampling_id, &sampling)) {
            return fail("sampling lookup failed");
        }
        if ((sampling.flags &
             PAL_UI_LAYOUT_SAMPLING_VISIBLE) == 0) {
            if (PalUiLayout_SamplingSourceAt(
                    &sampling, 0, 0, &source_x, &source_y)) {
                return fail("omitted sampling policy produced a pixel");
            }
            continue;
        }
        visible_sampling++;
        if ((sampling.flags &
             PAL_UI_LAYOUT_SAMPLING_CATALOG) != 0) {
            PalUiLayoutSampling found;
            catalog_sampling++;
            if (!PalUiLayout_FindCatalogSampling(
                    sampling.asset_class,
                    sampling.screen_id,
                    sampling.source_width,
                    sampling.source_height,
                    &found) ||
                found.role != sampling.role) {
                return fail("generated catalog lookup mismatch");
            }
        }
        if (!PalUiLayout_SamplingSourceAt(
                &sampling, 0, 0, &source_x, &source_y) ||
            source_x >= sampling.source_width ||
            source_y >= sampling.source_height ||
            !PalUiLayout_SamplingSourceAt(
                &sampling,
                (uint16_t)(sampling.destination_width - 1u),
                (uint16_t)(sampling.destination_height - 1u),
                &source_x,
                &source_y) ||
            source_x >= sampling.source_width ||
            source_y >= sampling.source_height ||
            PalUiLayout_SamplingSourceAt(
                &sampling,
                sampling.destination_width,
                0,
                &source_x,
                &source_y)) {
            return fail("generated sampling bounds mismatch");
        }
    }
    if (visible_sampling == 0) {
        return fail("fixture has no visible generated sampler");
    }
    if (catalog_sampling == 0) {
        PalUiLayoutSampling absent;
        if (PalUiLayout_FindCatalogSampling(
                PAL_UI_LAYOUT_ASSET_CLASS_PORTRAIT,
                0,
                64,
                64,
                &absent)) {
            return fail("fixture unexpectedly contains asset catalog");
        }
    }

    for (vector_id = 0;
         vector_id < profile.camera_vector_count;
         vector_id++) {
        PalUiLayoutCameraVector camera;
        PalUiLayoutPoint focus;
        PalUiLayoutPoint projected;
        PalUiLayoutPoint unprojected;
        if (!PalUiLayout_GetCameraVector(vector_id, &camera)) {
            return fail("camera vector lookup failed");
        }
        focus.x = camera.focus_x;
        focus.y = camera.focus_y;
        if (!PalUiLayout_ProjectCameraPoint(
                &camera, focus, &projected) ||
            projected.x != camera.projected_focus_x ||
            projected.y != camera.projected_focus_y ||
            !PalUiLayout_UnprojectCameraPoint(
                &camera, projected, &unprojected) ||
            unprojected.x < camera.source.x ||
            unprojected.y < camera.source.y ||
            unprojected.x >=
                (int32_t)camera.source.x + camera.source.width ||
            unprojected.y >=
                (int32_t)camera.source.y + camera.source.height) {
            return fail("camera shared-vector projection mismatch");
        }
    }

    printf(
        "pal UI layout runtime parity: %ux%u screens=%u "
        "elements=%u focus=%u sampling=%u camera=%u PASS\n",
        profile.display_width,
        profile.display_height,
        profile.screen_count,
        profile.element_count,
        profile.focus_count,
        profile.sampling_count,
        profile.camera_vector_count);
    return 0;
}
