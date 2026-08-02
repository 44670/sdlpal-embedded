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

static WORD g_wCurEffectSprite = 0;

#if defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)
#if defined(PAL_EXTREME_TWO_SCREENS)
static VOID
PAL_ExtremeRequireEndingScratch(
   const char *operation
)
{
   if (gpGlobals->fInBattle)
   {
      TerminateOnError(
         "Cardputer two-screen ownership: %s cannot replace battle background",
         operation);
   }
}
#else
#if defined(__GNUC__)
#define PAL_ENDING_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_ENDING_PSRAM
#endif
static uint8_t pal_psram_ending_fbp_static[320 * 200] PAL_ENDING_PSRAM;
#if defined(PAL_NO_RUNTIME_HEAP) && !defined(PAL_NO_RUNTIME_DECOMPRESS)
static uint8_t pal_psram_ending_sprite_static[320 * 200] PAL_ENDING_PSRAM;
static uint8_t pal_psram_ending_girl_static[6000] PAL_ENDING_PSRAM;
#endif
#endif
#endif

#ifdef PAL_NO_RUNTIME_DECOMPRESS
#if !defined(PAL_EXTREME_TWO_SCREENS)
static BOOL
PAL_EndingReadNativeFbp(
   LPBYTE         buf,
   WORD           wChunkNum
)
{
   return PAL_MKFReadChunk(buf, 320 * 200, wChunkNum, gpGlobals->f.fpFBP) == 320 * 200;
}
#endif

static BOOL
PAL_EndingMapNativeMgo(
   LPCSPRITE     *lplpSprite,
   WORD           wChunkNum
)
{
   UINT uiSpriteSize;
   return PAL_MKFMapChunk(gpGlobals->f.fpMGO, wChunkNum, lplpSprite, &uiSpriteSize) &&
      uiSpriteSize > 0;
}
#endif

VOID
PAL_EndingSetEffectSprite(
   WORD         wSpriteNum
)
/*++
  Purpose:

    Set the effect sprite of the ending.

  Parameters:

    [IN]  wSpriteNum - the number of the sprite.

  Return value:

    None.

--*/
{
   g_wCurEffectSprite = wSpriteNum;
}

VOID
PAL_ShowFBP(
   WORD         wChunkNum,
   WORD         wFade
)
/*++
  Purpose:

    Draw an FBP picture to the screen.

  Parameters:

    [IN]  wChunkNum - number of chunk in fbp.mkf file.

    [IN]  wFade - fading speed of showing the picture.

  Return value:

    None.

--*/
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   LPCSPRITE effect = NULL;

   PAL_ExtremeRequireEndingScratch("FBP");
   (void)wFade;
   if (PAL_FBPBlitChunkToSurface(gpGlobals->f.fpFBP,
      wChunkNum, gpScreen) != 0)
   {
      SDL_FillRect(gpScreen, NULL, 0);
   }
   if (g_wCurEffectSprite != 0 &&
      PAL_EndingMapNativeMgo(&effect, g_wCurEffectSprite) &&
      effect != NULL)
   {
      PAL_RLEBlitToSurface(PAL_SpriteGetFrame(effect, 0), gpScreen, PAL_XY(0, 0));
   }
   VIDEO_UpdateScreen(NULL);
#else
#if defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)
   BYTE                     *buf = pal_psram_ending_fbp_static;
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   LPCSPRITE                 lpEffectSprite = NULL;
#else
   BYTE                     *bufSprite = pal_psram_ending_sprite_static;
#endif
#else
   PAL_LARGE BYTE            buf[320 * 200];
   PAL_LARGE BYTE            bufSprite[320 * 200];
#endif
   const int                 rgIndex[6] = {0, 3, 1, 5, 2, 4};
   int                       i, j, k;
   BYTE                      a, b;

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   if (!PAL_EndingReadNativeFbp(buf, wChunkNum))
#else
   if (PAL_MKFDecompressChunk(buf, 320 * 200, wChunkNum, gpGlobals->f.fpFBP) <= 0)
