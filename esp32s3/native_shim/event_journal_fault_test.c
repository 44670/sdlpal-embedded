#include "pal_event_journal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PROFILE_ID       0x31545645u
#define TEST_REBOUND_PROFILE_ID 0x32445645u
#define TEST_TEMPLATE_CRC32   0xa57ec13du
#define TEST_OLD_TAG          0x35u
#define TEST_NEW_TAG          0xc9u
#define TEST_NEWER_TAG        0x72u
#define TEST_MAX_OPERATIONS   192u
#define TEST_MAX_PENDING      192u
#define TEST_EVENT_TAIL_BYTES \
    ((PAL_EVENT_PAGER_RECORD_COUNT % PAL_EVENT_PAGER_RECORDS_PER_PAGE) * \
     PAL_EVENT_PAGER_RECORD_BYTES)

typedef enum TestOperationKind {
    TEST_OPERATION_WRITE = 1,
    TEST_OPERATION_SYNC = 2,
} TestOperationKind;

typedef enum TestFaultKind {
    TEST_FAULT_NONE = 0,
    TEST_FAULT_BEFORE,
    TEST_FAULT_AFTER_DURABLE,
    TEST_FAULT_PARTIAL_DURABLE,
    TEST_FAULT_PARTIAL_SYNC,
} TestFaultKind;

typedef struct TestOperation {
    TestOperationKind kind;
    uint32_t offset;
    uint32_t size;
} TestOperation;

typedef struct TestRange {
    uint32_t offset;
    uint32_t size;
} TestRange;

typedef struct TestDevice {
    uint64_t now_us;
    uint32_t operation_count;
    uint32_t fail_operation;
    uint32_t partial_divisor;
    uint16_t pending_count;
    uint16_t trace_count;
    TestFaultKind fault_kind;
    bool fault_tripped;
    bool trace_enabled;
    bool silent_drop_enabled;
    uint32_t silent_drop_offset;
    uint32_t silent_drop_size;
    TestRange pending[TEST_MAX_PENDING];
    TestOperation trace[TEST_MAX_OPERATIONS];
} TestDevice;

typedef struct TestSource {
    uint8_t tag;
} TestSource;

static uint8_t test_volatile[PAL_EVENT_JOURNAL_FILE_BYTES];
static uint8_t test_durable[PAL_EVENT_JOURNAL_FILE_BYTES];
static uint8_t test_baseline[PAL_EVENT_JOURNAL_FILE_BYTES];
static uint8_t test_sector[PAL_EVENT_JOURNAL_COMMIT_BYTES];
static uint8_t test_page[PAL_EVENT_PAGER_PAGE_BYTES];
static uint8_t test_expected[PAL_EVENT_PAGER_PAGE_BYTES];
static uint8_t test_update_pages[3][PAL_EVENT_PAGER_PAGE_BYTES];

static uint32_t test_case_line;
static uint32_t test_case_operation;
static TestFaultKind test_case_fault;

#define TEST_CHECK(expression)                                                \
    do {                                                                      \
        if (!(expression)) {                                                  \
            fprintf(stderr,                                                   \
                "event journal fault test failed: line=%u op=%u fault=%u\n", \
                (unsigned)test_case_line,                                     \
                (unsigned)test_case_operation,                                \
                (unsigned)test_case_fault);                                   \
            return false;                                                     \
        }                                                                     \
    } while (0)

#define TEST_AT(expression)                  \
    do {                                     \
        test_case_line = (uint32_t)__LINE__; \
        TEST_CHECK(expression);              \
    } while (0)

static bool
checked_range(
    uint32_t offset,
    uint32_t size)
{
    return offset <= PAL_EVENT_JOURNAL_FILE_BYTES &&
        size <= PAL_EVENT_JOURNAL_FILE_BYTES - offset;
}

static void
record_operation(
    TestDevice *device,
    TestOperationKind kind,
    uint32_t offset,
    uint32_t size)
{
    device->operation_count++;
    if (device->trace_enabled &&
        device->trace_count < TEST_MAX_OPERATIONS) {
        TestOperation *operation = &device->trace[device->trace_count++];

        operation->kind = kind;
        operation->offset = offset;
        operation->size = size;
    }
}

static void
persist_pending(
    TestDevice *device,
    bool partial)
{
    uint16_t i;

    for (i = 0; i < device->pending_count; i++) {
        uint32_t size = device->pending[i].size;

        if (partial && size != 0u) {
            uint32_t divisor = device->partial_divisor != 0u
                ? device->partial_divisor : 2u;

            size /= divisor;
            if (size == 0u) {
                size = 1u;
            }
            if (size >= device->pending[i].size) {
                size = device->pending[i].size - 1u;
            }
        }
        if (size != 0u) {
            memcpy(
                test_durable + device->pending[i].offset,
                test_volatile + device->pending[i].offset,
                size);
        }
    }
}

