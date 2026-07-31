/*
 * Full-state TF save support for PAL_CARDPUTER_EXTREME.
 *
 * This file is included by global.c after the SAVEDGAME_* layouts have been
 * declared.  Keeping the implementation beside the event-state backend makes
 * the storage contract easier to audit while preserving the original static
 * save helpers in global.c.
 */

#if SDL_BYTEORDER != SDL_LIL_ENDIAN
#error PAL_CARDPUTER_EXTREME save serialization requires a little-endian target
#endif

#ifndef PAL_EXTREME_ALLOW_LEGACY_DOS_IMPORT
#define PAL_EXTREME_ALLOW_LEGACY_DOS_IMPORT 1
#endif

#define PAL_EXTREME_SAVE_MAGIC                 "PALXSAVE"
#define PAL_EXTREME_SAVE_VERSION               2u
#define PAL_EXTREME_SAVE_LEGACY_VERSION        1u
#define PAL_EXTREME_SAVE_HEADER_BYTES         64u
#define PAL_EXTREME_SAVE_LEGACY_HEADER_BYTES  48u
#define PAL_EXTREME_SAVE_FORMAT_DOS            1u
#define PAL_EXTREME_SAVE_FORMAT_WIN95          2u
#define PAL_EXTREME_SAVE_PATH_BYTES           32u
#define PAL_EXTREME_SAVE_EVENT_RECORDS \
   PAL_ENGINE_EVENT_STATE_RECORD_COUNT
#define PAL_EXTREME_SAVE_EVENT_BYTES \
   (PAL_EXTREME_SAVE_EVENT_RECORDS * sizeof(EVENTOBJECT))

/* Old "SZC2" saves stored IDs 1..423 followed by event 5334. */
#define PAL_EXTREME_LEGACY_PROFILE_ID          0x32435a53u
#define PAL_EXTREME_LEGACY_EVENT_RECORDS       424u
#define PAL_EXTREME_LEGACY_PREFIX_RECORDS      423u
#define PAL_EXTREME_LEGACY_SPARSE_EVENT_ID     5334u

#define PAL_EXTREME_HEADER_VERSION_OFFSET       8u
#define PAL_EXTREME_HEADER_SIZE_OFFSET         10u
#define PAL_EXTREME_HEADER_PROFILE_OFFSET      12u
#define PAL_EXTREME_HEADER_FORMAT_OFFSET       16u
#define PAL_EXTREME_HEADER_FLAGS_OFFSET        18u
#define PAL_EXTREME_HEADER_FIXED_BYTES_OFFSET  20u
#define PAL_EXTREME_HEADER_EVENT_COUNT_OFFSET  24u
#define PAL_EXTREME_HEADER_EVENT_BYTES_OFFSET  28u
#define PAL_EXTREME_HEADER_TOTAL_BYTES_OFFSET  32u
#define PAL_EXTREME_HEADER_PAYLOAD_CRC_OFFSET  36u
#define PAL_EXTREME_HEADER_SCENE_OFFSET        40u
#define PAL_EXTREME_HEADER_PARTY_OFFSET        42u
#define PAL_EXTREME_HEADER_TEMPLATE_CRC_OFFSET 44u
#define PAL_EXTREME_HEADER_GENERATION_OFFSET   48u
#define PAL_EXTREME_HEADER_FIXED_CRC_OFFSET    52u
#define PAL_EXTREME_HEADER_EVENT_CRC_OFFSET    56u
#define PAL_EXTREME_HEADER_CRC_OFFSET          60u

#define PAL_EXTREME_LEGACY_HEADER_CRC_OFFSET   44u

typedef enum tagPAL_EXTREME_SAVE_KIND
{
   kPalExtremeSaveKindInvalid = 0,
   kPalExtremeSaveKindCurrent,
   kPalExtremeSaveKindTaggedLegacy,
   kPalExtremeSaveKindDosLegacy
} PAL_EXTREME_SAVE_KIND;

typedef struct tagPAL_EXTREME_SAVE_META
{
   PAL_EXTREME_SAVE_KIND kind;
   UINT32 profile_id;
   UINT32 template_crc32;
   UINT32 generation;
   UINT32 payload_crc;
   UINT32 fixed_crc;
   UINT32 event_crc;
   UINT32 source_event_count;
   UINT32 header_bytes;
   WORD   scene;
   WORD   party_index;
} PAL_EXTREME_SAVE_META;

typedef enum tagPAL_EXTREME_SAVE_QUALITY
{
   kPalExtremeSaveInvalid = 0,
   kPalExtremeSaveLegacy,
   kPalExtremeSaveTagged
} PAL_EXTREME_SAVE_QUALITY;

typedef struct tagPAL_EXTREME_FILE_PAGE_SOURCE
{
   FILE        *fp;
   UINT32       event_offset;
   UINT32       source_event_count;
   const SCENE *scenes;
} PAL_EXTREME_FILE_PAGE_SOURCE;

static WORD
PAL_ExtremeReadLE16(
   const uint8_t *p
)
{
   return (WORD)((WORD)p[0] | ((WORD)p[1] << 8));
}

static UINT32
PAL_ExtremeReadLE32(
   const uint8_t *p
)
{
   return (UINT32)p[0] |
      ((UINT32)p[1] << 8) |
      ((UINT32)p[2] << 16) |
      ((UINT32)p[3] << 24);
}

static VOID
PAL_ExtremeWriteLE16(
   uint8_t *p,
   WORD value
)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
}

static VOID
PAL_ExtremeWriteLE32(
   uint8_t *p,
   UINT32 value
)
{
   p[0] = (uint8_t)value;
   p[1] = (uint8_t)(value >> 8);
   p[2] = (uint8_t)(value >> 16);
   p[3] = (uint8_t)(value >> 24);
}

