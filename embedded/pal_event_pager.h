#ifndef PAL_EVENT_PAGER_H
#define PAL_EVENT_PAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The record count is supplied from SSS#0 at runtime.  These are storage
 * capacities, not assertions about a particular PAL data release.  The final
 * page is zero-padded only in the session work file; padding is not part of
 * the game state or save format.
 */
#define PAL_EVENT_PAGER_RECORD_BYTES       32u
#define PAL_EVENT_PAGER_RECORD_CAPACITY    5500u
#define PAL_EVENT_PAGER_PAGE_BYTES         4096u
#define PAL_EVENT_PAGER_RECORDS_PER_PAGE   128u
#define PAL_EVENT_PAGER_PAGE_CAPACITY      \
    ((PAL_EVENT_PAGER_RECORD_CAPACITY + \
        PAL_EVENT_PAGER_RECORDS_PER_PAGE - 1u) / \
        PAL_EVENT_PAGER_RECORDS_PER_PAGE)
#define PAL_EVENT_PAGER_SLOT_COUNT         3u
#define PAL_EVENT_PAGER_DIRTY_WORDS        4u
#define PAL_EVENT_PAGER_INVALID_PAGE       0xffffu

typedef struct PalEventPagerIoResult {
    uint64_t elapsed_us;
    uint64_t storage_bytes;
} PalEventPagerIoResult;

typedef bool (*PalEventPagerReadPage)(
    void *user,
    uint16_t page,
    uint8_t *dst,
    PalEventPagerIoResult *result);

/* A dirty page is written only when normal LRU replacement evicts it. */
typedef bool (*PalEventPagerWritePage)(
    void *user,
    uint16_t page,
    const uint8_t *data,
    PalEventPagerIoResult *result);

typedef struct PalEventPagerIo {
    PalEventPagerReadPage read_page;
    PalEventPagerWritePage write_page;
    void *user;
} PalEventPagerIo;

typedef struct PalEventPagerMetrics {
    uint64_t acquire_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t page_read_count;
    uint64_t page_write_count;
    uint64_t eviction_count;
    uint64_t dirty_eviction_count;
    uint64_t no_victim_count;
    uint64_t io_failure_count;
    uint64_t logical_dirty_bytes;
    uint64_t storage_read_bytes;
    uint64_t storage_write_bytes;
    uint64_t read_elapsed_us;
    uint64_t write_elapsed_us;
} PalEventPagerMetrics;

typedef struct PalEventPagerSlot {
    uint16_t page;
    uint8_t valid;
    uint8_t scene_pinned;
    uint8_t transient_pins;
    uint8_t reserved;
    uint32_t epoch;
    uint64_t last_use;
    uint32_t dirty_records[PAL_EVENT_PAGER_DIRTY_WORDS];
} PalEventPagerSlot;

typedef struct PalEventPager {
    uint8_t (*storage)[PAL_EVENT_PAGER_PAGE_BYTES];
    PalEventPagerIo io;
    PalEventPagerSlot slots[PAL_EVENT_PAGER_SLOT_COUNT];
    PalEventPagerMetrics metrics;
    uint64_t use_sequence;
    uint16_t record_count;
    uint16_t page_count;
    uint16_t scene_first_page;
    uint16_t scene_last_page;
    uint8_t scene_page_count;
    uint8_t initialized;
} PalEventPager;

typedef struct PalEventPagerHandle {
    uint8_t *record;
    uint32_t epoch;
    uint16_t event_id;
    uint8_t slot;
    uint8_t writable;
} PalEventPagerHandle;

void PalEventPager_Init(
    PalEventPager *pager,
    uint8_t storage[PAL_EVENT_PAGER_SLOT_COUNT][PAL_EVENT_PAGER_PAGE_BYTES],
    const PalEventPagerIo *io,
    uint16_t record_count);

uint16_t PalEventPager_GetRecordCount(const PalEventPager *pager);

bool PalEventPager_Acquire(
    PalEventPager *pager,
    uint16_t event_id,
    bool writable,
    PalEventPagerHandle *handle);

bool PalEventPager_Release(
    PalEventPager *pager,
    PalEventPagerHandle *handle,
    bool changed);

/*
 * event_start is a zero-based index into the global EVENTOBJECT table and
 * event_count is the scene's contiguous record count.  A scene may occupy at
 * most two pages so the third slot remains available for script references;
 * data that exceeds this profile capacity is rejected rather than truncated.
 */
bool PalEventPager_PinScene(
    PalEventPager *pager,
    uint16_t event_start,
    uint16_t event_count);

/*
 * Drop all cached pages without writing them.  This is used only after the
 * session work file has been completely overwritten for New Game or Load
 * Game.  It fails while a transient handle is outstanding.
 */
bool PalEventPager_Invalidate(PalEventPager *pager);

const PalEventPagerMetrics *PalEventPager_GetMetrics(
    const PalEventPager *pager);

bool PalEventPager_HasOutstandingHandles(
    const PalEventPager *pager);

#ifdef __cplusplus
}
#endif

#endif
