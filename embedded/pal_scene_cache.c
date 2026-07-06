#include "pal_scene_cache.h"

#include "pal_memory.h"

#include <stddef.h>
#include <string.h>

#define SSS_EVENT_OBJECT_CHUNK 0u
#define SSS_SCENE_CHUNK 1u
#define SSS_SCENE_BYTES 8u
#define SSS_EVENT_OBJECT_BYTES 32u
#define EVENT_OBJECT_SPRITE_NUM_OFFSET 16u

static PalSceneSpriteRef pal_scene_sprite_refs[PAL_SCENE_MAX_EVENT_OBJECTS];
static uint16_t pal_scene_unique_sprite_nums[PAL_SCENE_MAX_UNIQUE_SPRITES];
static const uint8_t *pal_scene_unique_sprite_data[PAL_SCENE_MAX_UNIQUE_SPRITES];
static uint32_t pal_scene_unique_sprite_sizes[PAL_SCENE_MAX_UNIQUE_SPRITES];

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static bool get_scene_range(
    const PalPack *nor_pack,
    const uint8_t *scene_records,
    uint32_t scene_records_size,
    uint16_t scene_num,
    uint16_t *map_num,
    uint16_t *event_start,
    uint16_t *event_count)
{
    PalPackSpan scene_span;
    const uint8_t *scene;
    const uint8_t *next_scene;
    uint16_t next_event_start;

    if (map_num == NULL || event_start == NULL || event_count == NULL ||
        scene_num == 0 || scene_num >= PAL_SCENE_COUNT) {
        return false;
    }
    if (scene_records != NULL) {
        scene_span.data = scene_records;
        scene_span.size = scene_records_size;
    } else {
        if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_SCENE_CHUNK, &scene_span)) {
            return false;
        }
        if (scene_span.format != PAL_PACK_FORMAT_NATIVE) {
            return false;
        }
    }
    if (scene_span.data == NULL || scene_span.size < (uint32_t)(scene_num + 1u) * SSS_SCENE_BYTES) {
        return false;
    }

    scene = scene_span.data + (uint32_t)(scene_num - 1u) * SSS_SCENE_BYTES;
    next_scene = scene + SSS_SCENE_BYTES;
    *map_num = read_le16(scene);
    *event_start = read_le16(scene + 6);
    next_event_start = read_le16(next_scene + 6);
    if (next_event_start < *event_start) {
        return false;
    }
    *event_count = (uint16_t)(next_event_start - *event_start);
    return *event_count <= PAL_SCENE_MAX_EVENT_OBJECTS;
}

static int find_unique_sprite(uint16_t unique_count, uint16_t sprite_num)
{
    uint16_t i;

    for (i = 0; i < unique_count; i++) {
        if (pal_scene_unique_sprite_nums[i] == sprite_num) {
            return (int)i;
        }
    }
    return -1;
}

#if PAL_SCENE_ENABLE_PINNED
static uint32_t align4(uint32_t value)
{
    return (value + 3u) & ~3u;
}
#endif

static bool copy_tf_chunk(
    const PalPack *tf_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    uint16_t archive_id,
    uint16_t chunk_id,
    uint8_t *dst,
    uint32_t dst_capacity,
    uint32_t *out_size)
{
    if (tf_pack != NULL) {
        return PalPack_CopyRaw(tf_pack, archive_id, chunk_id, dst, dst_capacity, out_size);
    }
    if (tf_toc != NULL && read_at != NULL) {
        return PalPackToc_CopyRawReadAt(tf_toc, read_at, user, archive_id, chunk_id, dst, dst_capacity, out_size);
    }
    return false;
}