static UINT32
PAL_ExtremeCRC32Update(
   UINT32         crc,
   const uint8_t *bytes,
   size_t         size
)
{
   size_t i;

   for (i = 0; i < size; i++)
   {
      UINT32 value = crc ^ bytes[i];
      int bit;

      for (bit = 0; bit < 8; bit++)
      {
         value = (value >> 1) ^
            (0xedb88320u & (UINT32)-(int32_t)(value & 1u));
      }
      crc = value;
   }
   return crc;
}

static BOOL
PAL_ExtremeSaveSceneTableValid(
   LPSAVEDGAME_COMMON s
)
{
   UINT32 i;

   if (s == NULL || s->wNumScene == 0 || s->wNumScene >= MAX_SCENES)
   {
      return FALSE;
   }
   for (i = 0; i < MAX_SCENES; i++)
   {
      if (s->rgScene[i].wEventObjectIndex >
            PAL_EXTREME_SAVE_EVENT_RECORDS ||
         (i != 0 &&
          s->rgScene[i].wEventObjectIndex <
            s->rgScene[i - 1].wEventObjectIndex))
      {
         return FALSE;
      }
   }
   for (i = 1; i < MAX_SCENES; i++)
   {
      UINT32 start = s->rgScene[i - 1].wEventObjectIndex;
      UINT32 end = s->rgScene[i].wEventObjectIndex;

      if (end - start > PAL_EXTREME_SCENE_EVENT_OBJECT_CAPACITY ||
         (end != start &&
          (end - 1u) / PAL_EVENT_PAGER_RECORDS_PER_PAGE -
             start / PAL_EVENT_PAGER_RECORDS_PER_PAGE >= 2u))
      {
         return FALSE;
      }
   }
   return TRUE;
}

static BOOL
PAL_ExtremeSaveStateValid(
   LPSAVEDGAME_COMMON           s,
   const PAL_EXTREME_SAVE_META *meta
)
{
   UINT32 i;
   UINT32 active_party;
   WORD map_num;

   if (s == NULL || meta == NULL ||
      !PAL_ExtremeSaveSceneTableValid(s) ||
      s->nPartyMember >= MAX_PLAYERS_IN_PARTY)
   {
      return FALSE;
   }
   active_party = (UINT32)s->nPartyMember + 1u;
   if ((UINT32)s->nFollower > MAX_PLAYABLE_PLAYER_ROLES - active_party ||
      s->wPartyDirection >= kDirUnknown)
   {
      return FALSE;
   }
   for (i = 0; i < active_party + s->nFollower; i++)
   {
      if (s->rgParty[i].wPlayerRole >= MAX_PLAYER_ROLES)
      {
         return FALSE;
      }
   }
   if (meta->kind != kPalExtremeSaveKindDosLegacy &&
      (meta->scene != s->wNumScene ||
       meta->party_index != s->nPartyMember))
   {
      return FALSE;
   }
   map_num = s->rgScene[s->wNumScene - 1u].wMapNum;
   if (!PalEngineBridge_HasNativeChunk(
          PAL_PACK_ARCHIVE_MAP, map_num) ||
      !PalEngineBridge_HasNativeChunk(
          PAL_PACK_ARCHIVE_GOP, map_num))
   {
      return FALSE;
   }
#if defined(PAL_EXTREME_RIX_MUSIC)
   if ((s->wNumMusic != 0u &&
        !PalEngineBridge_HasNativeChunk(
           PAL_PACK_ARCHIVE_MUS, s->wNumMusic)) ||
      (s->wNumBattleMusic != 0u &&
        !PalEngineBridge_HasNativeChunk(
           PAL_PACK_ARCHIVE_MUS, s->wNumBattleMusic)))
   {
      return FALSE;
   }
#endif
   return TRUE;
}

static BOOL
PAL_ExtremeEventStorageReady(
   VOID
)
{
   return gpGlobals->g.nEventObject ==
         PAL_EXTREME_SAVE_EVENT_RECORDS &&
      PalEngineEventState_IsReady();
}

static BOOL
PAL_ExtremeSaveBuildPath(
   int         slot,
   const char *extension,
   char       *path,
   size_t      path_bytes
)
{
   char name[13];
   size_t base_bytes;
   BOOL append_separator;
   int name_bytes;
   int result;

   if (slot <= 0 || slot > 99999999 || extension == NULL ||
      strlen(extension) != 3 || path == NULL || path_bytes == 0 ||
      gConfig.pszSavePath == NULL)
   {
      return FALSE;
   }
   name_bytes = snprintf(name, sizeof(name), "%d.%s", slot, extension);
   if (name_bytes <= 0 || (size_t)name_bytes >= sizeof(name))
   {
      return FALSE;
   }
   base_bytes = strlen(gConfig.pszSavePath);
   append_separator = base_bytes > 0 &&
      !PAL_IS_PATH_SEPARATOR(gConfig.pszSavePath[base_bytes - 1]);
   if (append_separator)
   {
      result = snprintf(path, path_bytes, "%s%c%s", gConfig.pszSavePath,
         PAL_PATH_SEPARATORS[0], name);
   }
   else
   {
      result = snprintf(path, path_bytes, "%s%s",
         gConfig.pszSavePath, name);
   }
   return result > 0 && (size_t)result < path_bytes;
}

static BOOL
PAL_ExtremeSaveFileExists(
   const char *path
)
{
   FILE *fp = fopen(path, "rb");

   if (fp == NULL)
   {
      return FALSE;
   }
   return fclose(fp) == 0;
}

