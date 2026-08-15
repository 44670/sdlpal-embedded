#include "pal_target_board.h"
#include "pal_target_memory.h"

#include "global.h"

#include <nds.h>

#include <stdio.h>
#include <string.h>

#ifndef NDS_GIT_REVISION
#define NDS_GIT_REVISION "unknown"
#endif

enum {
   PAL_NDS_VISIBLE_BYTES = 256u * 192u,
   PAL_NDS_BITMAP_PAGE_BYTES = 256u * 256u,
   PAL_NDS_SECOND_PAGE_MAP_BASE = 4u,
   PAL_NDS_CONSOLE_MAP_BASE = 12u,
   PAL_NDS_MINIMAP_MAP_BASE = 16u,
   PAL_NDS_MINIMAP_TILE_BASE = 4u,
   PAL_NDS_MINIMAP_MARKER_TILE_BASE = 1u,
   PAL_NDS_MINIMAP_MARKER_MAP_BASE = 9u,
   PAL_NDS_MINIMAP_MARKER_PALETTE_BANK = 1u,
   PAL_NDS_MINIMAP_MAP_TILE_COLUMNS = 128u,
   PAL_NDS_MINIMAP_RAW_COLUMNS = 64u,
   PAL_NDS_MINIMAP_RAW_ROWS = 128u,
   PAL_NDS_MINIMAP_RAW_HALVES = 2u,
   PAL_NDS_MINIMAP_DIAGONAL_ORIGIN = 63,
   PAL_NDS_MINIMAP_CELL_PIXELS = 4u,
   PAL_NDS_MINIMAP_SCREEN_CENTER_X = 128,
   PAL_NDS_MINIMAP_SCREEN_CENTER_Y = 96,
   PAL_NDS_MINIMAP_AFFINE_ONE = 256,
   PAL_NDS_MINIMAP_BLUE = 254u,
   PAL_NDS_MINIMAP_WHITE = 255u,
   PAL_NDS_MINIMAP_MARKER_RED = 1u,
   PAL_NDS_MINIMAP_MARKER_CENTER = 4u,
   PAL_NDS_MAP_BLOCKED = 0x2000u,
   PAL_NDS_MINIMAP_MAX_OBSTACLES = 128u,
   PAL_NDS_MINIMAP_PALETTE_GRAY = 0u,
   PAL_NDS_MINIMAP_PALETTE_YELLOW = 1u,
   PAL_NDS_MINIMAP_PALETTE_GREEN = 2u,
   PAL_NDS_MINIMAP_SCRIPT_SCAN_LIMIT = 512u,
   PAL_NDS_MINIMAP_SCRIPT_GRAPH_LIMIT = 64u,
   PAL_NDS_MINIMAP_SCENE_EVENT_CAPACITY = 160u,
};

_Static_assert(
   PAL_NDS_MINIMAP_TILEMAP_ENTRIES >=
      PAL_NDS_MINIMAP_RAW_COLUMNS * PAL_NDS_MINIMAP_RAW_ROWS *
      PAL_NDS_MINIMAP_RAW_HALVES,
   "minimap tilemap cannot serve as the component queue");
_Static_assert(
   sizeof(pal_nds_minimap_pattern_tiles) >=
      PAL_NDS_MINIMAP_TOPOLOGY_BYTES,
   "minimap pattern owner cannot serve as the closure-blocked mask");
/* Four central occupancy bits each expose two independent outside edges:
 * sum(C(4, n) * 4^n) == 5^4 == 625 normalized patterns. */
_Static_assert(
   PAL_NDS_MINIMAP_TILE_COUNT >= 625u,
   "minimap tile dictionary cannot represent every component boundary");

/* Transparent tile 0 plus one small red point. */
static const uint8_t pal_nds_minimap_marker_tiles[2][32]
   __attribute__((aligned(4))) = {
   { 0 },
   {
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x10, 0x01, 0x00,
      0x00, 0x10, 0x01, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
   },
};

/* One four-pixel gray logical cell centered in an 8x8 OBJ tile. */
static const uint8_t pal_nds_minimap_obstacle_tile[32]
   __attribute__((aligned(4))) = {
   0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
   0x00, 0x11, 0x11, 0x00,
   0x00, 0x11, 0x11, 0x00,
   0x00, 0x11, 0x11, 0x00,
   0x00, 0x11, 0x11, 0x00,
   0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
};

static PrintConsole pal_nds_console;
static int pal_nds_bg;
static int pal_nds_minimap_bg = -1;
static int pal_nds_minimap_marker_bg = -1;
static unsigned pal_nds_visible_page;
volatile uint32_t pal_nds_present_count
   __attribute__((section(".bss.pal_nds_video")));
static uint16_t pal_nds_palette[256] __attribute__((aligned(4)));
static bool pal_nds_started;
static int pal_nds_minimap_map = -1;
static int pal_nds_minimap_pending_map = -1;
static int pal_nds_minimap_pending_scene = -1;
static int pal_nds_minimap_component_seed_column = -1;
static int pal_nds_minimap_component_seed_row = -1;
static uint32_t pal_nds_minimap_pending_portal_signature;
static uint32_t pal_nds_minimap_pending_structural_signature;
static uint32_t pal_nds_minimap_generation;
static uint32_t pal_nds_minimap_pending_generation;
static unsigned pal_nds_minimap_min_column;
static unsigned pal_nds_minimap_min_row;
static unsigned pal_nds_minimap_width_pixels;
static unsigned pal_nds_minimap_height_pixels;
static bool pal_nds_minimap_has_cells;
static int pal_nds_minimap_view_x;
static int pal_nds_minimap_view_y;
static int pal_nds_minimap_pending_view_x;
static int pal_nds_minimap_pending_view_y;
static int pal_nds_minimap_marker_x = -1;
static int pal_nds_minimap_marker_y = -1;
static int pal_nds_minimap_pending_marker_x = -1;
static int pal_nds_minimap_pending_marker_y = -1;
static unsigned pal_nds_minimap_tile_count;
static unsigned pal_nds_minimap_obstacle_count;
static bool pal_nds_minimap_pending_visible;
static unsigned pal_nds_minimap_stationary_event_count;
static uint8_t pal_nds_minimap_stationary_events[
   (PAL_NDS_MINIMAP_SCENE_EVENT_CAPACITY + 7u) / 8u];

static bool
pal_nds_minimap_script_changes_scene(
   WORD script_entry)
{
   unsigned scanned = 0u;

   while (script_entry != 0u &&
      (unsigned)script_entry < (unsigned)gpGlobals->g.nScriptEntry &&
      scanned++ < PAL_NDS_MINIMAP_SCRIPT_SCAN_LIMIT)
   {
      const SCRIPTENTRY *script =
         gpGlobals->g.lprgScriptEntry + script_entry;

      if (script->wOperation == 0x0059u)
      {
         return true;
      }
      if (script->wOperation == 0x0000u ||
         script->wOperation == 0x0001u ||
         script->wOperation == 0x0002u)
      {
         return false;
      }
      if (script->wOperation == 0x0003u &&
         script->rgwOperand[1] == 0u)
      {
         script_entry = script->rgwOperand[0];
      }
      else
      {
         script_entry++;
      }
   }
   return false;
}

