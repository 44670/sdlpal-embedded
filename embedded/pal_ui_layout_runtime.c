#include "pal_ui_layout_runtime.h"

#include <limits.h>
#include <stddef.h>

#ifndef PAL_UI_LAYOUT_GENERATED_HEADER
#error "PAL_UI_LAYOUT_GENERATED_HEADER must name one generated profile header"
#endif

#include PAL_UI_LAYOUT_GENERATED_HEADER

#if !defined(PAL_UI_GENERATED_COEFFICIENTS_ONLY) || \
    PAL_UI_GENERATED_COEFFICIENTS_ONLY != 1u
#error "UI runtime requires a Python-generated coefficient header"
#endif

#if PAL_UI_GENERATED_FIXED_Q_SHIFT != 16u
#error "UI generated-table ABI is Q16.16"
#endif

#if !defined(PAL_UI_GENERATED_PUBLIC_ABI_VERSION) || \
    PAL_UI_GENERATED_PUBLIC_ABI_VERSION != 1u
#error "UI generated/public ABI version is missing or unsupported"
#endif

#define PAL_UI_ENUM_MATCH(left, right) \
    ((int)(left) == (int)(right))
typedef char pal_ui_public_variant_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_VARIANT_FULL, PAL_UI_LAYOUT_VARIANT_FULL) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_VARIANT_COMPACT, PAL_UI_LAYOUT_VARIANT_COMPACT) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_VARIANT_SINGLE_COLUMN,
         PAL_UI_LAYOUT_VARIANT_SINGLE_COLUMN) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_VARIANT_PAGED, PAL_UI_LAYOUT_VARIANT_PAGED) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_VARIANT_TEXT_ONLY,
         PAL_UI_LAYOUT_VARIANT_TEXT_ONLY)) ? 1 : -1];
typedef char pal_ui_public_screen_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_DIALOG, PAL_UI_LAYOUT_SCREEN_DIALOG) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_OPENING_MENU,
         PAL_UI_LAYOUT_SCREEN_OPENING_MENU) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_GAME_MENU,
         PAL_UI_LAYOUT_SCREEN_GAME_MENU) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_SYSTEM_MENU,
         PAL_UI_LAYOUT_SCREEN_SYSTEM_MENU) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_SAVE_SLOTS,
         PAL_UI_LAYOUT_SCREEN_SAVE_SLOTS) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_CONFIRMATION,
         PAL_UI_LAYOUT_SCREEN_CONFIRMATION) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_ITEM, PAL_UI_LAYOUT_SCREEN_ITEM) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_MAGIC, PAL_UI_LAYOUT_SCREEN_MAGIC) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_STATUS, PAL_UI_LAYOUT_SCREEN_STATUS) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_EQUIP, PAL_UI_LAYOUT_SCREEN_EQUIP) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_BATTLE_HUD,
         PAL_UI_LAYOUT_SCREEN_BATTLE_HUD) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_BATTLE_MISC,
         PAL_UI_LAYOUT_SCREEN_BATTLE_MISC) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_BATTLE_ITEM_ACTION,
         PAL_UI_LAYOUT_SCREEN_BATTLE_ITEM_ACTION) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_SCREEN_COUNT, PAL_UI_LAYOUT_SCREEN_COUNT)) ? 1 : -1];
typedef char pal_ui_public_element_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_ACTION, PAL_UI_LAYOUT_ELEMENT_ACTION) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_DIALOG_TEXT,
         PAL_UI_LAYOUT_ELEMENT_DIALOG_TEXT) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_EQUIPMENT_LABEL,
         PAL_UI_LAYOUT_ELEMENT_EQUIPMENT_LABEL) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_EQUIPMENT_LABEL_VALUE,
         PAL_UI_LAYOUT_ELEMENT_EQUIPMENT_LABEL_VALUE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_EQUIPMENT_PREVIEW,
         PAL_UI_LAYOUT_ELEMENT_EQUIPMENT_PREVIEW) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_PAGE_INDICATOR,
         PAL_UI_LAYOUT_ELEMENT_PAGE_INDICATOR) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_PANEL, PAL_UI_LAYOUT_ELEMENT_PANEL) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_PARTY_SELECTOR,
         PAL_UI_LAYOUT_ELEMENT_PARTY_SELECTOR) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_PORTRAIT,
         PAL_UI_LAYOUT_ELEMENT_PORTRAIT) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_STAT_LABEL,
         PAL_UI_LAYOUT_ELEMENT_STAT_LABEL) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_STAT_LABEL_VALUE,
         PAL_UI_LAYOUT_ELEMENT_STAT_LABEL_VALUE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_TITLE, PAL_UI_LAYOUT_ELEMENT_TITLE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_ELEMENT_KIND_COUNT,
         PAL_UI_LAYOUT_ELEMENT_KIND_COUNT)) ? 1 : -1];
typedef char pal_ui_public_camera_kind_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_KIND_MAP,
         PAL_UI_LAYOUT_CAMERA_MAP) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_KIND_BATTLE,
         PAL_UI_LAYOUT_CAMERA_BATTLE)) ? 1 : -1];
typedef char pal_ui_public_camera_mode_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_MODE_FOLLOW,
         PAL_UI_LAYOUT_CAMERA_FOLLOW) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_MODE_SCRIPTED,
         PAL_UI_LAYOUT_CAMERA_SCRIPTED) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_MODE_IDLE,
         PAL_UI_LAYOUT_CAMERA_IDLE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_MODE_ACTOR_TARGET,
         PAL_UI_LAYOUT_CAMERA_ACTOR_TARGET) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_MODE_FIT_ALL,
         PAL_UI_LAYOUT_CAMERA_FIT_ALL)) ? 1 : -1];
typedef char pal_ui_public_asset_class_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_NONE,
         PAL_UI_LAYOUT_ASSET_CLASS_NONE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_LEGACY_STAGE,
         PAL_UI_LAYOUT_ASSET_CLASS_LEGACY_STAGE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_PORTRAIT,
         PAL_UI_LAYOUT_ASSET_CLASS_PORTRAIT) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_ITEM_PREVIEW,
         PAL_UI_LAYOUT_ASSET_CLASS_ITEM_PREVIEW) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_BATTLE_PLAYER,
         PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_PLAYER) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_BATTLE_ENEMY,
         PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_ENEMY) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_BATTLE_FIRE,
         PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_FIRE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_UI_SPRITE,
         PAL_UI_LAYOUT_ASSET_CLASS_UI_SPRITE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_BATTLE_EFFECT,
         PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_EFFECT) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ASSET_CLASS_COUNT,
         PAL_UI_LAYOUT_ASSET_CLASS_COUNT)) ? 1 : -1];
typedef char pal_ui_public_filter_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_FILTER_NEAREST_CENTER,
         PAL_UI_LAYOUT_FILTER_NEAREST_CENTER) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_FILTER_BOX_2X2,
         PAL_UI_LAYOUT_FILTER_BOX_2X2) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_FILTER_NONE,
         PAL_UI_LAYOUT_FILTER_NONE)) ? 1 : -1];
typedef char pal_ui_public_flag_ids_match[
    (PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ELEMENT_FLAG_VISIBLE,
         PAL_UI_LAYOUT_ELEMENT_VISIBLE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ELEMENT_FLAG_SELECTABLE,
         PAL_UI_LAYOUT_ELEMENT_SELECTABLE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_ELEMENT_FLAG_CRITICAL,
         PAL_UI_LAYOUT_ELEMENT_CRITICAL) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_SAMPLING_FLAG_VISIBLE,
         PAL_UI_LAYOUT_SAMPLING_VISIBLE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_SAMPLING_FLAG_LEGACY_STAGE,
         PAL_UI_LAYOUT_SAMPLING_LEGACY_STAGE) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_SAMPLING_FLAG_CATALOG,
         PAL_UI_LAYOUT_SAMPLING_CATALOG) &&
     PAL_UI_ENUM_MATCH(
         PAL_UI_GENERATED_CAMERA_FLAG_CLAMPED,
         PAL_UI_LAYOUT_CAMERA_CLAMPED)) ? 1 : -1];
