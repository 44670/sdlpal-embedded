#include "pal_guru_screen.h"

#include <string.h>

enum {
    PAL_GURU_GLYPH_FIRST = 32,
    PAL_GURU_GLYPH_LAST = 95,
    PAL_GURU_GLYPH_WIDTH = 5,
    PAL_GURU_GLYPH_HEIGHT = 7,
    PAL_GURU_GLYPH_ADVANCE = 6,
};

/*
 * Panic text must not depend on TF, FONT10, an allocator, or mutable game
 * state.  Each byte below is one five-bit row of a 5x7 ASCII glyph.
 */
static const uint8_t pal_guru_font_5x7
    [PAL_GURU_GLYPH_LAST - PAL_GURU_GLYPH_FIRST + 1][PAL_GURU_GLYPH_HEIGHT] = {
    ['-' - PAL_GURU_GLYPH_FIRST] = {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00},
    ['.' - PAL_GURU_GLYPH_FIRST] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c},
    ['/' - PAL_GURU_GLYPH_FIRST] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10},
    ['0' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
    ['1' - PAL_GURU_GLYPH_FIRST] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    ['2' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
    ['3' - PAL_GURU_GLYPH_FIRST] = {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
    ['4' - PAL_GURU_GLYPH_FIRST] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
    ['5' - PAL_GURU_GLYPH_FIRST] = {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
    ['6' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e},
    ['7' - PAL_GURU_GLYPH_FIRST] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
    ['9' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e},
    [':' - PAL_GURU_GLYPH_FIRST] = {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00},
    ['?' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    ['A' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    ['B' - PAL_GURU_GLYPH_FIRST] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    ['C' - PAL_GURU_GLYPH_FIRST] = {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f},
    ['D' - PAL_GURU_GLYPH_FIRST] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
    ['E' - PAL_GURU_GLYPH_FIRST] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
    ['F' - PAL_GURU_GLYPH_FIRST] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    ['G' - PAL_GURU_GLYPH_FIRST] = {0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f},
    ['H' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    ['I' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
    ['J' - PAL_GURU_GLYPH_FIRST] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c},
    ['K' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    ['L' - PAL_GURU_GLYPH_FIRST] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
    ['M' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
    ['N' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11},
    ['O' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    ['P' - PAL_GURU_GLYPH_FIRST] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
    ['Q' - PAL_GURU_GLYPH_FIRST] = {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
    ['R' - PAL_GURU_GLYPH_FIRST] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
    ['S' - PAL_GURU_GLYPH_FIRST] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
    ['T' - PAL_GURU_GLYPH_FIRST] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    ['V' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
    ['W' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11},
    ['X' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
    ['Y' - PAL_GURU_GLYPH_FIRST] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
    ['Z' - PAL_GURU_GLYPH_FIRST] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
    ['_' - PAL_GURU_GLYPH_FIRST] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f},
};

static size_t
bounded_length(
    const char *text,
    size_t capacity)
{
    size_t length = 0u;

    if (text == NULL) {
        return 0u;
    }
    while (length < capacity && text[length] != '\0') {
        length++;
    }
    return length;
}

static void
append_text(
    char *destination,
    size_t capacity,
    size_t *used,
    const char *source)
{
    if (destination == NULL || capacity == 0u || used == NULL || source == NULL) {
        return;
    }
    while (*source != '\0' && *used + 1u < capacity) {
        destination[*used] = *source;
        (*used)++;
        source++;
    }
    destination[*used] = '\0';
}

static void
append_decimal(
    char *destination,
    size_t capacity,
    size_t *used,
    uint32_t value)
{
    char reversed[10];
    size_t digits = 0u;

    do {
        reversed[digits++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u && digits < sizeof(reversed));
    while (digits != 0u && *used + 1u < capacity) {
        destination[*used] = reversed[--digits];
        (*used)++;
    }
    destination[*used] = '\0';
}

void
PalGuruScreen_BuildText(
    PalGuruScreenText *text,
    const char *file,
    uint32_t line,
    const char *revision,
    const char *reason)
{
    const char *base = file;
    const char *cursor;
    size_t used;

    if (text == NULL) {
        return;
    }
    memset(text, 0, sizeof(*text));
    if (base == NULL || base[0] == '\0') {
        base = "UNKNOWN";
    } else {
        for (cursor = base; *cursor != '\0'; cursor++) {
            if (*cursor == '/' || *cursor == '\\') {
                base = cursor + 1;
            }
        }
    }

    used = 0u;
    append_text(text->reason, sizeof(text->reason), &used,
        reason != NULL && reason[0] != '\0' ? reason : "FATAL ERROR");
    used = 0u;
    append_text(text->file, sizeof(text->file), &used, "FILE ");
    append_text(text->file, sizeof(text->file), &used, base);
    used = 0u;
    append_text(text->line, sizeof(text->line), &used, "LINE ");
    append_decimal(text->line, sizeof(text->line), &used, line);
    used = 0u;
    append_text(text->revision, sizeof(text->revision), &used, "GIT ");
    append_text(text->revision, sizeof(text->revision), &used,
        revision != NULL && revision[0] != '\0' ? revision : "UNKNOWN");
}

static unsigned char
font_character(
    unsigned char character)
{
    if (character >= 'a' && character <= 'z') {
        character = (unsigned char)(character - 'a' + 'A');
    }
    if (character < PAL_GURU_GLYPH_FIRST ||
        character > PAL_GURU_GLYPH_LAST) {
        character = '?';
    }
    return character;
}

static void
draw_line(
    uint16_t *pixels,
    uint16_t width,
    uint16_t first_y,
    uint16_t rows,
    const char *text,
    uint16_t y,
    uint16_t scale,
    uint16_t color)
{
    size_t length = bounded_length(text, PAL_GURU_TEXT_COLUMNS - 1u);
    uint32_t line_width;
    uint16_t origin_x;
    size_t index;

    if (length == 0u || scale == 0u) {
        return;
    }
    line_width = (uint32_t)length * PAL_GURU_GLYPH_ADVANCE * scale - scale;
    origin_x = line_width < width ? (uint16_t)((width - line_width) / 2u) : 0u;
    for (index = 0u; index < length; index++) {
        unsigned char character = font_character((unsigned char)text[index]);
        const uint8_t *glyph =
            pal_guru_font_5x7[character - PAL_GURU_GLYPH_FIRST];
        uint16_t glyph_y;

        for (glyph_y = 0u; glyph_y < PAL_GURU_GLYPH_HEIGHT; glyph_y++) {
            uint16_t glyph_x;

            for (glyph_x = 0u; glyph_x < PAL_GURU_GLYPH_WIDTH; glyph_x++) {
                uint16_t scale_y;

                if ((glyph[glyph_y] & (uint8_t)(0x10u >> glyph_x)) == 0u) {
                    continue;
                }
                for (scale_y = 0u; scale_y < scale; scale_y++) {
                    uint32_t destination_y =
                        (uint32_t)y + (uint32_t)glyph_y * scale + scale_y;
                    uint16_t scale_x;

                    if (destination_y < first_y ||
                        destination_y >= (uint32_t)first_y + rows) {
                        continue;
                    }
                    for (scale_x = 0u; scale_x < scale; scale_x++) {
                        uint32_t destination_x = (uint32_t)origin_x +
                            (uint32_t)index * PAL_GURU_GLYPH_ADVANCE * scale +
                            (uint32_t)glyph_x * scale + scale_x;

                        if (destination_x < width) {
                            pixels[(destination_y - first_y) * width +
                                destination_x] = color;
                        }
                    }
                }
            }
        }
    }
}

bool
PalGuruScreen_RenderRgb565Strip(
    const PalGuruScreenText *text,
    uint16_t *pixels,
    size_t pixel_capacity,
    uint16_t width,
    uint16_t height,
    uint16_t first_y,
    uint16_t rows,
    uint16_t background,
    uint16_t title_color,
    uint16_t detail_color)
{
    size_t count;
    size_t index;

    if (text == NULL || pixels == NULL || width == 0u || height == 0u ||
        rows == 0u || first_y >= height || rows > height - first_y ||
        (size_t)width > SIZE_MAX / rows) {
        return false;
    }
    count = (size_t)width * rows;
    if (count > pixel_capacity) {
        return false;
    }
    for (index = 0u; index < count; index++) {
        pixels[index] = background;
    }

    draw_line(pixels, width, first_y, rows,
        "GURU", 4u, 2u, title_color);
    draw_line(pixels, width, first_y, rows,
        "MEDITATION", 22u, 2u, title_color);
    draw_line(pixels, width, first_y, rows,
        text->reason, 43u, 1u, detail_color);
    draw_line(pixels, width, first_y, rows,
        text->file, 56u, 1u, detail_color);
    draw_line(pixels, width, first_y, rows,
        text->line, 69u, 1u, detail_color);
    draw_line(pixels, width, first_y, rows,
        text->revision, 82u, 1u, detail_color);
    draw_line(pixels, width, first_y, rows,
        "HALTED", 101u, 2u, title_color);
    return true;
}
