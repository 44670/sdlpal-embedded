#include "pal_event_journal.h"

#include <limits.h>
#include <string.h>

#define PAL_EVENT_JOURNAL_COMMIT_MAGIC_0 0x45564150u /* "PAVE" */
#define PAL_EVENT_JOURNAL_COMMIT_MAGIC_1 0x3154534au /* "JST1" */
#define PAL_EVENT_JOURNAL_PAGE_MAGIC     0x47505645u /* "EVPG" */
#define PAL_EVENT_JOURNAL_VERSION        1u
#define PAL_EVENT_JOURNAL_CRC_OFFSET     508u
#define PAL_EVENT_JOURNAL_DIRECTORY_OFFSET 64u
#define PAL_EVENT_JOURNAL_BANK_BIT       0x80000000u
#define PAL_EVENT_JOURNAL_GENERATION_MASK 0x7fffffffu
#define PAL_EVENT_JOURNAL_MAX_BATCH      4u
#define PAL_EVENT_JOURNAL_EVENT_TAIL_BYTES \
    ((PAL_EVENT_PAGER_RECORD_COUNT % PAL_EVENT_PAGER_RECORDS_PER_PAGE) * \
        PAL_EVENT_PAGER_RECORD_BYTES)

typedef char PalEventJournalAssertScenePage[
    PAL_EVENT_JOURNAL_SCENE_PAGE == PAL_EVENT_PAGER_PAGE_COUNT ? 1 : -1];
typedef char PalEventJournalAssertDirectoryFits[
    PAL_EVENT_JOURNAL_DIRECTORY_OFFSET +
        PAL_EVENT_JOURNAL_PAGE_COUNT * sizeof(uint32_t) <=
        PAL_EVENT_JOURNAL_CRC_OFFSET ? 1 : -1];
typedef char PalEventJournalAssertSlotShape[
    PAL_EVENT_JOURNAL_SLOT_BYTES ==
        PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES +
            PAL_EVENT_PAGER_PAGE_BYTES ? 1 : -1];
typedef char PalEventJournalAssertEventTail[
    PAL_EVENT_JOURNAL_EVENT_TAIL_BYTES == 3872u ? 1 : -1];
typedef char PalEventJournalAssertFileFits[
    PAL_EVENT_JOURNAL_FILE_BYTES <= UINT32_MAX ? 1 : -1];