static bool load_snapshot(
    const PalPack *nor_pack,
    const PalPack *tf_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const PalPack *sprite_pack,
    const uint8_t *scene_records,
    uint32_t scene_records_size,
    const uint8_t *event_objects,
    uint32_t event_objects_size,
    uint16_t scene_num,
    bool pin_sprites,
    PalSceneSnapshot *snapshot)
{
    PalPackSpan event_span;
    const uint8_t *event_data;
    uint32_t event_data_size;
    uint16_t map_num;
    uint16_t event_start;
    uint16_t event_count;
    uint16_t i;
    uint16_t unique_count = 0;
    uint16_t sprite_ref_count = 0;
    uint32_t copied = 0;
    uint32_t unique_sprite_bytes = 0;
#if PAL_SCENE_ENABLE_PINNED
    uint32_t pin_cursor = 0;
#else
    (void)pin_sprites;
#endif

    if (snapshot == NULL) {
        return false;
    }
    if (!get_scene_range(nor_pack, scene_records, scene_records_size, scene_num, &map_num, &event_start, &event_count)) {
        return false;
    }

    if (event_objects != NULL) {
        event_data = event_objects;
        event_data_size = event_objects_size;
    } else {
        if (!PalPack_MapConst(nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_EVENT_OBJECT_CHUNK, &event_span)) {
            return false;
        }
        event_data = event_span.data;
        event_data_size = event_span.size;
    }
    if (event_data_size < (uint32_t)(event_start + event_count) * SSS_EVENT_OBJECT_BYTES) {
        return false;
    }
    if (!copy_tf_chunk(tf_pack, tf_toc, read_at, user, PAL_PACK_ARCHIVE_MAP, map_num, pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES, &copied)) {
        return false;
    }
    if (copied != PAL_PSRAM_MAP_TILES_BYTES) {
        return false;
    }
    if (!copy_tf_chunk(tf_pack, tf_toc, read_at, user, PAL_PACK_ARCHIVE_GOP, map_num, pal_psram_gop_copy, PAL_PSRAM_GOP_COPY_BYTES, &copied)) {
        return false;
    }
    if (copied == 0 || copied > PAL_PSRAM_GOP_COPY_BYTES) {
        return false;
    }

    for (i = 0; i < event_count; i++) {
        const uint8_t *event_object = event_data + (uint32_t)(event_start + i) * SSS_EVENT_OBJECT_BYTES;
        uint16_t sprite_num = read_le16(event_object + EVENT_OBJECT_SPRITE_NUM_OFFSET);
        int unique_index;

        pal_scene_sprite_refs[i].sprite_num = sprite_num;
        pal_scene_sprite_refs[i].unique_index = 0xffffu;
        pal_scene_sprite_refs[i].data = NULL;
        pal_scene_sprite_refs[i].size = 0;

        if (sprite_num == 0) {
            continue;
        }

        unique_index = find_unique_sprite(unique_count, sprite_num);
        if (unique_index < 0) {
            PalPackSpan sprite_span;

            if (unique_count >= PAL_SCENE_MAX_UNIQUE_SPRITES) {
                return false;
            }
            if (!PalPack_MapConst(sprite_pack, PAL_PACK_ARCHIVE_MGO, sprite_num, &sprite_span)) {
                return false;
            }
            if (sprite_span.data == NULL || sprite_span.size == 0 || sprite_span.format != PAL_PACK_FORMAT_NATIVE) {
                return false;
            }
#if PAL_SCENE_ENABLE_PINNED
            if (pin_sprites) {
                uint32_t pin_offset = align4(pin_cursor);

                if (pin_offset > PAL_PSRAM_SPRITE_PIN_BYTES ||
                    sprite_span.size > PAL_PSRAM_SPRITE_PIN_BYTES - pin_offset) {
                    return false;
                }
                memcpy(pal_psram_sprite_pin + pin_offset, sprite_span.data, sprite_span.size);
                pin_cursor = pin_offset + sprite_span.size;
                sprite_span.data = pal_psram_sprite_pin + pin_offset;
            }
#endif

            unique_index = (int)unique_count;
            pal_scene_unique_sprite_nums[unique_count] = sprite_num;
            pal_scene_unique_sprite_data[unique_count] = sprite_span.data;
            pal_scene_unique_sprite_sizes[unique_count] = sprite_span.size;
            unique_sprite_bytes += sprite_span.size;
            unique_count++;
        }

        pal_scene_sprite_refs[i].unique_index = (uint16_t)unique_index;
        pal_scene_sprite_refs[i].data = pal_scene_unique_sprite_data[unique_index];
        pal_scene_sprite_refs[i].size = pal_scene_unique_sprite_sizes[unique_index];
        sprite_ref_count++;
    }

    snapshot->scene_num = scene_num;
    snapshot->map_num = map_num;
    snapshot->event_start = event_start;
    snapshot->event_count = event_count;
    snapshot->sprite_ref_count = sprite_ref_count;
    snapshot->unique_sprite_count = unique_count;
    snapshot->gop_size = copied;
    snapshot->unique_sprite_bytes = unique_sprite_bytes;
#if PAL_SCENE_ENABLE_PINNED
    snapshot->sprite_pin_bytes = pin_sprites ? pin_cursor : 0u;
#else
    snapshot->sprite_pin_bytes = 0u;
#endif
    snapshot->sprite_refs = pal_scene_sprite_refs;
    return true;
}

bool PalScene_LoadSnapshot(const PalPack *nor_pack, const PalPack *tf_pack, uint16_t scene_num, PalSceneSnapshot *snapshot)
{
    return load_snapshot(nor_pack, tf_pack, NULL, NULL, NULL, nor_pack, NULL, 0, NULL, 0, scene_num, false, snapshot);
}

bool PalScene_LoadSnapshotReadAt(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot)
{
    return load_snapshot(nor_pack, NULL, tf_toc, read_at, user, nor_pack, NULL, 0, NULL, 0, scene_num, false, snapshot);
}

bool PalScene_LoadSnapshotReadAtWithEvents(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const uint8_t *event_objects,
    uint32_t event_objects_size,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot)
{
    return load_snapshot(nor_pack, NULL, tf_toc, read_at, user, nor_pack, NULL, 0, event_objects, event_objects_size, scene_num, false, snapshot);
}

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
    PalSceneSnapshot *snapshot)
{
    return load_snapshot(nor_pack, NULL, tf_toc, read_at, user, nor_pack, scene_records, scene_records_size, event_objects, event_objects_size, scene_num, false, snapshot);
}

#if PAL_SCENE_ENABLE_PINNED
bool PalScene_LoadPinnedSnapshot(
    const PalPack *nor_pack,
    const PalPack *tf_pack,
    const PalPack *sprite_pack,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot)
{
    return load_snapshot(nor_pack, tf_pack, NULL, NULL, NULL, sprite_pack, NULL, 0, NULL, 0, scene_num, true, snapshot);
}

bool PalScene_LoadPinnedSnapshotReadAt(
    const PalPack *nor_pack,
    const PalPackToc *tf_toc,
    PalPackReadAt read_at,
    void *user,
    const PalPack *sprite_pack,
    uint16_t scene_num,
    PalSceneSnapshot *snapshot)
{
    return load_snapshot(nor_pack, NULL, tf_toc, read_at, user, sprite_pack, NULL, 0, NULL, 0, scene_num, true, snapshot);
}
#endif
