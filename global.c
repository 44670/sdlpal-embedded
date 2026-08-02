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
#include "resampler.h"
#include "palcfg.h"

#include <stddef.h>
#include <stdint.h>
#if defined(ESP_PLATFORM) && defined(MEM_LEVEL2)
#include <esp_attr.h>
#endif

/* Tagged target saves must never be mistaken for a legacy DOS/WIN save. */
#ifndef PAL_EXTREME_SAVE_MAGIC
#define PAL_EXTREME_SAVE_MAGIC "PALXSAVE"
#endif

#if defined(PAL_PAGED_EVENT_STATE)
#include "pal_engine_event_state.h"
#include "pal_engine_pack_provider.h"
#include "pal_target_board.h"
#endif

static GLOBALVARS _gGlobals;
GLOBALVARS * const  gpGlobals = &_gGlobals;

CONFIGURATION gConfig;

#ifdef PAL_NO_RUNTIME_HEAP
#if defined(ESP_PLATFORM) && defined(MEM_LEVEL2)
#define PAL_GLOBAL_PSRAM EXT_RAM_BSS_ATTR __attribute__((aligned(4)))
#elif defined(__GNUC__) && defined(MEM_LEVEL1)
#define PAL_GLOBAL_PSRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#elif defined(__GNUC__)
#define PAL_GLOBAL_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_GLOBAL_PSRAM
#endif
#define PAL_GLOBAL_MAGIC_SLOTS 114
#if !defined(PAL_PAGED_EVENT_STATE)
#define PAL_GLOBAL_EVENT_OBJECT_CAPACITY MAX_EVENT_OBJECTS
static uint8_t pal_psram_global_event_objects[
   PAL_GLOBAL_EVENT_OBJECT_CAPACITY * sizeof(EVENTOBJECT)] PAL_GLOBAL_PSRAM;
#define PAL_GLOBAL_EVENT_OBJECT_STORAGE pal_psram_global_event_objects
#endif
#if defined(PAL_PAGED_EVENT_STATE)
static uint8_t pal_sram_extreme_global_magics[
   PAL_GLOBAL_MAGIC_SLOTS * sizeof(MAGIC)] PAL_GLOBAL_PSRAM;
#define PAL_GLOBAL_MAGIC_STORAGE pal_sram_extreme_global_magics
#else
static uint8_t pal_psram_global_magics[PAL_GLOBAL_MAGIC_SLOTS * sizeof(MAGIC)] PAL_GLOBAL_PSRAM;
#define PAL_GLOBAL_MAGIC_STORAGE pal_psram_global_magics
#endif
#endif

BOOL
PAL_EventObjectRead(
   WORD          event_object_id,
   LPEVENTOBJECT event_object
)
{
   if (event_object == NULL || event_object_id == 0 ||
      event_object_id > (WORD)gpGlobals->g.nEventObject)
   {
      return FALSE;
   }
#if defined(PAL_PAGED_EVENT_STATE)
   return PalEngineEventState_ReadEvent(
      event_object_id, event_object, sizeof(*event_object));
#else
   *event_object =
      gpGlobals->g.lprgEventObject[event_object_id - 1];
   return TRUE;
#endif
}

BOOL
PAL_EventObjectWrite(
   WORD               event_object_id,
   const EVENTOBJECT *event_object
)
{
   if (event_object == NULL || event_object_id == 0 ||
      event_object_id > (WORD)gpGlobals->g.nEventObject)
   {
      return FALSE;
   }
#if defined(PAL_PAGED_EVENT_STATE)
   return PalEngineEventState_WriteEvent(
      event_object_id, event_object, sizeof(*event_object));
#else
   gpGlobals->g.lprgEventObject[event_object_id - 1] =
      *event_object;
   return TRUE;
#endif
}

BOOL
PAL_EventObjectPinScene(
   WORD scene
)
{
   UINT32 start;
   UINT32 end;

   if (scene == 0 || scene >= MAX_SCENES)
   {
      return FALSE;
   }
   start = gpGlobals->g.rgScene[scene - 1].wEventObjectIndex;
   end = gpGlobals->g.rgScene[scene].wEventObjectIndex;
   if (start > end || end > (UINT32)gpGlobals->g.nEventObject)
   {
      return FALSE;
   }
#if defined(PAL_PAGED_EVENT_STATE)
   return PalEngineEventState_PinScene(
      (uint16_t)start, (uint16_t)(end - start));
#else
   return TRUE;
#endif
}

BOOL
PAL_EventStateFlush(
   INT reason
)
{
#if defined(PAL_PAGED_EVENT_STATE)
   if (reason < PAL_EVENT_WRITE_EVICT ||
      reason > PAL_EVENT_WRITE_SHUTDOWN)
   {
      return FALSE;
   }
   return PalEngineEventState_Flush(
      (PalEventPagerWriteReason)reason);
#else
   (void)reason;
   return TRUE;
#endif
}

BOOL
PAL_EventStateCheckpoint(
   VOID
)
{
#if defined(PAL_PAGED_EVENT_STATE)
   return PalEngineEventState_Checkpoint();
#else
   return TRUE;
#endif
}

BOOL
PAL_SceneMarkDirty(
   WORD scene_index
)
{
   if (scene_index >= MAX_SCENES)
   {
      return FALSE;
   }
#if defined(PAL_PAGED_EVENT_STATE)
   return PalEngineEventState_MarkSceneDirty(scene_index);
#else
   return TRUE;
#endif
}

#if !defined(PAL_PAGED_EVENT_STATE)
LPEVENTOBJECT
PAL_GetEventObjectByID(
   WORD event_object_id
)
{
   return event_object_id != 0 &&
      event_object_id <= (WORD)gpGlobals->g.nEventObject
      ? &gpGlobals->g.lprgEventObject[event_object_id - 1]
      : NULL;
}
#endif

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define DO_BYTESWAP(buf, size)
#else
#define DO_BYTESWAP(buf, size)                                   \
   do {                                                          \
      int i;                                                     \
      for (i = 0; i < (size) / 2; i++)                           \
      {                                                          \
         ((LPWORD)(buf))[i] = SDL_SwapLE16(((LPWORD)(buf))[i]);  \
      }                                                          \
   } while(0)
#endif

#define LOAD_DATA(buf, size, chunknum, fp)                       \
   do {                                                          \
      PAL_MKFReadChunk((LPBYTE)(buf), (size), (chunknum), (fp)); \
      DO_BYTESWAP(buf, size);                                    \
   } while(0)

BOOL
PAL_IsWINVersion(
	BOOL *pfIsWIN95
)
{
#ifdef PAL_NO_RUNTIME_HEAP
	if (pfIsWIN95) *pfIsWIN95 = FALSE;
	return TRUE;
#else
	FILE *fps[] = { UTIL_OpenRequiredFile("abc.mkf"), UTIL_OpenRequiredFile("map.mkf"), gpGlobals->f.fpF, gpGlobals->f.fpFBP, gpGlobals->f.fpFIRE, gpGlobals->f.fpMGO };
	uint8_t *data = NULL;
	int data_size = 0, dos_score = 0, win_score = 0;
	BOOL result = FALSE;

	for (int i = 0; i < sizeof(fps) / sizeof(FILE *); i++)
	{
		//
		// Find the first non-empty sub-file
		//
		int count = PAL_MKFGetChunkCount(fps[i]), j = 0, size;
		while (j < count && (size = PAL_MKFGetChunkSize(j, fps[i])) < 4) j++;
		if (j >= count) goto PAL_IsWINVersion_Exit;

		//
		// Read the content and check the compression signature
		// Note that this check is not 100% correct, however in incorrect situations,
		// the sub-file will be over 784MB if uncompressed, which is highly unlikely.
		//
		if (data_size < size) data = (uint8_t *)realloc(data, data_size = size);
		PAL_MKFReadChunk(data, data_size, j, fps[i]);
		if (data[0] == 'Y' && data[1] == 'J' && data[2] == '_' && data[3] == '1')
		{
			if (win_score > 0)
				goto PAL_IsWINVersion_Exit;
			else
				dos_score++;
		}
		else
		{
			if (dos_score > 0)
				goto PAL_IsWINVersion_Exit;
			else
				win_score++;
		}
	}

	//
	// Finally check the size of object definition
	//
	data_size = PAL_MKFGetChunkSize(2, gpGlobals->f.fpSSS);
	if (data_size % sizeof(OBJECT) == 0 && data_size % sizeof(OBJECT_DOS) != 0 && dos_score > 0) goto PAL_IsWINVersion_Exit;
	if (data_size % sizeof(OBJECT_DOS) == 0 && data_size % sizeof(OBJECT) != 0 && win_score > 0) goto PAL_IsWINVersion_Exit;

	if (pfIsWIN95) *pfIsWIN95 = (win_score == sizeof(fps) / sizeof(FILE *)) ? TRUE : FALSE;

	result = TRUE;

PAL_IsWINVersion_Exit:
	free(data);
	fclose(fps[1]);
	fclose(fps[0]);

	return result;
#endif
}

CODEPAGE
PAL_DetectCodePage(
	const char *   filename
)
{
#ifdef PAL_NO_RUNTIME_HEAP
	(void)filename;
	return CP_BIG5;
#else
	FILE *fp;
	char *word_buf = NULL;
	size_t word_len;
	CODEPAGE cp = CP_BIG5;

	if (NULL != (fp = UTIL_OpenFile(filename)))
	{
		fseek(fp, 0, SEEK_END);
		word_len = ftell(fp);
		word_buf = (char *)malloc(word_len);
		fseek(fp, 0, SEEK_SET);
		word_len = fread(word_buf, 1, word_len, fp);
		UTIL_CloseFile(fp);
		// Eliminates null characters so that PAL_MultiByteToWideCharCP works properly
		for (char *ptr = word_buf; ptr < word_buf + word_len; ptr++)
		{
			if (!*ptr)
				*ptr = ' ';
		}
	}

	if (word_buf)
	{
		int probability;
		cp = PAL_DetectCodePageForString(word_buf, (int)word_len, cp, &probability);

		free(word_buf);

		if (probability == 100)
			UTIL_LogOutput(LOGLEVEL_INFO, "PAL_DetectCodePage detected code page '%s' for %s\n", cp ? "GBK" : "BIG5", filename);
		else
			UTIL_LogOutput(LOGLEVEL_WARNING, "PAL_DetectCodePage detected the most possible (%d) code page '%s' for %s\n", probability, cp ? "GBK" : "BIG5", filename);
	}

	return cp;
#endif
}

