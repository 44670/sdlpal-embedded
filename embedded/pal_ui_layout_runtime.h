#ifndef PAL_UI_LAYOUT_RUNTIME_H
#define PAL_UI_LAYOUT_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PAL_UI_LAYOUT_ELEMENT_VISIBLE = 1u << 0,
    PAL_UI_LAYOUT_ELEMENT_SELECTABLE = 1u << 1,
    PAL_UI_LAYOUT_ELEMENT_CRITICAL = 1u << 2,
    PAL_UI_LAYOUT_SAMPLING_VISIBLE = 1u << 0,
    PAL_UI_LAYOUT_SAMPLING_LEGACY_STAGE = 1u << 1,
    PAL_UI_LAYOUT_SAMPLING_CATALOG = 1u << 2,
    PAL_UI_LAYOUT_CAMERA_CLAMPED = 1u << 0,
    PAL_UI_LAYOUT_NO_SCREEN = 255u,
    PAL_UI_LAYOUT_FILTER_NEAREST_CENTER = 0u,
    PAL_UI_LAYOUT_FILTER_BOX_2X2 = 1u,
    PAL_UI_LAYOUT_FILTER_NONE = 255u,
    PAL_UI_LAYOUT_ASSET_CLASS_NONE = 0u,
    PAL_UI_LAYOUT_ASSET_CLASS_LEGACY_STAGE = 1u,
    PAL_UI_LAYOUT_ASSET_CLASS_PORTRAIT = 2u,
    PAL_UI_LAYOUT_ASSET_CLASS_ITEM_PREVIEW = 3u,
    PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_PLAYER = 4u,
    PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_ENEMY = 5u,
    PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_FIRE = 6u,
    PAL_UI_LAYOUT_ASSET_CLASS_UI_SPRITE = 7u,
    PAL_UI_LAYOUT_ASSET_CLASS_BATTLE_EFFECT = 8u,
    PAL_UI_LAYOUT_ASSET_CLASS_COUNT = 9u,
    PAL_UI_LAYOUT_BATTLE_HAS_ACTOR = 1u << 0,
    PAL_UI_LAYOUT_BATTLE_HAS_TARGET = 1u << 1,
    PAL_UI_LAYOUT_BATTLE_FORCE_FIT_ALL = 1u << 2,
    PAL_UI_LAYOUT_BATTLE_PRIMARY_TEAM = 255u,
    PAL_UI_LAYOUT_BATTLE_MAX_PLAYERS = 3u,
    PAL_UI_LAYOUT_ITEM_MAX_ITEMS = 256u,
    PAL_UI_LAYOUT_MAGIC_MAX_ITEMS = 32u,
    /* Fixed-point field names are an ABI: all generated values are Q16.16. */
    PAL_UI_LAYOUT_Q16_SHIFT = 16u,
};

/*
 * Stable public IDs for certified production profiles.  Generated headers are
 * private implementation details; target call sites must use these names
 * instead of depending on profile-local enum declarations or magic numbers.
 * New entries may only be appended.
 */
typedef enum PalUiLayoutVariantId {
    PAL_UI_LAYOUT_VARIANT_FULL = 0,
    PAL_UI_LAYOUT_VARIANT_COMPACT = 1,
    PAL_UI_LAYOUT_VARIANT_SINGLE_COLUMN = 2,
    PAL_UI_LAYOUT_VARIANT_PAGED = 3,
    PAL_UI_LAYOUT_VARIANT_TEXT_ONLY = 4,
} PalUiLayoutVariantId;

typedef enum PalUiLayoutScreenId {
    PAL_UI_LAYOUT_SCREEN_DIALOG = 0,
    PAL_UI_LAYOUT_SCREEN_OPENING_MENU = 1,
    PAL_UI_LAYOUT_SCREEN_GAME_MENU = 2,
    PAL_UI_LAYOUT_SCREEN_SYSTEM_MENU = 3,
    PAL_UI_LAYOUT_SCREEN_SAVE_SLOTS = 4,
    PAL_UI_LAYOUT_SCREEN_CONFIRMATION = 5,
    PAL_UI_LAYOUT_SCREEN_ITEM = 6,
    PAL_UI_LAYOUT_SCREEN_MAGIC = 7,
    PAL_UI_LAYOUT_SCREEN_STATUS = 8,
    PAL_UI_LAYOUT_SCREEN_EQUIP = 9,
    PAL_UI_LAYOUT_SCREEN_BATTLE_HUD = 10,
    PAL_UI_LAYOUT_SCREEN_BATTLE_MISC = 11,
    PAL_UI_LAYOUT_SCREEN_BATTLE_ITEM_ACTION = 12,
    PAL_UI_LAYOUT_SCREEN_COUNT = 13,
} PalUiLayoutScreenId;

