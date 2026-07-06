#include "cores3se_board.h"

#include "../../embedded/pal_global_cache.h"
#include "../../embedded/pal_memory.h"
#include "../../embedded/pal_pack.h"
#include "../../embedded/pal_scene_cache.h"
#include "../../embedded/pal_video_static.h"

#include <esp_log.h>
#include <esp_partition.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char *TAG = "sdlpal_cores3se";
static const char *TF_PACK_PATH = "/sdcard/pal_tf.pak";

#define DEMO_INITIAL_SCENE_NUM 1u
#define DEMO_LAST_SCENE_NUM (PAL_SCENE_COUNT - 1u)
#define SSS_EVENT_OBJECT_CHUNK 0u
#define SSS_EVENT_OBJECT_BYTES 32u
#define EVENT_VANISH_TIME_OFFSET 0u
#define EVENT_X_OFFSET 2u
#define EVENT_Y_OFFSET 4u
#define EVENT_LAYER_OFFSET 6u
#define EVENT_STATE_OFFSET 12u
#define EVENT_SPRITE_FRAMES_OFFSET 18u
#define EVENT_DIRECTION_OFFSET 20u
#define EVENT_CURRENT_FRAME_OFFSET 22u
#define DEMO_MAP_PIXEL_WIDTH (64 * 32)
#define DEMO_MAP_PIXEL_HEIGHT (128 * 16)
#define PLAYER_ROLE_COUNT 6u
#define PLAYER_ROLE_WORD_ARRAY_BYTES (PLAYER_ROLE_COUNT * 2u)
#define PLAYER_ROLE_SPRITE_NUM_OFFSET (2u * PLAYER_ROLE_WORD_ARRAY_BYTES)
#define PLAYER_ROLE_WALK_FRAMES_OFFSET 768u
#define DEMO_PARTY_SCREEN_X 160
#define DEMO_PARTY_SCREEN_Y 112
#define DEMO_DIR_SOUTH 0u
#define DEMO_DIR_WEST 1u
#define DEMO_DIR_NORTH 2u
#define DEMO_DIR_EAST 3u
#define SAVE_HEADER_BYTES 44u
#define SAVE_VIEWPORT_X_OFFSET 2u
#define SAVE_VIEWPORT_Y_OFFSET 4u
#define SAVE_SCENE_OFFSET 8u
#define SAVE_PARTY_DIRECTION_OFFSET 12u
#define SAVE_PALETTE_OFFSET_OFFSET 10u
#define SAVE_CASH_OFFSET 40u
#define SAVE_PARTY_OFFSET 44u
#define SAVE_PLAYER_ROLES_OFFSET 508u
#define SAVE_PLAYER_ROLES_BYTES 900u
#define SAVE_SCENES_OFFSET 3264u
#define SAVE_SCENES_BYTES (PAL_SCENE_COUNT * 8u)
#define SAVE_EVENT_OBJECTS_OFFSET 12864u
#define SAVE_SLOT_FIRST 1u
#define SAVE_SLOT_LAST 5u
#define SAVE_PATH_SLOT_INDEX 8u

static PalPack pal_nor_pack;
static PalPackToc pal_tf_toc;
static const PalGlobalCache *pal_global_cache;
static esp_partition_mmap_handle_t pal_nor_mmap_handle;
static int pal_tf_fd = -1;
static bool pal_nor_ready;
static bool pal_tf_ready;
static bool pal_tf_scene_ready;
static uint32_t pal_tf_scene_checksum;
static uint16_t pal_scene_num = DEMO_INITIAL_SCENE_NUM;
static PalSceneSnapshot pal_scene_snapshot;
static const uint8_t *pal_scene_event_objects;
static uint32_t pal_scene_event_objects_size;
static bool pal_scene_event_objects_mutable;
static int pal_viewport_x;
static int pal_viewport_y;
static bool pal_touch_tracking;
static bool pal_touch_scene_gate;
static uint16_t pal_touch_last_x;
static uint16_t pal_touch_last_y;
static const uint8_t *pal_player_sprite;
static uint32_t pal_player_sprite_size;
static uint16_t pal_player_walk_frames;
static uint16_t pal_player_frame_num;
static uint16_t pal_player_direction;
static uint16_t pal_player_role;
static bool pal_player_walking;
static int pal_initial_viewport_x;
static int pal_initial_viewport_y;
static const uint8_t *pal_save_player_roles;
static const uint8_t *pal_save_scenes;
static const uint8_t *pal_save_event_objects;
static uint32_t pal_save_event_objects_size;
static char pal_save_path[] = "/sdcard/1.rpg";
static uint8_t pal_save_slot;
static uint8_t pal_save_header[SAVE_HEADER_BYTES];
static bool pal_palette_night;

typedef struct DemoSpriteDraw {
    const uint8_t *rle;
    int x;
    int y;
    int sort_y;
} DemoSpriteDraw;