#endif
   {
      memset(buf, 0, 320 * 200);
   }

   if (g_wCurEffectSprite != 0)
   {
#ifdef PAL_NO_RUNTIME_DECOMPRESS
      PAL_EndingMapNativeMgo(&lpEffectSprite, g_wCurEffectSprite);
#else
      PAL_MKFDecompressChunk(bufSprite, 320 * 200, g_wCurEffectSprite, gpGlobals->f.fpMGO);
#endif
   }

   if (wFade)
   {
      SDL_Surface *p = VIDEO_CreateCompatibleSurface(gpScreen);

      wFade++;
      wFade *= 10;

      PAL_FBPBlitToSurface(buf, p);
      VIDEO_BackupScreen(gpScreen);

      for (i = 0; i < 16; i++)
      {
         for (j = 0; j < 6; j++)
         {
            //
            // Blend the pixels in the 2 buffers, and put the result into the
            // backup buffer
            //
            for (k = rgIndex[j]; k < gpScreen->pitch * gpScreen->h; k += 6)
            {
               a = ((LPBYTE)(p->pixels))[k];
               b = ((LPBYTE)(gpScreenBak->pixels))[k];

               if (i > 0)
               {
                  if ((a & 0x0F) > (b & 0x0F))
                  {
                     b++;
                  }
                  else if ((a & 0x0F) < (b & 0x0F))
                  {
                     b--;
                  }
               }

               ((LPBYTE)(gpScreenBak->pixels))[k] = ((a & 0xF0) | (b & 0x0F));
            }

			VIDEO_RestoreScreen(gpScreen);

            if (g_wCurEffectSprite != 0)
            {
               int f = SDL_GetTicks() / 150;
#ifdef PAL_NO_RUNTIME_DECOMPRESS
               if (lpEffectSprite != NULL)
               {
                  PAL_RLEBlitToSurface(PAL_SpriteGetFrame(lpEffectSprite, f % PAL_SpriteGetNumFrames(lpEffectSprite)),
                     gpScreen, PAL_XY(0, 0));
               }
#else
               PAL_RLEBlitToSurface(PAL_SpriteGetFrame(bufSprite, f % PAL_SpriteGetNumFrames(bufSprite)),
                  gpScreen, PAL_XY(0, 0));
#endif
            }

            VIDEO_UpdateScreen(NULL);
            UTIL_Delay(wFade);
         }
      }

	  VIDEO_FreeSurface(p);
   }

   //
   // HACKHACK: to make the ending show correctly
   //
   if (wChunkNum != (gConfig.fIsWIN95 ? 68 : 49))
   {
      PAL_FBPBlitToSurface(buf, gpScreen);
   }

   VIDEO_UpdateScreen(NULL);
#endif
}

VOID
PAL_ScrollFBP(
   WORD         wChunkNum,
   WORD         wScrollSpeed,
   BOOL         fScrollDown
)
/*++
  Purpose:

    Scroll up an FBP picture to the screen.

  Parameters:

    [IN]  wChunkNum - number of chunk in fbp.mkf file.

    [IN]  wScrollSpeed - scrolling speed of showing the picture.

    [IN]  fScrollDown - TRUE if scroll down, FALSE if scroll up.

  Return value:

    None.

--*/
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   PAL_ExtremeRequireEndingScratch("scroll FBP");
#endif
#if defined(PAL_EXTREME_TWO_SCREENS)
   (void)wScrollSpeed;
   (void)fScrollDown;
   if (PAL_FBPBlitChunkToSurface(gpGlobals->f.fpFBP,
      wChunkNum, gpScreen) == 0)
   {
      VIDEO_UpdateScreen(NULL);
   }
#else
   SDL_Surface          *p;
#if defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)
   BYTE                 *buf = pal_psram_ending_fbp_static;
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   LPCSPRITE             lpEffectSprite = NULL;
#else
   BYTE                 *bufSprite = pal_psram_ending_sprite_static;
#endif
#else
   PAL_LARGE BYTE        buf[320 * 200];
   PAL_LARGE BYTE        bufSprite[320 * 200];