INT
PAL_InitGlobals(
   VOID
)
/*++
  Purpose:

    Initialize global data.

  Parameters:

    None.

  Return value:

    0 = success, -1 = error.

--*/
{
   //
   // Open files
   //
#ifdef PAL_NO_RUNTIME_DECOMPRESS
   gpGlobals->f.fpFBP = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_FBP);
   gpGlobals->f.fpMGO = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_MGO);
   gpGlobals->f.fpBALL = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_BALL);
   gpGlobals->f.fpDATA = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_DATA);
   gpGlobals->f.fpF = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_F);
   gpGlobals->f.fpFIRE = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_FIRE);
   gpGlobals->f.fpRGM = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_RGM);
   gpGlobals->f.fpSSS = PAL_MKFOpenPackArchive(PAL_PACK_ARCHIVE_SSS);
   if (gpGlobals->f.fpFBP == NULL || gpGlobals->f.fpMGO == NULL ||
      gpGlobals->f.fpBALL == NULL || gpGlobals->f.fpDATA == NULL ||
      gpGlobals->f.fpF == NULL || gpGlobals->f.fpFIRE == NULL ||
      gpGlobals->f.fpRGM == NULL || gpGlobals->f.fpSSS == NULL)
   {
      TerminateOnError("Resource pack open error!\n");
   }
#else
   gpGlobals->f.fpFBP = UTIL_OpenRequiredFile("fbp.mkf");
   gpGlobals->f.fpMGO = UTIL_OpenRequiredFile("mgo.mkf");
   gpGlobals->f.fpBALL = UTIL_OpenRequiredFile("ball.mkf");
   gpGlobals->f.fpDATA = UTIL_OpenRequiredFile("data.mkf");
   gpGlobals->f.fpF = UTIL_OpenRequiredFile("f.mkf");
   gpGlobals->f.fpFIRE = UTIL_OpenRequiredFile("fire.mkf");
   gpGlobals->f.fpRGM = UTIL_OpenRequiredFile("rgm.mkf");
   gpGlobals->f.fpSSS = UTIL_OpenRequiredFile("sss.mkf");
#endif

   //
   // Retrieve game resource version
   //
   if (!PAL_IsWINVersion(&gConfig.fIsWIN95)) return -1;

   //
   // Enable AVI playing only when the resource is WIN95
   //
   gConfig.fEnableAviPlay = gConfig.fEnableAviPlay && gConfig.fIsWIN95;

   //
   // Detect game language only when no message file specified
   //
#ifdef PAL_NO_RUNTIME_HEAP
   if (!gConfig.pszMsgFile) PAL_SetCodePage(CP_BIG5);
#else
   if (!gConfig.pszMsgFile) PAL_SetCodePage(PAL_DetectCodePage("word.dat"));
#endif

#ifndef PAL_NO_RUNTIME_DECOMPRESS
   Decompress = gConfig.fIsWIN95 ? YJ2_Decompress : YJ1_Decompress;
#endif

#ifdef PAL_NO_RUNTIME_HEAP
   gpGlobals->lpObjectDesc = NULL;
#else
   gpGlobals->lpObjectDesc = gConfig.fIsWIN95 ? NULL : PAL_LoadObjectDesc("desc.dat");
#endif
   gpGlobals->bCurrentSaveSlot = 1;

   return 0;
}

VOID
PAL_FreeGlobals(
   VOID
)
/*++
  Purpose:

    Free global data.

  Parameters:

    None.

  Return value:

    None.

--*/
{
#if defined(PAL_PAGED_EVENT_STATE)
   PalEngineEventState_Shutdown();
#endif
   //
   // Close all opened files
   //
   UTIL_CloseFile(gpGlobals->f.fpFBP);
   UTIL_CloseFile(gpGlobals->f.fpMGO);
   UTIL_CloseFile(gpGlobals->f.fpBALL);
   UTIL_CloseFile(gpGlobals->f.fpDATA);
   UTIL_CloseFile(gpGlobals->f.fpF);
   UTIL_CloseFile(gpGlobals->f.fpFIRE);
   UTIL_CloseFile(gpGlobals->f.fpRGM);
   UTIL_CloseFile(gpGlobals->f.fpSSS);

   //
   // Free the game data
   //
#ifndef PAL_NO_RUNTIME_HEAP
   free(gpGlobals->g.lprgEventObject);
   free(gpGlobals->g.lprgScriptEntry);
   free(gpGlobals->g.lprgStore);
   free(gpGlobals->g.lprgEnemy);
   free(gpGlobals->g.lprgEnemyTeam);
   free(gpGlobals->g.lprgMagic);
   free(gpGlobals->g.lprgBattleField);
   free(gpGlobals->g.lprgLevelUpMagic);
#endif

   //
   // Free the object description data
   //
#ifndef PAL_NO_RUNTIME_HEAP
   if (!gConfig.fIsWIN95)
      PAL_FreeObjectDesc(gpGlobals->lpObjectDesc);
#endif

   //
   // Clear the instance
   //
   memset(gpGlobals, 0, sizeof(GLOBALVARS));

   PAL_FreeConfig();
}