static DemoSpriteDraw pal_scene_draw_items[PAL_SCENE_MAX_EVENT_OBJECTS + 1u];

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void write_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static int16_t read_s16(const uint8_t *p)
{
    return (int16_t)read_le16(p);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static bool map_tile_blocked(int x, int y, int h)
{
    uint32_t offset;

    if (!pal_tf_scene_ready || x < 0 || x >= 64 || y < 0 || y >= 128 || h < 0 || h > 1) {
        return true;
    }

    offset = ((((uint32_t)y * 64u) + (uint32_t)x) * 2u + (uint32_t)h) * 4u;
    return (read_le32(pal_psram_map_tiles + offset) & 0x2000u) != 0;
}

static bool map_position_blocked(int world_x, int world_y)
{
    int x;
    int y;
    int h = 0;
    int xr;
    int yr;
    int diagonal;

    if (world_x < 0 || world_y < 0) {
        return true;
    }

    x = world_x / 32;
    y = world_y / 16;
    xr = world_x % 32;
    yr = world_y % 16;
    diagonal = xr + yr * 2;

    if (diagonal >= 16) {
        if (diagonal >= 48) {
            x++;
            y++;
        } else if (32 - xr + yr * 2 < 16) {
            x++;
        } else if (32 - xr + yr * 2 < 48) {
            h = 1;
        } else {
            y++;
        }
    }

    return map_tile_blocked(x, y, h);
}

static bool viewport_party_position_blocked(int viewport_x, int viewport_y)
{
    return map_position_blocked(viewport_x + DEMO_PARTY_SCREEN_X,
                                viewport_y + DEMO_PARTY_SCREEN_Y);
}

static void update_demo_viewport(bool touched, uint16_t tx, uint16_t ty)
{
    const int max_x = DEMO_MAP_PIXEL_WIDTH - 320;
    const int max_y = DEMO_MAP_PIXEL_HEIGHT - 200;
    uint16_t local_y;

    if (!pal_tf_scene_ready ||
        !touched ||
        ty < CORES3SE_PAL_Y_OFFSET ||
        ty >= CORES3SE_PAL_Y_OFFSET + 200u) {
        pal_touch_tracking = false;
        pal_player_walking = false;
        return;
    }

    local_y = (uint16_t)(ty - CORES3SE_PAL_Y_OFFSET);
    if (pal_touch_tracking) {
        int dx = (int)pal_touch_last_x - (int)tx;
        int dy = (int)pal_touch_last_y - (int)local_y;
        int mag_x = dx < 0 ? -dx : dx;
        int mag_y = dy < 0 ? -dy : dy;
        int old_x = pal_viewport_x;
        int old_y = pal_viewport_y;
        int target_x = clamp_int(pal_viewport_x + dx, 0, max_x);
        int target_y = clamp_int(pal_viewport_y + dy, 0, max_y);

        if (!viewport_party_position_blocked(target_x, target_y)) {
            pal_viewport_x = target_x;
            pal_viewport_y = target_y;
        }
        pal_player_walking = pal_viewport_x != old_x || pal_viewport_y != old_y;
        if (mag_x != 0 || mag_y != 0) {
            if (mag_x > mag_y * 2) {
                pal_player_direction = dx > 0 ? DEMO_DIR_EAST : DEMO_DIR_WEST;
            } else if (mag_y > mag_x / 2) {
                pal_player_direction = dy > 0 ? DEMO_DIR_SOUTH : DEMO_DIR_NORTH;
            }
        }
    } else {
        pal_player_walking = false;
    }

    pal_touch_last_x = tx;
    pal_touch_last_y = local_y;
    pal_touch_tracking = true;
    pal_touch_scene_gate = false;
}

static void load_demo_palette(void)
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        pal_sram_palette_work[i * 3u + 0u] = (uint8_t)i;
        pal_sram_palette_work[i * 3u + 1u] = (uint8_t)((i * 5u) >> 2);
        pal_sram_palette_work[i * 3u + 2u] = (uint8_t)(255u - i);
    }
    (void)PalVideo_SetPaletteRgb(0, 256, pal_sram_palette_work);
}

static void load_pack_palette_or_demo(void)
{
    PalPackSpan span;
    uint32_t source_offset = 0;
    uint32_t i;

    if (pal_nor_ready &&
        PalPack_MapConst(&pal_nor_pack, PAL_PACK_ARCHIVE_PAT, 0, &span) &&
        span.format == PAL_PACK_FORMAT_NATIVE &&
        span.size >= PAL_SRAM_PALETTE_RGB_BYTES) {
        if (pal_palette_night && span.size >= PAL_SRAM_PALETTE_RGB_BYTES * 2u) {
            source_offset = PAL_SRAM_PALETTE_RGB_BYTES;
        }
        for (i = 0; i < PAL_SRAM_PALETTE_RGB_BYTES; i++) {
            pal_sram_palette_work[i] = (uint8_t)(span.data[source_offset + i] << 2);
        }
        (void)PalVideo_SetPaletteRgb(0, 256, pal_sram_palette_work);
        return;
    }
    load_demo_palette();
}

static bool read_tf_pack_at(void *user, uint32_t offset, uint8_t *dst, uint32_t size)
{
    int fd = *(int *)user;
    uint32_t done = 0;

    if (dst == NULL && size != 0) {
        return false;
    }
    CoreS3Se_PrepareTfAccess();
    while (done < size) {
        ssize_t got = pread(fd, dst + done, size - done, (off_t)offset + (off_t)done);
        if (got <= 0) {
            return false;
        }
        done += (uint32_t)got;
    }
    return true;
}