#endif
   int                   i, l;
   SDL_Rect              rect, dstrect;

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   if (!PAL_EndingReadNativeFbp(buf, wChunkNum))
#else
   if (PAL_MKFDecompressChunk(buf, 320 * 200, wChunkNum, gpGlobals->f.fpFBP) <= 0)
#endif
   {
      return;
   }

   if (g_wCurEffectSprite != 0)
   {
#ifdef PAL_NO_RUNTIME_DECOMPRESS
      PAL_EndingMapNativeMgo(&lpEffectSprite, g_wCurEffectSprite);
#else
      PAL_MKFDecompressChunk(bufSprite, 320 * 200, g_wCurEffectSprite, gpGlobals->f.fpMGO);
#endif
   }

   p = VIDEO_CreateCompatibleSurface(gpScreen);

   if (p == NULL)
   {
      return;
   }

   VIDEO_BackupScreen(gpScreen);
   PAL_FBPBlitToSurface(buf, p);

   if (wScrollSpeed == 0)
   {
      wScrollSpeed = 1;
   }

   rect.x = 0;
   rect.w = 320;
   dstrect.x = 0;
   dstrect.w = 320;

   for (l = 0; l < 220; l++)
   {
      i = l;
      if (i > 200)
      {
         i = 200;
      }

      if (fScrollDown)
      {
         rect.y = 0;
         dstrect.y = i;
         rect.h = 200 - i;
         dstrect.h = 200 - i;
      }
      else
      {
         rect.y = i;
         dstrect.y = 0;
         rect.h = 200 - i;
         dstrect.h = 200 - i;
      }

      VIDEO_CopySurface(gpScreenBak, &rect, gpScreen, &dstrect);

      if (fScrollDown)
      {
         rect.y = 200 - i;
         dstrect.y = 0;
         rect.h = i;
         dstrect.h = i;
      }
      else
      {
         rect.y = 0;
         dstrect.y = 200 - i;
         rect.h = i;
         dstrect.h = i;
      }

	  VIDEO_CopySurface(p, &rect, gpScreen, &dstrect);

      PAL_ApplyWave(gpScreen);

      if (g_wCurEffectSprite != 0)
      {
         int f = SDL_GetTicks() / 150;
#ifdef PAL_NO_RUNTIME_DECOMPRESS
         if (lpEffectSprite != NULL)
         {
            PAL_RLEBlitToSurface(PAL_SpriteGetFrame(lpEffectSprite, f % PAL_SpriteGetNumFrames(lpEffectSprite)),
               gpScreen, PAL_XY(0, 0));
         }
#else
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(bufSprite, f % PAL_SpriteGetNumFrames(bufSprite)),
            gpScreen, PAL_XY(0, 0));
#endif
      }

      VIDEO_UpdateScreen(NULL);

      if (gpGlobals->fNeedToFadeIn)
      {
         PAL_FadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette, 1);
         gpGlobals->fNeedToFadeIn = FALSE;
         VIDEO_UpdateSurfacePalette(p);
      }

      UTIL_Delay(800 / wScrollSpeed);
   }

   VIDEO_CopyEntireSurface(p, gpScreen);
   VIDEO_FreeSurface(p);
   VIDEO_UpdateScreen(NULL);
#endif
}

VOID
PAL_EndingAnimation(
   VOID
)
/*++
  Purpose:

    Show the ending animation.

  Parameters:

    None.

  Return value:

    None.

--*/
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   PAL_ExtremeRequireEndingScratch("ending animation");
#endif
#if defined(PAL_EXTREME_TWO_SCREENS)
   return;
#else
#if defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   LPBYTE            buf = pal_psram_ending_fbp_static;
   LPCSPRITE         lpBeastSprite = NULL;
   LPCSPRITE         lpGirlSprite = NULL;
#else
   LPBYTE            buf = pal_psram_ending_sprite_static;
   LPBYTE            bufGirl = pal_psram_ending_girl_static;
#endif
#else
   LPBYTE            buf;
   LPBYTE            bufGirl;
#endif
   SDL_Surface      *pUpper;
   SDL_Surface      *pLower;
   SDL_Rect          srcrect, dstrect;

   int               yPosGirl = 180;
   int               i;

