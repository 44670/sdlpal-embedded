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
#include <setjmp.h>

#if defined(PAL_EXTREME_TWO_SCREENS)
#include "embedded/pal_memory_profile.h"
#endif

#if defined(PAL_HAS_WS_SERVER)
#include "unix/pal_ws_server.h"
#endif

#if defined(PAL_EXTREME_RIX_MUSIC)
#include "pal_target_board.h"
#endif

#if SDL_VERSION_ATLEAST(3,0,0)
#if __EMSCRIPTEN__
#define SDL_MAIN_HANDLED
#endif
    #include <SDL3/SDL_main.h>
#endif

#if defined(PAL_HAS_GIT_REVISION)
# undef PAL_GIT_REVISION
# include "generated.h"
#endif

static jmp_buf g_exit_jmp_buf;
static int g_exit_code = 0;

char gExecutablePath[PAL_MAX_PATH];

#define BITMAPNUM_SPLASH_UP         (gConfig.fIsWIN95 ? 0x03 : 0x26)
#define BITMAPNUM_SPLASH_DOWN       (gConfig.fIsWIN95 ? 0x04 : 0x27)
#define SPRITENUM_SPLASH_TITLE      0x47
#define SPRITENUM_SPLASH_CRANE      0x49
#define NUM_RIX_TITLE               0x05

#if defined(PAL_EXTREME_TWO_SCREENS) && defined(PAL_NO_RUNTIME_DECOMPRESS)
static BOOL
PAL_SplashLoadNativeMgo(
   LPCSPRITE      *lplpSprite,
   WORD            wChunkNum,
   LPBYTE          lpStorage,
   UINT            uiStorageSize
)
{
   if (lplpSprite == NULL)
   {
      return FALSE;
   }
#if defined(MEM_LEVEL2)
   INT size;

   size = PAL_MKFGetChunkSize(wChunkNum, gpGlobals->f.fpMGO);
   if (lpStorage == NULL || size <= 0 || (UINT)size > uiStorageSize ||
      PAL_MKFReadChunk(lpStorage, uiStorageSize,
         wChunkNum, gpGlobals->f.fpMGO) != size)
   {
      return FALSE;
   }
   *lplpSprite = lpStorage;
   return TRUE;
#else
   {
      UINT mapped_size;

      (void)lpStorage;
      (void)uiStorageSize;
      return PAL_MKFMapChunk(gpGlobals->f.fpMGO, wChunkNum,
         lplpSprite, &mapped_size) && mapped_size != 0u;
   }
#endif
}

static INT
PAL_SplashAdvanceTransition(
   WORD             wChunkNum,
   UINT             uiPreviousProgress,
   UINT             uiProgress
)
{
   return PAL_FBPAdvanceChunkVerticalTransition(gpGlobals->f.fpFBP,
      wChunkNum, gpScreenBak,
      uiPreviousProgress, uiProgress, TRUE);
}
#endif

#if (defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)) && \
    !defined(PAL_EXTREME_TWO_SCREENS)
#define PAL_STATIC_SPLASH_BUFFERS 1
#if defined(__GNUC__)
#define PAL_SPLASH_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#define PAL_SPLASH_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_SPLASH_SRAM
#define PAL_SPLASH_PSRAM
#endif
static uint8_t pal_psram_splash_fbp[320 * 200] PAL_SPLASH_PSRAM;
#if defined(PAL_NO_RUNTIME_HEAP) && !defined(PAL_NO_RUNTIME_DECOMPRESS)
static uint8_t pal_psram_splash_title[32000] PAL_SPLASH_PSRAM;
static uint8_t pal_psram_splash_crane[32000] PAL_SPLASH_PSRAM;
#endif
#endif


