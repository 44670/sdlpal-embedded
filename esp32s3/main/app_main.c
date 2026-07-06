#include "cores3se_board.h"
#include "pal_save_fatfs.h"

#include "../../embedded/pal_battle_cache.h"
#include "../../embedded/pal_dialog_static.h"
#include "../../embedded/pal_ending_static.h"
#include "../../embedded/pal_font_cache.h"
#include "../../embedded/pal_global_cache.h"
#include "../../embedded/pal_menu_static.h"
#include "../../embedded/pal_memory.h"
#include "../../embedded/pal_music_cache.h"
#include "../../embedded/pal_pack.h"
#include "../../embedded/pal_palette_static.h"
#include "../../embedded/pal_rng_cache.h"
#include "../../embedded/pal_scene_cache.h"
#include "../../embedded/pal_script_static.h"
#include "../../embedded/pal_text_cache.h"
#include "../../embedded/pal_ui_cache.h"
#include "../../embedded/pal_video_static.h"

#include <esp_log.h>
#include <esp_partition.h>
#include <esp_err.h>
#include <ff.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <string.h>

static const char *TAG = "sdlpal_cores3se";
static const char *TF_PACK_PATH = "0:/pal_tf.pak";

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
#define EVENT_STATE_BLOCKER 2
#define DEMO_MAP_PIXEL_WIDTH (64 * 32)
#define DEMO_MAP_PIXEL_HEIGHT (128 * 16)
#define PLAYER_ROLE_COUNT 6u
#define DEMO_PLAYABLE_PARTY_SLOTS 5u
#define DEMO_SCENE_DRAW_ITEM_COUNT (PAL_SCENE_MAX_EVENT_OBJECTS + DEMO_PLAYABLE_PARTY_SLOTS)
#define DEMO_MAX_PARTY_INDEX 2u
#define PARTY_STRUCT_BYTES 10u
#define PARTY_ROLE_OFFSET 0u
#define PARTY_X_OFFSET 2u
#define PARTY_Y_OFFSET 4u
#define PARTY_FRAME_OFFSET 6u
#define PLAYER_ROLE_WORD_ARRAY_BYTES (PLAYER_ROLE_COUNT * 2u)
#define PLAYER_ROLE_MAX_HP_WORD_INDEX 7u
#define PLAYER_ROLE_MAX_MP_WORD_INDEX 8u
#define PLAYER_ROLE_HP_WORD_INDEX 9u
#define PLAYER_ROLE_MP_WORD_INDEX 10u
#define PLAYER_ROLE_SPRITE_NUM_OFFSET (2u * PLAYER_ROLE_WORD_ARRAY_BYTES)
#define PLAYER_ROLE_WALK_FRAMES_OFFSET 768u
#define INVENTORY_SLOT_BYTES 6u
#define INVENTORY_ITEM_OFFSET 0u
#define INVENTORY_AMOUNT_OFFSET 2u
#define INVENTORY_IN_USE_OFFSET 4u
#define TRAIL_STRUCT_BYTES 6u
#define TRAIL_X_OFFSET 0u
#define TRAIL_Y_OFFSET 2u
#define TRAIL_DIRECTION_OFFSET 4u
#define DEMO_PARTY_SCREEN_X 160
#define DEMO_PARTY_SCREEN_Y 112
#define DEMO_TOUCH_DEADZONE 12
#define DEMO_STEP_X 16
#define DEMO_STEP_Y 8
#define DEMO_DIR_SOUTH 0u
#define DEMO_DIR_WEST 1u
#define DEMO_DIR_NORTH 2u
#define DEMO_DIR_EAST 3u
#define SAVE_VIEWPORT_X_OFFSET 2u
#define SAVE_VIEWPORT_Y_OFFSET 4u
#define SAVE_SCENE_OFFSET 8u
#define SAVE_SAVED_TIMES_OFFSET 0u
#define SAVE_PARTY_DIRECTION_OFFSET 12u
#define SAVE_FOLLOWER_COUNT_OFFSET 32u
#define SAVE_PALETTE_OFFSET_OFFSET 10u
#define SAVE_LAYER_OFFSET 26u
#define SAVE_CASH_OFFSET 40u
#define SAVE_PARTY_OFFSET 44u
#define SAVE_TRAIL_OFFSET (SAVE_PARTY_OFFSET + DEMO_PLAYABLE_PARTY_SLOTS * PARTY_STRUCT_BYTES)
#define SAVE_PLAYER_ROLES_OFFSET 508u
#define SAVE_PLAYER_ROLES_BYTES 900u
#define SAVE_INVENTORY_OFFSET 1728u
#define SAVE_INVENTORY_BYTES 1536u
#define SAVE_SCENES_OFFSET 3264u
#define SAVE_SCENES_BYTES (PAL_SCENE_COUNT * 8u)
#define SAVE_EVENT_OBJECTS_OFFSET 12864u
#define SAVE_SLOT_FIRST 1u
#define SAVE_SLOT_LAST 5u
#define SAVE_PATH_SLOT_INDEX 3u
#define SCENE_MAP_OFFSET 0u
#define SCENE_SCRIPT_ON_ENTER_OFFSET 2u
#define SCENE_SCRIPT_ON_TELEPORT_OFFSET 4u
#define EVENT_TRIGGER_SCRIPT_OFFSET 8u
#define EVENT_AUTO_SCRIPT_OFFSET 10u
#define EVENT_TRIGGER_MODE_OFFSET 14u
#define EVENT_TRIGGER_IDLE_OFFSET 24u
#define EVENT_AUTO_IDLE_OFFSET 30u
#define SCRIPT_STOP 0x0000u
#define SCRIPT_STOP_NEXT 0x0001u
#define SCRIPT_STOP_GOTO 0x0002u
#define SCRIPT_GOTO 0x0003u
#define SCRIPT_CALL_TRIGGER 0x0004u
#define SCRIPT_REDRAW 0x0005u
#define SCRIPT_RANDOM_BRANCH 0x0006u
#define SCRIPT_START_BATTLE 0x0007u
#define SCRIPT_REPLACE_WITH_NEXT 0x0008u
#define SCRIPT_WAIT 0x0009u
#define SCRIPT_WALK_SOUTH 0x000Bu
#define SCRIPT_WALK_WEST 0x000Cu
#define SCRIPT_WALK_NORTH 0x000Du
#define SCRIPT_WALK_EAST 0x000Eu
#define SCRIPT_SET_DIRECTION_FRAME 0x000Fu
#define SCRIPT_NPC_WALK_TO 0x0010u
#define SCRIPT_NPC_SLOW_WALK_TO 0x0011u
#define SCRIPT_SET_EVENT_POSITION_RELATIVE 0x0012u
#define SCRIPT_SET_EVENT_POSITION 0x0013u
#define SCRIPT_SET_FRAME_SOUTH 0x0014u
#define SCRIPT_SET_PARTY_DIRECTION_FRAME 0x0015u
#define SCRIPT_SET_EVENT_DIRECTION_FRAME 0x0016u
#define SCRIPT_SET_PLAYER_STAT 0x001Au
#define SCRIPT_CHANGE_HP_MP 0x001Du
#define SCRIPT_ADD_CASH 0x001Eu
#define SCRIPT_ADD_ITEM 0x001Fu
#define SCRIPT_REMOVE_ITEM 0x0020u
#define SCRIPT_REMOVE_EQUIPMENT 0x0023u
#define SCRIPT_SET_AUTO_SCRIPT 0x0024u
#define SCRIPT_SET_TRIGGER_SCRIPT 0x0025u
#define SCRIPT_BUY_MENU 0x0026u
#define SCRIPT_SELL_MENU 0x0027u
#define SCRIPT_SET_CURRENT_RNG 0x0036u
#define SCRIPT_PLAY_RNG 0x0037u
#define SCRIPT_SHAKE_SCREEN 0x0035u
#define SCRIPT_DIALOG_CENTER 0x003Bu
#define SCRIPT_DIALOG_UPPER 0x003Cu
#define SCRIPT_DIALOG_LOWER 0x003Du
#define SCRIPT_DIALOG_CENTER_WINDOW 0x003Eu
#define SCRIPT_RIDE_EVENT_SLOW 0x003Fu
#define SCRIPT_SET_TRIGGER_MODE 0x0040u
#define SCRIPT_SET_MUSIC 0x0043u
#define SCRIPT_RIDE_EVENT 0x0044u
#define SCRIPT_SET_BATTLE_MUSIC 0x0045u
#define SCRIPT_SET_PARTY_POSITION 0x0046u
#define SCRIPT_PLAY_SOUND 0x0047u
#define SCRIPT_SET_BATTLEFIELD 0x004Au
#define SCRIPT_VANISH_EVENT 0x004Bu
#define SCRIPT_CHASE_PLAYER 0x004Cu
#define SCRIPT_SET_EVENT_STATE 0x0049u
#define SCRIPT_FADE_OUT 0x0050u
#define SCRIPT_FADE_IN 0x0051u
#define SCRIPT_HIDE_EVENT 0x0052u
#define SCRIPT_USE_DAY_PALETTE 0x0053u
#define SCRIPT_USE_NIGHT_PALETTE 0x0054u
#define SCRIPT_ADD_MAGIC 0x0055u
#define SCRIPT_JUMP_IF_ITEM_LESS 0x0058u
#define SCRIPT_CHANGE_SCENE 0x0059u
#define SCRIPT_SET_PLAYER_SPRITE 0x0065u
#define SCRIPT_MOVE_PLAYER 0x006Eu
#define SCRIPT_SYNC_EVENT_STATE 0x006Fu
#define SCRIPT_PARTY_WALK_TO_SLOW 0x0070u
#define SCRIPT_SCREEN_WAVE 0x0071u
#define SCRIPT_JUMP_IF_NOT_FULL_HP 0x0074u
#define SCRIPT_SET_SCENE_SCRIPTS 0x006Du
#define SCRIPT_SET_PARTY 0x0075u
#define SCRIPT_SHOW_FBP 0x0076u
#define SCRIPT_STOP_MUSIC 0x0077u
#define SCRIPT_UNKNOWN_0078 0x0078u
#define SCRIPT_JUMP_IF_PLAYER_IN_PARTY 0x0079u
#define SCRIPT_PARTY_WALK_TO 0x007Au
#define SCRIPT_PARTY_WALK_TO_FAST 0x007Bu
#define SCRIPT_NPC_WALK_TO_FAST 0x007Cu
#define SCRIPT_MOVE_EVENT_ONE_STEP 0x006Cu
#define SCRIPT_MOVE_EVENT 0x007Du
#define SCRIPT_SET_EVENT_LAYER 0x007Eu
#define SCRIPT_MOVE_VIEWPORT 0x007Fu
#define SCRIPT_TOGGLE_DAY_NIGHT 0x0080u
#define SCRIPT_NPC_WALK_TO_FASTEST 0x0082u
#define SCRIPT_JUMP_IF_EVENT_OUT_OF_ZONE 0x0083u
#define SCRIPT_JUMP_IF_ITEM_NOT_EQUIPPED 0x0086u
#define SCRIPT_FADE_TO_SCENE 0x0073u
#define SCRIPT_ANIMATE_EVENT 0x0087u
#define SCRIPT_CHANGE_PALETTE 0x008Bu
#define SCRIPT_SYNC_PARTY_TRAIL 0x008Cu
#define SCRIPT_SCENE_FADE 0x0093u
#define SCRIPT_JUMP_IF_EVENT_STATE 0x0094u
#define SCRIPT_RIDE_EVENT_FAST 0x0097u
#define SCRIPT_SET_FOLLOWERS 0x0098u
#define SCRIPT_CHANGE_SCENE_MAP 0x0099u
#define SCRIPT_SET_EVENT_RANGE_STATE 0x009Au
#define SCRIPT_FADE_CURRENT_SCENE 0x009Bu
#define SCRIPT_RANDOM_SKIP 0x00A2u
#define SCRIPT_PLAY_CD_MUSIC 0x00A3u
#define SCRIPT_SYNC_PARTY_TO_LEADER 0x00A1u
#define SCRIPT_DIALOG_TEXT 0xFFFFu
#define SCRIPT_AUTO_MAX_JUMPS 8u
#define SCRIPT_TRIGGER_MAX_STEPS 16u
#define SCRIPT_CALL_MAX_DEPTH 3u
#define PAL_DIALOG_MODE_CENTER 0u
#define PAL_DIALOG_MODE_UPPER 1u
#define PAL_DIALOG_MODE_LOWER 2u
#define PAL_DIALOG_MODE_CENTER_WINDOW 3u