static VOID
PAL_ReadGlobalGameData(
   VOID
)
/*++
  Purpose:

    Read global game data from data files.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   const GAMEDATA    *p = &gpGlobals->g;

#ifdef PAL_NO_RUNTIME_HEAP
   LOAD_DATA(p->lprgMagic, p->nMagic * sizeof(MAGIC), 4, gpGlobals->f.fpDATA);
#else
   LOAD_DATA(p->lprgScriptEntry, p->nScriptEntry * sizeof(SCRIPTENTRY),
      4, gpGlobals->f.fpSSS);

   LOAD_DATA(p->lprgStore, p->nStore * sizeof(STORE), 0, gpGlobals->f.fpDATA);
   LOAD_DATA(p->lprgEnemy, p->nEnemy * sizeof(ENEMY), 1, gpGlobals->f.fpDATA);
   LOAD_DATA(p->lprgEnemyTeam, p->nEnemyTeam * sizeof(ENEMYTEAM),
      2, gpGlobals->f.fpDATA);
   LOAD_DATA(p->lprgMagic, p->nMagic * sizeof(MAGIC), 4, gpGlobals->f.fpDATA);
   LOAD_DATA(p->lprgBattleField, p->nBattleField * sizeof(BATTLEFIELD),
      5, gpGlobals->f.fpDATA);
   LOAD_DATA(p->lprgLevelUpMagic, p->nLevelUpMagic * sizeof(LEVELUPMAGIC_ALL),
      6, gpGlobals->f.fpDATA);
#endif
   LOAD_DATA(p->rgwBattleEffectIndex, sizeof(p->rgwBattleEffectIndex),
      11, gpGlobals->f.fpDATA);
   PAL_MKFReadChunk((LPBYTE)&(p->EnemyPos), sizeof(p->EnemyPos),
      13, gpGlobals->f.fpDATA);
   DO_BYTESWAP(&(p->EnemyPos), sizeof(p->EnemyPos));
   PAL_MKFReadChunk((LPBYTE)(p->rgLevelUpExp), sizeof(p->rgLevelUpExp),
      14, gpGlobals->f.fpDATA);
   DO_BYTESWAP(p->rgLevelUpExp, sizeof(p->rgLevelUpExp));
}

static VOID
PAL_InitGlobalGameData(
   VOID
)
/*++
  Purpose:

    Initialize global game data.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int        len;

#ifdef PAL_NO_RUNTIME_HEAP
#define PAL_DOALLOCATE_STATIC(fp, num, type, lptype, ptr, n, storage)            \
   {                                                                             \
      len = PAL_MKFGetChunkSize(num, fp);                                        \
      if (len < 0 || (size_t)len > sizeof(storage))                              \
      {                                                                          \
         TerminateOnError("PAL_InitGlobalGameData(): Static buffer too small!"); \
      }                                                                          \
      ptr = (lptype)(storage);                                                   \
      n = len / sizeof(type);                                                    \
   }
#define PAL_DOMAP_STATIC(fp, num, type, lptype, ptr, n)                          \
   {                                                                             \
      LPCBYTE lpData;                                                            \
      UINT uiSize;                                                               \
      len = PAL_MKFGetChunkSize(num, fp);                                        \
      if (len < 0 || (len % sizeof(type)) != 0 ||                                \
         !PAL_MKFMapChunk(fp, num, &lpData, &uiSize) || uiSize != (UINT)len)      \
      {                                                                          \
         TerminateOnError("PAL_InitGlobalGameData(): Pack view error!");         \
      }                                                                          \
      ptr = (lptype)lpData;                                                      \
      n = len / sizeof(type);                                                    \
   }
#else
#define PAL_DOALLOCATE(fp, num, type, lptype, ptr, n)                            \
   {                                                                             \
      len = PAL_MKFGetChunkSize(num, fp);                                        \
      ptr = (lptype)malloc(len);                                                 \
      n = len / sizeof(type);                                                    \
      if (ptr == NULL)                                                           \
      {                                                                          \
         TerminateOnError("PAL_InitGlobalGameData(): Memory allocation error!"); \
      }                                                                          \
   }
#endif

   //
   // If the memory has not been allocated, allocate first.
   //
   if (
#if defined(PAL_PAGED_EVENT_STATE)
      gpGlobals->g.nEventObject == 0
#else
      gpGlobals->g.lprgEventObject == NULL
#endif
      )
   {
#ifdef PAL_NO_RUNTIME_HEAP
#if defined(PAL_PAGED_EVENT_STATE)
      if (!PalEngineEventState_Init())
      {
         TerminateOnError(
            "PAL_InitGlobalGameData(): TF event state initialization failed");
      }
      gpGlobals->g.lprgEventObject = NULL;
      gpGlobals->g.nEventObject =
         PAL_ENGINE_EVENT_STATE_RECORD_COUNT;
#else
      PAL_DOALLOCATE_STATIC(gpGlobals->f.fpSSS, 0, EVENTOBJECT, LPEVENTOBJECT,
         gpGlobals->g.lprgEventObject, gpGlobals->g.nEventObject,
         PAL_GLOBAL_EVENT_OBJECT_STORAGE);
#endif

      PAL_DOMAP_STATIC(gpGlobals->f.fpSSS, 4, SCRIPTENTRY, LPSCRIPTENTRY,
         gpGlobals->g.lprgScriptEntry, gpGlobals->g.nScriptEntry);

      PAL_DOMAP_STATIC(gpGlobals->f.fpDATA, 0, STORE, LPSTORE,
         gpGlobals->g.lprgStore, gpGlobals->g.nStore);

      PAL_DOMAP_STATIC(gpGlobals->f.fpDATA, 1, ENEMY, LPENEMY,
         gpGlobals->g.lprgEnemy, gpGlobals->g.nEnemy);

      PAL_DOMAP_STATIC(gpGlobals->f.fpDATA, 2, ENEMYTEAM, LPENEMYTEAM,
         gpGlobals->g.lprgEnemyTeam, gpGlobals->g.nEnemyTeam);

      PAL_DOALLOCATE_STATIC(gpGlobals->f.fpDATA, 4, MAGIC, LPMAGIC,
         gpGlobals->g.lprgMagic, gpGlobals->g.nMagic,
         PAL_GLOBAL_MAGIC_STORAGE);

      PAL_DOMAP_STATIC(gpGlobals->f.fpDATA, 5, BATTLEFIELD, LPBATTLEFIELD,
         gpGlobals->g.lprgBattleField, gpGlobals->g.nBattleField);

      PAL_DOMAP_STATIC(gpGlobals->f.fpDATA, 6, LEVELUPMAGIC_ALL, LPLEVELUPMAGIC_ALL,
         gpGlobals->g.lprgLevelUpMagic, gpGlobals->g.nLevelUpMagic);
#else
      PAL_DOALLOCATE(gpGlobals->f.fpSSS, 0, EVENTOBJECT, LPEVENTOBJECT,
         gpGlobals->g.lprgEventObject, gpGlobals->g.nEventObject);

      PAL_DOALLOCATE(gpGlobals->f.fpSSS, 4, SCRIPTENTRY, LPSCRIPTENTRY,
         gpGlobals->g.lprgScriptEntry, gpGlobals->g.nScriptEntry);

      PAL_DOALLOCATE(gpGlobals->f.fpDATA, 0, STORE, LPSTORE,
         gpGlobals->g.lprgStore, gpGlobals->g.nStore);

      PAL_DOALLOCATE(gpGlobals->f.fpDATA, 1, ENEMY, LPENEMY,
         gpGlobals->g.lprgEnemy, gpGlobals->g.nEnemy);

      PAL_DOALLOCATE(gpGlobals->f.fpDATA, 2, ENEMYTEAM, LPENEMYTEAM,
         gpGlobals->g.lprgEnemyTeam, gpGlobals->g.nEnemyTeam);

      PAL_DOALLOCATE(gpGlobals->f.fpDATA, 4, MAGIC, LPMAGIC,
         gpGlobals->g.lprgMagic, gpGlobals->g.nMagic);

      PAL_DOALLOCATE(gpGlobals->f.fpDATA, 5, BATTLEFIELD, LPBATTLEFIELD,
         gpGlobals->g.lprgBattleField, gpGlobals->g.nBattleField);

      PAL_DOALLOCATE(gpGlobals->f.fpDATA, 6, LEVELUPMAGIC_ALL, LPLEVELUPMAGIC_ALL,
         gpGlobals->g.lprgLevelUpMagic, gpGlobals->g.nLevelUpMagic);
#endif

      PAL_ReadGlobalGameData();
   }
#ifdef PAL_NO_RUNTIME_HEAP
#undef PAL_DOALLOCATE_STATIC
#undef PAL_DOMAP_STATIC
#else
#undef PAL_DOALLOCATE
#endif
}

static VOID
PAL_LoadDefaultGame(
   VOID
)
/*++
  Purpose:

    Load the default game data.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   GAMEDATA    *p = &gpGlobals->g;
   UINT32       i;

   //
   // Load the default data from the game data files.
   //
#if defined(PAL_PAGED_EVENT_STATE)
   if (!PalEngineEventState_ResetDefaults(
         p->rgScene, sizeof(p->rgScene)))
   {
      TerminateOnError(
         "PAL_LoadDefaultGame(): invalid or unreadable TF event template");
   }
#else
   LOAD_DATA(p->lprgEventObject, p->nEventObject * sizeof(EVENTOBJECT),
      0, gpGlobals->f.fpSSS);
   PAL_MKFReadChunk((LPBYTE)(p->rgScene), sizeof(p->rgScene), 1,
      gpGlobals->f.fpSSS);
   DO_BYTESWAP(p->rgScene, sizeof(p->rgScene));
#endif
   if (gConfig.fIsWIN95)
   {
      PAL_MKFReadChunk((LPBYTE)(p->rgObject), sizeof(p->rgObject), 2, gpGlobals->f.fpSSS);
      DO_BYTESWAP(p->rgObject, sizeof(p->rgObject));
   }
   else
   {
#if defined(MEM_LEVEL1) || defined(MEM_LEVEL2)
      LPCBYTE objects = NULL;
      UINT objects_size = 0;
      if (!PAL_MKFMapChunk(gpGlobals->f.fpSSS, 2, &objects, &objects_size) ||
         objects_size == 0 ||
         objects_size > MAX_OBJECTS * sizeof(OBJECT_DOS) ||
         (objects_size % sizeof(OBJECT_DOS)) != 0)
      {
         TerminateOnError("PAL_LoadDefaultGame(): invalid native DOS object table");
      }
      memset(p->rgObject, 0, sizeof(p->rgObject));
      for (i = 0; i < objects_size / sizeof(OBJECT_DOS); i++)
      {
         const OBJECT_DOS *object = (const OBJECT_DOS *)(objects + i * sizeof(OBJECT_DOS));
         memcpy(&p->rgObject[i], object, sizeof(OBJECT_DOS));
         p->rgObject[i].rgwData[6] = object->rgwData[5];
         p->rgObject[i].rgwData[5] = 0;
      }
#else
      OBJECT_DOS objects[MAX_OBJECTS];
		  PAL_MKFReadChunk((LPBYTE)(objects), sizeof(objects), 2, gpGlobals->f.fpSSS);
	  DO_BYTESWAP(objects, sizeof(objects));
      //
      // Convert the DOS-style data structure to WIN-style data structure
      //
      for (i = 0; i < MAX_OBJECTS; i++)
      {
         memcpy(&p->rgObject[i], &objects[i], sizeof(OBJECT_DOS));
         p->rgObject[i].rgwData[6] = objects[i].rgwData[5];     // wFlags
         p->rgObject[i].rgwData[5] = 0;                         // wScriptDesc or wReserved2
      }
#endif
   }

   PAL_MKFReadChunk((LPBYTE)(&(p->PlayerRoles)), sizeof(PLAYERROLES),
      3, gpGlobals->f.fpDATA);
   DO_BYTESWAP(&(p->PlayerRoles), sizeof(PLAYERROLES));

   //
   // Set some other default data.
   //
   gpGlobals->dwCash = 0;
   gpGlobals->wNumMusic = 0;
   gpGlobals->wNumPalette = 0;
   gpGlobals->wNumScene = 1;
   gpGlobals->wCollectValue = 0;
   gpGlobals->fNightPalette = FALSE;
   gpGlobals->wMaxPartyMemberIndex = 0;
   gpGlobals->viewport = PAL_XY(0, 0);
   gpGlobals->wLayer = 0;
   gpGlobals->nFollower = 0;
   gpGlobals->wChaseRange = 1;
#ifndef PAL_CLASSIC
   gpGlobals->bBattleSpeed = 2;
#endif

   memset(gpGlobals->rgInventory, 0, sizeof(gpGlobals->rgInventory));
   memset(gpGlobals->rgPoisonStatus, 0, sizeof(gpGlobals->rgPoisonStatus));
   memset(gpGlobals->rgParty, 0, sizeof(gpGlobals->rgParty));
   memset(gpGlobals->rgTrail, 0, sizeof(gpGlobals->rgTrail));
   memset(&(gpGlobals->Exp), 0, sizeof(gpGlobals->Exp));

   for (i = 0; i < MAX_PLAYER_ROLES; i++)
   {
      gpGlobals->Exp.rgPrimaryExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
      gpGlobals->Exp.rgHealthExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
      gpGlobals->Exp.rgMagicExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
      gpGlobals->Exp.rgAttackExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
      gpGlobals->Exp.rgMagicPowerExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
      gpGlobals->Exp.rgDefenseExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
      gpGlobals->Exp.rgDexterityExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
      gpGlobals->Exp.rgFleeExp[i].wLevel = p->PlayerRoles.rgwLevel[i];
   }

   gpGlobals->fEnteringScene = TRUE;
}

typedef struct tagSAVEDGAME_COMMON
{
	WORD             wSavedTimes;             // saved times
	WORD             wViewportX, wViewportY;  // viewport location
	WORD             nPartyMember;            // number of members in party
	WORD             wNumScene;               // scene number
	WORD             wPaletteOffset;
	WORD             wPartyDirection;         // party direction
	WORD             wNumMusic;               // music number
	WORD             wNumBattleMusic;         // battle music number
	WORD             wNumBattleField;         // battle field number
	WORD             wScreenWave;             // level of screen waving
	WORD             wBattleSpeed;            // battle speed
	WORD             wCollectValue;           // value of "collected" items
	WORD             wLayer;
	WORD             wChaseRange;
	WORD             wChasespeedChangeCycles;
	WORD             nFollower;
	WORD             rgwReserved2[3];         // unused
	DWORD            dwCash;                  // amount of cash
	PARTY            rgParty[MAX_PLAYABLE_PLAYER_ROLES];       // player party
	TRAIL            rgTrail[MAX_PLAYABLE_PLAYER_ROLES];       // player trail
	ALLEXPERIENCE    Exp;                     // experience data
	PLAYERROLES      PlayerRoles;
	POISONSTATUS     rgPoisonStatus[MAX_POISONS][MAX_PLAYABLE_PLAYER_ROLES]; // poison status
	INVENTORY        rgInventory[MAX_INVENTORY];               // inventory status
	SCENE            rgScene[MAX_SCENES];
} SAVEDGAME_COMMON, *LPSAVEDGAME_COMMON;

typedef struct tagSAVEDGAME_DOS
{
	WORD             wSavedTimes;             // saved times
	WORD             wViewportX, wViewportY;  // viewport location
	WORD             nPartyMember;            // number of members in party
	WORD             wNumScene;               // scene number
	WORD             wPaletteOffset;
	WORD             wPartyDirection;         // party direction
	WORD             wNumMusic;               // music number
	WORD             wNumBattleMusic;         // battle music number
	WORD             wNumBattleField;         // battle field number
	WORD             wScreenWave;             // level of screen waving
	WORD             wBattleSpeed;            // battle speed
	WORD             wCollectValue;           // value of "collected" items
	WORD             wLayer;
	WORD             wChaseRange;
	WORD             wChasespeedChangeCycles;
	WORD             nFollower;
	WORD             rgwReserved2[3];         // unused
	DWORD            dwCash;                  // amount of cash
	PARTY            rgParty[MAX_PLAYABLE_PLAYER_ROLES];       // player party
	TRAIL            rgTrail[MAX_PLAYABLE_PLAYER_ROLES];       // player trail
	ALLEXPERIENCE    Exp;                     // experience data
	PLAYERROLES      PlayerRoles;
	POISONSTATUS     rgPoisonStatus[MAX_POISONS][MAX_PLAYABLE_PLAYER_ROLES]; // poison status
	INVENTORY        rgInventory[MAX_INVENTORY];               // inventory status
	SCENE            rgScene[MAX_SCENES];
	OBJECT_DOS       rgObject[MAX_OBJECTS];
#if defined(PAL_PAGED_EVENT_STATE)
	EVENTOBJECT      rgEventObject[1]; /* streamed after the fixed header */