static VOID
PAL_Init(
   VOID
)
/*++
  Purpose:

    Initialize everything needed by the game.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int           e;
#if PAL_HAS_GIT_REVISION
   UTIL_LogOutput(LOGLEVEL_DEBUG, "SDLPal build revision: %s\n", PAL_GIT_REVISION);
#endif

   //
   // Initialize subsystems.
   //
   e = PAL_InitGlobals();
   if (e != 0)
   {
	   TerminateOnError("Could not initialize global data: %d.\n", e);
   }

   e = VIDEO_Startup();
   if (e != 0)
   {
      TerminateOnError("Could not initialize Video: %d.\n", e);
   }

   VIDEO_SetWindowTitle("Loading...");

   e = PAL_InitUI();
   if (e != 0)
   {
      TerminateOnError("Could not initialize UI subsystem: %d.\n", e);
   }

   e = PAL_InitText();
   if (e != 0)
   {
      TerminateOnError("Could not initialize text subsystem: %d.\n", e);
   }

   e = PAL_InitFont(&gConfig);
   if (e != 0)
   {
      TerminateOnError("Could not load fonts: %d.\n", e);
   }

   PAL_InitInput();
   PAL_InitResources();
#if defined(PAL_EXTREME_RIX_MUSIC)
   e = AUDIO_OpenDevice();
   if (e != 0)
   {
      PalTarget_ShowError("MUSIC FAIL", "CHECK USB LOG");
      TerminateOnError("Could not initialize target music: %d.\n", e);
   }
#else
   AUDIO_OpenDevice();
#endif
   PAL_AVIInit();

   VIDEO_SetWindowTitle(UTIL_va(UTIL_GlobalBuffer(0), PAL_GLOBAL_BUFFER_SIZE,
	   "Pal %s%s%s%s",
	   gConfig.fIsWIN95 ? "Win95" : "DOS",
#if defined(_DEBUG) || defined(DEBUG)
	   " (Debug) ",
#else
	   "",
#endif
#if defined(PAL_HAS_GIT_REVISION) && defined(PAL_GIT_REVISION)
	   " ["  PAL_GIT_REVISION "] "
#else
	   ""
#endif
       ,(gConfig.fEnableGLSL && gConfig.pszShader ? gConfig.pszShader : "")
   ));
}

VOID
PAL_Shutdown(
   int exit_code
)
/*++
  Purpose:

    Free everything needed by the game.

  Parameters:

    exit_code -  The exit code return to OS.

  Return value:

    None.

--*/
{
   AUDIO_CloseDevice();
   PAL_AVIShutdown();
   PAL_FreeFont();
   PAL_FreeResources();
   PAL_FreeUI();
   PAL_FreeText();
   PAL_ShutdownInput();
   VIDEO_Shutdown();
   
   //
   // global needs be free in last
   // since subsystems may needs config content during destroy
   // which also cleared here
   //
   PAL_FreeGlobals();

   g_exit_code = exit_code;
#if !__EMSCRIPTEN__ && SDL_MAJOR_VERSION < 3
   longjmp(g_exit_jmp_buf, 1);
#else
   SDL_Quit();
   UTIL_Platform_Quit();
#if !__WINRT__ && !__IOS__
   exit(0);
#endif
   return;
#endif
}

VOID
PAL_TrademarkScreen(
   VOID
)
/*++
  Purpose:

    Show the trademark screen.

  Parameters:

    None.

  Return value:

    None.

--*/
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   /*
    * The chapter pack intentionally omits RNG 6.  Skip the DOS trademark
    * movie explicitly instead of relying on a missing TF chunk to produce an
    * empty playback.
    */
   return;
#else
   if (PAL_PlayAVI("1.avi")) return;

   PAL_SetPalette(3, FALSE);
   PAL_RNGPlay(6, 0, -1, 25);
   UTIL_Delay(1000);
   PAL_FadeOut(1);
#endif
}

