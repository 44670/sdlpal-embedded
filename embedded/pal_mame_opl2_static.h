/*
 * SDLPAL
 * Copyright (c) 2011-2026, SDLPAL development team.
 * All rights reserved.
 *
 * This file is part of SDLPAL.
 *
 * SDLPAL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SDLPAL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * pal_mame_opl2_static.h - Fixed-memory 22.05 kHz MAME YM3812 slice.
 */

#ifndef PAL_MAME_OPL2_STATIC_H
#define PAL_MAME_OPL2_STATIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAL_MAME_OPL2_CLOCK_HZ 3579545u
#define PAL_MAME_OPL2_SAMPLE_RATE 22050u

/*
 * This backend deliberately owns exactly one YM3812 instance. The Cardputer
 * extreme music task is its sole caller, so a context allocator and chip
 * duplication API would only add RAM and invalid concurrency states.
 */
void PalMameOpl2_Init(void);
void PalMameOpl2_Reset(void);
void PalMameOpl2_Write(uint8_t reg, uint8_t value);
void PalMameOpl2_Render(int16_t *samples, size_t frames);

/* Build/runtime audits use these instead of depending on the private core. */
size_t PalMameOpl2_StateBytes(void);
/* Includes every fixed lookup table, not only the four generated tables. */
size_t PalMameOpl2_TableBytes(void);

#ifdef __cplusplus
}
#endif

#endif
