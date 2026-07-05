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

typedef struct tagRESOURCES
{
   BYTE             bLoadFlags;

   LPPALMAP         lpMap;                                      // current loaded map
   LPCSPRITE       *lppEventObjectSprites;                      // event object sprites
   int              nEventObject;                               // number of event objects

   LPCSPRITE        rglpPlayerSprite[MAX_PLAYABLE_PLAYER_ROLES]; // player sprites
} RESOURCES, *LPRESOURCES;

static LPRESOURCES gpResources = NULL;

#if defined(PAL_NO_RUNTIME_HEAP) || defined(PAL_NO_RUNTIME_DECOMPRESS)
#if defined(__GNUC__)
#define PAL_RES_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_RES_PSRAM
#endif
static uint8_t pal_psram_res_state[sizeof(RESOURCES)] PAL_RES_PSRAM;
static uint8_t pal_psram_res_event_sprite_ptrs[MAX_EVENT_OBJECTS * sizeof(LPCSPRITE)] PAL_RES_PSRAM;
#define PAL_RES_EVENT_SPRITE_PTRS ((LPCSPRITE *)pal_psram_res_event_sprite_ptrs)
#endif

static VOID
PAL_FreeEventObjectSprites(
   VOID
)
/*++
  Purpose:

    Free all sprites of event objects on the scene.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int i;

   if (gpResources->lppEventObjectSprites != NULL)
   {
#ifndef PAL_NO_RUNTIME_HEAP
      for (i = 0; i < gpResources->nEventObject; i++)
      {
         free((void *)gpResources->lppEventObjectSprites[i]);
      }

      free((void *)gpResources->lppEventObjectSprites);
#else
      (void)i;
#endif

      gpResources->lppEventObjectSprites = NULL;
      gpResources->nEventObject = 0;
   }
}

static VOID
PAL_FreePlayerSprites(
   VOID
)
/*++
  Purpose:

    Free all player sprites.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int i;

   for (i = 0; i < MAX_PLAYABLE_PLAYER_ROLES; i++)
   {
#ifndef PAL_NO_RUNTIME_HEAP
      free((void *)gpResources->rglpPlayerSprite[i]);
#endif
      gpResources->rglpPlayerSprite[i] = NULL;
   }
}

VOID
PAL_InitResources(
   VOID
)
/*++
  Purpose:

    Initialze the resource manager.

  Parameters:

    None.

  Return value:

    None.

--*/
{
#ifdef PAL_NO_RUNTIME_HEAP
   memset(pal_psram_res_state, 0, sizeof(pal_psram_res_state));
   gpResources = (LPRESOURCES)pal_psram_res_state;
#else
   gpResources = (LPRESOURCES)UTIL_calloc(1, sizeof(RESOURCES));
#endif
}

VOID
PAL_FreeResources(
   VOID
)
/*++
  Purpose:

    Free all loaded resources.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   if (gpResources != NULL)
   {
      //
      // Free all loaded sprites
      //
      PAL_FreePlayerSprites();
      PAL_FreeEventObjectSprites();

      //
      // Free map
      //
      PAL_FreeMap(gpResources->lpMap);

      //
      // Delete the instance
      //
#ifndef PAL_NO_RUNTIME_HEAP
      free(gpResources);
#endif
   }

   gpResources = NULL;
}

VOID
PAL_SetLoadFlags(
   BYTE       bFlags
)
/*++
  Purpose:

    Set flags to load resources.

  Parameters:

    [IN]  bFlags - flags to be set.

  Return value:

    None.

--*/
{
   if (gpResources == NULL)
   {
      return;
   }

   gpResources->bLoadFlags |= bFlags;
}

