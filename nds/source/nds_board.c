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
};

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

   if (world_x < 0 || world_y < 0 || column == NULL || row == NULL)
   {
      return false;
   }
   raw_x = world_x / 32;
   raw_y = world_y / 16;
   half = (world_x & 31) != 0 ? 1 : 0;
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
pal_nds_minimap_topology_set(
   unsigned column,
   unsigned row)
{
   unsigned bit = row * PAL_NDS_MINIMAP_LOGICAL_COLUMNS + column;

   pal_nds_minimap_topology[bit >> 3] |=
      (uint8_t)(1u << (bit & 7u));
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

static void
pal_nds_minimap_prepare(
   const uint32_t *map_tiles)
{
   unsigned min_column = PAL_NDS_MINIMAP_LOGICAL_COLUMNS;
   unsigned max_column = 0u;
   unsigned min_row = PAL_NDS_MINIMAP_LOGICAL_ROWS;
   unsigned max_row = 0u;
   unsigned raw_y;
   bool any = false;

   memset(pal_nds_minimap_topology, 0,
      sizeof(pal_nds_minimap_topology));
   memset(pal_nds_minimap_tilemap, 0, sizeof(pal_nds_minimap_tilemap));
   memset(pal_nds_minimap_pattern_tiles, 0xff,
      sizeof(pal_nds_minimap_pattern_tiles));
   memset(pal_nds_minimap_tiles, 0, 64u);
   pal_nds_minimap_pattern_tiles[0] = 0u;
   pal_nds_minimap_tile_count = 1u;

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

            if (tile == 0u || (tile & PAL_NDS_MAP_BLOCKED) != 0u)
            {
               continue;
            }
            column = raw_x + raw_y + half;
            row = (unsigned)((int)raw_y - (int)raw_x +
               PAL_NDS_MINIMAP_DIAGONAL_ORIGIN);
            pal_nds_minimap_topology_set(column, row);
            min_column = column < min_column ? column : min_column;
            max_column = column > max_column ? column : max_column;
            min_row = row < min_row ? row : min_row;
            max_row = row > max_row ? row : max_row;
            any = true;
         }
      }
   }

   if (any)
   {
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
}

static void
pal_nds_minimap_present(
   void)
{
   if (pal_nds_minimap_pending_map < 0)
   {
      return;
   }

   if (pal_nds_minimap_map != pal_nds_minimap_pending_map)
   {
      dmaCopy(pal_nds_minimap_tiles, bgGetGfxPtr(pal_nds_minimap_bg),
         pal_nds_minimap_tile_count * 64u);
      dmaCopy(pal_nds_minimap_tilemap, bgGetMapPtr(pal_nds_minimap_bg),
         sizeof(pal_nds_minimap_tilemap));
      pal_nds_minimap_map = pal_nds_minimap_pending_map;
   }
   if (pal_nds_minimap_view_x != pal_nds_minimap_pending_view_x ||
      pal_nds_minimap_view_y != pal_nds_minimap_pending_view_y)
   {
      /* Keep the map orthogonal at one source pixel per screen pixel.
         Movement changes only the affine reference point. */
      bgSetAffineMatrixScroll(pal_nds_minimap_bg,
         PAL_NDS_MINIMAP_AFFINE_ONE, 0, 0,
         PAL_NDS_MINIMAP_AFFINE_ONE,
         pal_nds_minimap_pending_view_x * PAL_NDS_MINIMAP_AFFINE_ONE,
         pal_nds_minimap_pending_view_y * PAL_NDS_MINIMAP_AFFINE_ONE);
      pal_nds_minimap_view_x = pal_nds_minimap_pending_view_x;
      pal_nds_minimap_view_y = pal_nds_minimap_pending_view_y;
   }
   if (pal_nds_minimap_marker_x != pal_nds_minimap_pending_marker_x ||
      pal_nds_minimap_marker_y != pal_nds_minimap_pending_marker_y)
   {
      if (pal_nds_minimap_pending_marker_x >= 0 &&
         pal_nds_minimap_pending_marker_y >= 0)
      {
         bgSetScroll(pal_nds_minimap_marker_bg,
            (256 - pal_nds_minimap_pending_marker_x) & 255,
            (256 - pal_nds_minimap_pending_marker_y) & 255);
         bgUpdate();
      }
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
         int column;
         int row;
         int source_x;
         int source_y;
         int screen_x;
         int screen_y;

         if (event_object->sState < kObjStateBlocker ||
            !pal_nds_minimap_world_to_cell(
               event_object->x, event_object->y, &column, &row) ||
            column < (int)pal_nds_minimap_min_column ||
            row < (int)pal_nds_minimap_min_row ||
            !pal_nds_minimap_topology_get(
               (unsigned)column, (unsigned)row))
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
            0, 0, SpriteSize_8x8, SpriteColorFormat_16Color,
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
   SPRITE_PALETTE_SUB[1] = RGB15(15, 15, 15);
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
   (void)save_type;
   iprintf("\nROM  DLDI self-ROM NitroFS\n");
   if (save_available)
   {
      iprintf("SAVE DLDI FAT 5 x %lu KiB\n",
         (unsigned long)(save_bytes / (5u * 1024u)));
   }
   else
   {
      iprintf("SAVE unavailable backend=%d\n", save_type);
      iprintf("     needs writable DLDI FAT\n");
   }
   iprintf("\nA confirm   B menu/back\n");
   iprintf("D-pad move  X status\n");
}

void
NdsTarget_MinimapSetMap(
   int map_number,
   const uint32_t *map_tiles,
   const struct tagEVENTOBJECT *event_objects,
   unsigned event_object_count,
   int world_x,
   int world_y)
{
   int column;
   int row;
   int source_x = -1;
   int source_y = -1;
   int view_x = 0;
   int view_y = 0;
   int marker_x = -1;
   int marker_y = -1;

   if (!pal_nds_started || map_number < 0 || map_tiles == NULL)
   {
      return;
   }
   if (pal_nds_minimap_pending_map != map_number)
   {
      pal_nds_minimap_prepare(map_tiles);
      pal_nds_minimap_pending_map = map_number;
   }

   if (world_x >= 0 && world_y >= 0 && pal_nds_minimap_has_cells)
   {
      if (pal_nds_minimap_world_to_cell(
            world_x, world_y, &column, &row) &&
         column >= (int)pal_nds_minimap_min_column &&
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

   threadWaitForVBlank();
   pal_nds_minimap_present();
   dmaCopy(pixels, hidden, PAL_NDS_VISIBLE_BYTES);
   dmaCopy(pal_nds_palette, BG_PALETTE, sizeof(pal_nds_palette));
   bgSetMapBase(pal_nds_bg,
      hidden_page != 0u ? PAL_NDS_SECOND_PAGE_MAP_BASE : 0u);
   pal_nds_visible_page = hidden_page;
   pal_nds_present_count++;
   return true;
}