static BOOL
PAL_ExtremeSaveAtomicReplace(
   const char *temporary_path,
   const char *final_path,
   const char *backup_path,
   PAL_EXTREME_SAVE_QUALITY final_quality
)
{
   BOOL moved_final = FALSE;
   BOOL backup_exists = PAL_ExtremeSaveFileExists(backup_path);

   if (PAL_ExtremeSaveFileExists(final_path))
   {
      if (final_quality == kPalExtremeSaveInvalid ||
         (final_quality == kPalExtremeSaveLegacy && backup_exists))
      {
         if (!PalTarget_SaveUnlink(final_path, false))
         {
            return FALSE;
         }
      }
      else
      {
         if (final_quality == kPalExtremeSaveTagged &&
            !PalTarget_SaveUnlink(backup_path, true))
         {
            return FALSE;
         }
         if (!PalTarget_SaveRename(final_path, backup_path))
         {
            return FALSE;
         }
         moved_final = TRUE;
      }
   }
   if (!PalTarget_SaveRename(temporary_path, final_path))
   {
      if (moved_final)
      {
         (void)PalTarget_SaveRename(backup_path, final_path);
      }
      return FALSE;
   }
   return TRUE;
}

static BOOL
PAL_ExtremeSaveScanPayload(
   FILE                  *fp,
   uint8_t               *scratch,
   size_t                 scratch_bytes,
   PAL_EXTREME_SAVE_META *meta,
   UINT32                 fixed_bytes,
   UINT32                 event_bytes
)
{
   UINT32 payload_crc = 0xffffffffu;
   UINT32 fixed_crc = 0xffffffffu;
   UINT32 event_crc = 0xffffffffu;
   UINT32 remaining = fixed_bytes;

   while (remaining != 0u)
   {
      size_t amount = remaining < scratch_bytes ? remaining : scratch_bytes;

      if (fread(scratch, 1, amount, fp) != amount)
      {
         return FALSE;
      }
      fixed_crc = PAL_ExtremeCRC32Update(fixed_crc, scratch, amount);
      payload_crc = PAL_ExtremeCRC32Update(payload_crc, scratch, amount);
      remaining -= (UINT32)amount;
   }
   remaining = event_bytes;
   while (remaining != 0u)
   {
      size_t amount = remaining < scratch_bytes ? remaining : scratch_bytes;

      if (fread(scratch, 1, amount, fp) != amount)
      {
         return FALSE;
      }
      event_crc = PAL_ExtremeCRC32Update(event_crc, scratch, amount);
      payload_crc = PAL_ExtremeCRC32Update(payload_crc, scratch, amount);
      remaining -= (UINT32)amount;
   }
   fixed_crc ^= 0xffffffffu;
   event_crc ^= 0xffffffffu;
   payload_crc ^= 0xffffffffu;
   if (payload_crc != meta->payload_crc ||
      (meta->kind == kPalExtremeSaveKindCurrent &&
       (fixed_crc != meta->fixed_crc ||
        event_crc != meta->event_crc)))
   {
      return FALSE;
   }
   meta->fixed_crc = fixed_crc;
   meta->event_crc = event_crc;
   return TRUE;
}