typedef struct PalEventJournalCandidate {
    uint32_t versions[PAL_EVENT_JOURNAL_PAGE_COUNT];
    uint32_t generation;
    uint32_t profile_id;
    uint32_t template_crc32;
    uint64_t sequence;
    uint8_t commit_slot;
    uint8_t valid;
} PalEventJournalCandidate;

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
write_le16(
    uint8_t *p,
    uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
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

uint32_t
PalEventJournal_Crc32(
    const uint8_t *bytes,
    uint32_t size)
{
    uint32_t crc = 0xffffffffu;
    uint32_t i;

    if (bytes == NULL && size != 0u) {
        return 0u;
    }
    for (i = 0; i < size; i++) {
        uint32_t value = crc ^ bytes[i];
        uint8_t bit;

        for (bit = 0; bit < 8u; bit++) {
            value = (value >> 1) ^
                (0xedb88320u & (uint32_t)-(int32_t)(value & 1u));
        }
        crc = value;
    }
    return crc ^ 0xffffffffu;
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
logical_page_bytes(
    uint16_t logical_page)
{
    if (logical_page == PAL_EVENT_PAGER_PAGE_COUNT - 1u) {
        return PAL_EVENT_JOURNAL_EVENT_TAIL_BYTES;
    }
    if (logical_page == PAL_EVENT_JOURNAL_SCENE_PAGE) {
        return PAL_EVENT_JOURNAL_SCENE_BYTES;
    }
    return PAL_EVENT_PAGER_PAGE_BYTES;
}

static uint32_t
padded_page_crc(
    uint16_t logical_page,
    const uint8_t *payload)
{
    static const uint8_t zeros[PAL_EVENT_JOURNAL_COMMIT_BYTES];
    uint32_t valid_bytes = logical_page_bytes(logical_page);
    uint32_t remaining = PAL_EVENT_PAGER_PAGE_BYTES - valid_bytes;
    uint32_t crc = crc32_update(0xffffffffu, payload, valid_bytes);

    while (remaining != 0u) {
        uint32_t amount = remaining > sizeof(zeros)
            ? (uint32_t)sizeof(zeros) : remaining;

        crc = crc32_update(crc, zeros, amount);
        remaining -= amount;
    }
    return crc ^ 0xffffffffu;
}

static uint32_t
crc_sector(
    const uint8_t *sector)
{
    uint32_t crc = 0xffffffffu;
    uint32_t i;

    for (i = 0; i < PAL_EVENT_JOURNAL_COMMIT_BYTES; i++) {
        uint8_t input =
            i >= PAL_EVENT_JOURNAL_CRC_OFFSET &&
            i < PAL_EVENT_JOURNAL_CRC_OFFSET + sizeof(uint32_t)
            ? 0u
            : sector[i];
        uint32_t value = crc ^ input;
        uint8_t bit;

        for (bit = 0; bit < 8u; bit++) {
            value = (value >> 1) ^
                (0xedb88320u & (uint32_t)-(int32_t)(value & 1u));
        }
        crc = value;
    }
    return crc ^ 0xffffffffu;
}

static bool
ranges_overlap(
    const void *a,
    uint32_t a_size,
    const void *b,
    uint32_t b_size)
{
    uintptr_t a_start = (uintptr_t)a;
    uintptr_t b_start = (uintptr_t)b;

    return a_size != 0u && b_size != 0u &&
        a_start < b_start + b_size &&
        b_start < a_start + a_size;
}

static uint64_t
now_us(
    PalEventJournal *journal)
{
    return journal->io.now_us != NULL
        ? journal->io.now_us(journal->io.user)
        : 0u;
}

static bool
io_read(
    PalEventJournal *journal,
    uint32_t offset,
    uint8_t *dst,
    uint32_t size)
{
    if (journal->io.read_at == NULL ||
        !journal->io.read_at(journal->io.user, offset, dst, size)) {
        journal->io_error = 1u;
        journal->metrics.failure_count++;
        return false;
    }
    journal->metrics.read_calls++;
    journal->metrics.read_bytes += size;
    return true;
}

static bool
io_write(
    PalEventJournal *journal,
    uint32_t offset,
    const uint8_t *src,
    uint32_t size)
{
    if (journal->io.write_at == NULL ||
        !journal->io.write_at(journal->io.user, offset, src, size)) {
        journal->metrics.failure_count++;
        return false;
    }
    journal->metrics.write_calls++;
    journal->metrics.write_bytes += size;
    return true;
}

static bool
io_sync(
    PalEventJournal *journal)
{
    if (journal->io.sync == NULL ||
        !journal->io.sync(journal->io.user)) {
        journal->metrics.failure_count++;
        return false;
    }
    journal->metrics.sync_calls++;
    return true;
}

static uint32_t
slot_offset(
    uint16_t logical_page,
    uint8_t bank)
{
    return PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET +
        ((uint32_t)logical_page * PAL_EVENT_JOURNAL_SLOT_COUNT + bank) *
            PAL_EVENT_JOURNAL_SLOT_BYTES;
}

static uint8_t
version_bank(
    uint32_t version)
{
    return (version & PAL_EVENT_JOURNAL_BANK_BIT) != 0u ? 1u : 0u;
}

static uint32_t
version_generation(
    uint32_t version)
{
    return version & PAL_EVENT_JOURNAL_GENERATION_MASK;
}

static uint32_t
make_version(
    uint8_t bank,
    uint32_t generation)
{
    return (bank != 0u ? PAL_EVENT_JOURNAL_BANK_BIT : 0u) |
        (generation & PAL_EVENT_JOURNAL_GENERATION_MASK);
}

static void
build_page_header(
    PalEventJournal *journal,
    uint16_t logical_page,
    uint8_t bank,
    uint32_t generation,
    const uint8_t *payload)
{
    uint8_t *header = journal->sector_scratch;
    uint32_t crc;

    memset(header, 0, PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES);
    write_le32(header, PAL_EVENT_JOURNAL_PAGE_MAGIC);
    write_le16(header + 4u, PAL_EVENT_JOURNAL_VERSION);
    write_le16(header + 6u, PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES);
    write_le16(header + 8u, logical_page);
    header[10] = bank;
    header[11] =
        logical_page == PAL_EVENT_JOURNAL_SCENE_PAGE ? 1u : 0u;
    write_le32(header + 12u, generation);
    write_le32(header + 16u, journal->profile_id);
    write_le32(header + 20u, journal->template_crc32);
    write_le32(header + 24u, logical_page_bytes(logical_page));
    write_le32(
        header + 28u,
        padded_page_crc(logical_page, payload));
    write_le32(header + 32u, (uint32_t)journal->sequence);
    write_le32(header + 36u, (uint32_t)(journal->sequence >> 32));
    write_le32(header + PAL_EVENT_JOURNAL_CRC_OFFSET, 0u);
    crc = PalEventJournal_Crc32(
        header, PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES);
    write_le32(header + PAL_EVENT_JOURNAL_CRC_OFFSET, crc);
}

static bool
validate_page_header(
    PalEventJournal *journal,
    uint16_t logical_page,
    uint8_t bank,
    uint32_t generation,
    const uint8_t *header)
{
    uint32_t expected_crc;

    if (read_le32(header) != PAL_EVENT_JOURNAL_PAGE_MAGIC ||
        read_le16(header + 4u) != PAL_EVENT_JOURNAL_VERSION ||
        read_le16(header + 6u) != PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES ||
        read_le16(header + 8u) != logical_page ||
        header[10] != bank ||
        header[11] !=
            (logical_page == PAL_EVENT_JOURNAL_SCENE_PAGE ? 1u : 0u) ||
        read_le32(header + 12u) != generation ||
        read_le32(header + 16u) != journal->profile_id ||
        read_le32(header + 20u) != journal->template_crc32 ||
        read_le32(header + 24u) != logical_page_bytes(logical_page)) {
        return false;
    }
    expected_crc = read_le32(
        header + PAL_EVENT_JOURNAL_CRC_OFFSET);
    return crc_sector(header) == expected_crc;
}

static bool
read_physical_page(
    PalEventJournal *journal,
    uint16_t logical_page,
    uint8_t bank,
    uint32_t generation,
    uint8_t *page)
{
    uint32_t offset = slot_offset(logical_page, bank);
    uint32_t payload_crc;

    if (!io_read(
            journal,
            offset,
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES) ||
        !validate_page_header(
            journal,
            logical_page,
            bank,
            generation,
            journal->sector_scratch)) {
        return false;
    }
    payload_crc = read_le32(journal->sector_scratch + 28u);
    if (!io_read(
            journal,
            offset + PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES,
            page,
            PAL_EVENT_PAGER_PAGE_BYTES)) {
        return false;
    }
    return PalEventJournal_Crc32(
        page, PAL_EVENT_PAGER_PAGE_BYTES) == payload_crc;
}

static bool
write_physical_page(
    PalEventJournal *journal,
    uint16_t logical_page,
    uint8_t bank,
    uint32_t generation,
    const uint8_t *page)
{
    uint32_t offset = slot_offset(logical_page, bank);
    uint32_t valid_bytes = logical_page_bytes(logical_page);
    uint32_t payload_offset = 0u;

    journal->sequence++;
    build_page_header(
        journal, logical_page, bank, generation, page);
    if (!io_write(
            journal,
            offset,
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES)) {
        return false;
    }
    if (valid_bytes == PAL_EVENT_PAGER_PAGE_BYTES) {
        return io_write(
            journal,
            offset + PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES,
            page,
            PAL_EVENT_PAGER_PAGE_BYTES);
    }

    /*
     * Keep every physical payload write sector-aligned while normalizing the
     * unused tail.  sector_scratch is free after the header has been written.
     */
    while (payload_offset < PAL_EVENT_PAGER_PAGE_BYTES) {
        uint32_t amount = 0u;

        memset(
            journal->sector_scratch,
            0,
            PAL_EVENT_JOURNAL_COMMIT_BYTES);
        if (payload_offset < valid_bytes) {
            amount = valid_bytes - payload_offset;
            if (amount > PAL_EVENT_JOURNAL_COMMIT_BYTES) {
                amount = PAL_EVENT_JOURNAL_COMMIT_BYTES;
            }
            memcpy(
                journal->sector_scratch,
                page + payload_offset,
                amount);
        }
        if (!io_write(
                journal,
                offset + PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES +
                    payload_offset,
                journal->sector_scratch,
                PAL_EVENT_JOURNAL_COMMIT_BYTES)) {
            return false;
        }
        payload_offset += PAL_EVENT_JOURNAL_COMMIT_BYTES;
    }
    return true;
}

static void
build_commit(
    PalEventJournal *journal,
    uint32_t generation,
    const uint32_t *versions)
{
    uint8_t *commit = journal->sector_scratch;
    uint16_t page;
    uint32_t crc;

    memset(commit, 0, PAL_EVENT_JOURNAL_COMMIT_BYTES);
    write_le32(commit, PAL_EVENT_JOURNAL_COMMIT_MAGIC_0);
    write_le32(commit + 4u, PAL_EVENT_JOURNAL_COMMIT_MAGIC_1);
    write_le16(commit + 8u, PAL_EVENT_JOURNAL_VERSION);
    write_le16(commit + 10u, PAL_EVENT_JOURNAL_COMMIT_BYTES);
    write_le16(commit + 12u, PAL_EVENT_JOURNAL_PAGE_COUNT);
    write_le16(commit + 14u, PAL_EVENT_PAGER_PAGE_BYTES);
    write_le16(commit + 16u, PAL_EVENT_PAGER_RECORD_BYTES);
    write_le16(commit + 18u, 8u);
    write_le32(commit + 20u, PAL_EVENT_PAGER_RECORD_COUNT);
    write_le32(commit + 24u, 300u);
    write_le32(commit + 28u, generation);
    write_le32(commit + 32u, journal->profile_id);
    write_le32(commit + 36u, journal->template_crc32);
    write_le32(commit + 40u, PAL_EVENT_JOURNAL_FILE_BYTES);
    write_le32(commit + 44u, PAL_EVENT_JOURNAL_SLOT_BYTES);
    write_le32(commit + 48u, PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET);
    write_le32(commit + 52u, (uint32_t)journal->sequence);
    write_le32(commit + 56u, (uint32_t)(journal->sequence >> 32));
    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        write_le32(
            commit + PAL_EVENT_JOURNAL_DIRECTORY_OFFSET +
                (uint32_t)page * 4u,
            versions[page]);
    }
    write_le32(commit + PAL_EVENT_JOURNAL_CRC_OFFSET, 0u);
    crc = PalEventJournal_Crc32(
        commit, PAL_EVENT_JOURNAL_COMMIT_BYTES);
    write_le32(commit + PAL_EVENT_JOURNAL_CRC_OFFSET, crc);
}

static bool
parse_commit(
    PalEventJournal *journal,
    uint8_t commit_slot,
    uint32_t expected_profile_id,
    uint32_t expected_template_crc32,
    bool require_identity,
    PalEventJournalCandidate *candidate)
{
    const uint8_t *commit = journal->sector_scratch;
    uint32_t expected_crc;
    uint32_t generation;
    uint16_t page;

    memset(candidate, 0, sizeof(*candidate));
    if (!io_read(
            journal,
            (uint32_t)commit_slot * PAL_EVENT_JOURNAL_COMMIT_BYTES,
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_COMMIT_BYTES)) {
        return false;
    }
    expected_crc = read_le32(
        commit + PAL_EVENT_JOURNAL_CRC_OFFSET);
    if (read_le32(commit) != PAL_EVENT_JOURNAL_COMMIT_MAGIC_0 ||
        read_le32(commit + 4u) != PAL_EVENT_JOURNAL_COMMIT_MAGIC_1 ||
        read_le16(commit + 8u) != PAL_EVENT_JOURNAL_VERSION ||
        read_le16(commit + 10u) != PAL_EVENT_JOURNAL_COMMIT_BYTES ||
        read_le16(commit + 12u) != PAL_EVENT_JOURNAL_PAGE_COUNT ||
        read_le16(commit + 14u) != PAL_EVENT_PAGER_PAGE_BYTES ||
        read_le16(commit + 16u) != PAL_EVENT_PAGER_RECORD_BYTES ||
        read_le16(commit + 18u) != 8u ||
        read_le32(commit + 20u) != PAL_EVENT_PAGER_RECORD_COUNT ||
        read_le32(commit + 24u) != 300u ||
        read_le32(commit + 40u) != PAL_EVENT_JOURNAL_FILE_BYTES ||
        read_le32(commit + 44u) != PAL_EVENT_JOURNAL_SLOT_BYTES ||
        read_le32(commit + 48u) !=
            PAL_EVENT_JOURNAL_SLOT_REGION_OFFSET ||
        crc_sector(journal->sector_scratch) != expected_crc) {
        return false;
    }

    generation = read_le32(commit + 28u);
    candidate->profile_id = read_le32(commit + 32u);
    candidate->template_crc32 = read_le32(commit + 36u);
    candidate->sequence =
        (uint64_t)read_le32(commit + 52u) |
        ((uint64_t)read_le32(commit + 56u) << 32);
    if (generation == 0u || candidate->profile_id == 0u ||
        generation > PAL_EVENT_JOURNAL_GENERATION_MASK ||
        (require_identity &&
         (candidate->profile_id != expected_profile_id ||
          candidate->template_crc32 != expected_template_crc32))) {
        return false;
    }

    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        uint32_t version = read_le32(
            commit + PAL_EVENT_JOURNAL_DIRECTORY_OFFSET +
                (uint32_t)page * 4u);
        uint32_t page_generation = version_generation(version);

        if (page_generation == 0u || page_generation > generation) {
            return false;
        }
        candidate->versions[page] = version;
    }
    candidate->generation = generation;
    candidate->commit_slot = commit_slot;
    candidate->valid = 1u;
    return true;
}