#else
	EVENTOBJECT      rgEventObject[MAX_EVENT_OBJECTS];
#endif
} SAVEDGAME_DOS, *LPSAVEDGAME_DOS;

typedef struct tagSAVEDGAME_WIN
{
	WORD             wSavedTimes;             // saved times
	WORD             wViewportX, wViewportY;  // viewport location
	WORD             nPartyMember;            // number of members in party
	WORD             wNumScene;               // scene number
	WORD             wPaletteOffset;
	WORD             wPartyDirection;         // party direction
	WORD             wNumMusic;               // music number
	WORD             wNumBattleMusic;         // battle music number
	WORD             wNumBattleField;         // battle field number
	WORD             wScreenWave;             // level of screen waving
	WORD             wBattleSpeed;            // battle speed
	WORD             wCollectValue;           // value of "collected" items
	WORD             wLayer;
	WORD             wChaseRange;
	WORD             wChasespeedChangeCycles;
	WORD             nFollower;
	WORD             rgwReserved2[3];         // unused
	DWORD            dwCash;                  // amount of cash
	PARTY            rgParty[MAX_PLAYABLE_PLAYER_ROLES];       // player party
	TRAIL            rgTrail[MAX_PLAYABLE_PLAYER_ROLES];       // player trail
	ALLEXPERIENCE    Exp;                     // experience data
	PLAYERROLES      PlayerRoles;
	POISONSTATUS     rgPoisonStatus[MAX_POISONS][MAX_PLAYABLE_PLAYER_ROLES]; // poison status
	INVENTORY        rgInventory[MAX_INVENTORY];               // inventory status
	SCENE            rgScene[MAX_SCENES];
	OBJECT           rgObject[MAX_OBJECTS];
#if defined(PAL_PAGED_EVENT_STATE)
	EVENTOBJECT      rgEventObject[1]; /* streamed after the fixed header */
#else
	EVENTOBJECT      rgEventObject[MAX_EVENT_OBJECTS];
#endif
} SAVEDGAME_WIN, *LPSAVEDGAME_WIN;

#ifdef PAL_NO_RUNTIME_HEAP
#if defined(PAL_PAGED_EVENT_STATE)
#define PAL_EXTREME_SAVE_DOS_BYTES offsetof(SAVEDGAME_DOS, rgEventObject)
#define PAL_EXTREME_SAVE_WIN_BYTES offsetof(SAVEDGAME_WIN, rgEventObject)
static uint8_t pal_sram_extreme_savegame_static[
   ((PAL_EXTREME_SAVE_WIN_BYTES > PAL_EXTREME_SAVE_DOS_BYTES) ?
      PAL_EXTREME_SAVE_WIN_BYTES : PAL_EXTREME_SAVE_DOS_BYTES)
   + sizeof(EVENTOBJECT)
] PAL_GLOBAL_PSRAM;
#define PAL_SAVEGAME_STATIC pal_sram_extreme_savegame_static
#else
static uint8_t pal_psram_savegame_static[
   (sizeof(SAVEDGAME_WIN) > sizeof(SAVEDGAME_DOS)) ? sizeof(SAVEDGAME_WIN) : sizeof(SAVEDGAME_DOS)
] PAL_GLOBAL_PSRAM;
#define PAL_SAVEGAME_STATIC pal_psram_savegame_static
#endif
#endif

#if defined(PAL_PAGED_EVENT_STATE)
#include "esp32s3/engine_bridge/pal_engine_extreme_save.inc"
#endif

WORD
PAL_GetSavedTimes(
   int iSaveSlot
)
{
#if defined(PAL_PAGED_EVENT_STATE)
   char final_path[PAL_EXTREME_SAVE_PATH_BYTES];
   char backup_path[PAL_EXTREME_SAVE_PATH_BYTES];
   WORD saved_times = 0;

   if (!PAL_ExtremeSaveBuildPath(iSaveSlot, "rpg", final_path,
         sizeof(final_path)) ||
      !PAL_ExtremeSaveBuildPath(iSaveSlot, "bak", backup_path,
         sizeof(backup_path)))
   {
      return 0;
   }
   if (PAL_ExtremeReadSavedTimesAtPath(final_path, &saved_times) ||
      PAL_ExtremeReadSavedTimesAtPath(backup_path, &saved_times))
   {
      return saved_times;
   }
   return 0;
#else
   FILE *fp = UTIL_OpenFileAtPath(gConfig.pszSavePath,
      PAL_va(0, "%d.rpg", iSaveSlot));
   WORD saved_times = 0;

   if (fp != NULL)
   {
      if (fread(&saved_times, sizeof(saved_times), 1, fp) == 1)
      {
         saved_times = SDL_SwapLE16(saved_times);
      }
      else
      {
         saved_times = 0;
      }
      fclose(fp);
   }
   return saved_times;
#endif
}

static BOOL
PAL_LoadGame_Common(
	int                 iSaveSlot,
	LPSAVEDGAME_COMMON  s,
	size_t              size
)
{
#if defined(PAL_PAGED_EVENT_STATE)
	char final_path[PAL_EXTREME_SAVE_PATH_BYTES];
	char backup_path[PAL_EXTREME_SAVE_PATH_BYTES];
	WORD format = gConfig.fIsWIN95 ?
		PAL_EXTREME_SAVE_FORMAT_WIN95 : PAL_EXTREME_SAVE_FORMAT_DOS;
	BOOL loaded = FALSE;
	FILE *fp = NULL;

	if (!PAL_ExtremeSaveBuildPath(iSaveSlot, "rpg", final_path,
			sizeof(final_path)) ||
		!PAL_ExtremeSaveBuildPath(iSaveSlot, "bak", backup_path,
			sizeof(backup_path)))
	{
		return FALSE;
	}

	fp = fopen(final_path, "rb");
	if (fp != NULL)
	{
		loaded = PAL_ExtremeSaveReadFile(fp, s, size, format);
		if (fclose(fp) != 0)
		{
			loaded = FALSE;
		}
	}

	if (!loaded)
	{
		fp = fopen(backup_path, "rb");
		if (fp != NULL)
		{
			loaded = PAL_ExtremeSaveReadFile(fp, s, size, format);
			if (fclose(fp) != 0)
			{
				loaded = FALSE;
			}
			if (loaded)
			{
				UTIL_LogOutput(LOGLEVEL_WARNING,
					"Recovered extreme save slot %d from backup\n", iSaveSlot);
			}
		}
	}

	if (!loaded)
	{
		UTIL_LogOutput(LOGLEVEL_WARNING,
			"Rejected missing, corrupt, incompatible, or out-of-profile "
			"extreme save slot %d\n", iSaveSlot);
		return FALSE;
	}
#else
	//
	// Try to open the specified file
	//
	FILE *fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, PAL_va(1, "%d.rpg", iSaveSlot));
	//
	// Read all data from the file and close.
	//
	size_t n = fp ? fread(s, 1, size, fp) : 0;

	if (fp != NULL)
	{
		fclose(fp);
	}

	if (n < size - sizeof(EVENTOBJECT) * MAX_EVENT_OBJECTS ||
		(n >= 8 && memcmp(s, PAL_EXTREME_SAVE_MAGIC, 8) == 0))
	{
		return FALSE;
	}
#endif

	//
	// Adjust endianness
	//
#if !defined(PAL_PAGED_EVENT_STATE)
	DO_BYTESWAP(s, size);
#endif

	//
	// Cash amount is in DWORD, so do a wordswap in Big-Endian.
	//
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	s->dwCash = ((s->dwCash >> 16) | (s->dwCash << 16));
#endif

	//
	// Get common data from the saved game struct.
	//
	gpGlobals->viewport = PAL_XY(s->wViewportX, s->wViewportY);
	gpGlobals->wMaxPartyMemberIndex = s->nPartyMember;
	gpGlobals->wNumScene = s->wNumScene;
	gpGlobals->fNightPalette = (s->wPaletteOffset != 0);
	gpGlobals->wPartyDirection = s->wPartyDirection;
	gpGlobals->wNumMusic = s->wNumMusic;
	gpGlobals->wNumBattleMusic = s->wNumBattleMusic;
	gpGlobals->wNumBattleField = s->wNumBattleField;
	gpGlobals->wScreenWave = s->wScreenWave;
	gpGlobals->sWaveProgression = 0;
	gpGlobals->wCollectValue = s->wCollectValue;
	gpGlobals->wLayer = s->wLayer;
	gpGlobals->wChaseRange = s->wChaseRange;
	gpGlobals->wChasespeedChangeCycles = s->wChasespeedChangeCycles;
	gpGlobals->nFollower = s->nFollower;
	gpGlobals->dwCash = s->dwCash;
#ifndef PAL_CLASSIC
	gpGlobals->bBattleSpeed = s->wBattleSpeed;
	if (gpGlobals->bBattleSpeed > 5 || gpGlobals->bBattleSpeed == 0)
	{
		gpGlobals->bBattleSpeed = 2;
	}
#endif

	memcpy(gpGlobals->rgParty, s->rgParty, sizeof(gpGlobals->rgParty));
	memcpy(gpGlobals->rgTrail, s->rgTrail, sizeof(gpGlobals->rgTrail));
	gpGlobals->Exp = s->Exp;
	gpGlobals->g.PlayerRoles = s->PlayerRoles;
	memset(gpGlobals->rgPoisonStatus, 0, sizeof(gpGlobals->rgPoisonStatus));
	memcpy(gpGlobals->rgInventory, s->rgInventory, sizeof(gpGlobals->rgInventory));
	memcpy(gpGlobals->g.rgScene, s->rgScene, sizeof(gpGlobals->g.rgScene));

	gpGlobals->fEnteringScene = FALSE;

	PAL_CompressInventory();

	return TRUE;
}

