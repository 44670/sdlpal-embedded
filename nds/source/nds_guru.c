#include "pal_engine_guru.h"

#include "pal_target_board.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void AUDIO_CloseDevice(void);

void
PalEngineBridge_GuruMeditationAt(
   const char *file,
   uint32_t line,
   const char *reason)
{
   static bool entering;

   if (!entering)
   {
      entering = true;
      AUDIO_CloseDevice();
   }
   NdsTarget_FatalAt(file, line, reason);
}

/*
 * Strong overrides of calico's weak ARM9 exception stubs.  The stock stubs
 * park the CPU in an infinite ITCM loop after pushing the fault frame to an
 * exception stack DeSmuME does not map, so any abort looked like a random
 * hang with no recoverable context.  These handlers record the abort type,
 * the faulting instruction address and the SPSR in a named owner, print them
 * on the console, and halt.
 */
typedef struct PalNdsFaultInfo {
   uint32_t type;      /* 1 prefetch abort, 2 data abort, 3 undefined insn */
   uint32_t address;   /* faulting instruction */
   uint32_t spsr;      /* CPSR of the aborted context */
} PalNdsFaultInfo;

PalNdsFaultInfo pal_nds_fault_info;
uint32_t pal_nds_fault_stack[64];
/* Register snapshot of the aborted context, written before any C code runs:
   [0..14] r0-r14 of the aborted (system/user) context, [15] faulting pc,
   [16] spsr, [17] abort type.  Only the FIRST fault is recorded; nested
   faults inside the handler path halt immediately.  Read it back over the
   debug harness. */
uint32_t pal_nds_fault_regs[18];
uint32_t pal_nds_fault_count;
static char pal_nds_fault_message[96];

void
pal_nds_fault_show(
   uint32_t type,
   uint32_t address,
   uint32_t spsr)
{
   /* Deliberately no libc and no console work here: the fault may have
      happened with stdio or the console driver in an inconsistent state,
      and re-entering it turned earlier builds into recursive-fault chaos
      that destroyed all evidence.  Record and halt; the debug harness
      reads the record back. */
   pal_nds_fault_info.type = type;
   pal_nds_fault_info.address = address;
   pal_nds_fault_info.spsr = spsr;
   (void)pal_nds_fault_message;
}

__attribute__((naked)) void
pal_nds_fault_common(
   void)
{
   __asm__ volatile(
      "push {r2, r3}\n"
      "ldr r3, =pal_nds_fault_count\n"
      "ldr r2, [r3]\n"
      "add r2, r2, #1\n"
      "str r2, [r3]\n"
      "cmp r2, #1\n"
      "pop {r2, r3}\n"
      "bne 2f\n"
      "ldr r3, =pal_nds_fault_regs+4\n"
      "stmia r3!, {r1-r12}\n"
      "stmia r3, {r13-r14}^\n"
      "mov r1, lr\n"
      "str r1, [r3, #8]\n"
      "mrs r2, spsr\n"
      "str r2, [r3, #12]\n"
      "str r0, [r3, #16]\n"
      "pop {r1}\n"
      "str r1, [r3, #-52]\n"
      "mov r1, lr\n"
      "bl pal_nds_fault_show\n"
      "2: mrs r1, cpsr\n"
      "orr r1, r1, #192\n"
      "msr cpsr_c, r1\n"
      "3: b 3b\n");
}

__attribute__((naked)) void
__arm_excpt_pabt(
   void)
{
   __asm__ volatile(
      "sub lr, lr, #4\n"
      "ldr sp, =pal_nds_fault_stack+256\n"
      "push {r0}\n"
      "mov r0, #1\n"
      "b pal_nds_fault_common\n");
}

__attribute__((naked)) void
__arm_excpt_dabt(
   void)
{
   __asm__ volatile(
      "sub lr, lr, #8\n"
      "ldr sp, =pal_nds_fault_stack+256\n"
      "push {r0}\n"
      "mov r0, #2\n"
      "b pal_nds_fault_common\n");
}

__attribute__((naked)) void
__arm_excpt_und(
   void)
{
   __asm__ volatile(
      "sub lr, lr, #4\n"
      "ldr sp, =pal_nds_fault_stack+256\n"
      "push {r0}\n"
      "mov r0, #3\n"
      "b pal_nds_fault_common\n");
}
