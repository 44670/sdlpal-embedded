#include "pal_event_pager.h"

#include <limits.h>
#include <string.h>

static bool
slot_dirty(
    const PalEventPagerSlot *slot)
{
    uint8_t i;

    for (i = 0; i < PAL_EVENT_PAGER_DIRTY_WORDS; i++) {
        if (slot->dirty_records[i] != 0u) {
            return true;
        }
    }
    return false;
}

static uint32_t
popcount32(
    uint32_t value)
{
    uint32_t count = 0;

    while (value != 0u) {
        value &= value - 1u;
        count++;
    }
    return count;
}

static int
find_page(
    const PalEventPager *pager,
    uint16_t page)
{
    uint8_t i;

    for (i = 0; i < PAL_EVENT_PAGER_SLOT_COUNT; i++) {
        if (pager->slots[i].valid && pager->slots[i].page == page) {
            return (int)i;
        }
    }
    return -1;
}

static int
choose_victim(
    PalEventPager *pager)
{
    int victim = -1;
    uint64_t oldest = UINT64_MAX;
    uint8_t i;

    for (i = 0; i < PAL_EVENT_PAGER_SLOT_COUNT; i++) {
        PalEventPagerSlot *slot = &pager->slots[i];

        if (!slot->valid) {
            return (int)i;
        }
        if (slot->scene_pinned || slot->transient_pins != 0u) {
            continue;
        }
        if (slot->last_use < oldest) {
            oldest = slot->last_use;
            victim = (int)i;
        }
    }
    if (victim < 0) {
        pager->metrics.no_victim_count++;
    }
    return victim;
}

static bool
write_slot(
    PalEventPager *pager,
    uint8_t slot_index)
{
    PalEventPagerSlot *slot = &pager->slots[slot_index];
    PalEventPagerIoResult result;
    uint64_t logical_bytes = 0u;
    uint8_t word;

    if (pager->io.write_page == NULL) {
        pager->metrics.io_failure_count++;
        return false;
    }
    for (word = 0u; word < PAL_EVENT_PAGER_DIRTY_WORDS; word++) {
        logical_bytes +=
            (uint64_t)popcount32(slot->dirty_records[word]) *
            PAL_EVENT_PAGER_RECORD_BYTES;
    }
    memset(&result, 0, sizeof(result));
    if (!pager->io.write_page(
            pager->io.user, slot->page, pager->storage[slot_index],
            &result)) {
        pager->metrics.io_failure_count++;
        return false;
    }
    pager->metrics.page_write_count++;
    pager->metrics.logical_dirty_bytes += logical_bytes;
    pager->metrics.storage_write_bytes += result.storage_bytes;
    pager->metrics.write_elapsed_us += result.elapsed_us;
    memset(slot->dirty_records, 0, sizeof(slot->dirty_records));
    return true;
}

static int
ensure_page(
    PalEventPager *pager,
    uint16_t page,
    bool count_access)
{
    PalEventPagerIoResult result;
    PalEventPagerSlot *slot;
    uint32_t next_epoch;
    int slot_index;

    slot_index = find_page(pager, page);
    if (slot_index >= 0) {
        if (count_access) {
            pager->metrics.hit_count++;
        }
        pager->slots[slot_index].last_use = ++pager->use_sequence;
        return slot_index;
    }

    if (count_access) {
        pager->metrics.miss_count++;
    }
    slot_index = choose_victim(pager);
    if (slot_index < 0) {
        return -1;
    }
    slot = &pager->slots[slot_index];
    if (slot->valid) {
        pager->metrics.eviction_count++;
        if (slot_dirty(slot)) {
            pager->metrics.dirty_eviction_count++;
            if (!write_slot(pager, (uint8_t)slot_index)) {
                return -1;
            }
        }
    }

    /*
     * A read callback is allowed to report failure after a short/partial
     * transfer.  Stop publishing the victim mapping before its bytes can be
     * overwritten so a failed read can never expose a half-old, half-new
     * page under the old page number.
     */
    next_epoch = slot->epoch + 1u;
    memset(slot, 0, sizeof(*slot));
    slot->page = PAL_EVENT_PAGER_INVALID_PAGE;
    slot->epoch = next_epoch == 0u ? 1u : next_epoch;

    if (pager->io.read_page == NULL) {
        pager->metrics.io_failure_count++;
        return -1;
    }
    memset(&result, 0, sizeof(result));
    if (!pager->io.read_page(
            pager->io.user,
            page,
            pager->storage[slot_index],
            &result)) {
        pager->metrics.io_failure_count++;
        return -1;
    }

    slot->page = page;
    slot->valid = 1u;
    slot->last_use = ++pager->use_sequence;
    pager->metrics.page_read_count++;
    pager->metrics.storage_read_bytes += result.storage_bytes;
    pager->metrics.read_elapsed_us += result.elapsed_us;
    return slot_index;
}