#if !defined(PAL_NO_RUNTIME_HEAP) && !defined(PAL_NO_RUNTIME_DECOMPRESS)
   buf = (LPBYTE)UTIL_calloc(1, 64000);
   bufGirl = (LPBYTE)UTIL_calloc(1, 6000);
#endif

   pUpper = VIDEO_CreateCompatibleSurface(gpScreen);
   pLower = VIDEO_CreateCompatibleSurface(gpScreen);

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   PAL_EndingReadNativeFbp(buf, gConfig.fIsWIN95 ? 69 : 61);
#else
   PAL_MKFDecompressChunk(buf, 64000, gConfig.fIsWIN95 ? 69 : 61, gpGlobals->f.fpFBP);
#endif
   PAL_FBPBlitToSurface(buf, pUpper);

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   PAL_EndingReadNativeFbp(buf, gConfig.fIsWIN95 ? 70 : 62);
#else
   PAL_MKFDecompressChunk(buf, 64000, gConfig.fIsWIN95 ? 70 : 62, gpGlobals->f.fpFBP);
#endif
   PAL_FBPBlitToSurface(buf, pLower);

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   PAL_EndingMapNativeMgo(&lpBeastSprite, 571);
   PAL_EndingMapNativeMgo(&lpGirlSprite, 572);
#else
   PAL_MKFDecompressChunk(buf, 64000, 571, gpGlobals->f.fpMGO);
   PAL_MKFDecompressChunk(bufGirl, 6000, 572, gpGlobals->f.fpMGO);
#endif

   srcrect.x = 0;
   dstrect.x = 0;
   srcrect.w = 320;
   dstrect.w = 320;

   gpGlobals->wScreenWave = 2;

   for (i = 0; i < 400; i++)
   {
      //
      // Draw the background
      //
      srcrect.y = 0;
      srcrect.h = 200 - i / 2;

      dstrect.y = i / 2;
      dstrect.h = 200 - i / 2;

	  VIDEO_CopySurface(pLower, &srcrect, gpScreen, &dstrect);

      srcrect.y = 200 - i / 2;
      srcrect.h = i / 2;

      dstrect.y = 0;
      dstrect.h = i / 2;

	  VIDEO_CopySurface(pUpper, &srcrect, gpScreen, &dstrect);

      PAL_ApplyWave(gpScreen);

      //
      // Draw the beast
      //
#ifdef PAL_NO_RUNTIME_DECOMPRESS
      if (lpBeastSprite != NULL)
      {
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(lpBeastSprite, 0), gpScreen, PAL_XY(0, -400 + i));
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(lpBeastSprite, 1), gpScreen, PAL_XY(0, -200 + i));
      }
#else
      PAL_RLEBlitToSurface(PAL_SpriteGetFrame(buf, 0), gpScreen, PAL_XY(0, -400 + i));
	  PAL_RLEBlitToSurface(PAL_SpriteGetFrame(buf, 1), gpScreen, PAL_XY(0, -200 + i));
#endif
      //
      // Draw the girl
      //
      yPosGirl -= i & 1;
      if (yPosGirl < 80)
      {
         yPosGirl = 80;
      }

#ifdef PAL_NO_RUNTIME_DECOMPRESS
      if (lpGirlSprite != NULL)
      {
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(lpGirlSprite, (SDL_GetTicks() / 50) % 4),
            gpScreen, PAL_XY(220, yPosGirl));
      }
#else
      PAL_RLEBlitToSurface(PAL_SpriteGetFrame(bufGirl, (SDL_GetTicks() / 50) % 4),
         gpScreen, PAL_XY(220, yPosGirl));
#endif

      //
      // Update the screen
      //
      VIDEO_UpdateScreen(NULL);
      if (gpGlobals->fNeedToFadeIn)
      {
         PAL_FadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette, 1);
         gpGlobals->fNeedToFadeIn = FALSE;
         VIDEO_UpdateSurfacePalette(pUpper);
         VIDEO_UpdateSurfacePalette(pLower);
      }

      UTIL_Delay(50);
   }

   gpGlobals->wScreenWave = 0;

   VIDEO_FreeSurface(pUpper);
   VIDEO_FreeSurface(pLower);