static BOOL
PAL_ExtremeSaveReadHeader(
   FILE                  *fp,
   size_t                 expected_fixed_bytes,
   WORD                   expected_format,
   uint8_t               *scratch,
   size_t                 scratch_bytes,
   PAL_EXTREME_SAVE_META *meta,
   BOOL                    verify_event_payload
)
{
   uint8_t header[PAL_EXTREME_SAVE_HEADER_BYTES];
   UINT32 fixed_bytes;
   UINT32 event_count;
   UINT32 event_bytes;
   UINT32 total_bytes;
   UINT32 expected_total;
   uint32_t expected_profile;
   uint32_t expected_template;
   uint32_t ignored_generation;
   UINT32 header_crc;
   WORD version;
   WORD header_bytes;
   long file_bytes;

   if (fp == NULL || scratch == NULL || scratch_bytes == 0 ||
      meta == NULL || expected_fixed_bytes > UINT32_MAX ||
      fseek(fp, 0, SEEK_SET) != 0)
   {
      return FALSE;
   }
   memset(header, 0, sizeof(header));
   if (fread(header, 1, sizeof(header), fp) != sizeof(header) ||
      memcmp(header, PAL_EXTREME_SAVE_MAGIC, 8) != 0)
   {
      return FALSE;
   }
   version = PAL_ExtremeReadLE16(
      header + PAL_EXTREME_HEADER_VERSION_OFFSET);
   header_bytes = PAL_ExtremeReadLE16(
      header + PAL_EXTREME_HEADER_SIZE_OFFSET);
   fixed_bytes = PAL_ExtremeReadLE32(
      header + PAL_EXTREME_HEADER_FIXED_BYTES_OFFSET);
   event_count = PAL_ExtremeReadLE32(
      header + PAL_EXTREME_HEADER_EVENT_COUNT_OFFSET);
   event_bytes = PAL_ExtremeReadLE32(
      header + PAL_EXTREME_HEADER_EVENT_BYTES_OFFSET);
   total_bytes = PAL_ExtremeReadLE32(
      header + PAL_EXTREME_HEADER_TOTAL_BYTES_OFFSET);
   if (fixed_bytes > UINT32_MAX - header_bytes ||
      event_bytes > UINT32_MAX - header_bytes - fixed_bytes)
   {
      return FALSE;
   }
   expected_total = header_bytes + fixed_bytes + event_bytes;
   file_bytes = flength(fp);
   if (file_bytes < 0 || (UINT32)file_bytes != total_bytes ||
      expected_total != total_bytes ||
      fixed_bytes != expected_fixed_bytes ||
      PAL_ExtremeReadLE16(
         header + PAL_EXTREME_HEADER_FORMAT_OFFSET) != expected_format ||
      PAL_ExtremeReadLE16(
         header + PAL_EXTREME_HEADER_FLAGS_OFFSET) != 0u)
   {
      return FALSE;
   }

   memset(meta, 0, sizeof(*meta));
   meta->profile_id = PAL_ExtremeReadLE32(
      header + PAL_EXTREME_HEADER_PROFILE_OFFSET);
   meta->payload_crc = PAL_ExtremeReadLE32(
      header + PAL_EXTREME_HEADER_PAYLOAD_CRC_OFFSET);
   meta->scene = PAL_ExtremeReadLE16(
      header + PAL_EXTREME_HEADER_SCENE_OFFSET);
   meta->party_index = PAL_ExtremeReadLE16(
      header + PAL_EXTREME_HEADER_PARTY_OFFSET);
   meta->source_event_count = event_count;
   meta->header_bytes = header_bytes;

   if (version == PAL_EXTREME_SAVE_VERSION)
   {
      if (header_bytes != PAL_EXTREME_SAVE_HEADER_BYTES ||
         event_count != PAL_EXTREME_SAVE_EVENT_RECORDS ||
         event_bytes != PAL_EXTREME_SAVE_EVENT_BYTES ||
         meta->profile_id == 0u ||
         !PalEngineEventState_GetIdentity(
            &expected_profile,
            &expected_template,
            &ignored_generation))
      {
         return FALSE;
      }
      meta->kind = kPalExtremeSaveKindCurrent;
      meta->template_crc32 = PAL_ExtremeReadLE32(
         header + PAL_EXTREME_HEADER_TEMPLATE_CRC_OFFSET);
      meta->generation = PAL_ExtremeReadLE32(
         header + PAL_EXTREME_HEADER_GENERATION_OFFSET);
      meta->fixed_crc = PAL_ExtremeReadLE32(
         header + PAL_EXTREME_HEADER_FIXED_CRC_OFFSET);
      meta->event_crc = PAL_ExtremeReadLE32(
         header + PAL_EXTREME_HEADER_EVENT_CRC_OFFSET);
      header_crc = PAL_ExtremeCRC32Update(
         0xffffffffu, header, PAL_EXTREME_HEADER_CRC_OFFSET) ^
         0xffffffffu;
      if (meta->template_crc32 != expected_template ||
         PAL_ExtremeReadLE32(
            header + PAL_EXTREME_HEADER_CRC_OFFSET) != header_crc)
      {
         return FALSE;
      }
   }
   else if (version == PAL_EXTREME_SAVE_LEGACY_VERSION)
   {
      header_crc = PAL_ExtremeCRC32Update(
         0xffffffffu, header, PAL_EXTREME_LEGACY_HEADER_CRC_OFFSET) ^
         0xffffffffu;
      if (header_bytes != PAL_EXTREME_SAVE_LEGACY_HEADER_BYTES ||
         meta->profile_id != PAL_EXTREME_LEGACY_PROFILE_ID ||
         event_count != PAL_EXTREME_LEGACY_EVENT_RECORDS ||
         event_bytes !=
            PAL_EXTREME_LEGACY_EVENT_RECORDS * sizeof(EVENTOBJECT) ||
         PAL_ExtremeReadLE32(
            header + PAL_EXTREME_LEGACY_HEADER_CRC_OFFSET) != header_crc)
      {
         return FALSE;
      }
      meta->kind = kPalExtremeSaveKindTaggedLegacy;
   }
   else
   {
      return FALSE;
   }

   if (fseek(fp, (long)header_bytes, SEEK_SET) != 0)
   {
      return FALSE;
   }
   if (!verify_event_payload &&
      meta->kind == kPalExtremeSaveKindCurrent)
   {
      UINT32 fixed_crc = 0xffffffffu;
      UINT32 remaining = fixed_bytes;

      while (remaining != 0u)
      {
         size_t amount =
            remaining < scratch_bytes ? remaining : scratch_bytes;

         if (fread(scratch, 1, amount, fp) != amount)
         {
            return FALSE;
         }
         fixed_crc = PAL_ExtremeCRC32Update(
            fixed_crc, scratch, amount);
         remaining -= (UINT32)amount;
      }
      return (fixed_crc ^ 0xffffffffu) == meta->fixed_crc;
   }
   if (!PAL_ExtremeSaveScanPayload(
         fp, scratch, scratch_bytes, meta, fixed_bytes, event_bytes))
   {
      return FALSE;
   }
   return TRUE;
}

static BOOL
PAL_ExtremeSaveReadFixed(
   FILE              *fp,
   LPSAVEDGAME_COMMON s,
   size_t             fixed_bytes,
   UINT32             offset
)
{
   return fp != NULL && s != NULL && offset <= (UINT32)LONG_MAX &&
      fseek(fp, (long)offset, SEEK_SET) == 0 &&
      fread(s, 1, fixed_bytes, fp) == fixed_bytes;
}

static BOOL
PAL_ExtremeSaveLegacyDosMeta(
   FILE                  *fp,
   size_t                 fixed_bytes,
   PAL_EXTREME_SAVE_META *meta
)
{
#if PAL_EXTREME_ALLOW_LEGACY_DOS_IMPORT
   long file_bytes;
   size_t payload_bytes;
   size_t source_count;

   if (fp == NULL || meta == NULL ||
      fixed_bytes != PAL_EXTREME_SAVE_DOS_BYTES ||
      (file_bytes = flength(fp)) < 0 ||
      (size_t)file_bytes < fixed_bytes)
   {
      return FALSE;
   }
   payload_bytes = (size_t)file_bytes - fixed_bytes;
   if (payload_bytes % sizeof(EVENTOBJECT) != 0)
   {
      return FALSE;
   }
   source_count = payload_bytes / sizeof(EVENTOBJECT);
   /*
    * An untagged DOS save has no resource identity.  Import only the exact
    * 5,369-record shape used by EVENT.DEF; accepting a larger table and
    * silently truncating its tail could bind a save from another data set to
    * the active scene table.
    */
   if (source_count != PAL_EXTREME_SAVE_EVENT_RECORDS)
   {
      return FALSE;
   }
   memset(meta, 0, sizeof(*meta));
   meta->kind = kPalExtremeSaveKindDosLegacy;
   meta->source_event_count = (UINT32)source_count;
   return TRUE;
#else
   (void)fp;
   (void)fixed_bytes;
   (void)meta;
   return FALSE;
#endif
}

