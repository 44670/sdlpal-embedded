#ifndef PAL_BATTLE_CACHE_H
#define PAL_BATTLE_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_BATTLE_MAX_PLAYERS 6u
#define PAL_BATTLE_MAX_ENEMIES 5u
#define PAL_BATTLE_MAX_UNIQUE_ENEMY_SPRITES 5u

typedef struct PalBattleSpriteRef {
    uint16_t chunk_num;
    uint16_t unique_index;
    const uint8_t *data;
    uint32_t size;
} PalBattleSpriteRef;

typedef struct PalBattleSnapshot {
    uint16_t team_num;
    uint16_t battlefield_num;
    uint16_t player_count;
    uint16_t enemy_ref_count;
    uint16_t unique_enemy_sprite_count;
    uint16_t effect_num;
    uint32_t background_size;
    uint32_t player_sprite_bytes;
    uint32_t unique_enemy_sprite_bytes;
    uint32_t effect_size;
    uint32_t battle_effect_size;
    const PalBattleSpriteRef *player_sprites;
    const PalBattleSpriteRef *enemy_sprites;
    const uint8_t *effect_data;
    const uint8_t *battle_effect_data;
} PalBattleSnapshot;

bool PalBattle_LoadSnapshot(
    const PalPack *nor_pack,
    const PalPack *tf_pack,
    uint16_t team_num,
    uint16_t battlefield_num,
    const uint16_t *player_sprite_nums,
    uint16_t player_count,
    uint16_t effect_num,
    PalBattleSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
