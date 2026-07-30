#ifndef PAL_TARGET_BOARD_H
#define PAL_TARGET_BOARD_H

#if defined(PAL_CARDPUTER_EXTREME)

#include "cardputer_extreme_board.h"

#define PAL_TARGET_LCD_WIDTH CARDPUTER_EXTREME_LCD_WIDTH
#define PAL_TARGET_LCD_HEIGHT CARDPUTER_EXTREME_LCD_HEIGHT
#define PalTarget_Begin CardputerExtreme_Begin
#define PalTarget_MountTf CardputerExtreme_MountTf
#define PalTarget_PrepareTfAccess CardputerExtreme_PrepareTfAccess
#define PalTarget_ShowError CardputerExtreme_ShowError
#define PalTarget_SaveUnlink CardputerExtreme_SaveUnlink
#define PalTarget_SaveRename CardputerExtreme_SaveRename

#else

#include "cores3se_board.h"

#define PAL_TARGET_LCD_WIDTH CORES3SE_LCD_WIDTH
#define PAL_TARGET_LCD_HEIGHT CORES3SE_LCD_HEIGHT
#define PalTarget_Begin CoreS3Se_Begin
#define PalTarget_MountTf CoreS3Se_MountTf
#define PalTarget_PrepareTfAccess CoreS3Se_PrepareTfAccess
#define PalTarget_ShowError CoreS3Se_ShowError

#endif

#endif
