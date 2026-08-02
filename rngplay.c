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
// Portions based on PalLibrary by Lou Yihua <louyihua@21cn.com>.
// Copyright (c) 2006-2007, Lou Yihua.
//

#include "main.h"
#include "embedded/pal_fullscreen_stretch.h"

#if defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)
#if defined(PAL_EXTREME_TWO_SCREENS)
#include "esp32s3/engine_bridge/pal_engine_pack_provider.h"
#include "esp32s3/main/pal_target_memory.h"
#define pal_psram_rng_frame_static pal_sram_aux_framebuffer
#define PAL_RNG_FRAME_STATIC_BYTES PAL_EXTREME_SCREEN_BYTES
#else
#if defined(__GNUC__)
#define PAL_RNG_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_RNG_PSRAM
#endif
static uint8_t pal_psram_rng_frame_static[65000] PAL_RNG_PSRAM;
#define PAL_RNG_FRAME_STATIC_BYTES 65000u
#endif
#endif

#if defined(PAL_NO_RUNTIME_DECOMPRESS) && \
   !defined(PAL_EXTREME_TWO_SCREENS)
static UINT
PAL_RNGReadLe32(
   LPCBYTE         data
)
{
   return (UINT)data[0] |
      ((UINT)data[1] << 8) |
      ((UINT)data[2] << 16) |
      ((UINT)data[3] << 24);
}
#endif

#if !defined(PAL_NO_RUNTIME_DECOMPRESS) || \
   !defined(PAL_EXTREME_TWO_SCREENS)
