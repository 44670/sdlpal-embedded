#ifndef PAL_ENGINE_EVENT_STATE_H
#define PAL_ENGINE_EVENT_STATE_H

#include "../../embedded/pal_event_journal.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_ENGINE_EVENT_STATE_RECORD_COUNT PAL_EVENT_PAGER_RECORD_COUNT
#define PAL_ENGINE_EVENT_STATE_SCENE_COUNT 300u
#define PAL_ENGINE_EVENT_STATE_SCENE_BYTES \
    PAL_EVENT_JOURNAL_SCENE_BYTES

typedef struct PalEngineEventStateIoMetrics {
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_calls;
    uint64_t write_calls;
    uint64_t sync_calls;
    uint64_t read_failures;
    uint64_t write_failures;
    uint64_t sync_failures;
} PalEngineEventStateIoMetrics;

typedef bool (*PalEngineEventStatePageTransform)(
    void *user,
    uint16_t logical_page,
    uint8_t page[PAL_EVENT_PAGER_PAGE_BYTES]);

bool PalEngineEventState_Init(void);
void PalEngineEventState_Shutdown(void);
bool PalEngineEventState_IsReady(void);

bool PalEngineEventState_GetIdentity(
    uint32_t *profile_id,
    uint32_t *template_crc32,
    uint32_t *generation);

bool PalEngineEventState_ResetDefaults(
    void *scene_table,
    uint32_t scene_table_bytes);

/*
 * Replace the complete event + SCENE generation from a validated snapshot.
 * source is called in ascending logical-page order and must zero-pad the
 * unused tail of event page 41 and SCENE page 42.
 */
bool PalEngineEventState_ReplaceAll(
    PalEventJournalPageSource source,
    void *source_user,
    void *scene_table,
    uint32_t scene_table_bytes);

/*
 * Atomically transform every page of EVENT.DEF.  Each callback receives one
 * validated default page and may modify it in place.  The current journal
 * generation remains recoverable until all transformed pages and the new
 * commit sector are durable.
 */
bool PalEngineEventState_TransformDefaults(
    PalEngineEventStatePageTransform transform,
    void *transform_user,
    void *scene_table,
    uint32_t scene_table_bytes);

bool PalEngineEventState_ReadEvent(
    uint16_t event_id,
    void *record,
    uint32_t record_bytes);

bool PalEngineEventState_WriteEvent(
    uint16_t event_id,
    const void *record,
    uint32_t record_bytes);

bool PalEngineEventState_PinScene(
    uint16_t event_start,
    uint16_t event_count);

bool PalEngineEventState_Flush(
    PalEventPagerWriteReason reason);

/* Flush once the oldest event/SCENE dirty byte reaches the bounded age. */
bool PalEngineEventState_Checkpoint(void);

bool PalEngineEventState_MarkSceneDirty(
    uint16_t scene_index);

bool PalEngineEventState_HasOutstandingHandles(void);

const PalEventPagerMetrics *PalEngineEventState_GetPagerMetrics(void);
const PalEventJournalMetrics *PalEngineEventState_GetJournalMetrics(void);
const PalEngineEventStateIoMetrics *PalEngineEventState_GetIoMetrics(void);

#ifdef __cplusplus
}
#endif

#endif