static bool
validate_candidate_pages(
    PalEventJournal *journal,
    const PalEventJournalCandidate *candidate,
    uint8_t *page_scratch)
{
    uint16_t page;
    uint32_t old_profile = journal->profile_id;
    uint32_t old_template_crc = journal->template_crc32;
    bool valid = true;

    journal->profile_id = candidate->profile_id;
    journal->template_crc32 = candidate->template_crc32;
    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        uint32_t version = candidate->versions[page];

        if (!read_physical_page(
                journal,
                page,
                version_bank(version),
                version_generation(version),
                page_scratch)) {
            valid = false;
            break;
        }
    }
    journal->profile_id = old_profile;
    journal->template_crc32 = old_template_crc;
    return valid;
}

static bool
write_commit_and_verify(
    PalEventJournal *journal,
    uint8_t commit_slot,
    uint32_t generation,
    const uint32_t *versions)
{
    PalEventJournalCandidate candidate;

    /*
     * A storage stack may complete some or all of a sector write and still
     * report an error.  From the first commit-sector write attempt onward,
     * the on-media generation is uncertain until a sync and full readback
     * prove exactly which directory was published.
     */
    journal->poisoned = 1u;
    build_commit(journal, generation, versions);
    if (!io_write(
            journal,
            (uint32_t)commit_slot * PAL_EVENT_JOURNAL_COMMIT_BYTES,
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_COMMIT_BYTES)) {
        return false;
    }
    /*
     * Once a commit sector write has started, a failed sync/readback leaves
     * the in-process active generation uncertain.  Force reopen/recovery
     * rather than allowing another transaction to overwrite either bank.
     */
    if (!io_sync(journal)) {
        return false;
    }
    if (!parse_commit(
            journal,
            commit_slot,
            journal->profile_id,
            journal->template_crc32,
            true,
            &candidate) ||
        candidate.generation != generation ||
        memcmp(
            candidate.versions,
            versions,
            sizeof(candidate.versions)) != 0) {
        return false;
    }
    journal->poisoned = 0u;
    return true;
}