#if defined(PAL_EXTREME_TWO_SCREENS) && defined(PAL_NO_RUNTIME_DECOMPRESS)
static VOID
PAL_SplashScreenNative(
   VOID
)
{
   SDL_Color *palette = PAL_GetPalette(1, FALSE);
   SDL_Color current_palette[256];
   LPCSPRITE title_sprite = NULL;
   LPCSPRITE crane_sprite = NULL;
   LPCBITMAPRLE title_bitmap;
   int crane_pos[9][3];
   int image_pos = 200;
   int crane_frame = 0;
   int title_height;
   int title_visible_height = 0;
   UINT image_revealed = 0u;
   int i;
   DWORD begin_time;
   BOOL use_cd = TRUE;

   if (PAL_PlayAVI("2.avi"))
   {
      return;
   }
   if (palette == NULL)
   {
      UTIL_LogOutput(LOGLEVEL_ERROR,
         "PAL_SplashScreen(): palette 1 is unavailable\n");
      return;
   }

#if defined(MEM_LEVEL2)
   if (!PAL_SplashLoadNativeMgo(&title_sprite, SPRITENUM_SPLASH_TITLE,
         pal_mem_level2_fight_effect,
         PAL_MEM_LEVEL2_FIGHT_EFFECT_BYTES) ||
      !PAL_SplashLoadNativeMgo(&crane_sprite, SPRITENUM_SPLASH_CRANE,
         pal_mem_level2_fight_summon,
         PAL_MEM_LEVEL2_FIGHT_SUMMON_BYTES))
#else
   if (!PAL_SplashLoadNativeMgo(&title_sprite, SPRITENUM_SPLASH_TITLE,
         NULL, 0u) ||
      !PAL_SplashLoadNativeMgo(&crane_sprite, SPRITENUM_SPLASH_CRANE,
         NULL, 0u))
#endif
   {
      UTIL_LogOutput(LOGLEVEL_ERROR,
         "PAL_SplashScreen(): native title sprites are unavailable\n");
      return;
   }
   title_bitmap = PAL_SpriteGetFrame(title_sprite, 0);
   if (title_bitmap == NULL || PAL_SpriteGetNumFrames(crane_sprite) < 8 ||
      PAL_FBPBlitChunkToSurface(gpGlobals->f.fpFBP,
         BITMAPNUM_SPLASH_DOWN, gpScreenBak) != 0 ||
      PAL_MKFGetChunkSize(BITMAPNUM_SPLASH_UP,
         gpGlobals->f.fpFBP) != 320 * 200)
   {
      UTIL_LogOutput(LOGLEVEL_ERROR,
         "PAL_SplashScreen(): native title canvases are unavailable\n");
      return;
   }
   title_height = PAL_RLEGetHeight(title_bitmap);
   if (title_height <= 0)
   {
      return;
   }

   memset(current_palette, 0xff, sizeof(current_palette));
   for (i = 0; i < 9; i++)
   {
      crane_pos[i][0] = RandomLong(300, 600);
      crane_pos[i][1] = RandomLong(0, 80);
      crane_pos[i][2] = RandomLong(0, 8);
   }
   if (!AUDIO_PlayCDTrack(7))
   {
      use_cd = FALSE;
      AUDIO_PlayMusic(NUM_RIX_TITLE, TRUE, 2);
   }

   PAL_ProcessEvent();
   PAL_ClearKeyState();
   begin_time = SDL_GetTicks();

   while (TRUE)
   {
      DWORD elapsed;

      PAL_ProcessEvent();
      elapsed = SDL_GetTicks() - begin_time;
      if (elapsed < 15000u)
      {
         for (i = 0; i < 256; i++)
         {
            current_palette[i].r =
               (BYTE)(palette[i].r * ((float)elapsed / 15000));
            current_palette[i].g =
               (BYTE)(palette[i].g * ((float)elapsed / 15000));
            current_palette[i].b =
               (BYTE)(palette[i].b * ((float)elapsed / 15000));
         }
      }
      VIDEO_SetPaletteDeferred(current_palette);

      if (image_pos > 1)
      {
         image_pos--;
      }
      if (PAL_SplashAdvanceTransition(BITMAPNUM_SPLASH_UP, image_revealed,
            (UINT)(200 - image_pos)) != 0)
      {
         UTIL_LogOutput(LOGLEVEL_ERROR,
            "PAL_SplashScreen(): title FBP transition failed\n");
         break;
      }
      image_revealed = (UINT)(200 - image_pos);
      VIDEO_CopyEntireSurface(gpScreenBak, gpScreen);

      for (i = 0; i < 9; i++)
      {
         LPCBITMAPRLE frame = PAL_SpriteGetFrame(crane_sprite,
            crane_pos[i][2] =
               (crane_pos[i][2] + (crane_frame & 1)) % 8);

         crane_pos[i][1] +=
            ((image_pos > 1) && (image_pos & 1)) ? 1 : 0;
         PAL_RLEBlitToSurfaceFullCanvas(frame, gpScreen,
            PAL_XY(crane_pos[i][0], crane_pos[i][1]), -1);
         crane_pos[i][0]--;
      }
      crane_frame++;

      if (title_visible_height < title_height)
      {
         title_visible_height++;
      }
      PAL_RLEBlitToSurfaceFullCanvas(title_bitmap, gpScreen,
         PAL_XY(255, 10), title_visible_height);
      VIDEO_UpdateScreen(NULL);

      if (g_InputState.dwKeyPress & (kKeyMenu | kKeySearch))
      {
         BOOL was_fading = elapsed < 15000u;

         title_visible_height = title_height;
         PAL_RLEBlitToSurfaceFullCanvas(title_bitmap, gpScreen,
            PAL_XY(255, 10), title_visible_height);
         VIDEO_UpdateScreen(NULL);

         while (elapsed < 15000u)
         {
            for (i = 0; i < 256; i++)
            {
               current_palette[i].r =
                  (BYTE)(palette[i].r * ((float)elapsed / 15000));
               current_palette[i].g =
                  (BYTE)(palette[i].g * ((float)elapsed / 15000));
               current_palette[i].b =
                  (BYTE)(palette[i].b * ((float)elapsed / 15000));
            }
            VIDEO_SetPalette(current_palette);
            UTIL_Delay(8);
            elapsed += 250u;
         }
         if (was_fading)
         {
            UTIL_Delay(500);
         }
         break;
      }

      PAL_ProcessEvent();
      while (SDL_GetTicks() - begin_time < elapsed + 85u)
      {
         SDL_Delay(1);
         PAL_ProcessEvent();
      }
   }

   if (!use_cd)
   {
      AUDIO_PlayMusic(0, FALSE, 1);
   }
   PAL_FadeOut(1);
}
#endif

