#include "pal_engine_event_state.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PAL_EVENT_STATE_WORK_PATH "0:/EVENT.WRK"
#define PAL_EVENT_STATE_OLD_SRAM_BYTES (424u * PAL_EVENT_PAGER_RECORD_BYTES)

#if defined(__GNUC__)
#define PAL_EVENT_STATE_SRAM \
    __attribute__((section(".bss.pal_sram"), aligned(8)))
#else
#define PAL_EVENT_STATE_SRAM
#endif

typedef struct PalEventStateBookkeeping {
    FILE *work;
    PalEngineEventStateIoMetrics io_metrics;
    uint16_t record_count;
    uint16_t page_count;
    uint8_t initialized;
    uint8_t ready;
} PalEventStateBookkeeping;

#define PAL_EVENT_STATE_OWNED_SRAM_BYTES \
    (PAL_EVENT_PAGER_SLOT_COUNT * PAL_EVENT_PAGER_PAGE_BYTES + \
        sizeof(PalEventPager) + sizeof(PalEventStateBookkeeping))

#if !defined(PAL_CORES3SE_NATIVE_ENGINE_HOST)
typedef char PalEventStateAssertReplacementBudget[
    PAL_EVENT_STATE_OWNED_SRAM_BYTES <= PAL_EVENT_STATE_OLD_SRAM_BYTES
        ? 1 : -1];
#endif

uint8_t pal_sram_extreme_event_pages[
    PAL_EVENT_PAGER_SLOT_COUNT * PAL_EVENT_PAGER_PAGE_BYTES]
    PAL_EVENT_STATE_SRAM;
uint8_t pal_sram_extreme_event_pager[
    sizeof(PalEventPager)] PAL_EVENT_STATE_SRAM;
uint8_t pal_sram_extreme_event_bookkeeping[
    sizeof(PalEventStateBookkeeping)] PAL_EVENT_STATE_SRAM;

static PalEventPager *
event_pager(void)
{
    return (PalEventPager *)(void *)pal_sram_extreme_event_pager;
}

static uint8_t (*
event_pages(void))[PAL_EVENT_PAGER_PAGE_BYTES]
{
    return (uint8_t (*)[PAL_EVENT_PAGER_PAGE_BYTES])
        (void *)pal_sram_extreme_event_pages;
}

static PalEventStateBookkeeping *
event_state(void)
{
    return (PalEventStateBookkeeping *)
        (void *)pal_sram_extreme_event_bookkeeping;
}

static bool
work_seek(FILE *work, uint16_t page)
{
    uint32_t offset = (uint32_t)page * PAL_EVENT_PAGER_PAGE_BYTES;

    return work != NULL && offset <= (uint32_t)LONG_MAX &&
        fseek(work, (long)offset, SEEK_SET) == 0;
}

static bool
work_read_page(
    void *user,
    uint16_t page,
    uint8_t *dst,
    PalEventPagerIoResult *result)
{
    PalEventStateBookkeeping *state = (PalEventStateBookkeeping *)user;
    size_t done;

    if (state == NULL || dst == NULL || result == NULL ||
        page >= state->page_count || !state->ready ||
        !work_seek(state->work, page)) {
        if (state != NULL) {
            state->io_metrics.read_failures++;
        }
        return false;
    }
    done = fread(dst, 1, PAL_EVENT_PAGER_PAGE_BYTES, state->work);
    state->io_metrics.read_calls++;
    state->io_metrics.read_bytes += done;
    result->storage_bytes = done;
    if (done != PAL_EVENT_PAGER_PAGE_BYTES) {
        state->io_metrics.read_failures++;
        return false;
    }
    return true;
}

static bool
work_write_page(
    void *user,
    uint16_t page,
    const uint8_t *data,
    PalEventPagerIoResult *result)
{
    PalEventStateBookkeeping *state = (PalEventStateBookkeeping *)user;
    size_t done;

    if (state == NULL || data == NULL || result == NULL ||
        page >= state->page_count || !state->ready ||
        !work_seek(state->work, page)) {
        if (state != NULL) {
            state->io_metrics.write_failures++;
        }
        return false;
    }
    done = fwrite(data, 1, PAL_EVENT_PAGER_PAGE_BYTES, state->work);
    state->io_metrics.write_calls++;
    state->io_metrics.write_bytes += done;
    result->storage_bytes = done;
    if (done != PAL_EVENT_PAGER_PAGE_BYTES) {
        state->io_metrics.write_failures++;
        return false;
    }
    return true;
}

static FILE *
open_work_file(void)
{
    FILE *work = fopen(PAL_EVENT_STATE_WORK_PATH, "r+b");

    return work != NULL ? work : fopen(PAL_EVENT_STATE_WORK_PATH, "w+b");
}

static bool
begin_replacement(void)
{
    PalEventStateBookkeeping *state = event_state();

    if (!state->initialized ||
        PalEventPager_HasOutstandingHandles(event_pager())) {
        return false;
    }
    state->ready = 0u;
    if (!PalEventPager_Invalidate(event_pager())) {
        return false;
    }
    if (state->work != NULL) {
        (void)fclose(state->work);
        state->work = NULL;
    }
    state->work = open_work_file();
    return state->work != NULL && fseek(state->work, 0L, SEEK_SET) == 0;
}