static INT
PAL_LoadGame_DOS(
   int            iSaveSlot
)
/*++
  Purpose:

    Load a saved game.

  Parameters:

    [IN]  szFileName - file name of saved game.

  Return value:

    0 if success, -1 if failed.

--*/
{
#ifdef PAL_NO_RUNTIME_HEAP
   SAVEDGAME_DOS   *s = (SAVEDGAME_DOS*)PAL_SAVEGAME_STATIC;
#else
   SAVEDGAME_DOS   *s = (SAVEDGAME_DOS*)malloc(sizeof(SAVEDGAME_DOS));
#endif
   int                       i;

   //
   // Get all the data from the saved game struct.
   //
   if (!PAL_LoadGame_Common(iSaveSlot, (LPSAVEDGAME_COMMON)s,
#if defined(PAL_PAGED_EVENT_STATE)
      PAL_EXTREME_SAVE_DOS_BYTES
#else
      sizeof(SAVEDGAME_DOS)
#endif
      ))
   {
#ifndef PAL_NO_RUNTIME_HEAP
      free(s);
#endif
	   return -1;
   }

   //
   // Convert the DOS-style data structure to WIN-style data structure
   //
   for (i = 0; i < MAX_OBJECTS; i++)
   {
      memcpy(&gpGlobals->g.rgObject[i], &s->rgObject[i], sizeof(OBJECT_DOS));
      gpGlobals->g.rgObject[i].rgwData[6] = s->rgObject[i].rgwData[5];     // wFlags
      gpGlobals->g.rgObject[i].rgwData[5] = 0;                            // wScriptDesc or wReserved2
   }
#if !defined(PAL_PAGED_EVENT_STATE)
   memcpy(gpGlobals->g.lprgEventObject, s->rgEventObject, sizeof(EVENTOBJECT) * gpGlobals->g.nEventObject);
#endif

#ifndef PAL_NO_RUNTIME_HEAP
   free(s);
#endif

   //
   // Success
   //
   return 0;
}

static INT
PAL_LoadGame_WIN(
   int            iSaveSlot
)
/*++
  Purpose:

    Load a saved game.

  Parameters:

    [IN]  szFileName - file name of saved game.

  Return value:

    0 if success, -1 if failed.

--*/
{
#ifdef PAL_NO_RUNTIME_HEAP
   SAVEDGAME_WIN   *s = (SAVEDGAME_WIN*)PAL_SAVEGAME_STATIC;
#else
   SAVEDGAME_WIN   *s = (SAVEDGAME_WIN*)malloc(sizeof(SAVEDGAME_WIN));
#endif

   //
   // Get all the data from the saved game struct.
   //
   if (!PAL_LoadGame_Common(iSaveSlot, (LPSAVEDGAME_COMMON)s,
#if defined(PAL_PAGED_EVENT_STATE)
      PAL_EXTREME_SAVE_WIN_BYTES
#else
      sizeof(SAVEDGAME_WIN)
#endif
      ))
   {
#ifndef PAL_NO_RUNTIME_HEAP
      free(s);
#endif
	   return -1;
   }

   memcpy(gpGlobals->g.rgObject, s->rgObject, sizeof(gpGlobals->g.rgObject));
#if !defined(PAL_PAGED_EVENT_STATE)
   memcpy(gpGlobals->g.lprgEventObject, s->rgEventObject, sizeof(EVENTOBJECT) * gpGlobals->g.nEventObject);
#endif
    
#ifndef PAL_NO_RUNTIME_HEAP
   free(s);
#endif

   //
   // Success
   //
   return 0;
}

static INT
PAL_LoadGame(
   int            iSaveSlot
)
{
	return gConfig.fIsWIN95 ? PAL_LoadGame_WIN(iSaveSlot) : PAL_LoadGame_DOS(iSaveSlot);
}

static BOOL
PAL_SaveGame_Common(
	int                iSaveSlot,
	WORD               wSavedTimes,
	LPSAVEDGAME_COMMON s,
	size_t             size
)
{
#if !defined(PAL_PAGED_EVENT_STATE)
	FILE *fp;
	size_t i;
#endif

	s->wSavedTimes = wSavedTimes;
	s->wViewportX = PAL_X(gpGlobals->viewport);
	s->wViewportY = PAL_Y(gpGlobals->viewport);
	s->nPartyMember = gpGlobals->wMaxPartyMemberIndex;
	s->wNumScene = gpGlobals->wNumScene;
	s->wPaletteOffset = (gpGlobals->fNightPalette ? 0x180 : 0);
	s->wPartyDirection = gpGlobals->wPartyDirection;
	s->wNumMusic = gpGlobals->wNumMusic;
	s->wNumBattleMusic = gpGlobals->wNumBattleMusic;
	s->wNumBattleField = gpGlobals->wNumBattleField;
	s->wScreenWave = gpGlobals->wScreenWave;
	s->wCollectValue = gpGlobals->wCollectValue;
	s->wLayer = gpGlobals->wLayer;
	s->wChaseRange = gpGlobals->wChaseRange;
	s->wChasespeedChangeCycles = gpGlobals->wChasespeedChangeCycles;
	s->nFollower = gpGlobals->nFollower;
	s->dwCash = gpGlobals->dwCash;
#ifndef PAL_CLASSIC
	s->wBattleSpeed = gpGlobals->bBattleSpeed;
#else
	s->wBattleSpeed = 2;
#endif

	memcpy(s->rgParty, gpGlobals->rgParty, sizeof(gpGlobals->rgParty));
	memcpy(s->rgTrail, gpGlobals->rgTrail, sizeof(gpGlobals->rgTrail));
	s->Exp = gpGlobals->Exp;
	s->PlayerRoles = gpGlobals->g.PlayerRoles;
	memcpy(s->rgPoisonStatus, gpGlobals->rgPoisonStatus, sizeof(gpGlobals->rgPoisonStatus));
	memcpy(s->rgInventory, gpGlobals->rgInventory, sizeof(gpGlobals->rgInventory));
	memcpy(s->rgScene, gpGlobals->g.rgScene, sizeof(gpGlobals->g.rgScene));

	//
	// Adjust endianness
	//
	DO_BYTESWAP(s, size);

	//
	// Cash amount is in DWORD, so do a wordswap in Big-Endian.
	//
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	s->dwCash = ((s->dwCash >> 16) | (s->dwCash << 16));
#endif

#if defined(PAL_PAGED_EVENT_STATE)
	BOOL result = PAL_ExtremeSaveWriteFile(iSaveSlot,
		gConfig.fIsWIN95 ? PAL_EXTREME_SAVE_FORMAT_WIN95 :
			PAL_EXTREME_SAVE_FORMAT_DOS,
		s, size);
	if (!result)
	{
		UTIL_LogOutput(LOGLEVEL_ERROR,
			"Failed to write, verify, or atomically replace extreme save "
			"slot %d\n", iSaveSlot);
	}
	return result;
#else
	//
	// Try writing to file
	//
	if ((fp = UTIL_OpenFileAtPathForMode(gConfig.pszSavePath, PAL_va(1, "%d.rpg", iSaveSlot), "wb")) == NULL)
	{
		return FALSE;
	}

	i = PAL_MKFGetChunkSize(0, gpGlobals->f.fpSSS);
	i += size - sizeof(EVENTOBJECT) * MAX_EVENT_OBJECTS;

	{
		BOOL result = fwrite(s, i, 1, fp) == 1;
		if (fclose(fp) != 0)
		{
			result = FALSE;
		}
		return result;
	}
#endif
}

static BOOL
PAL_SaveGame_DOS(
   int            iSaveSlot,
   WORD           wSavedTimes
)
/*++
  Purpose:

    Save the current game state to file.

  Parameters:

    [IN]  szFileName - file name of saved game.

  Return value:

    None.

--*/
{
#ifdef PAL_NO_RUNTIME_HEAP
   SAVEDGAME_DOS   *s = (SAVEDGAME_DOS*)PAL_SAVEGAME_STATIC;
#else
   SAVEDGAME_DOS   *s = (SAVEDGAME_DOS*)malloc(sizeof(SAVEDGAME_DOS));
#endif
   UINT32                    i;
   BOOL                      result;

#if defined(PAL_PAGED_EVENT_STATE)
   memset(s, 0, PAL_EXTREME_SAVE_DOS_BYTES);
#endif
   //
   // Convert the WIN-style data structure to DOS-style data structure
   //
   for (i = 0; i < MAX_OBJECTS; i++)
   {
      memcpy(&s->rgObject[i], &gpGlobals->g.rgObject[i], sizeof(OBJECT_DOS));
      s->rgObject[i].rgwData[5] = gpGlobals->g.rgObject[i].rgwData[6];     // wFlags
   }
#if !defined(PAL_PAGED_EVENT_STATE)
   memcpy(s->rgEventObject, gpGlobals->g.lprgEventObject, sizeof(EVENTOBJECT) * gpGlobals->g.nEventObject);
#endif

   //
   // Put all the data to the saved game struct.
   //
   result = PAL_SaveGame_Common(iSaveSlot, wSavedTimes, (LPSAVEDGAME_COMMON)s,
#if defined(PAL_PAGED_EVENT_STATE)
      PAL_EXTREME_SAVE_DOS_BYTES
#else
      sizeof(SAVEDGAME_DOS)
#endif
      );
#ifndef PAL_NO_RUNTIME_HEAP
   free(s);
#endif
   return result;
}

static BOOL
PAL_SaveGame_WIN(
   int            iSaveSlot,
   WORD           wSavedTimes
)
/*++
  Purpose:

    Save the current game state to file.

  Parameters:

    [IN]  szFileName - file name of saved game.

  Return value:

    None.

--*/
{
#ifdef PAL_NO_RUNTIME_HEAP
   SAVEDGAME_WIN   *s = (SAVEDGAME_WIN*)PAL_SAVEGAME_STATIC;
#else
   SAVEDGAME_WIN   *s = (SAVEDGAME_WIN*)malloc(sizeof(SAVEDGAME_WIN));
#endif
   BOOL result;

#if defined(PAL_PAGED_EVENT_STATE)
   memset(s, 0, PAL_EXTREME_SAVE_WIN_BYTES);
#endif
   //
   // Put all the data to the saved game struct.
   //
   memcpy(&s->rgObject, gpGlobals->g.rgObject, sizeof(gpGlobals->g.rgObject));
#if !defined(PAL_PAGED_EVENT_STATE)
   memcpy(&s->rgEventObject, gpGlobals->g.lprgEventObject, sizeof(EVENTOBJECT) * gpGlobals->g.nEventObject);
#endif

   result = PAL_SaveGame_Common(iSaveSlot, wSavedTimes, (LPSAVEDGAME_COMMON)s,
#if defined(PAL_PAGED_EVENT_STATE)
      PAL_EXTREME_SAVE_WIN_BYTES
#else
      sizeof(SAVEDGAME_WIN)
#endif
      );

#ifndef PAL_NO_RUNTIME_HEAP
   free(s);
#endif
   return result;
}