typedef enum PalUiLayoutElementKind {
    PAL_UI_LAYOUT_ELEMENT_ACTION = 0,
    PAL_UI_LAYOUT_ELEMENT_DIALOG_TEXT = 1,
    PAL_UI_LAYOUT_ELEMENT_EQUIPMENT_LABEL = 2,
    PAL_UI_LAYOUT_ELEMENT_EQUIPMENT_LABEL_VALUE = 3,
    PAL_UI_LAYOUT_ELEMENT_EQUIPMENT_PREVIEW = 4,
    PAL_UI_LAYOUT_ELEMENT_PAGE_INDICATOR = 5,
    PAL_UI_LAYOUT_ELEMENT_PANEL = 6,
    PAL_UI_LAYOUT_ELEMENT_PARTY_SELECTOR = 7,
    PAL_UI_LAYOUT_ELEMENT_PORTRAIT = 8,
    PAL_UI_LAYOUT_ELEMENT_STAT_LABEL = 9,
    PAL_UI_LAYOUT_ELEMENT_STAT_LABEL_VALUE = 10,
    PAL_UI_LAYOUT_ELEMENT_TITLE = 11,
    PAL_UI_LAYOUT_ELEMENT_KIND_COUNT = 12,
} PalUiLayoutElementKind;

typedef enum PalUiLayoutCameraKind {
    PAL_UI_LAYOUT_CAMERA_MAP = 0,
    PAL_UI_LAYOUT_CAMERA_BATTLE = 1,
} PalUiLayoutCameraKind;

typedef enum PalUiLayoutCameraMode {
    PAL_UI_LAYOUT_CAMERA_FOLLOW = 0,
    PAL_UI_LAYOUT_CAMERA_SCRIPTED = 1,
    PAL_UI_LAYOUT_CAMERA_IDLE = 2,
    PAL_UI_LAYOUT_CAMERA_ACTOR_TARGET = 3,
    PAL_UI_LAYOUT_CAMERA_FIT_ALL = 4,
} PalUiLayoutCameraMode;

typedef struct PalUiLayoutPoint {
    int32_t x;
    int32_t y;
} PalUiLayoutPoint;

typedef struct PalUiLayoutRect {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} PalUiLayoutRect;

typedef struct PalUiLayoutProfile {
    uint16_t display_width;
    uint16_t display_height;
    PalUiLayoutRect safe_rect;
    PalUiLayoutRect stage_rect;
    uint16_t stage_source_width;
    uint16_t stage_source_height;
    uint8_t fixed_q_shift;
    uint16_t stage_scale_numerator;
    uint16_t stage_scale_denominator;
    uint32_t stage_scale_q16;
    uint32_t stage_step_x_q16;
    uint32_t stage_step_y_q16;
    uint32_t stage_phase_x_q16;
    uint32_t stage_phase_y_q16;
    int16_t player_anchor_x;
    int16_t player_anchor_y;
    uint8_t font_cell_width;
    uint8_t font_cell_height;
    int8_t font_ascent;
    int8_t font_descent;
    uint8_t font_line_height;
    uint32_t font_glyph_count;
    uint32_t font_image_bytes;
    uint32_t font_payload_crc32;
    uint16_t screen_count;
    uint16_t element_count;
    uint16_t focus_count;
    uint16_t sampling_count;
    uint16_t camera_vector_count;
} PalUiLayoutProfile;

typedef struct PalUiLayoutScreen {
    uint16_t element_first;
    uint16_t element_count;
    uint16_t focus_first;
    uint16_t focus_count;
    uint8_t variant;
    uint8_t rows;
    uint8_t columns;
    uint8_t page_count;
    uint8_t page_capacity;
    uint8_t initial_page;
} PalUiLayoutScreen;

typedef struct PalUiLayoutElement {
    PalUiLayoutRect rect;
    int16_t return_value;
    uint16_t page;
    uint16_t local_id;
    uint8_t screen_id;
    uint8_t kind;
    uint8_t priority;
    uint8_t flags;
} PalUiLayoutElement;

typedef struct PalUiLayoutSampling {
    uint16_t role;
    uint8_t asset_class;
    uint8_t filter;
    uint8_t flags;
    uint8_t screen_id;
    uint16_t page;
    uint16_t numerator;
    uint16_t denominator;
    uint16_t source_width;
    uint16_t source_height;
    uint16_t destination_width;
    uint16_t destination_height;
    uint8_t fixed_q_shift;
    uint32_t scale_q16;
    uint32_t step_x_q16;
    uint32_t step_y_q16;
    uint32_t phase_x_q16;
    uint32_t phase_y_q16;
    uint16_t min_width;
    uint16_t min_height;
    uint16_t max_width;
    uint16_t max_height;
    PalUiLayoutRect destination;
} PalUiLayoutSampling;

typedef struct PalUiLayoutCameraVector {
    PalUiLayoutRect source;
    PalUiLayoutRect screen;
    int16_t focus_x;
    int16_t focus_y;
    int16_t camera_x;
    int16_t camera_y;
    int16_t desired_x;
    int16_t desired_y;
    int16_t projected_focus_x;
    int16_t projected_focus_y;
    uint16_t scale_numerator;
    uint16_t scale_denominator;
    uint8_t fixed_q_shift;
    uint32_t scale_q16;
    uint32_t source_step_q16;
    uint32_t source_phase_q16;
    uint8_t kind;
    uint8_t mode;
    uint8_t flags;
} PalUiLayoutCameraVector;

