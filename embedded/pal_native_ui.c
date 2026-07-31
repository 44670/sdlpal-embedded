#include "pal_native_ui.h"

#include <limits.h>

typedef struct PalNativeUiState {
    uint16_t source_x;
    uint16_t source_y;
    uint8_t kind;
    bool initialized;
} PalNativeUiState;

/* Six bytes of named state; all geometry remains generated read-only data. */
static PalNativeUiState pal_ui_native_viewport;

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static void set_world_origin(uint8_t kind)
{
    pal_ui_native_viewport.source_x =
        PAL_NATIVE_UI_GENERATED_WORLD_ORIGIN_X;
    pal_ui_native_viewport.source_y =
        PAL_NATIVE_UI_GENERATED_WORLD_ORIGIN_Y;
    pal_ui_native_viewport.kind = kind;
    pal_ui_native_viewport.initialized = true;
}

void PalNativeUi_SetWorldView(void)
{
    set_world_origin(PAL_NATIVE_UI_VIEW_WORLD);
}

void PalNativeUi_SetDialogView(void)
{
    set_world_origin(PAL_NATIVE_UI_VIEW_DIALOG);
}

void PalNativeUi_FocusLogical(
    int16_t logical_x,
    int16_t logical_y,
    PalNativeUiViewKind kind)
{
    int32_t max_x = (int32_t)PAL_NATIVE_UI_GENERATED_LOGICAL_WIDTH -
        PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH;
    int32_t max_y = (int32_t)PAL_NATIVE_UI_GENERATED_LOGICAL_HEIGHT -
        PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT;
    int32_t desired_x = (int32_t)logical_x -
        PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH / 2;
    int32_t desired_y = (int32_t)logical_y -
        PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT / 2;

    pal_ui_native_viewport.source_x = (uint16_t)clamp_i32(
        desired_x, 0, max_x);
    pal_ui_native_viewport.source_y = (uint16_t)clamp_i32(
        desired_y, 0, max_y);
    pal_ui_native_viewport.kind = (uint8_t)kind;
    pal_ui_native_viewport.initialized = true;
}

bool PalNativeUi_GetViewport(PalNativeUiViewport *out)
{
    if (out == NULL) {
        return false;
    }
    if (!pal_ui_native_viewport.initialized) {
        PalNativeUi_SetWorldView();
    }
    if ((uint32_t)pal_ui_native_viewport.source_x +
            PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH >
            PAL_NATIVE_UI_GENERATED_LOGICAL_WIDTH ||
        (uint32_t)pal_ui_native_viewport.source_y +
            PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT >
            PAL_NATIVE_UI_GENERATED_LOGICAL_HEIGHT) {
        return false;
    }
    out->source_x = pal_ui_native_viewport.source_x;
    out->source_y = pal_ui_native_viewport.source_y;
    out->width = PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH;
    out->height = PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT;
    out->kind = pal_ui_native_viewport.kind;
    return true;
}

static PalNativeUiRect local_to_logical(
    PalNativeUiViewport viewport,
    int16_t x,
    int16_t y,
    uint16_t width,
    uint16_t height)
{
    PalNativeUiRect result;
    result.x = (int16_t)(viewport.source_x + x);
    result.y = (int16_t)(viewport.source_y + y);
    result.width = width;
    result.height = height;
    return result;
}

bool PalNativeUi_GetDialogLayout(
    bool lower,
    bool has_portrait,
    PalNativeUiDialogLayout *out)
{
    PalNativeUiViewport viewport;

    if (out == NULL || !PalNativeUi_GetViewport(&viewport)) {
        return false;
    }
    if (viewport.kind != PAL_NATIVE_UI_VIEW_DIALOG) {
        PalNativeUi_SetDialogView();
        if (!PalNativeUi_GetViewport(&viewport)) {
            return false;
        }
    }

    if (lower) {
        out->portrait = local_to_logical(
            viewport,
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_X,
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_Y,
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_WIDTH,
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_HEIGHT);
        if (has_portrait) {
            out->text = local_to_logical(
                viewport,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_HEIGHT);
        } else {
            out->text = local_to_logical(
                viewport,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_HEIGHT);
        }
        out->title_x = (int16_t)(viewport.source_x +
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TITLE_X);
        out->title_y = (int16_t)(viewport.source_y +
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TITLE_Y);
    } else {
        out->portrait = local_to_logical(
            viewport,
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_X,
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_Y,
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_WIDTH,
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_HEIGHT);
        if (has_portrait) {
            out->text = local_to_logical(
                viewport,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_HEIGHT);
        } else {
            out->text = local_to_logical(
                viewport,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_HEIGHT);
        }
        out->title_x = (int16_t)(viewport.source_x +
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TITLE_X);
        out->title_y = (int16_t)(viewport.source_y +
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TITLE_Y);
    }
    out->line_height = PAL_NATIVE_UI_GENERATED_DIALOG_LINE_HEIGHT;
    out->page_lines = PAL_NATIVE_UI_GENERATED_DIALOG_PAGE_LINES;
    return true;
}