typedef char pal_ui_public_no_screen_id_matches[
    PAL_UI_ENUM_MATCH(
        PAL_UI_GENERATED_NO_SCREEN,
        PAL_UI_LAYOUT_NO_SCREEN) ? 1 : -1];
#undef PAL_UI_ENUM_MATCH

#define PAL_UI_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define PAL_UI_STORAGE_COUNT(count) ((count) == 0u ? 1u : (count))

typedef char pal_ui_stage_sample_x_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_stage_sample_x) ==
         PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT &&
     PAL_UI_GENERATED_STAGE_SAMPLE_X_COUNT ==
         PAL_UI_GENERATED_STAGE_DESTINATION_WIDTH) ? 1 : -1];
typedef char pal_ui_stage_sample_y_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_stage_sample_y) ==
         PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT &&
     PAL_UI_GENERATED_STAGE_SAMPLE_Y_COUNT ==
         PAL_UI_GENERATED_STAGE_DESTINATION_HEIGHT) ? 1 : -1];
typedef char pal_ui_battle_fit_sample_x_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_battle_fit_sample_x) ==
         PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT &&
     PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT ==
         PAL_UI_GENERATED_BATTLE_FIT_SCREEN_WIDTH) ? 1 : -1];
typedef char pal_ui_battle_fit_sample_y_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_battle_fit_sample_y) ==
         PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT &&
     PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT ==
         PAL_UI_GENERATED_BATTLE_FIT_SCREEN_HEIGHT) ? 1 : -1];
typedef char pal_ui_sampling_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_sampling) ==
         PAL_UI_STORAGE_COUNT(PAL_UI_SAMPLE_COUNT)) ? 1 : -1];
typedef char pal_ui_element_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_elements) ==
         PAL_UI_STORAGE_COUNT(PAL_UI_GENERATED_ELEMENT_COUNT)) ? 1 : -1];
typedef char pal_ui_focus_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_focus_order) ==
         PAL_UI_STORAGE_COUNT(PAL_UI_GENERATED_FOCUS_COUNT)) ? 1 : -1];
typedef char pal_ui_screen_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_screens) ==
         PAL_UI_STORAGE_COUNT(PAL_UI_SCREEN_COUNT)) ? 1 : -1];
typedef char pal_ui_camera_count_matches[
    (PAL_UI_ARRAY_COUNT(pal_ui_generated_camera_vectors) ==
         PAL_UI_STORAGE_COUNT(PAL_UI_GENERATED_CAMERA_VECTOR_COUNT)) ? 1 : -1];

static PalUiLayoutRect copy_rect(PalUiGeneratedRect source)
{
    PalUiLayoutRect result;
    result.x = source.x;
    result.y = source.y;
    result.width = source.width;
    result.height = source.height;
    return result;
}

static bool rect_is_positive(PalUiLayoutRect rect)
{
    return rect.width != 0 && rect.height != 0;
}

static bool rect_is_zero(PalUiLayoutRect rect)
{
    return rect.x == 0 && rect.y == 0 &&
           rect.width == 0 && rect.height == 0;
}

static bool rect_inside_display(
    PalUiLayoutRect rect,
    uint16_t display_width,
    uint16_t display_height)
{
    int32_t right = (int32_t)rect.x + rect.width;
    int32_t bottom = (int32_t)rect.y + rect.height;
    return rect.x >= 0 && rect.y >= 0 &&
           right <= display_width && bottom <= display_height;
}

static int32_t rect_right(PalUiLayoutRect rect)
{
    return (int32_t)rect.x + rect.width;
}

static int32_t rect_bottom(PalUiLayoutRect rect)
{
    return (int32_t)rect.y + rect.height;
}

static bool rect_inside_rect(
    PalUiLayoutRect inner,
    PalUiLayoutRect outer)
{
    return rect_is_positive(inner) &&
           inner.x >= outer.x &&
           inner.y >= outer.y &&
           rect_right(inner) <= rect_right(outer) &&
           rect_bottom(inner) <= rect_bottom(outer);
}

static bool rects_intersect(
    PalUiLayoutRect first,
    PalUiLayoutRect second)
{
    return rect_is_positive(first) &&
           rect_is_positive(second) &&
           first.x < rect_right(second) &&
           second.x < rect_right(first) &&
           first.y < rect_bottom(second) &&
           second.y < rect_bottom(first);
}

static bool rect_intersection(
    PalUiLayoutRect first,
    PalUiLayoutRect second,
    PalUiLayoutRect *out)
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    if (out == NULL ||
        !rect_is_positive(first) ||
        !rect_is_positive(second)) {
        return false;
    }
    left = first.x > second.x ? first.x : second.x;
    top = first.y > second.y ? first.y : second.y;
    right = rect_right(first) < rect_right(second)
        ? rect_right(first)
        : rect_right(second);
    bottom = rect_bottom(first) < rect_bottom(second)
        ? rect_bottom(first)
        : rect_bottom(second);
    if (left >= right || top >= bottom ||
        left < INT16_MIN || left > INT16_MAX ||
        top < INT16_MIN || top > INT16_MAX ||
        right - left > UINT16_MAX ||
        bottom - top > UINT16_MAX) {
        return false;
    }
    out->x = (int16_t)left;
    out->y = (int16_t)top;
    out->width = (uint16_t)(right - left);
    out->height = (uint16_t)(bottom - top);
    return true;
}

static bool rect_union(
    PalUiLayoutRect first,
    PalUiLayoutRect second,
    PalUiLayoutRect *out)
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    if (out == NULL ||
        !rect_is_positive(first) ||
        !rect_is_positive(second)) {
        return false;
    }
    left = first.x < second.x ? first.x : second.x;
    top = first.y < second.y ? first.y : second.y;
    right = rect_right(first) > rect_right(second)
        ? rect_right(first)
        : rect_right(second);
    bottom = rect_bottom(first) > rect_bottom(second)
        ? rect_bottom(first)
        : rect_bottom(second);
    if (left < INT16_MIN || left > INT16_MAX ||
        top < INT16_MIN || top > INT16_MAX ||
        right <= left || bottom <= top ||
        right - left > UINT16_MAX ||
        bottom - top > UINT16_MAX) {
        return false;
    }
    out->x = (int16_t)left;
    out->y = (int16_t)top;
    out->width = (uint16_t)(right - left);
    out->height = (uint16_t)(bottom - top);
    return true;
}

static bool rect_union_assign(
    PalUiLayoutRect rect,
    PalUiLayoutRect *accumulator,
    bool *has_accumulator)
{
    PalUiLayoutRect combined;
    if (accumulator == NULL || has_accumulator == NULL) {
        return false;
    }
    if (!*has_accumulator) {
        *accumulator = rect;
        *has_accumulator = true;
        return true;
    }
    if (!rect_union(*accumulator, rect, &combined)) {
        return false;
    }
    *accumulator = combined;
    return true;
}