VOID
PAL_LoadResources(
   VOID
)
/*++
  Purpose:

    Load the game resources if needed.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int                i, index, l, n;
   WORD               wPlayerID, wSpriteNum;
#ifdef PAL_NO_RUNTIME_HEAP
   int                eventObjectIndexBase = 0;
#endif

   if (gpResources == NULL || gpResources->bLoadFlags == 0)
   {
      return;
   }

   //
   // Load global data
   //
   if (gpResources->bLoadFlags & kLoadGlobalData)
   {
      PAL_InitGameData(gpGlobals->bCurrentSaveSlot);
      AUDIO_PlayMusic(gpGlobals->wNumMusic, TRUE, 1);
   }

   //
   // Load scene
   //
   if (gpResources->bLoadFlags & kLoadScene)
   {
      FILE              *fpMAP, *fpGOP;

#ifdef PAL_NO_RUNTIME_DECOMPRESS
      fpMAP = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_MAP);
      fpGOP = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_GOP);
      if (fpMAP == NULL || fpGOP == NULL)
      {
         TerminateOnError("Resource pack open error!\n");
      }
#else
      fpMAP = UTIL_OpenRequiredFile("map.mkf");
      fpGOP = UTIL_OpenRequiredFile("gop.mkf");
#endif

      if (gpGlobals->fEnteringScene)
      {
         gpGlobals->wScreenWave = 0;
         gpGlobals->sWaveProgression = 0;
      }

      //
      // Free previous loaded scene (sprites and map)
      //
      PAL_FreeEventObjectSprites();
      PAL_FreeMap(gpResources->lpMap);

      //
      // Load map
      //
      i = gpGlobals->wNumScene - 1;
      gpResources->lpMap = PAL_LoadMap(gpGlobals->g.rgScene[i].wMapNum,
         fpMAP, fpGOP);

      if (gpResources->lpMap == NULL)
      {
         UTIL_CloseFile(fpMAP);
         UTIL_CloseFile(fpGOP);

         TerminateOnError("PAL_LoadResources(): Fail to load map #%d (scene #%d) !",
            gpGlobals->g.rgScene[i].wMapNum, gpGlobals->wNumScene);
      }

      //
      // Load sprites
      //
      index = gpGlobals->g.rgScene[i].wEventObjectIndex;
#ifdef PAL_NO_RUNTIME_HEAP
      eventObjectIndexBase = index;
#endif
      gpResources->nEventObject = gpGlobals->g.rgScene[i + 1].wEventObjectIndex;
      gpResources->nEventObject -= index;

      if (gpResources->nEventObject > 0)
      {
#ifdef PAL_NO_RUNTIME_HEAP
         if (gpResources->nEventObject > MAX_EVENT_OBJECTS)
         {
            gpResources->nEventObject = MAX_EVENT_OBJECTS;
         }
         memset(pal_psram_res_event_sprite_ptrs, 0, sizeof(pal_psram_res_event_sprite_ptrs));
         gpResources->lppEventObjectSprites = PAL_RES_EVENT_SPRITE_PTRS;
#else
         gpResources->lppEventObjectSprites =
            (LPCSPRITE *)UTIL_calloc(gpResources->nEventObject, sizeof(LPCSPRITE));
#endif
      }

      for (i = 0; i < gpResources->nEventObject; i++, index++)
      {
         n = gpGlobals->g.lprgEventObject[index].wSpriteNum;
         if (n == 0)
         {
            //
            // this event object has no sprite
            //
            gpResources->lppEventObjectSprites[i] = NULL;
            continue;
         }

#ifdef PAL_NO_RUNTIME_HEAP
         for (l = 0; l < i; l++)
         {
            if (gpGlobals->g.lprgEventObject[eventObjectIndexBase + l].wSpriteNum == n &&
                gpResources->lppEventObjectSprites[l] != NULL)
            {
               gpResources->lppEventObjectSprites[i] = gpResources->lppEventObjectSprites[l];
               gpGlobals->g.lprgEventObject[index].nSpriteFramesAuto =
                  PAL_SpriteGetNumFrames(gpResources->lppEventObjectSprites[i]);
               break;
            }
         }
         if (l < i)
         {
            continue;
         }
#endif

#ifdef PAL_NO_RUNTIME_DECOMPRESS
         l = PAL_MKFGetChunkSize(n, gpGlobals->f.fpMGO);
#else
         l = PAL_MKFGetDecompressedSize(n, gpGlobals->f.fpMGO);
#endif

         if (l <= 0)
         {
            gpResources->lppEventObjectSprites[i] = NULL;
            continue;
         }

#ifdef PAL_NO_RUNTIME_HEAP
         {
            LPCBYTE lpSpriteData;
            UINT uiSpriteSize;
            if (!PAL_MKFMapChunk(gpGlobals->f.fpMGO, n, &lpSpriteData, &uiSpriteSize) ||
                uiSpriteSize != (UINT)l)
            {
               gpResources->lppEventObjectSprites[i] = NULL;
               continue;
            }
            gpResources->lppEventObjectSprites[i] = lpSpriteData;
         }
#else
         gpResources->lppEventObjectSprites[i] = (LPSPRITE)UTIL_malloc(l);

#ifdef PAL_NO_RUNTIME_DECOMPRESS
         if (PAL_MKFReadChunk((LPBYTE)gpResources->lppEventObjectSprites[i], l, n, gpGlobals->f.fpMGO) > 0)
#else
         if (PAL_MKFDecompressChunk((LPBYTE)gpResources->lppEventObjectSprites[i], l,
            n, gpGlobals->f.fpMGO) > 0)
#endif
#endif
         {
            gpGlobals->g.lprgEventObject[index].nSpriteFramesAuto =
               PAL_SpriteGetNumFrames(gpResources->lppEventObjectSprites[i]);
         }
      }

      gpGlobals->partyoffset = PAL_XY(160, 112);

      UTIL_CloseFile(fpGOP);
      UTIL_CloseFile(fpMAP);
   }

   //
   // Load player sprites
   //
   if (gpResources->bLoadFlags & kLoadPlayerSprite)
   {
      //
      // Free previous loaded player sprites
      //
      PAL_FreePlayerSprites();

      for (i = 0; i <= (short)gpGlobals->wMaxPartyMemberIndex; i++)
      {
         wPlayerID = gpGlobals->rgParty[i].wPlayerRole;
         assert(wPlayerID < MAX_PLAYER_ROLES);

         //
         // Load player sprite
         //
         wSpriteNum = gpGlobals->g.PlayerRoles.rgwSpriteNum[wPlayerID];

#ifdef PAL_NO_RUNTIME_DECOMPRESS
         l = PAL_MKFGetChunkSize(wSpriteNum, gpGlobals->f.fpMGO);
#else
         l = PAL_MKFGetDecompressedSize(wSpriteNum, gpGlobals->f.fpMGO);
#endif

         if (l <= 0
         )
         {
            continue;
         }

#ifdef PAL_NO_RUNTIME_HEAP
         {
            LPCBYTE lpSpriteData;
            UINT uiSpriteSize;
            if (PAL_MKFMapChunk(gpGlobals->f.fpMGO, wSpriteNum, &lpSpriteData, &uiSpriteSize) &&
                uiSpriteSize == (UINT)l)
            {
               gpResources->rglpPlayerSprite[i] = lpSpriteData;
            }
         }
#else
         gpResources->rglpPlayerSprite[i] = (LPSPRITE)UTIL_malloc(l);
         PAL_MKFDecompressChunk((LPBYTE)gpResources->rglpPlayerSprite[i], l, wSpriteNum,
            gpGlobals->f.fpMGO);
#endif
      }

      for (i = 1; i <= gpGlobals->nFollower; i++)
      {
         //
         // Load the follower sprite
         //
         wSpriteNum = gpGlobals->rgParty[(short)gpGlobals->wMaxPartyMemberIndex+i].wPlayerRole;

#ifdef PAL_NO_RUNTIME_DECOMPRESS
         l = PAL_MKFGetChunkSize(wSpriteNum, gpGlobals->f.fpMGO);
#else
         l = PAL_MKFGetDecompressedSize(wSpriteNum, gpGlobals->f.fpMGO);
#endif

         if (l <= 0
         )
         {
            continue;
         }

#ifdef PAL_NO_RUNTIME_HEAP
         {
            LPCBYTE lpSpriteData;
            UINT uiSpriteSize;
            if (PAL_MKFMapChunk(gpGlobals->f.fpMGO, wSpriteNum, &lpSpriteData, &uiSpriteSize) &&
                uiSpriteSize == (UINT)l)
            {
               gpResources->rglpPlayerSprite[(short)gpGlobals->wMaxPartyMemberIndex+i] = lpSpriteData;
            }
         }
#else
         gpResources->rglpPlayerSprite[(short)gpGlobals->wMaxPartyMemberIndex+i] = (LPSPRITE)UTIL_malloc(l);
         PAL_MKFDecompressChunk((LPBYTE)gpResources->rglpPlayerSprite[(short)gpGlobals->wMaxPartyMemberIndex+i], l, wSpriteNum,
            gpGlobals->f.fpMGO);
#endif
      }
   }

   //
   // Clear all of the load flags
   //
   gpResources->bLoadFlags = 0;
}

LPPALMAP
PAL_GetCurrentMap(
   VOID
)
/*++
  Purpose:

    Get the current loaded map.

  Parameters:

    None.

  Return value:

    Pointer to the current loaded map. NULL if no map is loaded.

--*/
{
   if (gpResources == NULL)
   {
      return NULL;
   }

   return gpResources->lpMap;
}

LPCSPRITE
PAL_GetPlayerSprite(
   BYTE      bPlayerIndex
)
/*++
  Purpose:

    Get the player sprite.

  Parameters:

    [IN]  bPlayerIndex - index of player in party (starts from 0).

  Return value:

    Pointer to the player sprite.

--*/
{
   if (gpResources == NULL || bPlayerIndex > MAX_PLAYABLE_PLAYER_ROLES-1)
   {
      return NULL;
   }

   return gpResources->rglpPlayerSprite[bPlayerIndex];
}

LPCSPRITE
PAL_GetEventObjectSprite(
   WORD      wEventObjectID
)
/*++
  Purpose:

    Get the sprite of the specified event object.

  Parameters:

    [IN]  wEventObjectID - the ID of event object.

  Return value:

    Pointer to the sprite.

--*/
{
   wEventObjectID -= gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
   wEventObjectID--;

   if (gpResources == NULL || wEventObjectID >= gpResources->nEventObject)
   {
      return NULL;
   }

   return gpResources->lppEventObjectSprites[wEventObjectID];
}
