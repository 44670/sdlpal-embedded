#include "nds_retail.h"

#include <nds.h>

#include <stdbool.h>
#include <stdint.h>

/*
 * Nintendo SDK 3 CARD reads keep the active request in a shared common
 * structure. A retail loader derives the structure address from the literal
 * table in nds_retail_card.s and consumes words 7..9 after replacing the
 * SDK-shaped transfer routine.
 */
volatile uint32_t g_nds_retail_card_common[10]
   __attribute__((aligned(4), section(".data.nds_retail_card")));

typedef void (*NdsRetailIrqHandler)(void);

extern NdsRetailIrqHandler __irq_table[];
extern NdsRetailIrqHandler g_nds_retail_arm9_irq_table[];

static NdsRetailIrqHandler s_original_ipc_sync;
static bool s_card_open;

void
NdsRetail_IrqBridgeArm9IpcSync(
   void)
{
   if (s_original_ipc_sync != NULL)
   {
      s_original_ipc_sync();
   }
}

bool NdsRetail_CardReadSdk(
   void *cache,
   void *destination,
   uint32_t offset,
   uint32_t size);

void NdsRetail_CardIrqEnableSdk(uint32_t mask);

void
NdsRetail_CardIrqEnableDirect(
   uint32_t mask)
{
   irqEnable(mask);
}

bool
NdsRetail_CardReadDirect(
   void *cache,
   void *destination,
   uint32_t offset,
   uint32_t size)
{
   (void)cache;
   if (!s_card_open)
   {
      if (!ntrcardOpen() || ntrcardGetMode() != NtrCardMode_Main)
      {
         return false;
      }
      s_card_open = true;
   }
   return ntrcardRomRead(-1, offset, destination, size);
}

bool
NdsRetail_CardRead(
   uint32_t offset,
   void *destination,
   uint32_t size)
{
   if ((destination == NULL && size != 0u) ||
      offset > UINT32_MAX - size)
   {
      return false;
   }
   if (!s_card_open)
   {
      /* A retail title initializes CARD before its first transfer.  Under a
       * direct cartridge boot this is the ordinary Calico setup.  A retail
       * loader replaces the SDK-shaped IRQ-enable routine and uses this call
       * to install its ARM9 IPC service into the table below. */
      s_original_ipc_sync = __irq_table[16];
      NdsRetail_CardIrqEnableSdk(IRQ_CARD);
      if (!ntrcardOpen() || ntrcardGetMode() != NtrCardMode_Main)
      {
         return false;
      }
      s_card_open = true;
      if (g_nds_retail_arm9_irq_table[16] !=
         NdsRetail_IrqBridgeArm9IpcSync)
      {
         __irq_table[16] = g_nds_retail_arm9_irq_table[16];
      }
   }
   g_nds_retail_card_common[7] = offset;
   g_nds_retail_card_common[8] = (uint32_t)(uintptr_t)destination;
   g_nds_retail_card_common[9] = size;
   return NdsRetail_CardReadSdk(NULL, destination, offset, size);
}