VOID
PAL_SplashScreen(
   VOID
)
/*++
  Purpose:

    Show the splash screen.

  Parameters:

    None.

  Return value:

    None.

--*/
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   PAL_SplashScreenNative();
#else
   SDL_Color     *palette = PAL_GetPalette(1, FALSE);
   SDL_Color      rgCurrentPalette[256];
   SDL_Surface   *lpBitmapDown, *lpBitmapUp;
   SDL_Rect       srcrect, dstrect;
   LPCSPRITE      lpSpriteCrane;
   LPCBITMAPRLE   lpBitmapTitle;
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   LPBYTE         buf;
#else
   LPBYTE         buf, buf2;
#endif
   int            cranepos[9][3], i, iImgPos = 200, iCraneFrame = 0, iTitleHeight;
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   int            iTitleVisibleHeight;
#endif
   DWORD          dwTime, dwBeginTime;
   BOOL           fUseCD = TRUE;

   memset(rgCurrentPalette, 0xff, sizeof(SDL_Color) * 256);

   if (PAL_PlayAVI("2.avi")) return;

   if (palette == NULL)
   {
      fprintf(stderr, "ERROR: PAL_SplashScreen(): palette == NULL\n");
      return;
   }

   //
   // Allocate all the needed memory at once for simplification
   //
#ifdef PAL_STATIC_SPLASH_BUFFERS
   buf = pal_psram_splash_fbp;
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   lpSpriteCrane = NULL;
#else
   buf2 = pal_psram_splash_title;
   lpSpriteCrane = (LPSPRITE)pal_psram_splash_crane;
#endif
#else
   buf = (LPBYTE)UTIL_calloc(1, 320 * 200 * 2);
   buf2 = (LPBYTE)(buf + 320 * 200);
   lpSpriteCrane = (LPSPRITE)buf2 + 32000;
