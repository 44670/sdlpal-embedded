#include "pal_save_cache.h"

#include "pal_memory.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAL_SAVE_COMMON_PREFIX_BYTES 44u
#define PAL_SAVE_CASH_OFFSET 40u

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t checksum32(const uint8_t *data, uint32_t size)
{
    uint32_t i;
    uint32_t sum = 0;

    for (i = 0; i < size; i++) {
        sum = (sum << 5) - sum + data[i];
    }
    return sum;
}

static bool read_exact(int fd, uint8_t *dst, uint32_t size)
{
    uint32_t total = 0;

    while (total < size) {
        ssize_t got = read(fd, dst + total, (size_t)(size - total));
        if (got <= 0) {
            return false;
        }
        total += (uint32_t)got;
    }
    return true;
}

bool PalSave_ReadFile(const char *path, PalSaveSlot *slot)
{
    int fd;
    struct stat st;
    uint32_t size;

    if (path == NULL || slot == NULL) {
        return false;
    }

    slot->data = NULL;
    slot->size = 0;
    slot->saved_times = 0;
    slot->viewport_x = 0;
    slot->viewport_y = 0;
    slot->party_members = 0;
    slot->scene_num = 0;
    slot->music_num = 0;
    slot->battle_music_num = 0;
    slot->battle_field_num = 0;
    slot->cash = 0;
    slot->checksum = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    if (fstat(fd, &st) != 0 || st.st_size < (off_t)PAL_SAVE_COMMON_PREFIX_BYTES || st.st_size > (off_t)PAL_PSRAM_SAVE_STATE_BYTES) {
        close(fd);
        return false;
    }

    size = (uint32_t)st.st_size;
    if (!read_exact(fd, pal_psram_save_state, size)) {
        close(fd);
        return false;
    }
    close(fd);

    slot->data = pal_psram_save_state;
    slot->size = size;
    slot->saved_times = read_le16(pal_psram_save_state + 0u);
    slot->viewport_x = read_le16(pal_psram_save_state + 2u);
    slot->viewport_y = read_le16(pal_psram_save_state + 4u);
    slot->party_members = read_le16(pal_psram_save_state + 6u);
    slot->scene_num = read_le16(pal_psram_save_state + 8u);
    slot->music_num = read_le16(pal_psram_save_state + 14u);
    slot->battle_music_num = read_le16(pal_psram_save_state + 16u);
    slot->battle_field_num = read_le16(pal_psram_save_state + 18u);
    slot->cash = read_le32(pal_psram_save_state + PAL_SAVE_CASH_OFFSET);
    slot->checksum = checksum32(pal_psram_save_state, size);
    return true;
}