static bool
finish_replacement(uint32_t event_bytes)
{
    PalEventStateBookkeeping *state = event_state();
    uint32_t work_bytes =
        (uint32_t)state->page_count * PAL_EVENT_PAGER_PAGE_BYTES;
    uint32_t padding;
    size_t done;

    if (event_bytes !=
            (uint32_t)state->record_count * PAL_EVENT_PAGER_RECORD_BYTES ||
        event_bytes > work_bytes) {
        return false;
    }
    padding = work_bytes - event_bytes;
    memset(event_pages()[0], 0, PAL_EVENT_PAGER_PAGE_BYTES);
    if (padding != 0u) {
        done = fwrite(event_pages()[0], 1, padding, state->work);
        state->io_metrics.write_calls++;
        state->io_metrics.write_bytes += done;
        if (done != padding) {
            state->io_metrics.write_failures++;
            return false;
        }
    }
    if (fseek(state->work, 0L, SEEK_SET) != 0) {
        state->io_metrics.write_failures++;
        return false;
    }
    state->ready = 1u;
    return true;
}

static void
replacement_failed(void)
{
    PalEventStateBookkeeping *state = event_state();

    state->ready = 0u;
    if (state->work != NULL) {
        (void)fclose(state->work);
        state->work = NULL;
    }
}

bool
PalEngineEventState_Init(uint16_t record_count)
{
    PalEventStateBookkeeping *state = event_state();
    PalEventPagerIo io;

    memset(pal_sram_extreme_event_pages, 0,
        sizeof(pal_sram_extreme_event_pages));
    memset(pal_sram_extreme_event_pager, 0,
        sizeof(pal_sram_extreme_event_pager));
    memset(state, 0, sizeof(*state));
    memset(&io, 0, sizeof(io));
    io.read_page = work_read_page;
    io.write_page = work_write_page;
    io.user = state;
    PalEventPager_Init(event_pager(), event_pages(), &io, record_count);
    state->record_count = PalEventPager_GetRecordCount(event_pager());
    state->page_count = event_pager()->page_count;
    state->initialized = event_pager()->initialized;
    return state->initialized != 0u;
}

void
PalEngineEventState_Shutdown(void)
{
    PalEventStateBookkeeping *state = event_state();

    if (!state->initialized) {
        return;
    }
    /* Dirty resident pages are intentionally not written at shutdown. */
    state->ready = 0u;
    if (state->work != NULL) {
        (void)fclose(state->work);
    }
    memset(state, 0, sizeof(*state));
}

bool
PalEngineEventState_IsReady(void)
{
    PalEventStateBookkeeping *state = event_state();

    return state->initialized != 0u && state->ready != 0u &&
        state->work != NULL;
}

uint16_t
PalEngineEventState_GetRecordCount(void)
{
    PalEventStateBookkeeping *state = event_state();

    return state->initialized != 0u ? state->record_count : 0u;
}

uint32_t
PalEngineEventState_GetEventBytes(void)
{
    return (uint32_t)PalEngineEventState_GetRecordCount() *
        PAL_EVENT_PAGER_RECORD_BYTES;
}

bool
PalEngineEventState_ResetDefaults(
    const void *events,
    uint32_t event_bytes)
{
    PalEventStateBookkeeping *state = event_state();
    size_t done;

    if (events == NULL || event_bytes != PalEngineEventState_GetEventBytes() ||
        !begin_replacement()) {
        return false;
    }
    done = fwrite(events, 1, event_bytes, state->work);
    state->io_metrics.write_calls++;
    state->io_metrics.write_bytes += done;
    if (done != event_bytes || !finish_replacement(event_bytes)) {
        state->io_metrics.write_failures++;
        replacement_failed();
        return false;
    }
    return true;
}

bool
PalEngineEventState_ReplaceFromFile(FILE *source)
{
    PalEventStateBookkeeping *state = event_state();
    uint8_t *scratch = event_pages()[0];
    long event_offset;
    long file_end;
    uint32_t remaining = PalEngineEventState_GetEventBytes();

    if (source == NULL ||
        (event_offset = ftell(source)) < 0 ||
        fseek(source, 0L, SEEK_END) != 0 ||
        (file_end = ftell(source)) < 0 ||
        remaining == 0u ||
        file_end - event_offset != (long)remaining ||
        fseek(source, event_offset, SEEK_SET) != 0 ||
        !begin_replacement()) {
        return false;
    }
    while (remaining != 0u) {
        uint32_t amount = remaining < PAL_EVENT_PAGER_PAGE_BYTES
            ? remaining : PAL_EVENT_PAGER_PAGE_BYTES;
        size_t read_done = fread(scratch, 1, amount, source);
        size_t write_done;

        state->io_metrics.read_calls++;
        state->io_metrics.read_bytes += read_done;
        if (read_done != amount) {
            state->io_metrics.read_failures++;
            replacement_failed();
            return false;
        }
        write_done = fwrite(scratch, 1, amount, state->work);
        state->io_metrics.write_calls++;
        state->io_metrics.write_bytes += write_done;
        if (write_done != amount) {
            state->io_metrics.write_failures++;
            replacement_failed();
            return false;
        }
        remaining -= amount;
    }
    if (!finish_replacement(PalEngineEventState_GetEventBytes())) {
        replacement_failed();
        return false;
    }
    return true;
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
        memcpy(handle.record, record, PAL_EVENT_PAGER_RECORD_BYTES);
    }
    return PalEventPager_Release(event_pager(), &handle, changed);
}

bool
PalEngineEventState_PinScene(
    uint16_t event_start,
    uint16_t event_count)
{
    return PalEngineEventState_IsReady() &&
        PalEventPager_PinScene(event_pager(), event_start, event_count);
}

bool
PalEngineEventState_HasOutstandingHandles(void)
{
    return event_state()->initialized != 0u &&
        PalEventPager_HasOutstandingHandles(event_pager());
}

const PalEventPagerMetrics *
PalEngineEventState_GetPagerMetrics(void)
{
    return event_state()->initialized != 0u
        ? PalEventPager_GetMetrics(event_pager()) : NULL;
}

const PalEngineEventStateIoMetrics *
PalEngineEventState_GetIoMetrics(void)
{
    return &event_state()->io_metrics;
}