static bool
PAL_ExtremeSaveFilePageSource(
   void     *user,
   uint16_t  logical_page,
   uint8_t   page[PAL_EVENT_PAGER_PAGE_BYTES]
)
{
   PAL_EXTREME_FILE_PAGE_SOURCE *source =
      (PAL_EXTREME_FILE_PAGE_SOURCE *)user;
   UINT32 record_offset;
   UINT32 records;
   UINT32 bytes;

   if (source == NULL || source->fp == NULL || page == NULL ||
      source->scenes == NULL ||
      source->source_event_count != PAL_EXTREME_SAVE_EVENT_RECORDS ||
      logical_page >= PAL_EVENT_JOURNAL_PAGE_COUNT)
   {
      return false;
   }
   memset(page, 0, PAL_EVENT_PAGER_PAGE_BYTES);
   if (logical_page == PAL_EVENT_JOURNAL_SCENE_PAGE)
   {
      memcpy(page, source->scenes, PAL_ENGINE_EVENT_STATE_SCENE_BYTES);
      return true;
   }
   record_offset =
      (UINT32)logical_page * PAL_EVENT_PAGER_RECORDS_PER_PAGE;
   records = PAL_EXTREME_SAVE_EVENT_RECORDS - record_offset;
   if (records > PAL_EVENT_PAGER_RECORDS_PER_PAGE)
   {
      records = PAL_EVENT_PAGER_RECORDS_PER_PAGE;
   }
   bytes = records * sizeof(EVENTOBJECT);
   return source->event_offset <= UINT32_MAX -
         record_offset * sizeof(EVENTOBJECT) &&
      source->event_offset +
         record_offset * sizeof(EVENTOBJECT) <= (UINT32)LONG_MAX &&
      fseek(source->fp,
         (long)(source->event_offset +
            record_offset * sizeof(EVENTOBJECT)),
         SEEK_SET) == 0 &&
      fread(page, 1, bytes, source->fp) == bytes;
}

static BOOL
PAL_ExtremeSaveReplaceFromFile(
   FILE                    *fp,
   UINT32                   event_offset,
   UINT32                   source_event_count,
   LPSAVEDGAME_COMMON       s
)
{
   PAL_EXTREME_FILE_PAGE_SOURCE source;

   source.fp = fp;
   source.event_offset = event_offset;
   source.source_event_count = source_event_count;
   source.scenes = s->rgScene;
   return PalEngineEventState_ReplaceAll(
      PAL_ExtremeSaveFilePageSource,
      &source,
      gpGlobals->g.rgScene,
      sizeof(gpGlobals->g.rgScene));
}

typedef struct tagPAL_EXTREME_LEGACY_TRANSFORM
{
   FILE        *fp;
   UINT32       event_offset;
   const SCENE *scenes;
} PAL_EXTREME_LEGACY_TRANSFORM;

static bool
PAL_ExtremeSaveLegacyTransformPage(
   void     *user,
   uint16_t  logical_page,
   uint8_t   page[PAL_EVENT_PAGER_PAGE_BYTES]
)
{
   PAL_EXTREME_LEGACY_TRANSFORM *source =
      (PAL_EXTREME_LEGACY_TRANSFORM *)user;
   UINT32 first_record;
   UINT32 records;
   UINT32 bytes;

   if (source == NULL || source->fp == NULL || source->scenes == NULL ||
      page == NULL || logical_page >= PAL_EVENT_JOURNAL_PAGE_COUNT)
   {
      return false;
   }
   if (logical_page == PAL_EVENT_JOURNAL_SCENE_PAGE)
   {
      memcpy(page, source->scenes, PAL_ENGINE_EVENT_STATE_SCENE_BYTES);
      return true;
   }

   first_record =
      (UINT32)logical_page * PAL_EVENT_PAGER_RECORDS_PER_PAGE;
   if (first_record < PAL_EXTREME_LEGACY_PREFIX_RECORDS)
   {
      records = PAL_EXTREME_LEGACY_PREFIX_RECORDS - first_record;
      if (records > PAL_EVENT_PAGER_RECORDS_PER_PAGE)
      {
         records = PAL_EVENT_PAGER_RECORDS_PER_PAGE;
      }
      bytes = records * sizeof(EVENTOBJECT);
      if (source->event_offset > UINT32_MAX -
            first_record * sizeof(EVENTOBJECT) ||
         source->event_offset +
            first_record * sizeof(EVENTOBJECT) > (UINT32)LONG_MAX ||
         fseek(source->fp,
            (long)(source->event_offset +
               first_record * sizeof(EVENTOBJECT)),
            SEEK_SET) != 0 ||
         fread(page, 1, bytes, source->fp) != bytes)
      {
         return false;
      }
   }

   if (logical_page ==
         (PAL_EXTREME_LEGACY_SPARSE_EVENT_ID - 1u) /
            PAL_EVENT_PAGER_RECORDS_PER_PAGE)
   {
      UINT32 sparse_offset =
         source->event_offset +
         PAL_EXTREME_LEGACY_PREFIX_RECORDS * sizeof(EVENTOBJECT);
      UINT32 page_offset =
         ((PAL_EXTREME_LEGACY_SPARSE_EVENT_ID - 1u) %
            PAL_EVENT_PAGER_RECORDS_PER_PAGE) * sizeof(EVENTOBJECT);

      if (sparse_offset > (UINT32)LONG_MAX ||
         fseek(source->fp, (long)sparse_offset, SEEK_SET) != 0 ||
         fread(page + page_offset, 1, sizeof(EVENTOBJECT), source->fp) !=
            sizeof(EVENTOBJECT))
      {
         return false;
      }
   }
   return true;
}

