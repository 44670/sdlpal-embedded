#ifndef PAL_SCENE_CACHE_H
#define PAL_SCENE_CACHE_H

#include "pal_pack.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef PAL_SCENE_ENABLE_PINNED
#define PAL_SCENE_ENABLE_PINNED 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_SCENE_COUNT 300u
#define PAL_SCENE_MAX_EVENT_OBJECTS 160u
#define PAL_SCENE_MAX_UNIQUE_SPRITES 64u

typedef struct PalSceneSpriteRef {
    uint16_t sprite_num;
    uint16_t unique_index;
    const uint8_t *data;
    uint32_t size;
} PalSceneSpriteRef;

typedef struct PalSceneSnapshot {
    uint16_t scene_num;
    uint16_t map_num;
    uint16_t event_start;
    uint16_t event_count;
    uint16_t sprite_ref_count;
    uint16_t unique_sprite_count;
    uint32_t gop_size;
    uint32_t unique_sprite_bytes;
    uint32_t sprite_pin_bytes;
    const PalSceneSpriteRef *sprite_refs;
} PalSceneSnapshot;

bool PalScene_LoadSnapshot(const PalPack *nor_pack, const PalPack *tf_pack, uint16_t scene_num, PalSceneSnapshot *snapshot);
bool PalScene_LoadSnapshotReadAt(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot);
bool PalScene_LoadSnapshotReadAtWithEvents(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const uint8_t *event_objects,
    uint32_t event_objects_size,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot);
bool PalScene_LoadSnapshotReadAtWithSceneData(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const uint8_t *scene_records,
    uint32_t scene_records_size,
    const uint8_t *event_objects,
    uint32_t event_objects_size,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot);
#if PAL_SCENE_ENABLE_PINNED
bool PalScene_LoadPinnedSnapshot(
    const PalPack *nor_pack,
    const PalPack *tf_pack,
    const PalPack *sprite_pack,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot);
bool PalScene_LoadPinnedSnapshotReadAt(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const PalPack *sprite_pack,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot);
bool PalScene_LoadPinnedSnapshotReadAtWithEvents(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const PalPack *sprite_pack,
    const uint8_t *event_objects,
    uint32_t event_objects_size,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot);
bool PalScene_LoadPinnedSnapshotReadAtWithSceneData(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const PalPack *sprite_pack,
    const uint8_t *scene_records,
    uint32_t scene_records_size,
    const uint8_t *event_objects,
    uint32_t event_objects_size,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot);
#endif

#ifdef __cplusplus
}
#endif

#endif
