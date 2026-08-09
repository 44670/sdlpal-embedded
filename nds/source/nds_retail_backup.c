#include "nds_retail.h"

#include <nds.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum {
   NDS_RETAIL_BACKUP_READ = 1,
   NDS_RETAIL_BACKUP_WRITE = 2,
   NDS_RETAIL_BACKUP_ERASE = 3,
   NDS_RETAIL_BACKUP_TRANSFER_BYTES = 256,
};

typedef struct NdsRetailBackupCommand {
   uint32_t operation;
   uint32_t reserved1;
   uint32_t reserved2;
   uint32_t argument0;
   uint32_t argument1;
   uint32_t size;
   uint32_t cache_padding[2];
} NdsRetailBackupCommand;

static NdsRetailBackupCommand s_backup_command
   __attribute__((aligned(32), section(".bss.nds_retail_backup")));
static uint8_t s_backup_transfer[NDS_RETAIL_BACKUP_TRANSFER_BYTES]
   __attribute__((aligned(32), section(".bss.nds_retail_backup")));
static bool s_backup_ready;

static bool
nds_retail_backup_command(
   uint32_t operation,
   uint32_t argument0,
   uint32_t argument1,
   uint32_t size)
{
   if (!s_backup_ready)
   {
      return false;
   }
   s_backup_command.operation = operation;
   s_backup_command.reserved1 = 0u;
   s_backup_command.reserved2 = 0u;
   s_backup_command.argument0 = argument0;
   s_backup_command.argument1 = argument1;
   s_backup_command.size = size;
   armDCacheFlush(&s_backup_command, sizeof(s_backup_command));
   return pxiSendAndReceive(
      PxiChannel_User0,
      (uint32_t)(uintptr_t)&s_backup_command) != 0u;
}

bool
NdsRetail_BackupInit(
   void)
{
   if (isDSiMode())
   {
      return false;
   }
   pxiWaitRemote(PxiChannel_User0);
   s_backup_ready = true;
   return true;
}

bool
NdsRetail_BackupRead(
   uint32_t offset,
   void *destination,
   uint32_t size)
{
   bool ok;

   if ((destination == NULL && size != 0u) ||
      size > sizeof(s_backup_transfer) || offset > UINT32_MAX - size)
   {
      return false;
   }
   armDCacheFlush(s_backup_transfer, sizeof(s_backup_transfer));
   ok = nds_retail_backup_command(
      NDS_RETAIL_BACKUP_READ,
      offset,
      (uint32_t)(uintptr_t)s_backup_transfer,
      size);
   armDCacheInvalidate(s_backup_transfer, sizeof(s_backup_transfer));
   if (ok && size != 0u)
   {
      memcpy(destination, s_backup_transfer, size);
   }
   return ok;
}

bool
NdsRetail_BackupWrite(
   uint32_t offset,
   const void *source,
   uint32_t size)
{
   if ((source == NULL && size != 0u) ||
      size > sizeof(s_backup_transfer) || offset > UINT32_MAX - size)
   {
      return false;
   }
   if (size != 0u)
   {
      memcpy(s_backup_transfer, source, size);
   }
   armDCacheFlush(s_backup_transfer, sizeof(s_backup_transfer));
   return nds_retail_backup_command(
      NDS_RETAIL_BACKUP_WRITE,
      (uint32_t)(uintptr_t)s_backup_transfer,
      offset,
      size);
}

bool
NdsRetail_BackupErase(
   uint32_t offset)
{
   return nds_retail_backup_command(
      NDS_RETAIL_BACKUP_ERASE, 0u, offset, 0u);
}
