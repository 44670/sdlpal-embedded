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
#include "embedded/pal_memory_profile.h"
#if defined(ESP_PLATFORM) && defined(MEM_LEVEL2)
#include <esp_attr.h>
#endif

#if defined(PAL_EXTREME_TWO_SCREENS)
#include "pal_engine_runtime_metrics.h"
#endif
#if defined(PAL_EXTREME_CHAPTER_CACHE)
#include "embedded/pal_event_pager.h"
#include "pal_engine_chapter_cache.h"
#endif

#if defined(PAL_PAGED_EVENT_STATE)
static VOID
PAL_ResReadEventObject(
   WORD             wEventObjectID,
   LPEVENTOBJECT    lpEventObject
)
{
   if (!PAL_EventObjectRead(wEventObjectID, lpEventObject))
   {
      TerminateOnError("Event-state read failed for object %u",
         wEventObjectID);
   }
}

static VOID
PAL_ResWriteEventObject(
   WORD                  wEventObjectID,
   const EVENTOBJECT    *lpEventObject
)
{
   if (!PAL_EventObjectWrite(wEventObjectID, lpEventObject))
   {
      TerminateOnError("Event-state write failed for object %u",
         wEventObjectID);
   }
}
#endif

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
#if defined(ESP_PLATFORM) && defined(MEM_LEVEL2)
#define PAL_RES_PSRAM EXT_RAM_BSS_ATTR __attribute__((aligned(4)))
#elif defined(__GNUC__) && defined(MEM_LEVEL1)
#define PAL_RES_PSRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#elif defined(__GNUC__)
#define PAL_RES_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_RES_PSRAM
#endif
#if defined(PAL_PAGED_EVENT_STATE)
/*
 * The complete 5,369-object data set peaks at 142 objects in one scene.
 * Round that audited maximum up to 160 so the fixed table also tolerates
 * modest save/script variations without reserving MAX_EVENT_OBJECTS pointers.
 */
#define PAL_RES_EVENT_SPRITE_CAPACITY \
   PAL_EXTREME_SCENE_EVENT_OBJECT_CAPACITY
static uint8_t pal_sram_extreme_res_state[sizeof(RESOURCES)] PAL_RES_PSRAM;
static uint8_t pal_sram_extreme_res_event_sprite_ptrs[
   PAL_RES_EVENT_SPRITE_CAPACITY * sizeof(LPCSPRITE)] PAL_RES_PSRAM;
#define PAL_RES_STATE_STORAGE pal_sram_extreme_res_state
#define PAL_RES_EVENT_SPRITE_STORAGE pal_sram_extreme_res_event_sprite_ptrs
#else
#define PAL_RES_EVENT_SPRITE_CAPACITY MAX_EVENT_OBJECTS
static uint8_t pal_psram_res_state[sizeof(RESOURCES)] PAL_RES_PSRAM;
static uint8_t pal_psram_res_event_sprite_ptrs[MAX_EVENT_OBJECTS * sizeof(LPCSPRITE)] PAL_RES_PSRAM;
#define PAL_RES_STATE_STORAGE pal_psram_res_state
#define PAL_RES_EVENT_SPRITE_STORAGE pal_psram_res_event_sprite_ptrs
#endif
#define PAL_RES_EVENT_SPRITE_PTRS ((LPCSPRITE *)PAL_RES_EVENT_SPRITE_STORAGE)
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
#if defined(MEM_LEVEL2)
   PAL_MemoryPlayerReset();
#endif
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
   memset(PAL_RES_STATE_STORAGE, 0, sizeof(PAL_RES_STATE_STORAGE));
   gpResources = (LPRESOURCES)PAL_RES_STATE_STORAGE;
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
#if defined(MEM_LEVEL2)
      PAL_MemorySceneReset();
#endif

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
      gpResources->lpMap = NULL;
#if defined(MEM_LEVEL2)
      PAL_MemorySceneReset();
#endif

#if defined(PAL_EXTREME_CHAPTER_CACHE)
      {
         BOOL force_verify =
            (gpResources->bLoadFlags & kLoadGlobalData) != 0;
         BOOL changes_bundle =
            PalEngineChapterCache_SceneNeedsBundle(gpGlobals->wNumScene);

         if (gpGlobals->fInBattle &&
            (force_verify || changes_bundle))
         {
            /*
             * Enemy ABC/FIRE views can still point into the active overlay.
             * A cache transaction is only legal after battle teardown.
             */
            TerminateOnError(
               "Chapter cache switch requested during battle (scene %u)",
               gpGlobals->wNumScene);
         }

         /*
          * MGO player pointers are const views into the overlay just like the
          * map and event sprites.  Drop them before PrepareScene() invokes the
          * provider-clear callback and unmaps the old flash pages.
          */
         if (force_verify || changes_bundle)
         {
            PAL_FreePlayerSprites();
            gpResources->bLoadFlags |= kLoadPlayerSprite;
         }
         if (!PAL_EventStateFlush(PAL_EVENT_WRITE_SCENE))
         {
            TerminateOnError(
               "Event-state flush failed before chapter cache prepare");
         }
         if (!PalEngineChapterCache_PrepareScene(
               gpGlobals->wNumScene, force_verify))
         {
            TerminateOnError(
               "Chapter cache prepare failed for scene %u",
               gpGlobals->wNumScene);
         }
      }
#endif

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

      //
      // Load map
      //
#if defined(MEM_LEVEL1) && !defined(PAL_EXTREME_CHAPTER_CACHE)
      if (!((gpGlobals->wNumScene >= 1 && gpGlobals->wNumScene <= 20) ||
            gpGlobals->wNumScene == 22))
      {
         TerminateOnError("MEM_LEVEL1 chapter boundary: scene %u is outside 1..20,22",
            gpGlobals->wNumScene);
      }
#endif
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
#if defined(PAL_PAGED_EVENT_STATE)
      if (index < 0 ||
         index > gpGlobals->g.rgScene[i + 1].wEventObjectIndex ||
         gpGlobals->g.rgScene[i + 1].wEventObjectIndex >
            gpGlobals->g.nEventObject)
      {
         TerminateOnError(
            "Paged event boundary: scene %u uses %d..%u, capacity is %d",
            gpGlobals->wNumScene,
            index,
            gpGlobals->g.rgScene[i + 1].wEventObjectIndex,
            gpGlobals->g.nEventObject);
      }
      if (!PAL_EventObjectPinScene(gpGlobals->wNumScene))
      {
         TerminateOnError(
            "Paged event-state pin failed for scene %u",
            gpGlobals->wNumScene);
      }
#endif
#ifdef PAL_NO_RUNTIME_HEAP
      eventObjectIndexBase = index;
#endif
      gpResources->nEventObject = gpGlobals->g.rgScene[i + 1].wEventObjectIndex;
      gpResources->nEventObject -= index;

      if (gpResources->nEventObject > 0)
      {
#ifdef PAL_NO_RUNTIME_HEAP
         if (gpResources->nEventObject > PAL_RES_EVENT_SPRITE_CAPACITY)
         {
#if defined(PAL_PAGED_EVENT_STATE)
            TerminateOnError("Paged scene sprite pointer capacity exceeded: %d > %d",
               gpResources->nEventObject, PAL_RES_EVENT_SPRITE_CAPACITY);
#else
            gpResources->nEventObject = MAX_EVENT_OBJECTS;
#endif
         }
         memset(PAL_RES_EVENT_SPRITE_STORAGE, 0, sizeof(PAL_RES_EVENT_SPRITE_STORAGE));
         gpResources->lppEventObjectSprites = PAL_RES_EVENT_SPRITE_PTRS;
#else
         gpResources->lppEventObjectSprites =
            (LPCSPRITE *)UTIL_calloc(gpResources->nEventObject, sizeof(LPCSPRITE));
#endif
      }

      for (i = 0; i < gpResources->nEventObject; i++, index++)
      {
#if defined(PAL_PAGED_EVENT_STATE)
         EVENTOBJECT eventObject;

         PAL_ResReadEventObject((WORD)(index + 1), &eventObject);
         n = eventObject.wSpriteNum;
#else
         n = gpGlobals->g.lprgEventObject[index].wSpriteNum;
#endif
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
#if defined(PAL_PAGED_EVENT_STATE)
            EVENTOBJECT previousEventObject;

            PAL_ResReadEventObject(
               (WORD)(eventObjectIndexBase + l + 1),
               &previousEventObject);
            if (previousEventObject.wSpriteNum == n &&
                gpResources->lppEventObjectSprites[l] != NULL)
#else
            if (gpGlobals->g.lprgEventObject[eventObjectIndexBase + l].wSpriteNum == n &&
                gpResources->lppEventObjectSprites[l] != NULL)
#endif
            {
               gpResources->lppEventObjectSprites[i] = gpResources->lppEventObjectSprites[l];
#if defined(PAL_PAGED_EVENT_STATE)
               eventObject.nSpriteFramesAuto =
                  PAL_SpriteGetNumFrames(gpResources->lppEventObjectSprites[i]);
               PAL_ResWriteEventObject((WORD)(index + 1), &eventObject);
#else
               gpGlobals->g.lprgEventObject[index].nSpriteFramesAuto =
                  PAL_SpriteGetNumFrames(gpResources->lppEventObjectSprites[i]);
#endif
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
#if defined(MEM_LEVEL2)
            LPBYTE sprite_data =
               (LPBYTE)PAL_MemorySceneAlloc((size_t)l);
            gpResources->lppEventObjectSprites[i] =
               (sprite_data != NULL &&
                  PAL_MKFReadChunk(sprite_data, (UINT)l,
                     (UINT)n, gpGlobals->f.fpMGO) == l) ?
                  (LPCSPRITE)sprite_data : NULL;
            if (gpResources->lppEventObjectSprites[i] == NULL)
            {
               TerminateOnError(
                  "MEM_LEVEL2 scene arena/read failed: chunk=%d size=%d used=%u/%u",
                  n, l, (unsigned)PAL_MemorySceneUsed(),
                  (unsigned)PAL_MEM_LEVEL2_SCENE_ARENA_BYTES);
               continue;
            }
#else
            LPCBYTE lpSpriteData;
            UINT uiSpriteSize;
            if (!PAL_MKFMapChunk(gpGlobals->f.fpMGO, n, &lpSpriteData, &uiSpriteSize) ||
                uiSpriteSize != (UINT)l)
            {
               gpResources->lppEventObjectSprites[i] = NULL;
               continue;
            }
            gpResources->lppEventObjectSprites[i] = lpSpriteData;
#endif
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
#if defined(PAL_PAGED_EVENT_STATE)
            eventObject.nSpriteFramesAuto =
               PAL_SpriteGetNumFrames(gpResources->lppEventObjectSprites[i]);
            PAL_ResWriteEventObject((WORD)(index + 1), &eventObject);
#else
            gpGlobals->g.lprgEventObject[index].nSpriteFramesAuto =
               PAL_SpriteGetNumFrames(gpResources->lppEventObjectSprites[i]);
#endif
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
#if defined(MEM_LEVEL2)
            LPBYTE sprite_data =
               (LPBYTE)PAL_MemoryPlayerAlloc((size_t)l);
            gpResources->rglpPlayerSprite[i] =
               (sprite_data != NULL &&
                  PAL_MKFReadChunk(sprite_data, (UINT)l,
                     (UINT)wSpriteNum, gpGlobals->f.fpMGO) == l) ?
                  (LPCSPRITE)sprite_data : NULL;
            if (gpResources->rglpPlayerSprite[i] == NULL)
            {
               TerminateOnError(
                  "MEM_LEVEL2 player arena/read failed: chunk=%u size=%d used=%u/%u",
                  (unsigned)wSpriteNum, l,
                  (unsigned)PAL_MemoryPlayerUsed(),
                  (unsigned)PAL_MEM_LEVEL2_PLAYER_ARENA_BYTES);
               continue;
            }
#else
            LPCBYTE lpSpriteData;
            UINT uiSpriteSize;
            if (PAL_MKFMapChunk(gpGlobals->f.fpMGO, wSpriteNum, &lpSpriteData, &uiSpriteSize) &&
                uiSpriteSize == (UINT)l)
            {
               gpResources->rglpPlayerSprite[i] = lpSpriteData;
            }
#endif
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
#if defined(MEM_LEVEL2)
            LPBYTE sprite_data =
               (LPBYTE)PAL_MemoryPlayerAlloc((size_t)l);
            gpResources->rglpPlayerSprite[
               (short)gpGlobals->wMaxPartyMemberIndex + i] =
               (sprite_data != NULL &&
                  PAL_MKFReadChunk(sprite_data, (UINT)l,
                     (UINT)wSpriteNum, gpGlobals->f.fpMGO) == l) ?
                  (LPCSPRITE)sprite_data : NULL;
            if (gpResources->rglpPlayerSprite[
                  (short)gpGlobals->wMaxPartyMemberIndex + i] == NULL)
            {
               TerminateOnError(
                  "MEM_LEVEL2 follower arena/read failed: chunk=%u size=%d used=%u/%u",
                  (unsigned)wSpriteNum, l,
                  (unsigned)PAL_MemoryPlayerUsed(),
                  (unsigned)PAL_MEM_LEVEL2_PLAYER_ARENA_BYTES);
               continue;
            }
#else
            LPCBYTE lpSpriteData;
            UINT uiSpriteSize;
            if (PAL_MKFMapChunk(gpGlobals->f.fpMGO, wSpriteNum, &lpSpriteData, &uiSpriteSize) &&
                uiSpriteSize == (UINT)l)
            {
               gpResources->rglpPlayerSprite[(short)gpGlobals->wMaxPartyMemberIndex+i] = lpSpriteData;
            }
#endif
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
#if defined(PAL_EXTREME_TWO_SCREENS)
   PalEngineBridge_LogRuntimeMemory("resources-loaded");
#endif
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