static bool rect_expand_inside(
    PalUiLayoutRect rect,
    uint8_t padding,
    PalUiLayoutRect bounds,
    PalUiLayoutRect *out)
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    if (out == NULL ||
        !rect_inside_rect(rect, bounds)) {
        return false;
    }
    left = (int32_t)rect.x - padding;
    top = (int32_t)rect.y - padding;
    right = rect_right(rect) + padding;
    bottom = rect_bottom(rect) + padding;
    if (left < bounds.x) {
        left = bounds.x;
    }
    if (top < bounds.y) {
        top = bounds.y;
    }
    if (right > rect_right(bounds)) {
        right = rect_right(bounds);
    }
    if (bottom > rect_bottom(bounds)) {
        bottom = rect_bottom(bounds);
    }
    if (left < INT16_MIN || left > INT16_MAX ||
        top < INT16_MIN || top > INT16_MAX ||
        right <= left || bottom <= top ||
        right - left > UINT16_MAX ||
        bottom - top > UINT16_MAX) {
        return false;
    }
    out->x = (int16_t)left;
    out->y = (int16_t)top;
    out->width = (uint16_t)(right - left);
    out->height = (uint16_t)(bottom - top);
    return true;
}

static int32_t rect_center_x(PalUiLayoutRect rect)
{
    return (int32_t)rect.x + rect.width / 2u;
}

static int32_t rect_center_y(PalUiLayoutRect rect)
{
    return (int32_t)rect.y + rect.height / 2u;
}

static int32_t clamp_i32(int32_t value, int32_t lower, int32_t upper)
{
    if (value < lower) {
        return lower;
    }
    if (value > upper) {
        return upper;
    }
    return value;
}

static bool fits_i16(int32_t value)
{
    return value >= INT16_MIN && value <= INT16_MAX;
}

static int64_t floor_div_i64(int64_t numerator, int64_t denominator)
{
    int64_t quotient = numerator / denominator;
    int64_t remainder = numerator % denominator;
    if (remainder != 0 && numerator < 0) {
        quotient--;
    }
    return quotient;
}

static int64_t ceil_div_i64(int64_t numerator, int64_t denominator)
{
    return -floor_div_i64(-numerator, denominator);
}

static bool point_fits_i32(int64_t x, int64_t y)
{
    return x >= INT32_MIN && x <= INT32_MAX &&
           y >= INT32_MIN && y <= INT32_MAX;
}

bool PalUiLayout_GetProfile(PalUiLayoutProfile *out)
{
    if (out == NULL) {
        return false;
    }
    out->display_width = PAL_UI_GENERATED_DISPLAY_WIDTH;
    out->display_height = PAL_UI_GENERATED_DISPLAY_HEIGHT;
    out->safe_rect = copy_rect(pal_ui_generated_safe_rect);
    out->stage_rect = copy_rect(pal_ui_generated_stage_rect);
    out->stage_source_width =
        PAL_UI_GENERATED_STAGE_SOURCE_WIDTH;
    out->stage_source_height =
        PAL_UI_GENERATED_STAGE_SOURCE_HEIGHT;
    out->fixed_q_shift =
        PAL_UI_GENERATED_FIXED_Q_SHIFT;
    out->stage_scale_numerator =
        PAL_UI_GENERATED_STAGE_SCALE_NUMERATOR;
    out->stage_scale_denominator =
        PAL_UI_GENERATED_STAGE_SCALE_DENOMINATOR;
    out->stage_scale_q16 = PAL_UI_GENERATED_STAGE_SCALE_Q16;
    out->stage_step_x_q16 =
        PAL_UI_GENERATED_STAGE_STEP_X_Q16;
    out->stage_step_y_q16 =
        PAL_UI_GENERATED_STAGE_STEP_Y_Q16;
    out->stage_phase_x_q16 =
        PAL_UI_GENERATED_STAGE_PHASE_X_Q16;
    out->stage_phase_y_q16 =
        PAL_UI_GENERATED_STAGE_PHASE_Y_Q16;
    out->player_anchor_x = PAL_UI_GENERATED_PLAYER_ANCHOR_X;
    out->player_anchor_y = PAL_UI_GENERATED_PLAYER_ANCHOR_Y;
    out->font_cell_width = PAL_UI_GENERATED_FONT_CELL_WIDTH;
    out->font_cell_height = PAL_UI_GENERATED_FONT_CELL_HEIGHT;
    out->font_ascent = PAL_UI_GENERATED_FONT_ASCENT;
    out->font_descent = PAL_UI_GENERATED_FONT_DESCENT;
    out->font_line_height = PAL_UI_GENERATED_FONT_LINE_HEIGHT;
    out->font_glyph_count = PAL_UI_GENERATED_FONT_GLYPH_COUNT;
    out->font_image_bytes = PAL_UI_GENERATED_FONT_IMAGE_BYTES;
    out->font_payload_crc32 =
        PAL_UI_GENERATED_FONT_PAYLOAD_CRC32;
    out->screen_count = PAL_UI_SCREEN_COUNT;
    out->element_count = PAL_UI_GENERATED_ELEMENT_COUNT;
    out->focus_count = PAL_UI_GENERATED_FOCUS_COUNT;
    out->sampling_count = PAL_UI_SAMPLE_COUNT;
    out->camera_vector_count = PAL_UI_GENERATED_CAMERA_VECTOR_COUNT;
    return true;
}

bool PalUiLayout_Font10IdentityMatches(
    uint32_t glyph_count,
    uint32_t image_bytes,
    uint32_t payload_crc32,
    uint8_t cell_width,
    uint8_t cell_height,
    int8_t ascent,
    int8_t descent)
{
    return PAL_UI_GENERATED_FONT_IMAGE_BYTES != 0 &&
           glyph_count == PAL_UI_GENERATED_FONT_GLYPH_COUNT &&
           image_bytes == PAL_UI_GENERATED_FONT_IMAGE_BYTES &&
           payload_crc32 == PAL_UI_GENERATED_FONT_PAYLOAD_CRC32 &&
           cell_width == PAL_UI_GENERATED_FONT_CELL_WIDTH &&
           cell_height == PAL_UI_GENERATED_FONT_CELL_HEIGHT &&
           ascent == PAL_UI_GENERATED_FONT_ASCENT &&
           descent == PAL_UI_GENERATED_FONT_DESCENT;
}

bool PalUiLayout_GetScreen(uint16_t screen_id, PalUiLayoutScreen *out)
{
    const PalUiGeneratedScreen *source;
    if (out == NULL || screen_id >= PAL_UI_SCREEN_COUNT) {
        return false;
    }
    source = &pal_ui_generated_screens[screen_id];
    out->element_first = source->element_first;
    out->element_count = source->element_count;
    out->focus_first = source->focus_first;
    out->focus_count = source->focus_count;
    out->variant = source->variant;
    out->rows = source->rows;
    out->columns = source->columns;
    out->page_count = source->page_count;
    out->page_capacity = source->page_capacity;
    out->initial_page = source->initial_page;
    return true;
}

bool PalUiLayout_GetElement(uint16_t element_id, PalUiLayoutElement *out)
{
    const PalUiGeneratedElement *source;
    if (out == NULL || element_id >= PAL_UI_GENERATED_ELEMENT_COUNT) {
        return false;
    }
    source = &pal_ui_generated_elements[element_id];
    out->rect = copy_rect(source->rect);
    out->return_value = source->return_value;
    out->page = source->page;
    out->local_id = source->local_id;
    out->screen_id = source->screen_id;
    out->kind = source->kind;
    out->priority = source->priority;
    out->flags = source->flags;
    return true;
}

bool PalUiLayout_GetScreenElement(
    uint16_t screen_id,
    uint16_t local_id,
    PalUiLayoutElement *out)
{
    PalUiLayoutScreen screen;
    if (!PalUiLayout_GetScreen(screen_id, &screen) ||
        local_id >= screen.element_count) {
        return false;
    }
    return PalUiLayout_GetElement(
        (uint16_t)(screen.element_first + local_id),
        out);
}

