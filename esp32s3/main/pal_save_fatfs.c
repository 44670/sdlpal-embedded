#include "pal_save_fatfs.h"

#include "cores3se_board.h"

#include "../../embedded/pal_memory.h"

#include <ff.h>
#include <stddef.h>

#define PAL_SAVE_COMMON_PREFIX_BYTES 44u
#define PAL_SAVE_CASH_OFFSET 40u

static FIL pal_save_file;
static uint8_t pal_sram_save_header[PAL_SAVE_COMMON_PREFIX_BYTES];

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

static bool read_exact(FIL *file, uint8_t *dst, uint32_t size)
{
    uint32_t done = 0;

    while (done < size) {
        UINT got = 0;
        FRESULT res = f_read(file, dst + done, size - done, &got);
        if (res != FR_OK || got == 0) {
            return false;
        }
        done += got;
    }
    return true;
}

static bool write_exact(FIL *file, const uint8_t *src, uint32_t size)
{
    uint32_t done = 0;

    while (done < size) {
        UINT wrote = 0;
        FRESULT res = f_write(file, src + done, size - done, &wrote);
        if (res != FR_OK || wrote == 0) {
            return false;
        }
        done += wrote;
    }
    return true;
}

bool PalSaveFatFs_ReadHeader(const char *path, uint16_t *saved_times)
{
    FRESULT res;
    bool ok;

    if (path == NULL || saved_times == NULL) {
        return false;
    }

    *saved_times = 0;
    CoreS3Se_PrepareTfAccess();
    res = f_open(&pal_save_file, path, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK) {
        return false;
    }
    ok = f_size(&pal_save_file) >= PAL_SAVE_COMMON_PREFIX_BYTES &&
         read_exact(&pal_save_file, pal_sram_save_header, PAL_SAVE_COMMON_PREFIX_BYTES);
    f_close(&pal_save_file);
    if (!ok) {
        return false;
    }

    *saved_times = read_le16(pal_sram_save_header);
    return true;
}

bool PalSaveFatFs_ReadFile(const char *path, PalFatFsSaveSlot *slot)
{
    FSIZE_t file_size;
    uint32_t size;
    FRESULT res;

    if (path == NULL || slot == NULL) {
        return false;
    }

    slot->data = NULL;
    slot->size = 0;
    slot->saved_times = 0;
    slot->viewport_x = 0;
    slot->viewport_y = 0;
    slot->scene_num = 0;
    slot->cash = 0;

    CoreS3Se_PrepareTfAccess();
    res = f_open(&pal_save_file, path, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK) {
        return false;
    }

    file_size = f_size(&pal_save_file);
    if (file_size < PAL_SAVE_COMMON_PREFIX_BYTES || file_size > PAL_PSRAM_SAVE_STATE_BYTES) {
        f_close(&pal_save_file);
        return false;
    }

    size = (uint32_t)file_size;
    if (!read_exact(&pal_save_file, pal_psram_save_state, size)) {
        f_close(&pal_save_file);
        return false;
    }
    f_close(&pal_save_file);

    slot->data = pal_psram_save_state;
    slot->size = size;
    slot->saved_times = read_le16(pal_psram_save_state);
    slot->viewport_x = read_le16(pal_psram_save_state + 2u);
    slot->viewport_y = read_le16(pal_psram_save_state + 4u);
    slot->scene_num = read_le16(pal_psram_save_state + 8u);
    slot->cash = read_le32(pal_psram_save_state + PAL_SAVE_CASH_OFFSET);
    return true;
}

bool PalSaveFatFs_WriteFile(const char *path, const uint8_t *data, uint32_t size)
{
    FRESULT res;
    bool ok;

    if (path == NULL || (data == NULL && size != 0u) || size > PAL_PSRAM_SAVE_STATE_BYTES) {
        return false;
    }

    CoreS3Se_PrepareTfAccess();
    res = f_open(&pal_save_file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        return false;
    }

    ok = write_exact(&pal_save_file, data, size);
    if (f_close(&pal_save_file) != FR_OK) {
        ok = false;
    }
    return ok;
}
