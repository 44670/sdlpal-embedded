#include "pal_save_fatfs.h"

#include "cores3se_board.h"

#include <ff.h>
#include <stddef.h>

#define PAL_SAVE_COMMON_PREFIX_BYTES 44u

static FIL pal_save_file;
static uint8_t pal_sram_save_header[PAL_SAVE_COMMON_PREFIX_BYTES];

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
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

bool PalSaveFatFs_ReadHeader(const char *path, uint16_t *saved_times)
{
    FRESULT res;
    bool ok = false;

    if (path == NULL || saved_times == NULL) {
        return false;
    }

    *saved_times = 0;
    CoreS3Se_PrepareTfAccess();
    res = f_open(&pal_save_file, path, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK) {
        goto finish;
    }
    ok = f_size(&pal_save_file) >= PAL_SAVE_COMMON_PREFIX_BYTES &&
         read_exact(&pal_save_file, pal_sram_save_header, PAL_SAVE_COMMON_PREFIX_BYTES);
    f_close(&pal_save_file);
    if (!ok) {
        goto finish;
    }

    *saved_times = read_le16(pal_sram_save_header);

finish:
    return ok;
}