bool PalUiLayout_GetFocusElement(
    uint16_t screen_id,
    uint16_t focus_index,
    PalUiLayoutElement *out)
{
    PalUiLayoutScreen screen;
    uint16_t element_id;
    if (!PalUiLayout_GetScreen(screen_id, &screen) ||
        focus_index >= screen.focus_count) {
        return false;
    }
    element_id =
        pal_ui_generated_focus_order[screen.focus_first + focus_index];
    return PalUiLayout_GetElement(element_id, out);
}

bool PalUiLayout_GetListTemplate(
    uint16_t screen_id,
    PalUiLayoutListTemplate *out)
{
    PalUiLayoutScreen screen;
    uint16_t local_id;
    uint16_t slot_count = 0;
    uint16_t maximum_items;
    if (out == NULL ||
        (screen_id != PAL_UI_LAYOUT_SCREEN_ITEM &&
         screen_id != PAL_UI_LAYOUT_SCREEN_MAGIC) ||
        !PalUiLayout_GetScreen(screen_id, &screen)) {
        return false;
    }
    maximum_items = screen_id == PAL_UI_LAYOUT_SCREEN_ITEM
        ? PAL_UI_LAYOUT_ITEM_MAX_ITEMS
        : PAL_UI_LAYOUT_MAGIC_MAX_ITEMS;
    for (local_id = 0; local_id < screen.element_count; local_id++) {
        PalUiLayoutElement element;
        if (!PalUiLayout_GetScreenElement(
                screen_id, local_id, &element)) {
            return false;
        }
        if (element.page == 0 &&
            element.kind == PAL_UI_LAYOUT_ELEMENT_ACTION &&
            (element.flags &
             PAL_UI_LAYOUT_ELEMENT_SELECTABLE) != 0) {
            slot_count++;
        }
    }
    if (slot_count == 0 ||
        slot_count != screen.page_capacity) {
        return false;
    }
    out->screen_id = screen_id;
    out->slot_count = slot_count;
    out->maximum_items = maximum_items;
    out->maximum_page_count =
        (uint16_t)(
            (maximum_items + slot_count - 1u) / slot_count);
    out->rows = screen.rows;
    out->columns = screen.columns;
    return true;
}

bool PalUiLayout_GetListSlot(
    uint16_t screen_id,
    uint16_t slot_index,
    PalUiLayoutElement *out)
{
    PalUiLayoutListTemplate list;
    PalUiLayoutScreen screen;
    uint16_t local_id;
    uint16_t seen = 0;
    if (out == NULL ||
        !PalUiLayout_GetListTemplate(screen_id, &list) ||
        slot_index >= list.slot_count ||
        !PalUiLayout_GetScreen(screen_id, &screen)) {
        return false;
    }
    for (local_id = 0; local_id < screen.element_count; local_id++) {
        PalUiLayoutElement element;
        if (!PalUiLayout_GetScreenElement(
                screen_id, local_id, &element)) {
            return false;
        }
        if (element.page == 0 &&
            element.kind == PAL_UI_LAYOUT_ELEMENT_ACTION &&
            (element.flags &
             PAL_UI_LAYOUT_ELEMENT_SELECTABLE) != 0) {
            if (seen == slot_index) {
                *out = element;
                return true;
            }
            seen++;
        }
    }
    return false;
}

bool PalUiLayout_GetListPageCount(
    const PalUiLayoutListTemplate *list,
    uint16_t item_count,
    uint16_t *page_count)
{
    uint16_t pages;
    if (list == NULL || page_count == NULL ||
        list->slot_count == 0 ||
        list->maximum_items == 0 ||
        list->maximum_page_count == 0 ||
        item_count > list->maximum_items) {
        return false;
    }
    pages = item_count == 0
        ? 1
        : (uint16_t)(
            (item_count + list->slot_count - 1u) /
            list->slot_count);
    if (pages > list->maximum_page_count) {
        return false;
    }
    *page_count = pages;
    return true;
}

bool PalUiLayout_GetSampling(
    uint16_t sampling_id,
    PalUiLayoutSampling *out)
{
    const PalUiGeneratedSampling *source;
    if (out == NULL || sampling_id >= PAL_UI_SAMPLE_COUNT) {
        return false;
    }
    source = &pal_ui_generated_sampling[sampling_id];
    out->role = source->role;
    out->asset_class = source->asset_class;
    out->filter = source->filter;
    out->flags = source->flags;
    out->screen_id = source->screen_id;
    out->page = source->page;
    out->numerator = source->numerator;
    out->denominator = source->denominator;
    out->source_width = source->source_width;
    out->source_height = source->source_height;
    out->destination_width = source->destination_width;
    out->destination_height = source->destination_height;
    out->fixed_q_shift = PAL_UI_GENERATED_FIXED_Q_SHIFT;
    out->scale_q16 = source->scale_q16;
    out->step_x_q16 = source->step_x_q16;
    out->step_y_q16 = source->step_y_q16;
    out->phase_x_q16 = source->phase_x_q16;
    out->phase_y_q16 = source->phase_y_q16;
    out->min_width = source->min_width;
    out->min_height = source->min_height;
    out->max_width = source->max_width;
    out->max_height = source->max_height;
    out->destination = copy_rect(source->destination);
    return true;
}

bool PalUiLayout_FindCatalogSampling(
    uint8_t asset_class,
    uint8_t screen_id,
    uint16_t source_width,
    uint16_t source_height,
    PalUiLayoutSampling *out)
{
    uint16_t sampling_id;
    if (out == NULL ||
        asset_class == PAL_UI_LAYOUT_ASSET_CLASS_NONE ||
        source_width == 0 || source_height == 0) {
        return false;
    }
    for (sampling_id = 0;
         sampling_id < PAL_UI_SAMPLE_COUNT;
         sampling_id++) {
        PalUiLayoutSampling candidate;
        if (!PalUiLayout_GetSampling(sampling_id, &candidate)) {
            return false;
        }
        if ((candidate.flags & PAL_UI_LAYOUT_SAMPLING_CATALOG) != 0 &&
            candidate.asset_class == asset_class &&
            candidate.screen_id == screen_id &&
            candidate.source_width == source_width &&
            candidate.source_height == source_height) {
            *out = candidate;
            return true;
        }
    }
    return false;
}

bool PalUiLayout_GetCameraVector(
    uint16_t vector_id,
    PalUiLayoutCameraVector *out)
{
    const PalUiGeneratedCameraVector *source;
    if (out == NULL ||
        vector_id >= PAL_UI_GENERATED_CAMERA_VECTOR_COUNT) {
        return false;
    }
    source = &pal_ui_generated_camera_vectors[vector_id];
    out->source = copy_rect(source->bounds);
    out->screen = copy_rect(source->screen);
    out->focus_x = source->focus_x;
    out->focus_y = source->focus_y;
    out->camera_x = source->camera_x;
    out->camera_y = source->camera_y;
    out->desired_x = source->desired_x;
    out->desired_y = source->desired_y;
    out->projected_focus_x = source->projected_focus_x;
    out->projected_focus_y = source->projected_focus_y;
    out->scale_numerator = source->scale_numerator;
    out->scale_denominator = source->scale_denominator;
    out->fixed_q_shift = PAL_UI_GENERATED_FIXED_Q_SHIFT;
    out->scale_q16 = source->scale_q16;
    out->source_step_q16 = source->source_step_q16;
    out->source_phase_q16 = source->source_phase_q16;
    out->kind = source->kind;
    out->mode = source->mode;
    out->flags = source->flags;
    return true;
}