static BOOL
PAL_ExtremeSaveInstallLegacyTagged(
   FILE              *fp,
   LPSAVEDGAME_COMMON s,
   size_t             fixed_bytes
)
{
   PAL_EXTREME_LEGACY_TRANSFORM source;

   if (!PAL_ExtremeEventStorageReady() || fp == NULL || s == NULL ||
      fixed_bytes > UINT32_MAX -
         PAL_EXTREME_SAVE_LEGACY_HEADER_BYTES)
   {
      return FALSE;
   }
   source.fp = fp;
   source.event_offset =
      PAL_EXTREME_SAVE_LEGACY_HEADER_BYTES + (UINT32)fixed_bytes;
   source.scenes = s->rgScene;
   if (!PalEngineEventState_TransformDefaults(
         PAL_ExtremeSaveLegacyTransformPage,
         &source,
         gpGlobals->g.rgScene,
         sizeof(gpGlobals->g.rgScene)))
   {
      return FALSE;
   }
   UTIL_LogOutput(LOGLEVEL_INFO,
      "Atomically migrated SZC2 save: events 1..423 plus 5334 "
      "over full TF defaults\n");
   return TRUE;
}

static BOOL
PAL_ExtremeSaveReadFile(
   FILE              *fp,
   LPSAVEDGAME_COMMON s,
   size_t             fixed_bytes,
   WORD               expected_format
)
{
   uint8_t magic[8];
   PAL_EXTREME_SAVE_META meta;
   BOOL tagged;

   if (fp == NULL || s == NULL ||
      fread(magic, 1, sizeof(magic), fp) != sizeof(magic) ||
      fseek(fp, 0, SEEK_SET) != 0)
   {
      return FALSE;
   }
   tagged = memcmp(magic, PAL_EXTREME_SAVE_MAGIC, sizeof(magic)) == 0;
   if (tagged)
   {
      if (!PAL_ExtremeSaveReadHeader(
            fp, fixed_bytes, expected_format,
            PAL_SAVEGAME_STATIC, fixed_bytes, &meta, TRUE) ||
         !PAL_ExtremeSaveReadFixed(
            fp, s, fixed_bytes, meta.header_bytes))
      {
         return FALSE;
      }
   }
   else
   {
      if (expected_format != PAL_EXTREME_SAVE_FORMAT_DOS ||
         !PAL_ExtremeSaveLegacyDosMeta(fp, fixed_bytes, &meta) ||
         !PAL_ExtremeSaveReadFixed(fp, s, fixed_bytes, 0u))
      {
         return FALSE;
      }
   }

   DO_BYTESWAP(s, fixed_bytes);
   if (!PAL_ExtremeSaveStateValid(s, &meta))
   {
      return FALSE;
   }

   if (meta.kind == kPalExtremeSaveKindTaggedLegacy)
   {
      return PAL_ExtremeSaveInstallLegacyTagged(fp, s, fixed_bytes);
   }
   if (!PAL_ExtremeSaveReplaceFromFile(
         fp,
         meta.kind == kPalExtremeSaveKindCurrent
            ? meta.header_bytes + (UINT32)fixed_bytes
            : (UINT32)fixed_bytes,
         meta.source_event_count,
         s))
   {
      return FALSE;
   }
   if (meta.kind == kPalExtremeSaveKindDosLegacy)
   {
      UTIL_LogOutput(LOGLEVEL_INFO,
         "Imported legacy DOS save with %u event objects into full TF state\n",
         (unsigned)meta.source_event_count);
   }
   return TRUE;
}

static PAL_EXTREME_SAVE_QUALITY
PAL_ExtremeSavePathQualityInternal(
   const char            *path,
   size_t                 fixed_bytes,
   WORD                   format,
   PAL_EXTREME_SAVE_META *out_meta,
   BOOL                    verify_event_payload
);

static PAL_EXTREME_SAVE_QUALITY
PAL_ExtremeSavePathQuality(
   const char            *path,
   size_t                 fixed_bytes,
   WORD                   format,
   PAL_EXTREME_SAVE_META *out_meta
);

static PAL_EXTREME_SAVE_QUALITY
PAL_ExtremeSavePathQualityInternal(
   const char            *path,
   size_t                 fixed_bytes,
   WORD                   format,
   PAL_EXTREME_SAVE_META *out_meta,
   BOOL                    verify_event_payload
)
{
   FILE *fp;
   uint8_t magic[8];
   PAL_EXTREME_SAVE_META meta;
   LPSAVEDGAME_COMMON state =
      (LPSAVEDGAME_COMMON)PAL_SAVEGAME_STATIC;
   BOOL valid = FALSE;
   BOOL tagged = FALSE;

   if (path == NULL || (fp = fopen(path, "rb")) == NULL)
   {
      return kPalExtremeSaveInvalid;
   }
   if (fread(magic, 1, sizeof(magic), fp) == sizeof(magic) &&
      fseek(fp, 0, SEEK_SET) == 0)
   {
      tagged = memcmp(
         magic, PAL_EXTREME_SAVE_MAGIC, sizeof(magic)) == 0;
      if (tagged)
      {
         valid = PAL_ExtremeSaveReadHeader(
            fp, fixed_bytes, format,
            PAL_SAVEGAME_STATIC, fixed_bytes, &meta,
            verify_event_payload) &&
            PAL_ExtremeSaveReadFixed(
               fp, state, fixed_bytes, meta.header_bytes);
      }
      else if (format == PAL_EXTREME_SAVE_FORMAT_DOS)
      {
         valid = PAL_ExtremeSaveLegacyDosMeta(
               fp, fixed_bytes, &meta) &&
            PAL_ExtremeSaveReadFixed(fp, state, fixed_bytes, 0u);
      }
   }
   if (valid)
   {
      DO_BYTESWAP(state, fixed_bytes);
      valid = PAL_ExtremeSaveStateValid(state, &meta);
   }
   if (fclose(fp) != 0)
   {
      valid = FALSE;
   }
   if (!valid)
   {
      return kPalExtremeSaveInvalid;
   }
   if (out_meta != NULL)
   {
      *out_meta = meta;
   }
   return tagged ? kPalExtremeSaveTagged : kPalExtremeSaveLegacy;
}