BOOL
PAL_SaveGame(
   int            iSaveSlot,
   WORD           wSavedTimes
)
{
	if (gConfig.fIsWIN95)
		return PAL_SaveGame_WIN(iSaveSlot, wSavedTimes);
	return PAL_SaveGame_DOS(iSaveSlot, wSavedTimes);
}

VOID
PAL_ReloadInNextTick(
    INT           iSaveSlot
)
/*++
  Purpose:

    Reload the game IN NEXT TICK, avoid reentrant problems.

  Parameters:

    [IN]  iSaveSlot - Slot of saved game.

  Return value:

    None.

--*/
{
    gpGlobals->bCurrentSaveSlot = (BYTE)iSaveSlot;
    PAL_SetLoadFlags(kLoadGlobalData | kLoadScene | kLoadPlayerSprite);
    gpGlobals->fEnteringScene = TRUE;
    gpGlobals->fNeedToFadeIn = TRUE;
    gpGlobals->dwFrameNum = 0;
}

VOID
PAL_InitGameData(
   INT         iSaveSlot
)
/*++
  Purpose:

    Initialize the game data (used when starting a new game or loading a saved game).

  Parameters:

    [IN]  iSaveSlot - Slot of saved game.

  Return value:

    None.

--*/
{
   PAL_InitGlobalGameData();

   gpGlobals->bCurrentSaveSlot = (BYTE)iSaveSlot;

   //
   // try loading from the saved game file.
   //
   if (iSaveSlot == 0 || PAL_LoadGame(iSaveSlot) != 0)
   {
      //
      // Cannot load the saved game file. Load the defaults.
      //
      PAL_LoadDefaultGame();
   }

   gpGlobals->iCurInvMenuItem = 0;
   gpGlobals->fInBattle = FALSE;

   memset(gpGlobals->rgPlayerStatus, 0, sizeof(gpGlobals->rgPlayerStatus));

   PAL_UpdateEquipments();
}

INT
PAL_CountItem(
   WORD          wObjectID
)
/*++
 Purpose:
 
 Count the specified kind of item in the inventory AND in players' equipments.
 
 Parameters:
 
 [IN]  wObjectID - object number of the item.
 
 Return value:
 
 Counted value.
 
 --*/
{
    int          index;
    int          count;
    int          i,j,w;

    if (wObjectID == 0)
    {
        return FALSE;
    }
    
    index = 0;
    count = 0;
    
    //
    // Search for the specified item in the inventory
    //
    while (index < MAX_INVENTORY)
    {
        if (gpGlobals->rgInventory[index].wItem == wObjectID)
        {
            count = gpGlobals->rgInventory[index].nAmount;
            break;
        }
        else if (gpGlobals->rgInventory[index].wItem == 0)
        {
            break;
        }
        index++;
    }
    
    for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
    {
        w = gpGlobals->rgParty[i].wPlayerRole;
        
        for (j = 0; j < MAX_PLAYER_EQUIPMENTS; j++)
        {
            if (gpGlobals->g.PlayerRoles.rgwEquipment[j][w] == wObjectID)
            {
                count++;
            }
        }
    }
    return count;
}

BOOL
PAL_GetItemIndexToInventory(
   WORD          wObjectID,
   INT          *index
)
/*++
  Purpose:

    Search for the specified item in the inventory.

  Parameters:

    [IN]  wObjectID - object number of the item.

    [IN]  index - use a pointer to receive the retrieved inventory index.

  Return value:

    TRUE if found it, FALSE if not found it.

--*/
{
   BOOL         fFound = FALSE;

   *index = 0;

   while (*index < MAX_INVENTORY)
   {
      if (gpGlobals->rgInventory[*index].wItem == wObjectID)
      {
         fFound = TRUE;
         break;
      }
      else if (gpGlobals->rgInventory[*index].wItem == 0)
      {
         break;
      }
      (*index)++;
   }

   return fFound;
}

INT
PAL_AddItemToInventory(
   WORD          wObjectID,
   INT           iNum
)
/*++
  Purpose:

    Add or remove the specified kind of item in the inventory.

  Parameters:

    [IN]  wObjectID - object number of the item.

    [IN]  iNum - number to be added (positive value) or removed (negative value).

  Return value:

    TRUE(1) if succeeded.
    FALSE(0) if failed and inventory keeps unmodified.
    Negative value indicates that a run-out case happened and inventory is cleared.

--*/
{
   int          index;
   BOOL         fFound;

   if (wObjectID == 0)
   {
      return FALSE;
   }

   if (iNum == 0)
   {
      iNum = 1;
   }

   index = 0;
   fFound = FALSE;

   //
   // Search for the specified item in the inventory
   //
   fFound = PAL_GetItemIndexToInventory(wObjectID, &index);

   if (iNum > 0)
   {
      //
      // Add item
      //
      if (index >= MAX_INVENTORY)
      {
         //
         // inventory is full. cannot add item
         //
         return FALSE;
      }

      if (fFound)
      {
         gpGlobals->rgInventory[index].nAmount += iNum;
         if (gpGlobals->rgInventory[index].nAmount > 99)
         {
            //
            // Maximum number is 99
            //
            gpGlobals->rgInventory[index].nAmount = 99;
         }
      }
      else
      {
         gpGlobals->rgInventory[index].wItem = wObjectID;
         if (iNum > 99)
         {
            iNum = 99;
         }
         gpGlobals->rgInventory[index].nAmount = iNum;
      }

      return TRUE;
   }
   else
   {
      //
      // Remove item
      //
      if (fFound)
      {
         iNum *= -1;
         if (gpGlobals->rgInventory[index].nAmount < iNum)
         {
            //
            // This item has been run out, should return the shortage amount for further processing
            //
            iNum -= gpGlobals->rgInventory[index].nAmount;
            gpGlobals->rgInventory[index].nAmount = 0;
            return -iNum;
         }

         gpGlobals->rgInventory[index].nAmount -= iNum;
         //
         /// Need process last item
         //
         if(gpGlobals->rgInventory[index].nAmount == 0 && index == gpGlobals->iCurInvMenuItem && index+1 < MAX_INVENTORY && gpGlobals->rgInventory[index+1].nAmount <= 0)
            gpGlobals->iCurInvMenuItem --;
         return TRUE;
      }

      return FALSE;
   }
}

INT
PAL_GetItemAmount(
   WORD        wItem
)
/*++
  Purpose:

    Get the amount of the specified item in the inventory.

  Parameters:

    [IN]  wItem - the object ID of the item.

  Return value:

    The amount of the item in the inventory.

--*/
{
   int i;

   for (i = 0; i < MAX_INVENTORY; i++)
   {
      if (gpGlobals->rgInventory[i].wItem == 0)
      {
         break;
      }

      if (gpGlobals->rgInventory[i].wItem == wItem)
      {
         return gpGlobals->rgInventory[i].nAmount;
      }
   }

   return 0;
}

VOID
PAL_CompressInventory(
   VOID
)
/*++
  Purpose:

    Remove all the items in inventory which has a number of zero.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int i, j;

   j = 0;

   for (i = 0; i < MAX_INVENTORY; i++)
   {
      //removed detect zero then break code, due to incompatible with save file hacked by palmod

      if (gpGlobals->rgInventory[i].nAmount > 0)
      {
         gpGlobals->rgInventory[j] = gpGlobals->rgInventory[i];
         j++;
      }
   }

   for (; j < MAX_INVENTORY; j++)
   {
      gpGlobals->rgInventory[j].nAmount = 0;
      gpGlobals->rgInventory[j].nAmountInUse = 0;
      gpGlobals->rgInventory[j].wItem = 0;
   }
}

BOOL
PAL_IncreaseHPMP(
   WORD          wPlayerRole,
   SHORT         sHP,
   SHORT         sMP
)
/*++
  Purpose:

    Increase or decrease player's HP and/or MP.

  Parameters:

    [IN]  wPlayerRole - the number of player role.

    [IN]  sHP - number of HP to be increased (positive value) or decrased
                (negative value).

    [IN]  sMP - number of MP to be increased (positive value) or decrased
                (negative value).

  Return value:

    TRUE if the operation is succeeded, FALSE if not.

--*/
{
   BOOL           fSuccess = FALSE;
   WORD           wOrigHP = gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole];
   WORD           wOrigMP = gpGlobals->g.PlayerRoles.rgwMP[wPlayerRole];

   //
   // Only care about alive players
   //
   if (gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] > 0)
   {
      //
      // change HP
      //
      gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] += sHP;

      if ((SHORT)(gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole]) < 0)
      {
         gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] = 0;
      }
      else if (gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] >
         gpGlobals->g.PlayerRoles.rgwMaxHP[wPlayerRole])
      {
         gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] =
            gpGlobals->g.PlayerRoles.rgwMaxHP[wPlayerRole];
      }

      //
      // Change MP
      //
      gpGlobals->g.PlayerRoles.rgwMP[wPlayerRole] += sMP;

      if ((SHORT)(gpGlobals->g.PlayerRoles.rgwMP[wPlayerRole]) < 0)
      {
         gpGlobals->g.PlayerRoles.rgwMP[wPlayerRole] = 0;
      }
      else if (gpGlobals->g.PlayerRoles.rgwMP[wPlayerRole] >
         gpGlobals->g.PlayerRoles.rgwMaxMP[wPlayerRole])
      {
         gpGlobals->g.PlayerRoles.rgwMP[wPlayerRole] =
            gpGlobals->g.PlayerRoles.rgwMaxMP[wPlayerRole];
      }

      //
      // Avoid over treatment
      //
      if (wOrigHP != gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] ||
          wOrigMP != gpGlobals->g.PlayerRoles.rgwMP[wPlayerRole])
         fSuccess = TRUE;
   }

   return fSuccess;
}