#endif

   //
   // Create the surfaces
   //
   lpBitmapDown = VIDEO_CreateCompatibleSurface(gpScreen);
   lpBitmapUp = VIDEO_CreateCompatibleSurface(gpScreen);

   //
   // Read the bitmaps
   //
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   if (PAL_MKFReadChunk(buf, 320 * 200, BITMAPNUM_SPLASH_UP, gpGlobals->f.fpFBP) != 320 * 200)
   {
      goto end;
   }
   PAL_FBPBlitToSurface(buf, lpBitmapUp);
   if (PAL_MKFReadChunk(buf, 320 * 200, BITMAPNUM_SPLASH_DOWN, gpGlobals->f.fpFBP) != 320 * 200)
   {
      goto end;
   }
   PAL_FBPBlitToSurface(buf, lpBitmapDown);
   {
      LPCBYTE lpTitleData;
      UINT uiTitleSize;
      if (!PAL_MKFMapChunk(gpGlobals->f.fpMGO, SPRITENUM_SPLASH_TITLE, &lpTitleData, &uiTitleSize) ||
          uiTitleSize == 0)
      {
         goto end;
      }
      lpBitmapTitle = PAL_SpriteGetFrame(lpTitleData, 0);
   }
   {
      LPCBYTE lpCraneData;
      UINT uiCraneSize;
      if (!PAL_MKFMapChunk(gpGlobals->f.fpMGO, SPRITENUM_SPLASH_CRANE, &lpCraneData, &uiCraneSize) ||
          uiCraneSize == 0)
      {
         goto end;
      }
      lpSpriteCrane = lpCraneData;
   }
#else
   PAL_MKFReadChunk(buf, 320 * 200, BITMAPNUM_SPLASH_UP, gpGlobals->f.fpFBP);
   Decompress(buf, buf2, 320 * 200);
   PAL_FBPBlitToSurface(buf2, lpBitmapUp);
   PAL_MKFReadChunk(buf, 320 * 200, BITMAPNUM_SPLASH_DOWN, gpGlobals->f.fpFBP);
   Decompress(buf, buf2, 320 * 200);
   PAL_FBPBlitToSurface(buf2, lpBitmapDown);
   PAL_MKFReadChunk(buf, 32000, SPRITENUM_SPLASH_TITLE, gpGlobals->f.fpMGO);
   Decompress(buf, buf2, 32000);
   lpBitmapTitle = PAL_SpriteGetFrame(buf2, 0);
   PAL_MKFReadChunk(buf, 32000, SPRITENUM_SPLASH_CRANE, gpGlobals->f.fpMGO);
   Decompress(buf, (LPBYTE)lpSpriteCrane, 32000);
#endif

   if (lpBitmapTitle == NULL)
   {
      goto end;
   }
   iTitleHeight = PAL_RLEGetHeight(lpBitmapTitle);
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   iTitleVisibleHeight = 0;
#else
   ((LPBYTE)lpBitmapTitle)[2] = 0;
   ((LPBYTE)lpBitmapTitle)[3] = 0; // HACKHACK
