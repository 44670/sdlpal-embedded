#include "../../embedded/pal_event_pager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PINNED_FIXTURE_RECORD_COUNT 5332u
#define TEST_SMALL_RECORD_COUNT 423u

typedef struct TestBackend {
    uint8_t pages[PAL_EVENT_PAGER_PAGE_CAPACITY][PAL_EVENT_PAGER_PAGE_BYTES];
    uint64_t now_us;
    uint32_t reads;
    uint32_t writes;
    uint16_t last_page;
    bool fail_read;
    bool partial_read;
    bool fail_write;
} TestBackend;

static uint8_t test_cache[
    PAL_EVENT_PAGER_SLOT_COUNT][PAL_EVENT_PAGER_PAGE_BYTES];
static PalEventPager test_pager;
static TestBackend test_backend;

static int
fail(
    const char *message)
{
    fprintf(stderr, "event pager test failed: %s\n", message);
    return 1;
}

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            return fail(message); \
        } \
    } while (0)

static bool
test_read_page(
    void *user,
    uint16_t page,
    uint8_t *dst,
    PalEventPagerIoResult *result)
{
    TestBackend *backend = (TestBackend *)user;

    if (backend == NULL || dst == NULL || result == NULL ||
        page >= PAL_EVENT_PAGER_PAGE_CAPACITY || backend->fail_read) {
        return false;
    }
    if (backend->partial_read) {
        memcpy(
            dst,
            backend->pages[page],
            PAL_EVENT_PAGER_PAGE_BYTES / 2u);
        return false;
    }
    memcpy(dst, backend->pages[page], PAL_EVENT_PAGER_PAGE_BYTES);
    backend->reads++;
    backend->now_us += 100u;
    result->elapsed_us = 100u;
    result->storage_bytes = PAL_EVENT_PAGER_PAGE_BYTES;
    return true;
}

static bool
test_write_page(
    void *user,
    uint16_t page,
    const uint8_t *data,
    PalEventPagerIoResult *result)
{
    TestBackend *backend = (TestBackend *)user;

    if (backend == NULL || data == NULL || result == NULL ||
        page >= PAL_EVENT_PAGER_PAGE_CAPACITY || backend->fail_write) {
        return false;
    }
    memcpy(backend->pages[page], data, PAL_EVENT_PAGER_PAGE_BYTES);
    backend->last_page = page;
    backend->writes++;
    backend->now_us += 250u;
    result->elapsed_us = 250u;
    result->storage_bytes = PAL_EVENT_PAGER_PAGE_BYTES;
    return true;
}

static void
seed_backend(
    TestBackend *backend)
{
    uint16_t page;

    memset(backend, 0, sizeof(*backend));
    for (page = 0; page < PAL_EVENT_PAGER_PAGE_CAPACITY; page++) {
        uint16_t record;

        for (record = 0; record < PAL_EVENT_PAGER_RECORDS_PER_PAGE; record++) {
            uint32_t event_index =
                (uint32_t)page * PAL_EVENT_PAGER_RECORDS_PER_PAGE + record;
            uint8_t *dst = backend->pages[page] +
                (uint32_t)record * PAL_EVENT_PAGER_RECORD_BYTES;

            if (event_index < PINNED_FIXTURE_RECORD_COUNT) {
                dst[0] = (uint8_t)(event_index + 1u);
                dst[1] = (uint8_t)((event_index + 1u) >> 8);
            } else {
                memset(dst, 0, PAL_EVENT_PAGER_RECORD_BYTES);
            }
        }
    }
}

static bool
touch_event(
    PalEventPager *pager,
    uint16_t event_id,
    uint8_t value)
{
    PalEventPagerHandle handle;

    if (!PalEventPager_Acquire(pager, event_id, true, &handle)) {
        return false;
    }
    handle.record[2] = value;
    return PalEventPager_Release(pager, &handle, true);
}