void
PalEventPager_Init(
    PalEventPager *pager,
    uint8_t storage[PAL_EVENT_PAGER_SLOT_COUNT][PAL_EVENT_PAGER_PAGE_BYTES],
    const PalEventPagerIo *io,
    uint16_t record_count)
{
    uint8_t i;

    if (pager == NULL) {
        return;
    }
    memset(pager, 0, sizeof(*pager));
    pager->storage = storage;
    if (io != NULL) {
        pager->io = *io;
    }
    for (i = 0; i < PAL_EVENT_PAGER_SLOT_COUNT; i++) {
        pager->slots[i].page = PAL_EVENT_PAGER_INVALID_PAGE;
    }
    pager->scene_first_page = PAL_EVENT_PAGER_INVALID_PAGE;
    pager->scene_last_page = PAL_EVENT_PAGER_INVALID_PAGE;
    pager->record_count = record_count;
    pager->page_count = (uint16_t)(
        ((uint32_t)record_count + PAL_EVENT_PAGER_RECORDS_PER_PAGE - 1u) /
        PAL_EVENT_PAGER_RECORDS_PER_PAGE);
    pager->initialized = storage != NULL &&
        pager->io.read_page != NULL &&
        pager->io.write_page != NULL &&
        record_count != 0u &&
        record_count <= PAL_EVENT_PAGER_RECORD_CAPACITY &&
        pager->page_count <= PAL_EVENT_PAGER_PAGE_CAPACITY;
}

uint16_t
PalEventPager_GetRecordCount(
    const PalEventPager *pager)
{
    return pager != NULL && pager->initialized ? pager->record_count : 0u;
}

bool
PalEventPager_Acquire(
    PalEventPager *pager,
    uint16_t event_id,
    bool writable,
    PalEventPagerHandle *handle)
{
    uint32_t record_index;
    uint16_t page;
    uint16_t record_in_page;
    PalEventPagerSlot *slot;
    int slot_index;

    if (pager == NULL || !pager->initialized || handle == NULL ||
        event_id == 0u || event_id > pager->record_count) {
        return false;
    }
    memset(handle, 0, sizeof(*handle));
    handle->slot = UINT8_MAX;

    pager->metrics.acquire_count++;
    record_index = (uint32_t)event_id - 1u;
    page = (uint16_t)(record_index / PAL_EVENT_PAGER_RECORDS_PER_PAGE);
    record_in_page =
        (uint16_t)(record_index % PAL_EVENT_PAGER_RECORDS_PER_PAGE);
    slot_index = ensure_page(pager, page, true);
    if (slot_index < 0) {
        return false;
    }

    slot = &pager->slots[slot_index];
    if (slot->transient_pins == UINT8_MAX) {
        return false;
    }
    slot->transient_pins++;
    slot->last_use = ++pager->use_sequence;
    handle->record = pager->storage[slot_index] +
        (uint32_t)record_in_page * PAL_EVENT_PAGER_RECORD_BYTES;
    handle->epoch = slot->epoch;
    handle->event_id = event_id;
    handle->slot = (uint8_t)slot_index;
    handle->writable = writable ? 1u : 0u;
    return true;
}

