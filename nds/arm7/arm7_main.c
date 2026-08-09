// SPDX-License-Identifier: ZPL-2.1
// SPDX-FileCopyrightText: Copyright fincs, devkitPro
#include <calico.h>
#include <nds/card.h>

#include <stdbool.h>
#include <stdint.h>

enum {
	NDS_RETAIL_BACKUP_READ = 1,
	NDS_RETAIL_BACKUP_WRITE = 2,
	NDS_RETAIL_BACKUP_ERASE = 3,
};

typedef struct NdsRetailBackupCommand {
	u32 operation;
	u32 reserved1;
	u32 reserved2;
	u32 argument0;
	u32 argument1;
	u32 size;
} NdsRetailBackupCommand;

bool NdsRetailBackup_ReadSdk(u32, u32, const NdsRetailBackupCommand*);
bool NdsRetailBackup_WriteSdk(u32, u32, const NdsRetailBackupCommand*);
bool NdsRetailBackup_EraseSdk(u32, u32, const NdsRetailBackupCommand*);

bool
NdsRetailBackup_ReadDirect(
	u32 offset,
	void *destination,
	u32 size)
{
	cardReadEeprom(offset, destination, size, 3);
	return true;
}

bool
NdsRetailBackup_WriteDirect(
	u32 offset,
	const void *source,
	u32 size)
{
	cardWriteEeprom(offset, (u8 *)(uintptr_t)source, size, 3);
	return true;
}

bool
NdsRetailBackup_ProgDirect(
	u32 offset,
	const void *source,
	u32 size)
{
	return NdsRetailBackup_WriteDirect(offset, source, size);
}

bool
NdsRetailBackup_VerifyDirect(
	u32 offset,
	const void *source,
	u32 size)
{
	/* The game commits and validates through a following read. This routine is
	 * present because it is part of the standard SDK backup jump surface. */
	(void)offset;
	(void)source;
	(void)size;
	return true;
}

bool
NdsRetailBackup_EraseDirect(
	u32 offset)
{
	cardEepromSectorErase(offset);
	return true;
}

static Thread s_backup_thread;
alignas(8) static u8 s_backup_thread_stack[1024];

static int
backup_thread_main(
	void *arg)
{
	Mailbox mailbox;
	u32 mailbox_slots[4];

	(void)arg;
	mailboxPrepare(&mailbox, mailbox_slots, 4);
	pxiSetMailbox(PxiChannel_User0, &mailbox);
	for (;;)
	{
		NdsRetailBackupCommand *command =
			(NdsRetailBackupCommand *)(uintptr_t)mailboxRecv(&mailbox);
		bool ok = false;

		if (command != NULL)
		{
			switch (command->operation)
			{
				case NDS_RETAIL_BACKUP_READ:
					ok = NdsRetailBackup_ReadSdk(0, 0, command);
					break;
				case NDS_RETAIL_BACKUP_WRITE:
					ok = NdsRetailBackup_WriteSdk(0, 0, command);
					break;
				case NDS_RETAIL_BACKUP_ERASE:
					ok = NdsRetailBackup_EraseSdk(0, 0, command);
					break;
				default:
					break;
			}
		}
		pxiReply(PxiChannel_User0, ok ? 1u : 0u);
	}
	return 0;
}

int
main(
	int argc,
	char *argv[])
{
	(void)argc;
	(void)argv;
	envReadNvramSettings();
	keypadStartExtServer();
	lcdSetIrqMask(DISPSTAT_IE_ALL, DISPSTAT_IE_VBLANK);
	irqEnable(IRQ_VBLANK);
	rtcInit();
	rtcSyncTime();
	pmInit();
	touchInit();
	touchStartServer(80, MAIN_THREAD_PRIO);
	soundStartServer(MAIN_THREAD_PRIO - 0x10);
	micStartServer(MAIN_THREAD_PRIO - 0x18);
	threadPrepare(
		&s_backup_thread,
		backup_thread_main,
		NULL,
		&s_backup_thread_stack[sizeof(s_backup_thread_stack)],
		MAIN_THREAD_PRIO - 8);
	threadStart(&s_backup_thread);
	while (pmMainLoop())
	{
		threadWaitForVBlank();
	}
	return 0;
}