VOID
PAL_UpdateEquipments(
   VOID
)
/*++
  Purpose:

    Update the effects of all equipped items for all players.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int      i, j;
   WORD     w;

   memset(&(gpGlobals->rgEquipmentEffect), 0, sizeof(gpGlobals->rgEquipmentEffect));

   for (i = 0; i < MAX_PLAYER_ROLES; i++)
   {
      for (j = 0; j < MAX_PLAYER_EQUIPMENTS; j++)
      {
         w = gpGlobals->g.PlayerRoles.rgwEquipment[j][i];

         if (w != 0)
         {
            gpGlobals->g.rgObject[w].item.wScriptOnEquip =
               PAL_RunTriggerScript(gpGlobals->g.rgObject[w].item.wScriptOnEquip, (WORD)i);
         }
      }
   }
}

VOID
PAL_RemoveEquipmentEffect(
   WORD         wPlayerRole,
   WORD         wEquipPart
)
/*++
  Purpose:

    Remove all the effects of the equipment for the player.

  Parameters:

    [IN]  wPlayerRole - the player role.

    [IN]  wEquipPart - the part of the equipment.

  Return value:

    None.

--*/
{
   WORD       *p;
   int         i, j;

   p = (WORD *)(&gpGlobals->rgEquipmentEffect[wEquipPart]); // HACKHACK

   for (i = 0; i < sizeof(PLAYERROLES) / sizeof(PLAYERS); i++)
   {
      p[i * MAX_PLAYER_ROLES + wPlayerRole] = 0;
   }

   //
   // Reset some parameters to default when appropriate
   //
   if (wEquipPart == kBodyPartHand)
   {
      //
      // reset the dual attack status
      //
      gpGlobals->rgPlayerStatus[wPlayerRole][kStatusDualAttack] = 0;
   }
   else if (wEquipPart == kBodyPartWear)
   {
      //
      // Remove all poisons leveled 99
      //
      for (i = 0; i <= (short)gpGlobals->wMaxPartyMemberIndex; i++)
      {
         if (gpGlobals->rgParty[i].wPlayerRole == wPlayerRole)
         {
            wPlayerRole = i;
            break;
         }
      }

      if (i <= (short)gpGlobals->wMaxPartyMemberIndex)
      {
         j = 0;

         for (i = 0; i < MAX_POISONS; i++)
         {
            WORD w = gpGlobals->rgPoisonStatus[i][wPlayerRole].wPoisonID;

            if (w == 0)
            {
               break;
            }

            if (gpGlobals->g.rgObject[w].poison.wPoisonLevel < 99)
            {
               gpGlobals->rgPoisonStatus[j][wPlayerRole] =
                  gpGlobals->rgPoisonStatus[i][wPlayerRole];
               j++;
            }
         }

         while (j < MAX_POISONS)
         {
            gpGlobals->rgPoisonStatus[j][wPlayerRole].wPoisonID = 0;
            gpGlobals->rgPoisonStatus[j][wPlayerRole].wPoisonScript = 0;
            j++;
         }
      }
   }
}

VOID
PAL_AddPoisonForPlayer(
   WORD           wPlayerRole,
   WORD           wPoisonID
)
/*++
  Purpose:

    Add the specified poison to the player.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  wPoisonID - the poison to be added.

  Return value:

    None.

--*/
{
   int         i, index;
   WORD        w;

   for (index = 0; index <= gpGlobals->wMaxPartyMemberIndex; index++)
   {
      if (gpGlobals->rgParty[index].wPlayerRole == wPlayerRole)
      {
         break;
      }
   }

   if (index > gpGlobals->wMaxPartyMemberIndex)
   {
      return; // don't go further
   }

   for (i = 0; i < MAX_POISONS; i++)
   {
      w = gpGlobals->rgPoisonStatus[i][index].wPoisonID;

      if (w == 0)
      {
         break;
      }

      if (w == wPoisonID)
      {
         return; // already poisoned
      }
   }

   if (i < MAX_POISONS)
   {
      gpGlobals->rgPoisonStatus[i][index].wPoisonID = wPoisonID;
      gpGlobals->rgPoisonStatus[i][index].wPoisonScript =
		  PAL_RunTriggerScript(gpGlobals->g.rgObject[wPoisonID].poison.wPlayerScript, wPlayerRole);
   }
}

VOID
PAL_CurePoisonByKind(
   WORD           wPlayerRole,
   WORD           wPoisonID
)
/*++
  Purpose:

    Remove the specified poison from the player.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  wPoisonID - the poison to be removed.

  Return value:

    None.

--*/
{
   int i, index;

   for (index = 0; index <= gpGlobals->wMaxPartyMemberIndex; index++)
   {
      if (gpGlobals->rgParty[index].wPlayerRole == wPlayerRole)
      {
         break;
      }
   }

   if (index > gpGlobals->wMaxPartyMemberIndex)
   {
      return; // don't go further
   }

   for (i = 0; i < MAX_POISONS; i++)
   {
      if (gpGlobals->rgPoisonStatus[i][index].wPoisonID == wPoisonID)
      {
         gpGlobals->rgPoisonStatus[i][index].wPoisonID = 0;
         gpGlobals->rgPoisonStatus[i][index].wPoisonScript = 0;
      }
   }
}

VOID
PAL_CurePoisonByLevel(
   WORD           wPlayerRole,
   WORD           wMaxLevel
)
/*++
  Purpose:

    Remove the poisons which have a maximum level of wMaxLevel from the player.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  wMaxLevel - the maximum level of poisons to be removed.

  Return value:

    None.

--*/
{
   int        i, index;
   WORD       w;

   for (index = 0; index <= gpGlobals->wMaxPartyMemberIndex; index++)
   {
      if (gpGlobals->rgParty[index].wPlayerRole == wPlayerRole)
      {
         break;
      }
   }

   if (index > gpGlobals->wMaxPartyMemberIndex)
   {
      return; // don't go further
   }

   for (i = 0; i < MAX_POISONS; i++)
   {
      w = gpGlobals->rgPoisonStatus[i][index].wPoisonID;

      if (gpGlobals->g.rgObject[w].poison.wPoisonLevel <= wMaxLevel)
      {
         gpGlobals->rgPoisonStatus[i][index].wPoisonID = 0;
         gpGlobals->rgPoisonStatus[i][index].wPoisonScript = 0;
      }
   }
}

BOOL
PAL_IsPlayerPoisonedByLevel(
   WORD           wPlayerRole,
   WORD           wMinLevel
)
/*++
  Purpose:

    Check if the player is poisoned by poisons at a minimum level of wMinLevel.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  wMinLevel - the minimum level of poison.

  Return value:

    TRUE if the player is poisoned by poisons at a minimum level of wMinLevel;
    FALSE if not.

--*/
{
   int         i, index;
   WORD        w;

   for (index = 0; index <= gpGlobals->wMaxPartyMemberIndex; index++)
   {
      if (gpGlobals->rgParty[index].wPlayerRole == wPlayerRole)
      {
         break;
      }
   }

   if (index > gpGlobals->wMaxPartyMemberIndex)
   {
      return FALSE; // don't go further
   }

   for (i = 0; i < MAX_POISONS; i++)
   {
      w = gpGlobals->rgPoisonStatus[i][index].wPoisonID;

      if (w == 0)
      {
         //
         // Skip empty PoisonID
         //
         continue;
      }

      w = gpGlobals->g.rgObject[w].poison.wPoisonLevel;

      if (w >= 99)
      {
         //
         // Ignore poisons which has a level of 99 (usually effect of equipment)
         //
         continue;
      }

      if (w >= wMinLevel)
      {
         return TRUE;
      }
   }

   return FALSE;
}

BOOL
PAL_IsPlayerPoisonedByKind(
   WORD           wPlayerRole,
   WORD           wPoisonID
)
/*++
  Purpose:

    Check if the player is poisoned by the specified poison.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  wPoisonID - the poison to be checked.

  Return value:

    TRUE if player is poisoned by the specified poison;
    FALSE if not.

--*/
{
   int i, index;

   for (index = 0; index <= gpGlobals->wMaxPartyMemberIndex; index++)
   {
      if (gpGlobals->rgParty[index].wPlayerRole == wPlayerRole)
      {
         break;
      }
   }

   if (index > gpGlobals->wMaxPartyMemberIndex)
   {
      return FALSE; // don't go further
   }

   for (i = 0; i < MAX_POISONS; i++)
   {
      if (gpGlobals->rgPoisonStatus[i][index].wPoisonID == wPoisonID)
      {
         return TRUE;
      }
   }

   return FALSE;
}

WORD
PAL_GetPlayerAttackStrength(
   WORD           wPlayerRole
)
/*++
  Purpose:

    Get the player's attack strength, count in the effect of equipments.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    The total attack strength of the player.

--*/
{
   WORD       w;
   int        i;

   w = gpGlobals->g.PlayerRoles.rgwAttackStrength[wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      w += gpGlobals->rgEquipmentEffect[i].rgwAttackStrength[wPlayerRole];
   }

   return w;
}

WORD
PAL_GetPlayerMagicStrength(
   WORD           wPlayerRole
)
/*++
  Purpose:

    Get the player's magic strength, count in the effect of equipments.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    The total magic strength of the player.

--*/
{
   WORD       w;
   int        i;

   w = gpGlobals->g.PlayerRoles.rgwMagicStrength[wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      w += gpGlobals->rgEquipmentEffect[i].rgwMagicStrength[wPlayerRole];
   }

   return w;
}

WORD
PAL_GetPlayerDefense(
   WORD           wPlayerRole
)
/*++
  Purpose:

    Get the player's defense value, count in the effect of equipments.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    The total defense value of the player.

--*/
{
   WORD       w;
   int        i;

   w = gpGlobals->g.PlayerRoles.rgwDefense[wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      w += gpGlobals->rgEquipmentEffect[i].rgwDefense[wPlayerRole];
   }

   return w;
}

WORD
PAL_GetPlayerDexterity(
   WORD           wPlayerRole
)
/*++
  Purpose:

    Get the player's dexterity, count in the effect of equipments.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    The total dexterity of the player.

--*/
{
   WORD       w;
   int        i;

   w = gpGlobals->g.PlayerRoles.rgwDexterity[wPlayerRole];

#ifdef PAL_CLASSIC
   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
#else
   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS - 1; i++)
#endif
   {
      w += gpGlobals->rgEquipmentEffect[i].rgwDexterity[wPlayerRole];
   }

   return w;
}

WORD
PAL_GetPlayerFleeRate(
   WORD           wPlayerRole
)
/*++
  Purpose:

    Get the player's flee rate, count in the effect of equipments.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    The total flee rate of the player.

--*/
{
   WORD       w;
   int        i;

   w = gpGlobals->g.PlayerRoles.rgwFleeRate[wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      w += gpGlobals->rgEquipmentEffect[i].rgwFleeRate[wPlayerRole];
   }

   return w;
}