#endif

   //
   // Generate the positions of the cranes
   //
   for (i = 0; i < 9; i++)
   {
      cranepos[i][0] = RandomLong(300, 600);
      cranepos[i][1] = RandomLong(0, 80);
      cranepos[i][2] = RandomLong(0, 8);
   }

   //
   // Play the title music
   //
   if (!AUDIO_PlayCDTrack(7))
   {
      fUseCD = FALSE;
      AUDIO_PlayMusic(NUM_RIX_TITLE, TRUE, 2);
   }

   //
   // Clear all of the events and key states
   //
   PAL_ProcessEvent();
   PAL_ClearKeyState();

   dwBeginTime = SDL_GetTicks();

   srcrect.x = 0;
   srcrect.w = 320;
   dstrect.x = 0;
   dstrect.w = 320;

   while (TRUE)
   {
      PAL_ProcessEvent();
      dwTime = SDL_GetTicks() - dwBeginTime;

      //
      // Set the palette
      //
      if (dwTime < 15000)
      {
         for (i = 0; i < 256; i++)
         {
            rgCurrentPalette[i].r = (BYTE)(palette[i].r * ((float)dwTime / 15000));
            rgCurrentPalette[i].g = (BYTE)(palette[i].g * ((float)dwTime / 15000));
            rgCurrentPalette[i].b = (BYTE)(palette[i].b * ((float)dwTime / 15000));
         }
      }

      VIDEO_SetPalette(rgCurrentPalette);
      VIDEO_UpdateSurfacePalette(lpBitmapDown);
      VIDEO_UpdateSurfacePalette(lpBitmapUp);

      //
      // Draw the screen
      //
      if (iImgPos > 1)
      {
         iImgPos--;
      }

      //
      // The upper part...
      //
      srcrect.y = iImgPos;
      srcrect.h = 200 - iImgPos;

      dstrect.y = 0;
      dstrect.h = srcrect.h;

	  VIDEO_CopySurface(lpBitmapUp, &srcrect, gpScreen, &dstrect);

      //
      // The lower part...
      //
      srcrect.y = 0;
      srcrect.h = iImgPos;

      dstrect.y = 200 - iImgPos;
      dstrect.h = srcrect.h;

	  VIDEO_CopySurface(lpBitmapDown, &srcrect, gpScreen, &dstrect);

      //
      // Draw the cranes...
      //
      for (i = 0; i < 9; i++)
      {
         LPCBITMAPRLE lpFrame = PAL_SpriteGetFrame(lpSpriteCrane,
            cranepos[i][2] = (cranepos[i][2] + (iCraneFrame & 1)) % 8);
         cranepos[i][1] += ((iImgPos > 1) && (iImgPos & 1)) ? 1 : 0;
         PAL_RLEBlitToSurface(lpFrame, gpScreen,
            PAL_XY(cranepos[i][0], cranepos[i][1]));
         cranepos[i][0]--;
      }
      iCraneFrame++;

      //
      // Draw the title...
      //
#ifdef PAL_NO_RUNTIME_DECOMPRESS
      if (iTitleVisibleHeight < iTitleHeight)
      {
         iTitleVisibleHeight++;
      }
#else
      if (PAL_RLEGetHeight(lpBitmapTitle) < iTitleHeight)
      {
         //
         // HACKHACK
         //
         WORD w = lpBitmapTitle[2] | (lpBitmapTitle[3] << 8);
         w++;
         ((LPBYTE)lpBitmapTitle)[2] = (w & 0xFF);
         ((LPBYTE)lpBitmapTitle)[3] = (w >> 8);
      }
#endif

#ifdef PAL_NO_RUNTIME_DECOMPRESS
      PAL_RLEBlitToSurfaceWithHeight(lpBitmapTitle, gpScreen, PAL_XY(255, 10), iTitleVisibleHeight);
#else
      PAL_RLEBlitToSurface(lpBitmapTitle, gpScreen, PAL_XY(255, 10));
#endif
      VIDEO_UpdateScreen(NULL);

      //
      // Check for keypress...
      //
      if (g_InputState.dwKeyPress & (kKeyMenu | kKeySearch))
      {
         //
         // User has pressed a key...
         //
#ifdef PAL_NO_RUNTIME_DECOMPRESS
         iTitleVisibleHeight = iTitleHeight;
         PAL_RLEBlitToSurfaceWithHeight(lpBitmapTitle, gpScreen, PAL_XY(255, 10), iTitleVisibleHeight);
#else
         ((LPBYTE)lpBitmapTitle)[2] = iTitleHeight & 0xFF;
         ((LPBYTE)lpBitmapTitle)[3] = iTitleHeight >> 8; // HACKHACK

         PAL_RLEBlitToSurface(lpBitmapTitle, gpScreen, PAL_XY(255, 10));
#endif

         VIDEO_UpdateScreen(NULL);

         if (dwTime < 15000)
         {
            //
            // If the picture has not completed fading in, complete the rest
            //
            while (dwTime < 15000)
            {
               for (i = 0; i < 256; i++)
               {
                  rgCurrentPalette[i].r = (BYTE)(palette[i].r * ((float)dwTime / 15000));
                  rgCurrentPalette[i].g = (BYTE)(palette[i].g * ((float)dwTime / 15000));
                  rgCurrentPalette[i].b = (BYTE)(palette[i].b * ((float)dwTime / 15000));
               }
               VIDEO_SetPalette(rgCurrentPalette);
               VIDEO_UpdateSurfacePalette(lpBitmapDown);
               VIDEO_UpdateSurfacePalette(lpBitmapUp);
               UTIL_Delay(8);
               dwTime += 250;
            }
            UTIL_Delay(500);
         }

         //
         // Quit the splash screen
         //
         break;
      }

      //
      // Delay a while...
      //
      PAL_ProcessEvent();
      while (SDL_GetTicks() - dwBeginTime < dwTime + 85)
      {
         SDL_Delay(1);
         PAL_ProcessEvent();
      }
   }

