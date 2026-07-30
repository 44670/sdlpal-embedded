#include "pal_target_board.h"

#include "pal_engine_pack_provider.h"

#include "ff.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAL_ENGINE_FATFS_STDIO_MAGIC 0x50465346u
#define PAL_ENGINE_FATFS_STDIO_SLOTS 2

typedef struct PalEngineFatfsFile {
   uint32_t magic;
   FIL file;
   bool eof;
   bool error;
} PalEngineFatfsFile;

static PalEngineFatfsFile pal_engine_fatfs_stdio_slots[PAL_ENGINE_FATFS_STDIO_SLOTS];

FILE *__real_fopen(const char *path, const char *mode);
int __real_fclose(FILE *stream);
size_t __real_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t __real_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int __real_fseek(FILE *stream, long offset, int whence);
long __real_ftell(FILE *stream);
char *__real_fgets(char *s, int size, FILE *stream);
int __real_fputs(const char *s, FILE *stream);
int __real_fflush(FILE *stream);
int __real_feof(FILE *stream);
int __real_ferror(FILE *stream);
void __real_rewind(FILE *stream);

static int
errno_from_fresult(
   FRESULT result
)
{
   switch (result)
   {
   case FR_OK:
      return 0;
   case FR_NO_FILE:
   case FR_NO_PATH:
      return ENOENT;
   case FR_DENIED:
   case FR_WRITE_PROTECTED:
      return EACCES;
   case FR_INVALID_NAME:
   case FR_INVALID_PARAMETER:
      return EINVAL;
   default:
      return EIO;
   }
}

static bool
is_fatfs_short_path(
   const char *path
)
{
   return path != NULL && path[0] == '0' && path[1] == ':' && path[2] == '/';
}

#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_DETERMINISTIC
static bool
is_native_deterministic_sidecar_path(
   const char *path
)
{
   static const char *const env_names[] = {
      "PAL_DETERMINISTIC_REPLAY",
      "PAL_DETERMINISTIC_RECORD",
      "PAL_DETERMINISTIC_CHECKPOINTS",
      "PAL_DETERMINISTIC_SCRIPT_TRACE",
      "PAL_DETERMINISTIC_EVENT_TRACE",
      "PAL_DETERMINISTIC_SCREENSHOT",
   };
   size_t i;

   if (path == NULL)
   {
      return false;
   }
   for (i = 0; i < sizeof(env_names) / sizeof(env_names[0]); i++)
   {
      const char *sidecar = getenv(env_names[i]);
      if (sidecar != NULL && sidecar[0] != '\0' && strcmp(path, sidecar) == 0)
      {
         return true;
      }
   }
   return false;
}

static FILE *
open_native_deterministic_sidecar(
   const char *path,
   const char *mode
)
{
   FILE *fp = __real_fopen(path, mode);
   if (fp == NULL)
   {
      errno = ENOENT;
   }
   return fp;
}
#endif

static bool
byte_count_to_uint(
   size_t size,
   size_t nmemb,
   UINT *out
)
{
   size_t bytes;

   if (out == NULL || (size != 0 && nmemb > ((size_t)UINT_MAX / size)))
   {
      return false;
   }
   bytes = size * nmemb;
   if (bytes > (size_t)UINT_MAX)
   {
      return false;
   }
   *out = (UINT)bytes;
   return true;
}

static BYTE
flags_from_mode(
   const char *mode
)
{
   bool read = false;
   bool write = false;
   BYTE flags = 0;

   if (mode == NULL || mode[0] == '\0')
   {
      return 0;
   }

   switch (mode[0])
   {
   case 'r':
      read = true;
      break;
   case 'w':
      write = true;
      flags |= FA_CREATE_ALWAYS;
      break;
   case 'a':
      write = true;
      flags |= FA_OPEN_APPEND;
      break;
   default:
      return 0;
   }

   if (strchr(mode, '+') != NULL)
   {
      read = true;
      write = true;
   }
   if (read)
   {
      flags |= FA_READ;
   }
   if (write)
   {
      flags |= FA_WRITE;
   }
   return flags;
}

static PalEngineFatfsFile *
fatfs_slot_from_file(
   FILE *stream
)
{
   uintptr_t value = (uintptr_t)stream;
   uintptr_t start = (uintptr_t)&pal_engine_fatfs_stdio_slots[0];
   uintptr_t end = (uintptr_t)&pal_engine_fatfs_stdio_slots[PAL_ENGINE_FATFS_STDIO_SLOTS];
   PalEngineFatfsFile *slot;

   if (value < start || value >= end ||
       ((value - start) % sizeof(pal_engine_fatfs_stdio_slots[0])) != 0)
   {
      return NULL;
   }

   slot = (PalEngineFatfsFile *)stream;
   return slot->magic == PAL_ENGINE_FATFS_STDIO_MAGIC ? slot : NULL;
}