void
PalEventJournal_Init(
    PalEventJournal *journal,
    const PalEventJournalIo *io,
    uint8_t sector_scratch[PAL_EVENT_JOURNAL_COMMIT_BYTES])
{
    if (journal == NULL) {
        return;
    }
    memset(journal, 0, sizeof(*journal));
    if (io != NULL) {
        journal->io = *io;
    }
    journal->sector_scratch = sector_scratch;
}

static bool
open_journal(
    PalEventJournal *journal,
    uint32_t expected_profile_id,
    uint32_t expected_template_crc32,
    bool require_identity,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES])
{
    PalEventJournalCandidate candidates[PAL_EVENT_JOURNAL_COMMIT_COUNT];
    int first;
    int second;

    if (journal == NULL || journal->sector_scratch == NULL ||
        page_scratch == NULL || journal->io.read_at == NULL ||
        journal->io.write_at == NULL || journal->io.sync == NULL ||
        journal->poisoned) {
        return false;
    }
    journal->ready = 0u;
    journal->io_error = 0u;
    (void)parse_commit(
        journal, 0u, expected_profile_id, expected_template_crc32,
        require_identity,
        &candidates[0]);
    (void)parse_commit(
        journal, 1u, expected_profile_id, expected_template_crc32,
        require_identity,
        &candidates[1]);
    if (journal->io_error) {
        return false;
    }

    first = candidates[1].valid &&
        (!candidates[0].valid ||
         candidates[1].generation > candidates[0].generation)
        ? 1 : 0;
    second = 1 - first;
    if (candidates[0].valid && candidates[1].valid &&
        candidates[0].generation == candidates[1].generation &&
        (candidates[0].profile_id != candidates[1].profile_id ||
         candidates[0].template_crc32 != candidates[1].template_crc32 ||
         memcmp(
             candidates[0].versions,
             candidates[1].versions,
             sizeof(candidates[0].versions)) != 0)) {
        return false;
    }
    if (candidates[first].valid &&
        validate_candidate_pages(
            journal, &candidates[first], page_scratch)) {
        if (candidates[second].valid &&
            candidates[second].generation >
                candidates[first].generation) {
            journal->metrics.recovery_fallback_count++;
        }
    } else if (!journal->io_error && candidates[second].valid &&
        validate_candidate_pages(
            journal, &candidates[second], page_scratch)) {
        first = second;
        journal->metrics.recovery_fallback_count++;
    } else {
        return false;
    }
    if (journal->io_error) {
        return false;
    }

    memcpy(
        journal->page_versions,
        candidates[first].versions,
        sizeof(journal->page_versions));
    journal->generation = candidates[first].generation;
    journal->profile_id = candidates[first].profile_id;
    journal->template_crc32 = candidates[first].template_crc32;
    journal->sequence = candidates[first].sequence;
    journal->active_commit = candidates[first].commit_slot;
    journal->ready = 1u;
    return true;
}