static bool
test_read_at(
    void *user,
    uint32_t offset,
    uint8_t *dst,
    uint32_t size)
{
    TestDevice *device = (TestDevice *)user;

    if (device == NULL || (dst == NULL && size != 0u) ||
        !checked_range(offset, size)) {
        return false;
    }
    if (size != 0u) {
        memcpy(dst, test_volatile + offset, size);
    }
    device->now_us += 7u;
    return true;
}

static bool
test_write_at(
    void *user,
    uint32_t offset,
    const uint8_t *src,
    uint32_t size)
{
    TestDevice *device = (TestDevice *)user;
    bool injected;

    if (device == NULL || (src == NULL && size != 0u) ||
        !checked_range(offset, size)) {
        return false;
    }
    record_operation(device, TEST_OPERATION_WRITE, offset, size);
    if (device->silent_drop_enabled &&
        offset >= device->silent_drop_offset &&
        offset <= device->silent_drop_offset +
            device->silent_drop_size &&
        size <= device->silent_drop_offset +
            device->silent_drop_size - offset) {
        device->now_us += 11u;
        return true;
    }
    injected = device->fail_operation != 0u &&
        device->operation_count == device->fail_operation;
    if (injected && device->fault_kind == TEST_FAULT_BEFORE) {
        device->fault_tripped = true;
        return false;
    }

    if (size != 0u) {
        uint32_t copied = size;

        if (injected &&
            device->fault_kind == TEST_FAULT_PARTIAL_DURABLE) {
            uint32_t divisor = device->partial_divisor != 0u
                ? device->partial_divisor : 2u;

            copied /= divisor;
            if (copied == 0u) {
                copied = 1u;
            }
            if (copied >= size) {
                copied = size - 1u;
            }
        }
        if (copied != 0u) {
            memcpy(test_volatile + offset, src, copied);
        }
        if (injected &&
            (device->fault_kind == TEST_FAULT_AFTER_DURABLE ||
             device->fault_kind == TEST_FAULT_PARTIAL_DURABLE) &&
            copied != 0u) {
            memcpy(test_durable + offset, src, copied);
        }
    }
    device->now_us += 11u;

    if (injected &&
        (device->fault_kind == TEST_FAULT_AFTER_DURABLE ||
         device->fault_kind == TEST_FAULT_PARTIAL_DURABLE)) {
        device->fault_tripped = true;
        return false;
    }
    if (device->pending_count >= TEST_MAX_PENDING) {
        return false;
    }
    device->pending[device->pending_count].offset = offset;
    device->pending[device->pending_count].size = size;
    device->pending_count++;
    return true;
}

static bool
test_sync(
    void *user)
{
    TestDevice *device = (TestDevice *)user;
    bool injected;

    if (device == NULL) {
        return false;
    }
    record_operation(device, TEST_OPERATION_SYNC, 0u, 0u);
    injected = device->fail_operation != 0u &&
        device->operation_count == device->fail_operation;
    if (injected && device->fault_kind == TEST_FAULT_BEFORE) {
        device->fault_tripped = true;
        return false;
    }
    if (injected && device->fault_kind == TEST_FAULT_PARTIAL_SYNC) {
        persist_pending(device, true);
        device->fault_tripped = true;
        return false;
    }

    persist_pending(device, false);
    device->pending_count = 0u;
    device->now_us += 17u;
    if (injected && device->fault_kind == TEST_FAULT_AFTER_DURABLE) {
        device->fault_tripped = true;
        return false;
    }
    return true;
}

static uint64_t
test_now_us(
    void *user)
{
    TestDevice *device = (TestDevice *)user;

    return device != NULL ? device->now_us : 0u;
}

static PalEventJournalIo
make_io(
    TestDevice *device)
{
    PalEventJournalIo io;

    memset(&io, 0, sizeof(io));
    io.read_at = test_read_at;
    io.write_at = test_write_at;
    io.sync = test_sync;
    io.now_us = test_now_us;
    io.user = device;
    return io;
}

static void
device_reset_empty(
    TestDevice *device)
{
    memset(test_volatile, 0xff, sizeof(test_volatile));
    memset(test_durable, 0xff, sizeof(test_durable));
    memset(device, 0, sizeof(*device));
}

static void
device_restore_baseline(
    TestDevice *device)
{
    memcpy(test_volatile, test_baseline, sizeof(test_volatile));
    memcpy(test_durable, test_baseline, sizeof(test_durable));
    memset(device, 0, sizeof(*device));
}

static void
device_crash(
    TestDevice *device)
{
    memcpy(test_volatile, test_durable, sizeof(test_volatile));
    device->pending_count = 0u;
    device->fail_operation = 0u;
    device->fault_kind = TEST_FAULT_NONE;
}