end:
   VIDEO_FreeSurface(lpBitmapDown);
   VIDEO_FreeSurface(lpBitmapUp);
#ifndef PAL_STATIC_SPLASH_BUFFERS
   free(buf);
#endif

   if (!fUseCD)
   {
      AUDIO_PlayMusic(0, FALSE, 1);
   }

   PAL_FadeOut(1);
#endif
}



int
main(
   int      argc,
   char    *argv[]
)
/*++
  Purpose:

    Program entry.

  Parameters:

    argc - Number of arguments.

    argv - Array of arguments.

  Return value:

    Integer value.

--*/
{
#if defined(PAL_HAS_WS_SERVER)
   PAL_WsReviewParseArgs(argc, argv);
#endif
#if !defined( __EMSCRIPTEN__ ) && !defined(__WINRT__) && !defined(__N3DS__)
   memset(gExecutablePath,0,PAL_MAX_PATH);
   strncpy(gExecutablePath, argv[0], PAL_MAX_PATH);
#endif

#if PAL_HAS_PLATFORM_STARTUP
   UTIL_Platform_Startup(argc,argv);
#endif

#if !__EMSCRIPTEN__ && SDL_MAJOR_VERSION < 3
   if (setjmp(g_exit_jmp_buf) != 0)
   {
	   // A longjmp is made, should exit here
	   SDL_Quit();
	   UTIL_Platform_Quit();
	   return g_exit_code;
   }
#endif

#if !defined(UNIT_TEST) || defined(UNIT_TEST_GAME_INIT)
   //
   // Initialize SDL
   //
   if (SDL_Init(PAL_SDL_INIT_FLAGS) == SDL_FAIL)
   {
	   TerminateOnError("Could not initialize SDL: %s.\n", SDL_GetError());
   }

   PAL_LoadConfig(TRUE);

   //
   // Platform-specific initialization
   //
   if (UTIL_Platform_Init(argc, argv) != 0)
	   return -1;

   //
   // Should launch setting?
   // Generally, the condition should never be TRUE as the UTIL_Platform_Init is assumed
   // to handle gConfig.fLaunchSetting correctly. However, it may actually be true due to
   // the activatation event on WinRT platform, so close the current process to make new
   // process go to setting.
   // For platforms without configuration page available, this condition will NEVER be true.
   //
   if (PAL_HAS_CONFIG_PAGE && gConfig.fLaunchSetting)
	   return 0;

   //
   // If user requests a file-based log, then add it after the system-specific one.
   //
   if (gConfig.pszLogFile)
	   UTIL_LogAddOutputCallback(UTIL_LogToFile, gConfig.iLogLevel);

   //
   // Initialize everything
   //
   PAL_Init();
#endif

#if !defined(UNIT_TEST)
   //
   // Show the trademark screen and splash screen
   //
   PAL_TrademarkScreen();
   PAL_SplashScreen();

   //
   // Run the main game routine
   //
   PAL_GameMain();

   //
   // Should not really reach here...
   //
   assert(FALSE);
   return 255;
#else
   extern int testmain(int argc, char *argv[]);
   return testmain(argc, argv);
#endif
}