bool PalUiLayout_GetBattleCameraPolicy(
    PalUiLayoutBattleCameraPolicy *out)
{
    const PalUiGeneratedBattleCameraPolicy *source =
        &pal_ui_generated_battle_camera_policy;
    if (out == NULL) {
        return false;
    }
    out->arena = copy_rect(source->arena);
    out->hud_rect = copy_rect(source->hud_rect);
    out->content_rect = copy_rect(source->content_rect);
    out->focus_screen = copy_rect(source->focus_screen);
    out->fit_screen = copy_rect(source->fit_screen);
    out->focus_source_width = source->focus_source_width;
    out->focus_source_height = source->focus_source_height;
    out->fit_numerator = source->fit_numerator;
    out->fit_denominator = source->fit_denominator;
    out->fixed_q_shift = PAL_UI_GENERATED_FIXED_Q_SHIFT;
    out->fit_scale_q16 = source->fit_scale_q16;
    out->fit_step_q16 = source->fit_step_q16;
    out->fit_phase_q16 = source->fit_phase_q16;
    out->padding = source->padding;
    out->max_players = source->max_players;
    return true;
}

bool PalUiLayout_BattleFitSourceX(
    uint16_t destination_x,
    uint16_t *source_x)
{
    if (source_x == NULL ||
        destination_x >= PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT) {
        return false;
    }
    *source_x = pal_ui_generated_battle_fit_sample_x[destination_x];
    return true;
}

bool PalUiLayout_BattleFitSourceY(
    uint16_t destination_y,
    uint16_t *source_y)
{
    if (source_y == NULL ||
        destination_y >= PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT) {
        return false;
    }
    *source_y = pal_ui_generated_battle_fit_sample_y[destination_y];
    return true;
}

bool PalUiLayout_ResolveBattleCamera(
    const PalUiLayoutBattleCameraInput *input,
    PalUiLayoutCameraVector *out)
{
    PalUiLayoutBattleCameraPolicy policy;
    PalUiLayoutRect clipped_players[PAL_UI_LAYOUT_BATTLE_MAX_PLAYERS];
    PalUiLayoutRect clipped_actor;
    PalUiLayoutRect clipped_target;
    PalUiLayoutRect player_union;
    PalUiLayoutRect required;
    PalUiLayoutRect padded_required;
    PalUiLayoutRect focus_rect;
    PalUiLayoutPoint focus;
    PalUiLayoutPoint projected;
    bool has_players = false;
    bool has_required = false;
    bool has_actor;
    bool has_target;
    bool force_fit;
    bool use_focus = false;
    uint8_t index;
    int32_t desired_x;
    int32_t desired_y;
    int32_t origin_x = 0;
    int32_t origin_y = 0;
    int32_t lower;
    int32_t upper;
    uint8_t known_flags =
        PAL_UI_LAYOUT_BATTLE_HAS_ACTOR |
        PAL_UI_LAYOUT_BATTLE_HAS_TARGET |
        PAL_UI_LAYOUT_BATTLE_FORCE_FIT_ALL;

    if (input == NULL || out == NULL ||
        !PalUiLayout_GetBattleCameraPolicy(&policy) ||
        input->player_count > PAL_UI_LAYOUT_BATTLE_MAX_PLAYERS ||
        input->player_count > policy.max_players ||
        (input->flags & (uint8_t)~known_flags) != 0 ||
        (input->primary_player != PAL_UI_LAYOUT_BATTLE_PRIMARY_TEAM &&
         input->primary_player >= input->player_count)) {
        return false;
    }

    has_actor =
        (input->flags & PAL_UI_LAYOUT_BATTLE_HAS_ACTOR) != 0;
    has_target =
        (input->flags & PAL_UI_LAYOUT_BATTLE_HAS_TARGET) != 0;
    force_fit =
        (input->flags & PAL_UI_LAYOUT_BATTLE_FORCE_FIT_ALL) != 0;

    for (index = 0; index < input->player_count; index++) {
        if (!rect_is_positive(input->players[index]) ||
            !rect_intersection(
                input->players[index],
                policy.arena,
                &clipped_players[index]) ||
            !rect_union_assign(
                clipped_players[index],
                &player_union,
                &has_players) ||
            !rect_union_assign(
                clipped_players[index],
                &required,
                &has_required)) {
            return false;
        }
    }
    if (has_actor &&
        (!rect_is_positive(input->actor) ||
         !rect_intersection(
             input->actor, policy.arena, &clipped_actor) ||
         !rect_union_assign(
             clipped_actor, &required, &has_required))) {
        return false;
    }
    if (has_target &&
        (!rect_is_positive(input->target) ||
         !rect_intersection(
             input->target, policy.arena, &clipped_target) ||
         !rect_union_assign(
             clipped_target, &required, &has_required))) {
        return false;
    }

    if (input->primary_player != PAL_UI_LAYOUT_BATTLE_PRIMARY_TEAM) {
        focus_rect = clipped_players[input->primary_player];
    } else if (has_players) {
        focus_rect = player_union;
    } else if (has_actor) {
        focus_rect = clipped_actor;
    } else if (has_target) {
        focus_rect = clipped_target;
    } else {
        int32_t center_x = rect_center_x(policy.arena);
        int32_t center_y = rect_center_y(policy.arena);
        if (!fits_i16(center_x) || !fits_i16(center_y)) {
            return false;
        }
        focus_rect.x = (int16_t)center_x;
        focus_rect.y = (int16_t)center_y;
        focus_rect.width = 1;
        focus_rect.height = 1;
        required = focus_rect;
        has_required = true;
    }
    if (!has_required ||
        !rect_expand_inside(
            required,
            policy.padding,
            policy.arena,
            &padded_required)) {
        return false;
    }

    focus.x = rect_center_x(focus_rect);
    focus.y = rect_center_y(focus_rect);
    desired_x =
        focus.x - (int32_t)policy.focus_source_width / 2;
    desired_y =
        focus.y - (int32_t)policy.focus_source_height / 2;

    if (!force_fit &&
        policy.arena.width >= policy.focus_source_width &&
        policy.arena.height >= policy.focus_source_height &&
        padded_required.width <= policy.focus_source_width &&
        padded_required.height <= policy.focus_source_height) {
        lower = rect_right(padded_required) -
            policy.focus_source_width;
        if (lower < policy.arena.x) {
            lower = policy.arena.x;
        }
        upper = padded_required.x;
        if (upper >
            rect_right(policy.arena) -
                policy.focus_source_width) {
            upper =
                rect_right(policy.arena) -
                policy.focus_source_width;
        }
        if (lower <= upper) {
            origin_x = clamp_i32(desired_x, lower, upper);
            lower = rect_bottom(padded_required) -
                policy.focus_source_height;
            if (lower < policy.arena.y) {
                lower = policy.arena.y;
            }
            upper = padded_required.y;
            if (upper >
                rect_bottom(policy.arena) -
                    policy.focus_source_height) {
                upper =
                    rect_bottom(policy.arena) -
                    policy.focus_source_height;
            }
            if (lower <= upper) {
                origin_y = clamp_i32(desired_y, lower, upper);
                use_focus = true;
            }
        }
    }

    if (!fits_i16(focus.x) || !fits_i16(focus.y) ||
        !fits_i16(desired_x) || !fits_i16(desired_y)) {
        return false;
    }
    out->focus_x = (int16_t)focus.x;
    out->focus_y = (int16_t)focus.y;
    out->desired_x = (int16_t)desired_x;
    out->desired_y = (int16_t)desired_y;
    out->fixed_q_shift = PAL_UI_GENERATED_FIXED_Q_SHIFT;
    out->kind = PAL_UI_LAYOUT_CAMERA_BATTLE;

    if (use_focus) {
        if (!fits_i16(origin_x) || !fits_i16(origin_y)) {
            return false;
        }
        out->source.x = (int16_t)origin_x;
        out->source.y = (int16_t)origin_y;
        out->source.width = policy.focus_source_width;
        out->source.height = policy.focus_source_height;
        out->screen = policy.focus_screen;
        out->scale_numerator = 1;
        out->scale_denominator = 1;
        out->scale_q16 = 1u << PAL_UI_LAYOUT_Q16_SHIFT;
        out->source_step_q16 =
            1u << PAL_UI_LAYOUT_Q16_SHIFT;
        out->source_phase_q16 =
            1u << (PAL_UI_LAYOUT_Q16_SHIFT - 1u);
        out->mode = (has_actor || has_target)
            ? PAL_UI_LAYOUT_CAMERA_ACTOR_TARGET
            : PAL_UI_LAYOUT_CAMERA_IDLE;
        out->flags =
            (origin_x != desired_x || origin_y != desired_y)
            ? PAL_UI_LAYOUT_CAMERA_CLAMPED
            : 0;
    } else {
        out->source = policy.arena;
        out->screen = policy.fit_screen;
        out->scale_numerator = policy.fit_numerator;
        out->scale_denominator = policy.fit_denominator;
        out->scale_q16 = policy.fit_scale_q16;
        out->source_step_q16 = policy.fit_step_q16;
        out->source_phase_q16 = policy.fit_phase_q16;
        out->mode = PAL_UI_LAYOUT_CAMERA_FIT_ALL;
        out->flags = PAL_UI_LAYOUT_CAMERA_CLAMPED;
    }
    out->camera_x = out->source.x;
    out->camera_y = out->source.y;
    if (!PalUiLayout_ProjectCameraPoint(out, focus, &projected) ||
        !fits_i16(projected.x) ||
        !fits_i16(projected.y)) {
        return false;
    }
    out->projected_focus_x = (int16_t)projected.x;
    out->projected_focus_y = (int16_t)projected.y;
    return true;
}