static PAL_EXTREME_SAVE_QUALITY
PAL_ExtremeSavePathQuality(
   const char            *path,
   size_t                 fixed_bytes,
   WORD                   format,
   PAL_EXTREME_SAVE_META *out_meta
)
{
   return PAL_ExtremeSavePathQualityInternal(
      path, fixed_bytes, format, out_meta, TRUE);
}

static VOID
PAL_ExtremeSaveBuildHeader(
   uint8_t header[PAL_EXTREME_SAVE_HEADER_BYTES],
   WORD    format,
   UINT32  fixed_bytes,
   UINT32  profile_id,
   UINT32  template_crc32,
   UINT32  generation,
   UINT32  payload_crc,
   UINT32  fixed_crc,
   UINT32  event_crc,
   WORD    scene,
   WORD    party_index
)
{
   UINT32 total_bytes = PAL_EXTREME_SAVE_HEADER_BYTES +
      fixed_bytes + PAL_EXTREME_SAVE_EVENT_BYTES;
   UINT32 header_crc;

   memset(header, 0, PAL_EXTREME_SAVE_HEADER_BYTES);
   memcpy(header, PAL_EXTREME_SAVE_MAGIC, 8);
   PAL_ExtremeWriteLE16(header + PAL_EXTREME_HEADER_VERSION_OFFSET,
      PAL_EXTREME_SAVE_VERSION);
   PAL_ExtremeWriteLE16(header + PAL_EXTREME_HEADER_SIZE_OFFSET,
      PAL_EXTREME_SAVE_HEADER_BYTES);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_PROFILE_OFFSET,
      profile_id);
   PAL_ExtremeWriteLE16(header + PAL_EXTREME_HEADER_FORMAT_OFFSET,
      format);
   PAL_ExtremeWriteLE16(header + PAL_EXTREME_HEADER_FLAGS_OFFSET, 0u);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_FIXED_BYTES_OFFSET,
      fixed_bytes);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_EVENT_COUNT_OFFSET,
      PAL_EXTREME_SAVE_EVENT_RECORDS);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_EVENT_BYTES_OFFSET,
      PAL_EXTREME_SAVE_EVENT_BYTES);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_TOTAL_BYTES_OFFSET,
      total_bytes);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_PAYLOAD_CRC_OFFSET,
      payload_crc);
   PAL_ExtremeWriteLE16(header + PAL_EXTREME_HEADER_SCENE_OFFSET, scene);
   PAL_ExtremeWriteLE16(header + PAL_EXTREME_HEADER_PARTY_OFFSET,
      party_index);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_TEMPLATE_CRC_OFFSET,
      template_crc32);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_GENERATION_OFFSET,
      generation);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_FIXED_CRC_OFFSET,
      fixed_crc);
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_EVENT_CRC_OFFSET,
      event_crc);
   header_crc = PAL_ExtremeCRC32Update(
      0xffffffffu, header, PAL_EXTREME_HEADER_CRC_OFFSET) ^ 0xffffffffu;
   PAL_ExtremeWriteLE32(header + PAL_EXTREME_HEADER_CRC_OFFSET,
      header_crc);
}

static BOOL
PAL_ExtremeSaveWriteCounted(
   FILE          *fp,
   const void    *data,
   size_t         bytes,
   unsigned long *api_write_bytes,
   UINT32        *write_calls
)
{
   size_t done;

   if (fp == NULL || data == NULL || api_write_bytes == NULL ||
      write_calls == NULL)
   {
      return FALSE;
   }
   done = fwrite(data, 1, bytes, fp);
   *api_write_bytes += (unsigned long)done;
   (*write_calls)++;
   return done == bytes;
}