static bool
pal_nds_minimap_operation_moves_event(
   WORD operation)
{
   return (operation >= 0x000bu && operation <= 0x000eu) ||
      operation == 0x0010u || operation == 0x0011u ||
      operation == 0x0012u || operation == 0x0013u ||
      operation == 0x003fu || operation == 0x0044u ||
      operation == 0x004cu || operation == 0x006cu ||
      operation == 0x007cu || operation == 0x007du ||
      operation == 0x0082u || operation == 0x0084u ||
      operation == 0x0097u;
}

static bool
pal_nds_minimap_auto_script_moves_event(
   WORD script_entry)
{
   WORD pending[PAL_NDS_MINIMAP_SCRIPT_GRAPH_LIMIT];
   WORD visited[PAL_NDS_MINIMAP_SCRIPT_GRAPH_LIMIT];
   unsigned pending_count = 0u;
   unsigned visited_count = 0u;

   if (script_entry == 0u)
   {
      return false;
   }
   pending[pending_count++] = script_entry;
   while (pending_count != 0u)
   {
      const SCRIPTENTRY *script;
      unsigned i;

      script_entry = pending[--pending_count];
      if (script_entry == 0u)
      {
         continue;
      }
      if ((unsigned)script_entry >= (unsigned)gpGlobals->g.nScriptEntry)
      {
         return true;
      }
      for (i = 0u; i < visited_count; i++)
      {
         if (visited[i] == script_entry)
         {
            break;
         }
      }
      if (i != visited_count)
      {
         continue;
      }
      if (visited_count >= PAL_NDS_MINIMAP_SCRIPT_GRAPH_LIMIT)
      {
         return true;
      }
      visited[visited_count++] = script_entry;
      script = gpGlobals->g.lprgScriptEntry + script_entry;
      if (pal_nds_minimap_operation_moves_event(script->wOperation))
      {
         return true;
      }
      if (script->wOperation == 0x0000u)
      {
         continue;
      }

#define PAL_NDS_MINIMAP_PUSH_SCRIPT(entry) do {                         \
      WORD next_entry = (WORD)(entry);                                  \
      if (next_entry != 0u)                                             \
      {                                                                 \
         if (pending_count >= PAL_NDS_MINIMAP_SCRIPT_GRAPH_LIMIT)       \
         {                                                              \
            return true;                                                \
         }                                                              \
         pending[pending_count++] = next_entry;                         \
      }                                                                 \
   } while (0)
      if (script->wOperation == 0x0002u ||
         script->wOperation == 0x0003u)
      {
         PAL_NDS_MINIMAP_PUSH_SCRIPT(script->rgwOperand[0]);
         if (script->rgwOperand[1] != 0u)
         {
            PAL_NDS_MINIMAP_PUSH_SCRIPT(script_entry + 1u);
         }
      }
      else if (script->wOperation == 0x0004u)
      {
         PAL_NDS_MINIMAP_PUSH_SCRIPT(script->rgwOperand[0]);
         PAL_NDS_MINIMAP_PUSH_SCRIPT(script_entry + 1u);
      }
      else if (script->wOperation == 0x0006u)
      {
         PAL_NDS_MINIMAP_PUSH_SCRIPT(script->rgwOperand[1]);
         PAL_NDS_MINIMAP_PUSH_SCRIPT(script_entry + 1u);
      }
      else
      {
         PAL_NDS_MINIMAP_PUSH_SCRIPT(script_entry + 1u);
      }
#undef PAL_NDS_MINIMAP_PUSH_SCRIPT
   }
   return false;
}

static bool
pal_nds_minimap_stationary_event_get(
   unsigned index)
{
   return index < pal_nds_minimap_stationary_event_count &&
      (pal_nds_minimap_stationary_events[index >> 3] &
         (uint8_t)(1u << (index & 7u))) != 0u;
}

static void
pal_nds_minimap_cache_stationary_events(
   const EVENTOBJECT *event_objects,
   unsigned event_object_count)
{
   unsigned i;

   if (event_object_count > PAL_NDS_MINIMAP_SCENE_EVENT_CAPACITY)
   {
      NdsTarget_FatalAt(__FILE__, __LINE__,
         "minimap scene-event profile overflow");
   }
   memset(pal_nds_minimap_stationary_events, 0,
      sizeof(pal_nds_minimap_stationary_events));
   pal_nds_minimap_stationary_event_count = event_object_count;
   for (i = 0u; i < event_object_count; i++)
   {
      if (!pal_nds_minimap_auto_script_moves_event(
            event_objects[i].wAutoScript))
      {
         pal_nds_minimap_stationary_events[i >> 3] |=
            (uint8_t)(1u << (i & 7u));
      }
   }
}

static bool
pal_nds_minimap_event_is_structural_blocker(
   const EVENTOBJECT *event_object,
   unsigned index)
{
   return event_object != NULL &&
      pal_nds_minimap_stationary_event_get(index) &&
      event_object->sVanishTime == 0 &&
      event_object->sState >= kObjStateBlocker &&
      event_object->wTriggerMode == kTriggerNone &&
      event_object->wTriggerScript == 0u;
}

static uint32_t
pal_nds_minimap_structural_signature(
   const EVENTOBJECT *event_objects,
   unsigned event_object_count)
{
   uint32_t signature = 2166136261u;
   unsigned i;

   for (i = 0u; i < event_object_count; i++)
   {
      const EVENTOBJECT *event_object = event_objects + i;

      if (!pal_nds_minimap_event_is_structural_blocker(event_object, i))
      {
         continue;
      }
#define PAL_NDS_MINIMAP_HASH(value) do { \
      signature ^= (uint32_t)(value);       \
      signature *= 16777619u;               \
   } while (0)
      PAL_NDS_MINIMAP_HASH(i);
      PAL_NDS_MINIMAP_HASH(event_object->x);
      PAL_NDS_MINIMAP_HASH(event_object->y);
      PAL_NDS_MINIMAP_HASH(event_object->sState);
#undef PAL_NDS_MINIMAP_HASH
   }
   return signature;
}

static bool
pal_nds_minimap_event_is_touch_exit(
   const EVENTOBJECT *event_object)
{
   return event_object != NULL &&
      event_object->sVanishTime == 0 &&
      event_object->sState > kObjStateHidden &&
      event_object->wTriggerMode >= kTriggerTouchNear &&
      event_object->wTriggerScript != 0u &&
      pal_nds_minimap_script_changes_scene(
         event_object->wTriggerScript);
}

static uint32_t
pal_nds_minimap_portal_signature(
   const EVENTOBJECT *event_objects,
   unsigned event_object_count)
{
   uint32_t signature = 2166136261u;
   unsigned i;

   for (i = 0u; i < event_object_count; i++)
   {
      const EVENTOBJECT *event_object = event_objects + i;

      if (!pal_nds_minimap_event_is_touch_exit(event_object))
      {
         continue;
      }
#define PAL_NDS_MINIMAP_HASH(value) do { \
      signature ^= (uint32_t)(value);       \
      signature *= 16777619u;               \
   } while (0)
      PAL_NDS_MINIMAP_HASH(i);
      PAL_NDS_MINIMAP_HASH(event_object->x);
      PAL_NDS_MINIMAP_HASH(event_object->y);
      PAL_NDS_MINIMAP_HASH(event_object->sState);
      PAL_NDS_MINIMAP_HASH(event_object->wTriggerMode);
      PAL_NDS_MINIMAP_HASH(event_object->wTriggerScript);
#undef PAL_NDS_MINIMAP_HASH
   }
   return signature;
}

