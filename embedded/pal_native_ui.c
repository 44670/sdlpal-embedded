#include "pal_native_ui.h"

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static PalNativeUiRect local_rect(
    int16_t x,
    int16_t y,
    uint16_t width,
    uint16_t height)
{
    PalNativeUiRect result;
    result.x = x;
    result.y = y;
    result.width = width;
    result.height = height;
    return result;
}

bool PalNativeUi_GetDialogLayout(
    bool lower,
    bool has_portrait,
    PalNativeUiDialogLayout *out)
{
    if (out == NULL) {
        return false;
    }

    if (lower) {
        out->portrait = local_rect(
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_X,
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_Y,
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_WIDTH,
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_PORTRAIT_HEIGHT);
        if (has_portrait) {
            out->text = local_rect(
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_HEIGHT);
        } else {
            out->text = local_rect(
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TEXT_NO_PORTRAIT_HEIGHT);
        }
        out->title_x = (int16_t)(has_portrait ?
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TITLE_X :
            PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TITLE_NO_PORTRAIT_X);
        out->title_y = PAL_NATIVE_UI_GENERATED_DIALOG_LOWER_TITLE_Y;
    } else {
        out->portrait = local_rect(
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_X,
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_Y,
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_WIDTH,
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_PORTRAIT_HEIGHT);
        if (has_portrait) {
            out->text = local_rect(
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_HEIGHT);
        } else {
            out->text = local_rect(
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_X,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_Y,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_WIDTH,
                PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TEXT_NO_PORTRAIT_HEIGHT);
        }
        out->title_x = (int16_t)(has_portrait ?
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TITLE_X :
            PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TITLE_NO_PORTRAIT_X);
        out->title_y = PAL_NATIVE_UI_GENERATED_DIALOG_UPPER_TITLE_Y;
    }
    out->line_height = PAL_NATIVE_UI_GENERATED_DIALOG_LINE_HEIGHT;
    out->page_lines = PAL_NATIVE_UI_GENERATED_DIALOG_PAGE_LINES;
    return true;
}