static PalPack pal_nor_pack;
static PalPackToc pal_tf_toc;
static PalTextCache pal_text_cache;
static PalFontCache pal_font_cache;
static PalUiAsset pal_ui_sprite_asset;
static PalUiAsset pal_ui_battle_effect_asset;
static PalUiAsset pal_ui_item_sample_asset;
static PalUiAsset pal_ui_face_sample_asset;
static PalDialogAsset pal_dialog_icon_asset;
static PalMenuBuffer pal_menu_background_buffer;
static PalMenuBuffer pal_menu_image_buffer;
static PalMenuBuffer pal_menu_box_buffer;
static PalMenuConstAsset pal_menu_item_asset;
static PalBattleSnapshot pal_battle_snapshot;
static PalBattleBuffer pal_battle_effect_buffer;
static PalRngMovieStream pal_rng_movie;
static PalRngFrame pal_rng_frame_a;
static PalRngFrame pal_rng_frame_b;
static PalMusicTrack pal_midi_sample_track;
static PalMusicTrack pal_mus_sample_track;
static PalEndingScreenPair pal_ending_pair;
static PalEndingConstAsset pal_ending_sprite_asset;
static PalScriptView pal_script_view;
static const PalGlobalCache *pal_global_cache;
static esp_partition_mmap_handle_t pal_nor_mmap_handle;
static FIL pal_tf_file;
static bool pal_tf_file_open;
static bool pal_nor_ready;
static bool pal_tf_ready;
static bool pal_text_ready;
static bool pal_font_ready;
static bool pal_ui_ready;
static bool pal_dialog_ready;
static bool pal_menu_ready;
static bool pal_battle_ready;
static bool pal_rng_ready;
static bool pal_music_ready;
static bool pal_ending_ready;
static bool pal_script_ready;
static bool pal_tf_scene_ready;
static uint8_t pal_scene_enter_reload_depth;
static uint32_t pal_tf_scene_checksum;
static uint16_t pal_scene_num = DEMO_INITIAL_SCENE_NUM;
static uint16_t pal_scene_script_idle;
static PalSceneSnapshot pal_scene_snapshot;
static const uint8_t *pal_scene_event_objects;
static uint32_t pal_scene_event_objects_size;
static bool pal_scene_event_objects_mutable;
static int pal_viewport_x;
static int pal_viewport_y;
static bool pal_touch_scene_gate;
static const uint8_t *pal_player_sprite;
static uint32_t pal_player_sprite_size;
static const uint8_t *pal_party_sprites[DEMO_PLAYABLE_PARTY_SLOTS];
static uint32_t pal_party_sprite_sizes[DEMO_PLAYABLE_PARTY_SLOTS];
static uint32_t pal_party_sprite_pin_bytes;
static uint16_t pal_party_roles[DEMO_PLAYABLE_PARTY_SLOTS];
static uint16_t pal_party_walk_frames[DEMO_PLAYABLE_PARTY_SLOTS];
static uint16_t pal_trail_x[DEMO_PLAYABLE_PARTY_SLOTS];
static uint16_t pal_trail_y[DEMO_PLAYABLE_PARTY_SLOTS];
static uint16_t pal_trail_direction[DEMO_PLAYABLE_PARTY_SLOTS];
static uint16_t pal_max_party_member_index;
static uint16_t pal_follower_count;
static uint16_t pal_party_layer;
static uint16_t pal_player_walk_frames;
static uint16_t pal_player_step_phase;
static uint16_t pal_player_direction;
static uint16_t pal_player_role;
static bool pal_player_walking;
static uint8_t pal_script_call_depth;
static uint32_t pal_script_rng_state = 0x4C50414Cu;
static bool pal_dialog_visible;
static uint8_t pal_dialog_mode = PAL_DIALOG_MODE_LOWER;
static uint16_t pal_dialog_message_id;
static bool pal_dialog_touch_gate;
static int pal_initial_viewport_x;
static int pal_initial_viewport_y;
static uint8_t *pal_save_player_roles;
static const uint8_t *pal_save_scenes;
static const uint8_t *pal_save_event_objects;
static uint32_t pal_save_event_objects_size;
static uint32_t pal_save_state_size;
static char pal_save_path[] = "0:/1.rpg";
static uint8_t pal_save_slot;
static bool pal_palette_night;

static const uint16_t pal_battle_sample_player_sprites[3] = {0u, 1u, 2u};

typedef struct DemoSpriteDraw {
    const uint8_t *rle;
    int x;
    int y;
    int sort_y;
} DemoSpriteDraw;

static DemoSpriteDraw pal_scene_draw_items[DEMO_SCENE_DRAW_ITEM_COUNT];

static void party_member_screen_position(uint16_t index, int *x, int *y, uint16_t *direction);
static void set_save_slot_path(uint8_t slot);
static void trigger_facing_event(void);
static void run_scene_enter_script_subset(void);

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

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
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

static int abs_int(int value)
{
    return value < 0 ? -value : value;
}

static uint32_t align4_u32(uint32_t value)
{
    return (value + 3u) & ~3u;
}

static void reset_party_state(void)
{
    uint16_t i;
    uint16_t world_x = (uint16_t)clamp_int(pal_initial_viewport_x + DEMO_PARTY_SCREEN_X, 0, DEMO_MAP_PIXEL_WIDTH - 1);
    uint16_t world_y = (uint16_t)clamp_int(pal_initial_viewport_y + DEMO_PARTY_SCREEN_Y, 0, DEMO_MAP_PIXEL_HEIGHT - 1);

    pal_max_party_member_index = 0;
    pal_follower_count = 0;
    pal_party_layer = 0;
    for (i = 0; i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
        pal_party_roles[i] = 0;
        pal_party_walk_frames[i] = 3;
        pal_party_sprites[i] = NULL;
        pal_party_sprite_sizes[i] = 0;
        pal_trail_x[i] = world_x;
        pal_trail_y[i] = world_y;
        pal_trail_direction[i] = pal_player_direction;
    }
    pal_player_role = 0;
}

