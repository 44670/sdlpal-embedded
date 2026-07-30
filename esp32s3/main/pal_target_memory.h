#ifndef PAL_TARGET_MEMORY_H
#define PAL_TARGET_MEMORY_H

#if defined(PAL_CARDPUTER_EXTREME)
#include "cardputer_extreme_memory.h"
#define PalTarget_TouchReservedBuffers CardputerExtreme_TouchReservedBuffers
#else
#include "cores3se_memory.h"
#define PalTarget_TouchReservedBuffers CoreS3Se_TouchReservedBuffers
#endif

#endif