bool
PalEventJournal_Open(
    PalEventJournal *journal,
    uint32_t expected_profile_id,
    uint32_t expected_template_crc32,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES])
{
    return open_journal(
        journal,
        expected_profile_id,
        expected_template_crc32,
        true,
        page_scratch);
}

bool
PalEventJournal_OpenAnyIdentity(
    PalEventJournal *journal,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES])
{
    return open_journal(
        journal,
        0u,
        0u,
        false,
        page_scratch);
}

bool
PalEventJournal_RebindIdentity(
    PalEventJournal *journal,
    uint32_t profile_id,
    uint32_t template_crc32,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result)
{
    uint32_t new_versions[PAL_EVENT_JOURNAL_PAGE_COUNT];
    uint32_t expected_page_crc[PAL_EVENT_JOURNAL_PAGE_COUNT];
    uint32_t old_profile;
    uint32_t new_generation;
    uint64_t before_write_bytes;
    uint64_t before_syncs;
    uint64_t start_us;
    uint16_t page;
    uint8_t commit_slot;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (journal == NULL || !journal->ready || journal->poisoned ||
        journal->sector_scratch == NULL || page_scratch == NULL ||
        profile_id == 0u ||
        template_crc32 != journal->template_crc32 ||
        ranges_overlap(
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_COMMIT_BYTES,
            page_scratch,
            PAL_EVENT_PAGER_PAGE_BYTES)) {
        return false;
    }
    if (profile_id == journal->profile_id) {
        return true;
    }
    new_generation = journal->generation + 1u;
    if (new_generation == 0u ||
        new_generation > PAL_EVENT_JOURNAL_GENERATION_MASK) {
        return false;
    }

    old_profile = journal->profile_id;
    before_write_bytes = journal->metrics.write_bytes;
    before_syncs = journal->metrics.sync_calls;
    start_us = now_us(journal);
    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        uint32_t old_version = journal->page_versions[page];
        uint8_t bank = (uint8_t)(1u - version_bank(old_version));

        journal->profile_id = old_profile;
        if (!read_physical_page(
                journal,
                page,
                version_bank(old_version),
                version_generation(old_version),
                page_scratch)) {
            /*
             * The source is part of the active generation.  As with a normal
             * active-page read failure, require a complete reopen/recovery
             * before permitting any further transaction.
             */
            journal->poisoned = 1u;
            return false;
        }
        expected_page_crc[page] = PalEventJournal_Crc32(
            page_scratch, PAL_EVENT_PAGER_PAGE_BYTES);

        journal->profile_id = profile_id;
        if (!write_physical_page(
                journal,
                page,
                bank,
                new_generation,
                page_scratch)) {
            journal->profile_id = old_profile;
            return false;
        }
        new_versions[page] = make_version(bank, new_generation);
    }

    journal->profile_id = profile_id;
    if (!io_sync(journal)) {
        journal->profile_id = old_profile;
        return false;
    }
    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        uint32_t version = new_versions[page];

        if (!read_physical_page(
                journal,
                page,
                version_bank(version),
                version_generation(version),
                page_scratch) ||
            PalEventJournal_Crc32(
                page_scratch, PAL_EVENT_PAGER_PAGE_BYTES) !=
                    expected_page_crc[page]) {
            journal->profile_id = old_profile;
            return false;
        }
    }

    commit_slot = (uint8_t)(1u - journal->active_commit);
    if (!write_commit_and_verify(
            journal, commit_slot, new_generation, new_versions)) {
        journal->profile_id = old_profile;
        return false;
    }
    memcpy(
        journal->page_versions,
        new_versions,
        sizeof(journal->page_versions));
    journal->generation = new_generation;
    journal->active_commit = commit_slot;
    journal->metrics.transaction_count++;
    journal->metrics.page_write_count +=
        PAL_EVENT_JOURNAL_PAGE_COUNT;
    journal->metrics.replacement_count++;
    journal->metrics.write_elapsed_us += now_us(journal) - start_us;
    if (result != NULL) {
        result->elapsed_us = now_us(journal) - start_us;
        result->storage_bytes =
            journal->metrics.write_bytes - before_write_bytes;
        result->sync_count = (uint32_t)(
            journal->metrics.sync_calls - before_syncs);
    }
    return true;
}

