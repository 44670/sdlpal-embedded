/* -*- mode: c; tab-width: 4; c-basic-offset: 4; c-file-style: "linux" -*- */
//
// Copyright (c) 2009-2011, Wei Mingzhi <whistler_wmz@users.sf.net>.
// Copyright (c) 2011-2026, SDLPAL development team.
// All rights reserved.
//
// This file is part of SDLPAL.
//
// SDLPAL is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 3
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "main.h"

#define PAL_PALETTE_COLORS 256
#define PAL_PALETTE_RGB_BYTES (PAL_PALETTE_COLORS * 3)
#define PAL_PALETTE_PAT_BYTES (PAL_PALETTE_RGB_BYTES * 2)
#define PAL_PALETTE_SDL_BYTES (PAL_PALETTE_COLORS * sizeof(SDL_Color))

#if defined(__GNUC__) && (defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS))
#define PAL_PALETTE_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#else
#define PAL_PALETTE_SRAM
#endif

static uint8_t pal_sram_palette_base[PAL_PALETTE_SDL_BYTES] PAL_PALETTE_SRAM;
static uint8_t pal_sram_palette_work[PAL_PALETTE_SDL_BYTES] PAL_PALETTE_SRAM;
static uint8_t pal_sram_palette_next[PAL_PALETTE_SDL_BYTES] PAL_PALETTE_SRAM;

static SDL_Color *
PAL_PaletteBase(
   VOID
)
{
   return (SDL_Color *)pal_sram_palette_base;
}

static SDL_Color *
PAL_PaletteWork(
   VOID
)
{
   return (SDL_Color *)pal_sram_palette_work;
}

static SDL_Color *
PAL_PaletteNext(
   VOID
)
{
   return (SDL_Color *)pal_sram_palette_next;
}

SDL_Color *
PAL_GetPalette(
   INT         iPaletteNum,
   BOOL        fNight
)
/*++
  Purpose:

    Get the specified palette in pat.mkf file.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

  Return value:

    Pointer to the palette. NULL if failed.

--*/
{
   SDL_Color            *palette = PAL_PaletteBase();
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   LPCBYTE               buf;
#else
   PAL_LARGE BYTE        buf[1536];
#endif
   INT                   i;
   UINT                  bufSize;
   FILE                 *fp;

   memset(palette, 0xff, PAL_PALETTE_SDL_BYTES);

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   fp = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_PAT);
   if (fp == NULL || !PAL_MKFMapChunk(fp, iPaletteNum, &buf, &bufSize) ||
      bufSize < PAL_PALETTE_RGB_BYTES)
   {
      return NULL;
   }
#else
   fp = UTIL_OpenRequiredFile("pat.mkf");

   //
   // Read the palette data from the pat.mkf file
   //
   i = PAL_MKFReadChunk(buf, 1536, iPaletteNum, fp);

   fclose(fp);

   if (i < 0)
   {
      //
      // Read failed
      //
      return NULL;
   }
   bufSize = (UINT)i;
#endif

   if (bufSize <= PAL_PALETTE_RGB_BYTES)
   {
      //
      // There is no night colors in the palette
      //
      fNight = FALSE;
   }

   for (i = 0; i < PAL_PALETTE_COLORS; i++)
   {
      palette[i].r = buf[(fNight ? PAL_PALETTE_RGB_BYTES : 0) + i * 3] << 2;
      palette[i].g = buf[(fNight ? PAL_PALETTE_RGB_BYTES : 0) + i * 3 + 1] << 2;
      palette[i].b = buf[(fNight ? PAL_PALETTE_RGB_BYTES : 0) + i * 3 + 2] << 2;
#if 0
      palette[i].r += (255 - palette[i].r) / 5;
      palette[i].g += (255 - palette[i].g) / 5;
      palette[i].b += (255 - palette[i].b) / 5;
#endif
   }

   return palette;
}

VOID
PAL_SetPalette(
   INT         iPaletteNum,
   BOOL        fNight
)
/*++
  Purpose:

    Set the screen palette to the specified one.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

  Return value:

    None.

--*/
{
   SDL_Color *p = PAL_GetPalette(iPaletteNum, fNight);

   if (p != NULL)
   {
      VIDEO_SetPalette(p);
   }
}

