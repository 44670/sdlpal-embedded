#ifndef PAL_SCRIPT_STATIC_H
#define PAL_SCRIPT_STATIC_H

#include "pal_global_cache.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_SCRIPT_ENTRY_BYTES 8u

typedef struct PalScriptEntry {
    uint16_t operation;
    uint16_t operand[3];
} PalScriptEntry;

typedef struct PalScriptView {
    const uint8_t *data;
    uint32_t size;
    uint32_t count;
} PalScriptView;

typedef struct PalScriptTrace {
    uint16_t start_entry;
    uint16_t step_count;
    uint16_t first_operation;
    uint16_t last_entry;
    uint16_t terminator_entry;
    bool terminated;
} PalScriptTrace;

bool PalScript_OpenFromGlobal(const PalGlobalCache *global_cache, PalScriptView *view);
bool PalScript_Read(const PalScriptView *view, uint16_t entry_num, PalScriptEntry *entry);
bool PalScript_TraceLinear(const PalScriptView *view, uint16_t start_entry, uint16_t max_steps, PalScriptTrace *trace);

#ifdef __cplusplus
}
#endif

#endif
