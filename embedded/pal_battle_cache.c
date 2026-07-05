#include "pal_battle_cache.h"

#include "pal_memory.h"

#include <stddef.h>

#define DATA_ENEMY_TEAM_CHUNK 2u
#define SSS_OBJECT_CHUNK 2u
#define ENEMY_TEAM_BYTES 10u
#define OBJECT_DOS_BYTES 12u
#define ENEMY_OBJECT_SENTINEL 0xffffu

static PalBattleSpriteRef pal_battle_player_sprites[PAL_BATTLE_MAX_PLAYERS];
static PalBattleSpriteRef pal_battle_enemy_sprites[PAL_BATTLE_MAX_ENEMIES];
static uint16_t pal_battle_unique_enemy_nums[PAL_BATTLE_MAX_UNIQUE_ENEMY_SPRITES];
static const uint8_t *pal_battle_unique_enemy_data[PAL_BATTLE_MAX_UNIQUE_ENEMY_SPRITES];
static uint32_t pal_battle_unique_enemy_sizes[PAL_BATTLE_MAX_UNIQUE_ENEMY_SPRITES];

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int find_unique_enemy(uint16_t unique_count, uint16_t enemy_sprite_num)
{
    uint16_t i;

    for (i = 0; i < unique_count; i++) {
        if (pal_battle_unique_enemy_nums[i] == enemy_sprite_num) {
            return (int)i;
        }
    }
    return -1;
}

static bool load_player_sprites(
    const PalPack *nor_pack,
    const uint16_t *player_sprite_nums,
    uint16_t player_count,
    uint32_t *player_sprite_bytes)
{
    uint16_t i;
    uint32_t total = 0;

    if (player_count > PAL_BATTLE_MAX_PLAYERS || player_sprite_nums == NULL || player_sprite_bytes == NULL) {
        return false;
    }

    for (i = 0; i < player_count; i++) {
        PalPackSpan span;
        uint16_t sprite_num = player_sprite_nums[i];

        if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_F, sprite_num, &span)) {
            return false;
        }
        if (span.data == NULL || span.size == 0 || span.format != PAL_PACK_FORMAT_NATIVE) {
            return false;
        }
        pal_battle_player_sprites[i].chunk_num = sprite_num;
        pal_battle_player_sprites[i].unique_index = i;
        pal_battle_player_sprites[i].data = span.data;
        pal_battle_player_sprites[i].size = span.size;
        total += span.size;
    }

    *player_sprite_bytes = total;
    return true;
}