bool PalNativeUi_GetCenterDialogLayout(
    PalNativeUiDialogLayout *out)
{
    PalNativeUiViewport viewport;

    if (out == NULL) {
        return false;
    }
    PalNativeUi_SetDialogView();
    if (!PalNativeUi_GetViewport(&viewport)) {
        return false;
    }
    out->portrait = local_to_logical(viewport, 0, 0, 1u, 1u);
    out->text = local_to_logical(
        viewport,
        PAL_NATIVE_UI_GENERATED_DIALOG_CENTER_TEXT_X,
        PAL_NATIVE_UI_GENERATED_DIALOG_CENTER_TEXT_Y,
        PAL_NATIVE_UI_GENERATED_DIALOG_CENTER_TEXT_WIDTH,
        PAL_NATIVE_UI_GENERATED_DIALOG_CENTER_TEXT_HEIGHT);
    out->title_x = out->text.x;
    out->title_y = out->text.y;
    out->line_height = PAL_NATIVE_UI_GENERATED_DIALOG_LINE_HEIGHT;
    out->page_lines = PAL_NATIVE_UI_GENERATED_DIALOG_PAGE_LINES;
    return true;
}

bool PalNativeUi_Font10IdentityMatches(
    uint32_t glyph_count,
    uint32_t image_bytes,
    uint32_t payload_crc32,
    uint8_t cell_width,
    uint8_t cell_height,
    uint8_t ascent,
    uint8_t descent)
{
    return glyph_count == PAL_NATIVE_UI_GENERATED_FONT_GLYPH_COUNT &&
        image_bytes == PAL_NATIVE_UI_GENERATED_FONT_IMAGE_BYTES &&
        payload_crc32 == PAL_NATIVE_UI_GENERATED_FONT_PAYLOAD_CRC32 &&
        cell_width == PAL_NATIVE_UI_GENERATED_FONT_CELL_WIDTH &&
        cell_height == PAL_NATIVE_UI_GENERATED_FONT_CELL_HEIGHT &&
        ascent == PAL_NATIVE_UI_GENERATED_FONT_ASCENT &&
        descent == PAL_NATIVE_UI_GENERATED_FONT_DESCENT;
}

bool PalNativeUi_DrawFont10Glyph(
    const uint8_t *bitmap,
    uint8_t *pixels,
    uint16_t pitch,
    uint16_t surface_width,
    uint16_t surface_height,
    int16_t x,
    int16_t y,
    uint8_t color)
{
    uint16_t glyph_y;

    if (bitmap == NULL || pixels == NULL || pitch < surface_width) {
        return false;
    }
    for (glyph_y = 0;
         glyph_y < PAL_NATIVE_UI_GENERATED_FONT_CELL_HEIGHT;
         glyph_y++) {
        uint16_t glyph_x;
        int32_t destination_y = (int32_t)y + glyph_y;
        if (destination_y < 0 || destination_y >= surface_height) {
            continue;
        }
        for (glyph_x = 0;
             glyph_x < PAL_NATIVE_UI_GENERATED_FONT_CELL_WIDTH;
             glyph_x++) {
            uint32_t bit = (uint32_t)glyph_y *
                PAL_NATIVE_UI_GENERATED_FONT_CELL_WIDTH + glyph_x;
            int32_t destination_x = (int32_t)x + glyph_x;
            if (destination_x >= 0 && destination_x < surface_width &&
                (bitmap[bit >> 3] & (uint8_t)(0x80u >> (bit & 7u))) != 0u) {
                pixels[(size_t)destination_y * pitch + destination_x] = color;
            }
        }
    }
    return true;
}

static bool rect_fits_surface(
    PalNativeUiRect rect,
    uint16_t surface_width,
    uint16_t surface_height)
{
    return rect.width != 0u && rect.height != 0u &&
        rect.x >= 0 && rect.y >= 0 &&
        (uint32_t)rect.x + rect.width <= surface_width &&
        (uint32_t)rect.y + rect.height <= surface_height;
}

