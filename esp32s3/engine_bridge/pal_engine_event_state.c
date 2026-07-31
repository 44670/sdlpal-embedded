#include "pal_engine_event_state.h"

#include "pal_engine_pack_provider.h"
#include "pal_target_board.h"

#include <esp_timer.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PAL_EVENT_STATE_TEMPLATE_PATH "0:/EVENT.DEF"
#define PAL_EVENT_STATE_LIVE_PATH     "0:/EVENT.STA"
#define PAL_EVENT_STATE_TEMP_PATH     "0:/EVENT.TMP"
#define PAL_EVENT_STATE_BAD_PATH      "0:/EVENT.BAD"

#define PAL_EVENT_TEMPLATE_MAGIC "PALEVT1\0"
#define PAL_EVENT_TEMPLATE_VERSION 1u
#define PAL_EVENT_TEMPLATE_HEADER_BYTES 512u
#define PAL_EVENT_TEMPLATE_FILE_BYTES \
    (PAL_EVENT_TEMPLATE_HEADER_BYTES + \
        PAL_EVENT_JOURNAL_PAGE_COUNT * PAL_EVENT_PAGER_PAGE_BYTES)
#define PAL_EVENT_TEMPLATE_HEADER_CRC_OFFSET 508u
#define PAL_EVENT_TEMPLATE_PACK_SET_OFFSET 12u
#define PAL_EVENT_TEMPLATE_PAYLOAD_CRC_OFFSET 48u
#define PAL_EVENT_TEMPLATE_EVENT_CRC_OFFSET 52u
#define PAL_EVENT_TEMPLATE_SCENE_CRC_OFFSET 56u
#define PAL_EVENT_TEMPLATE_RESERVED_OFFSET 124u

#define PAL_EVENT_STATE_SCENE_DIRTY_WORDS \
    ((PAL_ENGINE_EVENT_STATE_SCENE_COUNT + 31u) / 32u)
#define PAL_EVENT_STATE_IO_RETRIES 3u
#ifndef PAL_EVENT_STATE_CHECKPOINT_US
#define PAL_EVENT_STATE_CHECKPOINT_US 10000000u
#endif
#define PAL_EVENT_STATE_OLD_SRAM_BYTES (424u * PAL_EVENT_PAGER_RECORD_BYTES)

#if defined(__GNUC__)
#define PAL_EVENT_STATE_SRAM \
    __attribute__((section(".bss.pal_sram"), aligned(8)))
#else
#define PAL_EVENT_STATE_SRAM
#endif

typedef char PalEventStateAssertEventRecord[
    PAL_EVENT_PAGER_RECORD_BYTES == 32u ? 1 : -1];
typedef char PalEventStateAssertSceneBytes[
    PAL_ENGINE_EVENT_STATE_SCENE_BYTES == 2400u ? 1 : -1];

typedef struct PalEventTemplateSource {
    FILE *file;
    uint32_t pack_set_id;
    uint32_t payload_crc32;
    uint32_t event_crc32;
    uint32_t scene_crc32;
    uint32_t running_payload_crc;
    uint32_t running_event_crc;
    uint32_t running_scene_crc;
    uint16_t next_page;
} PalEventTemplateSource;

typedef struct PalEventTransformSource {
    PalEventTemplateSource *template_source;
    PalEngineEventStatePageTransform transform;
    void *user;
} PalEventTransformSource;

typedef struct PalEventStateBookkeeping {
    PalEngineEventStateIoMetrics io_metrics;
    uint64_t scene_dirty_since_us;
    uint32_t scene_dirty[PAL_EVENT_STATE_SCENE_DIRTY_WORDS];
    uint8_t *scene_mirror;
    uint8_t initialized;
    uint8_t skip_default_reset;
} PalEventStateBookkeeping;

#define PAL_EVENT_STATE_REPLACED_SRAM_BYTES \
    (PAL_EVENT_PAGER_SLOT_COUNT * PAL_EVENT_PAGER_PAGE_BYTES + \
        sizeof(PalEventPager) + sizeof(PalEventJournal) + \
        PAL_EVENT_JOURNAL_COMMIT_BYTES + \
        sizeof(PalEventStateBookkeeping))

#if !defined(PAL_CORES3SE_NATIVE_ENGINE_HOST)
typedef char PalEventStateAssertReplacementBudget[
    PAL_EVENT_STATE_REPLACED_SRAM_BYTES <=
        PAL_EVENT_STATE_OLD_SRAM_BYTES ? 1 : -1];
#endif

uint8_t pal_sram_extreme_event_pages[
    PAL_EVENT_PAGER_SLOT_COUNT * PAL_EVENT_PAGER_PAGE_BYTES]
    PAL_EVENT_STATE_SRAM;
uint8_t pal_sram_extreme_event_pager[
    sizeof(PalEventPager)] PAL_EVENT_STATE_SRAM;
uint8_t pal_sram_extreme_event_journal[
    sizeof(PalEventJournal)] PAL_EVENT_STATE_SRAM;
uint8_t pal_sram_extreme_event_sector[
    PAL_EVENT_JOURNAL_COMMIT_BYTES] PAL_EVENT_STATE_SRAM;
uint8_t pal_sram_extreme_event_bookkeeping[
    sizeof(PalEventStateBookkeeping)] PAL_EVENT_STATE_SRAM;

static PalEventPager *
event_pager(void)
{
    return (PalEventPager *)(void *)pal_sram_extreme_event_pager;
}

static PalEventJournal *
event_journal(void)
{
    return (PalEventJournal *)(void *)pal_sram_extreme_event_journal;
}

static uint8_t (*
event_pages(void))[PAL_EVENT_PAGER_PAGE_BYTES]
{
    return (uint8_t (*)[PAL_EVENT_PAGER_PAGE_BYTES])
        (void *)pal_sram_extreme_event_pages;
}

static PalEventStateBookkeeping *
event_bookkeeping(void)
{
    return (PalEventStateBookkeeping *)
        (void *)pal_sram_extreme_event_bookkeeping;
}

#define pal_event_io_metrics \
    (event_bookkeeping()->io_metrics)
#define pal_event_scene_dirty_since_us \
    (event_bookkeeping()->scene_dirty_since_us)
#define pal_event_scene_dirty \
    (event_bookkeeping()->scene_dirty)
#define pal_event_scene_mirror \
    (event_bookkeeping()->scene_mirror)
#define pal_event_state_initialized \
    (event_bookkeeping()->initialized)