bool PalUiLayout_ProjectCameraPoint(
    const PalUiLayoutCameraVector *camera,
    PalUiLayoutPoint world,
    PalUiLayoutPoint *screen)
{
    int64_t x;
    int64_t y;
    if (camera == NULL || screen == NULL ||
        camera->scale_numerator == 0 ||
        camera->scale_denominator == 0) {
        return false;
    }
    x = camera->screen.x + floor_div_i64(
        ((int64_t)world.x - camera->camera_x) *
            camera->scale_numerator,
        camera->scale_denominator);
    y = camera->screen.y + floor_div_i64(
        ((int64_t)world.y - camera->camera_y) *
            camera->scale_numerator,
        camera->scale_denominator);
    if (!point_fits_i32(x, y)) {
        return false;
    }
    screen->x = (int32_t)x;
    screen->y = (int32_t)y;
    return true;
}

bool PalUiLayout_UnprojectCameraPoint(
    const PalUiLayoutCameraVector *camera,
    PalUiLayoutPoint screen,
    PalUiLayoutPoint *world)
{
    int64_t x;
    int64_t y;
    if (camera == NULL || world == NULL ||
        camera->scale_numerator == 0 ||
        camera->scale_denominator == 0) {
        return false;
    }
    x = camera->camera_x + floor_div_i64(
        ((int64_t)screen.x - camera->screen.x) *
            camera->scale_denominator,
        camera->scale_numerator);
    y = camera->camera_y + floor_div_i64(
        ((int64_t)screen.y - camera->screen.y) *
            camera->scale_denominator,
        camera->scale_numerator);
    if (!point_fits_i32(x, y)) {
        return false;
    }
    world->x = (int32_t)x;
    world->y = (int32_t)y;
    return true;
}

bool PalUiLayout_ProjectCameraRect(
    const PalUiLayoutCameraVector *camera,
    PalUiLayoutRect world,
    PalUiLayoutRect *screen)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t width;
    int64_t height;
    if (camera == NULL || screen == NULL ||
        camera->scale_numerator == 0 ||
        camera->scale_denominator == 0 ||
        !rect_is_positive(world)) {
        return false;
    }
    left = camera->screen.x + floor_div_i64(
        ((int64_t)world.x - camera->camera_x) *
            camera->scale_numerator,
        camera->scale_denominator);
    top = camera->screen.y + floor_div_i64(
        ((int64_t)world.y - camera->camera_y) *
            camera->scale_numerator,
        camera->scale_denominator);
    right = camera->screen.x + ceil_div_i64(
        ((int64_t)world.x + world.width - camera->camera_x) *
            camera->scale_numerator,
        camera->scale_denominator);
    bottom = camera->screen.y + ceil_div_i64(
        ((int64_t)world.y + world.height - camera->camera_y) *
            camera->scale_numerator,
        camera->scale_denominator);
    width = right - left;
    height = bottom - top;
    if (left < INT16_MIN || left > INT16_MAX ||
        top < INT16_MIN || top > INT16_MAX ||
        width <= 0 || width > UINT16_MAX ||
        height <= 0 || height > UINT16_MAX) {
        return false;
    }
    screen->x = (int16_t)left;
    screen->y = (int16_t)top;
    screen->width = (uint16_t)width;
    screen->height = (uint16_t)height;
    return true;
}

bool PalUiLayout_SamplingSourceAt(
    const PalUiLayoutSampling *sampling,
    uint16_t destination_x,
    uint16_t destination_y,
    uint16_t *source_x,
    uint16_t *source_y)
{
    uint64_t x;
    uint64_t y;
    if (sampling == NULL || source_x == NULL || source_y == NULL ||
        (sampling->flags & PAL_UI_LAYOUT_SAMPLING_VISIBLE) == 0 ||
        sampling->source_width == 0 ||
        sampling->source_height == 0 ||
        sampling->destination_width == 0 ||
        sampling->destination_height == 0 ||
        destination_x >= sampling->destination_width ||
        destination_y >= sampling->destination_height) {
        return false;
    }
    x = ((uint64_t)destination_x * 2u + 1u) *
        sampling->source_width /
        ((uint64_t)sampling->destination_width * 2u);
    y = ((uint64_t)destination_y * 2u + 1u) *
        sampling->source_height /
        ((uint64_t)sampling->destination_height * 2u);
    if (x >= sampling->source_width ||
        y >= sampling->source_height) {
        return false;
    }
    *source_x = (uint16_t)x;
    *source_y = (uint16_t)y;
    return true;
}

static bool validate_screen(
    uint16_t screen_id,
    const PalUiLayoutProfile *profile,
    uint16_t expected_element_first,
    uint16_t expected_focus_first)
{
    PalUiLayoutScreen screen;
    uint16_t local_id;
    uint16_t focus_index;
    if (!PalUiLayout_GetScreen(screen_id, &screen) ||
        screen.element_first != expected_element_first ||
        screen.focus_first != expected_focus_first ||
        screen.rows == 0 || screen.columns == 0 ||
        screen.page_count == 0 || screen.page_capacity == 0 ||
        screen.initial_page >= screen.page_count ||
        (uint32_t)screen.element_first + screen.element_count >
            profile->element_count ||
        (uint32_t)screen.focus_first + screen.focus_count >
            profile->focus_count) {
        return false;
    }
    for (local_id = 0; local_id < screen.element_count; local_id++) {
        PalUiLayoutElement element;
        if (!PalUiLayout_GetScreenElement(
                screen_id, local_id, &element) ||
            element.screen_id != screen_id ||
            element.local_id != local_id ||
            element.page >= screen.page_count ||
            !rect_is_positive(element.rect) ||
            !rect_inside_display(
                element.rect,
                profile->display_width,
                profile->display_height)) {
            return false;
        }
    }
    for (focus_index = 0;
         focus_index < screen.focus_count;
         focus_index++) {
        PalUiLayoutElement element;
        if (!PalUiLayout_GetFocusElement(
                screen_id, focus_index, &element) ||
            element.screen_id != screen_id ||
            (element.flags & PAL_UI_LAYOUT_ELEMENT_VISIBLE) == 0 ||
            (element.flags & PAL_UI_LAYOUT_ELEMENT_SELECTABLE) == 0) {
            return false;
        }
    }
    return true;
}

