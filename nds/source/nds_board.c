#include "pal_target_board.h"

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
};

static PrintConsole pal_nds_console;
static int pal_nds_bg;
static unsigned pal_nds_visible_page;
volatile uint32_t pal_nds_present_count
   __attribute__((section(".bss.pal_nds_video")));
static uint16_t pal_nds_palette[256] __attribute__((aligned(4)));
static bool pal_nds_started;

bool
NdsTarget_Begin(
   void)
{
   powerOn(POWER_ALL_2D);
   lcdMainOnTop();

   /* Bring up the sub-screen console first so every later boot stage can
      report progress or a failure on real hardware. */
   videoSetModeSub(MODE_0_2D);
   vramSetBankC(VRAM_C_SUB_BG);
   consoleInit(&pal_nds_console, 0, BgType_Text4bpp,
      BgSize_T_256x256, 31, 0, false, true);
   consoleSelect(&pal_nds_console);
   consoleClear();
   iprintf("SDLPAL Nintendo DS\n");
   iprintf("commit %s\n", NDS_GIT_REVISION);
   iprintf("mode: %s\n\n", isDSiMode() ? "TWL (DSi)" : "NTR (DS)");
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
   consoleSelect(&pal_nds_console);
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
   consoleSelect(&pal_nds_console);
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
   consoleSelect(&pal_nds_console);
   iprintf("\nROM  NitroFS pal_full.pak\n");
   if (save_available)
   {
      iprintf("SAVE FAT /sdlpal %lu KiB\n",
         (unsigned long)(save_bytes / 1024u));
   }
   else
   {
      iprintf("SAVE unavailable backend=%d\n", save_type);
      iprintf("     needs a writable DLDI device\n");
   }
   iprintf("\nA confirm   B menu/back\n");
   iprintf("D-pad move  X status\n");
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
      consoleSelect(&pal_nds_console);
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

   threadWaitForVBlank();
   dmaCopy(pixels, hidden, PAL_NDS_VISIBLE_BYTES);
   dmaCopy(pal_nds_palette, BG_PALETTE, sizeof(pal_nds_palette));
   bgSetMapBase(pal_nds_bg,
      hidden_page != 0u ? PAL_NDS_SECOND_PAGE_MAP_BASE : 0u);
   pal_nds_visible_page = hidden_page;
   pal_nds_present_count++;
   return true;
}