static uint8_t
page_byte(
    uint8_t tag,
    uint16_t page,
    uint32_t offset)
{
    if (page == PAL_EVENT_PAGER_PAGE_COUNT - 1u &&
        offset >= TEST_EVENT_TAIL_BYTES) {
        return 0u;
    }
    if (page == PAL_EVENT_JOURNAL_SCENE_PAGE &&
        offset >= PAL_EVENT_JOURNAL_SCENE_BYTES) {
        return 0u;
    }
    return (uint8_t)(
        tag ^ (uint8_t)(page * 37u) ^
        (uint8_t)offset ^ (uint8_t)(offset >> 8));
}

static void
fill_page(
    uint8_t tag,
    uint16_t page,
    uint8_t dst[PAL_EVENT_PAGER_PAGE_BYTES])
{
    uint32_t i;

    for (i = 0; i < PAL_EVENT_PAGER_PAGE_BYTES; i++) {
        dst[i] = page_byte(tag, page, i);
    }
}

static bool
source_page(
    void *user,
    uint16_t logical_page,
    uint8_t page[PAL_EVENT_PAGER_PAGE_BYTES])
{
    const TestSource *source = (const TestSource *)user;

    if (source == NULL ||
        logical_page >= PAL_EVENT_JOURNAL_PAGE_COUNT) {
        return false;
    }
    fill_page(source->tag, logical_page, page);
    return true;
}

static void
init_journal(
    PalEventJournal *journal,
    TestDevice *device)
{
    PalEventJournalIo io = make_io(device);

    memset(test_sector, 0, sizeof(test_sector));
    PalEventJournal_Init(journal, &io, test_sector);
}

static bool
open_journal(
    PalEventJournal *journal,
    TestDevice *device,
    uint32_t profile_id,
    uint32_t template_crc32)
{
    init_journal(journal, device);
    return PalEventJournal_Open(
        journal,
        profile_id,
        template_crc32,
        test_page);
}

static bool
read_matches(
    PalEventJournal *journal,
    uint16_t page,
    uint8_t tag)
{
    PalEventPagerIoResult result;

    fill_page(tag, page, test_expected);
    return PalEventJournal_ReadPage(
            journal, page, test_page, &result) &&
        memcmp(test_page, test_expected, sizeof(test_page)) == 0;
}

static bool
verify_all_pages(
    PalEventJournal *journal,
    uint8_t tag)
{
    uint16_t page;

    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        if (!read_matches(journal, page, tag)) {
            return false;
        }
    }
    return true;
}

static void
prepare_updates(
    PalEventPagerPageWrite writes[3])
{
    static const uint16_t page_numbers[3] = {
        0u,
        PAL_EVENT_PAGER_PAGE_COUNT - 1u,
        PAL_EVENT_JOURNAL_SCENE_PAGE,
    };
    uint8_t i;

    memset(writes, 0, sizeof(*writes) * 3u);
    for (i = 0; i < 3u; i++) {
        fill_page(TEST_NEW_TAG, page_numbers[i], test_update_pages[i]);
        writes[i].page = page_numbers[i];
        writes[i].data = test_update_pages[i];
        writes[i].first_dirty_us = 100u + i;
        writes[i].dirty_records[0] = 0xffffffffu;
        writes[i].dirty_records[1] = 0xffffffffu;
        writes[i].dirty_records[2] = 0xffffffffu;
        writes[i].dirty_records[3] =
            page_numbers[i] == PAL_EVENT_PAGER_PAGE_COUNT - 1u
            ? 0x01ffffffu : 0xffffffffu;
    }
}

static bool
verify_update_atomic(
    PalEventJournal *journal)
{
    static const uint16_t updated[3] = {
        0u,
        PAL_EVENT_PAGER_PAGE_COUNT - 1u,
        PAL_EVENT_JOURNAL_SCENE_PAGE,
    };
    uint8_t selected_tag;
    uint8_t i;

    if (read_matches(journal, updated[0], TEST_OLD_TAG)) {
        selected_tag = TEST_OLD_TAG;
    } else if (read_matches(journal, updated[0], TEST_NEW_TAG)) {
        selected_tag = TEST_NEW_TAG;
    } else {
        return false;
    }
    for (i = 1u; i < 3u; i++) {
        if (!read_matches(journal, updated[i], selected_tag)) {
            return false;
        }
    }
    return read_matches(journal, 17u, TEST_OLD_TAG);
}

static bool
make_baseline(
    TestDevice *device)
{
    PalEventJournal journal;
    PalEventPagerIoResult result;
    TestSource source = { TEST_OLD_TAG };

    device_reset_empty(device);
    init_journal(&journal, device);
    TEST_AT(!PalEventJournal_Open(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        test_page));
    TEST_AT(PalEventJournal_FormatAll(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        source_page,
        &source,
        test_page,
        &result));
    TEST_AT(result.storage_bytes >=
        (uint64_t)PAL_EVENT_JOURNAL_PAGE_COUNT *
            PAL_EVENT_JOURNAL_SLOT_BYTES);
    TEST_AT(result.sync_count >= 2u);
    memcpy(test_baseline, test_durable, sizeof(test_baseline));

    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(journal.generation == 1u);
    TEST_AT(verify_all_pages(&journal, TEST_OLD_TAG));
    return true;
}