VOID
PAL_FadeOut(
   INT         iDelay
)
/*++
  Purpose:

    Fadeout screen to black from the specified palette.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  iDelay - delay time for each step.

  Return value:

    None.

--*/
{
   int                      i, j;
   UINT                     time;
   SDL_Color               *palette = PAL_PaletteWork();
   SDL_Color               *newpalette = PAL_PaletteNext();

   //
   // Get the original palette...
   //
   for (i = 0; i < PAL_PALETTE_COLORS; i++)
   {
      palette[i] = VIDEO_GetPalette()[i];
   }

   memset(newpalette, 0xff, PAL_PALETTE_SDL_BYTES);

   //
   // Start fading out...
   //
   time = SDL_GetTicks() + iDelay * 10 * 60;

   while (TRUE)
   {
      //
      // Set the current palette...
      //
      j = (int)(time - SDL_GetTicks()) / iDelay / 10;
      if (j < 0)
      {
         break;
      }

      for (i = 0; i < PAL_PALETTE_COLORS; i++)
      {
         newpalette[i].r = (palette[i].r * j) >> 6;
         newpalette[i].g = (palette[i].g * j) >> 6;
         newpalette[i].b = (palette[i].b * j) >> 6;
      }

      VIDEO_SetPalette(newpalette);

      UTIL_Delay(10);
   }

   memset(newpalette, 0, PAL_PALETTE_SDL_BYTES);
   VIDEO_SetPalette(newpalette);
}

VOID
PAL_FadeIn(
   INT         iPaletteNum,
   BOOL        fNight,
   INT         iDelay
)
/*++
  Purpose:

    Fade in the screen to the specified palette.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  iDelay - delay time for each step.

  Return value:

    None.

--*/
{
   int                      i, j;
   UINT                     time;
   SDL_Color               *palette;
   SDL_Color               *newpalette = PAL_PaletteNext();
    
   memset(newpalette, 0xff, PAL_PALETTE_SDL_BYTES);

   //
   // Get the new palette...
   //
   palette = PAL_GetPalette(iPaletteNum, fNight);
   if (palette == NULL)
   {
      return;
   }

   //
   // Start fading in...
   //
   time = SDL_GetTicks() + iDelay * 10 * 60;
   while (TRUE)
   {
      //
      // Set the current palette...
      //
      j = (int)(time - SDL_GetTicks()) / iDelay / 10;
      if (j < 0)
      {
         break;
      }

      j = 60 - j;

      for (i = 0; i < PAL_PALETTE_COLORS; i++)
      {
         newpalette[i].r = (palette[i].r * j) >> 6;
         newpalette[i].g = (palette[i].g * j) >> 6;
         newpalette[i].b = (palette[i].b * j) >> 6;
      }

      VIDEO_SetPalette(newpalette);

      UTIL_Delay(10);
   }

   VIDEO_SetPalette(palette);
}