static bool fit_extent(
    uint16_t source_width,
    uint16_t source_height,
    uint16_t max_width,
    uint16_t max_height,
    uint16_t *destination_width,
    uint16_t *destination_height)
{
    uint32_t width;
    uint32_t height;

    if (source_width == 0u || source_height == 0u ||
        max_width == 0u || max_height == 0u ||
        destination_width == NULL || destination_height == NULL) {
        return false;
    }
    if (source_width <= max_width && source_height <= max_height) {
        *destination_width = source_width;
        *destination_height = source_height;
        return true;
    }
    if ((uint32_t)max_width * source_height <=
        (uint32_t)max_height * source_width) {
        width = max_width;
        height = ((uint32_t)source_height * width + source_width / 2u) /
            source_width;
    } else {
        height = max_height;
        width = ((uint32_t)source_width * height + source_height / 2u) /
            source_height;
    }
    if (width == 0u) {
        width = 1u;
    }
    if (height == 0u) {
        height = 1u;
    }
    if (width > max_width || height > max_height ||
        width > source_width || height > source_height) {
        return false;
    }
    *destination_width = (uint16_t)width;
    *destination_height = (uint16_t)height;
    return true;
}

static uint32_t nearest_source_index(
    uint16_t destination_x,
    uint16_t destination_y,
    uint16_t destination_width,
    uint16_t destination_height,
    uint16_t source_width,
    uint16_t source_height)
{
    uint32_t source_x =
        ((uint32_t)(2u * destination_x + 1u) * source_width) /
        ((uint32_t)2u * destination_width);
    uint32_t source_y =
        ((uint32_t)(2u * destination_y + 1u) * source_height) /
        ((uint32_t)2u * destination_height);
    return source_y * source_width + source_x;
}

bool PalNativeUi_BlitRleFitIndexed(
    const uint8_t *rle,
    size_t rle_bytes,
    uint8_t *pixels,
    uint16_t pitch,
    uint16_t surface_width,
    uint16_t surface_height,
    PalNativeUiRect box,
    PalNativeUiRect *drawn)
{
    const uint8_t *cursor;
    const uint8_t *end;
    size_t header_offset = 0u;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t destination_width;
    uint16_t destination_height;
    uint16_t destination_x = 0u;
    uint16_t destination_y = 0u;
    uint32_t source_area;
    uint32_t source_cursor = 0u;
    uint32_t next_sample;
    PalNativeUiRect destination;

    if (rle == NULL || pixels == NULL || pitch < surface_width ||
        !rect_fits_surface(box, surface_width, surface_height)) {
        return false;
    }
    if (rle_bytes >= 4u && rle[0] == 0x02u &&
        rle[1] == 0u && rle[2] == 0u && rle[3] == 0u) {
        header_offset = 4u;
    }
    if (rle_bytes < header_offset + 4u) {
        return false;
    }
    source_width = read_le16(rle + header_offset);
    source_height = read_le16(rle + header_offset + 2u);
    if (!fit_extent(source_width, source_height, box.width, box.height,
            &destination_width, &destination_height)) {
        return false;
    }
    source_area = (uint32_t)source_width * source_height;
    if (source_area == 0u) {
        return false;
    }
    destination.x = (int16_t)(box.x +
        (int32_t)(box.width - destination_width) / 2);
    destination.y = (int16_t)(box.y +
        (int32_t)(box.height - destination_height) / 2);
    destination.width = destination_width;
    destination.height = destination_height;
    if (!rect_fits_surface(destination, surface_width, surface_height)) {
        return false;
    }

    cursor = rle + header_offset + 4u;
    end = rle + rle_bytes;
    next_sample = nearest_source_index(
        0u, 0u, destination_width, destination_height,
        source_width, source_height);
    while (source_cursor < source_area) {
        uint8_t token;
        uint32_t count;
        uint32_t source_end;
        bool transparent;
        const uint8_t *literal;

        if (cursor >= end) {
            return false;
        }
        token = *cursor++;
        transparent = (token & 0x80u) != 0u &&
            token <= (uint32_t)0x80u + source_width;
        count = transparent ? (uint32_t)token - 0x80u : token;
        if (count == 0u || count > source_area - source_cursor ||
            (!transparent && (size_t)(end - cursor) < count)) {
            return false;
        }
        literal = transparent ? NULL : cursor;
        source_end = source_cursor + count;
        while (destination_y < destination_height &&
               next_sample < source_end) {
            if (next_sample < source_cursor) {
                return false;
            }
            if (literal != NULL) {
                size_t offset =
                    (size_t)(destination.y + destination_y) * pitch +
                    (uint16_t)(destination.x + destination_x);
                pixels[offset] = literal[next_sample - source_cursor];
            }
            destination_x++;
            if (destination_x == destination_width) {
                destination_x = 0u;
                destination_y++;
            }
            if (destination_y < destination_height) {
                next_sample = nearest_source_index(
                    destination_x, destination_y,
                    destination_width, destination_height,
                    source_width, source_height);
            }
        }
        if (!transparent) {
            cursor += count;
        }
        source_cursor = source_end;
    }
    if (destination_y != destination_height) {
        return false;
    }
    if (drawn != NULL) {
        *drawn = destination;
    }
    return true;
}
