#ifndef PAL_GLOBAL_CACHE_H
#define PAL_GLOBAL_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_GLOBAL_EVENT_OBJECT_BYTES 32u
#define PAL_GLOBAL_SCENE_BYTES 8u
#define PAL_GLOBAL_OBJECT_DOS_BYTES 12u
#define PAL_GLOBAL_SCRIPT_ENTRY_BYTES 8u
#define PAL_GLOBAL_STORE_BYTES 18u
#define PAL_GLOBAL_ENEMY_BYTES 70u
#define PAL_GLOBAL_ENEMY_TEAM_BYTES 10u
#define PAL_GLOBAL_PLAYER_ROLES_BYTES 900u
#define PAL_GLOBAL_MAGIC_BYTES 32u
#define PAL_GLOBAL_BATTLEFIELD_BYTES 6u
#define PAL_GLOBAL_LEVELUP_MAGIC_ALL_BYTES 8u

typedef struct PalGlobalMutableSlice {
    uint8_t *data;
    uint32_t size;
    uint32_t count;
} PalGlobalMutableSlice;

typedef struct PalGlobalConstSlice {
    const uint8_t *data;
    uint32_t size;
    uint32_t count;
} PalGlobalConstSlice;

typedef struct PalGlobalCache {
    uint32_t mutable_bytes;
    PalGlobalMutableSlice event_objects;
    PalGlobalMutableSlice scenes;
    PalGlobalMutableSlice objects_dos;
    PalGlobalMutableSlice player_roles;
    PalGlobalConstSlice script_entries;
    PalGlobalConstSlice stores;
    PalGlobalConstSlice enemies;
    PalGlobalConstSlice enemy_teams;
    PalGlobalConstSlice magics;
    PalGlobalConstSlice battlefields;
    PalGlobalConstSlice levelup_magics;
    PalGlobalConstSlice battle_effect_index;
    PalGlobalConstSlice enemy_positions;
    PalGlobalConstSlice levelup_exp;
} PalGlobalCache;

bool PalGlobal_LoadDefault(const PalPack *nor_pack, const PalGlobalCache **cache);
bool PalGlobal_LoadReadonly(const PalPack *nor_pack, const PalGlobalCache **cache);

#ifdef __cplusplus
}
#endif

#endif