static int
pal_nds_minimap_event_palette(
   const EVENTOBJECT *event_object)
{
   if (event_object->sVanishTime != 0 ||
      event_object->sState <= kObjStateHidden)
   {
      return -1;
   }
   if (event_object->wTriggerMode != kTriggerNone &&
      event_object->wTriggerScript != 0u &&
      pal_nds_minimap_script_changes_scene(
         event_object->wTriggerScript))
   {
      return PAL_NDS_MINIMAP_PALETTE_YELLOW;
   }
   if (event_object->nSpriteFrames == 3u)
   {
      return -1;
   }
   if (event_object->sState >= kObjStateBlocker)
   {
      return PAL_NDS_MINIMAP_PALETTE_GRAY;
   }
   if (event_object->wTriggerMode != kTriggerNone &&
      event_object->wTriggerScript != 0u)
   {
      return PAL_NDS_MINIMAP_PALETTE_GREEN;
   }
   return -1;
}

static void
pal_nds_show_console(
   void)
{
   oamDisable(&oamSub);
   if (pal_nds_minimap_bg >= 0)
   {
      bgHide(pal_nds_minimap_bg);
   }
   if (pal_nds_minimap_marker_bg >= 0)
   {
      bgHide(pal_nds_minimap_marker_bg);
   }
   if (pal_nds_console.bgId >= 0)
   {
      bgShow(pal_nds_console.bgId);
   }
   consoleSelect(&pal_nds_console);
}

static bool
pal_nds_minimap_world_to_cell(
   int world_x,
   int world_y,
   int *column,
   int *row)
{
   int raw_x;
   int raw_y;
   int half;
   int residual_x;
   int residual_y;

   if (world_x < 0 || world_y < 0 || column == NULL || row == NULL)
   {
      return false;
   }
   raw_x = world_x / 32;
   raw_y = world_y / 16;
   residual_x = world_x % 32;
   residual_y = world_y % 16;
   half = 0;
   if (residual_x + residual_y * 2 >= 16)
   {
      if (residual_x + residual_y * 2 >= 48)
      {
         raw_x++;
         raw_y++;
      }
      else if (32 - residual_x + residual_y * 2 < 16)
      {
         raw_x++;
      }
      else if (32 - residual_x + residual_y * 2 < 48)
      {
         half = 1;
      }
      else
      {
         raw_y++;
      }
   }
   *column = raw_x + raw_y + half;
   *row = raw_y - raw_x + PAL_NDS_MINIMAP_DIAGONAL_ORIGIN;
   return *column >= 0 && *row >= 0 &&
      *column < (int)PAL_NDS_MINIMAP_LOGICAL_COLUMNS &&
      *row < (int)PAL_NDS_MINIMAP_LOGICAL_ROWS;
}

static bool
pal_nds_minimap_topology_get(
   unsigned column,
   unsigned row)
{
   unsigned bit;

   if (column >= PAL_NDS_MINIMAP_LOGICAL_COLUMNS ||
      row >= PAL_NDS_MINIMAP_LOGICAL_ROWS)
   {
      return false;
   }
   bit = row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS + column;
   return (pal_nds_minimap_topology[bit >> 3] &
      (uint8_t)(1u << (bit & 7u))) != 0u;
}

static void
pal_nds_minimap_topology_clear(
   unsigned column,
   unsigned row)
{
   unsigned bit = row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS + column;

   pal_nds_minimap_topology[bit >> 3] &=
      (uint8_t)~(uint8_t)(1u << (bit & 7u));
}

static void
pal_nds_minimap_topology_set(
   unsigned column,
   unsigned row)
{
   unsigned bit = row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS + column;

   pal_nds_minimap_topology[bit >> 3] |=
      (uint8_t)(1u << (bit & 7u));
}

/* The pattern dictionary is not initialized until after the closure has
 * been selected, so its first topology-sized extent is the closure-blocked
 * mask during that bounded preparation phase. */
static bool
pal_nds_minimap_closure_blocked_get(
   unsigned column,
   unsigned row)
{
   const uint8_t *closure_blocked =
      (const uint8_t *)pal_nds_minimap_pattern_tiles;
   unsigned bit;

   if (column >= PAL_NDS_MINIMAP_LOGICAL_COLUMNS ||
      row >= PAL_NDS_MINIMAP_LOGICAL_ROWS)
   {
      return false;
   }
   bit = row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS + column;
   return (closure_blocked[bit >> 3] &
      (uint8_t)(1u << (bit & 7u))) != 0u;
}