static uint16_t visible_party_last_index(void)
{
    uint16_t last = (uint16_t)(pal_max_party_member_index + pal_follower_count);

    if (last >= DEMO_PLAYABLE_PARTY_SLOTS) {
        return DEMO_PLAYABLE_PARTY_SLOTS - 1u;
    }
    return last;
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

static bool event_position_blocked(int world_x, int world_y)
{
    uint16_t i;

    if (pal_scene_event_objects == NULL) {
        return false;
    }

    for (i = 0; i < pal_scene_snapshot.event_count; i++) {
        uint32_t offset = ((uint32_t)pal_scene_snapshot.event_start + i) * SSS_EVENT_OBJECT_BYTES;
        const uint8_t *event_object;
        int state;
        int event_x;
        int event_y;

        if (offset > pal_scene_event_objects_size ||
            SSS_EVENT_OBJECT_BYTES > pal_scene_event_objects_size - offset) {
            break;
        }

        event_object = pal_scene_event_objects + offset;
        state = read_s16(event_object + EVENT_STATE_OFFSET);
        if (state < EVENT_STATE_BLOCKER) {
            continue;
        }

        event_x = read_s16(event_object + EVENT_X_OFFSET);
        event_y = read_s16(event_object + EVENT_Y_OFFSET);
        if (abs_int(event_x - world_x) + abs_int(event_y - world_y) * 2 < 16) {
            return true;
        }
    }

    return false;
}

static bool viewport_party_position_blocked(int viewport_x, int viewport_y)
{
    int world_x = viewport_x + DEMO_PARTY_SCREEN_X;
    int world_y = viewport_y + DEMO_PARTY_SCREEN_Y;

    return map_position_blocked(world_x, world_y) ||
           event_position_blocked(world_x, world_y);
}

static void move_party_direction(uint16_t direction)
{
    const int max_x = DEMO_MAP_PIXEL_WIDTH - 320;
    const int max_y = DEMO_MAP_PIXEL_HEIGHT - 200;
    uint16_t i;
    int old_x = pal_viewport_x;
    int old_y = pal_viewport_y;
    int target_x = pal_viewport_x;
    int target_y = pal_viewport_y;
    uint16_t source_x = (uint16_t)(pal_viewport_x + DEMO_PARTY_SCREEN_X);
    uint16_t source_y = (uint16_t)(pal_viewport_y + DEMO_PARTY_SCREEN_Y);

    switch (direction) {
    case DEMO_DIR_SOUTH:
        target_x -= DEMO_STEP_X;
        target_y += DEMO_STEP_Y;
        break;
    case DEMO_DIR_WEST:
        target_x -= DEMO_STEP_X;
        target_y -= DEMO_STEP_Y;
        break;
    case DEMO_DIR_NORTH:
        target_x += DEMO_STEP_X;
        target_y -= DEMO_STEP_Y;
        break;
    case DEMO_DIR_EAST:
        target_x += DEMO_STEP_X;
        target_y += DEMO_STEP_Y;
        break;
    default:
        pal_player_walking = false;
        return;
    }

    target_x = clamp_int(target_x, 0, max_x);
    target_y = clamp_int(target_y, 0, max_y);
    pal_player_direction = direction;

    if (!viewport_party_position_blocked(target_x, target_y)) {
        pal_viewport_x = target_x;
        pal_viewport_y = target_y;
    }
    pal_player_walking = pal_viewport_x != old_x || pal_viewport_y != old_y;
    if (pal_player_walking) {
        for (i = DEMO_PLAYABLE_PARTY_SLOTS - 1u; i > 0; i--) {
            pal_trail_x[i] = pal_trail_x[i - 1u];
            pal_trail_y[i] = pal_trail_y[i - 1u];
            pal_trail_direction[i] = pal_trail_direction[i - 1u];
        }
        pal_trail_x[0] = source_x;
        pal_trail_y[0] = source_y;
        pal_trail_direction[0] = direction;
    }
}

static uint16_t walk_frame_for_phase(uint16_t walk_frames, bool follower)
{
    uint16_t leader_frame;

    if (!pal_player_walking || walk_frames == 0) {
        return 0;
    }
    if (walk_frames == 4u) {
        return (uint16_t)(pal_player_step_phase & 3u);
    }
    if ((pal_player_step_phase & 1u) == 0) {
        return 0;
    }

    leader_frame = (uint16_t)((pal_player_step_phase + 1u) / 2u);
    return follower ? (uint16_t)(3u - leader_frame) : leader_frame;
}

static void sync_runtime_save_position(void)
{
    uint16_t i;

    if (pal_save_state_size < SAVE_TRAIL_OFFSET + DEMO_PLAYABLE_PARTY_SLOTS * TRAIL_STRUCT_BYTES) {
        return;
    }

    write_le16(pal_psram_save_state + SAVE_SCENE_OFFSET, pal_scene_num);
    write_le16(pal_psram_save_state + SAVE_VIEWPORT_X_OFFSET, (uint16_t)pal_viewport_x);
    write_le16(pal_psram_save_state + SAVE_VIEWPORT_Y_OFFSET, (uint16_t)pal_viewport_y);
    write_le16(pal_psram_save_state + SAVE_PARTY_DIRECTION_OFFSET, pal_player_direction);
    write_le16(pal_psram_save_state + SAVE_LAYER_OFFSET, pal_party_layer);

    for (i = 0; i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
        uint8_t *trail = pal_psram_save_state + SAVE_TRAIL_OFFSET + (uint32_t)i * TRAIL_STRUCT_BYTES;
        uint8_t *party = pal_psram_save_state + SAVE_PARTY_OFFSET + (uint32_t)i * PARTY_STRUCT_BYTES;
        uint16_t walk_frames = pal_party_walk_frames[i] == 0 ? 3u : pal_party_walk_frames[i];
        uint16_t draw_direction = pal_player_direction;
        bool follower = i > pal_max_party_member_index;
        uint16_t frame_num;
        int px = DEMO_PARTY_SCREEN_X;
        int py = DEMO_PARTY_SCREEN_Y;

        write_le16(trail + TRAIL_X_OFFSET, pal_trail_x[i]);
        write_le16(trail + TRAIL_Y_OFFSET, pal_trail_y[i]);
        write_le16(trail + TRAIL_DIRECTION_OFFSET, pal_trail_direction[i]);

        if (i <= visible_party_last_index()) {
            party_member_screen_position(i, &px, &py, &draw_direction);
        }
        frame_num = walk_frame_for_phase(walk_frames, follower);
        write_le16(party + PARTY_X_OFFSET, (uint16_t)px);
        write_le16(party + PARTY_Y_OFFSET, (uint16_t)py);
        write_le16(party + PARTY_FRAME_OFFSET, (uint16_t)(draw_direction * walk_frames + frame_num));
    }
}

static bool persist_runtime_save(const char *reason)
{
    uint16_t saved_times;
    bool ok;

    if (pal_save_slot == 0 || pal_save_state_size == 0) {
        return false;
    }

    sync_runtime_save_position();
    saved_times = read_le16(pal_psram_save_state + SAVE_SAVED_TIMES_OFFSET);
    write_le16(pal_psram_save_state + SAVE_SAVED_TIMES_OFFSET, (uint16_t)(saved_times + 1u));
    set_save_slot_path(pal_save_slot);
    ok = PalSaveFatFs_WriteFile(pal_save_path, pal_psram_save_state, pal_save_state_size);
    CoreS3Se_PrepareLcdAccess();

    if (ok) {
        ESP_LOGI(TAG,
                 "runtime save written: slot=%u bytes=%" PRIu32 " scene=%u reason=%s",
                 (unsigned)pal_save_slot,
                 pal_save_state_size,
                 (unsigned)pal_scene_num,
                 reason != NULL ? reason : "update");
    } else {
        write_le16(pal_psram_save_state + SAVE_SAVED_TIMES_OFFSET, saved_times);
        ESP_LOGW(TAG, "runtime save write failed: %s", pal_save_path);
    }
    return ok;
}

static void update_demo_viewport(bool touched, uint16_t tx, uint16_t ty)
{
    uint16_t local_y;
    int touch_dx;
    int touch_dy;
    uint16_t direction;

    if (!pal_tf_scene_ready ||
        !touched ||
        ty < CORES3SE_PAL_Y_OFFSET ||
        ty >= CORES3SE_PAL_Y_OFFSET + 200u) {
        pal_player_walking = false;
        return;
    }

    local_y = (uint16_t)(ty - CORES3SE_PAL_Y_OFFSET);
    touch_dx = (int)tx - DEMO_PARTY_SCREEN_X;
    touch_dy = (int)local_y - DEMO_PARTY_SCREEN_Y;
    if (abs_int(touch_dx) + abs_int(touch_dy) < DEMO_TOUCH_DEADZONE) {
        pal_player_walking = false;
        if (!pal_touch_scene_gate) {
            trigger_facing_event();
            pal_touch_scene_gate = true;
        }
        return;
    }

    if (touch_dx < 0) {
        direction = touch_dy < 0 ? DEMO_DIR_WEST : DEMO_DIR_SOUTH;
    } else {
        direction = touch_dy < 0 ? DEMO_DIR_NORTH : DEMO_DIR_EAST;
    }
    move_party_direction(direction);
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
    PalPaletteBuffer palette;
    uint32_t source_offset = 0;

    if (pal_nor_ready &&
        PalPack_MapConst(&pal_nor_pack, PAL_PACK_ARCHIVE_PAT, 0, &span) &&
        span.format == PAL_PACK_FORMAT_NATIVE &&
        span.size >= PAL_SRAM_PALETTE_RGB_BYTES) {
        if (pal_palette_night && span.size >= PAL_SRAM_PALETTE_RGB_BYTES * 2u) {
            source_offset = PAL_SRAM_PALETTE_RGB_BYTES;
        }
        if (PalPalette_LoadCurrentRgb6(span.data + source_offset, PAL_SRAM_PALETTE_RGB_BYTES, &palette)) {
            (void)PalVideo_SetPaletteRgb(0, 256, palette.data);
            return;
        }
    }
    load_demo_palette();
}

static bool read_tf_pack_at(void *user, uint32_t offset, uint8_t *dst, uint32_t size)
{
    FIL *file = (FIL *)user;
    uint32_t done = 0;

    if (dst == NULL && size != 0) {
        return false;
    }
    if (file == NULL || !pal_tf_file_open) {
        return false;
    }
    CoreS3Se_PrepareTfAccess();
    while (done < size) {
        UINT got = 0;
        FRESULT res = f_lseek(file, offset + done);
        if (res != FR_OK) {
            return false;
        }
        res = f_read(file, dst + done, size - done, &got);
        if (res != FR_OK || got == 0) {
            return false;
        }
        done += got;
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
    FRESULT res;
    uint32_t pack_size;

    res = f_open(&pal_tf_file, TF_PACK_PATH, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK) {
        ESP_LOGW(TAG, "TF pack missing: %s (%d)", TF_PACK_PATH, (int)res);
        return false;
    }

    pack_size = (uint32_t)f_size(&pal_tf_file);
    if (pack_size == 0 || f_size(&pal_tf_file) > UINT32_MAX) {
        ESP_LOGE(TAG, "bad TF pack size: %s", TF_PACK_PATH);
        f_close(&pal_tf_file);
        return false;
    }
    pal_tf_file_open = true;

    if (!PalPack_OpenTocRead(
            &pal_tf_toc,
            read_tf_pack_at,
            &pal_tf_file,
            pack_size,
            pal_psram_tf_toc,
            PAL_PSRAM_TF_TOC_BYTES)) {
        ESP_LOGE(TAG, "TF pack TOC open failed: %s", TF_PACK_PATH);
        f_close(&pal_tf_file);
        pal_tf_file_open = false;
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
    if (saved_times == NULL) {
        return false;
    }
    set_save_slot_path(slot);
    return PalSaveFatFs_ReadHeader(pal_save_path, saved_times);
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
    PalFatFsSaveSlot save;
    uint32_t size;
    uint16_t scene_num;
    uint16_t viewport_x;
    uint16_t viewport_y;
    uint16_t i;

    pal_initial_viewport_x = 0;
    pal_initial_viewport_y = 0;
    pal_player_role = 0;
    pal_player_direction = DEMO_DIR_SOUTH;
    pal_palette_night = false;
    pal_save_player_roles = NULL;
    pal_save_scenes = NULL;
    pal_save_event_objects = NULL;
    pal_save_event_objects_size = 0;
    pal_save_state_size = 0;
    reset_party_state();

    if (!pal_tf_ready) {
        return false;
    }

    set_save_slot_path(slot);
    if (!PalSaveFatFs_ReadFile(pal_save_path, &save)) {
        ESP_LOGI(TAG, "startup save unavailable: %s", pal_save_path);
        return false;
    }

    size = save.size;
    if (size < SAVE_EVENT_OBJECTS_OFFSET + SSS_EVENT_OBJECT_BYTES) {
        ESP_LOGW(TAG, "startup save size unsupported: %s", pal_save_path);
        return false;
    }

    scene_num = save.scene_num;
    viewport_x = save.viewport_x;
    viewport_y = save.viewport_y;
    if (scene_num == 0 ||
        scene_num >= PAL_SCENE_COUNT ||
        size < SAVE_TRAIL_OFFSET + DEMO_PLAYABLE_PARTY_SLOTS * TRAIL_STRUCT_BYTES ||
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
    reset_party_state();
    pal_party_layer = read_le16(pal_psram_save_state + SAVE_LAYER_OFFSET);
    pal_max_party_member_index = read_le16(pal_psram_save_state + 6u);
    if (pal_max_party_member_index > DEMO_MAX_PARTY_INDEX) {
        pal_max_party_member_index = DEMO_MAX_PARTY_INDEX;
    }
    pal_follower_count = read_le16(pal_psram_save_state + SAVE_FOLLOWER_COUNT_OFFSET);
    if (pal_follower_count > DEMO_PLAYABLE_PARTY_SLOTS - 1u - pal_max_party_member_index) {
        pal_follower_count = DEMO_PLAYABLE_PARTY_SLOTS - 1u - pal_max_party_member_index;
    }
    for (i = 0; i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
        const uint8_t *party = pal_psram_save_state + SAVE_PARTY_OFFSET + (uint32_t)i * PARTY_STRUCT_BYTES;
        const uint8_t *trail = pal_psram_save_state + SAVE_TRAIL_OFFSET + (uint32_t)i * TRAIL_STRUCT_BYTES;
        uint16_t role = read_le16(party + PARTY_ROLE_OFFSET);

        if (i <= pal_max_party_member_index && role >= PLAYER_ROLE_COUNT) {
            role = 0;
        }
        pal_party_roles[i] = role;
        pal_trail_x[i] = read_le16(trail + TRAIL_X_OFFSET);
        pal_trail_y[i] = read_le16(trail + TRAIL_Y_OFFSET);
        pal_trail_direction[i] = read_le16(trail + TRAIL_DIRECTION_OFFSET);
        if (pal_trail_direction[i] > DEMO_DIR_EAST) {
            pal_trail_direction[i] = pal_player_direction;
        }
    }
    pal_player_role = pal_party_roles[0];
    pal_palette_night = read_le16(pal_psram_save_state + SAVE_PALETTE_OFFSET_OFFSET) != 0;
    pal_save_player_roles = pal_psram_save_state + SAVE_PLAYER_ROLES_OFFSET;
    pal_save_scenes = pal_psram_save_state + SAVE_SCENES_OFFSET;
    pal_save_event_objects = pal_psram_save_state + SAVE_EVENT_OBJECTS_OFFSET;
    pal_save_event_objects_size = size - SAVE_EVENT_OBJECTS_OFFSET;
    pal_save_state_size = size;
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

static void load_readonly_global_cache(void)
{
    pal_global_cache = NULL;
    if (!pal_nor_ready) {
        return;
    }
    if (!PalGlobal_LoadReadonly(&pal_nor_pack, &pal_global_cache)) {
        ESP_LOGW(TAG, "PAL readonly global cache load failed");
        pal_global_cache = NULL;
        return;
    }
    ESP_LOGI(TAG,
             "PAL readonly global cache loaded: scripts=%" PRIu32 " stores=%" PRIu32 " enemies=%" PRIu32,
             pal_global_cache->script_entries.count,
             pal_global_cache->stores.count,
             pal_global_cache->enemies.count);
}

static void load_script_cache(void)
{
    PalScriptEntry entry;

    pal_script_ready = false;
    memset(&pal_script_view, 0, sizeof(pal_script_view));
    if (pal_global_cache == NULL) {
        return;
    }

    pal_script_ready = PalScript_OpenFromGlobal(pal_global_cache, &pal_script_view) &&
                       PalScript_Read(&pal_script_view, 2u, &entry) &&
                       entry.operation != 0u;
    if (!pal_script_ready) {
        ESP_LOGW(TAG, "PAL script view load failed");
        return;
    }

    ESP_LOGI(TAG,
             "PAL script view: entries=%" PRIu32 " sample_op=%u sample_arg=%u",
             pal_script_view.count,
             (unsigned)entry.operation,
             (unsigned)entry.operand[0]);
}

static void load_text_font_cache(void)
{
    const uint8_t *sample_text = NULL;
    const uint8_t *sample_glyph = NULL;
    uint32_t sample_text_size = 0;
    uint16_t sample_glyph_size = 0;

    pal_text_ready = false;
    pal_font_ready = false;
    if (!pal_nor_ready) {
        return;
    }

    pal_text_ready = PalText_Open(&pal_nor_pack, &pal_text_cache) &&
                     PalText_GetMessage(&pal_text_cache, 0, &sample_text, &sample_text_size) &&
                     sample_text != NULL &&
                     sample_text_size != 0;
    if (!pal_text_ready) {
        ESP_LOGW(TAG, "PAL text cache load failed");
    }

    pal_font_ready = PalFont_Open(&pal_nor_pack, &pal_font_cache) &&
                     PalFont_FindGlyph(&pal_font_cache, 0x7d93u, &sample_glyph, &sample_glyph_size) &&
                     sample_glyph != NULL &&
                     sample_glyph_size == PAL_FONT_GLYPH_BYTES;
    if (!pal_font_ready) {
        ESP_LOGW(TAG, "PAL font cache load failed");
    }

    ESP_LOGI(TAG,
             "PAL text/font cache: text=%u words=%u messages=%u font=%u glyphs=%u",
             pal_text_ready ? 1u : 0u,
             (unsigned)pal_text_cache.word_count,
             (unsigned)pal_text_cache.message_count,
             pal_font_ready ? 1u : 0u,
             (unsigned)pal_font_cache.glyph_count);
}

static void load_ui_dialog_cache(void)
{
    pal_ui_ready = false;
    pal_dialog_ready = false;
    pal_ui_sprite_asset.data = NULL;
    pal_ui_sprite_asset.size = 0;
    pal_ui_battle_effect_asset.data = NULL;
    pal_ui_battle_effect_asset.size = 0;
    pal_ui_item_sample_asset.data = NULL;
    pal_ui_item_sample_asset.size = 0;
    pal_ui_face_sample_asset.data = NULL;
    pal_ui_face_sample_asset.size = 0;
    pal_dialog_icon_asset.data = NULL;
    pal_dialog_icon_asset.size = 0;

    if (!pal_nor_ready) {
        return;
    }

    pal_ui_ready = PalUi_MapUiSprite(&pal_nor_pack, &pal_ui_sprite_asset) &&
                   PalUi_MapBattleEffect(&pal_nor_pack, &pal_ui_battle_effect_asset) &&
                   PalUi_MapItemBitmap(&pal_nor_pack, 95u, &pal_ui_item_sample_asset) &&
                   PalUi_MapFaceBitmap(&pal_nor_pack, 72u, &pal_ui_face_sample_asset);
    if (!pal_ui_ready) {
        ESP_LOGW(TAG, "PAL UI cache load failed");
    }

    pal_dialog_ready = PalDialog_MapIcons(&pal_nor_pack, &pal_dialog_icon_asset);
    if (!pal_dialog_ready) {
        ESP_LOGW(TAG, "PAL dialog cache load failed");
    }

    ESP_LOGI(TAG,
             "PAL UI/dialog cache: ui=%u ui_bytes=%" PRIu32 " effect=%" PRIu32 " item=%" PRIu32 " face=%" PRIu32 " dialog=%u icons=%" PRIu32,
             pal_ui_ready ? 1u : 0u,
             pal_ui_sprite_asset.size,
             pal_ui_battle_effect_asset.size,
             pal_ui_item_sample_asset.size,
             pal_ui_face_sample_asset.size,
             pal_dialog_ready ? 1u : 0u,
             pal_dialog_icon_asset.size);
}

static void load_menu_cache(void)
{
    pal_menu_ready = false;
    pal_menu_background_buffer.data = NULL;
    pal_menu_background_buffer.size = 0;
    pal_menu_image_buffer.data = NULL;
    pal_menu_image_buffer.size = 0;
    pal_menu_box_buffer.data = NULL;
    pal_menu_box_buffer.size = 0;
    pal_menu_item_asset.data = NULL;
    pal_menu_item_asset.size = 0;

    if (!pal_nor_ready || !pal_tf_ready) {
        return;
    }

    pal_menu_ready = PalMenu_LoadBackgroundReadAt(
                         &pal_tf_toc,
                         read_tf_pack_at,
                         &pal_tf_file,
                         0,
                         &pal_menu_background_buffer) &&
                     PalMenu_CopyImage(&pal_nor_pack, PAL_PACK_ARCHIVE_RGM, 72u, &pal_menu_image_buffer) &&
                     PalMenu_MapImage(&pal_nor_pack, PAL_PACK_ARCHIVE_BALL, 95u, &pal_menu_item_asset) &&
                     PalMenu_PrepareBox(72u, 72u, 0x5au, &pal_menu_box_buffer);
    if (!pal_menu_ready) {
        ESP_LOGW(TAG, "PAL menu cache load failed");
    }

    ESP_LOGI(TAG,
             "PAL menu cache: ready=%u fbp=%" PRIu32 " image=%" PRIu32 " item=%" PRIu32 " box=%" PRIu32,
             pal_menu_ready ? 1u : 0u,
             pal_menu_background_buffer.size,
             pal_menu_image_buffer.size,
             pal_menu_item_asset.size,
             pal_menu_box_buffer.size);
}

static void load_battle_cache(void)
{
    pal_battle_ready = false;
    memset(&pal_battle_snapshot, 0, sizeof(pal_battle_snapshot));
    pal_battle_effect_buffer.data = NULL;
    pal_battle_effect_buffer.size = 0;

    if (!pal_nor_ready || !pal_tf_ready) {
        return;
    }

    pal_battle_ready = PalBattle_LoadSnapshotReadAt(
                           &pal_nor_pack,
                           &pal_tf_toc,
                           read_tf_pack_at,
                           &pal_tf_file,
                           156u,
                           0u,
                           pal_battle_sample_player_sprites,
                           3u,
                           7u,
                           &pal_battle_snapshot) &&
                       PalBattle_LoadEffectScratch(&pal_nor_pack, 37u, &pal_battle_effect_buffer);
    if (!pal_battle_ready) {
        ESP_LOGW(TAG, "PAL battle cache load failed");
    }

    ESP_LOGI(TAG,
             "PAL battle cache: ready=%u bg=%" PRIu32 " players=%u player_bytes=%" PRIu32 " enemies=%u unique=%u enemy_bytes=%" PRIu32 " effect=%" PRIu32 " scratch=%" PRIu32,
             pal_battle_ready ? 1u : 0u,
             pal_battle_snapshot.background_size,
             (unsigned)pal_battle_snapshot.player_count,
             pal_battle_snapshot.player_sprite_bytes,
             (unsigned)pal_battle_snapshot.enemy_ref_count,
             (unsigned)pal_battle_snapshot.unique_enemy_sprite_count,
             pal_battle_snapshot.unique_enemy_sprite_bytes,
             pal_battle_snapshot.effect_size,
             pal_battle_effect_buffer.size);
}

static void load_rng_cache(void)
{
    pal_rng_ready = false;
    memset(&pal_rng_movie, 0, sizeof(pal_rng_movie));
    memset(&pal_rng_frame_a, 0, sizeof(pal_rng_frame_a));
    memset(&pal_rng_frame_b, 0, sizeof(pal_rng_frame_b));

    if (!pal_tf_ready) {
        return;
    }

    pal_rng_ready = PalRng_OpenMovieReadAt(&pal_tf_toc, read_tf_pack_at, &pal_tf_file, 4u, &pal_rng_movie) &&
                    PalRng_LoadFrameReadAt(
                        &pal_rng_movie,
                        read_tf_pack_at,
                        &pal_tf_file,
                        0u,
                        PAL_RNG_FRAME_BUFFER_A,
                        &pal_rng_frame_a) &&
                    PalRng_LoadFrameReadAt(
                        &pal_rng_movie,
                        read_tf_pack_at,
                        &pal_tf_file,
                        1u,
                        PAL_RNG_FRAME_BUFFER_B,
                        &pal_rng_frame_b);
    if (!pal_rng_ready) {
        ESP_LOGW(TAG, "PAL RNG cache load failed");
    }

    ESP_LOGI(TAG,
             "PAL RNG cache: ready=%u movie=%u frames=%u table=%" PRIu32 " frame_a=%" PRIu32 " frame_b=%" PRIu32,
             pal_rng_ready ? 1u : 0u,
             (unsigned)pal_rng_movie.movie_num,
             (unsigned)pal_rng_movie.frame_count,
             pal_rng_movie.table_size,
             pal_rng_frame_a.size,
             pal_rng_frame_b.size);
}

static void load_music_cache(void)
{
    pal_music_ready = false;
    memset(&pal_midi_sample_track, 0, sizeof(pal_midi_sample_track));
    memset(&pal_mus_sample_track, 0, sizeof(pal_mus_sample_track));

    if (!pal_nor_ready) {
        return;
    }

    pal_music_ready = PalMusic_MapMidi(&pal_nor_pack, 31u, &pal_midi_sample_track) &&
                      PalMusic_MapMus(&pal_nor_pack, 31u, &pal_mus_sample_track);
    if (!pal_music_ready) {
        ESP_LOGW(TAG, "PAL music cache load failed");
    }

    ESP_LOGI(TAG,
             "PAL music cache: ready=%u midi=%" PRIu32 " mus=%" PRIu32,
             pal_music_ready ? 1u : 0u,
             pal_midi_sample_track.size,
             pal_mus_sample_track.size);
}

static void load_ending_cache(void)
{
    pal_ending_ready = false;
    memset(&pal_ending_pair, 0, sizeof(pal_ending_pair));
    pal_ending_sprite_asset.data = NULL;
    pal_ending_sprite_asset.size = 0;

    if (!pal_nor_ready || !pal_tf_ready) {
        return;
    }

    pal_ending_ready = PalEnding_LoadFbpPairReadAt(
                           &pal_tf_toc,
                           read_tf_pack_at,
                           &pal_tf_file,
                           61u,
                           62u,
                           &pal_ending_pair) &&
                       PalEnding_MapSprite(&pal_nor_pack, 571u, &pal_ending_sprite_asset);
    if (!pal_ending_ready) {
        ESP_LOGW(TAG, "PAL ending cache load failed");
    }

    ESP_LOGI(TAG,
             "PAL ending cache: ready=%u upper=%" PRIu32 " lower=%" PRIu32 " sprite=%" PRIu32,
             pal_ending_ready ? 1u : 0u,
             pal_ending_pair.upper.size,
             pal_ending_pair.lower.size,
             pal_ending_sprite_asset.size);
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

static bool write_player_role_word(uint32_t field_offset, uint16_t role, uint16_t value)
{
    uint8_t *player_roles;
    uint32_t player_roles_size;
    uint32_t offset = field_offset + (uint32_t)role * 2u;

    if (pal_save_player_roles != NULL) {
        player_roles = pal_save_player_roles;
        player_roles_size = SAVE_PLAYER_ROLES_BYTES;
    } else if (pal_global_cache != NULL && pal_global_cache->player_roles.data != NULL) {
        player_roles = pal_global_cache->player_roles.data;
        player_roles_size = pal_global_cache->player_roles.size;
    } else {
        return false;
    }

    if (role >= PLAYER_ROLE_COUNT ||
        offset > player_roles_size ||
        2u > player_roles_size - offset) {
        return false;
    }
    write_le16(player_roles + offset, value);
    return true;
}

static uint16_t player_role_word_by_index(uint16_t word_index, uint16_t role)
{
    return player_role_word((uint32_t)word_index * PLAYER_ROLE_WORD_ARRAY_BYTES, role);
}

static bool write_player_role_word_by_index(uint16_t word_index, uint16_t role, uint16_t value)
{
    return write_player_role_word((uint32_t)word_index * PLAYER_ROLE_WORD_ARRAY_BYTES, role, value);
}

static uint16_t clamp_player_role_stat(int value)
{
    return (uint16_t)clamp_int(value, 0, 9999);
}

static bool change_player_role_hpmp(uint16_t role, int hp_delta, int mp_delta)
{
    uint16_t hp;
    uint16_t mp;
    uint16_t max_hp;
    uint16_t max_mp;

    if (role >= PLAYER_ROLE_COUNT) {
        return false;
    }
    hp = player_role_word_by_index(PLAYER_ROLE_HP_WORD_INDEX, role);
    mp = player_role_word_by_index(PLAYER_ROLE_MP_WORD_INDEX, role);
    max_hp = player_role_word_by_index(PLAYER_ROLE_MAX_HP_WORD_INDEX, role);
    max_mp = player_role_word_by_index(PLAYER_ROLE_MAX_MP_WORD_INDEX, role);

    hp = clamp_player_role_stat((int)hp + hp_delta);
    mp = clamp_player_role_stat((int)mp + mp_delta);
    if (max_hp != 0 && hp > max_hp) {
        hp = max_hp;
    }
    if (max_mp != 0 && mp > max_mp) {
        mp = max_mp;
    }
    return write_player_role_word_by_index(PLAYER_ROLE_HP_WORD_INDEX, role, hp) &&
           write_player_role_word_by_index(PLAYER_ROLE_MP_WORD_INDEX, role, mp);
}

static bool save_inventory_available(void)
{
    return pal_save_state_size >= SAVE_INVENTORY_OFFSET + SAVE_INVENTORY_BYTES;
}

static uint8_t *inventory_slot(uint16_t index)
{
    if (!save_inventory_available() || index >= SAVE_INVENTORY_BYTES / INVENTORY_SLOT_BYTES) {
        return NULL;
    }
    return pal_psram_save_state + SAVE_INVENTORY_OFFSET + (uint32_t)index * INVENTORY_SLOT_BYTES;
}

static bool find_inventory_slot(uint16_t item_id, uint16_t *slot_index, bool *found)
{
    uint16_t i;

    if (slot_index == NULL || found == NULL || item_id == 0 || !save_inventory_available()) {
        return false;
    }
    *slot_index = 0;
    *found = false;

    for (i = 0; i < SAVE_INVENTORY_BYTES / INVENTORY_SLOT_BYTES; i++) {
        uint8_t *slot = inventory_slot(i);
        uint16_t item;

        if (slot == NULL) {
            return false;
        }
        item = read_le16(slot + INVENTORY_ITEM_OFFSET);
        if (item == item_id) {
            *slot_index = i;
            *found = true;
            return true;
        }
        if (item == 0) {
            *slot_index = i;
            return true;
        }
    }
    *slot_index = SAVE_INVENTORY_BYTES / INVENTORY_SLOT_BYTES;
    return true;
}

static uint16_t inventory_item_amount(uint16_t item_id)
{
    uint16_t slot_index;
    bool found;
    uint8_t *slot;

    if (!find_inventory_slot(item_id, &slot_index, &found) || !found) {
        return 0;
    }
    slot = inventory_slot(slot_index);
    return slot != NULL ? read_le16(slot + INVENTORY_AMOUNT_OFFSET) : 0;
}

static bool add_inventory_item(uint16_t item_id, int amount)
{
    uint16_t slot_index;
    bool found;
    uint8_t *slot;
    int current;

    if (amount == 0) {
        amount = 1;
    }
    if (!find_inventory_slot(item_id, &slot_index, &found)) {
        return false;
    }
    slot = inventory_slot(slot_index);
    if (slot == NULL) {
        return false;
    }
    if (amount > 0) {
        if (!found && slot_index >= SAVE_INVENTORY_BYTES / INVENTORY_SLOT_BYTES) {
            return false;
        }
        current = found ? read_le16(slot + INVENTORY_AMOUNT_OFFSET) : 0;
        current = clamp_int(current + amount, 0, 99);
        write_le16(slot + INVENTORY_ITEM_OFFSET, item_id);
        write_le16(slot + INVENTORY_AMOUNT_OFFSET, (uint16_t)current);
        write_le16(slot + INVENTORY_IN_USE_OFFSET, 0);
        return true;
    }

    if (!found) {
        return false;
    }
    current = (int)read_le16(slot + INVENTORY_AMOUNT_OFFSET) + amount;
    if (current <= 0) {
        write_le16(slot + INVENTORY_ITEM_OFFSET, 0);
        write_le16(slot + INVENTORY_AMOUNT_OFFSET, 0);
        write_le16(slot + INVENTORY_IN_USE_OFFSET, 0);
        return current == 0;
    }
    write_le16(slot + INVENTORY_AMOUNT_OFFSET, (uint16_t)current);
    return true;
}

static void load_player_sprite(void)
{
    uint16_t i;
    uint16_t last_index = visible_party_last_index();
    uint32_t pin_start = pal_tf_scene_ready ? align4_u32(pal_scene_snapshot.sprite_pin_bytes) : 0;
    uint32_t pin_cursor = pin_start;

    pal_player_sprite = NULL;
    pal_player_sprite_size = 0;
    pal_party_sprite_pin_bytes = 0;
    pal_player_walk_frames = 3;
    pal_player_step_phase = 0;
    pal_player_walking = false;
    for (i = 0; i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
        pal_party_sprites[i] = NULL;
        pal_party_sprite_sizes[i] = 0;
        pal_party_walk_frames[i] = 3;
    }

    if (!pal_nor_ready) {
        return;
    }

    for (i = 0; i <= last_index && i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
        PalPackSpan span;
        uint16_t role = pal_party_roles[i];
        uint16_t sprite_num = role;

        if (i <= pal_max_party_member_index) {
            sprite_num = player_role_word(PLAYER_ROLE_SPRITE_NUM_OFFSET, role);
            pal_party_walk_frames[i] = player_role_word(PLAYER_ROLE_WALK_FRAMES_OFFSET, role);
        } else {
            pal_party_walk_frames[i] = 3;
        }
        if (pal_party_walk_frames[i] == 0 || pal_party_walk_frames[i] > 4u) {
            pal_party_walk_frames[i] = 3;
        }

        if (PalPack_MapConst(&pal_nor_pack, PAL_PACK_ARCHIVE_MGO, sprite_num, &span) &&
            span.format == PAL_PACK_FORMAT_NATIVE &&
            span.data != NULL &&
            span.size != 0) {
            if (pal_tf_scene_ready) {
                uint32_t pin_offset = align4_u32(pin_cursor);

                if (pin_offset <= PAL_PSRAM_SPRITE_PIN_BYTES &&
                    span.size <= PAL_PSRAM_SPRITE_PIN_BYTES - pin_offset) {
                    memcpy(pal_psram_sprite_pin + pin_offset, span.data, span.size);
                    pin_cursor = pin_offset + span.size;
                    span.data = pal_psram_sprite_pin + pin_offset;
                }
            }
            pal_party_sprites[i] = span.data;
            pal_party_sprite_sizes[i] = span.size;
        }
    }

    if (pin_cursor > pin_start) {
        pal_party_sprite_pin_bytes = pin_cursor - pin_start;
    }
    pal_player_sprite = pal_party_sprites[0];
    pal_player_sprite_size = pal_party_sprite_sizes[0];
    pal_player_walk_frames = pal_party_walk_frames[0];
    ESP_LOGI(TAG, "party sprites loaded: members=%u followers=%u leader_role=%u leader_bytes=%" PRIu32 " pinned=%" PRIu32 " walk_frames=%u",
             (unsigned)(last_index + 1u),
             (unsigned)pal_follower_count,
             (unsigned)pal_player_role,
             pal_player_sprite_size,
             pal_party_sprite_pin_bytes,
             (unsigned)pal_player_walk_frames);
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

static const uint8_t *current_scene_record(void)
{
    const uint8_t *scenes = NULL;
    uint32_t scenes_size = 0;
    uint32_t offset;

    if (pal_scene_num == 0 || pal_scene_num > PAL_SCENE_COUNT) {
        return NULL;
    }
    if (pal_save_scenes != NULL) {
        scenes = pal_save_scenes;
        scenes_size = SAVE_SCENES_BYTES;
    } else if (pal_global_cache != NULL && pal_global_cache->scenes.data != NULL) {
        scenes = pal_global_cache->scenes.data;
        scenes_size = pal_global_cache->scenes.size;
    }
    if (scenes == NULL) {
        return NULL;
    }

    offset = (uint32_t)(pal_scene_num - 1u) * PAL_GLOBAL_SCENE_BYTES;
    if (offset > scenes_size || PAL_GLOBAL_SCENE_BYTES > scenes_size - offset) {
        return NULL;
    }
    return scenes + offset;
}

static uint8_t *mutable_scene_record(uint16_t scene_num)
{
    uint8_t *scenes = NULL;
    uint32_t scenes_size = 0;
    uint32_t offset;

    if (scene_num == 0 || scene_num > PAL_SCENE_COUNT) {
        return NULL;
    }
    if (pal_save_scenes != NULL) {
        scenes = (uint8_t *)pal_save_scenes;
        scenes_size = SAVE_SCENES_BYTES;
    } else if (pal_global_cache != NULL && pal_global_cache->scenes.data != NULL) {
        scenes = pal_global_cache->scenes.data;
        scenes_size = pal_global_cache->scenes.size;
    }
    if (scenes == NULL) {
        return NULL;
    }

    offset = (uint32_t)(scene_num - 1u) * PAL_GLOBAL_SCENE_BYTES;
    if (offset > scenes_size || PAL_GLOBAL_SCENE_BYTES > scenes_size - offset) {
        return NULL;
    }
    return scenes + offset;
}

static uint8_t *mutable_current_scene_record(void)
{
    return mutable_scene_record(pal_scene_num);
}

static uint16_t trace_script_steps(uint16_t script_entry, uint16_t *first_operation)
{
    PalScriptTrace trace;

    if (first_operation != NULL) {
        *first_operation = 0;
    }
    if (!pal_script_ready || script_entry == 0 ||
        !PalScript_TraceLinear(&pal_script_view, script_entry, 32u, &trace)) {
        return 0;
    }
    if (first_operation != NULL) {
        *first_operation = trace.first_operation;
    }
    return trace.step_count;
}

static void log_scene_script_summary(void)
{
    const uint8_t *scene = current_scene_record();
    uint16_t enter_script = 0;
    uint16_t teleport_script = 0;
    uint16_t enter_op = 0;
    uint16_t enter_steps = 0;
    uint16_t trigger_count = 0;
    uint16_t auto_count = 0;
    uint16_t first_trigger = 0;
    uint16_t first_auto = 0;
    uint16_t i;

    if (!pal_script_ready) {
        return;
    }
    if (scene != NULL) {
        enter_script = read_le16(scene + SCENE_SCRIPT_ON_ENTER_OFFSET);
        teleport_script = read_le16(scene + SCENE_SCRIPT_ON_TELEPORT_OFFSET);
    }

    if (pal_scene_event_objects != NULL) {
        for (i = 0; i < pal_scene_snapshot.event_count; i++) {
            uint32_t offset = ((uint32_t)pal_scene_snapshot.event_start + i) * SSS_EVENT_OBJECT_BYTES;
            uint16_t trigger_script;
            uint16_t auto_script;

            if (offset > pal_scene_event_objects_size ||
                SSS_EVENT_OBJECT_BYTES > pal_scene_event_objects_size - offset) {
                break;
            }
            trigger_script = read_le16(pal_scene_event_objects + offset + EVENT_TRIGGER_SCRIPT_OFFSET);
            auto_script = read_le16(pal_scene_event_objects + offset + EVENT_AUTO_SCRIPT_OFFSET);
            if (trigger_script != 0) {
                if (first_trigger == 0) {
                    first_trigger = trigger_script;
                }
                trigger_count++;
            }
            if (auto_script != 0) {
                if (first_auto == 0) {
                    first_auto = auto_script;
                }
                auto_count++;
            }
        }
    }
    enter_steps = trace_script_steps(enter_script, &enter_op);

    ESP_LOGI(TAG,
             "scene scripts: scene=%u enter=%u enter_steps=%u enter_op=%u teleport=%u trigger_events=%u first_trigger=%u auto_events=%u first_auto=%u",
             (unsigned)pal_scene_num,
             (unsigned)enter_script,
             (unsigned)enter_steps,
             (unsigned)enter_op,
             (unsigned)teleport_script,
             (unsigned)trigger_count,
             (unsigned)first_trigger,
             (unsigned)auto_count,
             (unsigned)first_auto);
}

static void load_tf_scene_chunks(void)
{
    PalPackSpan event_span;
    const uint8_t *event_objects = NULL;
    uint32_t event_objects_size = 0;
    bool event_objects_mutable = false;
    uint16_t loaded_scene = pal_scene_num;

    pal_tf_scene_ready = false;
    pal_scene_event_objects = NULL;
    pal_scene_event_objects_size = 0;
    pal_scene_event_objects_mutable = false;
    pal_scene_script_idle = 0;
    pal_dialog_visible = false;

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
        if (!PalScene_LoadPinnedSnapshotReadAtWithSceneData(
                &pal_nor_pack,
                &pal_tf_toc,
                read_tf_pack_at,
                &pal_tf_file,
                &pal_nor_pack,
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
        if (!PalScene_LoadPinnedSnapshotReadAtWithEvents(
                &pal_nor_pack,
                &pal_tf_toc,
                read_tf_pack_at,
                &pal_tf_file,
                &pal_nor_pack,
                event_objects,
                event_objects_size,
                pal_scene_num,
                &pal_scene_snapshot)) {
            ESP_LOGW(TAG, "TF scene snapshot load failed");
            return;
        }
    } else if (!PalScene_LoadPinnedSnapshotReadAt(
                &pal_nor_pack,
                &pal_tf_toc,
                read_tf_pack_at,
                &pal_tf_file,
                &pal_nor_pack,
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
             "TF scene loaded: scene=%u map=%u events=%u unique_sprites=%u sprite_bytes=%" PRIu32 " pinned=%" PRIu32 " gop=%" PRIu32 " mark=0x%08" PRIx32,
             (unsigned)pal_scene_snapshot.scene_num,
             (unsigned)pal_scene_snapshot.map_num,
             (unsigned)pal_scene_snapshot.event_count,
             (unsigned)pal_scene_snapshot.unique_sprite_count,
             pal_scene_snapshot.unique_sprite_bytes,
             pal_scene_snapshot.sprite_pin_bytes,
             pal_scene_snapshot.gop_size,
             pal_tf_scene_checksum);
    log_scene_script_summary();
    run_scene_enter_script_subset();
    if (pal_scene_num != loaded_scene && pal_scene_enter_reload_depth < 3u) {
        pal_scene_enter_reload_depth++;
        load_tf_scene_chunks();
        pal_scene_enter_reload_depth--;
        return;
    }
    load_player_sprite();
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

    pal_initial_viewport_x = 0;
    pal_initial_viewport_y = 0;
    pal_scene_num = (uint16_t)next_scene;
    load_tf_scene_chunks();
    if (pal_tf_scene_ready) {
        (void)persist_runtime_save("scene");
    }
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

static bool update_dialog_touch(bool touched)
{
    if (!touched) {
        pal_dialog_touch_gate = false;
        return false;
    }
    if (!pal_dialog_visible) {
        return false;
    }
    if (!pal_dialog_touch_gate) {
        pal_dialog_visible = false;
        pal_dialog_touch_gate = true;
    }
    pal_player_walking = false;
    return true;
}

static uint8_t *mutable_event_object_by_id(uint16_t event_object_id)
{
    uint32_t offset;

    if (!pal_scene_event_objects_mutable || pal_scene_event_objects == NULL || event_object_id == 0) {
        return NULL;
    }
    offset = (uint32_t)(event_object_id - 1u) * SSS_EVENT_OBJECT_BYTES;
    if (offset > pal_scene_event_objects_size || SSS_EVENT_OBJECT_BYTES > pal_scene_event_objects_size - offset) {
        return NULL;
    }
    return (uint8_t *)pal_scene_event_objects + offset;
}

static uint8_t *script_current_event_object(uint16_t current_event_object_id, uint16_t operand0)
{
    uint16_t event_object_id;
    int index;

    if (operand0 == 0 || operand0 == 0xFFFFu) {
        event_object_id = current_event_object_id;
    } else {
        index = (int)operand0 - 1;
        if (index > 0x9000) {
            index -= 0x9000;
        }
        if (index < 0 || index >= 0xFFFF) {
            return NULL;
        }
        event_object_id = (uint16_t)(index + 1);
    }
    return mutable_event_object_by_id(event_object_id);
}

static void advance_event_object_frame(uint8_t *event_object)
{
    uint16_t sprite_frames;
    uint16_t frame;

    if (event_object == NULL) {
        return;
    }
    sprite_frames = read_le16(event_object + EVENT_SPRITE_FRAMES_OFFSET);
    if (sprite_frames == 0) {
        return;
    }
    frame = read_le16(event_object + EVENT_CURRENT_FRAME_OFFSET);
    frame = (uint16_t)((frame + 1u) % sprite_frames);
    write_le16(event_object + EVENT_CURRENT_FRAME_OFFSET, frame);
}

static void set_party_world_position(int world_x, int world_y, uint16_t layer)
{
    uint16_t i;

    pal_viewport_x = clamp_int(world_x - DEMO_PARTY_SCREEN_X, 0, DEMO_MAP_PIXEL_WIDTH - 320);
    pal_viewport_y = clamp_int(world_y - DEMO_PARTY_SCREEN_Y, 0, DEMO_MAP_PIXEL_HEIGHT - 200);
    pal_initial_viewport_x = pal_viewport_x;
    pal_initial_viewport_y = pal_viewport_y;
    pal_party_layer = layer;

    for (i = 0; i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
        pal_trail_x[i] = (uint16_t)clamp_int(world_x, 0, DEMO_MAP_PIXEL_WIDTH - 1);
        pal_trail_y[i] = (uint16_t)clamp_int(world_y, 0, DEMO_MAP_PIXEL_HEIGHT - 1);
        pal_trail_direction[i] = pal_player_direction;
    }
}

static void move_party_world_offset(int dx, int dy, uint16_t layer)
{
    int world_x = pal_viewport_x + DEMO_PARTY_SCREEN_X + dx;
    int world_y = pal_viewport_y + DEMO_PARTY_SCREEN_Y + dy;

    set_party_world_position(world_x, world_y, layer);
}

static uint16_t script_random_percent(void)
{
    pal_script_rng_state = pal_script_rng_state * 1103515245u + 12345u;
    return (uint16_t)(((pal_script_rng_state >> 16) % 100u) + 1u);
}

static void event_step_delta(uint16_t direction, int *dx, int *dy)
{
    *dx = 0;
    *dy = 0;
    switch (direction) {
    case DEMO_DIR_SOUTH:
        *dx = -DEMO_STEP_X;
        *dy = DEMO_STEP_Y;
        break;
    case DEMO_DIR_WEST:
        *dx = -DEMO_STEP_X;
        *dy = -DEMO_STEP_Y;
        break;
    case DEMO_DIR_NORTH:
        *dx = DEMO_STEP_X;
        *dy = -DEMO_STEP_Y;
        break;
    case DEMO_DIR_EAST:
        *dx = DEMO_STEP_X;
        *dy = DEMO_STEP_Y;
        break;
    default:
        break;
    }
}

static void move_event_object_one_step(uint8_t *event_object, uint16_t direction)
{
    int dx;
    int dy;

    if (event_object == NULL || direction > DEMO_DIR_EAST) {
        return;
    }
    event_step_delta(direction, &dx, &dy);
    write_le16(event_object + EVENT_DIRECTION_OFFSET, direction);
    write_le16(event_object + EVENT_X_OFFSET, (uint16_t)(read_s16(event_object + EVENT_X_OFFSET) + (int16_t)dx));
    write_le16(event_object + EVENT_Y_OFFSET, (uint16_t)(read_s16(event_object + EVENT_Y_OFFSET) + (int16_t)dy));
    advance_event_object_frame(event_object);
}

static void set_event_object_position(uint8_t *event_object, uint16_t x, uint16_t y, uint16_t layer)
{
    if (event_object == NULL) {
        return;
    }
    write_le16(event_object + EVENT_X_OFFSET, x);
    write_le16(event_object + EVENT_Y_OFFSET, y);
    write_le16(event_object + EVENT_LAYER_OFFSET, layer);
    advance_event_object_frame(event_object);
}

static uint16_t execute_script_mutation(
    uint16_t script_entry,
    uint16_t event_object_id,
    uint16_t idle_offset,
    bool trigger_mode)
{
    uint8_t *event_object = mutable_event_object_by_id(event_object_id);
    uint8_t *current;
    uint16_t steps;
    uint16_t max_steps = trigger_mode ? SCRIPT_TRIGGER_MAX_STEPS : SCRIPT_AUTO_MAX_JUMPS;
    bool scene_script = event_object_id == 0xFFFFu;

    if (!pal_script_ready || (!scene_script && event_object == NULL) || script_entry == 0) {
        return script_entry;
    }

    for (steps = 0; steps < max_steps && script_entry != 0; steps++) {
        PalScriptEntry entry;
        uint16_t idle = scene_script ? pal_scene_script_idle : read_le16(event_object + idle_offset);

        if (!PalScript_Read(&pal_script_view, script_entry, &entry)) {
            return script_entry;
        }
        current = scene_script ? NULL : script_current_event_object(event_object_id, entry.operand[0]);

        switch (entry.operation) {
        case SCRIPT_STOP:
            return script_entry;

        case SCRIPT_STOP_NEXT:
            return (uint16_t)(script_entry + 1u);

        case SCRIPT_CALL_TRIGGER:
        {
            uint16_t nested_event_id = entry.operand[1] == 0 ? event_object_id : entry.operand[1];

            if (entry.operand[0] != 0 && pal_script_call_depth < SCRIPT_CALL_MAX_DEPTH) {
                pal_script_call_depth++;
                (void)execute_script_mutation(entry.operand[0], nested_event_id, EVENT_TRIGGER_IDLE_OFFSET, true);
                pal_script_call_depth--;
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_RANDOM_BRANCH:
            if (entry.operand[1] != 0 && script_random_percent() >= entry.operand[0]) {
                script_entry = entry.operand[1];
            } else {
                script_entry = (uint16_t)(script_entry + 1u);
            }
            continue;

        case SCRIPT_START_BATTLE:
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_REPLACE_WITH_NEXT:
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_STOP_GOTO:
            if (entry.operand[1] == 0 || (uint16_t)(idle + 1u) < entry.operand[1]) {
                if (scene_script) {
                    pal_scene_script_idle = (uint16_t)(idle + 1u);
                } else {
                    write_le16(event_object + idle_offset, (uint16_t)(idle + 1u));
                }
                return entry.operand[0];
            }
            if (scene_script) {
                pal_scene_script_idle = 0;
            } else {
                write_le16(event_object + idle_offset, 0);
            }
            return (uint16_t)(script_entry + 1u);

        case SCRIPT_GOTO:
            if (entry.operand[1] == 0 || (uint16_t)(idle + 1u) < entry.operand[1]) {
                if (scene_script) {
                    pal_scene_script_idle = (uint16_t)(idle + 1u);
                } else {
                    write_le16(event_object + idle_offset, (uint16_t)(idle + 1u));
                }
                script_entry = entry.operand[0];
                continue;
            }
            if (scene_script) {
                pal_scene_script_idle = 0;
            } else {
                write_le16(event_object + idle_offset, 0);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            continue;

        case SCRIPT_WAIT:
            if ((uint16_t)(idle + 1u) >= entry.operand[0]) {
                if (scene_script) {
                    pal_scene_script_idle = 0;
                } else {
                    write_le16(event_object + idle_offset, 0);
                }
                return (uint16_t)(script_entry + 1u);
            }
            if (scene_script) {
                pal_scene_script_idle = (uint16_t)(idle + 1u);
            } else {
                write_le16(event_object + idle_offset, (uint16_t)(idle + 1u));
            }
            return script_entry;

        case SCRIPT_DIALOG_CENTER:
            pal_dialog_mode = PAL_DIALOG_MODE_CENTER;
            pal_dialog_visible = false;
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_DIALOG_UPPER:
            pal_dialog_mode = PAL_DIALOG_MODE_UPPER;
            pal_dialog_visible = false;
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_DIALOG_LOWER:
            pal_dialog_mode = PAL_DIALOG_MODE_LOWER;
            pal_dialog_visible = false;
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_DIALOG_CENTER_WINDOW:
            pal_dialog_mode = PAL_DIALOG_MODE_CENTER_WINDOW;
            pal_dialog_visible = false;
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_REDRAW:
        case SCRIPT_SHAKE_SCREEN:
        case SCRIPT_REMOVE_EQUIPMENT:
        case SCRIPT_BUY_MENU:
        case SCRIPT_SELL_MENU:
        case SCRIPT_SET_CURRENT_RNG:
        case SCRIPT_PLAY_RNG:
        case SCRIPT_SET_BATTLEFIELD:
        case SCRIPT_CHASE_PLAYER:
        case SCRIPT_FADE_TO_SCENE:
        case SCRIPT_SCREEN_WAVE:
        case SCRIPT_JUMP_IF_NOT_FULL_HP:
        case SCRIPT_SHOW_FBP:
        case SCRIPT_STOP_MUSIC:
        case SCRIPT_UNKNOWN_0078:
        case SCRIPT_JUMP_IF_ITEM_NOT_EQUIPPED:
        case SCRIPT_SCENE_FADE:
        case SCRIPT_FADE_CURRENT_SCENE:
        case SCRIPT_CHANGE_PALETTE:
        case SCRIPT_ADD_MAGIC:
        case SCRIPT_PLAY_CD_MUSIC:
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_PLAYER_STAT:
        {
            uint16_t role = entry.operand[2] == 0 ? event_object_id : (uint16_t)(entry.operand[2] - 1u);

            if (role < PLAYER_ROLE_COUNT) {
                (void)write_player_role_word_by_index(entry.operand[0], role, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_CHANGE_HP_MP:
            if (entry.operand[0] != 0) {
                uint16_t i;

                for (i = 0; i <= pal_max_party_member_index && i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
                    (void)change_player_role_hpmp(pal_party_roles[i], (int16_t)entry.operand[1], (int16_t)entry.operand[1]);
                }
            } else {
                (void)change_player_role_hpmp(event_object_id, (int16_t)entry.operand[1], (int16_t)entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_ADD_CASH:
            if (pal_save_state_size >= SAVE_CASH_OFFSET + 4u) {
                uint32_t cash = read_le32(pal_psram_save_state + SAVE_CASH_OFFSET);
                int delta = (int16_t)entry.operand[0];

                if (delta < 0 && cash < (uint32_t)-delta && entry.operand[1] != 0) {
                    script_entry = entry.operand[1];
                    continue;
                }
                if (delta < 0 && cash < (uint32_t)-delta) {
                    cash = 0;
                } else if (delta > 0 && cash > UINT32_MAX - (uint32_t)delta) {
                    cash = UINT32_MAX;
                } else {
                    cash = (uint32_t)(cash + delta);
                }
                write_le32(pal_psram_save_state + SAVE_CASH_OFFSET, cash);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_ADD_ITEM:
            (void)add_inventory_item(entry.operand[0], (int16_t)entry.operand[1]);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_REMOVE_ITEM:
        {
            int amount = entry.operand[1] != 0 ? (int)entry.operand[1] : 1;
            bool enough = inventory_item_amount(entry.operand[0]) >= amount;

            if (!enough && entry.operand[2] != 0) {
                script_entry = entry.operand[2];
                continue;
            }
            (void)add_inventory_item(entry.operand[0], -amount);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_JUMP_IF_ITEM_LESS:
            if (inventory_item_amount(entry.operand[0]) < entry.operand[1] && entry.operand[2] != 0) {
                script_entry = entry.operand[2];
            } else {
                script_entry = (uint16_t)(script_entry + 1u);
            }
            continue;

        case SCRIPT_WALK_SOUTH:
        case SCRIPT_WALK_WEST:
        case SCRIPT_WALK_NORTH:
        case SCRIPT_WALK_EAST:
            move_event_object_one_step(event_object, (uint16_t)(entry.operation - SCRIPT_WALK_SOUTH));
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_NPC_WALK_TO:
        case SCRIPT_NPC_SLOW_WALK_TO:
        case SCRIPT_NPC_WALK_TO_FAST:
        case SCRIPT_NPC_WALK_TO_FASTEST:
            set_event_object_position(current != NULL ? current : event_object, entry.operand[0], entry.operand[1], entry.operand[2]);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_EVENT_POSITION_RELATIVE:
            set_event_object_position(
                current != NULL ? current : event_object,
                (uint16_t)(pal_viewport_x + DEMO_PARTY_SCREEN_X + (int16_t)entry.operand[1]),
                (uint16_t)(pal_viewport_y + DEMO_PARTY_SCREEN_Y + (int16_t)entry.operand[2]),
                current != NULL ? read_le16(current + EVENT_LAYER_OFFSET) : pal_party_layer);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_EVENT_POSITION:
            set_event_object_position(
                current != NULL ? current : event_object,
                entry.operand[1],
                entry.operand[2],
                current != NULL ? read_le16(current + EVENT_LAYER_OFFSET) : pal_party_layer);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_DIRECTION_FRAME:
            if (event_object == NULL) {
                return script_entry;
            }
            if (entry.operand[0] != 0xFFFFu) {
                write_le16(event_object + EVENT_DIRECTION_OFFSET, entry.operand[0]);
            }
            if (entry.operand[1] != 0xFFFFu) {
                write_le16(event_object + EVENT_CURRENT_FRAME_OFFSET, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_FRAME_SOUTH:
            if (event_object == NULL) {
                return script_entry;
            }
            write_le16(event_object + EVENT_CURRENT_FRAME_OFFSET, entry.operand[0]);
            write_le16(event_object + EVENT_DIRECTION_OFFSET, DEMO_DIR_SOUTH);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_PARTY_DIRECTION_FRAME:
            if (entry.operand[0] <= DEMO_DIR_EAST) {
                pal_player_direction = entry.operand[0];
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_EVENT_DIRECTION_FRAME:
            if (current != NULL && entry.operand[0] != 0) {
                write_le16(current + EVENT_DIRECTION_OFFSET, entry.operand[1]);
                write_le16(current + EVENT_CURRENT_FRAME_OFFSET, entry.operand[2]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_AUTO_SCRIPT:
            if (current != NULL) {
                write_le16(current + EVENT_AUTO_SCRIPT_OFFSET, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_TRIGGER_SCRIPT:
            if (current != NULL) {
                write_le16(current + EVENT_TRIGGER_SCRIPT_OFFSET, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_TRIGGER_MODE:
            if (current != NULL) {
                write_le16(current + EVENT_TRIGGER_MODE_OFFSET, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_MUSIC:
        case SCRIPT_SET_BATTLE_MUSIC:
        case SCRIPT_PLAY_SOUND:
        case SCRIPT_FADE_OUT:
        case SCRIPT_FADE_IN:
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_VANISH_EVENT:
            if (event_object != NULL) {
                write_le16(event_object + EVENT_VANISH_TIME_OFFSET, (uint16_t)(int16_t)-15);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_HIDE_EVENT:
            if (event_object != NULL) {
                int16_t state = read_s16(event_object + EVENT_STATE_OFFSET);
                if (state > 0) {
                    write_le16(event_object + EVENT_STATE_OFFSET, (uint16_t)(-state));
                }
                write_le16(event_object + EVENT_VANISH_TIME_OFFSET, entry.operand[0] != 0 ? entry.operand[0] : 800u);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_USE_DAY_PALETTE:
            pal_palette_night = false;
            load_pack_palette_or_demo();
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_USE_NIGHT_PALETTE:
            pal_palette_night = true;
            load_pack_palette_or_demo();
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_CHANGE_SCENE:
            if (entry.operand[0] > 0 && entry.operand[0] <= PAL_SCENE_COUNT && pal_scene_num != entry.operand[0]) {
                pal_scene_num = entry.operand[0];
                pal_initial_viewport_x = 0;
                pal_initial_viewport_y = 0;
            }
            return (uint16_t)(script_entry + 1u);

        case SCRIPT_SET_PARTY_POSITION:
            set_party_world_position(
                (int)entry.operand[0] * 32 + (int)entry.operand[2] * 16,
                (int)entry.operand[1] * 16 + (int)entry.operand[2] * 8,
                pal_party_layer);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_RIDE_EVENT_SLOW:
        case SCRIPT_RIDE_EVENT:
        case SCRIPT_RIDE_EVENT_FAST:
        case SCRIPT_PARTY_WALK_TO_SLOW:
        case SCRIPT_PARTY_WALK_TO:
        case SCRIPT_PARTY_WALK_TO_FAST:
        {
            int world_x = (int)entry.operand[0] * 32 + (int)entry.operand[2] * 16;
            int world_y = (int)entry.operand[1] * 16 + (int)entry.operand[2] * 8;

            if ((entry.operation == SCRIPT_RIDE_EVENT_SLOW ||
                 entry.operation == SCRIPT_RIDE_EVENT ||
                 entry.operation == SCRIPT_RIDE_EVENT_FAST) &&
                event_object != NULL) {
                set_event_object_position(event_object, (uint16_t)world_x, (uint16_t)world_y, entry.operand[2]);
            }
            set_party_world_position(world_x, world_y, pal_party_layer);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_SET_PARTY:
        {
            uint16_t i;
            uint16_t count = 0;

            for (i = 0; i < 3u && i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
                if (entry.operand[i] != 0) {
                    pal_party_roles[count++] = (uint16_t)(entry.operand[i] - 1u);
                }
            }
            if (count == 0) {
                pal_party_roles[0] = 0;
                count = 1;
            }
            pal_max_party_member_index = (uint16_t)(count - 1u);
            if (pal_follower_count > DEMO_PLAYABLE_PARTY_SLOTS - 1u - pal_max_party_member_index) {
                pal_follower_count = DEMO_PLAYABLE_PARTY_SLOTS - 1u - pal_max_party_member_index;
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_SYNC_PARTY_TRAIL:
        {
            int world_x = pal_viewport_x + DEMO_PARTY_SCREEN_X;
            int world_y = pal_viewport_y + DEMO_PARTY_SCREEN_Y;
            uint16_t i;

            for (i = 0; i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
                pal_trail_x[i] = (uint16_t)world_x;
                pal_trail_y[i] = (uint16_t)world_y;
                pal_trail_direction[i] = pal_player_direction;
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_SET_EVENT_STATE:
            if (current != NULL) {
                write_le16(current + EVENT_STATE_OFFSET, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SYNC_EVENT_STATE:
            if (current != NULL && event_object != NULL &&
                read_s16(current + EVENT_STATE_OFFSET) == (int16_t)entry.operand[1]) {
                write_le16(event_object + EVENT_STATE_OFFSET, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_MOVE_EVENT_ONE_STEP:
            if (current != NULL) {
                write_le16(current + EVENT_X_OFFSET, (uint16_t)(read_s16(current + EVENT_X_OFFSET) + (int16_t)entry.operand[1]));
                write_le16(current + EVENT_Y_OFFSET, (uint16_t)(read_s16(current + EVENT_Y_OFFSET) + (int16_t)entry.operand[2]));
                advance_event_object_frame(current);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_SCENE_SCRIPTS:
            if (entry.operand[0] != 0) {
                uint8_t *scene = mutable_scene_record(entry.operand[0]);
                if (scene != NULL) {
                    if (entry.operand[1] != 0) {
                        write_le16(scene + SCENE_SCRIPT_ON_ENTER_OFFSET, entry.operand[1]);
                    }
                    if (entry.operand[2] != 0) {
                        write_le16(scene + SCENE_SCRIPT_ON_TELEPORT_OFFSET, entry.operand[2]);
                    }
                    if (entry.operand[1] == 0 && entry.operand[2] == 0) {
                        write_le16(scene + SCENE_SCRIPT_ON_ENTER_OFFSET, 0);
                        write_le16(scene + SCENE_SCRIPT_ON_TELEPORT_OFFSET, 0);
                    }
                }
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_PLAYER_SPRITE:
            if (write_player_role_word(PLAYER_ROLE_SPRITE_NUM_OFFSET, entry.operand[0], entry.operand[1]) &&
                entry.operand[2] != 0) {
                load_player_sprite();
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_MOVE_PLAYER:
            move_party_world_offset((int16_t)entry.operand[0], (int16_t)entry.operand[1], (uint16_t)(entry.operand[2] * 8u));
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_MOVE_EVENT:
            if (current != NULL) {
                write_le16(current + EVENT_X_OFFSET, (uint16_t)(read_s16(current + EVENT_X_OFFSET) + (int16_t)entry.operand[1]));
                write_le16(current + EVENT_Y_OFFSET, (uint16_t)(read_s16(current + EVENT_Y_OFFSET) + (int16_t)entry.operand[2]));
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_EVENT_LAYER:
            if (current != NULL) {
                write_le16(current + EVENT_LAYER_OFFSET, entry.operand[1]);
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_MOVE_VIEWPORT:
            if (entry.operand[0] == 0 && entry.operand[1] == 0) {
                set_party_world_position(pal_viewport_x + DEMO_PARTY_SCREEN_X, pal_viewport_y + DEMO_PARTY_SCREEN_Y, pal_party_layer);
            } else if (entry.operand[2] == 0xFFFFu) {
                pal_viewport_x = clamp_int((int)entry.operand[0] * 32 - DEMO_PARTY_SCREEN_X, 0, DEMO_MAP_PIXEL_WIDTH - 320);
                pal_viewport_y = clamp_int((int)entry.operand[1] * 16 - DEMO_PARTY_SCREEN_Y, 0, DEMO_MAP_PIXEL_HEIGHT - 200);
                pal_initial_viewport_x = pal_viewport_x;
                pal_initial_viewport_y = pal_viewport_y;
            } else {
                pal_viewport_x = clamp_int(pal_viewport_x + (int16_t)entry.operand[0], 0, DEMO_MAP_PIXEL_WIDTH - 320);
                pal_viewport_y = clamp_int(pal_viewport_y + (int16_t)entry.operand[1], 0, DEMO_MAP_PIXEL_HEIGHT - 200);
                pal_initial_viewport_x = pal_viewport_x;
                pal_initial_viewport_y = pal_viewport_y;
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_TOGGLE_DAY_NIGHT:
            pal_palette_night = !pal_palette_night;
            load_pack_palette_or_demo();
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_JUMP_IF_PLAYER_IN_PARTY:
        {
            bool in_party = false;
            uint16_t i;

            for (i = 0; i <= pal_max_party_member_index && i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
                if (player_role_word(0u, pal_party_roles[i]) == entry.operand[0]) {
                    in_party = true;
                    break;
                }
            }
            if (in_party && entry.operand[1] != 0) {
                script_entry = entry.operand[1];
            } else {
                script_entry = (uint16_t)(script_entry + 1u);
            }
            continue;
        }

        case SCRIPT_JUMP_IF_EVENT_OUT_OF_ZONE:
        {
            bool outside = true;

            if (current != NULL && event_object != NULL) {
                int dx = read_s16(event_object + EVENT_X_OFFSET) - read_s16(current + EVENT_X_OFFSET);
                int dy = read_s16(event_object + EVENT_Y_OFFSET) - read_s16(current + EVENT_Y_OFFSET);
                outside = abs_int(dx) + abs_int(dy * 2) >= (int)entry.operand[1] * 32 + 16;
            }
            if (outside && entry.operand[2] != 0) {
                script_entry = entry.operand[2];
            } else {
                script_entry = (uint16_t)(script_entry + 1u);
            }
            continue;
        }

        case SCRIPT_JUMP_IF_EVENT_STATE:
            if (current != NULL && read_s16(current + EVENT_STATE_OFFSET) == (int16_t)entry.operand[1] &&
                entry.operand[2] != 0) {
                script_entry = entry.operand[2];
            } else {
                script_entry = (uint16_t)(script_entry + 1u);
            }
            continue;

        case SCRIPT_SET_EVENT_RANGE_STATE:
            if (entry.operand[0] != 0 && entry.operand[1] >= entry.operand[0]) {
                uint16_t event_id;

                for (event_id = entry.operand[0]; event_id <= entry.operand[1]; event_id++) {
                    uint8_t *range_event = mutable_event_object_by_id(event_id);
                    if (range_event != NULL) {
                        write_le16(range_event + EVENT_STATE_OFFSET, entry.operand[2]);
                    }
                    if (event_id == 0xFFFFu) {
                        break;
                    }
                }
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_SET_FOLLOWERS:
        {
            uint16_t follower_count = 0;
            uint16_t i;

            for (i = 0; i < 2u && pal_max_party_member_index + 1u + i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
                if (entry.operand[i] != 0) {
                    pal_party_roles[pal_max_party_member_index + 1u + i] = entry.operand[i];
                    follower_count = (uint16_t)(i + 1u);
                }
            }
            pal_follower_count = follower_count;
            load_player_sprite();
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_CHANGE_SCENE_MAP:
            if (entry.operand[0] == 0xFFFFu) {
                uint8_t *scene = mutable_current_scene_record();
                if (scene != NULL) {
                    write_le16(scene + SCENE_MAP_OFFSET, entry.operand[1]);
                    load_tf_scene_chunks();
                }
            } else {
                uint8_t *scene = mutable_scene_record(entry.operand[0]);
                if (scene != NULL) {
                    write_le16(scene + SCENE_MAP_OFFSET, entry.operand[1]);
                }
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_RANDOM_SKIP:
            if (entry.operand[0] > 1u) {
                script_entry = (uint16_t)(script_entry + (script_random_percent() % entry.operand[0]));
            } else {
                script_entry = (uint16_t)(script_entry + 1u);
            }
            continue;

        case SCRIPT_SYNC_PARTY_TO_LEADER:
        {
            int world_x = pal_viewport_x + DEMO_PARTY_SCREEN_X;
            int world_y = pal_viewport_y + DEMO_PARTY_SCREEN_Y;
            uint16_t i;

            for (i = 0; i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
                pal_trail_x[i] = (uint16_t)world_x;
                pal_trail_y[i] = (uint16_t)world_y;
                pal_trail_direction[i] = pal_player_direction;
            }
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;
        }

        case SCRIPT_ANIMATE_EVENT:
            advance_event_object_frame(current != NULL ? current : event_object);
            script_entry = (uint16_t)(script_entry + 1u);
            if (!trigger_mode) {
                return script_entry;
            }
            continue;

        case SCRIPT_DIALOG_TEXT:
            pal_dialog_message_id = entry.operand[0];
            pal_dialog_visible = true;
            return (uint16_t)(script_entry + 1u);

        default:
            return script_entry;
        }
    }
    return script_entry;
}

static uint16_t run_auto_script_step(uint16_t script_entry, uint16_t event_object_id)
{
    return execute_script_mutation(script_entry, event_object_id, EVENT_AUTO_IDLE_OFFSET, false);
}

static uint16_t run_trigger_script_subset(uint16_t script_entry, uint16_t event_object_id)
{
    return execute_script_mutation(script_entry, event_object_id, EVENT_TRIGGER_IDLE_OFFSET, true);
}

static uint16_t find_facing_event_object(void)
{
    int player_x = pal_viewport_x + DEMO_PARTY_SCREEN_X;
    int player_y = pal_viewport_y + DEMO_PARTY_SCREEN_Y;
    int target_x = player_x;
    int target_y = player_y;
    uint16_t i;

    switch (pal_player_direction) {
    case DEMO_DIR_SOUTH:
        target_x -= DEMO_STEP_X;
        target_y += DEMO_STEP_Y;
        break;
    case DEMO_DIR_WEST:
        target_x -= DEMO_STEP_X;
        target_y -= DEMO_STEP_Y;
        break;
    case DEMO_DIR_NORTH:
        target_x += DEMO_STEP_X;
        target_y -= DEMO_STEP_Y;
        break;
    case DEMO_DIR_EAST:
        target_x += DEMO_STEP_X;
        target_y += DEMO_STEP_Y;
        break;
    default:
        return 0;
    }

    for (i = 0; i < pal_scene_snapshot.event_count; i++) {
        uint16_t event_object_id = (uint16_t)(pal_scene_snapshot.event_start + i + 1u);
        uint8_t *event_object = mutable_event_object_by_id(event_object_id);
        int event_x;
        int event_y;

        if (event_object == NULL || read_s16(event_object + EVENT_STATE_OFFSET) <= 0) {
            continue;
        }
        event_x = read_s16(event_object + EVENT_X_OFFSET);
        event_y = read_s16(event_object + EVENT_Y_OFFSET);
        if (abs_int(event_x - target_x) + abs_int(event_y - target_y) * 2 < 32) {
            return event_object_id;
        }
    }
    return 0;
}

static void trigger_facing_event(void)
{
    uint16_t event_object_id = find_facing_event_object();
    uint8_t *event_object;
    uint16_t trigger_script;
    uint16_t next_script;

    if (event_object_id == 0) {
        return;
    }
    event_object = mutable_event_object_by_id(event_object_id);
    if (event_object == NULL) {
        return;
    }
    trigger_script = read_le16(event_object + EVENT_TRIGGER_SCRIPT_OFFSET);
    if (trigger_script != 0) {
        next_script = run_trigger_script_subset(trigger_script, event_object_id);
        write_le16(event_object + EVENT_TRIGGER_SCRIPT_OFFSET, next_script);
        if (next_script != trigger_script) {
            (void)persist_runtime_save("trigger");
        }
    }
}

static void run_scene_enter_script_subset(void)
{
    uint8_t *scene = mutable_current_scene_record();
    uint16_t enter_script;
    uint16_t next_script;

    if (scene == NULL || !pal_script_ready) {
        return;
    }

    enter_script = read_le16(scene + SCENE_SCRIPT_ON_ENTER_OFFSET);
    if (enter_script == 0) {
        return;
    }

    next_script = run_trigger_script_subset(enter_script, 0xFFFFu);
    if (next_script != enter_script) {
        write_le16(scene + SCENE_SCRIPT_ON_ENTER_OFFSET, next_script);
        (void)persist_runtime_save("enter");
    }
}

static void advance_scene_auto_scripts(void)
{
    uint16_t i;

    if (!pal_tf_scene_ready || !pal_scene_event_objects_mutable || pal_scene_event_objects == NULL) {
        return;
    }

    for (i = 0; i < pal_scene_snapshot.event_count; i++) {
        uint16_t event_object_id = (uint16_t)(pal_scene_snapshot.event_start + i + 1u);
        uint8_t *event_object = mutable_event_object_by_id(event_object_id);
        uint16_t auto_script;

        if (event_object == NULL ||
            read_s16(event_object + EVENT_STATE_OFFSET) <= 0 ||
            read_s16(event_object + EVENT_VANISH_TIME_OFFSET) != 0) {
            continue;
        }
        auto_script = read_le16(event_object + EVENT_AUTO_SCRIPT_OFFSET);
        if (auto_script != 0) {
            write_le16(event_object + EVENT_AUTO_SCRIPT_OFFSET, run_auto_script_step(auto_script, event_object_id));
        }
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

        if (offset > pal_scene_event_objects_size ||
            SSS_EVENT_OBJECT_BYTES > pal_scene_event_objects_size - offset) {
            break;
        }

        event_object = (uint8_t *)pal_scene_event_objects + offset;
        advance_event_object_frame(event_object);
    }
}

static void advance_player_frame(void)
{
    if (pal_player_sprite == NULL || pal_player_walk_frames == 0) {
        return;
    }
    if (!pal_player_walking) {
        pal_player_step_phase = 0;
        return;
    }
    pal_player_step_phase = (uint16_t)((pal_player_step_phase + 1u) & 3u);
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

static void party_member_screen_position(uint16_t index, int *x, int *y, uint16_t *direction)
{
    int px;
    int py;
    uint16_t base_direction;

    if (index == 0) {
        *x = DEMO_PARTY_SCREEN_X;
        *y = DEMO_PARTY_SCREEN_Y;
        *direction = pal_player_direction;
        return;
    }

    if (index > pal_max_party_member_index) {
        uint16_t trail_index = (uint16_t)(2u + index - pal_max_party_member_index);
        if (trail_index >= DEMO_PLAYABLE_PARTY_SLOTS) {
            trail_index = DEMO_PLAYABLE_PARTY_SLOTS - 1u;
        }
        *x = (int)pal_trail_x[trail_index] - pal_viewport_x;
        *y = (int)pal_trail_y[trail_index] - pal_viewport_y;
        *direction = pal_trail_direction[trail_index];
        return;
    }

    px = (int)pal_trail_x[1] - pal_viewport_x;
    py = (int)pal_trail_y[1] - pal_viewport_y;
    base_direction = pal_trail_direction[1];
    if (index == 2u) {
        px += (base_direction == DEMO_DIR_EAST || base_direction == DEMO_DIR_WEST) ? -16 : 16;
        py += 8;
    } else {
        px += (base_direction == DEMO_DIR_WEST || base_direction == DEMO_DIR_SOUTH) ? 16 : -16;
        py += (base_direction == DEMO_DIR_WEST || base_direction == DEMO_DIR_NORTH) ? 8 : -8;
    }

    if (map_position_blocked(px + pal_viewport_x, py + pal_viewport_y) ||
        event_position_blocked(px + pal_viewport_x, py + pal_viewport_y)) {
        px = (int)pal_trail_x[1] - pal_viewport_x;
        py = (int)pal_trail_y[1] - pal_viewport_y;
    }

    *x = px;
    *y = py;
    *direction = pal_trail_direction[2];
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

    for (i = 0; i <= visible_party_last_index() && i < DEMO_PLAYABLE_PARTY_SLOTS; i++) {
        uint16_t walk_frames;
        uint16_t draw_direction;
        uint16_t frame_num;
        uint16_t frame_index;
        const uint8_t *rle;
        uint16_t w;
        uint16_t h;
        int px;
        int py;
        int layer = (int)pal_party_layer;

        if (pal_party_sprites[i] == NULL ||
            draw_count >= (uint16_t)(sizeof(pal_scene_draw_items) / sizeof(pal_scene_draw_items[0]))) {
            continue;
        }

        walk_frames = pal_party_walk_frames[i] == 0 ? 3u : pal_party_walk_frames[i];
        party_member_screen_position(i, &px, &py, &draw_direction);
        frame_num = walk_frame_for_phase(walk_frames, i > pal_max_party_member_index);
        frame_index = (uint16_t)(draw_direction * walk_frames + frame_num);
        rle = sprite_frame(pal_party_sprites[i], frame_index);
        if (rle == NULL) {
            rle = sprite_frame(pal_party_sprites[i], (uint16_t)(draw_direction * walk_frames));
        }
        w = rle_width(rle);
        h = rle_height(rle);
        if (rle != NULL && w != 0 && h != 0) {
            pal_scene_draw_items[draw_count].rle = rle;
            pal_scene_draw_items[draw_count].x = px - (int)w / 2;
            pal_scene_draw_items[draw_count].y = py + layer + 10 - (int)h;
            pal_scene_draw_items[draw_count].sort_y = py + layer + 6;
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

static void fill_framebuffer_rect(int x, int y, int width, int height, uint8_t color)
{
    int row;

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > (int)PAL_VIDEO_WIDTH) {
        width = (int)PAL_VIDEO_WIDTH - x;
    }
    if (y + height > (int)PAL_VIDEO_HEIGHT) {
        height = (int)PAL_VIDEO_HEIGHT - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    for (row = 0; row < height; row++) {
        memset(pal_sram_framebuffer + (uint32_t)(y + row) * PAL_VIDEO_WIDTH + (uint32_t)x, color, (uint32_t)width);
    }
}

static void draw_font_glyph(uint16_t codepoint, int x, int y, uint8_t color)
{
    const uint8_t *glyph;
    uint16_t glyph_bytes;
    int row;

    if (codepoint == 0x20u || !pal_font_ready || !PalFont_FindGlyph(&pal_font_cache, codepoint, &glyph, &glyph_bytes) ||
        glyph == NULL || glyph_bytes < PAL_FONT_GLYPH_BYTES) {
        return;
    }

    for (row = 0; row < 16; row++) {
        uint16_t bits = (uint16_t)(glyph[(uint32_t)row * 2u] | ((uint16_t)glyph[(uint32_t)row * 2u + 1u] << 8));
        int col;
        int yy = y + row;

        if (yy < 0 || yy >= (int)PAL_VIDEO_HEIGHT) {
            continue;
        }
        for (col = 0; col < 16; col++) {
            int xx = x + col;
            if (xx >= 0 && xx < (int)PAL_VIDEO_WIDTH && (bits & (uint16_t)(0x8000u >> col)) != 0) {
                pal_sram_framebuffer[(uint32_t)yy * PAL_VIDEO_WIDTH + (uint32_t)xx] = color;
            }
        }
    }
}

static void draw_utf16_text(const uint8_t *utf16le, uint32_t byte_size, int x, int y, int width, int height, uint8_t color)
{
    uint32_t offset;
    int cursor_x = x;
    int cursor_y = y;
    int x_limit = x + width;
    int y_limit = y + height;

    for (offset = 0; offset + 1u < byte_size; offset += 2u) {
        uint16_t codepoint = read_le16(utf16le + offset);

        if (codepoint == 0 || codepoint == 0x0Du) {
            continue;
        }
        if (codepoint == 0x0Au || cursor_x + 16 > x_limit) {
            cursor_x = x;
            cursor_y += 16;
            if (codepoint == 0x0Au) {
                continue;
            }
        }
        if (cursor_y + 16 > y_limit) {
            break;
        }
        draw_font_glyph(codepoint, cursor_x, cursor_y, color);
        cursor_x += 16;
    }
}

static void draw_dialog_overlay(void)
{
    const uint8_t *message;
    uint32_t message_size;
    int x = 8;
    int y = 136;
    int width = 304;
    int height = 56;

    if (!pal_dialog_visible || !pal_text_ready ||
        !PalText_GetMessage(&pal_text_cache, pal_dialog_message_id, &message, &message_size)) {
        return;
    }

    if (pal_dialog_mode == PAL_DIALOG_MODE_UPPER) {
        y = 8;
    } else if (pal_dialog_mode == PAL_DIALOG_MODE_CENTER) {
        y = 68;
    } else if (pal_dialog_mode == PAL_DIALOG_MODE_CENTER_WINDOW) {
        y = 52;
        height = 96;
    }

    fill_framebuffer_rect(x, y, width, height, 0u);
    fill_framebuffer_rect(x + 1, y + 1, width - 2, height - 2, 15u);
    fill_framebuffer_rect(x + 3, y + 3, width - 6, height - 6, 0u);
    draw_utf16_text(message, message_size, x + 8, y + 8, width - 16, height - 16, 0x2Fu);
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
    draw_dialog_overlay();
}

void app_main(void)
{
    uint32_t tick = 0;
    bool save_loaded;

    if (!CoreS3Se_Begin()) {
        CoreS3Se_ShowError("BOARD FAIL", "CORES3SE INIT");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    pal_nor_ready = open_nor_pack();
    pal_tf_ready = CoreS3Se_MountTf() && open_tf_pack();
    reset_party_state();
    load_text_font_cache();
    load_ui_dialog_cache();
    load_menu_cache();
    load_battle_cache();
    load_rng_cache();
    load_music_cache();
    load_ending_cache();
    save_loaded = load_startup_save();
    if (save_loaded) {
        load_readonly_global_cache();
    } else {
        load_global_cache();
    }
    load_script_cache();
    load_pack_palette_or_demo();
    load_tf_scene_chunks();
    if (!pal_tf_scene_ready) {
        load_player_sprite();
    }
    for (;;) {
        uint16_t tx = 0;
        uint16_t ty = 0;
        bool touched = CoreS3Se_TouchPoint(&tx, &ty);
        bool dialog_consumed = update_dialog_touch(touched);

        if (!dialog_consumed) {
            update_scene_selection(touched, ty);
            update_demo_viewport(touched, tx, ty);
        }
        if ((tick & 7u) == 0) {
            advance_scene_auto_scripts();
            advance_scene_event_frames();
            advance_player_frame();
        }
        sync_runtime_save_position();
        draw_demo_frame(tick, touched, tx, ty);
        if (!CoreS3Se_FlushPalFramebuffer()) {
            CoreS3Se_ShowError("LCD FAIL", "FLUSH");
        }
        tick += touched ? 7u : 1u;
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