WORD
PAL_GetPlayerPoisonResistance(
   WORD           wPlayerRole
)
/*++
  Purpose:

    Get the player's resistance to poisons, count in the effect of equipments.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    The total resistance to poisons of the player.

--*/
{
   WORD       w;
   int        i;

   w = gpGlobals->g.PlayerRoles.rgwPoisonResistance[wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      w += gpGlobals->rgEquipmentEffect[i].rgwPoisonResistance[wPlayerRole];
   }

   if (w > 100)
   {
      w = 100;
   }

   return w;
}

WORD
PAL_GetPlayerElementalResistance(
   WORD           wPlayerRole,
   INT            iAttrib
)
/*++
  Purpose:

    Get the player's resistance to attributed magics, count in the effect
    of equipments.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  iAttrib - the attribute of magics.

  Return value:

    The total resistance to the attributed magics of the player.

--*/
{
   WORD       w;
   int        i;

   w = gpGlobals->g.PlayerRoles.rgwElementalResistance[iAttrib][wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      w += gpGlobals->rgEquipmentEffect[i].rgwElementalResistance[iAttrib][wPlayerRole];
   }

   if (w > 100)
   {
      w = 100;
   }

   return w;
}

WORD
PAL_GetPlayerBattleSprite(
   WORD             wPlayerRole
)
/*++
  Purpose:

    Get player's battle sprite.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    Number of the player's battle sprite.

--*/
{
   int       i;
   WORD      w;

   w = gpGlobals->g.PlayerRoles.rgwSpriteNumInBattle[wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      if (gpGlobals->rgEquipmentEffect[i].rgwSpriteNumInBattle[wPlayerRole] != 0)
      {
         w = gpGlobals->rgEquipmentEffect[i].rgwSpriteNumInBattle[wPlayerRole];
      }
   }

   return w;
}

WORD
PAL_GetPlayerCooperativeMagic(
   WORD             wPlayerRole
)
/*++
  Purpose:

    Get player's cooperative magic.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    Object ID of the player's cooperative magic.

--*/
{
   int       i;
   WORD      w;

   w = gpGlobals->g.PlayerRoles.rgwCooperativeMagic[wPlayerRole];

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      if (gpGlobals->rgEquipmentEffect[i].rgwCooperativeMagic[wPlayerRole] != 0)
      {
         w = gpGlobals->rgEquipmentEffect[i].rgwCooperativeMagic[wPlayerRole];
      }
   }

   return w;
}

BOOL
PAL_PlayerCanAttackAll(
   WORD        wPlayerRole
)
/*++
  Purpose:

    Check if the player can attack all of the enemies in one move.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

  Return value:

    TRUE if player can attack all of the enemies in one move, FALSE if not.

--*/
{
   int       i;
   BOOL      f;

   f = FALSE;

   for (i = 0; i <= MAX_PLAYER_EQUIPMENTS; i++)
   {
      if (gpGlobals->rgEquipmentEffect[i].rgwAttackAll[wPlayerRole] != 0)
      {
         f = TRUE;
         break;
      }
   }

   return f;
}

BOOL
PAL_AddMagic(
   WORD           wPlayerRole,
   WORD           wMagic
)
/*++
  Purpose:

    Add a magic to the player.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  wMagic - the object ID of the magic.

  Return value:

    TRUE if succeeded, FALSE if failed.

--*/
{
   int            i;

   for (i = 0; i < MAX_PLAYER_MAGICS; i++)
   {
      if (gpGlobals->g.PlayerRoles.rgwMagic[i][wPlayerRole] == wMagic)
      {
         //
         // already have this magic
         //
         return FALSE;
      }
   }

   for (i = 0; i < MAX_PLAYER_MAGICS; i++)
   {
      if (gpGlobals->g.PlayerRoles.rgwMagic[i][wPlayerRole] == 0)
      {
         break;
      }
   }

   if (i >= MAX_PLAYER_MAGICS)
   {
      //
      // Not enough slots
      //
      return FALSE;
   }

   gpGlobals->g.PlayerRoles.rgwMagic[i][wPlayerRole] = wMagic;
   return TRUE;
}

VOID
PAL_RemoveMagic(
   WORD           wPlayerRole,
   WORD           wMagic
)
/*++
  Purpose:

    Remove a magic to the player.

  Parameters:

    [IN]  wPlayerRole - the player role ID.

    [IN]  wMagic - the object ID of the magic.

  Return value:

    None.

--*/
{
   int            i;

   for (i = 0; i < MAX_PLAYER_MAGICS; i++)
   {
      if (gpGlobals->g.PlayerRoles.rgwMagic[i][wPlayerRole] == wMagic)
      {
         gpGlobals->g.PlayerRoles.rgwMagic[i][wPlayerRole] = 0;
         break;
      }
   }
}

BOOL
PAL_SetPlayerStatus(
   WORD         wPlayerRole,
   WORD         wStatusID,
   WORD         wNumRound
)
/*++
  Purpose:

    Set one of the statuses for the player.

  Parameters:

    [IN]  wPlayerRole - the player ID.

    [IN]  wStatusID - the status to be set.

    [IN]  wNumRound - the effective rounds of the status.

  Return value:

    None.

--*/
{
   BOOL           fSuccess = TRUE;

#ifndef PAL_CLASSIC
   if (wStatusID == kStatusSlow &&
      gpGlobals->rgPlayerStatus[wPlayerRole][kStatusHaste] > 0)
   {
      //
      // Remove the haste status
      //
      PAL_RemovePlayerStatus(wPlayerRole, kStatusHaste);
      return;
   }

   if (wStatusID == kStatusHaste &&
      gpGlobals->rgPlayerStatus[wPlayerRole][kStatusSlow] > 0)
   {
      //
      // Remove the slow status
      //
      PAL_RemovePlayerStatus(wPlayerRole, kStatusSlow);
      return;
   }
#endif

   switch (wStatusID)
   {
   case kStatusConfused:
   case kStatusSleep:
   case kStatusSilence:
#ifdef PAL_CLASSIC
   case kStatusParalyzed:
#else
   case kStatusSlow:
#endif
      //
      // for "bad" statuses, don't set the status when we already have it
      //
      if (gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] == 0)
      {
         gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] = wNumRound;
      }
      break;

   case kStatusPuppet:
      //
      // only allow dead players for "puppet" status
      //
      if (gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] == 0)
      {
         if (gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] < wNumRound)
         {
            gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] = wNumRound;
         }
      }
      else
      {
         fSuccess = FALSE;
      }
      break;

   case kStatusBravery:
   case kStatusProtect:
   case kStatusDualAttack:
   case kStatusHaste:
      //
      // for "good" statuses, reset the status if the status to be set lasts longer
      //
      if (gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] != 0 &&
         gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] < wNumRound)
      {
         gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] = wNumRound;
      }
      break;

   default:
      assert(FALSE);
      break;
   }

   return fSuccess;
}

VOID
PAL_RemovePlayerStatus(
   WORD         wPlayerRole,
   WORD         wStatusID
)
/*++
  Purpose:

    Remove one of the status for player.

  Parameters:

    [IN]  wPlayerRole - the player ID.

    [IN]  wStatusID - the status to be set.

  Return value:

    None.

--*/
{
   //
   // Don't remove effects of equipments
   //
   if (gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] <= 999)
   {
      gpGlobals->rgPlayerStatus[wPlayerRole][wStatusID] = 0;
   }
}

VOID
PAL_ClearAllPlayerStatus(
   VOID
)
/*++
  Purpose:

    Clear all player status.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int      i, j;

   for (i = 0; i < MAX_PLAYER_ROLES; i++)
   {
      for (j = 0; j < kStatusAll; j++)
      {
         //
         // Don't remove effects of equipments
         //
         if (gpGlobals->rgPlayerStatus[i][j] <= 999)
         {
            gpGlobals->rgPlayerStatus[i][j] = 0;
         }
      }
   }
}

VOID
PAL_PlayerLevelUp(
   WORD          wPlayerRole,
   WORD          wNumLevel
)
/*++
  Purpose:

    Increase the player's level by wLevels.

  Parameters:

    [IN]  wPlayerRole - player role ID.

    [IN]  wNumLevel - number of levels to be increased.

  Return value:

    None.

--*/
{
   WORD          i;

   //
   // Add the level
   //
   gpGlobals->g.PlayerRoles.rgwLevel[wPlayerRole] += wNumLevel;
   if (gpGlobals->g.PlayerRoles.rgwLevel[wPlayerRole] > MAX_LEVELS)
   {
      gpGlobals->g.PlayerRoles.rgwLevel[wPlayerRole] = MAX_LEVELS;
   }

   for (i = 0; i < wNumLevel; i++)
   {
      //
      // Increase player's stats
      //
      gpGlobals->g.PlayerRoles.rgwMaxHP[wPlayerRole] += 10 + RandomLong(0, 7);
      gpGlobals->g.PlayerRoles.rgwMaxMP[wPlayerRole] += 8 + RandomLong(0, 5);
      gpGlobals->g.PlayerRoles.rgwAttackStrength[wPlayerRole] += 4 + RandomLong(0, 1);
      gpGlobals->g.PlayerRoles.rgwMagicStrength[wPlayerRole] += 4 + RandomLong(0, 1);
      gpGlobals->g.PlayerRoles.rgwDefense[wPlayerRole] += 2 + RandomLong(0, 1);
      gpGlobals->g.PlayerRoles.rgwDexterity[wPlayerRole] += 2 + RandomLong(0, 1);
      gpGlobals->g.PlayerRoles.rgwFleeRate[wPlayerRole] += 2;
   }

#define STAT_LIMIT(t) { if ((t) > 999) (t) = 999; }
   STAT_LIMIT(gpGlobals->g.PlayerRoles.rgwMaxHP[wPlayerRole]);
   STAT_LIMIT(gpGlobals->g.PlayerRoles.rgwMaxMP[wPlayerRole]);
   STAT_LIMIT(gpGlobals->g.PlayerRoles.rgwAttackStrength[wPlayerRole]);
   STAT_LIMIT(gpGlobals->g.PlayerRoles.rgwMagicStrength[wPlayerRole]);
   STAT_LIMIT(gpGlobals->g.PlayerRoles.rgwDefense[wPlayerRole]);
   STAT_LIMIT(gpGlobals->g.PlayerRoles.rgwDexterity[wPlayerRole]);
   STAT_LIMIT(gpGlobals->g.PlayerRoles.rgwFleeRate[wPlayerRole]);
#undef STAT_LIMIT

   //
   // Reset experience points to zero
   //
   gpGlobals->Exp.rgPrimaryExp[wPlayerRole].wExp = 0;
   gpGlobals->Exp.rgPrimaryExp[wPlayerRole].wLevel =
      gpGlobals->g.PlayerRoles.rgwLevel[wPlayerRole];
}