bool PalNativeUi_GetCenterDialogLayout(
    PalNativeUiDialogLayout *out)
{
    if (out == NULL) {
        return false;
    }
    out->portrait = local_rect(0, 0, 1u, 1u);
    out->text = local_rect(
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

static int16_t map_virtual_coordinate(
    int32_t value,
    uint16_t physical_extent,
    uint16_t virtual_extent)
{
    int32_t product = value * physical_extent;

    if (product >= 0) {
        return (int16_t)((product + virtual_extent / 2u) / virtual_extent);
    }
    return (int16_t)-(((-product) + virtual_extent / 2u) /
        virtual_extent);
}

int16_t PalNativeUi_MapVirtualX(int16_t x)
{
    return map_virtual_coordinate(
        x,
        PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH,
        PAL_NATIVE_UI_GENERATED_VIRTUAL_WIDTH);
}

int16_t PalNativeUi_MapVirtualY(int16_t y)
{
    return map_virtual_coordinate(
        y,
        PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT,
        PAL_NATIVE_UI_GENERATED_VIRTUAL_HEIGHT);
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

static bool rle_geometry(
    const uint8_t *rle,
    size_t rle_bytes,
    size_t *header_offset,
    uint16_t *source_width,
    uint16_t *source_height)
{
    size_t offset = 0u;

    if (rle == NULL || header_offset == NULL || source_width == NULL ||
        source_height == NULL) {
        return false;
    }
    if (rle_bytes >= 4u && rle[0] == 0x02u &&
        rle[1] == 0u && rle[2] == 0u && rle[3] == 0u) {
        offset = 4u;
    }
    if (rle_bytes < offset + 4u) {
        return false;
    }
    *source_width = read_le16(rle + offset);
    *source_height = read_le16(rle + offset + 2u);
    if (*source_width == 0u || *source_height == 0u) {
        return false;
    }
    *header_offset = offset;
    return true;
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

static bool blit_rle_scaled_indexed(
    const uint8_t *rle,
    size_t rle_bytes,
    uint8_t *pixels,
    uint16_t pitch,
    uint16_t surface_width,
    uint16_t surface_height,
    PalNativeUiRect destination,
    PalNativeUiRect *drawn)
{
    const uint8_t *cursor;
    const uint8_t *end;
    size_t header_offset;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t destination_x = 0u;
    uint16_t destination_y = 0u;
    uint32_t source_area;
    uint32_t source_cursor = 0u;
    uint32_t next_sample;

    if (pixels == NULL || pitch < surface_width ||
        destination.width == 0u || destination.height == 0u ||
        !rle_geometry(rle, rle_bytes, &header_offset,
            &source_width, &source_height)) {
        return false;
    }
    source_area = (uint32_t)source_width * source_height;
    cursor = rle + header_offset + 4u;
    end = rle + rle_bytes;
    next_sample = nearest_source_index(
        0u, 0u, destination.width, destination.height,
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
        while (destination_y < destination.height &&
               next_sample < source_end) {
            if (next_sample < source_cursor) {
                return false;
            }
            if (literal != NULL) {
                int32_t x = (int32_t)destination.x + destination_x;
                int32_t y = (int32_t)destination.y + destination_y;
                if (x >= 0 && x < surface_width &&
                    y >= 0 && y < surface_height) {
                    pixels[(size_t)y * pitch + (size_t)x] =
                        literal[next_sample - source_cursor];
                }
            }
            destination_x++;
            if (destination_x == destination.width) {
                destination_x = 0u;
                destination_y++;
            }
            if (destination_y < destination.height) {
                next_sample = nearest_source_index(
                    destination_x, destination_y,
                    destination.width, destination.height,
                    source_width, source_height);
            }
        }
        if (!transparent) {
            cursor += count;
        }
        source_cursor = source_end;
    }
    if (destination_y != destination.height) {
        return false;
    }
    if (drawn != NULL) {
        *drawn = destination;
    }
    return true;
}

bool PalNativeUi_BlitRleMappedIndexed(
    const uint8_t *rle,
    size_t rle_bytes,
    uint8_t *pixels,
    uint16_t pitch,
    uint16_t surface_width,
    uint16_t surface_height,
    int16_t virtual_x,
    int16_t virtual_y,
    PalNativeUiRect *drawn)
{
    size_t header_offset;
    uint16_t source_width;
    uint16_t source_height;
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
    PalNativeUiRect destination;

    if (surface_width != PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH ||
        surface_height != PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT ||
        !rle_geometry(rle, rle_bytes, &header_offset,
            &source_width, &source_height)) {
        return false;
    }
    (void)header_offset;
    left = PalNativeUi_MapVirtualX(virtual_x);
    top = PalNativeUi_MapVirtualY(virtual_y);
    right = map_virtual_coordinate(
        (int32_t)virtual_x + source_width,
        PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH,
        PAL_NATIVE_UI_GENERATED_VIRTUAL_WIDTH);
    bottom = map_virtual_coordinate(
        (int32_t)virtual_y + source_height,
        PAL_NATIVE_UI_GENERATED_DISPLAY_HEIGHT,
        PAL_NATIVE_UI_GENERATED_VIRTUAL_HEIGHT);
    if (right <= left) {
        right = (int16_t)(left + 1);
    }
    if (bottom <= top) {
        bottom = (int16_t)(top + 1);
    }
    destination.x = left;
    destination.y = top;
    destination.width = (uint16_t)(right - left);
    destination.height = (uint16_t)(bottom - top);
    return blit_rle_scaled_indexed(
        rle, rle_bytes, pixels, pitch, surface_width, surface_height,
        destination, drawn);
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
    size_t header_offset;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t destination_width;
    uint16_t destination_height;
    PalNativeUiRect destination;

    if (pixels == NULL || pitch < surface_width ||
        !rect_fits_surface(box, surface_width, surface_height) ||
        !rle_geometry(rle, rle_bytes, &header_offset,
            &source_width, &source_height) ||
        !fit_extent(source_width, source_height, box.width, box.height,
            &destination_width, &destination_height)) {
        return false;
    }
    (void)header_offset;
    destination.x = (int16_t)(box.x +
        (int32_t)(box.width - destination_width) / 2);
    destination.y = (int16_t)(box.y +
        (int32_t)(box.height - destination_height) / 2);
    destination.width = destination_width;
    destination.height = destination_height;
    return blit_rle_scaled_indexed(
        rle, rle_bytes, pixels, pitch, surface_width, surface_height,
        destination, drawn);
}