static BOOL
PAL_ExtremeSaveWriteFile(
   int                 slot,
   WORD                format,
   LPSAVEDGAME_COMMON  s,
   size_t              fixed_bytes
)
{
   uint8_t header[PAL_EXTREME_SAVE_HEADER_BYTES];
   uint8_t *staging = PAL_SAVEGAME_STATIC;
   PAL_EXTREME_SAVE_META state_meta;
   PAL_EXTREME_SAVE_META verify_meta;
   char temporary_path[PAL_EXTREME_SAVE_PATH_BYTES];
   char final_path[PAL_EXTREME_SAVE_PATH_BYTES];
   char backup_path[PAL_EXTREME_SAVE_PATH_BYTES];
   EVENTOBJECT event_object;
   FILE *fp = NULL;
   uint32_t profile_id = 0u;
   uint32_t template_crc32 = 0u;
   uint32_t generation = 0u;
   UINT32 fixed_crc = 0u;
   UINT32 event_crc = 0xffffffffu;
   UINT32 payload_crc;
   UINT32 event_id;
   UINT32 staged = 0u;
   UINT32 write_calls = 0u;
   UINT32 sync_calls = 0u;
   unsigned long api_write_bytes = 0u;
   UINT32 start_ms = SDL_GetTicks();
   WORD scene;
   WORD party_index;
   BOOL result = FALSE;
   PAL_EXTREME_SAVE_QUALITY final_quality;

   memset(&state_meta, 0, sizeof(state_meta));
   state_meta.kind = kPalExtremeSaveKindCurrent;
   state_meta.scene = s->wNumScene;
   state_meta.party_index = s->nPartyMember;
   if (fixed_bytes > UINT32_MAX ||
      !PAL_ExtremeEventStorageReady() ||
      !PAL_ExtremeSaveStateValid(s, &state_meta) ||
      !PAL_EventStateFlush(PAL_EVENT_WRITE_SAVE) ||
      !PalEngineEventState_GetIdentity(
         &profile_id, &template_crc32, &generation) ||
      !PAL_ExtremeSaveBuildPath(slot, "tmp", temporary_path,
         sizeof(temporary_path)) ||
      !PAL_ExtremeSaveBuildPath(slot, "rpg", final_path,
         sizeof(final_path)) ||
      !PAL_ExtremeSaveBuildPath(slot, "bak", backup_path,
         sizeof(backup_path)))
   {
      goto done;
   }
   scene = s->wNumScene;
   party_index = s->nPartyMember;
   fixed_crc = PAL_ExtremeCRC32Update(
      0xffffffffu, (const uint8_t *)s, fixed_bytes) ^ 0xffffffffu;
   payload_crc = fixed_crc ^ 0xffffffffu;

   fp = fopen(temporary_path, "wb");
   if (fp == NULL)
   {
      goto done;
   }
   memset(header, 0, sizeof(header));
   if (!PAL_ExtremeSaveWriteCounted(
         fp, header, sizeof(header),
         &api_write_bytes, &write_calls) ||
      !PAL_ExtremeSaveWriteCounted(
         fp, s, fixed_bytes,
         &api_write_bytes, &write_calls))
   {
      goto close_failed;
   }

   for (event_id = 1u;
        event_id <= PAL_EXTREME_SAVE_EVENT_RECORDS;
        event_id++)
   {
      if (!PAL_EventObjectRead((WORD)event_id, &event_object))
      {
         goto close_failed;
      }
      memcpy(staging + staged, &event_object, sizeof(event_object));
      staged += sizeof(event_object);
      event_crc = PAL_ExtremeCRC32Update(
         event_crc, (const uint8_t *)&event_object,
         sizeof(event_object));
      payload_crc = PAL_ExtremeCRC32Update(
         payload_crc, (const uint8_t *)&event_object,
         sizeof(event_object));
      if (staged == PAL_EVENT_PAGER_PAGE_BYTES ||
         event_id == PAL_EXTREME_SAVE_EVENT_RECORDS)
      {
         if (!PAL_ExtremeSaveWriteCounted(
               fp, staging, staged,
               &api_write_bytes, &write_calls))
         {
            goto close_failed;
         }
         staged = 0u;
      }
   }
   event_crc ^= 0xffffffffu;
   payload_crc ^= 0xffffffffu;
   PAL_ExtremeSaveBuildHeader(
      header, format, (UINT32)fixed_bytes,
      profile_id, template_crc32, generation,
      payload_crc, fixed_crc, event_crc, scene, party_index);
   if (fseek(fp, 0L, SEEK_SET) != 0 ||
      !PAL_ExtremeSaveWriteCounted(
         fp, header, sizeof(header),
         &api_write_bytes, &write_calls))
   {
      goto close_failed;
   }
   sync_calls++;
   if (fflush(fp) != 0 || ferror(fp) != 0)
   {
      goto close_failed;
   }
   if (fclose(fp) != 0)
   {
      fp = NULL;
      goto failed_file;
   }
   fp = NULL;

   fp = fopen(temporary_path, "rb");
   result = fp != NULL &&
      PAL_ExtremeSaveReadHeader(
         fp, fixed_bytes, format,
         PAL_SAVEGAME_STATIC, fixed_bytes, &verify_meta, TRUE);
   if (fp != NULL && fclose(fp) != 0)
   {
      result = FALSE;
   }
   fp = NULL;
   if (!result)
   {
      goto failed_file;
   }
   final_quality = PAL_ExtremeSavePathQuality(
      final_path, fixed_bytes, format, NULL);
   if (!PAL_ExtremeSaveAtomicReplace(
         temporary_path, final_path, backup_path, final_quality))
   {
      result = FALSE;
      goto failed_file;
   }
   result = TRUE;
   goto done;

close_failed:
   if (fp != NULL && fclose(fp) != 0)
   {
      result = FALSE;
   }
   fp = NULL;
failed_file:
   (void)PalTarget_SaveUnlink(temporary_path, true);
done:
   printf(
      "PAL_TFIO v=1 op=save_snapshot slot=%d generation=%lu "
      "records=%u logical_b=%lu api_write_b=%lu write_calls=%lu "
      "sync_calls=%u elapsed_ms=%lu metadata_ops=3 result=%s\n",
      slot,
      (unsigned long)generation,
      (unsigned)PAL_EXTREME_SAVE_EVENT_RECORDS,
      (unsigned long)((UINT32)fixed_bytes +
         PAL_EXTREME_SAVE_EVENT_BYTES),
      api_write_bytes,
      (unsigned long)write_calls,
      (unsigned)sync_calls,
      (unsigned long)(SDL_GetTicks() - start_ms),
      result ? "ok" : "fail");
   return result;
}

static BOOL
PAL_ExtremeReadSavedTimesAtPath(
   const char *path,
   WORD       *saved_times
)
{
   FILE *fp;
   uint8_t saved_times_bytes[2];
   size_t fixed_bytes = gConfig.fIsWIN95 ?
      PAL_EXTREME_SAVE_WIN_BYTES : PAL_EXTREME_SAVE_DOS_BYTES;
   WORD format = gConfig.fIsWIN95 ?
      PAL_EXTREME_SAVE_FORMAT_WIN95 : PAL_EXTREME_SAVE_FORMAT_DOS;
   PAL_EXTREME_SAVE_META meta;
   PAL_EXTREME_SAVE_QUALITY quality;
   UINT32 offset;
   BOOL valid;

   if (path == NULL || saved_times == NULL)
   {
      return FALSE;
   }
   quality = PAL_ExtremeSavePathQualityInternal(
      path, fixed_bytes, format, &meta, FALSE);
   if (quality == kPalExtremeSaveInvalid)
   {
      return FALSE;
   }
   offset = quality == kPalExtremeSaveTagged ? meta.header_bytes : 0u;
   fp = fopen(path, "rb");
   if (fp == NULL)
   {
      return FALSE;
   }
   valid = offset <= (UINT32)LONG_MAX &&
      fseek(fp, (long)offset, SEEK_SET) == 0 &&
      fread(saved_times_bytes, 1, sizeof(saved_times_bytes), fp) ==
         sizeof(saved_times_bytes);
   if (fclose(fp) != 0)
   {
      valid = FALSE;
   }
   if (valid)
   {
      *saved_times = PAL_ExtremeReadLE16(saved_times_bytes);
   }
   return valid;
}