static INT
PAL_RNGReadFrame(
   LPBYTE          lpBuffer,
   UINT            uiBufferSize,
   UINT            uiRngNum,
   UINT            uiFrameNum,
   FILE           *fpRngMKF
)
/*++
  Purpose:

    Read a frame from a RNG animation.

  Parameters:

    [OUT] lpBuffer - pointer to the destination buffer.

    [IN]  uiBufferSize - size of the destination buffer.

    [IN]  uiRngNum - the number of the RNG animation in the MKF archive.

    [IN]  uiFrameNum - frame number in the RNG animation.

    [IN]  fpRngMKF - pointer to the fopen'ed MKF file.

  Return value:

    Integer value which indicates the size of the chunk.
    -1 if there are error in parameters.
    -2 if buffer size is not enough.

--*/
{
#if !defined(PAL_NO_RUNTIME_DECOMPRESS) || \
   !defined(PAL_EXTREME_TWO_SCREENS)
   UINT         uiSubOffset    = 0;
   UINT         uiNextOffset   = 0;
   UINT         uiChunkCount   = 0;
   INT          iChunkLen      = 0;
#endif
#ifdef PAL_NO_RUNTIME_DECOMPRESS
#if !defined(PAL_EXTREME_TWO_SCREENS)
   LPCBYTE      lpMovie        = NULL;
   UINT         uiMovieSize    = 0;
#endif
#else
   UINT         uiOffset       = 0;
#endif

   if (lpBuffer == NULL || fpRngMKF == NULL || uiBufferSize == 0)
   {
      return -1;
   }

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   if (!PAL_MKFMapChunk(fpRngMKF, uiRngNum, &lpMovie, &uiMovieSize) || uiMovieSize < 4)
   {
      return -1;
   }

   uiChunkCount = PAL_RNGReadLe32(lpMovie);
   if (uiFrameNum >= uiChunkCount ||
      uiChunkCount > (UINT_MAX - 8u) / 4u ||
      uiMovieSize < 4u + (uiChunkCount + 1u) * 4u)
   {
      return -1;
   }

   uiSubOffset = PAL_RNGReadLe32(lpMovie + 4u + 4u * uiFrameNum);
   uiNextOffset = PAL_RNGReadLe32(lpMovie + 4u + 4u * (uiFrameNum + 1u));
   if (uiSubOffset > uiNextOffset || uiNextOffset > uiMovieSize)
   {
      return -1;
   }

   iChunkLen = (INT)(uiNextOffset - uiSubOffset);
   if ((UINT)iChunkLen > uiBufferSize)
   {
      return -2;
   }
   if (iChunkLen != 0)
   {
      memcpy(lpBuffer, lpMovie + uiSubOffset, (size_t)iChunkLen);
      return iChunkLen;
   }

   return -1;
#else
   //
   // Get the total number of chunks.
   //
   uiChunkCount = PAL_MKFGetChunkCount(fpRngMKF);
   if (uiRngNum >= uiChunkCount)
   {
      return -1;
   }

   //
   // Get the offset of the chunk.
   //
   fseek(fpRngMKF, 4 * uiRngNum, SEEK_SET);
   PAL_fread(&uiOffset, sizeof(UINT), 1, fpRngMKF);
   PAL_fread(&uiNextOffset, sizeof(UINT), 1, fpRngMKF);
   uiOffset = SDL_SwapLE32(uiOffset);
   uiNextOffset = SDL_SwapLE32(uiNextOffset);

   //
   // Get the length of the chunk.
   //
   iChunkLen = uiNextOffset - uiOffset;
   if (iChunkLen != 0)
   {
      fseek(fpRngMKF, uiOffset, SEEK_SET);
   }
   else
   {
      return -1;
   }

#ifdef PAL_NO_RUNTIME_DECOMPRESS
   //
   // Native RNG chunks start with frame_count, then frame offsets.
   //
   PAL_fread(&uiChunkCount, sizeof(UINT), 1, fpRngMKF);
   uiChunkCount = SDL_SwapLE32(uiChunkCount);
   if (uiFrameNum >= uiChunkCount)
   {
      return -1;
   }

   //
   // Get the offset of the sub chunk.
   //
   fseek(fpRngMKF, uiOffset + 4 + 4 * uiFrameNum, SEEK_SET);
   PAL_fread(&uiSubOffset, sizeof(UINT), 1, fpRngMKF);
   PAL_fread(&uiNextOffset, sizeof(UINT), 1, fpRngMKF);
   uiSubOffset = SDL_SwapLE32(uiSubOffset);
   uiNextOffset = SDL_SwapLE32(uiNextOffset);
#else
   //
   // Get the number of sub chunks.
   //
   PAL_fread(&uiChunkCount, sizeof(UINT), 1, fpRngMKF);
   uiChunkCount = (SDL_SwapLE32(uiChunkCount) - 4) / 4;
   if (uiFrameNum >= uiChunkCount)
   {
      return -1;
   }

   //
   // Get the offset of the sub chunk.
   //
   fseek(fpRngMKF, uiOffset + 4 * uiFrameNum, SEEK_SET);
   PAL_fread(&uiSubOffset, sizeof(UINT), 1, fpRngMKF);
   PAL_fread(&uiNextOffset, sizeof(UINT), 1, fpRngMKF);
   uiSubOffset = SDL_SwapLE32(uiSubOffset);
   uiNextOffset = SDL_SwapLE32(uiNextOffset);
#endif

   //
   // Get the length of the sub chunk.
   //
   iChunkLen = uiNextOffset - uiSubOffset;
   if ((UINT)iChunkLen > uiBufferSize)
   {
      return -2;
   }

   if (iChunkLen != 0)
   {
      fseek(fpRngMKF, uiOffset + uiSubOffset, SEEK_SET);
      return (int)fread(lpBuffer, 1, iChunkLen, fpRngMKF);
   }

   return -1;
#endif
}
#endif

#define PAL_RNG_SOURCE_WIDTH 320u
#define PAL_RNG_SOURCE_HEIGHT 200u
#define PAL_RNG_SOURCE_PIXELS \
   (PAL_RNG_SOURCE_WIDTH * PAL_RNG_SOURCE_HEIGHT)

typedef BOOL (*PALRNGREADAT)(
   void     *user,
   uint32_t  offset,
   LPBYTE    destination,
   uint32_t  size
);

typedef struct tagPALRNGINPUT
{
   LPCBYTE      memory;
   PALRNGREADAT read_at;
   void        *user;
   LPBYTE       window;
   uint32_t     length;
   uint32_t     position;
   uint32_t     window_capacity;
   uint32_t     window_offset;
   uint32_t     window_size;
} PALRNGINPUT;

static BOOL
PAL_RNGInputAvailable(
   const PALRNGINPUT *input,
   uint32_t           amount
)
{
   return input != NULL && input->position <= input->length &&
      amount <= input->length - input->position;
}

static BOOL
PAL_RNGInputRead(
   PALRNGINPUT *input,
   LPBYTE       destination,
   uint32_t     amount
)
{
   if ((destination == NULL && amount != 0u) ||
      !PAL_RNGInputAvailable(input, amount))
   {
      return FALSE;
   }
   if (amount == 0u)
   {
      return TRUE;
   }
   if (input->memory != NULL)
   {
      memcpy(destination, input->memory + input->position, amount);
      input->position += amount;
      return TRUE;
   }
   if (input->read_at == NULL || input->window == NULL ||
      input->window_capacity == 0u)
   {
      return FALSE;
   }

   while (amount != 0u)
   {
      uint32_t window_after = input->window_offset + input->window_size;
      uint32_t in_window;
      uint32_t available;
      uint32_t copy_size;

      if (input->position < input->window_offset ||
         input->position >= window_after)
      {
         uint32_t remaining = input->length - input->position;
         uint32_t fill_size = remaining < input->window_capacity ?
            remaining : input->window_capacity;

         if (fill_size == 0u ||
            !input->read_at(input->user, input->position,
               input->window, fill_size))
         {
            return FALSE;
         }
         input->window_offset = input->position;
         input->window_size = fill_size;
         window_after = input->window_offset + input->window_size;
      }
      in_window = input->position - input->window_offset;
      available = window_after - input->position;
      copy_size = amount < available ? amount : available;
      memcpy(destination, input->window + in_window, copy_size);
      destination += copy_size;
      input->position += copy_size;
      amount -= copy_size;
   }
   return TRUE;
}

static BOOL
PAL_RNGInputReadU8(
   PALRNGINPUT *input,
   uint8_t     *value
)
{
   return PAL_RNGInputRead(input, value, 1u);
}

static BOOL
PAL_RNGInputReadU16Le(
   PALRNGINPUT *input,
   uint16_t    *value
)
{
   uint8_t bytes[2];

   if (value == NULL || !PAL_RNGInputRead(input, bytes, sizeof(bytes)))
   {
      return FALSE;
   }
   *value = (uint16_t)((uint16_t)bytes[0] |
      ((uint16_t)bytes[1] << 8));
   return TRUE;
}

#if !defined(PAL_NO_RUNTIME_DECOMPRESS) || \
   !defined(PAL_EXTREME_TWO_SCREENS) || defined(PAL_RNG_DECODER_TEST)
static BOOL
PAL_RNGInputInitMemory(
   PALRNGINPUT *input,
   LPCBYTE      memory,
   uint32_t     length
)
{
   if (input == NULL || memory == NULL)
   {
      return FALSE;
   }
   memset(input, 0, sizeof(*input));
   input->memory = memory;
   input->length = length;
   return TRUE;
}
#endif

#if defined(PAL_EXTREME_TWO_SCREENS) || defined(PAL_RNG_DECODER_TEST)
static BOOL
PAL_RNGInputInitReadAt(
   PALRNGINPUT *input,
   PALRNGREADAT read_at,
   void        *user,
   uint32_t     length,
   LPBYTE       window,
   uint32_t     window_capacity
)
{
   if (input == NULL || read_at == NULL || user == NULL || length == 0u ||
      window == NULL || window_capacity == 0u)
   {
      return FALSE;
   }
   memset(input, 0, sizeof(*input));
   input->read_at = read_at;
   input->user = user;
   input->window = window;
   input->length = length;
   input->window_capacity = window_capacity;
   return TRUE;
}
#endif

static INT
PAL_RNGWriteCanonicalPixel(
   SDL_Surface *surface,
   uint32_t     source_offset,
   uint8_t      value
)
{
   uint32_t source_x;
   uint32_t source_y;
   uint32_t destination_x;
   uint32_t destination_x_after;
   uint32_t destination_y;
   uint32_t destination_y_after;

   if (surface == NULL || surface->pixels == NULL || surface->pitch <= 0 ||
      surface->w <= 0 || surface->h <= 0 || surface->pitch < surface->w ||
      source_offset >= PAL_RNG_SOURCE_PIXELS)
   {
      return -1;
   }
   source_x = source_offset % PAL_RNG_SOURCE_WIDTH;
   source_y = source_offset / PAL_RNG_SOURCE_WIDTH;
   if (!PalFullScreenStretch_DestinationRange(source_x,
         PAL_RNG_SOURCE_WIDTH, (uint32_t)surface->w,
         &destination_x, &destination_x_after) ||
      !PalFullScreenStretch_DestinationRange(source_y,
         PAL_RNG_SOURCE_HEIGHT, (uint32_t)surface->h,
         &destination_y, &destination_y_after))
   {
      return 0;
   }
   for (; destination_y < destination_y_after; destination_y++)
   {
      uint32_t x;
      LPBYTE row = (LPBYTE)surface->pixels +
         (size_t)destination_y * (size_t)surface->pitch;

      for (x = destination_x; x < destination_x_after; x++)
      {
         row[x] = value;
      }
   }
   return 0;
}

static INT
PAL_RNGWritePair(
   SDL_Surface *surface,
   uint32_t    *destination,
   uint8_t      first,
   uint8_t      second
)
{
   if (destination == NULL ||
      *destination > PAL_RNG_SOURCE_PIXELS - 2u ||
      PAL_RNGWriteCanonicalPixel(surface, *destination, first) != 0 ||
      PAL_RNGWriteCanonicalPixel(surface, *destination + 1u, second) != 0)
   {
      return -1;
   }
   *destination += 2u;
   return 0;
}

static BOOL
PAL_RNGAdvance(
   uint32_t *destination,
   uint32_t  pixels
)
{
   if (destination == NULL ||
      pixels > PAL_RNG_SOURCE_PIXELS - *destination)
   {
      return FALSE;
   }
   *destination += pixels;
   return TRUE;
}

static INT
PAL_RNGBlitInputToSurface(
   PALRNGINPUT *input,
   SDL_Surface *lpDstSurface
)
/*++
  Purpose:

    Apply one canonical 320x200 RNG delta frame through the common full-screen
    stretch transform. The input may be resident or a bounded storage window;
    the physical framebuffer is never treated as a 64KB canonical surface.

  Return value:

    0 = success, -1 = malformed input or invalid destination.

--*/
{
   uint32_t destination = 0u;

   if (input == NULL || lpDstSurface == NULL ||
      lpDstSurface->pixels == NULL || lpDstSurface->w <= 0 ||
      lpDstSurface->h <= 0 || lpDstSurface->pitch < lpDstSurface->w)
   {
      return -1;
   }

   while (input->position < input->length)
   {
      uint8_t opcode;
      uint32_t pairs;
      uint32_t i;
      uint8_t first;
      uint8_t second;
      uint16_t word;

      if (!PAL_RNGInputReadU8(input, &opcode))
      {
         return -1;
      }

      switch (opcode)
      {
      case 0x00:
      case 0x13:
         return 0;

      case 0x02:
         if (!PAL_RNGAdvance(&destination, 2u))
         {
            return -1;
         }
         break;

      case 0x03:
         if (!PAL_RNGInputReadU8(input, &first))
         {
            return -1;
         }
         pairs = (uint32_t)first + 1u;
         if (!PAL_RNGAdvance(&destination, pairs * 2u))
         {
            return -1;
         }
         break;

      case 0x04:
         if (!PAL_RNGInputReadU16Le(input, &word))
         {
            return -1;
         }
         pairs = (uint32_t)word + 1u;
         if (!PAL_RNGAdvance(&destination, pairs * 2u))
         {
            return -1;
         }
         break;

      case 0x06:
      case 0x07:
      case 0x08:
      case 0x09:
      case 0x0a:
         pairs = (uint32_t)opcode - 0x05u;
         if (!PAL_RNGInputAvailable(input, pairs * 2u) ||
            pairs > (PAL_RNG_SOURCE_PIXELS - destination) / 2u)
         {
            return -1;
         }
         for (i = 0u; i < pairs; i++)
         {
            if (!PAL_RNGInputReadU8(input, &first) ||
               !PAL_RNGInputReadU8(input, &second) ||
               PAL_RNGWritePair(lpDstSurface, &destination,
                  first, second) != 0)
            {
               return -1;
            }
         }
         break;

      case 0x0b:
         if (!PAL_RNGInputReadU8(input, &first))
         {
            return -1;
         }
         pairs = (uint32_t)first + 1u;
         if (!PAL_RNGInputAvailable(input, pairs * 2u) ||
            pairs > (PAL_RNG_SOURCE_PIXELS - destination) / 2u)
         {
            return -1;
         }
         for (i = 0u; i < pairs; i++)
         {
            if (!PAL_RNGInputReadU8(input, &first) ||
               !PAL_RNGInputReadU8(input, &second) ||
               PAL_RNGWritePair(lpDstSurface, &destination,
                  first, second) != 0)
            {
               return -1;
            }
         }
         break;

      case 0x0c:
         if (!PAL_RNGInputReadU16Le(input, &word))
         {
            return -1;
         }
         pairs = (uint32_t)word + 1u;
         if (!PAL_RNGInputAvailable(input, pairs * 2u) ||
            pairs > (PAL_RNG_SOURCE_PIXELS - destination) / 2u)
         {
            return -1;
         }
         for (i = 0u; i < pairs; i++)
         {
            if (!PAL_RNGInputReadU8(input, &first) ||
               !PAL_RNGInputReadU8(input, &second) ||
               PAL_RNGWritePair(lpDstSurface, &destination,
                  first, second) != 0)
            {
               return -1;
            }
         }
         break;

      case 0x0d:
      case 0x0e:
      case 0x0f:
      case 0x10:
         pairs = (uint32_t)opcode - 0x0bu;
         if (pairs > (PAL_RNG_SOURCE_PIXELS - destination) / 2u ||
            !PAL_RNGInputReadU8(input, &first) ||
            !PAL_RNGInputReadU8(input, &second))
         {
            return -1;
         }
         for (i = 0u; i < pairs; i++)
         {
            if (PAL_RNGWritePair(lpDstSurface, &destination,
                  first, second) != 0)
            {
               return -1;
            }
         }
         break;

      case 0x11:
         if (!PAL_RNGInputReadU8(input, &first))
         {
            return -1;
         }
         pairs = (uint32_t)first + 1u;
         if (pairs > (PAL_RNG_SOURCE_PIXELS - destination) / 2u ||
            !PAL_RNGInputReadU8(input, &first) ||
            !PAL_RNGInputReadU8(input, &second))
         {
            return -1;
         }
         for (i = 0u; i < pairs; i++)
         {
            if (PAL_RNGWritePair(lpDstSurface, &destination,
                  first, second) != 0)
            {
               return -1;
            }
         }
         break;

      case 0x12:
         if (!PAL_RNGInputReadU16Le(input, &word))
         {
            return -1;
         }
         pairs = (uint32_t)word + 1u;
         if (pairs > (PAL_RNG_SOURCE_PIXELS - destination) / 2u ||
            !PAL_RNGInputReadU8(input, &first) ||
            !PAL_RNGInputReadU8(input, &second))
         {
            return -1;
         }
         for (i = 0u; i < pairs; i++)
         {
            if (PAL_RNGWritePair(lpDstSurface, &destination,
                  first, second) != 0)
            {
               return -1;
            }
         }
         break;

      default:
         return -1;
      }
   }

   return 0;
}

#if !defined(PAL_NO_RUNTIME_DECOMPRESS) || \
   !defined(PAL_EXTREME_TWO_SCREENS) || defined(PAL_RNG_DECODER_TEST)
static INT
PAL_RNGBlitToSurface(
   const uint8_t *rng,
   int            length,
   SDL_Surface   *lpDstSurface
)
{
   PALRNGINPUT input;

   if (length < 0 ||
      !PAL_RNGInputInitMemory(&input, rng, (uint32_t)length))
   {
      return -1;
   }
   return PAL_RNGBlitInputToSurface(&input, lpDstSurface);
}
#endif

#if defined(PAL_RNG_DECODER_TEST)
INT
PAL_RNGBlitToSurfaceForTest(
   const uint8_t *rng,
   int            length,
   SDL_Surface   *surface
)
{
   return PAL_RNGBlitToSurface(rng, length, surface);
}

INT
PAL_RNGBlitReadAtToSurfaceForTest(
   PALRNGTESTREADAT read_at,
   void            *user,
   uint32_t         length,
   uint8_t         *window,
   uint32_t         window_capacity,
   SDL_Surface     *surface
)
{
   PALRNGINPUT input;

   if (!PAL_RNGInputInitReadAt(&input, read_at, user, length,
         window, window_capacity))
   {
      return -1;
   }
   return PAL_RNGBlitInputToSurface(&input, surface);
}
#endif

#if defined(PAL_NO_RUNTIME_DECOMPRESS) && \
   defined(PAL_EXTREME_TWO_SCREENS)
static BOOL
PAL_RNGReadNativeFrameAt(
   void     *user,
   uint32_t  offset,
   LPBYTE    destination,
   uint32_t  size
)
{
   return PalEngineBridge_ReadNativeRngFrameRange(
      (const PalEngineBridgeNativeRngFrame *)user,
      offset, destination, size) ? TRUE : FALSE;
}
#endif

VOID
PAL_RNGPlay(
   INT           iNumRNG,
   INT           iStartFrame,
   INT           iEndFrame,
   INT           iSpeed
)
/*++
  Purpose:

    Play a RNG movie.

  Parameters:

    [IN]  iNumRNG - number of the RNG movie.

    [IN]  iStartFrame - start frame number.

    [IN]  iEndFrame - end frame number.

    [IN]  iSpeed - speed of playing.

  Return value:

    None.

--*/
{
   double         iDelay = (double)SDL_GetPerformanceFrequency() / (iSpeed == 0 ? 16 : iSpeed);
#if defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)
   uint8_t        *rng = pal_psram_rng_frame_static;
#else
   uint8_t        *rng = (uint8_t *)malloc(65000);
   uint8_t        *buf = (uint8_t *)malloc(65000);
#endif
#ifdef PAL_NO_RUNTIME_DECOMPRESS
#if !defined(PAL_EXTREME_TWO_SCREENS)
   FILE           *fp = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_RNG);
#endif
#else
   FILE           *fp = UTIL_OpenRequiredFile("rng.mkf");
#endif
#if !defined(PAL_NO_RUNTIME_DECOMPRESS) || \
   !defined(PAL_EXTREME_TWO_SCREENS)
   INT             frameLen;
#endif

#if defined(PAL_EXTREME_TWO_SCREENS)
   if (gpGlobals->fInBattle)
   {
      TerminateOnError(
         "Cardputer two-screen ownership: RNG cannot replace battle background");
   }
#endif

#ifdef PAL_NO_RUNTIME_DECOMPRESS
#if !defined(PAL_EXTREME_TWO_SCREENS)
   if (fp == NULL)
   {
      TerminateOnError("Resource pack open error!\n");
   }
#endif
#endif

   //
   // Avoid losing the last frame
   //
   if (iEndFrame > 0) iEndFrame++;

   for (double iTime = SDL_GetPerformanceCounter(); rng &&
#ifndef PAL_NO_RUNTIME_DECOMPRESS
      buf &&
#endif
      iStartFrame != iEndFrame; iStartFrame++)
   {
      iTime += iDelay;

      //
      // Read, decompress and render the frame
      //
#ifdef PAL_NO_RUNTIME_DECOMPRESS
#if defined(PAL_EXTREME_TWO_SCREENS)
      {
         PalEngineBridgeNativeRngFrame frame;
         PALRNGINPUT input;

         if (iNumRNG < 0 || iNumRNG > UINT16_MAX ||
            iStartFrame < 0 || iStartFrame > UINT16_MAX ||
            !PalEngineBridge_OpenNativeRngFrame((uint16_t)iNumRNG,
               (uint16_t)iStartFrame, &frame) ||
            !PAL_RNGInputInitReadAt(&input, PAL_RNGReadNativeFrameAt,
               &frame, frame.size, rng, PAL_RNG_FRAME_STATIC_BYTES) ||
            PAL_RNGBlitInputToSurface(&input, gpScreen) == -1)
         {
            break;
         }
      }
#else
      frameLen = PAL_RNGReadFrame(rng, PAL_RNG_FRAME_STATIC_BYTES, iNumRNG, iStartFrame, fp);
      if (frameLen < 0 ||
          PAL_RNGBlitToSurface(rng, frameLen, gpScreen) == -1)
#endif
#else
      frameLen = PAL_RNGReadFrame(buf, 65000, iNumRNG, iStartFrame, fp);
      if (frameLen < 0 ||
          PAL_RNGBlitToSurface(rng, Decompress(buf, rng, 65000), gpScreen) == -1)
#endif
#if !defined(PAL_NO_RUNTIME_DECOMPRESS) || \
   !defined(PAL_EXTREME_TWO_SCREENS)
      {
         //
         // Failed to get the frame, don't go further
         //
         break;
      }
#endif

      //
      // Update the screen
      //
      VIDEO_UpdateScreen(NULL);

      //
      // Fade in the screen if needed
      //
      if (gpGlobals->fNeedToFadeIn)
      {
         PAL_FadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette, 1);
         gpGlobals->fNeedToFadeIn = FALSE;
      }

      //
      // Delay for a while
      //
	  PAL_DelayUntilPC(iTime);
   }

#if !defined(PAL_NO_RUNTIME_DECOMPRESS) || \
   !defined(PAL_EXTREME_TWO_SCREENS)
   UTIL_CloseFile(fp);
#endif
#if !defined(PAL_NO_RUNTIME_HEAP) && !defined(PAL_NO_RUNTIME_DECOMPRESS)
   free(rng);
   free(buf);
#endif
}