static void
pal_nds_minimap_closure_blocked_set(
   unsigned column,
   unsigned row)
{
   uint8_t *closure_blocked =
      (uint8_t *)pal_nds_minimap_pattern_tiles;
   unsigned bit = row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS + column;

   closure_blocked[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
}

static bool
pal_nds_minimap_topology_near(
   int column,
   int row)
{
   static const int offsets[5][2] = {
      { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
   };
   unsigned i;

   for (i = 0u; i < 5u; i++)
   {
      int neighbor_column = column + offsets[i][0];
      int neighbor_row = row + offsets[i][1];

      if (neighbor_column >= 0 && neighbor_row >= 0 &&
         pal_nds_minimap_topology_get(
            (unsigned)neighbor_column, (unsigned)neighbor_row))
      {
         return true;
      }
   }
   return false;
}

static bool
pal_nds_minimap_find_touch_exit_marker(
   const EVENTOBJECT *event_object,
   int *column,
   int *row)
{
   int center_column;
   int center_row;
   int maximum_steps;
   int steps;

   if (event_object == NULL || column == NULL || row == NULL ||
      event_object->wTriggerMode < kTriggerTouchNear ||
      !pal_nds_minimap_world_to_cell(
         event_object->x, event_object->y,
         &center_column, &center_row))
   {
      return false;
   }
   maximum_steps =
      (int)event_object->wTriggerMode - (int)kTriggerTouchNear;
   for (steps = 0; steps <= maximum_steps; steps++)
   {
      int row_offset;

      for (row_offset = -steps; row_offset <= steps; row_offset++)
      {
         int column_offset = steps -
            (row_offset < 0 ? -row_offset : row_offset);
         int candidate_column = center_column + column_offset;
         int candidate_row = center_row + row_offset;

         if (candidate_column >= 0 && candidate_row >= 0 &&
            !pal_nds_minimap_topology_get(
               (unsigned)candidate_column, (unsigned)candidate_row) &&
            pal_nds_minimap_topology_near(
               candidate_column, candidate_row))
         {
            *column = candidate_column;
            *row = candidate_row;
            return true;
         }
         if (column_offset != 0)
         {
            candidate_column = center_column - column_offset;
            if (candidate_column >= 0 && candidate_row >= 0 &&
               !pal_nds_minimap_topology_get(
                  (unsigned)candidate_column, (unsigned)candidate_row) &&
               pal_nds_minimap_topology_near(
                  candidate_column, candidate_row))
            {
               *column = candidate_column;
               *row = candidate_row;
               return true;
            }
         }
      }
   }
   return false;
}

static uint16_t
pal_nds_minimap_tile_key(
   unsigned column,
   unsigned row)
{
   uint16_t key = 0u;

   key |= pal_nds_minimap_topology_get(column, row) ? 1u << 0 : 0u;
   key |= pal_nds_minimap_topology_get(column + 1u, row) ? 1u << 1 : 0u;
   key |= pal_nds_minimap_topology_get(column, row + 1u) ? 1u << 2 : 0u;
   key |= pal_nds_minimap_topology_get(column + 1u, row + 1u) ?
      1u << 3 : 0u;
   key |= pal_nds_minimap_topology_get(column, row - 1u) ? 1u << 4 : 0u;
   key |= pal_nds_minimap_topology_get(column + 1u, row - 1u) ?
      1u << 5 : 0u;
   key |= pal_nds_minimap_topology_get(column - 1u, row) ? 1u << 6 : 0u;
   key |= pal_nds_minimap_topology_get(column - 1u, row + 1u) ?
      1u << 7 : 0u;
   key |= pal_nds_minimap_topology_get(column + 2u, row) ? 1u << 8 : 0u;
   key |= pal_nds_minimap_topology_get(column + 2u, row + 1u) ?
      1u << 9 : 0u;
   key |= pal_nds_minimap_topology_get(column, row + 2u) ? 1u << 10 : 0u;
   key |= pal_nds_minimap_topology_get(column + 1u, row + 2u) ?
      1u << 11 : 0u;

   /* Neighbors of empty central cells cannot change the rendered tile. */
   key &= (uint16_t)(0x000fu |
      ((key & (1u << 0)) != 0u ? (1u << 4) | (1u << 6) : 0u) |
      ((key & (1u << 1)) != 0u ? (1u << 5) | (1u << 8) : 0u) |
      ((key & (1u << 2)) != 0u ? (1u << 7) | (1u << 10) : 0u) |
      ((key & (1u << 3)) != 0u ? (1u << 9) | (1u << 11) : 0u));
   return key;
}

static void
pal_nds_minimap_make_tile(
   uint16_t key,
   uint8_t *tile)
{
   unsigned cell_y;

   memset(tile, 0, 64u);
   for (cell_y = 0u; cell_y < 2u; cell_y++)
   {
      unsigned cell_x;

      for (cell_x = 0u; cell_x < 2u; cell_x++)
      {
         unsigned cell_bit = cell_y * 2u + cell_x;
         bool occupied = (key & (1u << cell_bit)) != 0u;
         bool occupied_top;
         bool occupied_bottom;
         bool occupied_left;
         bool occupied_right;
         unsigned y;

         if (!occupied)
         {
            continue;
         }
         occupied_top = cell_y != 0u ?
            (key & (1u << (cell_bit - 2u))) != 0u :
            (key & (1u << (4u + cell_x))) != 0u;
         occupied_bottom = cell_y == 0u ?
            (key & (1u << (cell_bit + 2u))) != 0u :
            (key & (1u << (10u + cell_x))) != 0u;
         occupied_left = cell_x != 0u ?
            (key & (1u << (cell_bit - 1u))) != 0u :
            (key & (1u << (6u + cell_y))) != 0u;
         occupied_right = cell_x == 0u ?
            (key & (1u << (cell_bit + 1u))) != 0u :
            (key & (1u << (8u + cell_y))) != 0u;

         for (y = 0u; y < PAL_NDS_MINIMAP_CELL_PIXELS; y++)
         {
            unsigned x;

            for (x = 0u; x < PAL_NDS_MINIMAP_CELL_PIXELS; x++)
            {
               bool white = (!occupied_top && y == 0u) ||
                  (!occupied_bottom && y + 1u ==
                     PAL_NDS_MINIMAP_CELL_PIXELS) ||
                  (!occupied_left && x == 0u) ||
                  (!occupied_right && x + 1u ==
                     PAL_NDS_MINIMAP_CELL_PIXELS);
               unsigned output_x =
                  cell_x * PAL_NDS_MINIMAP_CELL_PIXELS + x;
               unsigned output_y =
                  cell_y * PAL_NDS_MINIMAP_CELL_PIXELS + y;

               tile[output_y * 8u + output_x] =
                  white ? PAL_NDS_MINIMAP_WHITE : PAL_NDS_MINIMAP_BLUE;
            }
         }
      }
   }
}

static bool
pal_nds_minimap_cell_is_closure_blocked(
   int world_x,
   int world_y,
   const EVENTOBJECT *event_objects,
   unsigned closure_blocker_count)
{
   unsigned blocker;

   for (blocker = 0u; blocker < closure_blocker_count; blocker++)
   {
      const EVENTOBJECT *event_object = event_objects +
         pal_nds_minimap_tilemap[blocker];
      int delta_x = (int)event_object->x - world_x;
      int delta_y = (int)event_object->y - world_y;
      uint32_t distance;
      uint32_t blocked_distance = 16u;

      delta_x = delta_x < 0 ? -delta_x : delta_x;
      delta_y = delta_y < 0 ? -delta_y : delta_y;
      distance = (uint32_t)delta_x + (uint32_t)delta_y * 2u;
      if (pal_nds_minimap_event_is_touch_exit(event_object))
      {
         blocked_distance =
            ((uint32_t)event_object->wTriggerMode -
               (uint32_t)kTriggerTouchNear) * 32u + 16u;
      }
      if (distance < blocked_distance)
      {
         return true;
      }
   }
   return false;
}

static bool
pal_nds_minimap_select_component(
   int seed_column,
   int seed_row,
   unsigned *min_column,
   unsigned *max_column,
   unsigned *min_row,
   unsigned *max_row)
{
   static const int offsets[4][2] = {
      { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
   };
   unsigned head = 0u;
   unsigned tail = 0u;
   unsigned column;
   unsigned row;

   /* Scripts can place the party outside ordinary floor topology. Guessing
    * the nearest cell can select an unrelated exterior component, so fail
    * closed until the party reaches a real nonblocking MAP cell. */
   if (seed_column < 0 || seed_row < 0 ||
      !pal_nds_minimap_topology_get(
         (unsigned)seed_column, (unsigned)seed_row) ||
      pal_nds_minimap_closure_blocked_get(
         (unsigned)seed_column, (unsigned)seed_row))
   {
      return false;
   }

   pal_nds_minimap_tilemap[tail++] = (uint16_t)(
      (unsigned)seed_row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS +
      (unsigned)seed_column);
   pal_nds_minimap_topology_clear(
      (unsigned)seed_column, (unsigned)seed_row);
   while (head < tail)
   {
      unsigned cell = pal_nds_minimap_tilemap[head++];
      int cell_column = (int)(cell % PAL_NDS_MINIMAP_LOGICAL_COLUMNS);
      int cell_row = (int)(cell / PAL_NDS_MINIMAP_LOGICAL_COLUMNS);
      unsigned i;

      for (i = 0u; i < 4u; i++)
      {
         int neighbor_column = cell_column + offsets[i][0];
         int neighbor_row = cell_row + offsets[i][1];

         if (neighbor_column < 0 || neighbor_row < 0 ||
            !pal_nds_minimap_topology_get(
               (unsigned)neighbor_column, (unsigned)neighbor_row) ||
            pal_nds_minimap_closure_blocked_get(
               (unsigned)neighbor_column, (unsigned)neighbor_row))
         {
            continue;
         }
         if (tail >= PAL_NDS_MINIMAP_TILEMAP_ENTRIES)
         {
            NdsTarget_FatalAt(__FILE__, __LINE__,
               "minimap component queue overflow");
         }
         pal_nds_minimap_tilemap[tail++] = (uint16_t)(
            (unsigned)neighbor_row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS +
            (unsigned)neighbor_column);
         pal_nds_minimap_topology_clear(
            (unsigned)neighbor_column, (unsigned)neighbor_row);
      }
   }

   memset(pal_nds_minimap_topology, 0,
      sizeof(pal_nds_minimap_topology));
   *min_column = PAL_NDS_MINIMAP_LOGICAL_COLUMNS;
   *max_column = 0u;
   *min_row = PAL_NDS_MINIMAP_LOGICAL_ROWS;
   *max_row = 0u;
   for (head = 0u; head < tail; head++)
   {
      unsigned cell = pal_nds_minimap_tilemap[head];

      column = cell % PAL_NDS_MINIMAP_LOGICAL_COLUMNS;
      row = cell / PAL_NDS_MINIMAP_LOGICAL_COLUMNS;
      pal_nds_minimap_topology_set(column, row);
      *min_column = column < *min_column ? column : *min_column;
      *max_column = column > *max_column ? column : *max_column;
      *min_row = row < *min_row ? row : *min_row;
      *max_row = row > *max_row ? row : *max_row;
   }
   return tail != 0u;
}

static void
pal_nds_minimap_prepare(
   const uint32_t *map_tiles,
   const EVENTOBJECT *event_objects,
   unsigned event_object_count,
   int seed_column,
   int seed_row)
{
   unsigned min_column = PAL_NDS_MINIMAP_LOGICAL_COLUMNS;
   unsigned max_column = 0u;
   unsigned min_row = PAL_NDS_MINIMAP_LOGICAL_ROWS;
   unsigned max_row = 0u;
   unsigned closure_blocker_count = 0u;
   unsigned raw_y;
   bool any;

   memset(pal_nds_minimap_topology, 0,
      sizeof(pal_nds_minimap_topology));
   memset(pal_nds_minimap_pattern_tiles, 0,
      PAL_NDS_MINIMAP_TOPOLOGY_BYTES);

   /* Automatic scene exits and permanent stationary blockers both terminate
    * ordinary walking. Their event indices occupy the queue owner only until
    * topology construction starts. Moving or interactive blockers remain
    * traversable for the complete floor plan. */
   if (event_objects != NULL)
   {
      unsigned i;

      for (i = 0u; i < event_object_count; i++)
      {
         if (!pal_nds_minimap_event_is_touch_exit(event_objects + i) &&
            !pal_nds_minimap_event_is_structural_blocker(
               event_objects + i, i))
         {
            continue;
         }
         if (closure_blocker_count >= PAL_NDS_MINIMAP_TILEMAP_ENTRIES ||
            i > UINT16_MAX)
         {
            NdsTarget_FatalAt(__FILE__, __LINE__,
               "minimap closure-blocker profile overflow");
         }
         pal_nds_minimap_tilemap[closure_blocker_count++] = (uint16_t)i;
      }
   }

   /* MAP records are stored in an isometric (x, y, half-tile) lattice. */
   for (raw_y = 0u; raw_y < PAL_NDS_MINIMAP_RAW_ROWS; raw_y++)
   {
      unsigned raw_x;

      for (raw_x = 0u; raw_x < PAL_NDS_MINIMAP_RAW_COLUMNS; raw_x++)
      {
         unsigned half;

         for (half = 0u; half < PAL_NDS_MINIMAP_RAW_HALVES; half++)
         {
            unsigned index =
               (raw_y * PAL_NDS_MINIMAP_RAW_COLUMNS + raw_x) *
               PAL_NDS_MINIMAP_RAW_HALVES + half;
            uint32_t tile = map_tiles[index];
            unsigned column;
            unsigned row;

            /* Match PAL_MapTileIsBlocked(): visual tile indices, including
             * DWORD zero/GOP frame zero, do not participate in collision. */
            if ((tile & PAL_NDS_MAP_BLOCKED) != 0u)
            {
               continue;
            }
            column = raw_x + raw_y + half;
            row = (unsigned)((int)raw_y - (int)raw_x +
               PAL_NDS_MINIMAP_DIAGONAL_ORIGIN);
            pal_nds_minimap_topology_set(column, row);
            if (closure_blocker_count != 0u &&
               pal_nds_minimap_cell_is_closure_blocked(
                  (int)(raw_x * 32u + half * 16u),
                  (int)(raw_y * 16u + half * 8u),
                  event_objects, closure_blocker_count))
            {
               pal_nds_minimap_closure_blocked_set(column, row);
            }
         }
      }
   }

   any = seed_column >= 0 && seed_row >= 0 &&
      pal_nds_minimap_select_component(
         seed_column, seed_row,
         &min_column, &max_column, &min_row, &max_row);
   memset(pal_nds_minimap_tilemap, 0, sizeof(pal_nds_minimap_tilemap));
   memset(pal_nds_minimap_pattern_tiles, 0xff,
      sizeof(pal_nds_minimap_pattern_tiles));
   memset(pal_nds_minimap_tiles, 0, 64u);
   pal_nds_minimap_pattern_tiles[0] = 0u;
   pal_nds_minimap_tile_count = 1u;

   if (any)
   {
      /* One empty cell keeps blocked stair/event markers adjacent to the
       * reachable component inside the cropped floor-plan extent. */
      min_column = min_column > 0u ? min_column - 1u : min_column;
      min_row = min_row > 0u ? min_row - 1u : min_row;
      max_column = max_column + 1u < PAL_NDS_MINIMAP_LOGICAL_COLUMNS ?
         max_column + 1u : max_column;
      max_row = max_row + 1u < PAL_NDS_MINIMAP_LOGICAL_ROWS ?
         max_row + 1u : max_row;
      unsigned width = max_column - min_column + 1u;
      unsigned height = max_row - min_row + 1u;
      unsigned tile_columns = (width + 1u) / 2u;
      unsigned tile_rows = (height + 1u) / 2u;
      unsigned tile_y;

      pal_nds_minimap_min_column = min_column;
      pal_nds_minimap_min_row = min_row;
      pal_nds_minimap_width_pixels =
         width * PAL_NDS_MINIMAP_CELL_PIXELS;
      pal_nds_minimap_height_pixels =
         height * PAL_NDS_MINIMAP_CELL_PIXELS;
      pal_nds_minimap_has_cells = true;

      for (tile_y = 0u; tile_y < tile_rows; tile_y++)
      {
         unsigned tile_x;

         for (tile_x = 0u; tile_x < tile_columns; tile_x++)
         {
            uint16_t key = pal_nds_minimap_tile_key(
               min_column + tile_x * 2u, min_row + tile_y * 2u);
            uint16_t tile_index = pal_nds_minimap_pattern_tiles[key];

            if (tile_index == UINT16_MAX)
            {
               if (pal_nds_minimap_tile_count >= PAL_NDS_MINIMAP_TILE_COUNT)
               {
                  NdsTarget_FatalAt(__FILE__, __LINE__,
                     "minimap tile profile overflow");
               }
               tile_index = (uint16_t)pal_nds_minimap_tile_count++;
               pal_nds_minimap_pattern_tiles[key] = tile_index;
               pal_nds_minimap_make_tile(key,
                  pal_nds_minimap_tiles + tile_index * 64u);
            }
            pal_nds_minimap_tilemap[
               tile_y * PAL_NDS_MINIMAP_MAP_TILE_COLUMNS + tile_x] =
               tile_index;
         }
      }
   }
   else
   {
      pal_nds_minimap_width_pixels = 0u;
      pal_nds_minimap_height_pixels = 0u;
      pal_nds_minimap_has_cells = false;
   }
   pal_nds_minimap_pending_generation++;
}

static void
pal_nds_minimap_present(
   void)
{
   bool view_changed;
   bool marker_changed;

   if (!pal_nds_minimap_pending_visible)
   {
      oamDisable(&oamSub);
      bgHide(pal_nds_minimap_marker_bg);
      bgHide(pal_nds_minimap_bg);
      return;
   }
   if (pal_nds_minimap_pending_map < 0)
   {
      return;
   }

   if (pal_nds_minimap_map != pal_nds_minimap_pending_map ||
      pal_nds_minimap_generation != pal_nds_minimap_pending_generation)
   {
      dmaCopy(pal_nds_minimap_tiles, bgGetGfxPtr(pal_nds_minimap_bg),
         pal_nds_minimap_tile_count * 64u);
      dmaCopy(pal_nds_minimap_tilemap, bgGetMapPtr(pal_nds_minimap_bg),
         sizeof(pal_nds_minimap_tilemap));
      pal_nds_minimap_map = pal_nds_minimap_pending_map;
      pal_nds_minimap_generation = pal_nds_minimap_pending_generation;
   }
   view_changed =
      pal_nds_minimap_view_x != pal_nds_minimap_pending_view_x ||
      pal_nds_minimap_view_y != pal_nds_minimap_pending_view_y;
   marker_changed =
      pal_nds_minimap_marker_x != pal_nds_minimap_pending_marker_x ||
      pal_nds_minimap_marker_y != pal_nds_minimap_pending_marker_y;
   if (view_changed || marker_changed)
   {
      ArmIrqState irq_state = armIrqLockByPsr();

      /* BG3X/BG3Y are live affine reference registers, not a buffered
       * libnds transform. Commit them and the BG1 player marker as one short
       * VBlank transaction so an interrupt cannot expose mixed coordinates
       * for one hardware frame. The identity matrix is installed once at
       * initialization. */
      if (view_changed)
      {
         REG_BG3X_SUB =
            pal_nds_minimap_pending_view_x * PAL_NDS_MINIMAP_AFFINE_ONE;
         REG_BG3Y_SUB =
            pal_nds_minimap_pending_view_y * PAL_NDS_MINIMAP_AFFINE_ONE;
      }
      if (marker_changed && pal_nds_minimap_pending_marker_x >= 0 &&
         pal_nds_minimap_pending_marker_y >= 0)
      {
         REG_BG1HOFS_SUB = (uint16_t)(
            (256 - pal_nds_minimap_pending_marker_x) & 255);
         REG_BG1VOFS_SUB = (uint16_t)(
            (256 - pal_nds_minimap_pending_marker_y) & 255);
      }
      armIrqUnlockByPsr(irq_state);
      pal_nds_minimap_view_x = pal_nds_minimap_pending_view_x;
      pal_nds_minimap_view_y = pal_nds_minimap_pending_view_y;
      pal_nds_minimap_marker_x = pal_nds_minimap_pending_marker_x;
      pal_nds_minimap_marker_y = pal_nds_minimap_pending_marker_y;
   }
   if (pal_nds_minimap_pending_marker_x >= 0 &&
      pal_nds_minimap_pending_marker_y >= 0)
   {
      bgShow(pal_nds_minimap_marker_bg);
   }
   else
   {
      bgHide(pal_nds_minimap_marker_bg);
   }
   oamEnable(&oamSub);
   oamUpdate(&oamSub);
   bgHide(pal_nds_console.bgId);
   bgShow(pal_nds_minimap_bg);
}

void
NdsTarget_MinimapSetVisible(
   bool visible)
{
   pal_nds_minimap_pending_visible = visible;
}

static void
pal_nds_minimap_update_obstacles(
   const EVENTOBJECT *event_objects,
   unsigned event_object_count,
   int view_x,
   int view_y)
{
   unsigned obstacle_count = 0u;
   unsigned i;

   if (event_objects != NULL && pal_nds_minimap_has_cells)
   {
      for (i = 0u; i < event_object_count; i++)
      {
         const EVENTOBJECT *event_object = event_objects + i;
         int palette;
         int column;
         int row;
         int source_x;
         int source_y;
         int screen_x;
         int screen_y;

         /* Walking characters do not belong on the floor plan. */
         palette = pal_nds_minimap_event_palette(event_object);
         if (palette < 0 ||
            !pal_nds_minimap_world_to_cell(
               event_object->x, event_object->y, &column, &row))
         {
            continue;
         }
         if (palette == PAL_NDS_MINIMAP_PALETTE_YELLOW &&
            event_object->wTriggerMode >= kTriggerTouchNear &&
            !pal_nds_minimap_topology_near(column, row) &&
            !pal_nds_minimap_find_touch_exit_marker(
               event_object, &column, &row))
         {
            continue;
         }
         if (
            column < (int)pal_nds_minimap_min_column ||
            row < (int)pal_nds_minimap_min_row ||
            (unsigned)(column - (int)pal_nds_minimap_min_column) *
               PAL_NDS_MINIMAP_CELL_PIXELS >=
               pal_nds_minimap_width_pixels ||
            (unsigned)(row - (int)pal_nds_minimap_min_row) *
               PAL_NDS_MINIMAP_CELL_PIXELS >=
               pal_nds_minimap_height_pixels ||
            !pal_nds_minimap_topology_near(column, row))
         {
            continue;
         }
         source_x = (column - (int)pal_nds_minimap_min_column) *
            PAL_NDS_MINIMAP_CELL_PIXELS +
            PAL_NDS_MINIMAP_CELL_PIXELS / 2;
         source_y = (row - (int)pal_nds_minimap_min_row) *
            PAL_NDS_MINIMAP_CELL_PIXELS +
            PAL_NDS_MINIMAP_CELL_PIXELS / 2;
         screen_x = source_x - view_x;
         screen_y = source_y - view_y;
         if (screen_x < -2 || screen_x > (int)PAL_TARGET_LCD_WIDTH + 1 ||
            screen_y < -2 || screen_y > (int)PAL_TARGET_LCD_HEIGHT + 1)
         {
            continue;
         }
         if (obstacle_count >= PAL_NDS_MINIMAP_MAX_OBSTACLES)
         {
            NdsTarget_FatalAt(__FILE__, __LINE__,
               "visible minimap obstacle profile overflow");
         }
         oamSet(&oamSub, (int)obstacle_count,
            screen_x - 4, screen_y - 4,
            0, palette, SpriteSize_8x8, SpriteColorFormat_16Color,
            SPRITE_GFX_SUB, -1, false, false,
            false, false, false);
         obstacle_count++;
      }
   }
   for (i = obstacle_count; i < pal_nds_minimap_obstacle_count; i++)
   {
      oamSetHidden(&oamSub, (int)i, true);
   }
   pal_nds_minimap_obstacle_count = obstacle_count;
}

bool
NdsTarget_Begin(
   void)
{
   powerOn(POWER_ALL_2D);
   lcdMainOnTop();

   /* Bring up the sub-screen console first so every later boot stage can
      report progress or a failure on real hardware. */
   videoSetModeSub(MODE_5_2D);
   vramSetBankC(VRAM_C_SUB_BG);
   vramSetBankI(VRAM_I_SUB_SPRITE);
   oamInit(&oamSub, SpriteMapping_1D_32, false);
   consoleInit(&pal_nds_console, 0, BgType_Text4bpp,
      BgSize_T_256x256, PAL_NDS_CONSOLE_MAP_BASE, 0, false, true);
   consoleSelect(&pal_nds_console);
   consoleClear();
   pal_nds_minimap_bg = bgInitSub(
      3, BgType_ExRotation, BgSize_ER_1024x1024,
      PAL_NDS_MINIMAP_MAP_BASE, PAL_NDS_MINIMAP_TILE_BASE);
   pal_nds_minimap_marker_bg = bgInitSub(
      1, BgType_Text4bpp, BgSize_T_256x256,
      PAL_NDS_MINIMAP_MARKER_MAP_BASE,
      PAL_NDS_MINIMAP_MARKER_TILE_BASE);
   if (pal_nds_minimap_bg < 0 || pal_nds_minimap_marker_bg < 0)
   {
      return false;
   }
   dmaFillWords(0, bgGetGfxPtr(pal_nds_minimap_bg),
      PAL_NDS_MINIMAP_TILE_BYTES);
   dmaFillWords(0, bgGetMapPtr(pal_nds_minimap_bg),
      sizeof(pal_nds_minimap_tilemap));
   bgWrapOff(pal_nds_minimap_bg);
   bgSetAffineMatrixScroll(pal_nds_minimap_bg,
      PAL_NDS_MINIMAP_AFFINE_ONE, 0, 0,
      PAL_NDS_MINIMAP_AFFINE_ONE, 0, 0);
   BG_PALETTE_SUB[PAL_NDS_MINIMAP_BLUE] = RGB15(0, 0, 31);
   BG_PALETTE_SUB[PAL_NDS_MINIMAP_WHITE] = RGB15(31, 31, 31);
   armDCacheFlush(pal_nds_minimap_marker_tiles,
      sizeof(pal_nds_minimap_marker_tiles));
   dmaCopy(pal_nds_minimap_marker_tiles,
      bgGetGfxPtr(pal_nds_minimap_marker_bg),
      sizeof(pal_nds_minimap_marker_tiles));
   dmaFillHalfWords(0, bgGetMapPtr(pal_nds_minimap_marker_bg), 2048u);
   bgGetMapPtr(pal_nds_minimap_marker_bg)[0] =
      (uint16_t)(1u | (PAL_NDS_MINIMAP_MARKER_PALETTE_BANK << 12));
   BG_PALETTE_SUB[PAL_NDS_MINIMAP_MARKER_PALETTE_BANK * 16u +
      PAL_NDS_MINIMAP_MARKER_RED] = RGB15(31, 0, 0);
   armDCacheFlush(pal_nds_minimap_obstacle_tile,
      sizeof(pal_nds_minimap_obstacle_tile));
   dmaCopy(pal_nds_minimap_obstacle_tile, SPRITE_GFX_SUB,
      sizeof(pal_nds_minimap_obstacle_tile));
   SPRITE_PALETTE_SUB[PAL_NDS_MINIMAP_PALETTE_GRAY * 16u + 1u] =
      RGB15(15, 15, 15);
   SPRITE_PALETTE_SUB[PAL_NDS_MINIMAP_PALETTE_YELLOW * 16u + 1u] =
      RGB15(31, 31, 0);
   SPRITE_PALETTE_SUB[PAL_NDS_MINIMAP_PALETTE_GREEN * 16u + 1u] =
      RGB15(0, 31, 0);
   bgSetPriority(pal_nds_minimap_marker_bg, 0);
   bgSetPriority(pal_nds_minimap_bg, 1);
   bgSetScroll(pal_nds_minimap_marker_bg,
      (256 - (PAL_NDS_MINIMAP_SCREEN_CENTER_X -
         PAL_NDS_MINIMAP_MARKER_CENTER)) & 255,
      (256 - (PAL_NDS_MINIMAP_SCREEN_CENTER_Y -
         PAL_NDS_MINIMAP_MARKER_CENTER)) & 255);
   bgUpdate();
   bgHide(pal_nds_minimap_bg);
   bgHide(pal_nds_minimap_marker_bg);
   iprintf("SDLPAL Nintendo DS\n");
   iprintf("commit %s\n", NDS_GIT_REVISION);
   iprintf("mode: %s\n\n", isDSiMode() ? "TWL rejected" : "NTR only");
   iprintf("console ok\n");

   videoSetMode(MODE_5_2D);
   vramSetBankA(VRAM_A_MAIN_BG);
   pal_nds_bg = bgInit(
      3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
   if (pal_nds_bg < 0)
   {
      return false;
   }
   dmaFillWords(0, BG_BMP_RAM(0), 2u * PAL_NDS_BITMAP_PAGE_BYTES);
   dmaFillWords(0, BG_PALETTE, 512u);
   bgSetPriority(pal_nds_bg, 0);
   pal_nds_visible_page = 0u;
   pal_nds_present_count = 0u;
   iprintf("main display ok\n");

   pal_nds_started = true;
   return true;
}

void
NdsTarget_BootLog(
   const char *line)
{
   pal_nds_show_console();
   iprintf("%s\n", line != NULL ? line : "?");
}

void
NdsTarget_ShowError(
   const char *title,
   const char *detail)
{
   if (!pal_nds_started)
   {
      return;
   }
   pal_nds_show_console();
   consoleClear();
   iprintf("%s\n\n%s\n",
      title != NULL ? title : "ERROR",
      detail != NULL ? detail : "unknown failure");
}

void
NdsTarget_ShowReady(
   bool save_available,
   int save_type,
   uint32_t save_bytes)
{
   if (!pal_nds_started)
   {
      return;
   }
   pal_nds_show_console();
   if (save_type == 3)
   {
      iprintf("\nROM  Slot-1 NitroFS\n");
      if (save_available)
      {
         iprintf("SAVE SPI FLASH %lu KiB\n",
            (unsigned long)(save_bytes / 1024u));
      }
      else
      {
         iprintf("SAVE SPI FLASH unavailable\n");
         iprintf("     needs 1 MiB type-3\n");
      }
   }
   else
   {
      iprintf("\nROM  DLDI self-ROM NitroFS\n");
      if (save_available)
      {
         iprintf("SAVE DLDI FAT 5 x %lu KiB\n",
            (unsigned long)(save_bytes / (5u * 1024u)));
      }
      else
      {
         iprintf("SAVE DLDI FAT unavailable\n");
         iprintf("     needs writable DLDI FAT\n");
      }
   }
   iprintf("\nA confirm   B menu/back\n");
   iprintf("D-pad move  X status\n");
}

void
NdsTarget_MinimapSetMap(
   int map_number,
   int scene_number,
   const uint32_t *map_tiles,
   const struct tagEVENTOBJECT *event_objects,
   unsigned event_object_count,
   int world_x,
   int world_y)
{
   int column = -1;
   int row = -1;
   int source_x = -1;
   int source_y = -1;
   int view_x = 0;
   int view_y = 0;
   int marker_x = -1;
   int marker_y = -1;
   bool player_cell_valid;
   uint32_t portal_signature;
   uint32_t structural_signature;
   bool component_missing;
   bool component_seed_changed;
   bool scene_changed;

   if (!pal_nds_started || map_number < 0 || scene_number <= 0 ||
      map_tiles == NULL ||
      (event_object_count != 0u && event_objects == NULL))
   {
      return;
   }
   scene_changed = pal_nds_minimap_pending_scene != scene_number;
   if (scene_changed ||
      pal_nds_minimap_stationary_event_count != event_object_count)
   {
      pal_nds_minimap_cache_stationary_events(
         event_objects, event_object_count);
   }
   portal_signature = pal_nds_minimap_portal_signature(
      event_objects, event_object_count);
   structural_signature = pal_nds_minimap_structural_signature(
      event_objects, event_object_count);
   player_cell_valid = world_x >= 0 && world_y >= 0 &&
      pal_nds_minimap_world_to_cell(
         world_x, world_y, &column, &row);
   component_missing = player_cell_valid &&
      (!pal_nds_minimap_has_cells ||
         !pal_nds_minimap_topology_get(
            (unsigned)column, (unsigned)row));
   component_seed_changed = player_cell_valid &&
      (column != pal_nds_minimap_component_seed_column ||
         row != pal_nds_minimap_component_seed_row);
   if (pal_nds_minimap_pending_map != map_number ||
      pal_nds_minimap_pending_scene != scene_number ||
      pal_nds_minimap_pending_portal_signature != portal_signature ||
      pal_nds_minimap_pending_structural_signature != structural_signature ||
      (component_missing && component_seed_changed))
   {
      pal_nds_minimap_prepare(
         map_tiles,
         event_objects,
         event_object_count,
         player_cell_valid ? column : -1,
         player_cell_valid ? row : -1);
      pal_nds_minimap_pending_map = map_number;
      pal_nds_minimap_pending_scene = scene_number;
      pal_nds_minimap_pending_portal_signature = portal_signature;
      pal_nds_minimap_pending_structural_signature = structural_signature;
      pal_nds_minimap_component_seed_column =
         player_cell_valid ? column : -1;
      pal_nds_minimap_component_seed_row =
         player_cell_valid ? row : -1;
   }

   if (player_cell_valid && pal_nds_minimap_has_cells)
   {
      if (column >= (int)pal_nds_minimap_min_column &&
         row >= (int)pal_nds_minimap_min_row &&
         column < (int)PAL_NDS_MINIMAP_LOGICAL_COLUMNS &&
         row < (int)PAL_NDS_MINIMAP_LOGICAL_ROWS)
      {
         source_x = (column - (int)pal_nds_minimap_min_column) *
            PAL_NDS_MINIMAP_CELL_PIXELS +
            PAL_NDS_MINIMAP_CELL_PIXELS / 2;
         source_y = (row - (int)pal_nds_minimap_min_row) *
            PAL_NDS_MINIMAP_CELL_PIXELS +
            PAL_NDS_MINIMAP_CELL_PIXELS / 2;
         if (source_x >= (int)pal_nds_minimap_width_pixels ||
            source_y >= (int)pal_nds_minimap_height_pixels)
         {
            source_x = -1;
            source_y = -1;
         }
      }
   }
   if (source_x >= 0 && source_y >= 0)
   {
      if (pal_nds_minimap_width_pixels <= PAL_TARGET_LCD_WIDTH)
      {
         view_x = -((int)PAL_TARGET_LCD_WIDTH -
            (int)pal_nds_minimap_width_pixels) / 2;
      }
      else
      {
         int maximum = (int)pal_nds_minimap_width_pixels -
            (int)PAL_TARGET_LCD_WIDTH;

         view_x = source_x - PAL_NDS_MINIMAP_SCREEN_CENTER_X;
         view_x = view_x < 0 ? 0 : (view_x > maximum ? maximum : view_x);
      }
      if (pal_nds_minimap_height_pixels <= PAL_TARGET_LCD_HEIGHT)
      {
         view_y = -((int)PAL_TARGET_LCD_HEIGHT -
            (int)pal_nds_minimap_height_pixels) / 2;
      }
      else
      {
         int maximum = (int)pal_nds_minimap_height_pixels -
            (int)PAL_TARGET_LCD_HEIGHT;

         view_y = source_y - PAL_NDS_MINIMAP_SCREEN_CENTER_Y;
         view_y = view_y < 0 ? 0 : (view_y > maximum ? maximum : view_y);
      }
      marker_x = source_x - view_x - PAL_NDS_MINIMAP_MARKER_CENTER;
      marker_y = source_y - view_y - PAL_NDS_MINIMAP_MARKER_CENTER;
      marker_x = marker_x < 0 ? 0 : (marker_x > 248 ? 248 : marker_x);
      marker_y = marker_y < 0 ? 0 : (marker_y > 184 ? 184 : marker_y);
   }
   pal_nds_minimap_pending_view_x = view_x;
   pal_nds_minimap_pending_view_y = view_y;
   pal_nds_minimap_pending_marker_x = marker_x;
   pal_nds_minimap_pending_marker_y = marker_y;
   pal_nds_minimap_update_obstacles(
      event_objects, event_object_count, view_x, view_y);
}

void
NdsTarget_FatalAt(
   const char *file,
   uint32_t line,
   const char *reason)
{
   if (pal_nds_started)
   {
      dmaFillWords(0, BG_BMP_RAM(0), 2u * PAL_NDS_BITMAP_PAGE_BYTES);
      dmaFillWords(0, BG_PALETTE, 512u);
      bgSetMapBase(pal_nds_bg, 0);
      pal_nds_show_console();
      consoleClear();
      iprintf("GURU MEDITATION\n\n");
      iprintf("%s:%lu\n",
         file != NULL ? file : "?", (unsigned long)line);
      iprintf("commit %s\n\n", NDS_GIT_REVISION);
      iprintf("%s\n", reason != NULL ? reason : "fatal error");
   }
   for (;;)
   {
      swiWaitForVBlank();
   }
}

bool
NdsTarget_FlushIndexedFramebuffer(
   const uint8_t *pixels,
   uint16_t pitch,
   const uint8_t *palette_rgba)
{
   unsigned hidden_page;
   uint8_t *hidden;
   unsigned i;

   if (!pal_nds_started || pixels == NULL || palette_rgba == NULL ||
      pitch != 256u)
   {
      return false;
   }

   NdsTarget_AudioPump();

   hidden_page = pal_nds_visible_page ^ 1u;
   hidden = (uint8_t *)BG_BMP_RAM(
      hidden_page != 0u ? PAL_NDS_SECOND_PAGE_MAP_BASE : 0u);
   for (i = 0u; i < 256u; i++)
   {
      const uint8_t *color = palette_rgba + i * 4u;
      pal_nds_palette[i] = RGB15(
         color[0] >> 3, color[1] >> 3, color[2] >> 3);
   }

   /* Both DMA sources live in cached ARM9 main RAM.  Flush once after the
      complete indexed frame and palette have been produced. */
   armDCacheFlushAll();

   /* The destination page is hidden, so populate it before VBlank. Keeping
    * this 48KiB DMA out of the display commit window leaves the lower-screen
    * affine and marker register transaction at the start of VBlank. */
   dmaCopy(pixels, hidden, PAL_NDS_VISIBLE_BYTES);
   threadWaitForVBlank();
   if (!lcdInVBlank())
   {
      /* A peer audio worker can finish after the interrupt that woke us.
       * Defer the register commit instead of writing during active scanout. */
      threadWaitForVBlank();
   }
   pal_nds_minimap_present();
   dmaCopy(pal_nds_palette, BG_PALETTE, sizeof(pal_nds_palette));
   bgSetMapBase(pal_nds_bg,
      hidden_page != 0u ? PAL_NDS_SECOND_PAGE_MAP_BASE : 0u);
   pal_nds_visible_page = hidden_page;
   pal_nds_present_count++;
   return true;
}