static PalEngineFatfsFile *
alloc_fatfs_slot(
   void
)
{
   size_t i;

   for (i = 0; i < PAL_ENGINE_FATFS_STDIO_SLOTS; i++)
   {
      if (pal_engine_fatfs_stdio_slots[i].magic != PAL_ENGINE_FATFS_STDIO_MAGIC)
      {
         memset(&pal_engine_fatfs_stdio_slots[i], 0, sizeof(pal_engine_fatfs_stdio_slots[i]));
         pal_engine_fatfs_stdio_slots[i].magic = PAL_ENGINE_FATFS_STDIO_MAGIC;
         return &pal_engine_fatfs_stdio_slots[i];
      }
   }
   errno = EMFILE;
   return NULL;
}

FILE *
__wrap_fopen(
   const char *path,
   const char *mode
)
{
   PalEngineFatfsFile *slot;
   BYTE flags;
   FRESULT result;

   if (!is_fatfs_short_path(path))
   {
#if PAL_CORES3SE_NATIVE_ENGINE_HOST && PAL_DETERMINISTIC
      if (is_native_deterministic_sidecar_path(path))
      {
         return open_native_deterministic_sidecar(path, mode);
      }
#endif
      errno = ENOENT;
      return NULL;
   }

   flags = flags_from_mode(mode);
   if (flags == 0)
   {
      errno = EINVAL;
      return NULL;
   }

   slot = alloc_fatfs_slot();
   if (slot == NULL)
   {
      return NULL;
   }

   PalTarget_PrepareTfAccess();
   result = f_open(&slot->file, path, flags);
   if (result != FR_OK)
   {
      slot->magic = 0;
      errno = errno_from_fresult(result);
      return NULL;
   }
   slot->eof = false;
   slot->error = false;
   return (FILE *)slot;
}

int
__wrap_fclose(
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   FRESULT result;

   if (slot == NULL)
   {
      if (PalEngineBridge_IsPackFile(stream))
      {
         return 0;
      }
      return __real_fclose(stream);
   }

   PalTarget_PrepareTfAccess();
   result = f_close(&slot->file);
   slot->magic = 0;
   if (result != FR_OK)
   {
      errno = errno_from_fresult(result);
      return EOF;
   }
   return 0;
}

size_t
__wrap_fread(
   void *ptr,
   size_t size,
   size_t nmemb,
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   UINT bytes_read = 0;
   UINT bytes_requested;
   FRESULT result;

   if (slot == NULL)
   {
      return __real_fread(ptr, size, nmemb, stream);
   }
   if (size == 0 || nmemb == 0)
   {
      return 0;
   }

   if (!byte_count_to_uint(size, nmemb, &bytes_requested))
   {
      errno = EOVERFLOW;
      slot->error = true;
      return 0;
   }

   PalTarget_PrepareTfAccess();
   result = f_read(&slot->file, ptr, bytes_requested, &bytes_read);
   if (result != FR_OK)
   {
      errno = errno_from_fresult(result);
      slot->error = true;
      return 0;
   }
   if (bytes_read < bytes_requested)
   {
      slot->eof = true;
   }
   return bytes_read / size;
}

size_t
__wrap_fwrite(
   const void *ptr,
   size_t size,
   size_t nmemb,
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   UINT bytes_written = 0;
   UINT bytes_requested;
   FRESULT result;

   if (slot == NULL)
   {
      return __real_fwrite(ptr, size, nmemb, stream);
   }
   if (size == 0 || nmemb == 0)
   {
      return 0;
   }

   if (!byte_count_to_uint(size, nmemb, &bytes_requested))
   {
      errno = EOVERFLOW;
      slot->error = true;
      return 0;
   }

   PalTarget_PrepareTfAccess();
   result = f_write(&slot->file, ptr, bytes_requested, &bytes_written);
   if (result != FR_OK)
   {
      errno = errno_from_fresult(result);
      slot->error = true;
      return 0;
   }
   if (bytes_written < bytes_requested)
   {
      errno = EIO;
      slot->error = true;
   }
   return bytes_written / size;
}

int
__wrap_fseek(
   FILE *stream,
   long offset,
   int whence
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   FSIZE_t base = 0;
   FSIZE_t target;
   FRESULT result;

   if (slot == NULL)
   {
      return __real_fseek(stream, offset, whence);
   }

   switch (whence)
   {
   case SEEK_SET:
      base = 0;
      break;
   case SEEK_CUR:
      base = f_tell(&slot->file);
      break;
   case SEEK_END:
      base = f_size(&slot->file);
      break;
   default:
      errno = EINVAL;
      slot->error = true;
      return -1;
   }

   if (offset < 0)
   {
      unsigned long magnitude = offset == LONG_MIN ? (unsigned long)LONG_MAX + 1UL : (unsigned long)(-offset);
      if ((FSIZE_t)magnitude > base)
      {
         errno = EINVAL;
         slot->error = true;
         return -1;
      }
      target = base - (FSIZE_t)magnitude;
   }
   else
   {
      target = base + (FSIZE_t)offset;
   }

   PalTarget_PrepareTfAccess();
   result = f_lseek(&slot->file, target);
   if (result != FR_OK)
   {
      errno = errno_from_fresult(result);
      slot->error = true;
      return -1;
   }
   slot->eof = false;
   return 0;
}

long
__wrap_ftell(
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   FSIZE_t pos;

   if (slot == NULL)
   {
      return __real_ftell(stream);
   }

   pos = f_tell(&slot->file);
   if ((FSIZE_t)(long)pos != pos)
   {
      errno = EOVERFLOW;
      slot->error = true;
      return -1;
   }
   return (long)pos;
}

char *
__wrap_fgets(
   char *s,
   int size,
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   int used = 0;

   if (slot == NULL)
   {
      return __real_fgets(s, size, stream);
   }
   if (s == NULL || size <= 0)
   {
      errno = EINVAL;
      slot->error = true;
      return NULL;
   }

   while (used + 1 < size)
   {
      char ch = 0;
      UINT bytes_read = 0;
      FRESULT result;

      PalTarget_PrepareTfAccess();
      result = f_read(&slot->file, &ch, 1, &bytes_read);
      if (result != FR_OK)
      {
         errno = errno_from_fresult(result);
         slot->error = true;
         break;
      }
      if (bytes_read == 0)
      {
         slot->eof = true;
         break;
      }

      s[used++] = ch;
      if (ch == '\n')
      {
         break;
      }
   }

   if (used == 0)
   {
      return NULL;
   }
   s[used] = '\0';
   return s;
}

int
__wrap_fputs(
   const char *s,
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   UINT bytes_written = 0;
   UINT bytes_requested;
   FRESULT result;
   size_t len;

   if (slot == NULL)
   {
      return __real_fputs(s, stream);
   }
   if (s == NULL)
   {
      errno = EINVAL;
      slot->error = true;
      return EOF;
   }

   len = strlen(s);
   bytes_requested = (UINT)len;
   if ((size_t)bytes_requested != len)
   {
      errno = EOVERFLOW;
      slot->error = true;
      return EOF;
   }

   PalTarget_PrepareTfAccess();
   result = f_write(&slot->file, s, bytes_requested, &bytes_written);
   if (result != FR_OK || bytes_written != bytes_requested)
   {
      errno = result == FR_OK ? EIO : errno_from_fresult(result);
      slot->error = true;
      return EOF;
   }
   return 0;
}

int
__wrap_fflush(
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);
   FRESULT result;

   if (slot == NULL)
   {
      return __real_fflush(stream);
   }

   PalTarget_PrepareTfAccess();
   result = f_sync(&slot->file);
   if (result != FR_OK)
   {
      errno = errno_from_fresult(result);
      slot->error = true;
      return EOF;
   }
   return 0;
}

int
__wrap_feof(
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);

   if (slot == NULL)
   {
      return __real_feof(stream);
   }
   return slot->eof ? 1 : 0;
}

int
__wrap_ferror(
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);

   if (slot == NULL)
   {
      return __real_ferror(stream);
   }
   return slot->error ? 1 : 0;
}

void
__wrap_rewind(
   FILE *stream
)
{
   PalEngineFatfsFile *slot = fatfs_slot_from_file(stream);

   if (slot == NULL)
   {
      __real_rewind(stream);
      return;
   }

   PalTarget_PrepareTfAccess();
   if (f_lseek(&slot->file, 0) != FR_OK)
   {
      slot->error = true;
   }
   slot->eof = false;
}