int
main(
    void)
{
    const PalEventPagerMetrics *metrics;
    PalEventPagerIo io;
    PalEventPagerHandle held;
    PalEventPagerHandle other;
    uint16_t event_id;
    uint32_t writes_before;

    seed_backend(&test_backend);
    memset(test_cache, 0xa5, sizeof(test_cache));
    memset(&io, 0, sizeof(io));
    io.read_page = test_read_page;
    io.write_page = test_write_page;
    io.user = &test_backend;
    PalEventPager_Init(
        &test_pager, test_cache, &io, TEST_SMALL_RECORD_COUNT);
    CHECK(PalEventPager_GetRecordCount(&test_pager) ==
          TEST_SMALL_RECORD_COUNT,
        "runtime record count was not retained");
    CHECK(PalEventPager_Acquire(
          &test_pager, TEST_SMALL_RECORD_COUNT, false, &held),
        "cannot acquire final record in smaller data set");
    CHECK(PalEventPager_Release(&test_pager, &held, false),
        "cannot release final record in smaller data set");
    CHECK(!PalEventPager_Acquire(
          &test_pager, TEST_SMALL_RECORD_COUNT + 1u, false, &held),
        "smaller data set accepted an out-of-range record");

    seed_backend(&test_backend);
    memset(test_cache, 0xa5, sizeof(test_cache));
    PalEventPager_Init(
        &test_pager, test_cache, &io, PINNED_FIXTURE_RECORD_COUNT);

    /* scene 59 owns event IDs 984..1125 and spans pages 7 and 8. */
    CHECK(PalEventPager_PinScene(&test_pager, 983u, 142u),
        "cannot pin two-page scene 59");
    CHECK(test_pager.scene_page_count == 2u &&
          test_pager.scene_first_page == 7u &&
          test_pager.scene_last_page == 8u,
        "scene 59 page range mismatch");
    CHECK(test_backend.reads == 2u, "scene pin did not read two pages");

    CHECK(PalEventPager_Acquire(&test_pager, 984u, false, &held),
        "cannot acquire current-scene event");
    CHECK(held.record[0] == (uint8_t)984u &&
          held.record[1] == (uint8_t)(984u >> 8),
        "current-scene event data mismatch");
    CHECK(PalEventPager_Release(&test_pager, &held, false),
        "cannot release current-scene event");

    /* The final stock event is page 41, record 83, outside this scene. */
    CHECK(touch_event(&test_pager, PINNED_FIXTURE_RECORD_COUNT, 0x5au),
        "cannot dirty final fixture event");
    CHECK(test_pager.slots[2].page == 41u ||
          test_pager.slots[1].page == 41u ||
          test_pager.slots[0].page == 41u,
        "final fixture event did not map to page 41");

    /*
     * Both scene pages are fixed and the foreign page is transient-pinned:
     * another miss must fail immediately instead of evicting a live pointer.
     */
    CHECK(PalEventPager_Acquire(
          &test_pager, PINNED_FIXTURE_RECORD_COUNT, false, &held),
        "cannot pin foreign event");
    CHECK(!PalEventPager_Acquire(&test_pager, 1u, false, &other),
        "all-pinned cache unexpectedly found a victim");
    CHECK(PalEventPager_Release(&test_pager, &held, false),
        "cannot release foreign pin");

    /*
     * Loading page 0 now evicts dirty page 41.  The eviction is its own
     * only permitted work-file write and must update the backing model.
     */
    CHECK(PalEventPager_Acquire(&test_pager, 1u, false, &held),
        "cannot acquire after releasing foreign pin");
    CHECK(PalEventPager_Release(&test_pager, &held, false),
        "cannot release page-zero event");
    CHECK(test_backend.writes == 1u && test_backend.last_page == 41u &&
          test_backend.pages[41][83u * PAL_EVENT_PAGER_RECORD_BYTES + 2u] ==
              0x5au,
        "dirty LRU eviction was not written");

    /*
     * scene 156 owns IDs 2616..2745 and spans pages 20 and 21.  Dirty one
     * record in each fixed page.  Changing the pinned scene must not flush
     * either page; only later eviction may write it.
     */
    CHECK(PalEventPager_PinScene(&test_pager, 2615u, 130u),
        "cannot switch to scene 156");
    CHECK(touch_event(&test_pager, 2616u, 0x61u),
        "cannot dirty scene 156 first page");
    CHECK(touch_event(&test_pager, 2745u, 0x62u),
        "cannot dirty scene 156 second page");
    writes_before = test_backend.writes;
    CHECK(PalEventPager_PinScene(&test_pager, 0u, 32u),
        "cannot switch to one-page scene");
    CHECK(test_backend.writes == writes_before,
        "scene pin change unexpectedly flushed dirty pages");

    /* A failed dirty eviction must retain the page for a later retry. */
    test_backend.fail_write = true;
    CHECK(!PalEventPager_Acquire(&test_pager, 385u, false, &held),
        "injected eviction write failure unexpectedly succeeded");
    test_backend.fail_write = false;
    CHECK(PalEventPager_Acquire(&test_pager, 385u, false, &held),
        "dirty eviction was not retryable after write failure");
    CHECK(PalEventPager_Release(&test_pager, &held, false),
        "cannot release event after eviction retry");
    CHECK(test_backend.writes == writes_before + 1u,
        "retry did not write exactly one evicted page");

    /*
     * Opcode 009A has a real cross-page range 889..898.  Sequential
     * acquire/release must work with a single non-scene LRU slot.
     */
    for (event_id = 889u; event_id <= 898u; event_id++) {
        CHECK(touch_event(&test_pager, event_id, (uint8_t)event_id),
            "cross-page 009A-style write failed");
    }
    /* The final record remains addressable and uses the stock count. */
    CHECK(PalEventPager_Acquire(
          &test_pager, PINNED_FIXTURE_RECORD_COUNT, false, &held),
        "cannot acquire final fixture event");
    CHECK(held.record[0] == (uint8_t)PINNED_FIXTURE_RECORD_COUNT &&
          held.record[1] ==
              (uint8_t)(PINNED_FIXTURE_RECORD_COUNT >> 8),
        "final fixture event page/record mapping mismatch");
    CHECK(!PalEventPager_Invalidate(&test_pager),
        "invalidate ignored outstanding handle");
    CHECK(PalEventPager_Release(&test_pager, &held, false),
        "cannot release orphan event");
    CHECK(PalEventPager_Invalidate(&test_pager),
        "cannot invalidate released pager");

    test_backend.partial_read = true;
    CHECK(!PalEventPager_Acquire(&test_pager, 1u, false, &held),
        "partial page read unexpectedly succeeded");
    CHECK(!test_pager.slots[0].valid &&
          !test_pager.slots[1].valid &&
          !test_pager.slots[2].valid,
        "partial read left a published cache slot");
    test_backend.partial_read = false;
    CHECK(PalEventPager_Acquire(&test_pager, 1u, false, &held),
        "cannot retry page after partial read");
    CHECK(!PalEventPager_Release(&test_pager, &held, true),
        "readonly changed release was not rejected");
    CHECK(!PalEventPager_HasOutstandingHandles(&test_pager),
        "readonly misuse leaked a transient pin");

    metrics = PalEventPager_GetMetrics(&test_pager);
    CHECK(metrics != NULL &&
          metrics->hit_count > 0u &&
          metrics->miss_count > 0u &&
          metrics->no_victim_count == 1u &&
          metrics->dirty_eviction_count > 0u &&
          metrics->logical_dirty_bytes > 0u &&
          metrics->storage_write_bytes > metrics->logical_dirty_bytes,
        "pager metrics did not capture cache/write pressure");

    printf(
        "event pager logic: ok sizeof=%zu cache=%u hits=%llu misses=%llu "
        "writes=%llu logical=%llu storage=%llu\n",
        sizeof(test_pager),
        (unsigned)sizeof(test_cache),
        (unsigned long long)metrics->hit_count,
        (unsigned long long)metrics->miss_count,
        (unsigned long long)metrics->page_write_count,
        (unsigned long long)metrics->logical_dirty_bytes,
        (unsigned long long)metrics->storage_write_bytes);
    return 0;
}
