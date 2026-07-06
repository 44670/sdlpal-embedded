#include "pal_global_cache.h"

#include "pal_memory.h"

#include <string.h>

#define SSS_EVENT_OBJECT_CHUNK 0u
#define SSS_SCENE_CHUNK 1u
#define SSS_OBJECT_CHUNK 2u
#define SSS_SCRIPT_CHUNK 4u

#define DATA_STORE_CHUNK 0u
#define DATA_ENEMY_CHUNK 1u
#define DATA_ENEMY_TEAM_CHUNK 2u
#define DATA_PLAYER_ROLES_CHUNK 3u
#define DATA_MAGIC_CHUNK 4u
#define DATA_BATTLEFIELD_CHUNK 5u
#define DATA_LEVELUP_MAGIC_CHUNK 6u
#define DATA_BATTLE_EFFECT_INDEX_CHUNK 11u
#define DATA_ENEMY_POSITION_CHUNK 13u
#define DATA_LEVELUP_EXP_CHUNK 14u

static PalGlobalCache pal_global_cache;

static uint32_t align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}

static bool map_const_records(
    const PalPack *pack,
    uint16_t archive_id,
    uint16_t chunk_id,
    uint32_t record_size,
    PalGlobalConstSlice *slice)
{
    PalPackSpan span;

    if (slice == 0 || record_size == 0) {
        return false;
    }
    if (!PalPack_MapConst(pack, archive_id, chunk_id, &span)) {
        return false;
    }
    if (span.data == 0 || span.format != PAL_PACK_FORMAT_NATIVE || (span.size % record_size) != 0) {
        return false;
    }

    slice->data = span.data;
    slice->size = span.size;
    slice->count = span.size / record_size;
    return true;
}

static bool copy_mutable_records(
    const PalPack *pack,
    uint16_t archive_id,
    uint16_t chunk_id,
    uint32_t record_size,
    uint32_t *cursor,
    PalGlobalMutableSlice *slice)
{
    PalPackSpan span;
    uint32_t offset;

    if (cursor == 0 || slice == 0 || record_size == 0) {
        return false;
    }
    if (!PalPack_MapConst(pack, archive_id, chunk_id, &span)) {
        return false;
    }
    if (span.data == 0 || span.format != PAL_PACK_FORMAT_NATIVE || (span.size % record_size) != 0) {
        return false;
    }

    offset = align4(*cursor);
    if (offset > PAL_PSRAM_SAVE_STATE_BYTES || span.size > PAL_PSRAM_SAVE_STATE_BYTES - offset) {
        return false;
    }
    if (span.size != 0) {
        memcpy(pal_psram_save_state + offset, span.data, span.size);
    }

    slice->data = pal_psram_save_state + offset;
    slice->size = span.size;
    slice->count = span.size / record_size;
    *cursor = offset + span.size;
    return true;
}

static bool map_readonly_records(const PalPack *nor_pack)
{
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_SCRIPT_CHUNK, PAL_GLOBAL_SCRIPT_ENTRY_BYTES, &pal_global_cache.script_entries)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_STORE_CHUNK, PAL_GLOBAL_STORE_BYTES, &pal_global_cache.stores)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_ENEMY_CHUNK, PAL_GLOBAL_ENEMY_BYTES, &pal_global_cache.enemies)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_ENEMY_TEAM_CHUNK, PAL_GLOBAL_ENEMY_TEAM_BYTES, &pal_global_cache.enemy_teams)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_MAGIC_CHUNK, PAL_GLOBAL_MAGIC_BYTES, &pal_global_cache.magics)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_BATTLEFIELD_CHUNK, PAL_GLOBAL_BATTLEFIELD_BYTES, &pal_global_cache.battlefields)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_LEVELUP_MAGIC_CHUNK, PAL_GLOBAL_LEVELUP_MAGIC_ALL_BYTES, &pal_global_cache.levelup_magics)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_BATTLE_EFFECT_INDEX_CHUNK, 4u, &pal_global_cache.battle_effect_index)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_ENEMY_POSITION_CHUNK, 100u, &pal_global_cache.enemy_positions)) {
        return false;
    }
    if (!map_const_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_LEVELUP_EXP_CHUNK, 2u, &pal_global_cache.levelup_exp)) {
        return false;
    }
    return true;
}

bool PalGlobal_LoadDefault(const PalPack *nor_pack, const PalGlobalCache **cache)
{
    uint32_t cursor = 0;

    if (cache == 0) {
        return false;
    }
    *cache = 0;

    memset(&pal_global_cache, 0, sizeof(pal_global_cache));
    if (!copy_mutable_records(nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_EVENT_OBJECT_CHUNK, PAL_GLOBAL_EVENT_OBJECT_BYTES, &cursor, &pal_global_cache.event_objects)) {
        return false;
    }
    if (!copy_mutable_records(nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_SCENE_CHUNK, PAL_GLOBAL_SCENE_BYTES, &cursor, &pal_global_cache.scenes)) {
        return false;
    }
    if (!copy_mutable_records(nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_OBJECT_CHUNK, PAL_GLOBAL_OBJECT_DOS_BYTES, &cursor, &pal_global_cache.objects_dos)) {
        return false;
    }
    if (!copy_mutable_records(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_PLAYER_ROLES_CHUNK, PAL_GLOBAL_PLAYER_ROLES_BYTES, &cursor, &pal_global_cache.player_roles)) {
        return false;
    }

    if (!map_readonly_records(nor_pack)) {
        return false;
    }

    pal_global_cache.mutable_bytes = cursor;
    *cache = &pal_global_cache;
    return true;
}

bool PalGlobal_LoadReadonly(const PalPack *nor_pack, const PalGlobalCache **cache)
{
    if (cache == 0) {
        return false;
    }
    *cache = 0;

    memset(&pal_global_cache, 0, sizeof(pal_global_cache));
    if (!map_readonly_records(nor_pack)) {
        return false;
    }

    *cache = &pal_global_cache;
    return true;
}