static bool
test_profile_and_tails(
    TestDevice *device)
{
    PalEventJournal journal;
    PalEventPagerIoResult result;
    TestSource source = { TEST_OLD_TAG };
    uint32_t i;

    device_restore_baseline(device);
    TEST_AT(!open_journal(
        &journal,
        device,
        TEST_PROFILE_ID ^ 1u,
        TEST_TEMPLATE_CRC32));
    device_restore_baseline(device);
    TEST_AT(!open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32 ^ 1u));
    device_restore_baseline(device);
    init_journal(&journal, device);
    TEST_AT(PalEventJournal_OpenAnyIdentity(
        &journal, test_page));
    TEST_AT(journal.profile_id == TEST_PROFILE_ID);
    TEST_AT(journal.template_crc32 == TEST_TEMPLATE_CRC32);
    TEST_AT(verify_all_pages(&journal, TEST_OLD_TAG));

    /*
     * CRC32 zero is a valid identity, not an "ignore template" sentinel.
     * Exact Open must distinguish it while OpenAnyIdentity remains explicit.
     */
    device_reset_empty(device);
    init_journal(&journal, device);
    TEST_AT(PalEventJournal_FormatAll(
        &journal,
        TEST_PROFILE_ID,
        0u,
        source_page,
        &source,
        test_page,
        &result));
    device_crash(device);
    TEST_AT(open_journal(
        &journal, device, TEST_PROFILE_ID, 0u));
    device_crash(device);
    TEST_AT(!open_journal(
        &journal, device, TEST_PROFILE_ID, 1u));
    device_crash(device);
    init_journal(&journal, device);
    TEST_AT(PalEventJournal_OpenAnyIdentity(
        &journal, test_page));
    TEST_AT(journal.template_crc32 == 0u);

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    device->operation_count = 0u;
    TEST_AT(!PalEventJournal_RebindIdentity(
        &journal,
        TEST_REBOUND_PROFILE_ID,
        TEST_TEMPLATE_CRC32 ^ 1u,
        test_page,
        &result));
    TEST_AT(device->operation_count == 0u);
    TEST_AT(PalEventJournal_RebindIdentity(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        test_page,
        &result));
    TEST_AT(result.storage_bytes == 0u);
    TEST_AT(result.sync_count == 0u);
    TEST_AT(device->operation_count == 0u);

    TEST_AT(PalEventJournal_ReadPage(
        &journal,
        PAL_EVENT_PAGER_PAGE_COUNT - 1u,
        test_page,
        NULL));
    for (i = TEST_EVENT_TAIL_BYTES;
         i < PAL_EVENT_PAGER_PAGE_BYTES;
         i++) {
        TEST_AT(test_page[i] == 0u);
    }
    TEST_AT(PalEventJournal_ReadPage(
        &journal,
        PAL_EVENT_JOURNAL_SCENE_PAGE,
        test_page,
        NULL));
    for (i = PAL_EVENT_JOURNAL_SCENE_BYTES;
         i < PAL_EVENT_PAGER_PAGE_BYTES;
         i++) {
        TEST_AT(test_page[i] == 0u);
    }
    return true;
}

static bool
prepare_rebind_source_state(
    PalEventJournal *journal)
{
    PalEventPagerPageWrite write;

    memset(&write, 0, sizeof(write));
    fill_page(TEST_NEW_TAG, 17u, test_update_pages[0]);
    write.page = 17u;
    write.data = test_update_pages[0];
    write.first_dirty_us = 100u;
    write.dirty_records[0] = 0xffffffffu;
    write.dirty_records[1] = 0xffffffffu;
    write.dirty_records[2] = 0xffffffffu;
    write.dirty_records[3] = 0xffffffffu;
    return PalEventJournal_WritePages(
        journal,
        &write,
        1u,
        PAL_EVENT_WRITE_SAVE,
        NULL);
}

static bool
verify_rebind_source_state(
    PalEventJournal *journal)
{
    uint16_t page;

    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        if (!read_matches(
                journal,
                page,
                page == 17u ? TEST_NEW_TAG : TEST_OLD_TAG)) {
            return false;
        }
    }
    return true;
}

static bool
run_rebind_transaction(
    TestDevice *device,
    PalEventJournal *journal)
{
    PalEventPagerIoResult result;

    (void)device;
    return PalEventJournal_RebindIdentity(
        journal,
        TEST_REBOUND_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        test_page,
        &result);
}