#define pal_event_state_skip_default_reset \
    (event_bookkeeping()->skip_default_reset)

static uint16_t
read_le16(
    const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t
read_le32(
    const uint8_t *p)
{
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

static void
write_le32(
    uint8_t *p,
    uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t
crc32_update(
    uint32_t crc,
    const uint8_t *bytes,
    uint32_t size)
{
    uint32_t i;

    for (i = 0; i < size; i++) {
        uint32_t value = crc ^ bytes[i];
        uint8_t bit;

        for (bit = 0; bit < 8u; bit++) {
            value = (value >> 1) ^
                (0xedb88320u & (uint32_t)-(int32_t)(value & 1u));
        }
        crc = value;
    }
    return crc;
}

static uint32_t
popcount32(
    uint32_t value)
{
    uint32_t count = 0u;

    while (value != 0u) {
        value &= value - 1u;
        count++;
    }
    return count;
}

static uint64_t
event_now_us(void)
{
    int64_t value = esp_timer_get_time();

    return value > 0 ? (uint64_t)value : 0u;
}

static bool
file_seek_read(
    FILE *file,
    uint32_t offset,
    uint8_t *dst,
    uint32_t size)
{
    uint8_t attempt;

    if (file == NULL || (dst == NULL && size != 0u) ||
        offset > (uint32_t)LONG_MAX) {
        return false;
    }
    for (attempt = 0u; attempt < PAL_EVENT_STATE_IO_RETRIES; attempt++) {
        size_t got;

        pal_event_io_metrics.read_calls++;
        if (fseek(file, (long)offset, SEEK_SET) == 0) {
            got = fread(dst, 1u, size, file);
            pal_event_io_metrics.read_bytes += got;
            if (got == size) {
                return true;
            }
        }
    }
    pal_event_io_metrics.read_failures++;
    return false;
}

static bool
file_seek_write(
    FILE *file,
    uint32_t offset,
    const uint8_t *src,
    uint32_t size)
{
    uint8_t attempt;

    if (file == NULL || (src == NULL && size != 0u) ||
        offset > (uint32_t)LONG_MAX) {
        return false;
    }
    for (attempt = 0u; attempt < PAL_EVENT_STATE_IO_RETRIES; attempt++) {
        size_t done;

        pal_event_io_metrics.write_calls++;
        if (fseek(file, (long)offset, SEEK_SET) == 0) {
            done = fwrite(src, 1u, size, file);
            pal_event_io_metrics.write_bytes += done;
            if (done == size) {
                return true;
            }
        }
    }
    pal_event_io_metrics.write_failures++;
    return false;
}

static bool
file_sync(
    FILE *file)
{
    uint8_t attempt;

    if (file == NULL) {
        return false;
    }
    for (attempt = 0u; attempt < PAL_EVENT_STATE_IO_RETRIES; attempt++) {
        pal_event_io_metrics.sync_calls++;
        if (fflush(file) == 0) {
            return true;
        }
    }
    pal_event_io_metrics.sync_failures++;
    return false;
}

static bool
journal_read_at(
    void *user,
    uint32_t offset,
    uint8_t *dst,
    uint32_t size)
{
    return file_seek_read((FILE *)user, offset, dst, size);
}

static bool
journal_write_at(
    void *user,
    uint32_t offset,
    const uint8_t *src,
    uint32_t size)
{
    return file_seek_write((FILE *)user, offset, src, size);
}

static bool
journal_sync(
    void *user)
{
    return file_sync((FILE *)user);
}

static uint64_t
journal_now_us(
    void *user)
{
    (void)user;
    return event_now_us();
}

static bool
state_file_size(
    FILE *file,
    uint32_t *size)
{
    long value;

    if (file == NULL || size == NULL ||
        fseek(file, 0L, SEEK_END) != 0 ||
        (value = ftell(file)) < 0 ||
        (unsigned long)value > UINT32_MAX) {
        return false;
    }
    *size = (uint32_t)value;
    return true;
}

static bool
state_file_has_expected_size(
    FILE *file)
{
    uint32_t size;

    return state_file_size(file, &size) &&
        size == PAL_EVENT_JOURNAL_FILE_BYTES;
}

static bool
preallocate_state_file(
    FILE *file)
{
    uint8_t *sector = pal_sram_extreme_event_sector;
    uint32_t offset;
    uint64_t before_bytes = pal_event_io_metrics.write_bytes;
    uint64_t before_calls = pal_event_io_metrics.write_calls;
    uint64_t before_syncs = pal_event_io_metrics.sync_calls;
    uint64_t start_us = event_now_us();

    memset(sector, 0, PAL_EVENT_JOURNAL_COMMIT_BYTES);
    for (offset = 0u;
         offset < PAL_EVENT_JOURNAL_FILE_BYTES;
         offset += PAL_EVENT_JOURNAL_COMMIT_BYTES) {
        if (!file_seek_write(
                file,
                offset,
                sector,
                PAL_EVENT_JOURNAL_COMMIT_BYTES)) {
            return false;
        }
    }
    if (!file_sync(file) || !state_file_has_expected_size(file)) {
        return false;
    }
    printf(
        "PAL_TFIO v=1 op=preallocate ts_us=%" PRIu64
        " api_write_b=%" PRIu64 " write_calls=%" PRIu64
        " sync_calls=%" PRIu64 " elapsed_us=%" PRIu64
        " result=ok\n",
        start_us,
        pal_event_io_metrics.write_bytes - before_bytes,
        pal_event_io_metrics.write_calls - before_calls,
        pal_event_io_metrics.sync_calls - before_syncs,
        event_now_us() - start_us);
    return true;
}

static bool
template_open(
    PalEventTemplateSource *source)
{
    uint8_t *header = pal_sram_extreme_event_sector;
    uint32_t active_pack_set_id;
    uint32_t declared_header_crc;
    uint32_t actual_header_crc;
    uint16_t i;
    long file_bytes;

    if (source == NULL ||
        !PalEngineBridge_GetActivePackSetId(&active_pack_set_id)) {
        return false;
    }
    memset(source, 0, sizeof(*source));
    source->file = fopen(PAL_EVENT_STATE_TEMPLATE_PATH, "rb");
    if (source->file == NULL ||
        !file_seek_read(
            source->file,
            0u,
            header,
            PAL_EVENT_TEMPLATE_HEADER_BYTES) ||
        fseek(source->file, 0L, SEEK_END) != 0 ||
        (file_bytes = ftell(source->file)) < 0 ||
        (uint32_t)file_bytes != PAL_EVENT_TEMPLATE_FILE_BYTES) {
        goto fail;
    }

    declared_header_crc = read_le32(
        header + PAL_EVENT_TEMPLATE_HEADER_CRC_OFFSET);
    write_le32(header + PAL_EVENT_TEMPLATE_HEADER_CRC_OFFSET, 0u);
    actual_header_crc = PalEventJournal_Crc32(
        header, PAL_EVENT_TEMPLATE_HEADER_BYTES);
    write_le32(
        header + PAL_EVENT_TEMPLATE_HEADER_CRC_OFFSET,
        declared_header_crc);
    if (memcmp(header, PAL_EVENT_TEMPLATE_MAGIC, 8u) != 0 ||
        read_le16(header + 8u) != PAL_EVENT_TEMPLATE_VERSION ||
        read_le16(header + 10u) != PAL_EVENT_TEMPLATE_HEADER_BYTES ||
        read_le32(header + PAL_EVENT_TEMPLATE_PACK_SET_OFFSET) !=
            active_pack_set_id ||
        read_le16(header + 16u) != PAL_EVENT_PAGER_RECORD_BYTES ||
        read_le16(header + 18u) != PAL_EVENT_PAGER_RECORD_COUNT ||
        read_le16(header + 20u) != 8u ||
        read_le16(header + 22u) !=
            PAL_ENGINE_EVENT_STATE_SCENE_COUNT ||
        read_le32(header + 24u) != PAL_EVENT_PAGER_PAGE_BYTES ||
        read_le16(header + 28u) != PAL_EVENT_PAGER_PAGE_COUNT ||
        read_le16(header + 30u) != PAL_EVENT_JOURNAL_PAGE_COUNT ||
        read_le32(header + 32u) !=
            PAL_EVENT_PAGER_RECORD_COUNT *
                PAL_EVENT_PAGER_RECORD_BYTES ||
        read_le32(header + 36u) !=
            PAL_ENGINE_EVENT_STATE_SCENE_BYTES ||
        read_le32(header + 40u) !=
            PAL_EVENT_TEMPLATE_HEADER_BYTES ||
        read_le32(header + 44u) !=
            PAL_EVENT_JOURNAL_PAGE_COUNT *
                PAL_EVENT_PAGER_PAGE_BYTES ||
        declared_header_crc != actual_header_crc) {
        goto fail;
    }
    for (i = PAL_EVENT_TEMPLATE_RESERVED_OFFSET;
         i < PAL_EVENT_TEMPLATE_HEADER_CRC_OFFSET;
         i++) {
        if (header[i] != 0u) {
            goto fail;
        }
    }
    source->pack_set_id = active_pack_set_id;
    source->payload_crc32 = read_le32(
        header + PAL_EVENT_TEMPLATE_PAYLOAD_CRC_OFFSET);
    source->event_crc32 = read_le32(
        header + PAL_EVENT_TEMPLATE_EVENT_CRC_OFFSET);
    source->scene_crc32 = read_le32(
        header + PAL_EVENT_TEMPLATE_SCENE_CRC_OFFSET);
    source->running_payload_crc = 0xffffffffu;
    source->running_event_crc = 0xffffffffu;
    source->running_scene_crc = 0xffffffffu;
    return true;

fail:
    if (source->file != NULL) {
        (void)fclose(source->file);
    }
    memset(source, 0, sizeof(*source));
    return false;
}

static void
template_close(
    PalEventTemplateSource *source)
{
    if (source != NULL && source->file != NULL) {
        (void)fclose(source->file);
        source->file = NULL;
    }
}

static bool
template_page_source(
    void *user,
    uint16_t logical_page,
    uint8_t page[PAL_EVENT_PAGER_PAGE_BYTES])
{
    PalEventTemplateSource *source =
        (PalEventTemplateSource *)user;
    uint32_t event_bytes = 0u;
    uint32_t i;

    if (source == NULL || source->file == NULL ||
        logical_page != source->next_page ||
        logical_page >= PAL_EVENT_JOURNAL_PAGE_COUNT ||
        !file_seek_read(
            source->file,
            PAL_EVENT_TEMPLATE_HEADER_BYTES +
                (uint32_t)logical_page *
                    PAL_EVENT_PAGER_PAGE_BYTES,
            page,
            PAL_EVENT_PAGER_PAGE_BYTES)) {
        return false;
    }

    source->running_payload_crc = crc32_update(
        source->running_payload_crc,
        page,
        PAL_EVENT_PAGER_PAGE_BYTES);
    if (logical_page < PAL_EVENT_PAGER_PAGE_COUNT) {
        event_bytes = PAL_EVENT_PAGER_PAGE_BYTES;
        if (logical_page == PAL_EVENT_PAGER_PAGE_COUNT - 1u) {
            event_bytes =
                (PAL_EVENT_PAGER_RECORD_COUNT %
                    PAL_EVENT_PAGER_RECORDS_PER_PAGE) *
                PAL_EVENT_PAGER_RECORD_BYTES;
            for (i = event_bytes;
                 i < PAL_EVENT_PAGER_PAGE_BYTES;
                 i++) {
                if (page[i] != 0u) {
                    return false;
                }
            }
        }
        source->running_event_crc = crc32_update(
            source->running_event_crc, page, event_bytes);
    } else {
        source->running_scene_crc = crc32_update(
            source->running_scene_crc,
            page,
            PAL_ENGINE_EVENT_STATE_SCENE_BYTES);
        for (i = PAL_ENGINE_EVENT_STATE_SCENE_BYTES;
             i < PAL_EVENT_PAGER_PAGE_BYTES;
             i++) {
            if (page[i] != 0u) {
                return false;
            }
        }
    }
    source->next_page++;
    if (source->next_page == PAL_EVENT_JOURNAL_PAGE_COUNT &&
        ((source->running_payload_crc ^ 0xffffffffu) !=
             source->payload_crc32 ||
         (source->running_event_crc ^ 0xffffffffu) !=
             source->event_crc32 ||
         (source->running_scene_crc ^ 0xffffffffu) !=
             source->scene_crc32)) {
        return false;
    }
    return true;
}

static void
journal_init_for_file(
    FILE *state_file)
{
    PalEventJournalIo journal_io;

    memset(&journal_io, 0, sizeof(journal_io));
    journal_io.read_at = journal_read_at;
    journal_io.write_at = journal_write_at;
    journal_io.sync = journal_sync;
    journal_io.now_us = journal_now_us;
    journal_io.user = state_file;
    PalEventJournal_Init(
        event_journal(),
        &journal_io,
        pal_sram_extreme_event_sector);
}

static bool
journal_open_and_rebase(
    FILE *state_file,
    PalEventTemplateSource *template_source,
    bool *skip_default_reset)
{
    PalEventPagerIoResult result;
    uint32_t old_profile_id;
    uint32_t old_template_crc32;

    if (state_file == NULL || template_source == NULL ||
        skip_default_reset == NULL) {
        return false;
    }
    *skip_default_reset = false;
    journal_init_for_file(state_file);

    /*
     * Always recover the newest internally valid generation first.  Opening
     * by the requested identity alone could select an older commit left by a
     * previous A->B resource rebase and resurrect stale A state when switching
     * back.  Identity comparison happens only after newest-generation
     * recovery.
     */
    if (!PalEventJournal_OpenAnyIdentity(
            event_journal(), event_pages()[0])) {
        return false;
    }
    if (event_journal()->profile_id == template_source->pack_set_id &&
        event_journal()->template_crc32 ==
            template_source->payload_crc32) {
        return true;
    }

    old_profile_id = event_journal()->profile_id;
    old_template_crc32 = event_journal()->template_crc32;
    memset(&result, 0, sizeof(result));
    if (old_template_crc32 == template_source->payload_crc32) {
        if (!PalEventJournal_RebindIdentity(
                event_journal(),
                template_source->pack_set_id,
                template_source->payload_crc32,
                event_pages()[0],
                &result)) {
            return false;
        }
        printf(
            "PAL_TFIO v=1 op=state_rebind old_profile=%08" PRIx32
            " profile=%08" PRIx32 " template=%08" PRIx32
            " generation=%" PRIu32 " api_write_b=%" PRIu64
            " sync_calls=%" PRIu32 " elapsed_us=%" PRIu64
            " preserved=1 result=ok\n",
            old_profile_id,
            template_source->pack_set_id,
            template_source->payload_crc32,
            event_journal()->generation,
            result.storage_bytes,
            result.sync_count,
            result.elapsed_us);
        return true;
    }
    if (template_source->next_page != 0u ||
        !PalEventJournal_ReplaceAll(
            event_journal(),
            template_source->pack_set_id,
            template_source->payload_crc32,
            template_page_source,
            template_source,
            event_pages()[0],
            &result)) {
        return false;
    }
    *skip_default_reset = true;
    printf(
        "PAL_TFIO v=1 op=state_rebase old_profile=%08" PRIx32
        " old_template=%08" PRIx32
        " profile=%08" PRIx32 " template=%08" PRIx32
        " generation=%" PRIu32 " api_write_b=%" PRIu64
        " sync_calls=%" PRIu32 " elapsed_us=%" PRIu64
        " result=ok\n",
        old_profile_id,
        old_template_crc32,
        template_source->pack_set_id,
        template_source->payload_crc32,
        event_journal()->generation,
        result.storage_bytes,
        result.sync_count,
        result.elapsed_us);
    return true;
}

static bool
format_state_temp(
    PalEventTemplateSource *template_source)
{
    PalEventPagerIoResult result;
    FILE *temp_file;
    bool ok;

    if (template_source == NULL || template_source->next_page != 0u ||
        !PalTarget_SaveUnlink(PAL_EVENT_STATE_TEMP_PATH, true)) {
        return false;
    }
    temp_file = fopen(PAL_EVENT_STATE_TEMP_PATH, "w+b");
    if (temp_file == NULL ||
        !preallocate_state_file(temp_file)) {
        if (temp_file != NULL) {
            (void)fclose(temp_file);
        }
        return false;
    }

    journal_init_for_file(temp_file);
    memset(&result, 0, sizeof(result));
    ok = PalEventJournal_FormatAll(
        event_journal(),
        template_source->pack_set_id,
        template_source->payload_crc32,
        template_page_source,
        template_source,
        event_pages()[0],
        &result);
    if (fclose(temp_file) != 0) {
        ok = false;
    }
    if (!ok) {
        return false;
    }
    printf(
        "PAL_TFIO v=1 op=state_provision profile=%08" PRIx32
        " template=%08" PRIx32 " api_write_b=%" PRIu64
        " sync_calls=%" PRIu32 " elapsed_us=%" PRIu64
        " result=ok\n",
        template_source->pack_set_id,
        template_source->payload_crc32,
        result.storage_bytes,
        result.sync_count,
        result.elapsed_us);
    return true;
}

static bool
promote_state_temp(
    bool live_exists)
{
    if (live_exists) {
        if (!PalTarget_SaveUnlink(PAL_EVENT_STATE_BAD_PATH, true) ||
            !PalTarget_SaveRename(
                PAL_EVENT_STATE_LIVE_PATH,
                PAL_EVENT_STATE_BAD_PATH)) {
            return false;
        }
    }
    if (!PalTarget_SaveRename(
            PAL_EVENT_STATE_TEMP_PATH,
            PAL_EVENT_STATE_LIVE_PATH)) {
        /*
         * If live_exists was true, EVENT.BAD preserves the rejected image and
         * the fully verified EVENT.TMP remains promotable on the next boot.
         * Do not restore a known-invalid live file over that recovery path.
         */
        return false;
    }
    printf(
        "PAL_TFIO v=1 op=state_install replaced_bad=%u result=ok\n",
        live_exists ? 1u : 0u);
    return true;
}

static bool
recover_or_build_state(
    PalEventTemplateSource *template_source,
    bool live_exists,
    bool *skip_default_reset)
{
    FILE *temp_file;
    uint32_t temp_size;
    bool state_prepared = false;
    bool temp_valid = false;
    int open_error;

    if (template_source == NULL || skip_default_reset == NULL) {
        return false;
    }
    *skip_default_reset = false;
    errno = 0;
    temp_file = fopen(PAL_EVENT_STATE_TEMP_PATH, "r+b");
    open_error = errno;
    if (temp_file != NULL) {
        if (!state_file_size(temp_file, &temp_size)) {
            (void)fclose(temp_file);
            return false;
        }
        if (temp_size == PAL_EVENT_JOURNAL_FILE_BYTES) {
            temp_valid = journal_open_and_rebase(
                temp_file, template_source, &state_prepared);
            if (!temp_valid && event_journal()->io_error) {
                (void)fclose(temp_file);
                return false;
            }
        }
        if (fclose(temp_file) != 0) {
            return false;
        }
        if (!temp_valid &&
            !PalTarget_SaveUnlink(PAL_EVENT_STATE_TEMP_PATH, false)) {
            return false;
        }
    } else if (open_error != ENOENT) {
        return false;
    }

    if (!temp_valid) {
        if (!format_state_temp(template_source)) {
            return false;
        }
        *skip_default_reset = true;
    } else if (state_prepared) {
        *skip_default_reset = true;
    }
    return promote_state_temp(live_exists);
}

static uint32_t
scene_dirty_count(void)
{
    uint32_t count = 0u;
    uint8_t i;

    for (i = 0u; i < PAL_EVENT_STATE_SCENE_DIRTY_WORDS; i++) {
        count += popcount32(pal_event_scene_dirty[i]);
    }
    return count;
}

static const char *
write_reason_name(
    PalEventPagerWriteReason reason)
{
    switch (reason) {
    case PAL_EVENT_WRITE_EVICT:
        return "evict";
    case PAL_EVENT_WRITE_SCENE:
        return "scene";
    case PAL_EVENT_WRITE_SAVE:
        return "save";
    case PAL_EVENT_WRITE_SYNC:
        return "sync";
    case PAL_EVENT_WRITE_SHUTDOWN:
        return "shutdown";
    default:
        return "invalid";
    }
}

static bool
event_journal_write(
    void *user,
    const PalEventPagerPageWrite *pages,
    uint8_t page_count,
    PalEventPagerWriteReason reason,
    PalEventPagerIoResult *result)
{
    PalEventJournal *journal = (PalEventJournal *)user;
    PalEventPagerPageWrite combined[PAL_EVENT_PAGER_SLOT_COUNT + 1u];
    const PalEventPagerMetrics *pager_metrics =
        PalEventPager_GetMetrics(event_pager());
    uint64_t before_write_bytes = pal_event_io_metrics.write_bytes;
    uint64_t before_write_calls = pal_event_io_metrics.write_calls;
    uint64_t before_syncs = pal_event_io_metrics.sync_calls;
    uint64_t before_write_failures =
        pal_event_io_metrics.write_failures;
    uint64_t before_sync_failures =
        pal_event_io_metrics.sync_failures;
    uint64_t logical_bytes = 0u;
    uint64_t timestamp = event_now_us();
    uint64_t oldest_dirty_us = 0u;
    uint32_t dirty_scenes = scene_dirty_count();
    uint8_t combined_count = page_count;
    uint8_t event_page_count = page_count;
    bool includes_scene = false;
    uint8_t i;
    bool ok;

    if (journal == NULL || pages == NULL || page_count == 0u ||
        page_count > PAL_EVENT_PAGER_SLOT_COUNT ||
        (dirty_scenes != 0u && pal_event_scene_mirror == NULL)) {
        return false;
    }
    memcpy(
        combined,
        pages,
        (size_t)page_count * sizeof(combined[0]));
    for (i = 0u; i < page_count; i++) {
        uint8_t word;

        if (pages[i].page == PAL_EVENT_JOURNAL_SCENE_PAGE) {
            if (includes_scene || pages[i].data != pal_event_scene_mirror) {
                return false;
            }
            includes_scene = true;
            event_page_count--;
            logical_bytes += (uint64_t)dirty_scenes * 8u;
            oldest_dirty_us = pages[i].first_dirty_us;
            continue;
        }
        if (pages[i].page >= PAL_EVENT_PAGER_PAGE_COUNT) {
            return false;
        }
        for (word = 0u; word < PAL_EVENT_PAGER_DIRTY_WORDS; word++) {
            logical_bytes +=
                (uint64_t)popcount32(pages[i].dirty_records[word]) *
                PAL_EVENT_PAGER_RECORD_BYTES;
        }
        if (pages[i].first_dirty_us != 0u &&
            (oldest_dirty_us == 0u ||
             pages[i].first_dirty_us < oldest_dirty_us)) {
            oldest_dirty_us = pages[i].first_dirty_us;
        }
    }
    if (dirty_scenes != 0u && !includes_scene) {
        PalEventPagerPageWrite *scene = &combined[combined_count++];

        memset(scene, 0, sizeof(*scene));
        scene->page = PAL_EVENT_JOURNAL_SCENE_PAGE;
        scene->data = pal_event_scene_mirror;
        scene->first_dirty_us = pal_event_scene_dirty_since_us;
        logical_bytes += (uint64_t)dirty_scenes * 8u;
        includes_scene = true;
        if (scene->first_dirty_us != 0u &&
            (oldest_dirty_us == 0u ||
             scene->first_dirty_us < oldest_dirty_us)) {
            oldest_dirty_us = scene->first_dirty_us;
        }
    }

    ok = PalEventJournal_WritePages(
        journal,
        combined,
        combined_count,
        reason,
        result);
    if (ok && dirty_scenes != 0u) {
        memset(
            pal_event_scene_dirty,
            0,
            sizeof(pal_event_scene_dirty));
        pal_event_scene_dirty_since_us = 0u;
    }
    printf(
        "PAL_TFIO v=1 op=state_commit seq=%" PRIu64
        " ts_us=%" PRIu64 " reason=%s reason_id=%u generation=%" PRIu32
        " event_pages=%u scene_page=%u dirty_records_b=%" PRIu64
        " api_write_b=%" PRIu64 " write_calls=%" PRIu64
        " sync_calls=%" PRIu64 " write_failures=%" PRIu64
        " sync_failures=%" PRIu64 " dirty_age_us=%" PRIu64
        " elapsed_us=%" PRIu64
        " hits=%" PRIu64 " misses=%" PRIu64
        " cum_write_b=%" PRIu64 " sw_wa_milli=%" PRIu64
        " result=%s\n",
        journal->sequence,
        timestamp,
        write_reason_name(reason),
        (unsigned)reason,
        journal->generation,
        (unsigned)event_page_count,
        includes_scene ? 1u : 0u,
        logical_bytes,
        pal_event_io_metrics.write_bytes - before_write_bytes,
        pal_event_io_metrics.write_calls - before_write_calls,
        pal_event_io_metrics.sync_calls - before_syncs,
        pal_event_io_metrics.write_failures - before_write_failures,
        pal_event_io_metrics.sync_failures - before_sync_failures,
        oldest_dirty_us != 0u && timestamp >= oldest_dirty_us
            ? timestamp - oldest_dirty_us : 0u,
        result != NULL ? result->elapsed_us : 0u,
        pager_metrics != NULL ? pager_metrics->hit_count : 0u,
        pager_metrics != NULL ? pager_metrics->miss_count : 0u,
        pal_event_io_metrics.write_bytes,
        logical_bytes != 0u
            ? ((pal_event_io_metrics.write_bytes - before_write_bytes) *
                1000u) / logical_bytes
            : 0u,
        ok ? "ok" : "fail");
    return ok;
}

static bool
event_journal_read(
    void *user,
    uint16_t page,
    uint8_t *dst,
    PalEventPagerIoResult *result)
{
    return PalEventJournal_ReadPage(
        (PalEventJournal *)user, page, dst, result);
}

static uint64_t
event_pager_now_us(
    void *user)
{
    (void)user;
    return event_now_us();
}

static bool
load_committed_scene(
    void *scene_table,
    uint32_t scene_table_bytes)
{
    if (scene_table == NULL ||
        scene_table_bytes != PAL_ENGINE_EVENT_STATE_SCENE_BYTES ||
        !PalEventJournal_ReadPage(
            event_journal(),
            PAL_EVENT_JOURNAL_SCENE_PAGE,
            event_pages()[0],
            NULL)) {
        return false;
    }
    memcpy(
        scene_table,
        event_pages()[0],
        PAL_ENGINE_EVENT_STATE_SCENE_BYTES);
    pal_event_scene_mirror = (uint8_t *)scene_table;
    memset(
        pal_event_scene_dirty,
        0,
        sizeof(pal_event_scene_dirty));
    pal_event_scene_dirty_since_us = 0u;
    return true;
}

bool
PalEngineEventState_Init(void)
{
    PalEventTemplateSource template_source;
    PalEventPagerIo pager_io;
    FILE *state_file = NULL;
    uint32_t state_size;
    bool state_ready = false;
    bool live_exists = false;
    bool skip_default_reset = false;
    bool state_prepared = false;
    int open_error;

    if (pal_event_state_initialized) {
        return true;
    }
    memset(&pal_event_io_metrics, 0, sizeof(pal_event_io_metrics));
    if (!template_open(&template_source)) {
        return false;
    }

    errno = 0;
    state_file = fopen(PAL_EVENT_STATE_LIVE_PATH, "r+b");
    open_error = errno;
    if (state_file != NULL) {
        live_exists = true;
        if (!state_file_size(state_file, &state_size)) {
            goto fail;
        }
        if (state_size == PAL_EVENT_JOURNAL_FILE_BYTES) {
            state_ready = journal_open_and_rebase(
                state_file, &template_source, &state_prepared);
            if (!state_ready && event_journal()->io_error) {
                goto fail;
            }
            if (state_prepared) {
                skip_default_reset = true;
            }
        }
    } else if (open_error != ENOENT) {
        /*
         * Never reinterpret EMFILE/EIO/not-ready as "missing": opening with
         * w+b in that case would truncate the only committed live state.
         */
        goto fail;
    }

    if (!state_ready) {
        if (state_file != NULL) {
            if (fclose(state_file) != 0) {
                state_file = NULL;
                goto fail;
            }
            state_file = NULL;
        }
        if (!recover_or_build_state(
                &template_source,
                live_exists,
                &skip_default_reset)) {
            goto fail;
        }
        state_file = fopen(PAL_EVENT_STATE_LIVE_PATH, "r+b");
        if (state_file == NULL ||
            !state_file_has_expected_size(state_file)) {
            goto fail;
        }
        state_prepared = false;
        if (!journal_open_and_rebase(
                state_file, &template_source, &state_prepared) ||
            state_prepared ||
            event_journal()->profile_id != template_source.pack_set_id ||
            event_journal()->template_crc32 !=
                template_source.payload_crc32) {
            goto fail;
        }
        state_ready = true;
    }

    memset(&pager_io, 0, sizeof(pager_io));
    pager_io.read_page = event_journal_read;
    pager_io.write_pages = event_journal_write;
    pager_io.now_us = event_pager_now_us;
    pager_io.user = event_journal();
    PalEventPager_Init(event_pager(), event_pages(), &pager_io);
    if (!event_pager()->initialized) {
        goto fail;
    }
    pal_event_state_initialized = 1u;
    pal_event_state_skip_default_reset =
        skip_default_reset ? 1u : 0u;
    template_close(&template_source);
    return true;

fail:
    template_close(&template_source);
    if (state_file != NULL) {
        (void)fclose(state_file);
    }
    memset(event_journal(), 0, sizeof(*event_journal()));
    memset(event_pager(), 0, sizeof(*event_pager()));
    pal_event_state_skip_default_reset = 0u;
    return false;
}

void
PalEngineEventState_Shutdown(void)
{
    const PalEventPagerMetrics *pager_metrics;
    const PalEventJournalMetrics *journal_metrics;
    FILE *state_file;
    bool flush_ok;
    bool close_ok = true;

    if (!pal_event_state_initialized) {
        return;
    }
    flush_ok = PalEngineEventState_Flush(PAL_EVENT_WRITE_SHUTDOWN);
    pager_metrics = PalEventPager_GetMetrics(event_pager());
    journal_metrics = PalEventJournal_GetMetrics(event_journal());
    state_file = (FILE *)event_journal()->io.user;
    if (state_file != NULL && fclose(state_file) != 0) {
        close_ok = false;
    }
    printf(
        "PAL_TFIO v=1 op=state_summary generation=%" PRIu32
        " cache_hits=%" PRIu64 " cache_misses=%" PRIu64
        " evictions=%" PRIu64 " dirty_evictions=%" PRIu64
        " logical_dirty_b=%" PRIu64
        " api_read_b=%" PRIu64 " api_write_b=%" PRIu64
        " read_calls=%" PRIu64 " write_calls=%" PRIu64
        " sync_calls=%" PRIu64 " read_failures=%" PRIu64
        " write_failures=%" PRIu64 " sync_failures=%" PRIu64
        " transactions=%" PRIu64 " replacements=%" PRIu64
        " flush_result=%s close_result=%s result=%s\n",
        event_journal()->generation,
        pager_metrics != NULL ? pager_metrics->hit_count : 0u,
        pager_metrics != NULL ? pager_metrics->miss_count : 0u,
        pager_metrics != NULL ? pager_metrics->eviction_count : 0u,
        pager_metrics != NULL
            ? pager_metrics->dirty_eviction_count : 0u,
        pager_metrics != NULL
            ? pager_metrics->logical_dirty_bytes : 0u,
        pal_event_io_metrics.read_bytes,
        pal_event_io_metrics.write_bytes,
        pal_event_io_metrics.read_calls,
        pal_event_io_metrics.write_calls,
        pal_event_io_metrics.sync_calls,
        pal_event_io_metrics.read_failures,
        pal_event_io_metrics.write_failures,
        pal_event_io_metrics.sync_failures,
        journal_metrics != NULL
            ? journal_metrics->transaction_count : 0u,
        journal_metrics != NULL
            ? journal_metrics->replacement_count : 0u,
        flush_ok ? "ok" : "fail",
        close_ok ? "ok" : "fail",
        flush_ok && close_ok ? "ok" : "fail");
    memset(event_journal(), 0, sizeof(*event_journal()));
    memset(event_pager(), 0, sizeof(*event_pager()));
    memset(
        pal_event_scene_dirty,
        0,
        sizeof(pal_event_scene_dirty));
    pal_event_scene_mirror = NULL;
    pal_event_scene_dirty_since_us = 0u;
    pal_event_state_initialized = 0u;
    pal_event_state_skip_default_reset = 0u;
}

bool
PalEngineEventState_IsReady(void)
{
    return pal_event_state_initialized &&
        event_journal()->ready &&
        !event_journal()->poisoned &&
        event_pager()->initialized;
}

bool
PalEngineEventState_GetIdentity(
    uint32_t *profile_id,
    uint32_t *template_crc32,
    uint32_t *generation)
{
    if (!pal_event_state_initialized ||
        profile_id == NULL ||
        template_crc32 == NULL ||
        generation == NULL ||
        event_journal()->profile_id == 0u) {
        return false;
    }
    *profile_id = event_journal()->profile_id;
    *template_crc32 = event_journal()->template_crc32;
    *generation = event_journal()->generation;
    return true;
}

static bool
transform_page_source(
    void *user,
    uint16_t logical_page,
    uint8_t page[PAL_EVENT_PAGER_PAGE_BYTES])
{
    PalEventTransformSource *source =
        (PalEventTransformSource *)user;

    return source != NULL && source->template_source != NULL &&
        source->transform != NULL &&
        template_page_source(
            source->template_source, logical_page, page) &&
        source->transform(source->user, logical_page, page);
}

bool
PalEngineEventState_ReplaceAll(
    PalEventJournalPageSource source,
    void *source_user,
    void *scene_table,
    uint32_t scene_table_bytes)
{
    PalEventPagerIoResult result;
    bool ok;

    if (!pal_event_state_initialized || source == NULL ||
        scene_table == NULL ||
        scene_table_bytes != PAL_ENGINE_EVENT_STATE_SCENE_BYTES ||
        PalEventPager_HasOutstandingHandles(event_pager()) ||
        !PalEventPager_Invalidate(event_pager())) {
        return false;
    }
    if (!event_journal()->ready) {
        return false;
    }
    ok = PalEventJournal_ReplaceAll(
        event_journal(),
        event_journal()->profile_id,
        event_journal()->template_crc32,
        source,
        source_user,
        event_pages()[0],
        &result);
    if (!ok || !load_committed_scene(
            scene_table, scene_table_bytes)) {
        return false;
    }
    pal_event_state_skip_default_reset = 0u;
    printf(
        "PAL_TFIO v=1 op=state_replace generation=%" PRIu32
        " api_write_b=%" PRIu64 " sync_calls=%" PRIu32
        " elapsed_us=%" PRIu64 " result=ok\n",
        event_journal()->generation,
        result.storage_bytes,
        result.sync_count,
        result.elapsed_us);
    return true;
}

bool
PalEngineEventState_TransformDefaults(
    PalEngineEventStatePageTransform transform,
    void *transform_user,
    void *scene_table,
    uint32_t scene_table_bytes)
{
    PalEventTemplateSource template_source;
    PalEventTransformSource source;
    PalEventPagerIoResult result;
    bool ok;

    if (!pal_event_state_initialized || transform == NULL ||
        scene_table == NULL ||
        scene_table_bytes != PAL_ENGINE_EVENT_STATE_SCENE_BYTES ||
        !event_journal()->ready ||
        PalEventPager_HasOutstandingHandles(event_pager())) {
        return false;
    }
    if (!template_open(&template_source)) {
        return false;
    }
    if (event_journal()->profile_id != template_source.pack_set_id ||
        event_journal()->template_crc32 !=
            template_source.payload_crc32 ||
        !PalEventPager_Invalidate(event_pager())) {
        template_close(&template_source);
        return false;
    }
    source.template_source = &template_source;
    source.transform = transform;
    source.user = transform_user;
    ok = PalEventJournal_ReplaceAll(
            event_journal(),
            event_journal()->profile_id,
            event_journal()->template_crc32,
            transform_page_source,
            &source,
            event_pages()[0],
            &result);
    template_close(&template_source);
    if (!ok ||
        !load_committed_scene(scene_table, scene_table_bytes)) {
        return false;
    }
    pal_event_state_skip_default_reset = 0u;
    printf(
        "PAL_TFIO v=1 op=state_transform generation=%" PRIu32
        " api_write_b=%" PRIu64 " sync_calls=%" PRIu32
        " elapsed_us=%" PRIu64 " result=ok\n",
        event_journal()->generation,
        result.storage_bytes,
        result.sync_count,
        result.elapsed_us);
    return true;
}

bool
PalEngineEventState_ResetDefaults(
    void *scene_table,
    uint32_t scene_table_bytes)
{
    PalEventTemplateSource source;
    bool ok;

    if (!pal_event_state_initialized || scene_table == NULL ||
        scene_table_bytes != PAL_ENGINE_EVENT_STATE_SCENE_BYTES) {
        return false;
    }
    if (pal_event_state_skip_default_reset) {
        if (!PalEventPager_Invalidate(event_pager()) ||
            !load_committed_scene(scene_table, scene_table_bytes)) {
            return false;
        }
        pal_event_state_skip_default_reset = 0u;
        return true;
    }
    if (!template_open(&source)) {
        return false;
    }
    if (event_journal()->profile_id == 0u) {
        event_journal()->profile_id = source.pack_set_id;
        event_journal()->template_crc32 = source.payload_crc32;
    }
    if (event_journal()->profile_id != source.pack_set_id ||
        event_journal()->template_crc32 != source.payload_crc32) {
        template_close(&source);
        return false;
    }
    ok = PalEngineEventState_ReplaceAll(
        template_page_source,
        &source,
        scene_table,
        scene_table_bytes);
    template_close(&source);
    return ok;
}

bool
PalEngineEventState_ReadEvent(
    uint16_t event_id,
    void *record,
    uint32_t record_bytes)
{
    PalEventPagerHandle handle;
    bool ok;

    if (!PalEngineEventState_IsReady() || record == NULL ||
        record_bytes != PAL_EVENT_PAGER_RECORD_BYTES ||
        !PalEventPager_Acquire(
            event_pager(), event_id, false, &handle)) {
        return false;
    }
    memcpy(record, handle.record, PAL_EVENT_PAGER_RECORD_BYTES);
    ok = PalEventPager_Release(event_pager(), &handle, false);
    return ok;
}

bool
PalEngineEventState_WriteEvent(
    uint16_t event_id,
    const void *record,
    uint32_t record_bytes)
{
    PalEventPagerHandle handle;
    bool changed;

    if (!PalEngineEventState_IsReady() || record == NULL ||
        record_bytes != PAL_EVENT_PAGER_RECORD_BYTES ||
        !PalEventPager_Acquire(
            event_pager(), event_id, true, &handle)) {
        return false;
    }
    changed = memcmp(
        handle.record, record, PAL_EVENT_PAGER_RECORD_BYTES) != 0;
    if (changed) {
        memcpy(
            handle.record, record, PAL_EVENT_PAGER_RECORD_BYTES);
    }
    return PalEventPager_Release(
        event_pager(), &handle, changed);
}

bool
PalEngineEventState_PinScene(
    uint16_t event_start,
    uint16_t event_count)
{
    return PalEngineEventState_IsReady() &&
        /*
         * PalEventPager_PinScene() flushes dirty event pages before changing
         * pins, but a scene-only mutation is tracked outside the pager.  Route
         * through the integration flush first so a scene transition is also a
         * durability boundary when no event record happened to be dirty.
         */
        PalEngineEventState_Flush(PAL_EVENT_WRITE_SCENE) &&
        PalEventPager_PinScene(
            event_pager(), event_start, event_count);
}

bool
PalEngineEventState_Flush(
    PalEventPagerWriteReason reason)
{
    PalEventPagerPageWrite scene;
    PalEventPagerIoResult result;

    if (!PalEngineEventState_IsReady() ||
        !PalEventPager_Flush(event_pager(), reason)) {
        return false;
    }
    if (scene_dirty_count() == 0u) {
        return true;
    }
    if (pal_event_scene_mirror == NULL) {
        return false;
    }
    memset(&scene, 0, sizeof(scene));
    scene.page = PAL_EVENT_JOURNAL_SCENE_PAGE;
    scene.data = pal_event_scene_mirror;
    scene.first_dirty_us = pal_event_scene_dirty_since_us;
    return event_journal_write(
        event_journal(), &scene, 1u, reason, &result);
}

bool
PalEngineEventState_Checkpoint(void)
{
    PalEventPager *pager;
    uint64_t oldest_dirty_us = 0u;
    uint64_t timestamp;
    uint8_t slot_index;

    if (!pal_event_state_initialized) {
        return true;
    }
    if (!PalEngineEventState_IsReady()) {
        return false;
    }
    pager = event_pager();
    for (slot_index = 0u;
         slot_index < PAL_EVENT_PAGER_SLOT_COUNT;
         slot_index++) {
        PalEventPagerSlot *slot = &pager->slots[slot_index];
        uint8_t word;
        bool dirty = false;

        for (word = 0u; word < PAL_EVENT_PAGER_DIRTY_WORDS; word++) {
            if (slot->dirty_records[word] != 0u) {
                dirty = true;
                break;
            }
        }
        if (dirty &&
            (oldest_dirty_us == 0u ||
             (slot->first_dirty_us != 0u
                  ? slot->first_dirty_us : 1u) < oldest_dirty_us)) {
            oldest_dirty_us = slot->first_dirty_us != 0u
                ? slot->first_dirty_us : 1u;
        }
    }
    if (scene_dirty_count() != 0u &&
        (oldest_dirty_us == 0u ||
         (pal_event_scene_dirty_since_us != 0u
              ? pal_event_scene_dirty_since_us : 1u) <
            oldest_dirty_us)) {
        oldest_dirty_us = pal_event_scene_dirty_since_us != 0u
            ? pal_event_scene_dirty_since_us : 1u;
    }
    if (oldest_dirty_us == 0u) {
        return true;
    }
    timestamp = event_now_us();
    if (timestamp < oldest_dirty_us ||
        timestamp - oldest_dirty_us < PAL_EVENT_STATE_CHECKPOINT_US) {
        return true;
    }
    return PalEngineEventState_Flush(PAL_EVENT_WRITE_SYNC);
}

bool
PalEngineEventState_MarkSceneDirty(
    uint16_t scene_index)
{
    uint8_t word;
    uint8_t bit;

    if (!PalEngineEventState_IsReady() ||
        pal_event_scene_mirror == NULL ||
        scene_index >= PAL_ENGINE_EVENT_STATE_SCENE_COUNT) {
        return false;
    }
    word = (uint8_t)(scene_index / 32u);
    bit = (uint8_t)(scene_index % 32u);
    if (scene_dirty_count() == 0u) {
        pal_event_scene_dirty_since_us = event_now_us();
    }
    pal_event_scene_dirty[word] |= (uint32_t)1u << bit;
    return true;
}

bool
PalEngineEventState_HasOutstandingHandles(void)
{
    return pal_event_state_initialized &&
        PalEventPager_HasOutstandingHandles(event_pager());
}

const PalEventPagerMetrics *
PalEngineEventState_GetPagerMetrics(void)
{
    return pal_event_state_initialized
        ? PalEventPager_GetMetrics(event_pager()) : NULL;
}

const PalEventJournalMetrics *
PalEngineEventState_GetJournalMetrics(void)
{
    return pal_event_state_initialized
        ? PalEventJournal_GetMetrics(event_journal()) : NULL;
}

const PalEngineEventStateIoMetrics *
PalEngineEventState_GetIoMetrics(void)
{
    return &pal_event_io_metrics;
}
