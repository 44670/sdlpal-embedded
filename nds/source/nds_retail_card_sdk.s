	.syntax unified
	.arm

	.section .text.nds_retail_card,"ax",%progbits
	.align 2

	/*
	 * SDK 3 CARDi_ReadCard-shaped entry. The unpatched path calls Calico's
	 * physical Slot-1 reader. Retail loaders replace the first two words and
	 * use the common-structure pointer immediately before the literal pair.
	 */
	.global NdsRetail_CardReadSdk
	.type NdsRetail_CardReadSdk,%function
NdsRetail_CardReadSdk:
	.word 0xe92d4ff0              /* stmdb sp!, {r4-r11,lr} */
	bl NdsRetail_CardReadDirect
	.word 0xe8bd8ff0              /* ldmia sp!, {r4-r11,pc} */
	.space 32, 0
	.word g_nds_retail_card_common
	.word 0x04100010
	.word 0x040001a4
	.word 0xe92d4ff0              /* SDK 3 elaborate end marker */
	.word 0xe8bd8ff0
	.size NdsRetail_CardReadSdk, .-NdsRetail_CardReadSdk

	/* SDK 3 CARD IRQ-enable entry.  Its first four words are the standard
	 * finder surface.  A retail loader replaces the first two words with its
	 * implementation; an unpatched cartridge boot falls through to Calico. */
	.align 2
	.global NdsRetail_CardIrqEnableSdk
	.type NdsRetail_CardIrqEnableSdk,%function
	.global g_nds_retail_arm9_irq_surface
NdsRetail_CardIrqEnableSdk:
g_nds_retail_arm9_irq_surface:
	.word 0xe59fc028
	.word 0xe3a01000
	.word 0xe1dc30b0
	.word 0xe59f2020
	b NdsRetail_CardIrqEnableDirect
	.space 28, 0
	.word 0x04000210
	.word 0
	.size NdsRetail_CardIrqEnableSdk, .-NdsRetail_CardIrqEnableSdk

	/* Nintendo SDK-style ARM9 IRQ dispatcher surface.  A retail loader needs
	 * the table address so its ARM9 service can receive IPC-sync requests from
	 * the patched ARM7 backup routines. */
	.align 2
	.global g_nds_retail_arm9_irq_handler
g_nds_retail_arm9_irq_handler:
	.word 0xe92d4000
	.word 0xe3a0c301
	.word 0xe28cce21
	.word 0xe51c1008
	.word 0xe3510000
	.word 0xe59f1008
	.word 0xe7910100
	.word 0xe59fe004
	.word 0xe12fff10
	.word g_nds_retail_arm9_irq_table
	.word g_nds_retail_arm9_irq_return_anchor
g_nds_retail_arm9_irq_return_anchor:
	.word 0xe12fff1e
	.align 2
	.global g_nds_retail_arm9_irq_table
g_nds_retail_arm9_irq_table:
	.space 16 * 4, 0
	.word NdsRetail_IrqBridgeArm9IpcSync
	.space 15 * 4, 0

	/* Real 9-word NitroSDK module-parameter record. nds-bootstrap finds the
	 * two magic words and walks back seven words to sdk_version. */
	.align 2
	.global g_nds_retail_module_params
g_nds_retail_module_params:
	.word 0                       /* autoload list offset */
	.word 0                       /* autoload list end */
	.word 0                       /* autoload start */
	.word 0                       /* static BSS start */
	.word 0                       /* static BSS end */
	.word 0                       /* compressed static end */
	.word 0x03002001              /* Nintendo SDK 3.2.1 classifier */
	.word 0xdec00621
	.word 0x2106c0de
