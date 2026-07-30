#include "../embedded/pal_pack.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *archive_name(uint16_t archive_id)
{
    switch (archive_id) {
    case PAL_PACK_ARCHIVE_ABC: return "ABC";
    case PAL_PACK_ARCHIVE_BALL: return "BALL";
    case PAL_PACK_ARCHIVE_DATA: return "DATA";
    case PAL_PACK_ARCHIVE_F: return "F";
    case PAL_PACK_ARCHIVE_FBP: return "FBP";
    case PAL_PACK_ARCHIVE_FIRE: return "FIRE";
    case PAL_PACK_ARCHIVE_GOP: return "GOP";
    case PAL_PACK_ARCHIVE_MAP: return "MAP";
    case PAL_PACK_ARCHIVE_MGO: return "MGO";
    case PAL_PACK_ARCHIVE_MIDI: return "MIDI";
    case PAL_PACK_ARCHIVE_MUS: return "MUS";
    case PAL_PACK_ARCHIVE_PAT: return "PAT";
    case PAL_PACK_ARCHIVE_RGM: return "RGM";
    case PAL_PACK_ARCHIVE_RNG: return "RNG";
    case PAL_PACK_ARCHIVE_SSS: return "SSS";
    case PAL_PACK_ARCHIVE_VOC: return "VOC";
    case PAL_PACK_ARCHIVE_TEXT: return "TEXT";
    case PAL_PACK_ARCHIVE_FONT: return "FONT";
    case PAL_PACK_ARCHIVE_SFX: return "SFX";
    case PAL_PACK_ARCHIVE_CACHE: return "CACHE";
    default: return "?";
    }
}

static int check_pack(const char *path)
{
    int fd;
    struct stat st;
    const uint8_t *mapped;
    PalPack pack;
    uint16_t archive_id;
    uint32_t archives = 0;
    uint64_t payload_bytes = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "%s: open failed: %s\n", path, strerror(errno));
        return 1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > UINT32_MAX) {
        fprintf(stderr, "%s: bad file size\n", path);
        close(fd);
        return 1;
    }

    mapped = (const uint8_t *)mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        fprintf(stderr, "%s: mmap failed: %s\n", path, strerror(errno));
        return 1;
    }

    if (!PalPack_OpenConst(&pack, mapped, (uint32_t)st.st_size)) {
        fprintf(stderr, "%s: not a valid PAL pack\n", path);
        munmap((void *)mapped, (size_t)st.st_size);
        return 1;
    }

    printf("%s: size=%" PRIdMAX "\n", path, (intmax_t)st.st_size);
    for (archive_id = PAL_PACK_ARCHIVE_ABC; archive_id <= PAL_PACK_ARCHIVE_CACHE; archive_id++) {
        uint16_t chunk_count = 0;
        uint16_t chunk_id;
        uint64_t archive_bytes = 0;

        if (!PalPack_GetChunkCount(&pack, archive_id, &chunk_count)) {
            continue;
        }

        archives++;
        for (chunk_id = 0; chunk_id < chunk_count; chunk_id++) {
            PalPackSpan span;
            if (!PalPack_MapConst(&pack, archive_id, chunk_id, &span)) {
                fprintf(stderr, "%s: bad chunk %s #%u\n", path, archive_name(archive_id), chunk_id);
                munmap((void *)mapped, (size_t)st.st_size);
                return 1;
            }
            archive_bytes += span.size;
        }
        payload_bytes += archive_bytes;
        printf("  %-4s chunks=%5u payload=%" PRIu64 "\n", archive_name(archive_id), chunk_count, archive_bytes);
    }
    printf("  archives=%" PRIu32 " payload=%" PRIu64 "\n", archives, payload_bytes);

    munmap((void *)mapped, (size_t)st.st_size);
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    int rc = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s PACK...\n", argv[0]);
        return 2;
    }

    for (i = 1; i < argc; i++) {
        if (check_pack(argv[i]) != 0) {
            rc = 1;
        }
    }
    return rc;
}