static bool open_nor_pack(void)
{
    const esp_partition_t *partition;
    uint8_t header[32];
    uint32_t pack_size;
    const void *mapped = NULL;
    esp_err_t err;

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "pal_nor");
    ESP_LOGI(TAG, "pal_nor partition %s", partition != NULL ? "present" : "missing");
    if (partition == NULL) {
        return false;
    }

    ESP_LOGI(TAG, "pal_nor offset=0x%08" PRIx32 " size=%" PRIu32, partition->address, partition->size);
    err = esp_partition_read(partition, 0, header, sizeof(header));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read pal_nor header: %s", esp_err_to_name(err));
        return false;
    }
    if (read_le32(header) != PAL_PACK_MAGIC || read_le16(header + 4) != PAL_PACK_VERSION) {
        ESP_LOGE(TAG, "pal_nor does not contain a PAL pack");
        return false;
    }

    pack_size = read_le32(header + 24);
    if (pack_size > partition->size || pack_size < sizeof(header)) {
        ESP_LOGE(TAG, "bad pal_nor pack size: %" PRIu32, pack_size);
        return false;
    }

    err = esp_partition_mmap(partition, 0, pack_size, ESP_PARTITION_MMAP_DATA, &mapped, &pal_nor_mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap pal_nor pack: %s", esp_err_to_name(err));
        return false;
    }
    if (!PalPack_OpenConst(&pal_nor_pack, (const uint8_t *)mapped, pack_size)) {
        ESP_LOGE(TAG, "PAL pack open failed");
        return false;
    }

    ESP_LOGI(TAG, "PAL NOR pack mapped: size=%" PRIu32 " archives=%u", pal_nor_pack.size, (unsigned)pal_nor_pack.archive_count);
    return true;
}

static bool open_tf_pack(void)
{
    struct stat st;

    pal_tf_fd = open(TF_PACK_PATH, O_RDONLY);
    if (pal_tf_fd < 0) {
        ESP_LOGW(TAG, "TF pack missing: %s", TF_PACK_PATH);
        return false;
    }

    if (fstat(pal_tf_fd, &st) != 0 || st.st_size <= 0 || st.st_size > UINT32_MAX) {
        ESP_LOGE(TAG, "bad TF pack size: %s", TF_PACK_PATH);
        close(pal_tf_fd);
        pal_tf_fd = -1;
        return false;
    }

    if (!PalPack_OpenTocRead(
            &pal_tf_toc,
            read_tf_pack_at,
            &pal_tf_fd,
            (uint32_t)st.st_size,
            pal_psram_tf_toc,
            PAL_PSRAM_TF_TOC_BYTES)) {
        ESP_LOGE(TAG, "TF pack TOC open failed: %s", TF_PACK_PATH);
        close(pal_tf_fd);
        pal_tf_fd = -1;
        return false;
    }

    ESP_LOGI(TAG, "PAL TF pack opened: size=%" PRIu32 " toc=%" PRIu32 " archives=%u",
             pal_tf_toc.pack_size, pal_tf_toc.toc_size, (unsigned)pal_tf_toc.archive_count);
    return true;
}

static void set_save_slot_path(uint8_t slot)
{
    pal_save_path[SAVE_PATH_SLOT_INDEX] = (char)('0' + slot);
}

