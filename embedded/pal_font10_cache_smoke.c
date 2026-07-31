#define _XOPEN_SOURCE 700

#include "pal_font10_cache.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct stat st;
    const uint8_t *image;
    PalPack pack;
    PalFont10Cache cache;
    PalFont10Glyph glyph;
    int fd;
    int result = 0;

    if (argc != 2) {
        return 64;
    }
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        return 65;
    }
    if (fstat(fd, &st) != 0 ||
        st.st_size <= 0 ||
        (uint64_t)st.st_size > UINT32_MAX) {
        close(fd);
        return 66;
    }
    image = (const uint8_t *)mmap(
        NULL,
        (size_t)st.st_size,
        PROT_READ,
        MAP_PRIVATE,
        fd,
        0);
    close(fd);
    if (image == MAP_FAILED) {
        return 67;
    }

    if (!PalPack_OpenConst(&pack, image, (uint32_t)st.st_size)) {
        result = 1;
    } else if (!PalFont10_Open(&pack, &cache)) {
        result = 2;
    } else if (cache.glyph_count < 2u ||
               cache.cell_width != PAL_FONT10_CELL_WIDTH ||
               cache.cell_height != PAL_FONT10_CELL_HEIGHT ||
               (uint16_t)cache.ascent + cache.descent !=
                   PAL_FONT10_CELL_HEIGHT) {
        result = 3;
    } else if (!PalFont10_FindGlyph(&cache, 0x0020u, &glyph) ||
               glyph.codepoint != 0x0020u ||
               glyph.advance == 0u ||
               glyph.bitmap == NULL) {
        result = 4;
    } else if (!PalFont10_FindGlyph(&cache, 0x4ed9u, &glyph) ||
               glyph.codepoint != 0x4ed9u ||
               glyph.advance == 0u ||
               glyph.bitmap == NULL) {
        result = 5;
    } else if (PalFont10_FindGlyph(&cache, 0x0000u, &glyph) ||
               glyph.bitmap != NULL ||
               glyph.codepoint != 0u ||
               glyph.advance != 0u) {
        result = 6;
    }

    munmap((void *)image, (size_t)st.st_size);
    return result;
}