#if !defined(PAL_NO_RUNTIME_HEAP) && !defined(PAL_NO_RUNTIME_DECOMPRESS)
   free(buf);
   free(bufGirl);
#endif
#endif
}

VOID
PAL_EndingScreen(
   VOID
)
/*++
 Purpose:
 
   Show the ending screen for Win95 version.

 Parameters:

   None.

 Return value:

   None.

--*/
{
    //
    // Use AVI & WIN95's music if we can
	// Otherwise, simulate the ending of DOS version
	//
	BOOL avi_played = PAL_PlayAVI("4.avi");

	if (!(avi_played = PAL_PlayAVI("5.avi")))
	{
		BOOL win_music = AUDIO_PlayCDTrack(12);

		if (!win_music) AUDIO_PlayMusic(0x1a, TRUE, 0);
		PAL_RNGPlay(gpGlobals->iCurPlayingRNG, 110, 150, 7);
		PAL_RNGPlay(gpGlobals->iCurPlayingRNG, 151, -1, 9);

		PAL_FadeOut(2);

		if (!win_music) AUDIO_PlayMusic(0x19, TRUE, 0);

		PAL_ShowFBP(75, 0);
		PAL_FadeIn(5, FALSE, 1);
		PAL_ScrollFBP(74, 0xf, TRUE);

		PAL_FadeOut(1);

		SDL_FillRect(gpScreen, NULL, 0);
		gpGlobals->wNumPalette = 4;
		gpGlobals->fNeedToFadeIn = TRUE;
		PAL_EndingAnimation();

		if (!win_music) AUDIO_PlayMusic(0, FALSE, 2);
		PAL_ColorFade(7, 15, FALSE);

		if (!win_music && !AUDIO_PlayCDTrack(2))
		{
			AUDIO_PlayMusic(0x11, TRUE, 0);
		}

		SDL_FillRect(gpScreen, NULL, 0);
		PAL_SetPalette(0, FALSE);
		PAL_RNGPlay(0xb, 0, -1, 7);

		PAL_FadeOut(2);

		SDL_FillRect(gpScreen, NULL, 0);
		gpGlobals->wNumPalette = 8;
		gpGlobals->fNeedToFadeIn = TRUE;
		PAL_RNGPlay(10, 0, -1, 6);

		PAL_EndingSetEffectSprite(0);
		PAL_ShowFBP(77, 10);

		VIDEO_BackupScreen(gpScreen);

		PAL_EndingSetEffectSprite(0x27b);
		PAL_ShowFBP(76, 7);

		PAL_SetPalette(5, FALSE);
		PAL_ShowFBP(73, 7);
		PAL_ScrollFBP(72, 0xf, TRUE);

		PAL_ShowFBP(71, 7);
		PAL_ShowFBP(68, 7);

		PAL_EndingSetEffectSprite(0);
		PAL_ShowFBP(68, 6);

		PAL_WaitForKey(0);
		AUDIO_PlayMusic(0, FALSE, 1);
		UTIL_Delay(500);
	}

	if (!PAL_PlayAVI("6.avi"))
	{
		if (avi_played)
		{
			gpGlobals->fNeedToFadeIn = FALSE;
			PAL_SetPalette(5, FALSE);
			PAL_EndingSetEffectSprite(0);
		}

		if (!AUDIO_PlayCDTrack(13))
		{
			AUDIO_PlayMusic(9, TRUE, 0);
		}

		PAL_ScrollFBP(67, 0xf, TRUE);
		PAL_ScrollFBP(66, 0xf, TRUE);
		PAL_ScrollFBP(65, 0xf, TRUE);
		PAL_ScrollFBP(64, 0xf, TRUE);
		PAL_ScrollFBP(63, 0xf, TRUE);
		PAL_ScrollFBP(62, 0xf, TRUE);
		PAL_ScrollFBP(61, 0xf, TRUE);
		PAL_ScrollFBP(60, 0xf, TRUE);
		PAL_ScrollFBP(59, 0xf, TRUE);

		AUDIO_PlayMusic(0, FALSE, 6);
		PAL_FadeOut(3);
	}
}
