/* Standard PAL .rpg streaming support for the MEM_LEVEL1 event pager. */

#if SDL_BYTEORDER != SDL_LIL_ENDIAN
#error MEM_LEVEL1 standard save streaming requires a little-endian target
#endif

#define PAL_PAGED_SAVE_DOS_BYTES offsetof(SAVEDGAME_DOS, rgEventObject)
#define PAL_PAGED_SAVE_WIN_BYTES offsetof(SAVEDGAME_WIN, rgEventObject)

static BOOL
PAL_PagedSaveReadEvents(
   FILE *fp
)
{
   return fp != NULL && PalEngineEventState_ReplaceFromFile(fp);
}

static BOOL
PAL_PagedSaveWriteFile(
   int                slot,
   LPSAVEDGAME_COMMON fixed,
   size_t             fixed_bytes
)
{
   uint8_t *staging = (uint8_t *)fixed;
   size_t staged = 0u;
   UINT32 event_id;
   FILE *fp;
   BOOL result = FALSE;

   if (fixed == NULL || fixed_bytes < PAL_EVENT_PAGER_PAGE_BYTES ||
      gpGlobals->g.nEventObject <= 0 ||
      gpGlobals->g.nEventObject !=
         (int)PalEngineEventState_GetRecordCount() ||
      !PalEngineEventState_IsReady())
   {
      return FALSE;
   }
   fp = UTIL_OpenFileAtPathForMode(
      gConfig.pszSavePath, PAL_va(1, "%d.rpg", slot), "wb");
   if (fp == NULL)
   {
      return FALSE;
   }
   if (fwrite(fixed, 1, fixed_bytes, fp) != fixed_bytes)
   {
      goto done;
   }

   /*
    * Read through the live pager rather than flushing it to EVENT.WRK.
    * Dirty resident records therefore enter the save directly.  Sequential
    * reads may cause normal LRU eviction, which is the only path allowed to
    * write a dirty page to the session work file.
    */
   for (event_id = 1u;
        event_id <= (UINT32)gpGlobals->g.nEventObject;
        event_id++)
   {
      if (!PAL_EventObjectRead(
            (WORD)event_id,
            (LPEVENTOBJECT)(void *)(staging + staged)))
      {
         goto done;
      }
      staged += sizeof(EVENTOBJECT);
      if (staged == PAL_EVENT_PAGER_PAGE_BYTES ||
         event_id == (UINT32)gpGlobals->g.nEventObject)
      {
         if (fwrite(staging, 1, staged, fp) != staged)
         {
            goto done;
         }
         staged = 0u;
      }
   }
   result = TRUE;

done:
   if (fclose(fp) != 0)
   {
      result = FALSE;
   }
   return result;
}