static bool
capture_rebind_trace(
    TestDevice *device,
    TestOperation *operations,
    uint16_t *operation_count)
{
    PalEventJournal journal;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(prepare_rebind_source_state(&journal));
    TEST_AT(journal.generation == 2u);
    device->operation_count = 0u;
    device->trace_count = 0u;
    device->trace_enabled = true;
    TEST_AT(run_rebind_transaction(device, &journal));
    TEST_AT(journal.profile_id == TEST_REBOUND_PROFILE_ID);
    TEST_AT(journal.template_crc32 == TEST_TEMPLATE_CRC32);
    TEST_AT(journal.generation == 3u);
    TEST_AT(verify_rebind_source_state(&journal));
    TEST_AT(device->trace_count != 0u);
    TEST_AT(device->trace_count <= TEST_MAX_OPERATIONS);
    memcpy(
        operations,
        device->trace,
        (size_t)device->trace_count * sizeof(*operations));
    *operation_count = device->trace_count;
    return true;
}

static bool
run_rebind_fault_case(
    TestDevice *device,
    uint32_t operation,
    TestFaultKind fault,
    uint32_t divisor)
{
    PalEventJournal journal;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(prepare_rebind_source_state(&journal));
    TEST_AT(journal.generation == 2u);
    device->operation_count = 0u;
    device->fail_operation = operation;
    device->fault_kind = fault;
    device->partial_divisor = divisor;

    TEST_AT(!run_rebind_transaction(device, &journal));
    TEST_AT(device->fault_tripped);
    device_crash(device);
    init_journal(&journal, device);
    TEST_AT(PalEventJournal_OpenAnyIdentity(
        &journal, test_page));
    TEST_AT(journal.template_crc32 == TEST_TEMPLATE_CRC32);
    TEST_AT(journal.profile_id == TEST_PROFILE_ID ||
        journal.profile_id == TEST_REBOUND_PROFILE_ID);
    TEST_AT(journal.generation ==
        (journal.profile_id == TEST_PROFILE_ID ? 2u : 3u));
    TEST_AT(verify_rebind_source_state(&journal));
    return true;
}

static bool
test_rebind_fault_boundaries(
    TestDevice *device)
{
    TestOperation operations[TEST_MAX_OPERATIONS];
    uint16_t operation_count;
    uint16_t i;

    TEST_AT(capture_rebind_trace(
        device, operations, &operation_count));
    for (i = 0; i < operation_count; i++) {
        uint32_t operation = (uint32_t)i + 1u;

        test_case_operation = operation;
        test_case_fault = TEST_FAULT_BEFORE;
        TEST_AT(run_rebind_fault_case(
            device, operation, TEST_FAULT_BEFORE, 0u));

        test_case_fault = TEST_FAULT_AFTER_DURABLE;
        TEST_AT(run_rebind_fault_case(
            device, operation, TEST_FAULT_AFTER_DURABLE, 0u));

        if (operations[i].kind == TEST_OPERATION_WRITE &&
            operations[i].size > 1u) {
            test_case_fault = TEST_FAULT_PARTIAL_DURABLE;
            TEST_AT(run_rebind_fault_case(
                device,
                operation,
                TEST_FAULT_PARTIAL_DURABLE,
                2u));
        } else if (operations[i].kind == TEST_OPERATION_SYNC) {
            test_case_fault = TEST_FAULT_PARTIAL_SYNC;
            TEST_AT(run_rebind_fault_case(
                device,
                operation,
                TEST_FAULT_PARTIAL_SYNC,
                2u));
        }
    }
    test_case_operation = 0u;
    test_case_fault = TEST_FAULT_NONE;
    return true;
}

static bool
run_update_transaction(
    TestDevice *device,
    PalEventJournal *journal,
    PalEventPagerPageWrite writes[3])
{
    PalEventPagerIoResult result;

    (void)device;
    return PalEventJournal_WritePages(
        journal,
        writes,
        3u,
        PAL_EVENT_WRITE_SAVE,
        &result);
}

static bool
capture_update_trace(
    TestDevice *device,
    TestOperation *operations,
    uint16_t *operation_count)
{
    PalEventJournal journal;
    PalEventPagerPageWrite writes[3];

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    prepare_updates(writes);
    device->operation_count = 0u;
    device->trace_count = 0u;
    device->trace_enabled = true;
    TEST_AT(run_update_transaction(device, &journal, writes));
    TEST_AT(device->trace_count != 0u);
    TEST_AT(device->trace_count <= TEST_MAX_OPERATIONS);
    memcpy(
        operations,
        device->trace,
        (size_t)device->trace_count * sizeof(*operations));
    *operation_count = device->trace_count;
    return true;
}