static bool
invalidate_commits(
    PalEventJournal *journal)
{
    memset(
        journal->sector_scratch,
        0,
        PAL_EVENT_JOURNAL_COMMIT_BYTES);
    return io_write(
            journal,
            0u,
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_COMMIT_BYTES) &&
        io_write(
            journal,
            PAL_EVENT_JOURNAL_COMMIT_BYTES,
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_COMMIT_BYTES) &&
        io_sync(journal);
}

static bool
replace_all(
    PalEventJournal *journal,
    uint32_t profile_id,
    uint32_t template_crc32,
    PalEventJournalPageSource source,
    void *source_user,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result,
    bool destructive_format)
{
    uint32_t new_versions[PAL_EVENT_JOURNAL_PAGE_COUNT];
    uint32_t expected_page_crc[PAL_EVENT_JOURNAL_PAGE_COUNT];
    uint32_t old_profile;
    uint32_t old_template_crc;
    uint32_t new_generation;
    uint64_t before_write_bytes;
    uint64_t before_syncs;
    uint64_t start_us;
    uint16_t page;
    uint8_t commit_slot;
    bool had_ready;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (journal == NULL || journal->sector_scratch == NULL ||
        page_scratch == NULL || source == NULL || profile_id == 0u ||
        journal->poisoned ||
        ranges_overlap(
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_COMMIT_BYTES,
            page_scratch,
            PAL_EVENT_PAGER_PAGE_BYTES)) {
        return false;
    }
    had_ready = journal->ready != 0u;
    if ((!had_ready && !destructive_format) ||
        (had_ready && destructive_format)) {
        return false;
    }
    old_profile = journal->profile_id;
    old_template_crc = journal->template_crc32;
    new_generation = had_ready ? journal->generation + 1u : 1u;
    if (new_generation == 0u ||
        new_generation > PAL_EVENT_JOURNAL_GENERATION_MASK) {
        return false;
    }
    before_write_bytes = journal->metrics.write_bytes;
    before_syncs = journal->metrics.sync_calls;
    start_us = now_us(journal);

    if (!had_ready) {
        uint8_t last_byte = 0u;

        if (!invalidate_commits(journal) ||
            !io_write(
                journal,
                PAL_EVENT_JOURNAL_FILE_BYTES - 1u,
                &last_byte,
                1u)) {
            return false;
        }
    }

    journal->profile_id = profile_id;
    journal->template_crc32 = template_crc32;
    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        uint8_t bank = had_ready
            ? (uint8_t)(1u - version_bank(journal->page_versions[page]))
            : 0u;

        if (!source(source_user, page, page_scratch)) {
            journal->profile_id = old_profile;
            journal->template_crc32 = old_template_crc;
            return false;
        }
        expected_page_crc[page] =
            padded_page_crc(page, page_scratch);
        if (!write_physical_page(
                journal,
                page,
                bank,
                new_generation,
                page_scratch)) {
            journal->profile_id = old_profile;
            journal->template_crc32 = old_template_crc;
            return false;
        }
        new_versions[page] = make_version(bank, new_generation);
    }
    if (!io_sync(journal)) {
        journal->profile_id = old_profile;
        journal->template_crc32 = old_template_crc;
        return false;
    }
    for (page = 0; page < PAL_EVENT_JOURNAL_PAGE_COUNT; page++) {
        uint32_t version = new_versions[page];

        if (!read_physical_page(
                journal,
                page,
                version_bank(version),
                version_generation(version),
                page_scratch) ||
            PalEventJournal_Crc32(
                page_scratch, PAL_EVENT_PAGER_PAGE_BYTES) !=
                    expected_page_crc[page]) {
            journal->profile_id = old_profile;
            journal->template_crc32 = old_template_crc;
            return false;
        }
    }

    commit_slot = had_ready
        ? (uint8_t)(1u - journal->active_commit)
        : 0u;
    if (!write_commit_and_verify(
            journal, commit_slot, new_generation, new_versions)) {
        journal->profile_id = old_profile;
        journal->template_crc32 = old_template_crc;
        return false;
    }
    memcpy(
        journal->page_versions,
        new_versions,
        sizeof(journal->page_versions));
    journal->generation = new_generation;
    journal->active_commit = commit_slot;
    journal->ready = 1u;
    journal->metrics.transaction_count++;
    journal->metrics.page_write_count +=
        PAL_EVENT_JOURNAL_PAGE_COUNT;
    journal->metrics.replacement_count++;
    journal->metrics.write_elapsed_us += now_us(journal) - start_us;
    if (result != NULL) {
        result->elapsed_us = now_us(journal) - start_us;
        result->storage_bytes =
            journal->metrics.write_bytes - before_write_bytes;
        result->sync_count = (uint32_t)(
            journal->metrics.sync_calls - before_syncs);
    }
    return true;
}

