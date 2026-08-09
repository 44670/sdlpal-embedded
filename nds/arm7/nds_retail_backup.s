	.syntax unified
	.arm

	/* The first 0x200 bytes are scanned for the SDK WRAM-end literal. This
	 * prepad is never executed; the ELF entry remains __ds7_bootstub. */
	.section .prepad,"ax",%progbits
	.align 2
	.global g_nds_retail_arm7_prepad
g_nds_retail_arm7_prepad:
	.word 0x0380ff00
	.space 28, 0
	.word 0xe92d4010
	.word 0xe3a00008
	.space 80, 0

	.section .crt0.nds_retail_backup,"ax",%progbits
	.align 2

	/* SDK universal backup wrappers. The loader finds the three-load bodies
	 * and replaces their BL instructions with save-file-backed operations. */
	.global NdsRetailBackup_ReadSdk
	.type NdsRetailBackup_ReadSdk,%function
NdsRetailBackup_ReadSdk:
	push {r4,lr}
	.word 0xe592000c
	.word 0xe5921010
	.word 0xe5922014
	bl NdsRetailBackup_ReadDirect
	pop {r4,lr}
	bx lr
	.size NdsRetailBackup_ReadSdk, .-NdsRetailBackup_ReadSdk

	.global NdsRetailBackup_WriteSdk
	.type NdsRetailBackup_WriteSdk,%function
NdsRetailBackup_WriteSdk:
	push {r4,lr}
	.word 0xe5920010
	.word 0xe592100c
	.word 0xe5922014
	bl NdsRetailBackup_WriteDirect
	pop {r4,lr}
	bx lr
	.size NdsRetailBackup_WriteSdk, .-NdsRetailBackup_WriteSdk

	.global NdsRetailBackup_ProgSdk
	.type NdsRetailBackup_ProgSdk,%function
NdsRetailBackup_ProgSdk:
	push {r4,lr}
	.word 0xe5920010
	.word 0xe592100c
	.word 0xe5922014
	bl NdsRetailBackup_ProgDirect
	pop {r4,lr}
	bx lr
	.size NdsRetailBackup_ProgSdk, .-NdsRetailBackup_ProgSdk

	.global NdsRetailBackup_VerifySdk
	.type NdsRetailBackup_VerifySdk,%function
NdsRetailBackup_VerifySdk:
	push {r4,lr}
	.word 0xe5920010
	.word 0xe592100c
	.word 0xe5922014
	bl NdsRetailBackup_VerifyDirect
	pop {r4,lr}
	bx lr
	.size NdsRetailBackup_VerifySdk, .-NdsRetailBackup_VerifySdk

	.global NdsRetailBackup_EraseSdk
	.type NdsRetailBackup_EraseSdk,%function
NdsRetailBackup_EraseSdk:
	push {r4,lr}
	.word 0xe5920010
	.word 0xe5921014
	bl NdsRetailBackup_EraseDirect
	pop {r4,lr}
	bx lr
	.size NdsRetailBackup_EraseSdk, .-NdsRetailBackup_EraseSdk

	/* Required ARM7 CARD IRQ-enable finder surface. */
	.align 2
	.global g_nds_retail_arm7_irq_surface
	g_nds_retail_arm7_irq_surface:
	.word 0xe59fc028
	.word 0xe1dc30b0
	.word 0xe3a01000
	.word 0xe1cc10b0
	.space 48, 0

	/* Nintendo SDK-style ARM7 IRQ dispatcher surface. nds-bootstrap locates
	 * the vector table from the paired handler signatures and redirects its
	 * VBlank and IPC-sync entries to cardengine. Calico owns the live IRQ
	 * dispatcher during a direct cartridge boot; retail loaders replace these
	 * table entries before transferring control to the game. */
	.align 2
	.global g_nds_retail_arm7_irq_handler
g_nds_retail_arm7_irq_handler:
	.word 0xe92d4000
	.word 0xe3a0c301
	.word 0xe28cce21
	.word 0xe51c1008
	.word 0xe3510000
	.word 0xe59f1008
	.word 0xe7910100
	.word 0xe59fe004
	.word 0xe12fff10
	.word g_nds_retail_arm7_irq_table
	.word g_nds_retail_arm7_irq_return_anchor
g_nds_retail_arm7_irq_return_anchor:
	.word 0xe12fff1e
	.align 2
	.global g_nds_retail_arm7_irq_table
g_nds_retail_arm7_irq_table:
	.word NdsRetail_IrqBridgeVBlank
	.space 15 * 4, 0
	.word NdsRetail_IrqBridgeIpcSync
	.space 15 * 4, 0

	/* Minimal valid SDK3 relocation descriptor. The wrappers above remain in
	 * their raw main-RAM image, so source and destination bases intentionally
	 * cancel in nds-bootstrap's branch-address calculation. */
	.align 2
	.global g_nds_retail_arm7_relocation
g_nds_retail_arm7_relocation:
	.word 0x027ffffa
	.word g_nds_retail_arm7_relocation_target
	.word 0
	.word 0x037f8000
	.word 0x037f8000
	.word 0x037f8000
g_nds_retail_arm7_relocation_target:
	.word 0x037f8000
