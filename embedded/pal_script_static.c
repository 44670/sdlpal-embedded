#include "pal_script_static.h"

#include <stddef.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

bool PalScript_OpenFromGlobal(const PalGlobalCache *global_cache, PalScriptView *view)
{
    if (view == NULL) {
        return false;
    }
    memset(view, 0, sizeof(*view));

    if (global_cache == NULL ||
        global_cache->script_entries.data == NULL ||
        global_cache->script_entries.size == 0 ||
        (global_cache->script_entries.size % PAL_SCRIPT_ENTRY_BYTES) != 0) {
        return false;
    }

    view->data = global_cache->script_entries.data;
    view->size = global_cache->script_entries.size;
    view->count = global_cache->script_entries.size / PAL_SCRIPT_ENTRY_BYTES;
    return view->count == global_cache->script_entries.count;
}

bool PalScript_Read(const PalScriptView *view, uint16_t entry_num, PalScriptEntry *entry)
{
    uint32_t offset;
    const uint8_t *src;

    if (view == NULL || entry == NULL || view->data == NULL || entry_num >= view->count) {
        return false;
    }
    offset = (uint32_t)entry_num * PAL_SCRIPT_ENTRY_BYTES;
    if (offset > view->size || PAL_SCRIPT_ENTRY_BYTES > view->size - offset) {
        return false;
    }

    src = view->data + offset;
    entry->operation = read_le16(src);
    entry->operand[0] = read_le16(src + 2u);
    entry->operand[1] = read_le16(src + 4u);
    entry->operand[2] = read_le16(src + 6u);
    return true;
}

bool PalScript_TraceLinear(const PalScriptView *view, uint16_t start_entry, uint16_t max_steps, PalScriptTrace *trace)
{
    uint16_t i;

    if (trace == NULL || max_steps == 0) {
        return false;
    }
    memset(trace, 0, sizeof(*trace));
    trace->start_entry = start_entry;
    trace->last_entry = start_entry;

    for (i = 0; i < max_steps; i++) {
        PalScriptEntry entry;
        uint16_t entry_num = (uint16_t)(start_entry + i);

        if (entry_num < start_entry || !PalScript_Read(view, entry_num, &entry)) {
            return i != 0;
        }
        if (i == 0) {
            trace->first_operation = entry.operation;
        }
        trace->last_entry = entry_num;
        trace->step_count = (uint16_t)(i + 1u);
        if (entry.operation == 0u) {
            trace->terminator_entry = entry_num;
            trace->terminated = true;
            return true;
        }
    }

    return true;
}