bool
PalEventJournal_ReplaceAll(
    PalEventJournal *journal,
    uint32_t profile_id,
    uint32_t template_crc32,
    PalEventJournalPageSource source,
    void *source_user,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result)
{
    return replace_all(
        journal,
        profile_id,
        template_crc32,
        source,
        source_user,
        page_scratch,
        result,
        false);
}

bool
PalEventJournal_FormatAll(
    PalEventJournal *journal,
    uint32_t profile_id,
    uint32_t template_crc32,
    PalEventJournalPageSource source,
    void *source_user,
    uint8_t page_scratch[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result)
{
    return replace_all(
        journal,
        profile_id,
        template_crc32,
        source,
        source_user,
        page_scratch,
        result,
        true);
}

bool
PalEventJournal_ReadPage(
    PalEventJournal *journal,
    uint16_t logical_page,
    uint8_t page[PAL_EVENT_PAGER_PAGE_BYTES],
    PalEventPagerIoResult *result)
{
    uint32_t version;
    uint64_t before_read_bytes;
    uint64_t start_us;
    bool ok;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    if (journal == NULL || !journal->ready || journal->poisoned ||
        page == NULL || logical_page >= PAL_EVENT_JOURNAL_PAGE_COUNT) {
        return false;
    }
    if (ranges_overlap(
            journal->sector_scratch,
            PAL_EVENT_JOURNAL_COMMIT_BYTES,
            page,
            PAL_EVENT_PAGER_PAGE_BYTES)) {
        return false;
    }
    version = journal->page_versions[logical_page];
    before_read_bytes = journal->metrics.read_bytes;
    start_us = now_us(journal);
    ok = read_physical_page(
        journal,
        logical_page,
        version_bank(version),
        version_generation(version),
        page);
    if (!ok) {
        /*
         * Do not let a later transaction publish a directory that keeps
         * referencing a known-unreadable active page and overwrites the last
         * recoverable commit.  Recovery must re-open and validate all pages.
         */
        journal->poisoned = 1u;
    }
    journal->metrics.read_elapsed_us += now_us(journal) - start_us;
    if (result != NULL) {
        result->elapsed_us = now_us(journal) - start_us;
        result->storage_bytes =
            journal->metrics.read_bytes - before_read_bytes;
    }
    return ok;
}

bool
PalEventJournal_WritePages(
    PalEventJournal *journal,
    const PalEventPagerPageWrite *pages,
    uint8_t page_count,
    PalEventPagerWriteReason reason,
    PalEventPagerIoResult *result)
{
    uint32_t new_versions[PAL_EVENT_JOURNAL_PAGE_COUNT];
    uint32_t new_generation;
    uint64_t before_write_bytes;
    uint64_t before_syncs;
    uint64_t start_us;
    uint8_t commit_slot;
    uint8_t i;

    if (result != NULL) {
        memset(result, 0, sizeof(*result));
    }
    (void)reason;
    if (journal == NULL || !journal->ready || journal->poisoned ||
        pages == NULL || page_count == 0u ||
        page_count > PAL_EVENT_JOURNAL_MAX_BATCH) {
        return false;
    }
    new_generation = journal->generation + 1u;
    if (new_generation == 0u ||
        new_generation > PAL_EVENT_JOURNAL_GENERATION_MASK) {
        return false;
    }
    memcpy(
        new_versions,
        journal->page_versions,
        sizeof(new_versions));
    for (i = 0; i < page_count; i++) {
        uint8_t other;

        if (pages[i].page >= PAL_EVENT_JOURNAL_PAGE_COUNT ||
            pages[i].data == NULL ||
            ranges_overlap(
                journal->sector_scratch,
                PAL_EVENT_JOURNAL_COMMIT_BYTES,
                pages[i].data,
                logical_page_bytes(pages[i].page))) {
            return false;
        }
        for (other = 0; other < i; other++) {
            if (pages[other].page == pages[i].page) {
                return false;
            }
        }
    }

    before_write_bytes = journal->metrics.write_bytes;
    before_syncs = journal->metrics.sync_calls;
    start_us = now_us(journal);
    for (i = 0; i < page_count; i++) {
        uint16_t page = pages[i].page;
        uint8_t bank =
            (uint8_t)(1u - version_bank(journal->page_versions[page]));

        if (!write_physical_page(
                journal,
                page,
                bank,
                new_generation,
                pages[i].data)) {
            return false;
        }
        new_versions[page] = make_version(bank, new_generation);
    }
    if (!io_sync(journal)) {
        return false;
    }
    /*
     * Verify every staged page before publishing its bank in the commit
     * directory.  Reuse the caller's const page only as the expected CRC;
     * the physical verification reads into no extra 4 KiB scratch by first
     * checking the header and then comparing the payload in 512-byte slices.
     */
    for (i = 0; i < page_count; i++) {
        uint16_t page = pages[i].page;
        uint32_t version = new_versions[page];
        uint32_t offset = slot_offset(page, version_bank(version));
        uint32_t payload_crc;
        uint32_t running_crc = 0xffffffffu;
        uint32_t chunk_offset;

        if (!io_read(
                journal,
                offset,
                journal->sector_scratch,
                PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES) ||
            !validate_page_header(
                journal,
                page,
                version_bank(version),
                new_generation,
                journal->sector_scratch)) {
            return false;
        }
        payload_crc = read_le32(journal->sector_scratch + 28u);
        for (chunk_offset = 0;
             chunk_offset < PAL_EVENT_PAGER_PAGE_BYTES;
             chunk_offset += PAL_EVENT_JOURNAL_COMMIT_BYTES) {
            uint16_t byte;

            if (!io_read(
                    journal,
                    offset + PAL_EVENT_JOURNAL_SLOT_HEADER_BYTES +
                        chunk_offset,
                    journal->sector_scratch,
                    PAL_EVENT_JOURNAL_COMMIT_BYTES)) {
                return false;
            }
            for (byte = 0;
                 byte < PAL_EVENT_JOURNAL_COMMIT_BYTES;
                 byte++) {
                uint32_t value =
                    running_crc ^ journal->sector_scratch[byte];
                uint8_t bit;

                for (bit = 0; bit < 8u; bit++) {
                    value = (value >> 1) ^
                        (0xedb88320u &
                         (uint32_t)-(int32_t)(value & 1u));
                }
                running_crc = value;
            }
        }
        if ((running_crc ^ 0xffffffffu) != payload_crc ||
            padded_page_crc(page, pages[i].data) != payload_crc) {
            return false;
        }
    }

    commit_slot = (uint8_t)(1u - journal->active_commit);
    if (!write_commit_and_verify(
            journal, commit_slot, new_generation, new_versions)) {
        return false;
    }
    memcpy(
        journal->page_versions,
        new_versions,
        sizeof(journal->page_versions));
    journal->generation = new_generation;
    journal->active_commit = commit_slot;
    journal->metrics.transaction_count++;
    journal->metrics.page_write_count += page_count;
    journal->metrics.write_elapsed_us += now_us(journal) - start_us;
    if (result != NULL) {
        result->elapsed_us = now_us(journal) - start_us;
        result->storage_bytes =
            journal->metrics.write_bytes - before_write_bytes;
        result->sync_count = (uint32_t)(
            journal->metrics.sync_calls - before_syncs);
    }
    return true;
}

const PalEventJournalMetrics *
PalEventJournal_GetMetrics(
    const PalEventJournal *journal)
{
    return journal != NULL ? &journal->metrics : NULL;
}