VOID
PAL_SceneFade(
   INT         iPaletteNum,
   BOOL        fNight,
   INT         iStep
)
/*++
  Purpose:

    Fade in or fade out the screen. Update the scene during the process.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  iStep - positive to fade in, nagative to fade out.

  Return value:

    None.

--*/
{
   SDL_Color            *palette, *newpalette = PAL_PaletteNext();
   int                   i, j;
   DWORD                 time;

   memset(newpalette, 0xff, PAL_PALETTE_SDL_BYTES);

   palette = PAL_GetPalette(iPaletteNum, fNight);

   if (palette == NULL)
   {
      return;
   }

   if (iStep == 0)
   {
      iStep = 1;
   }

   gpGlobals->fNeedToFadeIn = FALSE;

   if (iStep > 0)
   {
      for (i = 0; i < 64; i += iStep)
      {
         time = SDL_GetTicks() + 100;

         //
         // Generate the scene
         //
         PAL_ClearKeyState();
         g_InputState.dir = kDirUnknown;
         g_InputState.prevdir = kDirUnknown;
         PAL_GameUpdate(FALSE);
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);

         //
         // Calculate the current palette...
         //
         for (j = 0; j < PAL_PALETTE_COLORS; j++)
         {
            newpalette[j].r = (palette[j].r * i) >> 6;
            newpalette[j].g = (palette[j].g * i) >> 6;
            newpalette[j].b = (palette[j].b * i) >> 6;
         }
         VIDEO_SetPalette(newpalette);

         PAL_ProcessEvent();

         while (!SDL_TICKS_PASSED(SDL_GetTicks(), time))
         {
            PAL_ProcessEvent();
            SDL_Delay(5);
         }
      }
   }
   else
   {
      for (i = 63; i >= 0; i += iStep)
      {
         time = SDL_GetTicks() + 100;

         //
         // Generate the scene
         //
         PAL_ClearKeyState();
         g_InputState.dir = kDirUnknown;
         g_InputState.prevdir = kDirUnknown;
         PAL_GameUpdate(FALSE);
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);

         //
         // Calculate the current palette...
         //
         for (j = 0; j < PAL_PALETTE_COLORS; j++)
         {
            newpalette[j].r = (palette[j].r * i) >> 6;
            newpalette[j].g = (palette[j].g * i) >> 6;
            newpalette[j].b = (palette[j].b * i) >> 6;
         }
         VIDEO_SetPalette(newpalette);

         PAL_ProcessEvent();

         while (!SDL_TICKS_PASSED(SDL_GetTicks(), time))
         {
            PAL_ProcessEvent();
            SDL_Delay(5);
         }
      }
   }
}

VOID
PAL_PaletteFade(
   INT         iPaletteNum,
   BOOL        fNight,
   BOOL        fUpdateScene
)
/*++
  Purpose:

    Fade from the current palette to the specified one.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  fUpdateScene - TRUE if update the scene in the progress.

  Return value:

    None.

--*/
{
   int            i, j;
   UINT           time;
   SDL_Color     *newpalette = PAL_GetPalette(iPaletteNum, fNight);
   SDL_Color     *palette = PAL_PaletteWork();
   SDL_Color     *t = PAL_PaletteNext();

   memset(t, 0xff, PAL_PALETTE_SDL_BYTES);

   if (newpalette == NULL)
   {
      return;
   }

   for (i = 0; i < PAL_PALETTE_COLORS; i++)
   {
      palette[i] = VIDEO_GetPalette()[i];
   }

   //
   // Start fading...
   //
   for (i = 0; i < 32; i++)
   {
      time = SDL_GetTicks() + (fUpdateScene ? FRAME_TIME : FRAME_TIME / 4);

      for (j = 0; j < PAL_PALETTE_COLORS; j++)
      {
         t[j].r =
            (BYTE)(((INT)(palette[j].r) * (31 - i) + (INT)(newpalette[j].r) * i) / 31);
         t[j].g =
            (BYTE)(((INT)(palette[j].g) * (31 - i) + (INT)(newpalette[j].g) * i) / 31);
         t[j].b =
            (BYTE)(((INT)(palette[j].b) * (31 - i) + (INT)(newpalette[j].b) * i) / 31);
      }
      VIDEO_SetPalette(t);

      if (fUpdateScene)
      {
         PAL_ClearKeyState();
         g_InputState.dir = kDirUnknown;
         g_InputState.prevdir = kDirUnknown;
         PAL_GameUpdate(FALSE);
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);
      }

      PAL_ProcessEvent();

      while (!SDL_TICKS_PASSED(SDL_GetTicks(), time))
      {
         PAL_ProcessEvent();
         SDL_Delay(5);
      }
   }
}