static bool validate_sampling(
    uint16_t sampling_id,
    const PalUiLayoutProfile *profile)
{
    PalUiLayoutSampling sampling;
    bool visible;
    if (!PalUiLayout_GetSampling(sampling_id, &sampling) ||
        sampling.role != sampling_id ||
        sampling.asset_class >= PAL_UI_ASSET_CLASS_COUNT ||
        sampling.denominator == 0) {
        return false;
    }
    visible =
        (sampling.flags & PAL_UI_LAYOUT_SAMPLING_VISIBLE) != 0;
    if ((sampling.flags & PAL_UI_LAYOUT_SAMPLING_CATALOG) != 0 &&
        (!visible ||
         sampling.asset_class == PAL_UI_LAYOUT_ASSET_CLASS_NONE)) {
        return false;
    }
    if (!visible) {
        return sampling.filter == PAL_UI_LAYOUT_FILTER_NONE &&
               sampling.numerator == 0 &&
               sampling.denominator == 1 &&
               sampling.destination_width == 0 &&
               sampling.destination_height == 0 &&
               sampling.scale_q16 == 0 &&
               sampling.step_x_q16 == 0 &&
               sampling.step_y_q16 == 0 &&
               sampling.phase_x_q16 == 0 &&
               sampling.phase_y_q16 == 0 &&
               rect_is_zero(sampling.destination);
    }
    if (sampling.filter != PAL_UI_LAYOUT_FILTER_NEAREST_CENTER &&
        sampling.filter != PAL_UI_LAYOUT_FILTER_BOX_2X2) {
        return false;
    }
    if (sampling.numerator == 0 ||
        sampling.source_width == 0 ||
        sampling.source_height == 0 ||
        sampling.destination_width == 0 ||
        sampling.destination_height == 0 ||
        sampling.fixed_q_shift != PAL_UI_LAYOUT_Q16_SHIFT ||
        sampling.fixed_q_shift != profile->fixed_q_shift ||
        sampling.scale_q16 !=
            ((uint64_t)sampling.numerator <<
                sampling.fixed_q_shift) /
                sampling.denominator ||
        sampling.step_x_q16 !=
            ((uint64_t)sampling.source_width <<
                sampling.fixed_q_shift) /
                sampling.destination_width ||
        sampling.step_y_q16 !=
            ((uint64_t)sampling.source_height <<
                sampling.fixed_q_shift) /
                sampling.destination_height ||
        sampling.phase_x_q16 != sampling.step_x_q16 / 2u ||
        sampling.phase_y_q16 != sampling.step_y_q16 / 2u ||
        sampling.destination_width < sampling.min_width ||
        sampling.destination_width > sampling.max_width ||
        sampling.destination_height < sampling.min_height ||
        sampling.destination_height > sampling.max_height ||
        (sampling.screen_id != PAL_UI_LAYOUT_NO_SCREEN &&
         sampling.screen_id >= profile->screen_count)) {
        return false;
    }
    if (sampling.screen_id != PAL_UI_LAYOUT_NO_SCREEN) {
        PalUiLayoutScreen screen;
        if (!PalUiLayout_GetScreen(sampling.screen_id, &screen) ||
            sampling.page >= screen.page_count) {
            return false;
        }
    }
    if ((sampling.flags & PAL_UI_LAYOUT_SAMPLING_LEGACY_STAGE) != 0) {
        if (rect_is_zero(sampling.destination)) {
            return true;
        }
    }
    return sampling.destination.width == sampling.destination_width &&
           sampling.destination.height == sampling.destination_height &&
           rect_inside_display(
               sampling.destination,
               profile->display_width,
               profile->display_height);
}

static bool validate_camera(
    uint16_t vector_id,
    const PalUiLayoutProfile *profile)
{
    PalUiLayoutCameraVector camera;
    PalUiLayoutPoint focus;
    PalUiLayoutPoint projected;
    PalUiLayoutRect projected_source;
    if (!PalUiLayout_GetCameraVector(vector_id, &camera) ||
        !rect_is_positive(camera.source) ||
        !rect_is_positive(camera.screen) ||
        !rect_inside_display(
            camera.screen,
            profile->display_width,
            profile->display_height) ||
        camera.camera_x != camera.source.x ||
        camera.camera_y != camera.source.y ||
        camera.scale_numerator == 0 ||
        camera.scale_denominator == 0 ||
        camera.fixed_q_shift != PAL_UI_LAYOUT_Q16_SHIFT ||
        camera.fixed_q_shift != profile->fixed_q_shift ||
        camera.scale_q16 !=
            ((uint64_t)camera.scale_numerator <<
                camera.fixed_q_shift) /
                camera.scale_denominator ||
        camera.source_step_q16 !=
            ((uint64_t)camera.scale_denominator <<
                camera.fixed_q_shift) /
                camera.scale_numerator ||
        camera.source_phase_q16 != camera.source_step_q16 / 2u ||
        camera.screen.width !=
            (uint32_t)camera.source.width *
                camera.scale_numerator /
                camera.scale_denominator ||
        camera.screen.height !=
            (uint32_t)camera.source.height *
                camera.scale_numerator /
                camera.scale_denominator) {
        return false;
    }
    focus.x = camera.focus_x;
    focus.y = camera.focus_y;
    if (!PalUiLayout_ProjectCameraPoint(
            &camera, focus, &projected) ||
        projected.x != camera.projected_focus_x ||
        projected.y != camera.projected_focus_y ||
        projected.x < camera.screen.x ||
        projected.y < camera.screen.y ||
        projected.x >= (int32_t)camera.screen.x + camera.screen.width ||
        projected.y >= (int32_t)camera.screen.y + camera.screen.height ||
        !PalUiLayout_ProjectCameraRect(
            &camera, camera.source, &projected_source) ||
        projected_source.x != camera.screen.x ||
        projected_source.y != camera.screen.y ||
        projected_source.width != camera.screen.width ||
        projected_source.height != camera.screen.height) {
        return false;
    }
    return true;
}

