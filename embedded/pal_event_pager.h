#ifndef PAL_EVENT_PAGER_H
#define PAL_EVENT_PAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The DOS data set used by the extreme profile contains 5,369 packed
 * EVENTOBJECT records.  A record is exactly 32 bytes, so one 4 KiB page
 * contains 128 records and the complete table occupies 42 logical pages.
 */
#define PAL_EVENT_PAGER_RECORD_BYTES       32u
#define PAL_EVENT_PAGER_RECORD_COUNT       5369u
#define PAL_EVENT_PAGER_PAGE_BYTES         4096u
#define PAL_EVENT_PAGER_RECORDS_PER_PAGE   128u
#define PAL_EVENT_PAGER_PAGE_COUNT         42u
#define PAL_EVENT_PAGER_SLOT_COUNT         3u
#define PAL_EVENT_PAGER_DIRTY_WORDS        4u
#define PAL_EVENT_PAGER_INVALID_PAGE       0xffffu

typedef enum PalEventPagerWriteReason {
    PAL_EVENT_WRITE_EVICT = 1,
    PAL_EVENT_WRITE_SCENE = 2,
    PAL_EVENT_WRITE_SAVE = 3,
    PAL_EVENT_WRITE_SYNC = 4,
    PAL_EVENT_WRITE_SHUTDOWN = 5,
} PalEventPagerWriteReason;

typedef struct PalEventPagerIoResult {
    uint64_t elapsed_us;
    uint64_t storage_bytes;
    uint32_t sync_count;
} PalEventPagerIoResult;

typedef struct PalEventPagerPageWrite {
    uint16_t page;
    const uint8_t *data;
    uint32_t dirty_records[PAL_EVENT_PAGER_DIRTY_WORDS];
    uint64_t first_dirty_us;
} PalEventPagerPageWrite;

typedef bool (*PalEventPagerReadPage)(
    void *user,
    uint16_t page,
    uint8_t *dst,
    PalEventPagerIoResult *result);

/*
 * write_pages is one durability transaction.  It must either make every
 * supplied page recoverable as a group or leave the previously committed
 * generation recoverable.  The pager clears dirty bits only after success.
 */
typedef bool (*PalEventPagerWritePages)(
    void *user,
    const PalEventPagerPageWrite *pages,
    uint8_t page_count,
    PalEventPagerWriteReason reason,
    PalEventPagerIoResult *result);

typedef uint64_t (*PalEventPagerNowUs)(void *user);

typedef struct PalEventPagerIo {
    PalEventPagerReadPage read_page;
    PalEventPagerWritePages write_pages;
    PalEventPagerNowUs now_us;
    void *user;
} PalEventPagerIo;

typedef struct PalEventPagerMetrics {
    uint64_t acquire_count;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t page_read_count;
    uint64_t page_write_count;
    uint64_t write_transaction_count;
    uint64_t eviction_count;
    uint64_t dirty_eviction_count;
    uint64_t no_victim_count;
    uint64_t io_failure_count;
    uint64_t logical_dirty_bytes;
    uint64_t storage_read_bytes;
    uint64_t storage_write_bytes;
    uint64_t storage_sync_count;
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
    uint64_t first_dirty_us;
    uint32_t dirty_records[PAL_EVENT_PAGER_DIRTY_WORDS];
} PalEventPagerSlot;

typedef struct PalEventPager {
    uint8_t (*storage)[PAL_EVENT_PAGER_PAGE_BYTES];
    PalEventPagerIo io;
    PalEventPagerSlot slots[PAL_EVENT_PAGER_SLOT_COUNT];
    PalEventPagerMetrics metrics;
    uint64_t use_sequence;
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
    const PalEventPagerIo *io);

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
 * event_count is the scene's contiguous record count.  The real data set
 * never requires more than two pages for one scene.
 */
bool PalEventPager_PinScene(
    PalEventPager *pager,
    uint16_t event_start,
    uint16_t event_count);

bool PalEventPager_Flush(
    PalEventPager *pager,
    PalEventPagerWriteReason reason);

/*
 * Drop all cached pages without writing them.  This is used only after the
 * authoritative backing file has been atomically replaced during new-game
 * or load-game recovery.  It fails while a transient handle is outstanding.
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
