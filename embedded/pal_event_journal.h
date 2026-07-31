#ifndef PAL_EVENT_JOURNAL_H
#define PAL_EVENT_JOURNAL_H

#include "pal_event_pager.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One additional logical page stores the mutable 300-entry SCENE table.
 * Every logical page has two 512-byte-aligned copy-on-write physical slots.
 * Two checksummed commit sectors select a complete directory generation.
 */
#define PAL_EVENT_JOURNAL_SCENE_PAGE          42u
#define PAL_EVENT_JOURNAL_PAGE_COUNT          43u
#define PAL_EVENT_JOURNAL_COMMIT_BYTES        512u
#define PAL_EVENT_JOURNAL_COMMIT_COUNT        2u
#define PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES   512u
#define PAL_EVENT_JOURNAL_SLOT_BYTES          4608u
#define PAL_EVENT_JOURNAL_SLOT_COUNT          2u
#define PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET  1024u
#define PAL_EVENT_JOURNAL_FILE_BYTES \
    (PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET + \
     PAL_EVENT_JOURNAL_PAGE_COUNT * PAL_EVENT_JOURNAL_SLOT_COUNT * \
         PAL_EVENT_JOURNAL_SLOT_BYTES)
#define PAL_EVENT_JOURNAL_SCENE_BYTES         2400u

typedef bool (*PalEventJournalReadAt)(
    void *user,
    uint32_t offset,
    uint8_t *dst,
    uint32_t size);

typedef bool (*PalEventJournalWriteAt)(
    void *user,
    uint32_t offset,
    const uint8_t *src,
    uint32_t size);

typedef bool (*PalEventJournalSync)(void *user);
typedef uint64_t (*PalEventJournalNowUs)(void *user);

typedef struct PalEventJournalIo {
    PalEventJournalReadAt read_at;
    PalEventJournalWriteAt write_at;
    PalEventJournalSync sync;
    PalEventJournalNowUs now_us;
    void *user;
} PalEventJournalIo;

typedef struct PalEventJournalMetrics {
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_calls;
    uint64_t write_calls;
    uint64_t sync_calls;
    uint64_t transaction_count;
    uint64_t page_write_count;
    uint64_t replacement_count;
    uint64_t recovery_fallback_count;
    uint64_t failure_count;
    uint64_t read_elapsed_us;
    uint64_t write_elapsed_us;
} PalEventJournalMetrics;

typedef struct PalEventJournal {
    PalEventJournalIo io;
    uint8_t *sector_scratch;
    uint32_t page_versions[PAL_EVENT_JOURNAL_PAGE_COUNT];
    PalEventJournalMetrics metrics;
    uint32_t profile_id;
    uint32_t template_crc32;
    uint32_t generation;
    uint64_t sequence;
    uint8_t active_commit;
    uint8_t ready;
    uint8_t poisoned;
    uint8_t io_error;
} PalEventJournal;

typedef bool (*PalEventJournalPageSource)(
    void *user,
    uint16_t logical_page,
    uint8_t page[PAL_EVENT_PAGER_PAGE_BYTES]);

void PalEventJournal_Init(
    PalEventJournal *journal,
    const PalEventJournalIo *io,
    uint8_t sector_scratch[PAL_EVENT_JOURNAL_COMMIT_BYTES]);

/*
 * Open both commit candidates and fully validate the newest candidate's
 * selected pages.  If those fail, page_scratch is used to validate the older
 * candidate so recovery can fall back to a complete generation.  Open
 * refuses a poisoned object:
 * after an uncertain commit or active-page read failure, the caller must
 * close/remount/reopen its storage view and call PalEventJournal_Init before
 * recovery.  This prevents volatile, not-yet-durable cache contents from
 * being mistaken for a recovered generation.
 */
bool PalEventJournal_Open(
    PalEventJournal *journal,
    uint32_t expected_profile_id,
    uint32_t expected_template_crc32,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES]);

/*
 * Recover the newest internally valid generation without constraining its
 * resource identity.  This is only for a caller that has already failed an
 * exact Open() without an I/O error and needs to distinguish a sound journal
 * from a resource-pack switch.  On success profile_id/template_crc32 in the
 * journal identify the recovered data; callers must never use it as current
 * game state until they have explicitly rebased/replaced all pages.
 */
bool PalEventJournal_OpenAnyIdentity(
    PalEventJournal *journal,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES]);

/*
 * Preserve every committed page while changing only the physical pack/profile
 * identity.  This operation is deliberately limited to an unchanged template
 * CRC: callers must use ReplaceAll when the default event/scene payload has
 * changed.
 *
 * All pages are copied to their inactive banks under the new identity, synced,
 * and verified before one new commit sector publishes the directory.  A crash
 * can therefore recover only the complete old identity or the complete new
 * identity, never a mixture.
 */
bool PalEventJournal_RebindIdentity(
    PalEventJournal *journal,
    uint32_t profile_id,
    uint32_t template_crc32,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result);

/*
 * Atomically replace all 42 event pages and the SCENE page of an already-open
 * valid journal.  Its selected slots remain untouched until the new commit
 * sector is durable and verified.
 */
bool PalEventJournal_ReplaceAll(
    PalEventJournal *journal,
    uint32_t profile_id,
    uint32_t template_crc32,
    PalEventJournalPageSource source,
    void *source_user,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result);

/*
 * Destructively initialize a file that the caller has independently proven
 * to be new/disposable.  Unlike ReplaceAll, this deliberately invalidates
 * both old commit sectors first and provides no old-generation fallback.
 * Never use it as an automatic response to an I/O error or profile mismatch.
 */
bool PalEventJournal_FormatAll(
    PalEventJournal *journal,
    uint32_t profile_id,
    uint32_t template_crc32,
    PalEventJournalPageSource source,
    void *source_user,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result);

bool PalEventJournal_ReadPage(
    PalEventJournal *journal,
    uint16_t logical_page,
    uint8_t page[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result);

/*
 * Commit up to four distinct logical pages as one generation.  The pager
 * currently supplies at most three event pages; the fourth position permits
 * an event flush and the SCENE mirror to share one commit in the integration
 * layer.
 */
bool PalEventJournal_WritePages(
    PalEventJournal *journal,
    const PalEventPagerPageWrite *pages,
    uint8_t page_count,
    PalEventPagerWriteReason reason,
    PalEventPagerIoResult *result);

/*
 * For event pages 0..40, PalEventPagerPageWrite.data addresses 4096 bytes.
 * Page 41 consumes only the 3,872 bytes occupied by events 5,249..5,369 and
 * stores a zero tail.  The SCENE page consumes only 2,400 bytes and stores a
 * zero tail, so its caller does not need a separate padded 4 KiB buffer.
 */

const PalEventJournalMetrics *PalEventJournal_GetMetrics(
    const PalEventJournal *journal);

uint32_t PalEventJournal_Crc32(
    const uint8_t *bytes,
    uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