bool
PalEventPager_Release(
    PalEventPager *pager,
    PalEventPagerHandle *handle,
    bool changed)
{
    PalEventPagerSlot *slot;
    uint32_t record_index;
    uint16_t record_in_page;
    bool valid_change;

    if (pager == NULL || handle == NULL ||
        handle->slot >= PAL_EVENT_PAGER_SLOT_COUNT) {
        return false;
    }
    slot = &pager->slots[handle->slot];
    if (!slot->valid || slot->epoch != handle->epoch ||
        slot->transient_pins == 0u) {
        return false;
    }
    valid_change = !changed || handle->writable;
    if (changed && handle->writable) {
        uint8_t word;
        uint8_t bit;

        record_index = (uint32_t)handle->event_id - 1u;
        record_in_page =
            (uint16_t)(record_index % PAL_EVENT_PAGER_RECORDS_PER_PAGE);
        word = (uint8_t)(record_in_page / 32u);
        bit = (uint8_t)(record_in_page % 32u);
        slot->dirty_records[word] |= (uint32_t)1u << bit;
    }
    slot->transient_pins--;
    slot->last_use = ++pager->use_sequence;
    memset(handle, 0, sizeof(*handle));
    handle->slot = UINT8_MAX;
    return valid_change;
}

bool
PalEventPager_HasOutstandingHandles(
    const PalEventPager *pager)
{
    uint8_t i;

    if (pager == NULL) {
        return false;
    }
    for (i = 0; i < PAL_EVENT_PAGER_SLOT_COUNT; i++) {
        if (pager->slots[i].transient_pins != 0u) {
            return true;
        }
    }
    return false;
}

bool
PalEventPager_PinScene(
    PalEventPager *pager,
    uint16_t event_start,
    uint16_t event_count)
{
    uint16_t first_page = PAL_EVENT_PAGER_INVALID_PAGE;
    uint16_t last_page = PAL_EVENT_PAGER_INVALID_PAGE;
    uint8_t page_count = 0;
    uint8_t i;

    if (pager == NULL || !pager->initialized ||
        (uint32_t)event_start + event_count > pager->record_count ||
        PalEventPager_HasOutstandingHandles(pager)) {
        return false;
    }
    if (event_count != 0u) {
        first_page =
            (uint16_t)(event_start / PAL_EVENT_PAGER_RECORDS_PER_PAGE);
        last_page = (uint16_t)(
            ((uint32_t)event_start + event_count - 1u) /
            PAL_EVENT_PAGER_RECORDS_PER_PAGE);
        page_count = (uint8_t)(last_page - first_page + 1u);
        if (page_count > 2u) {
            return false;
        }
    }

    if (pager->scene_page_count == page_count &&
        pager->scene_first_page == first_page &&
        pager->scene_last_page == last_page) {
        return true;
    }
    pager->scene_first_page = PAL_EVENT_PAGER_INVALID_PAGE;
    pager->scene_last_page = PAL_EVENT_PAGER_INVALID_PAGE;
    pager->scene_page_count = 0u;
    for (i = 0; i < PAL_EVENT_PAGER_SLOT_COUNT; i++) {
        pager->slots[i].scene_pinned = 0u;
    }
    for (i = 0; i < page_count; i++) {
        int slot_index = ensure_page(
            pager, (uint16_t)(first_page + i), false);
        if (slot_index < 0) {
            uint8_t clear;

            for (clear = 0; clear < PAL_EVENT_PAGER_SLOT_COUNT; clear++) {
                pager->slots[clear].scene_pinned = 0u;
            }
            return false;
        }
        pager->slots[slot_index].scene_pinned = 1u;
    }
    pager->scene_first_page = first_page;
    pager->scene_last_page = last_page;
    pager->scene_page_count = page_count;
    return true;
}

bool
PalEventPager_Invalidate(
    PalEventPager *pager)
{
    uint8_t i;

    if (pager == NULL || !pager->initialized ||
        PalEventPager_HasOutstandingHandles(pager)) {
        return false;
    }
    for (i = 0; i < PAL_EVENT_PAGER_SLOT_COUNT; i++) {
        uint32_t epoch = pager->slots[i].epoch + 1u;

        memset(&pager->slots[i], 0, sizeof(pager->slots[i]));
        pager->slots[i].page = PAL_EVENT_PAGER_INVALID_PAGE;
        pager->slots[i].epoch = epoch == 0u ? 1u : epoch;
    }
    pager->scene_first_page = PAL_EVENT_PAGER_INVALID_PAGE;
    pager->scene_last_page = PAL_EVENT_PAGER_INVALID_PAGE;
    pager->scene_page_count = 0u;
    return true;
}

const PalEventPagerMetrics *
PalEventPager_GetMetrics(
    const PalEventPager *pager)
{
    return pager != NULL ? &pager->metrics : NULL;
}