typedef struct PalUiLayoutBattleCameraPolicy {
    PalUiLayoutRect arena;
    PalUiLayoutRect hud_rect;
    PalUiLayoutRect content_rect;
    PalUiLayoutRect focus_screen;
    PalUiLayoutRect fit_screen;
    uint16_t focus_source_width;
    uint16_t focus_source_height;
    uint16_t fit_numerator;
    uint16_t fit_denominator;
    uint8_t fixed_q_shift;
    uint32_t fit_scale_q16;
    uint32_t fit_step_q16;
    uint32_t fit_phase_q16;
    uint8_t padding;
    uint8_t max_players;
} PalUiLayoutBattleCameraPolicy;

typedef struct PalUiLayoutBattleCameraInput {
    uint8_t player_count;
    uint8_t primary_player;
    uint8_t flags;
    PalUiLayoutRect players[PAL_UI_LAYOUT_BATTLE_MAX_PLAYERS];
    PalUiLayoutRect actor;
    PalUiLayoutRect target;
} PalUiLayoutBattleCameraInput;

/*
 * Item/magic layouts are page templates.  Their generated audit labels and
 * return values are fixtures only; production binds each visible slot to the
 * current live object ID, enabled state, count, and cursor.
 */
typedef struct PalUiLayoutListTemplate {
    uint16_t screen_id;
    uint16_t slot_count;
    uint16_t maximum_items;
    uint16_t maximum_page_count;
    uint8_t rows;
    uint8_t columns;
} PalUiLayoutListTemplate;

/*
 * The generated header is private to pal_ui_layout_runtime.c.  Callers see
 * only copied records, so including this API never duplicates static tables.
 */
bool PalUiLayout_GetProfile(PalUiLayoutProfile *out);
bool PalUiLayout_Font10IdentityMatches(
    uint32_t glyph_count,
    uint32_t image_bytes,
    uint32_t payload_crc32,
    uint8_t cell_width,
    uint8_t cell_height,
    int8_t ascent,
    int8_t descent);
bool PalUiLayout_GetScreen(uint16_t screen_id, PalUiLayoutScreen *out);
bool PalUiLayout_GetElement(uint16_t element_id, PalUiLayoutElement *out);
bool PalUiLayout_GetScreenElement(
    uint16_t screen_id,
    uint16_t local_id,
    PalUiLayoutElement *out);
bool PalUiLayout_GetFocusElement(
    uint16_t screen_id,
    uint16_t focus_index,
    PalUiLayoutElement *out);
bool PalUiLayout_GetListTemplate(
    uint16_t screen_id,
    PalUiLayoutListTemplate *out);
bool PalUiLayout_GetListSlot(
    uint16_t screen_id,
    uint16_t slot_index,
    PalUiLayoutElement *out);
bool PalUiLayout_GetListPageCount(
    const PalUiLayoutListTemplate *list,
    uint16_t item_count,
    uint16_t *page_count);
bool PalUiLayout_GetSampling(
    uint16_t sampling_id,
    PalUiLayoutSampling *out);
/*
 * Look up a fully generated transform for one real source RLE extent.
 * No target-side scale or layout solve occurs on a miss.
 */
bool PalUiLayout_FindCatalogSampling(
    uint8_t asset_class,
    uint8_t screen_id,
    uint16_t source_width,
    uint16_t source_height,
    PalUiLayoutSampling *out);
bool PalUiLayout_GetCameraVector(
    uint16_t vector_id,
    PalUiLayoutCameraVector *out);
bool PalUiLayout_GetBattleCameraPolicy(
    PalUiLayoutBattleCameraPolicy *out);
/*
 * Resolve one live camera using the generated HUD-free policy.  This is a
 * fixed O(3) integer evaluator, not a target-side UI/layout solver.
 */
bool PalUiLayout_ResolveBattleCamera(
    const PalUiLayoutBattleCameraInput *input,
    PalUiLayoutCameraVector *out);
bool PalUiLayout_BattleFitSourceX(
    uint16_t destination_x,
    uint16_t *source_x);
bool PalUiLayout_BattleFitSourceY(
    uint16_t destination_y,
    uint16_t *source_y);

bool PalUiLayout_ProjectCameraPoint(
    const PalUiLayoutCameraVector *camera,
    PalUiLayoutPoint world,
    PalUiLayoutPoint *screen);
bool PalUiLayout_UnprojectCameraPoint(
    const PalUiLayoutCameraVector *camera,
    PalUiLayoutPoint screen,
    PalUiLayoutPoint *world);
bool PalUiLayout_ProjectCameraRect(
    const PalUiLayoutCameraVector *camera,
    PalUiLayoutRect world,
    PalUiLayoutRect *screen);
bool PalUiLayout_SamplingSourceAt(
    const PalUiLayoutSampling *sampling,
    uint16_t destination_x,
    uint16_t destination_y,
    uint16_t *source_x,
    uint16_t *source_y);

/* Fail closed if a generated table is truncated or internally inconsistent. */
bool PalUiLayout_ValidateGenerated(void);

#ifdef __cplusplus
}
#endif

#endif