static bool read_save_header(uint8_t slot, uint16_t *saved_times)
{
    int fd;
    uint32_t done = 0;

    if (saved_times == NULL) {
        return false;
    }
    *saved_times = 0;
    set_save_slot_path(slot);
    CoreS3Se_PrepareTfAccess();
    fd = open(pal_save_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    while (done < SAVE_HEADER_BYTES) {
        ssize_t got = read(fd, pal_save_header + done, SAVE_HEADER_BYTES - done);
        if (got <= 0) {
            close(fd);
            return false;
        }
        done += (uint32_t)got;
    }
    close(fd);
    *saved_times = read_le16(pal_save_header);
    return true;
}

static bool find_startup_save_slot(uint8_t *slot)
{
    uint8_t best_slot = 0;
    uint16_t best_saved_times = 0;
    uint8_t candidate;

    if (slot == NULL) {
        return false;
    }

    for (candidate = SAVE_SLOT_FIRST; candidate <= SAVE_SLOT_LAST; candidate++) {
        uint16_t saved_times = 0;
        if (!read_save_header(candidate, &saved_times)) {
            continue;
        }
        if (best_slot == 0 || saved_times >= best_saved_times) {
            best_slot = candidate;
            best_saved_times = saved_times;
        }
    }

    if (best_slot == 0) {
        return false;
    }
    *slot = best_slot;
    return true;
}

static bool load_startup_save_slot(uint8_t slot)
{
    int fd;
    struct stat st;
    uint32_t size;
    uint32_t done = 0;
    uint16_t scene_num;
    uint16_t viewport_x;
    uint16_t viewport_y;

    pal_initial_viewport_x = 0;
    pal_initial_viewport_y = 0;
    pal_player_role = 0;
    pal_player_direction = DEMO_DIR_SOUTH;
    pal_palette_night = false;
    pal_save_player_roles = NULL;
    pal_save_scenes = NULL;
    pal_save_event_objects = NULL;
    pal_save_event_objects_size = 0;

    if (!pal_tf_ready) {
        return false;
    }

    set_save_slot_path(slot);
    CoreS3Se_PrepareTfAccess();
    fd = open(pal_save_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGI(TAG, "startup save missing: %s", pal_save_path);
        return false;
    }
    if (fstat(fd, &st) != 0 ||
        st.st_size < (off_t)(SAVE_EVENT_OBJECTS_OFFSET + SSS_EVENT_OBJECT_BYTES) ||
        st.st_size > (off_t)PAL_PSRAM_SAVE_STATE_BYTES) {
        ESP_LOGW(TAG, "startup save size unsupported: %s", pal_save_path);
        close(fd);
        return false;
    }

    size = (uint32_t)st.st_size;
    while (done < size) {
        ssize_t got = read(fd, pal_psram_save_state + done, size - done);
        if (got <= 0) {
            ESP_LOGW(TAG, "startup save read failed: %s", pal_save_path);
            close(fd);
            return false;
        }
        done += (uint32_t)got;
    }
    close(fd);

    scene_num = read_le16(pal_psram_save_state + SAVE_SCENE_OFFSET);
    viewport_x = read_le16(pal_psram_save_state + SAVE_VIEWPORT_X_OFFSET);
    viewport_y = read_le16(pal_psram_save_state + SAVE_VIEWPORT_Y_OFFSET);
    if (scene_num == 0 ||
        scene_num >= PAL_SCENE_COUNT ||
        size < SAVE_PARTY_OFFSET + 2u ||
        size < SAVE_PLAYER_ROLES_OFFSET + SAVE_PLAYER_ROLES_BYTES ||
        size < SAVE_SCENES_OFFSET + SAVE_SCENES_BYTES) {
        ESP_LOGW(TAG, "startup save scene out of range: %u", (unsigned)scene_num);
        return false;
    }

    pal_scene_num = scene_num;
    pal_initial_viewport_x = viewport_x;
    pal_initial_viewport_y = viewport_y;
    pal_player_role = read_le16(pal_psram_save_state + SAVE_PARTY_OFFSET);
    if (pal_player_role >= PLAYER_ROLE_COUNT) {
        pal_player_role = 0;
    }
    pal_player_direction = read_le16(pal_psram_save_state + SAVE_PARTY_DIRECTION_OFFSET);
    if (pal_player_direction > DEMO_DIR_EAST) {
        pal_player_direction = DEMO_DIR_SOUTH;
    }
    pal_palette_night = read_le16(pal_psram_save_state + SAVE_PALETTE_OFFSET_OFFSET) != 0;
    pal_save_player_roles = pal_psram_save_state + SAVE_PLAYER_ROLES_OFFSET;
    pal_save_scenes = pal_psram_save_state + SAVE_SCENES_OFFSET;
    pal_save_event_objects = pal_psram_save_state + SAVE_EVENT_OBJECTS_OFFSET;
    pal_save_event_objects_size = size - SAVE_EVENT_OBJECTS_OFFSET;
    pal_save_slot = slot;
    ESP_LOGI(TAG,
             "startup save loaded: slot=%u bytes=%" PRIu32 " scene=%u viewport=%u,%u role=%u dir=%u night=%u cash=%" PRIu32,
             (unsigned)pal_save_slot,
             size,
             (unsigned)scene_num,
             (unsigned)viewport_x,
             (unsigned)viewport_y,
             (unsigned)pal_player_role,
             (unsigned)pal_player_direction,
             pal_palette_night ? 1u : 0u,
             read_le32(pal_psram_save_state + SAVE_CASH_OFFSET));
    return true;
}

static bool load_startup_save(void)
{
    uint8_t slot = 0;

    if (!pal_tf_ready || !find_startup_save_slot(&slot)) {
        pal_save_slot = 0;
        return false;
    }
    return load_startup_save_slot(slot);
}

static void load_global_cache(void)
{
    pal_global_cache = NULL;
    if (!pal_nor_ready) {
        return;
    }
    if (!PalGlobal_LoadDefault(&pal_nor_pack, &pal_global_cache)) {
        ESP_LOGW(TAG, "PAL global cache load failed");
        pal_global_cache = NULL;
        return;
    }
    ESP_LOGI(TAG,
             "PAL global cache loaded: mutable=%" PRIu32 " events=%" PRIu32 " scenes=%" PRIu32,
             pal_global_cache->mutable_bytes,
             pal_global_cache->event_objects.count,
             pal_global_cache->scenes.count);
}

static uint16_t player_role_word(uint32_t field_offset, uint16_t role)
{
    const uint8_t *player_roles;
    uint32_t player_roles_size;
    uint32_t offset = field_offset + (uint32_t)role * 2u;

    if (pal_save_player_roles != NULL) {
        player_roles = pal_save_player_roles;
        player_roles_size = SAVE_PLAYER_ROLES_BYTES;
    } else if (pal_global_cache != NULL && pal_global_cache->player_roles.data != NULL) {
        player_roles = pal_global_cache->player_roles.data;
        player_roles_size = pal_global_cache->player_roles.size;
    } else {
        return 0;
    }

    if (role >= PLAYER_ROLE_COUNT ||
        offset > player_roles_size ||
        2u > player_roles_size - offset) {
        return 0;
    }
    return read_le16(player_roles + offset);
}

static void load_player_sprite(void)
{
    PalPackSpan span;
    uint16_t sprite_num;

    pal_player_sprite = NULL;
    pal_player_sprite_size = 0;
    pal_player_walk_frames = 3;
    pal_player_frame_num = 0;
    pal_player_walking = false;

    if (!pal_nor_ready) {
        return;
    }

    sprite_num = player_role_word(PLAYER_ROLE_SPRITE_NUM_OFFSET, pal_player_role);
    pal_player_walk_frames = player_role_word(PLAYER_ROLE_WALK_FRAMES_OFFSET, pal_player_role);
    if (pal_player_walk_frames == 0 || pal_player_walk_frames > 4u) {
        pal_player_walk_frames = 3;
    }

    if (PalPack_MapConst(&pal_nor_pack, PAL_PACK_ARCHIVE_MGO, sprite_num, &span) &&
        span.format == PAL_PACK_FORMAT_NATIVE &&
        span.data != NULL &&
        span.size != 0) {
        pal_player_sprite = span.data;
        pal_player_sprite_size = span.size;
        ESP_LOGI(TAG, "player sprite loaded: role=%u sprite=%u bytes=%" PRIu32 " walk_frames=%u",
                 (unsigned)pal_player_role,
                 (unsigned)sprite_num,
                 pal_player_sprite_size,
                 (unsigned)pal_player_walk_frames);
    }
}

static uint32_t sample_checksum(const uint8_t *data, uint32_t size)
{
    uint32_t i;
    uint32_t hash = 2166136261u;

    for (i = 0; i < size; i += 257u) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void load_tf_scene_chunks(void)
{
    PalPackSpan event_span;
    const uint8_t *event_objects = NULL;
    uint32_t event_objects_size = 0;
    bool event_objects_mutable = false;

    pal_tf_scene_ready = false;
    pal_scene_event_objects = NULL;
    pal_scene_event_objects_size = 0;
    pal_scene_event_objects_mutable = false;

    if (!pal_tf_ready || !pal_nor_ready) {
        return;
    }

    if (pal_save_event_objects != NULL) {
        event_objects = pal_save_event_objects;
        event_objects_size = pal_save_event_objects_size;
        event_objects_mutable = true;
    } else if (pal_global_cache != NULL) {
        event_objects = pal_global_cache->event_objects.data;
        event_objects_size = pal_global_cache->event_objects.size;
        event_objects_mutable = true;
    }

    if (pal_save_scenes != NULL && event_objects != NULL) {
        if (!PalScene_LoadSnapshotReadAtWithSceneData(
                &pal_nor_pack,
                &pal_tf_toc,
                read_tf_pack_at,
                &pal_tf_fd,
                pal_save_scenes,
                SAVE_SCENES_BYTES,
                event_objects,
                event_objects_size,
                pal_scene_num,
                &pal_scene_snapshot)) {
            ESP_LOGW(TAG, "TF scene snapshot load failed");
            return;
        }
    } else if (event_objects != NULL) {
        if (!PalScene_LoadSnapshotReadAtWithEvents(
                &pal_nor_pack,
                &pal_tf_toc,
                read_tf_pack_at,
                &pal_tf_fd,
                event_objects,
                event_objects_size,
                pal_scene_num,
                &pal_scene_snapshot)) {
            ESP_LOGW(TAG, "TF scene snapshot load failed");
            return;
        }
    } else if (!PalScene_LoadSnapshotReadAt(
                &pal_nor_pack,
                &pal_tf_toc,
                read_tf_pack_at,
                &pal_tf_fd,
                pal_scene_num,
                &pal_scene_snapshot)) {
        ESP_LOGW(TAG, "TF scene snapshot load failed");
        return;
    }

    if (event_objects_size >= ((uint32_t)pal_scene_snapshot.event_start + pal_scene_snapshot.event_count) * SSS_EVENT_OBJECT_BYTES) {
        pal_scene_event_objects = event_objects;
        pal_scene_event_objects_size = event_objects_size;
        pal_scene_event_objects_mutable = event_objects_mutable;
    } else {
        if (!PalPack_MapConst(&pal_nor_pack, PAL_PACK_ARCHIVE_SSS, SSS_EVENT_OBJECT_CHUNK, &event_span) ||
            event_span.format != PAL_PACK_FORMAT_NATIVE ||
            event_span.size < ((uint32_t)pal_scene_snapshot.event_start + pal_scene_snapshot.event_count) * SSS_EVENT_OBJECT_BYTES) {
            ESP_LOGW(TAG, "NOR event object span missing");
            return;
        }
        pal_scene_event_objects = event_span.data;
        pal_scene_event_objects_size = event_span.size;
    }
    pal_tf_scene_checksum = sample_checksum(pal_psram_map_tiles, PAL_PSRAM_MAP_TILES_BYTES) ^
                            sample_checksum(pal_psram_gop_copy, pal_scene_snapshot.gop_size);
    pal_viewport_x = clamp_int(pal_initial_viewport_x, 0, DEMO_MAP_PIXEL_WIDTH - 320);
    pal_viewport_y = clamp_int(pal_initial_viewport_y, 0, DEMO_MAP_PIXEL_HEIGHT - 200);
    pal_tf_scene_ready = true;
    ESP_LOGI(TAG,
             "TF scene loaded: scene=%u map=%u events=%u unique_sprites=%u gop=%" PRIu32 " mark=0x%08" PRIx32,
             (unsigned)pal_scene_snapshot.scene_num,
             (unsigned)pal_scene_snapshot.map_num,
             (unsigned)pal_scene_snapshot.event_count,
             (unsigned)pal_scene_snapshot.unique_sprite_count,
             pal_scene_snapshot.gop_size,
             pal_tf_scene_checksum);
}

static void select_relative_scene(int delta)
{
    int next_scene = (int)pal_scene_num + delta;

    if (!pal_tf_ready || !pal_nor_ready) {
        return;
    }
    if (next_scene < 1) {
        next_scene = (int)DEMO_LAST_SCENE_NUM;
    } else if (next_scene > (int)DEMO_LAST_SCENE_NUM) {
        next_scene = 1;
    }

    pal_touch_tracking = false;
    pal_initial_viewport_x = 0;
    pal_initial_viewport_y = 0;
    pal_scene_num = (uint16_t)next_scene;
    load_tf_scene_chunks();
}

static void update_scene_selection(bool touched, uint16_t ty)
{
    if (!touched) {
        pal_touch_scene_gate = false;
        return;
    }
    if (pal_touch_scene_gate) {
        return;
    }

    if (ty < CORES3SE_PAL_Y_OFFSET) {
        select_relative_scene(-1);
        pal_touch_scene_gate = true;
    } else if (ty >= CORES3SE_PAL_Y_OFFSET + 200u) {
        select_relative_scene(1);
        pal_touch_scene_gate = true;
    }
}

static void advance_scene_event_frames(void)
{
    uint16_t i;

    if (!pal_scene_event_objects_mutable || pal_scene_event_objects == NULL) {
        return;
    }

    for (i = 0; i < pal_scene_snapshot.event_count; i++) {
        uint32_t offset = ((uint32_t)pal_scene_snapshot.event_start + i) * SSS_EVENT_OBJECT_BYTES;
        uint8_t *event_object;
        uint16_t sprite_frames;
        uint16_t frame;

        if (offset > pal_scene_event_objects_size ||
            SSS_EVENT_OBJECT_BYTES > pal_scene_event_objects_size - offset) {
            break;
        }

        event_object = (uint8_t *)pal_scene_event_objects + offset;
        sprite_frames = read_le16(event_object + EVENT_SPRITE_FRAMES_OFFSET);
        if (sprite_frames == 0) {
            continue;
        }
        frame = read_le16(event_object + EVENT_CURRENT_FRAME_OFFSET);
        frame = (uint16_t)((frame + 1u) % sprite_frames);
        write_le16(event_object + EVENT_CURRENT_FRAME_OFFSET, frame);
    }
}

static void advance_player_frame(void)
{
    if (pal_player_sprite == NULL || pal_player_walk_frames == 0) {
        return;
    }
    if (!pal_player_walking) {
        pal_player_frame_num = 0;
        return;
    }
    pal_player_frame_num = (uint16_t)((pal_player_frame_num + 1u) % pal_player_walk_frames);
}

static uint16_t sprite_frame_count(const uint8_t *sprite)
{
    if (sprite == NULL) {
        return 0;
    }
    return read_le16(sprite);
}

static const uint8_t *sprite_frame(const uint8_t *sprite, uint16_t frame)
{
    uint16_t count;
    uint32_t offset;
    if (sprite == NULL) {
        return NULL;
    }
    count = sprite_frame_count(sprite);
    if (frame >= count) {
        return NULL;
    }
    offset = (uint32_t)read_le16(sprite + (uint32_t)frame * 2u) * 2u;
    if (offset == 0) {
        return NULL;
    }
    return sprite + offset;
}

static const uint8_t *map_tile_bitmap(int x, int y, int h, uint8_t layer)
{
    uint32_t tile;
    uint16_t frame;
    uint32_t offset;

    if (x < 0 || x >= 64 || y < 0 || y >= 128 || h < 0 || h > 1) {
        return NULL;
    }

    offset = ((((uint32_t)y * 64u) + (uint32_t)x) * 2u + (uint32_t)h) * 4u;
    tile = read_le32(pal_psram_map_tiles + offset);
    if (layer == 0) {
        frame = (uint16_t)((tile & 0xFFu) | ((tile >> 4) & 0x100u));
    } else {
        tile >>= 16;
        frame = (uint16_t)((tile & 0xFFu) | ((tile >> 4) & 0x100u));
        if (frame == 0) {
            return NULL;
        }
        frame--;
    }
    return sprite_frame(pal_psram_gop_copy, frame);
}

static uint16_t rle_width(const uint8_t *rle)
{
    if (rle == NULL) {
        return 0;
    }
    if (read_le32(rle) == 2u) {
        rle += 4;
    }
    return read_le16(rle);
}

static uint16_t rle_height(const uint8_t *rle)
{
    if (rle == NULL) {
        return 0;
    }
    if (read_le32(rle) == 2u) {
        rle += 4;
    }
    return read_le16(rle + 2);
}

static void blit_rle_to_framebuffer(const uint8_t *rle, int dx, int dy)
{
    uint32_t i = 0;
    uint32_t src_x = 0;
    uint16_t width;
    uint16_t height;
    uint32_t len;

    if (rle == NULL) {
        return;
    }
    if (read_le32(rle) == 2u) {
        rle += 4;
    }
    width = read_le16(rle);
    height = read_le16(rle + 2);
    if (width == 0 || height == 0 || width > 320u || height > 200u) {
        return;
    }
    if (dx + (int)width <= 0 || dx >= 320 || dy + (int)height <= 0 || dy >= 200) {
        return;
    }

    len = (uint32_t)width * height;
    rle += 4;
    while (i < len) {
        uint8_t t = *rle++;
        if ((t & 0x80u) != 0 && t <= 0x80u + width) {
            uint32_t skip = (uint32_t)t - 0x80u;
            i += skip;
            src_x += skip;
            while (src_x >= width) {
                src_x -= width;
                dy++;
            }
        } else {
            uint32_t j = 0;
            uint32_t sx = src_x;
            int x = dx + (int)src_x;
            int y = dy;
            while (j < t) {
                uint32_t k;
                if (y < 0) {
                    uint32_t skip = (uint32_t)(-y) * width;
                    if (skip >= t - j) {
                        j = t;
                        break;
                    }
                    j += skip;
                    y = 0;
                } else if (y >= 200) {
                    return;
                }
                if (x < 0) {
                    uint32_t skip = (uint32_t)(-x);
                    if (skip >= t - j) {
                        j = t;
                        break;
                    }
                    j += skip;
                    sx += skip;
                    x = 0;
                } else if (x >= 320) {
                    j += width - sx;
                    x -= (int)sx;
                    sx = 0;
                    y++;
                    continue;
                }

                k = (uint32_t)t - j;
                if (320u - (uint32_t)x < k) k = 320u - (uint32_t)x;
                if ((uint32_t)width - sx < k) k = (uint32_t)width - sx;
                for (; k != 0; k--) {
                    pal_sram_framebuffer[(uint32_t)y * 320u + (uint32_t)x] = rle[j];
                    j++;
                    x++;
                    sx++;
                }
                if (sx >= width) {
                    sx -= width;
                    x -= width;
                    y++;
                }
            }
            rle += t;
            i += t;
            src_x += t;
            while (src_x >= width) {
                src_x -= width;
                dy++;
            }
        }
    }
}

static void draw_map_layer(uint8_t layer, int viewport_x, int viewport_y)
{
    int sx = viewport_x / 32 - 1;
    int dx = (viewport_x + 320) / 32 + 2;
    int sy = viewport_y / 16 - 1;
    int dy = (viewport_y + 200) / 16 + 2;
    int y_pos = sy * 16 - 8 - viewport_y;
    int y;

    for (y = sy; y < dy; y++) {
        int h;
        for (h = 0; h < 2; h++, y_pos += 8) {
            int x_pos = sx * 32 + h * 16 - 16 - viewport_x;
            int x;
            for (x = sx; x < dx; x++, x_pos += 32) {
                const uint8_t *tile = map_tile_bitmap(x, y, h, layer);
                if (tile == NULL && layer == 0) {
                    tile = map_tile_bitmap(0, 0, 0, 0);
                }
                blit_rle_to_framebuffer(tile, x_pos, y_pos);
            }
        }
    }
}

static void draw_scene_event_sprites(int viewport_x, int viewport_y)
{
    uint16_t i;
    uint16_t draw_count = 0;

    if (pal_scene_event_objects == NULL || pal_scene_snapshot.sprite_refs == NULL) {
        return;
    }

    for (i = 0; i < pal_scene_snapshot.event_count; i++) {
        uint32_t offset = ((uint32_t)pal_scene_snapshot.event_start + i) * SSS_EVENT_OBJECT_BYTES;
        const uint8_t *event_object;
        const PalSceneSpriteRef *sprite_ref;
        const uint8_t *rle;
        uint16_t sprite_frames;
        uint16_t frame;
        uint16_t direction;
        int16_t state;
        int16_t vanish_time;
        int16_t layer;
        int x;
        int y;
        uint16_t w;
        uint16_t h;

        if (offset > pal_scene_event_objects_size ||
            SSS_EVENT_OBJECT_BYTES > pal_scene_event_objects_size - offset) {
            break;
        }

        event_object = pal_scene_event_objects + offset;
        state = read_s16(event_object + EVENT_STATE_OFFSET);
        vanish_time = read_s16(event_object + EVENT_VANISH_TIME_OFFSET);
        if (state <= 0 || vanish_time > 0) {
            continue;
        }

        sprite_ref = &pal_scene_snapshot.sprite_refs[i];
        if (sprite_ref->data == NULL || sprite_ref->size == 0) {
            continue;
        }

        sprite_frames = read_le16(event_object + EVENT_SPRITE_FRAMES_OFFSET);
        if (sprite_frames == 0) {
            sprite_frames = 1;
        }
        frame = read_le16(event_object + EVENT_CURRENT_FRAME_OFFSET);
        if (sprite_frames == 3u) {
            if (frame == 2u) {
                frame = 0u;
            } else if (frame == 3u) {
                frame = 2u;
            }
        }
        direction = read_le16(event_object + EVENT_DIRECTION_OFFSET);
        rle = sprite_frame(sprite_ref->data, (uint16_t)(direction * sprite_frames + frame));
        if (rle == NULL) {
            continue;
        }

        w = rle_width(rle);
        h = rle_height(rle);
        layer = read_s16(event_object + EVENT_LAYER_OFFSET);
        x = (int)read_s16(event_object + EVENT_X_OFFSET) - viewport_x - (int)w / 2;
        y = (int)read_s16(event_object + EVENT_Y_OFFSET) - viewport_y + 7 - (int)h;
        if (x >= 320 || x < -(int)w || y >= 200 || y < -(int)h) {
            continue;
        }

        if (draw_count < (uint16_t)(sizeof(pal_scene_draw_items) / sizeof(pal_scene_draw_items[0]))) {
            pal_scene_draw_items[draw_count].rle = rle;
            pal_scene_draw_items[draw_count].x = x;
            pal_scene_draw_items[draw_count].y = y;
            pal_scene_draw_items[draw_count].sort_y =
                (int)read_s16(event_object + EVENT_Y_OFFSET) - viewport_y + layer * 8 + 9;
            draw_count++;
        }
    }

    if (pal_player_sprite != NULL && draw_count < (uint16_t)(sizeof(pal_scene_draw_items) / sizeof(pal_scene_draw_items[0]))) {
        uint16_t frame_index = (uint16_t)(pal_player_direction * pal_player_walk_frames + pal_player_frame_num);
        const uint8_t *rle = sprite_frame(pal_player_sprite, frame_index);
        uint16_t w;
        uint16_t h;

        if (rle == NULL) {
            rle = sprite_frame(pal_player_sprite, (uint16_t)(pal_player_direction * pal_player_walk_frames));
        }
        w = rle_width(rle);
        h = rle_height(rle);
        if (rle != NULL && w != 0 && h != 0) {
            pal_scene_draw_items[draw_count].rle = rle;
            pal_scene_draw_items[draw_count].x = DEMO_PARTY_SCREEN_X - (int)w / 2;
            pal_scene_draw_items[draw_count].y = DEMO_PARTY_SCREEN_Y + 10 - (int)h;
            pal_scene_draw_items[draw_count].sort_y = DEMO_PARTY_SCREEN_Y + 6;
            draw_count++;
        }
    }

    for (i = 1; i < draw_count; i++) {
        DemoSpriteDraw item = pal_scene_draw_items[i];
        uint16_t j = i;
        while (j > 0 && pal_scene_draw_items[j - 1u].sort_y > item.sort_y) {
            pal_scene_draw_items[j] = pal_scene_draw_items[j - 1u];
            j--;
        }
        pal_scene_draw_items[j] = item;
    }

    for (i = 0; i < draw_count; i++) {
        blit_rle_to_framebuffer(pal_scene_draw_items[i].rle, pal_scene_draw_items[i].x, pal_scene_draw_items[i].y);
    }
}

static void draw_scene_background(uint32_t tick)
{
    if (pal_tf_scene_ready) {
        memset(pal_sram_framebuffer, 0, PAL_SRAM_FRAMEBUFFER_BYTES);
        draw_map_layer(0, pal_viewport_x, pal_viewport_y);
        draw_map_layer(1, pal_viewport_x, pal_viewport_y);
        draw_scene_event_sprites(pal_viewport_x, pal_viewport_y);
    } else {
        uint32_t x;
        uint32_t y;
        for (y = 0; y < 200u; y++) {
            uint8_t *dst = pal_sram_framebuffer + y * 320u;
            for (x = 0; x < 320u; x++) {
                uint32_t value = (x + y + tick) & 0xFFu;
                if (((x / 16u) ^ (y / 16u)) & 1u) {
                    value = (value + 64u) & 0xFFu;
                }
                dst[x] = (uint8_t)value;
            }
        }
    }
}

static void draw_demo_frame(uint32_t tick, bool touched, uint16_t tx, uint16_t ty)
{
    draw_scene_background(tick);

    if (touched && ty >= CORES3SE_PAL_Y_OFFSET && ty < CORES3SE_PAL_Y_OFFSET + 200u) {
        const int cx = (int)tx;
        const int cy = (int)ty - (int)CORES3SE_PAL_Y_OFFSET;
        int dy;
        for (dy = -10; dy <= 10; dy++) {
            int yy = cy + dy;
            int dx;
            if (yy < 0 || yy >= 200) {
                continue;
            }
            for (dx = -10; dx <= 10; dx++) {
                int xx = cx + dx;
                if (xx >= 0 && xx < 320 && dx * dx + dy * dy <= 100) {
                    pal_sram_framebuffer[(uint32_t)yy * 320u + (uint32_t)xx] = 255u;
                }
            }
        }
    }
}

void app_main(void)
{
    uint32_t tick = 0;

    if (!CoreS3Se_Begin()) {
        CoreS3Se_ShowError("BOARD FAIL", "CORES3SE INIT");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    pal_nor_ready = open_nor_pack();
    pal_tf_ready = CoreS3Se_MountTf() && open_tf_pack();
    if (!load_startup_save()) {
        load_global_cache();
    }
    load_pack_palette_or_demo();
    load_player_sprite();
    load_tf_scene_chunks();
    for (;;) {
        uint16_t tx = 0;
        uint16_t ty = 0;
        bool touched = CoreS3Se_TouchPoint(&tx, &ty);
        update_scene_selection(touched, ty);
        update_demo_viewport(touched, tx, ty);
        if ((tick & 7u) == 0) {
            advance_scene_event_frames();
            advance_player_frame();
        }
        draw_demo_frame(tick, touched, tx, ty);
        if (!CoreS3Se_FlushPalFramebuffer()) {
            CoreS3Se_ShowError("LCD FAIL", "FLUSH");
        }
        tick += touched ? 7u : 1u;
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