static bool
run_update_fault_case(
    TestDevice *device,
    uint32_t operation,
    TestFaultKind fault,
    uint32_t divisor)
{
    PalEventJournal journal;
    PalEventPagerPageWrite writes[3];

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    prepare_updates(writes);
    device->operation_count = 0u;
    device->fail_operation = operation;
    device->fault_kind = fault;
    device->partial_divisor = divisor;

    TEST_AT(!run_update_transaction(device, &journal, writes));
    TEST_AT(device->fault_tripped);
    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(verify_update_atomic(&journal));
    return true;
}

static bool
test_update_fault_boundaries(
    TestDevice *device)
{
    TestOperation operations[TEST_MAX_OPERATIONS];
    uint16_t operation_count;
    uint16_t i;

    TEST_AT(capture_update_trace(
        device, operations, &operation_count));
    for (i = 0; i < operation_count; i++) {
        uint32_t operation = (uint32_t)i + 1u;

        test_case_operation = operation;
        test_case_fault = TEST_FAULT_BEFORE;
        TEST_AT(run_update_fault_case(
            device, operation, TEST_FAULT_BEFORE, 0u));

        test_case_fault = TEST_FAULT_AFTER_DURABLE;
        TEST_AT(run_update_fault_case(
            device, operation, TEST_FAULT_AFTER_DURABLE, 0u));

        if (operations[i].kind == TEST_OPERATION_WRITE &&
            operations[i].size > 1u) {
            static const uint32_t divisors[] = { 1u, 2u, 4u, 16u };
            uint8_t cut;

            for (cut = 0;
                 cut < sizeof(divisors) / sizeof(divisors[0]);
                 cut++) {
                test_case_fault = TEST_FAULT_PARTIAL_DURABLE;
                TEST_AT(run_update_fault_case(
                    device,
                    operation,
                    TEST_FAULT_PARTIAL_DURABLE,
                    divisors[cut]));
            }
        } else if (operations[i].kind == TEST_OPERATION_SYNC) {
            test_case_fault = TEST_FAULT_PARTIAL_SYNC;
            TEST_AT(run_update_fault_case(
                device,
                operation,
                TEST_FAULT_PARTIAL_SYNC,
                2u));
        }
    }
    test_case_operation = 0u;
    test_case_fault = TEST_FAULT_NONE;
    return true;
}

static bool
run_replace_transaction(
    TestDevice *device,
    PalEventJournal *journal)
{
    PalEventPagerIoResult result;
    TestSource source = { TEST_NEW_TAG };

    (void)device;
    return PalEventJournal_ReplaceAll(
        journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        source_page,
        &source,
        test_page,
        &result);
}

static bool
capture_replace_trace(
    TestDevice *device,
    TestOperation *operations,
    uint16_t *operation_count)
{
    PalEventJournal journal;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    device->operation_count = 0u;
    device->trace_count = 0u;
    device->trace_enabled = true;
    TEST_AT(run_replace_transaction(device, &journal));
    TEST_AT(device->trace_count != 0u);
    TEST_AT(device->trace_count <= TEST_MAX_OPERATIONS);
    memcpy(
        operations,
        device->trace,
        (size_t)device->trace_count * sizeof(*operations));
    *operation_count = device->trace_count;
    return true;
}

static bool
run_replace_fault_case(
    TestDevice *device,
    uint32_t operation,
    TestFaultKind fault,
    uint32_t divisor)
{
    PalEventJournal journal;
    uint8_t selected_tag;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    device->operation_count = 0u;
    device->fail_operation = operation;
    device->fault_kind = fault;
    device->partial_divisor = divisor;

    TEST_AT(!run_replace_transaction(device, &journal));
    TEST_AT(device->fault_tripped);
    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    if (read_matches(&journal, 0u, TEST_OLD_TAG)) {
        selected_tag = TEST_OLD_TAG;
    } else if (read_matches(&journal, 0u, TEST_NEW_TAG)) {
        selected_tag = TEST_NEW_TAG;
    } else {
        TEST_AT(false);
    }
    TEST_AT(verify_all_pages(&journal, selected_tag));
    return true;
}

static bool
test_replace_fault_boundaries(
    TestDevice *device)
{
    TestOperation operations[TEST_MAX_OPERATIONS];
    uint16_t operation_count;
    uint16_t i;

    TEST_AT(capture_replace_trace(
        device, operations, &operation_count));
    for (i = 0; i < operation_count; i++) {
        uint32_t operation = (uint32_t)i + 1u;

        test_case_operation = operation;
        test_case_fault = TEST_FAULT_BEFORE;
        TEST_AT(run_replace_fault_case(
            device, operation, TEST_FAULT_BEFORE, 0u));

        test_case_fault = TEST_FAULT_AFTER_DURABLE;
        TEST_AT(run_replace_fault_case(
            device, operation, TEST_FAULT_AFTER_DURABLE, 0u));

        if (operations[i].kind == TEST_OPERATION_WRITE &&
            operations[i].size > 1u) {
            test_case_fault = TEST_FAULT_PARTIAL_DURABLE;
            TEST_AT(run_replace_fault_case(
                device,
                operation,
                TEST_FAULT_PARTIAL_DURABLE,
                2u));
        } else if (operations[i].kind == TEST_OPERATION_SYNC) {
            test_case_fault = TEST_FAULT_PARTIAL_SYNC;
            TEST_AT(run_replace_fault_case(
                device,
                operation,
                TEST_FAULT_PARTIAL_SYNC,
                2u));
        }
    }
    test_case_operation = 0u;
    test_case_fault = TEST_FAULT_NONE;
    return true;
}

