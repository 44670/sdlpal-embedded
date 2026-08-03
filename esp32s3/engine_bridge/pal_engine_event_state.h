#ifndef PAL_ENGINE_EVENT_STATE_H
#define PAL_ENGINE_EVENT_STATE_H

#include "../../embedded/pal_event_pager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PalEngineEventStateIoMetrics {
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_calls;
    uint64_t write_calls;
    uint64_t read_failures;
    uint64_t write_failures;
} PalEngineEventStateIoMetrics;

/* Initialize SRAM ownership only.  Startup deliberately ignores EVENT.WRK. */
bool PalEngineEventState_Init(uint16_t record_count);
void PalEngineEventState_Shutdown(void);
bool PalEngineEventState_IsReady(void);
uint16_t PalEngineEventState_GetRecordCount(void);
uint32_t PalEngineEventState_GetEventBytes(void);

/*
 * Start a new session from the stock SSS event table.  Every work-file byte
 * is overwritten, so a stale file from an earlier boot is neither recovered
 * nor explicitly deleted/truncated.
 */
bool PalEngineEventState_ResetDefaults(
    const void *events,
    uint32_t event_bytes);

/* Replace the session from the standard .rpg event payload at the cursor. */
bool PalEngineEventState_ReplaceFromFile(FILE *source);

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

bool PalEngineEventState_HasOutstandingHandles(void);

const PalEventPagerMetrics *PalEngineEventState_GetPagerMetrics(void);
const PalEngineEventStateIoMetrics *PalEngineEventState_GetIoMetrics(void);

#ifdef __cplusplus
}
#endif

#endif