static bool load_enemy_sprites(
    const PalPack *nor_pack,
    uint16_t team_num,
    uint16_t *enemy_ref_count,
    uint16_t *unique_enemy_sprite_count,
    uint32_t *unique_enemy_sprite_bytes)
{
    PalPackSpan team_span;
    PalPackSpan object_span;
    const uint8_t *team;
    uint16_t i;
    uint16_t refs = 0;
    uint16_t unique_count = 0;
    uint32_t unique_bytes = 0;

    if (enemy_ref_count == NULL || unique_enemy_sprite_count == NULL || unique_enemy_sprite_bytes == NULL) {
        return false;
    }
    if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_DATA, DATA_ENEMY_TEAM_CHUNK, &team_span)) {
        return false;
    }
    if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_OBJECT_CHUNK, &object_span)) {
        return false;
    }
    if (team_span.size < (uint32_t)(team_num + 1u) * ENEMY_TEAM_BYTES) {
        return false;
    }

    team = team_span.data + (uint32_t)team_num * ENEMY_TEAM_BYTES;
    for (i = 0; i < PAL_BATTLE_MAX_ENEMIES; i++) {
        uint16_t object_id = read_le16(team + (uint32_t)i * 2u);
        uint16_t enemy_sprite_num;
        int unique_index;

        pal_battle_enemy_sprites[i].chunk_num = 0;
        pal_battle_enemy_sprites[i].unique_index = 0xffffu;
        pal_battle_enemy_sprites[i].data = NULL;
        pal_battle_enemy_sprites[i].size = 0;

        if (object_id == 0 || object_id == ENEMY_OBJECT_SENTINEL) {
            continue;
        }
        if (object_span.size < (uint32_t)(object_id + 1u) * OBJECT_DOS_BYTES) {
            return false;
        }

        enemy_sprite_num = read_le16(object_span.data + (uint32_t)object_id * OBJECT_DOS_BYTES);
        unique_index = find_unique_enemy(unique_count, enemy_sprite_num);
        if (unique_index < 0) {
            PalPackSpan enemy_span;

            if (unique_count >= PAL_BATTLE_MAX_UNIQUE_ENEMY_SPRITES) {
                return false;
            }
            if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_ABC, enemy_sprite_num, &enemy_span)) {
                return false;
            }
            if (enemy_span.data == NULL || enemy_span.size == 0 || enemy_span.format != PAL_PACK_FORMAT_NATIVE) {
                return false;
            }

            unique_index = (int)unique_count;
            pal_battle_unique_enemy_nums[unique_count] = enemy_sprite_num;
            pal_battle_unique_enemy_data[unique_count] = enemy_span.data;
            pal_battle_unique_enemy_sizes[unique_count] = enemy_span.size;
            unique_bytes += enemy_span.size;
            unique_count++;
        }

        pal_battle_enemy_sprites[i].chunk_num = enemy_sprite_num;
        pal_battle_enemy_sprites[i].unique_index = (uint16_t)unique_index;
        pal_battle_enemy_sprites[i].data = pal_battle_unique_enemy_data[unique_index];
        pal_battle_enemy_sprites[i].size = pal_battle_unique_enemy_sizes[unique_index];
        refs++;
    }

    *enemy_ref_count = refs;
    *unique_enemy_sprite_count = unique_count;
    *unique_enemy_sprite_bytes = unique_bytes;
    return true;
}

bool PalBattle_LoadSnapshot(
    const PalPack *nor_pack,
    const PalPack *tf_pack,
    uint16_t team_num,
    uint16_t battlefield_num,
    const uint16_t *player_sprite_nums,
    uint16_t player_count,
    uint16_t effect_num,
    PalBattleSnapshot *snapshot)
{
    uint32_t copied = 0;
    uint32_t player_sprite_bytes = 0;
    uint16_t enemy_ref_count = 0;
    uint16_t unique_enemy_count = 0;
    uint32_t unique_enemy_bytes = 0;
    PalPackSpan effect_span;

    if (snapshot == NULL) {
        return false;
    }
    if (!PalPack_CopyRaw(tf_pack, PAL_PACK_ARCHIVE_FBP, battlefield_num, pal_psram_fbp_background, PAL_PSRAM_FBP_BACKGROUND_BYTES, &copied)) {
        return false;
    }
    if (copied != PAL_PSRAM_FBP_BACKGROUND_BYTES) {
        return false;
    }
    if (!load_player_sprites(nor_pack, player_sprite_nums, player_count, &player_sprite_bytes)) {
        return false;
    }
    if (!load_enemy_sprites(nor_pack, team_num, &enemy_ref_count, &unique_enemy_count, &unique_enemy_bytes)) {
        return false;
    }
    if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_FIRE, effect_num, &effect_span)) {
        return false;
    }
    if (effect_span.data == NULL || effect_span.size == 0 || effect_span.format != PAL_PACK_FORMAT_NATIVE) {
        return false;
    }

    snapshot->team_num = team_num;
    snapshot->battlefield_num = battlefield_num;
    snapshot->player_count = player_count;
    snapshot->enemy_ref_count = enemy_ref_count;
    snapshot->unique_enemy_sprite_count = unique_enemy_count;
    snapshot->effect_num = effect_num;
    snapshot->background_size = copied;
    snapshot->player_sprite_bytes = player_sprite_bytes;
    snapshot->unique_enemy_sprite_bytes = unique_enemy_bytes;
    snapshot->effect_size = effect_span.size;
    snapshot->player_sprites = pal_battle_player_sprites;
    snapshot->enemy_sprites = pal_battle_enemy_sprites;
    snapshot->effect_data = effect_span.data;
    return true;
}