static uint32_t
physical_slot_offset(
    uint16_t page,
    uint32_t version)
{
    uint8_t bank = (version & 0x80000000u) != 0u ? 1u : 0u;

    return PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET +
        ((uint32_t)page * PAL_EVENT_JOURNAL_SLOT_COUNT + bank) *
            PAL_EVENT_JOURNAL_SLOT_BYTES;
}

static void
write_test_le32(
    uint8_t *p,
    uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void
refresh_test_slot_crcs(
    uint8_t *image,
    uint32_t slot_offset)
{
    uint8_t *header = image + slot_offset;
    uint8_t *payload =
        header + PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES;
    uint32_t crc;

    crc = PalEventJournal_Crc32(
        payload, PAL_EVENT_PAGER_PAGE_BYTES);
    write_test_le32(header + 28u, crc);
    write_test_le32(
        header + PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES - sizeof(uint32_t),
        0u);
    crc = PalEventJournal_Crc32(
        header, PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES);
    write_test_le32(
        header + PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES - sizeof(uint32_t),
        crc);
}

static bool
test_corrupt_newest_fallback(
    TestDevice *device)
{
    PalEventJournal journal;
    PalEventPagerPageWrite writes[3];
    uint32_t corrupt_offset;
    const PalEventJournalMetrics *metrics;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    prepare_updates(writes);
    TEST_AT(run_update_transaction(device, &journal, writes));
    TEST_AT(journal.generation == 2u);

    corrupt_offset = physical_slot_offset(
        PAL_EVENT_PAGER_PAGE_COUNT - 1u,
        journal.page_versions[PAL_EVENT_PAGER_PAGE_COUNT - 1u]) +
        PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES + 123u;
    TEST_AT(corrupt_offset < PAL_EVENT_JOURNAL_FILE_BYTES);
    test_durable[corrupt_offset] ^= 0x5au;
    device_crash(device);

    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(journal.generation == 1u);
    TEST_AT(verify_all_pages(&journal, TEST_OLD_TAG));
    metrics = PalEventJournal_GetMetrics(&journal);
    TEST_AT(metrics != NULL);
    TEST_AT(metrics->recovery_fallback_count == 1u);
    return true;
}

static bool
test_active_read_failure_is_fail_stop(
    TestDevice *device)
{
    PalEventJournal journal;
    PalEventPagerPageWrite writes[3];
    uint32_t corrupt_offset;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    prepare_updates(writes);
    TEST_AT(run_update_transaction(device, &journal, writes));
    TEST_AT(journal.generation == 2u);

    corrupt_offset = physical_slot_offset(
        PAL_EVENT_PAGER_PAGE_COUNT - 1u,
        journal.page_versions[PAL_EVENT_PAGER_PAGE_COUNT - 1u]) +
        PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES + 321u;
    TEST_AT(corrupt_offset < PAL_EVENT_JOURNAL_FILE_BYTES);
    test_volatile[corrupt_offset] ^= 0xa6u;
    test_durable[corrupt_offset] ^= 0xa6u;

    TEST_AT(!PalEventJournal_ReadPage(
        &journal,
        PAL_EVENT_PAGER_PAGE_COUNT - 1u,
        test_page,
        NULL));
    TEST_AT(journal.poisoned);
    TEST_AT(!PalEventJournal_WritePages(
        &journal,
        writes,
        1u,
        PAL_EVENT_WRITE_SYNC,
        NULL));
    TEST_AT(!PalEventJournal_Open(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        test_page));

    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(journal.generation == 1u);
    TEST_AT(verify_all_pages(&journal, TEST_OLD_TAG));
    return true;
}

static bool
test_replace_rejects_self_consistent_stale_orphan(
    TestDevice *device)
{
    TestOperation operations[TEST_MAX_OPERATIONS];
    PalEventJournal journal;
    PalEventPagerIoResult result;
    TestSource orphan_source = { TEST_NEW_TAG };
    TestSource replacement_source = { TEST_NEWER_TAG };
    uint16_t operation_count;
    uint32_t commit_write_operation;
    uint32_t orphan_slot_offset;

    TEST_AT(capture_replace_trace(
        device, operations, &operation_count));
    TEST_AT(operation_count >= 2u);
    TEST_AT(operations[operation_count - 2u].kind ==
        TEST_OPERATION_WRITE);
    TEST_AT(operations[operation_count - 1u].kind ==
        TEST_OPERATION_SYNC);
    commit_write_operation = operation_count - 1u;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    device->operation_count = 0u;
    device->fail_operation = commit_write_operation;
    device->fault_kind = TEST_FAULT_BEFORE;
    TEST_AT(!PalEventJournal_ReplaceAll(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        source_page,
        &orphan_source,
        test_page,
        &result));
    TEST_AT(device->fault_tripped);

    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(journal.generation == 1u);

    orphan_slot_offset =
        PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET +
        (10u * PAL_EVENT_JOURNAL_SLOT_COUNT + 1u) *
            PAL_EVENT_JOURNAL_SLOT_BYTES;
    device->silent_drop_enabled = true;
    device->silent_drop_offset = orphan_slot_offset;
    device->silent_drop_size = PAL_EVENT_JOURNAL_SLOT_BYTES;
    TEST_AT(!PalEventJournal_ReplaceAll(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        source_page,
        &replacement_source,
        test_page,
        &result));

    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(journal.generation == 1u);
    TEST_AT(verify_all_pages(&journal, TEST_OLD_TAG));
    return true;
}

static bool
test_replace_rejects_self_consistent_nonzero_tail(
    TestDevice *device)
{
    TestOperation operations[TEST_MAX_OPERATIONS];
    PalEventJournal journal;
    PalEventPagerIoResult result;
    TestSource source = { TEST_NEW_TAG };
    uint16_t operation_count;
    uint32_t commit_write_operation;
    uint32_t orphan_slot_offset;
    uint32_t tail_offset;

    TEST_AT(capture_replace_trace(
        device, operations, &operation_count));
    TEST_AT(operation_count >= 2u);
    TEST_AT(operations[operation_count - 2u].kind ==
        TEST_OPERATION_WRITE);
    TEST_AT(operations[operation_count - 1u].kind ==
        TEST_OPERATION_SYNC);
    commit_write_operation = operation_count - 1u;

    device_restore_baseline(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    device->operation_count = 0u;
    device->fail_operation = commit_write_operation;
    device->fault_kind = TEST_FAULT_BEFORE;
    TEST_AT(!PalEventJournal_ReplaceAll(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        source_page,
        &source,
        test_page,
        &result));
    TEST_AT(device->fault_tripped);

    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(journal.generation == 1u);

    orphan_slot_offset =
        PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET +
        ((uint32_t)PAL_EVENT_JOURNAL_SCENE_PAGE *
             PAL_EVENT_JOURNAL_SLOT_COUNT +
         1u) *
            PAL_EVENT_JOURNAL_SLOT_BYTES;
    tail_offset = orphan_slot_offset +
        PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES +
        PAL_EVENT_JOURNAL_SCENE_BYTES;
    TEST_AT(tail_offset < PAL_EVENT_JOURNAL_FILE_BYTES);
    TEST_AT(test_volatile[tail_offset] == 0u);
    TEST_AT(test_durable[tail_offset] == 0u);
    test_volatile[tail_offset] = 0xa5u;
    test_durable[tail_offset] = 0xa5u;
    refresh_test_slot_crcs(test_volatile, orphan_slot_offset);
    refresh_test_slot_crcs(test_durable, orphan_slot_offset);

    device->silent_drop_enabled = true;
    device->silent_drop_offset = orphan_slot_offset;
    device->silent_drop_size = PAL_EVENT_JOURNAL_SLOT_BYTES;
    TEST_AT(!PalEventJournal_ReplaceAll(
        &journal,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32,
        source_page,
        &source,
        test_page,
        &result));

    device_crash(device);
    TEST_AT(open_journal(
        &journal,
        device,
        TEST_PROFILE_ID,
        TEST_TEMPLATE_CRC32));
    TEST_AT(journal.generation == 1u);
    TEST_AT(verify_all_pages(&journal, TEST_OLD_TAG));
    return true;
}

int
main(
    void)
{
    TestDevice device;

    memset(&device, 0, sizeof(device));
    if (!make_baseline(&device) ||
        !test_profile_and_tails(&device) ||
        !test_rebind_fault_boundaries(&device) ||
        !test_update_fault_boundaries(&device) ||
        !test_replace_fault_boundaries(&device) ||
        !test_corrupt_newest_fallback(&device) ||
        !test_active_read_failure_is_fail_stop(&device) ||
        !test_replace_rejects_self_consistent_stale_orphan(&device) ||
        !test_replace_rejects_self_consistent_nonzero_tail(&device)) {
        return 1;
    }

    puts(
        "event journal fault test: baseline/reopen, atomic rebind/batch/replace, "
        "rebind write+sync crash boundaries, torn writes, profile/tails, "
        "newest fallback, active-read fail-stop, stale-orphan and nonzero-tail "
        "reject: ok");
    return 0;
}