VOID
PAL_ColorFade(
   INT        iDelay,
   BYTE       bColor,
   BOOL       fFrom
)
/*++
  Purpose:

    Fade the palette from/to the specified color.

  Parameters:

    [IN]  iDelay - the delay time of each step.

    [IN]  bColor - the color to fade from/to.

    [IN]  fFrom - if TRUE then fade from bColor, else fade to bColor.

  Return value:

    None.

--*/
{
   SDL_Color       *palette;
   SDL_Color       *newpalette = PAL_PaletteNext();
   int              i, j;

   memset(newpalette, 0xff, PAL_PALETTE_SDL_BYTES);

   palette = PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
   if (palette == NULL)
   {
      return;
   }

   iDelay *= 10;
   if (iDelay == 0)
   {
      iDelay = 10;
   }

   if (fFrom)
   {
      for (i = 0; i < PAL_PALETTE_COLORS; i++)
      {
         newpalette[i] = palette[bColor];
      }

      for (i = 0; i < 64; i++)
      {
         for (j = 0; j < PAL_PALETTE_COLORS; j++)
         {
            if (newpalette[j].r > palette[j].r)
            {
               newpalette[j].r -= 4;
            }
            else if (newpalette[j].r < palette[j].r)
            {
               newpalette[j].r += 4;
            }

            if (newpalette[j].g > palette[j].g)
            {
               newpalette[j].g -= 4;
            }
            else if (newpalette[j].g < palette[j].g)
            {
               newpalette[j].g += 4;
            }

            if (newpalette[j].b > palette[j].b)
            {
               newpalette[j].b -= 4;
            }
            else if (newpalette[j].b < palette[j].b)
            {
               newpalette[j].b += 4;
            }
         }

         VIDEO_SetPalette(newpalette);
         UTIL_Delay(iDelay);
      }

      VIDEO_SetPalette(palette);
   }
   else
   {
      memcpy(newpalette, palette, PAL_PALETTE_SDL_BYTES);

      for (i = 0; i < 64; i++)
      {
         for (j = 0; j < PAL_PALETTE_COLORS; j++)
         {
            if (newpalette[j].r > palette[bColor].r)
            {
               newpalette[j].r -= 4;
            }
            else if (newpalette[j].r < palette[bColor].r)
            {
               newpalette[j].r += 4;
            }

            if (newpalette[j].g > palette[bColor].g)
            {
               newpalette[j].g -= 4;
            }
            else if (newpalette[j].g < palette[bColor].g)
            {
               newpalette[j].g += 4;
            }

            if (newpalette[j].b > palette[bColor].b)
            {
               newpalette[j].b -= 4;
            }
            else if (newpalette[j].b < palette[bColor].b)
            {
               newpalette[j].b += 4;
            }
         }

         VIDEO_SetPalette(newpalette);
         UTIL_Delay(iDelay);
      }

      for (i = 0; i < PAL_PALETTE_COLORS; i++)
      {
         newpalette[i] = palette[bColor];
      }

      VIDEO_SetPalette(newpalette);
   }
}

VOID
PAL_FadeToRed(
   VOID
)
/*++
  Purpose:

    Fade the whole screen to red color.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   SDL_Color                 *palette;
   SDL_Color                 *newpalette = PAL_PaletteNext();
   int                        i, j;
   BYTE                       color;

   memset(newpalette, 0xff, PAL_PALETTE_SDL_BYTES);
    
   palette = PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
   if (palette == NULL)
   {
      return;
   }
   memcpy(newpalette, palette, PAL_PALETTE_SDL_BYTES);

   for (i = 0; i < gpScreen->pitch * gpScreen->h; i++)
   {
      if (((LPBYTE)(gpScreen->pixels))[i] == 0x4F)
      {
         ((LPBYTE)(gpScreen->pixels))[i] = 0x4E; // HACKHACK
      }
   }

   VIDEO_UpdateScreen(NULL);

   for (i = 0; i < 32; i++)
   {
      for (j = 0; j < PAL_PALETTE_COLORS; j++)
      {
         if (j == 0x4F)
         {
            continue; // so that texts will not be affected
         }

         color = ((INT)palette[j].r + (INT)palette[j].g + (INT)palette[j].b) / 4 + 64;

         if (newpalette[j].r > color)
         {
            newpalette[j].r -= (newpalette[j].r - color > 8 ? 8 : newpalette[j].r - color);
         }
         else if (newpalette[j].r < color)
         {
            newpalette[j].r += (color - newpalette[j].r > 8 ? 8 : color - newpalette[j].r);
         }

         if (newpalette[j].g > 0)
         {
            newpalette[j].g -= (newpalette[j].g > 8 ? 8 : newpalette[j].g);
         }

         if (newpalette[j].b > 0)
         {
            newpalette[j].b -= (newpalette[j].b > 8 ? 8 : newpalette[j].b);
         }
      }

      VIDEO_SetPalette(newpalette);
      UTIL_Delay(75);
   }
}
