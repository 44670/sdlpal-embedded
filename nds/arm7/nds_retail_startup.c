// SPDX-License-Identifier: ZPL-2.1
/*
 * NTR retail-compatible entry shim for the Calico ARM7 runtime.
 *
 * Calico's normal homebrew startup clears the reserved main-RAM ABI blocks at
 * 0x02ffd000 and above.  A retail loader owns that area for patch state and its
 * card engine, so a retail ROM must leave it alone.  The boot stub still performs
 * the hardware hand-off; this replacement only loads this binary's declared
 * sections, initializes Calico's local scheduler/PXI state, and joins the
 * loader-patched SDK IRQ table to Calico's live dispatcher.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*NdsRetailIrqHandler)(void);

extern uint8_t __heap_start[];
extern uint8_t __heap_end[];
extern uint8_t *fake_heap_start;
extern uint8_t *fake_heap_end;
bool g_isTwlMode;
bool g_cdcIsTwlMode;

extern NdsRetailIrqHandler __irq_table[];
extern NdsRetailIrqHandler g_nds_retail_arm7_irq_table[];

void _threadInit(void);
void _pxiInit(void);

static NdsRetailIrqHandler s_original_vblank;
static NdsRetailIrqHandler s_original_ipc_sync;

void
NdsRetail_IrqBridgeVBlank(
   void)
{
   if (s_original_vblank != NULL)
   {
      s_original_vblank();
   }
}

void
NdsRetail_IrqBridgeIpcSync(
   void)
{
   if (s_original_ipc_sync != NULL)
   {
      s_original_ipc_sync();
   }
}

static void
nds_retail_copy_words(
   uint32_t *destination,
   const uint32_t *source,
   uint32_t size)
{
   while (size >= sizeof(*destination))
   {
      *destination++ = *source++;
      size -= sizeof(*destination);
   }
}

static void
nds_retail_clear_words(
   uint32_t *destination,
   uint32_t size)
{
   while (size >= sizeof(*destination))
   {
      *destination++ = 0u;
      size -= sizeof(*destination);
   }
}

static void
nds_retail_process_load_list(
   const uint32_t *list_header)
{
   const uint32_t *source = (const uint32_t *)(uintptr_t)list_header[0];
   const uint32_t *entry = (const uint32_t *)(uintptr_t)list_header[1];
   const uint32_t *end = (const uint32_t *)(uintptr_t)list_header[2];

   while (entry < end)
   {
      uint32_t *destination = (uint32_t *)(uintptr_t)entry[0];
      uint32_t *data_end = (uint32_t *)(uintptr_t)entry[1];
      uint32_t *bss_end = (uint32_t *)(uintptr_t)entry[2];
      const uint32_t data_size =
         (uint32_t)((uintptr_t)data_end - (uintptr_t)destination);
      const uint32_t bss_size =
         (uint32_t)((uintptr_t)bss_end - (uintptr_t)data_end);

      nds_retail_copy_words(destination, source, data_size);
      source += data_size / sizeof(*source);
      nds_retail_clear_words(data_end, bss_size);
      entry += 3;
   }
}

__attribute__((section(".crt0.nds_retail_startup")))
void
crt0Startup(
   const uint32_t *module_header,
   uint32_t is_twl_mode)
{
   volatile uint16_t *const registers =
      (volatile uint16_t *)(uintptr_t)0x04000000u;
   volatile uint32_t *const reg_ime =
      (volatile uint32_t *)(uintptr_t)0x04000208u;
   volatile uint32_t *const reg_ie =
      (volatile uint32_t *)(uintptr_t)0x04000210u;
   volatile uint32_t *const reg_if =
      (volatile uint32_t *)(uintptr_t)0x04000214u;
   unsigned channel;

   (void)is_twl_mode;

   /* Stop inherited DMA and timer activity before loading WRAM. */
   for (channel = 0u; channel < 4u; channel++)
   {
      registers[(0x00bau + channel * 12u) / 2u] = 0u;
      registers[(0x0102u + channel * 4u) / 2u] = 0u;
   }

   nds_retail_process_load_list(module_header + 2);

   g_isTwlMode = false;
   fake_heap_start = __heap_start;
   fake_heap_end = __heap_end;

   *reg_ime = 0u;
   *reg_ie = 0u;
   *reg_if = UINT32_MAX;
   _threadInit();
   _pxiInit();

   /* The retail loader patches the SDK-shaped table in the raw ARM7 image.
    * Preserve Calico's original callbacks for the card engine's chain, then
    * install those patched entries into the live dispatcher. */
   s_original_vblank = __irq_table[0];
   s_original_ipc_sync = __irq_table[16];
   __irq_table[0] = g_nds_retail_arm7_irq_table[0];
   __irq_table[16] = g_nds_retail_arm7_irq_table[16];

   *reg_ime = 1u;
}
