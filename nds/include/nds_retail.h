#ifndef PAL_NDS_RETAIL_H
#define PAL_NDS_RETAIL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool NdsRetail_CardRead(
   uint32_t offset,
   void *destination,
   uint32_t size);

bool NdsRetail_BackupInit(void);
bool NdsRetail_BackupRead(
   uint32_t offset,
   void *destination,
   uint32_t size);
bool NdsRetail_BackupWrite(
   uint32_t offset,
   const void *source,
   uint32_t size);
bool NdsRetail_BackupErase(uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif
