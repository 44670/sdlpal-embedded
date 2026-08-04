#ifndef PAL_TARGET_MEMORY_H
#define PAL_TARGET_MEMORY_H

#include "pal_target_board.h"

#if defined(PAL_TARGET_XIAOMIAO)
#include "xiaomiao_memory.h"
#define PalTarget_TouchReservedBuffers Xiaomiao_TouchReservedBuffers
#elif defined(PAL_TARGET_CARDPUTER_ADV)
#include "cardputer_extreme_memory.h"
#define PalTarget_TouchReservedBuffers CardputerExtreme_TouchReservedBuffers
#else
#include "cores3se_memory.h"
#define PalTarget_TouchReservedBuffers CoreS3Se_TouchReservedBuffers
#endif

#if defined(PAL_EXTREME_TWO_SCREENS)
#if !defined(PAL_EXTREME_SCREEN_WIDTH) || \
    !defined(PAL_EXTREME_SCREEN_HEIGHT) || \
    !defined(PAL_EXTREME_SCREEN_BYTES)
#error "two-screen target memory must declare its native framebuffer geometry"
#endif
#if PAL_EXTREME_SCREEN_WIDTH != PAL_TARGET_LCD_WIDTH || \
    PAL_EXTREME_SCREEN_HEIGHT != PAL_TARGET_LCD_HEIGHT
#error "target framebuffer geometry does not match the selected LCD"
#endif
#if PAL_EXTREME_SCREEN_BYTES != \
    (PAL_TARGET_LCD_WIDTH * PAL_TARGET_LCD_HEIGHT)
#error "target framebuffer byte count does not match the selected LCD"
#endif
#endif

#endif