static bool validate_battle_policy(
    const PalUiLayoutProfile *profile)
{
    PalUiLayoutBattleCameraPolicy policy;
    uint16_t index;
    if (!PalUiLayout_GetBattleCameraPolicy(&policy) ||
        !rect_is_positive(policy.arena) ||
        !rect_is_positive(policy.content_rect) ||
        !rect_is_positive(policy.focus_screen) ||
        !rect_is_positive(policy.fit_screen) ||
        !rect_inside_rect(policy.content_rect, profile->safe_rect) ||
        !rect_inside_rect(policy.focus_screen, policy.content_rect) ||
        !rect_inside_rect(policy.fit_screen, policy.content_rect) ||
        (!rect_is_zero(policy.hud_rect) &&
         (!rect_inside_rect(policy.hud_rect, profile->safe_rect) ||
          rects_intersect(policy.hud_rect, policy.content_rect))) ||
        policy.focus_source_width == 0 ||
        policy.focus_source_height == 0 ||
        policy.focus_source_width > policy.arena.width ||
        policy.focus_source_height > policy.arena.height ||
        policy.focus_screen.width != policy.focus_source_width ||
        policy.focus_screen.height != policy.focus_source_height ||
        policy.fit_numerator == 0 ||
        policy.fit_denominator == 0 ||
        policy.fixed_q_shift != PAL_UI_LAYOUT_Q16_SHIFT ||
        policy.fixed_q_shift != profile->fixed_q_shift ||
        policy.fit_scale_q16 !=
            ((uint64_t)policy.fit_numerator <<
                policy.fixed_q_shift) /
                policy.fit_denominator ||
        policy.fit_step_q16 !=
            ((uint64_t)policy.fit_denominator <<
                policy.fixed_q_shift) /
                policy.fit_numerator ||
        policy.fit_phase_q16 != policy.fit_step_q16 / 2u ||
        ((uint64_t)policy.arena.width *
            policy.fit_numerator) %
                policy.fit_denominator != 0 ||
        ((uint64_t)policy.arena.height *
            policy.fit_numerator) %
                policy.fit_denominator != 0 ||
        policy.fit_screen.width !=
            (uint64_t)policy.arena.width *
                policy.fit_numerator /
                policy.fit_denominator ||
        policy.fit_screen.height !=
            (uint64_t)policy.arena.height *
                policy.fit_numerator /
                policy.fit_denominator ||
        policy.max_players != PAL_UI_LAYOUT_BATTLE_MAX_PLAYERS) {
        return false;
    }
    for (index = 0;
         index < PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT;
         index++) {
        uint16_t source;
        uint64_t expected =
            ((uint64_t)index * 2u + 1u) *
                policy.arena.width /
                ((uint64_t)policy.fit_screen.width * 2u);
        if (!PalUiLayout_BattleFitSourceX(index, &source) ||
            source != expected ||
            source >= policy.arena.width) {
            return false;
        }
    }
    for (index = 0;
         index < PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT;
         index++) {
        uint16_t source;
        uint64_t expected =
            ((uint64_t)index * 2u + 1u) *
                policy.arena.height /
                ((uint64_t)policy.fit_screen.height * 2u);
        if (!PalUiLayout_BattleFitSourceY(index, &source) ||
            source != expected ||
            source >= policy.arena.height) {
            return false;
        }
    }
    if (PalUiLayout_BattleFitSourceX(
            PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_X_COUNT,
            &index) ||
        PalUiLayout_BattleFitSourceY(
            PAL_UI_GENERATED_BATTLE_FIT_SAMPLE_Y_COUNT,
            &index)) {
        return false;
    }
    return true;
}

#if defined(PAL_UI_GENERATED_PUBLIC_ABI_VERSION) && \
    PAL_UI_GENERATED_PUBLIC_ABI_VERSION == 1u
static bool validate_list_template(uint16_t screen_id)
{
    PalUiLayoutListTemplate list;
    PalUiLayoutElement slot;
    uint16_t page_count;
    uint16_t index;
    if (!PalUiLayout_GetListTemplate(screen_id, &list) ||
        list.screen_id != screen_id ||
        list.slot_count == 0 ||
        list.rows == 0 ||
        list.columns == 0 ||
        !PalUiLayout_GetListPageCount(&list, 0, &page_count) ||
        page_count != 1 ||
        !PalUiLayout_GetListPageCount(
            &list, list.maximum_items, &page_count) ||
        page_count != list.maximum_page_count ||
        PalUiLayout_GetListPageCount(
            &list,
            (uint16_t)(list.maximum_items + 1u),
            &page_count)) {
        return false;
    }
    for (index = 0; index < list.slot_count; index++) {
        if (!PalUiLayout_GetListSlot(screen_id, index, &slot) ||
            slot.page != 0 ||
            slot.kind != PAL_UI_LAYOUT_ELEMENT_ACTION ||
            (slot.flags &
             PAL_UI_LAYOUT_ELEMENT_SELECTABLE) == 0) {
            return false;
        }
    }
    return !PalUiLayout_GetListSlot(
        screen_id, list.slot_count, &slot);
}
#endif

bool PalUiLayout_ValidateGenerated(void)
{
    PalUiLayoutProfile profile;
    uint16_t screen_id;
    uint16_t sampling_id;
    uint16_t vector_id;
    uint16_t element_first = 0;
    uint16_t focus_first = 0;
    if (!PalUiLayout_GetProfile(&profile) ||
        profile.display_width == 0 ||
        profile.display_height == 0 ||
        profile.font_cell_width == 0 ||
        profile.font_cell_height == 0 ||
        profile.font_line_height == 0 ||
        profile.font_line_height < profile.font_cell_height ||
        profile.stage_source_width == 0 ||
        profile.stage_source_height == 0 ||
        profile.fixed_q_shift != PAL_UI_LAYOUT_Q16_SHIFT ||
        profile.stage_scale_numerator == 0 ||
        profile.stage_scale_denominator == 0 ||
        !rect_is_positive(profile.safe_rect) ||
        !rect_is_positive(profile.stage_rect) ||
        profile.stage_scale_q16 !=
            ((uint64_t)profile.stage_scale_numerator <<
                profile.fixed_q_shift) /
                profile.stage_scale_denominator ||
        profile.stage_rect.width !=
            (uint32_t)profile.stage_source_width *
                profile.stage_scale_numerator /
                profile.stage_scale_denominator ||
        profile.stage_rect.height !=
            (uint32_t)profile.stage_source_height *
                profile.stage_scale_numerator /
                profile.stage_scale_denominator ||
        profile.stage_step_x_q16 !=
            ((uint64_t)profile.stage_source_width <<
                profile.fixed_q_shift) /
                profile.stage_rect.width ||
        profile.stage_step_y_q16 !=
            ((uint64_t)profile.stage_source_height <<
                profile.fixed_q_shift) /
                profile.stage_rect.height ||
        profile.stage_phase_x_q16 !=
            profile.stage_step_x_q16 / 2u ||
        profile.stage_phase_y_q16 !=
            profile.stage_step_y_q16 / 2u ||
#if PAL_UI_GENERATED_SAMPLING_CATALOG_COUNT > 0
        PAL_UI_GENERATED_SAMPLING_CATALOG_COUNT >
            profile.sampling_count ||
#endif
        !rect_inside_display(
            profile.safe_rect,
            profile.display_width,
            profile.display_height) ||
        !rect_inside_display(
            profile.stage_rect,
            profile.display_width,
            profile.display_height) ||
        !validate_battle_policy(&profile)) {
        return false;
    }
    for (screen_id = 0; screen_id < profile.screen_count; screen_id++) {
        PalUiLayoutScreen screen;
        if (!validate_screen(
                screen_id,
                &profile,
                element_first,
                focus_first) ||
            !PalUiLayout_GetScreen(screen_id, &screen)) {
            return false;
        }
        element_first =
            (uint16_t)(element_first + screen.element_count);
        focus_first = (uint16_t)(focus_first + screen.focus_count);
    }
    if (element_first != profile.element_count ||
        focus_first != profile.focus_count) {
        return false;
    }
    for (sampling_id = 0;
         sampling_id < profile.sampling_count;
         sampling_id++) {
        if (!validate_sampling(sampling_id, &profile)) {
            return false;
        }
    }
    for (vector_id = 0;
         vector_id < profile.camera_vector_count;
         vector_id++) {
        if (!validate_camera(vector_id, &profile)) {
            return false;
        }
    }
#if defined(PAL_UI_GENERATED_PUBLIC_ABI_VERSION) && \
    PAL_UI_GENERATED_PUBLIC_ABI_VERSION == 1u
    if (!validate_list_template(PAL_UI_LAYOUT_SCREEN_ITEM) ||
        !validate_list_template(PAL_UI_LAYOUT_SCREEN_MAGIC)) {
        return false;
    }
#endif
    return true;
}
