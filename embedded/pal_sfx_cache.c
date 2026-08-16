#include "pal_sfx_cache.h"

#include "pal_memory.h"

#include <stddef.h>

static PalSfxEntry pal_sfx_entries[PAL_SFX_MAX_BANKED_SOUNDS];

static uint32_t align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

bool PalSfx_LoadBank(const PalPack *tf_pack, const uint16_t *chunk_nums, uint16_t count, PalSfxBank *sfx_bank)
{
    uint16_t i;
    uint32_t cursor = 0;

    if (sfx_bank == NULL || count > PAL_SFX_MAX_BANKED_SOUNDS) {
        return false;
    }
    sfx_bank->entry_count = 0;
    sfx_bank->used_bytes = 0;
    sfx_bank->entries = pal_sfx_entries;
    sfx_bank->data = pal_psram_sfx_bank;

    if (count != 0 && chunk_nums == NULL) {
        return false;
    }

    for (i = 0; i < count; i++) {
        PalPackSpan span;
        uint32_t copied = 0;
        uint32_t offset = align4(cursor);

        if (!PalPack_MapConst(tf_pack, PAL_PACK_ARCHIVE_SFX, chunk_nums[i], &span)) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (span.format != PAL_PACK_FORMAT_SFX_PCM8) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (offset > PAL_PSRAM_SFX_BANK_BYTES || span.size > PAL_PSRAM_SFX_BANK_BYTES - offset) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (!PalPack_CopyRaw(tf_pack, PAL_PACK_ARCHIVE_SFX, chunk_nums[i], pal_psram_sfx_bank + offset, span.size, &copied)) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (copied != span.size) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }

        pal_sfx_entries[i].offset = offset;
        pal_sfx_entries[i].size = span.size;
        pal_sfx_entries[i].chunk_num = chunk_nums[i];
        cursor = offset + span.size;
    }

    sfx_bank->entry_count = count;
    sfx_bank->used_bytes = cursor;
    return true;
}

bool PalSfx_LoadBankReadAt(
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const uint16_t *chunk_nums,
    uint16_t count,
    PalSfxBank *sfx_bank)
{
    uint16_t i;
    uint32_t cursor = 0;

    if (sfx_bank == NULL || read_at == NULL || count > PAL_SFX_MAX_BANKED_SOUNDS) {
        return false;
    }
    sfx_bank->entry_count = 0;
    sfx_bank->used_bytes = 0;
    sfx_bank->entries = pal_sfx_entries;
    sfx_bank->data = pal_psram_sfx_bank;

    if (count != 0 && chunk_nums == NULL) {
        return false;
    }

    for (i = 0; i < count; i++) {
        PalPackChunkInfo info;
        uint32_t copied = 0;
        uint32_t offset = align4(cursor);

        if (!PalPackToc_GetChunkInfo(tf_toc, PAL_PACK_ARCHIVE_SFX, chunk_nums[i], &info)) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (info.format != PAL_PACK_FORMAT_SFX_PCM8 || info.flags != 0u) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (offset > PAL_PSRAM_SFX_BANK_BYTES || info.size > PAL_PSRAM_SFX_BANK_BYTES - offset) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (!PalPackToc_CopyRawReadAt(tf_toc, read_at, user, PAL_PACK_ARCHIVE_SFX, chunk_nums[i], pal_psram_sfx_bank + offset, info.size, &copied)) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }
        if (copied != info.size) {
            sfx_bank->entry_count = 0;
            sfx_bank->used_bytes = 0;
            return false;
        }

        pal_sfx_entries[i].offset = offset;
        pal_sfx_entries[i].size = info.size;
        pal_sfx_entries[i].chunk_num = chunk_nums[i];
        cursor = offset + info.size;
    }

    sfx_bank->entry_count = count;
    sfx_bank->used_bytes = cursor;
    return true;
}

bool PalSfx_Get(const PalSfxBank *sfx_bank, uint16_t chunk_num, const uint8_t **data, uint32_t *size)
{
    uint16_t i;

    if (sfx_bank == NULL || data == NULL || size == NULL || sfx_bank->data == NULL || sfx_bank->entries == NULL) {
        return false;
    }

    for (i = 0; i < sfx_bank->entry_count; i++) {
        const PalSfxEntry *entry = &sfx_bank->entries[i];
        if (entry->chunk_num == chunk_num) {
            if (entry->offset > PAL_PSRAM_SFX_BANK_BYTES || entry->size > PAL_PSRAM_SFX_BANK_BYTES - entry->offset) {
                return false;
            }
            *data = sfx_bank->data + entry->offset;
            *size = entry->size;
            return true;
        }
    }

    return false;
}
