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
 * This file embeds Jarek Burczynski and Tatsuyuki Satoh's MAME FMOPL
 * v0.72 core. Its original GPL-2.0-or-later notice is retained in
 * adplug/mame/fmopl.cpp.h and adplug/mame/fmopl.h.
 */

#include "pal_mame_opl2_static.h"

#include "../adplug/mame/mame.h"
#include <type_traits>

#define SDLPAL_BUILD_OPL_CORE
#define SDLPAL_MAME_OPL2_STATIC_22050

namespace PalMameOpl2Core
{
#include "../adplug/mame/fmopl.cpp.h"

#if defined(__GNUC__)
#define PAL_MAME_OPL2_STATE_ATTR \
	__attribute__((section(".bss.pal_mame_opl2"), aligned(8)))
#else
#define PAL_MAME_OPL2_STATE_ATTR
#endif

FM_OPL pal_mame_opl2_state PAL_MAME_OPL2_STATE_ATTR;

static_assert(
	std::is_trivially_copyable<FM_OPL>::value,
	"fixed OPL2 state must remain safe to zero without construction"
);
static_assert(
	PAL_MAME_OPL2_FIXED_CLOCK == PAL_MAME_OPL2_CLOCK_HZ &&
	PAL_MAME_OPL2_FIXED_RATE == PAL_MAME_OPL2_SAMPLE_RATE,
	"generated OPL2 tables do not match the public fixed-rate API"
);

#undef PAL_MAME_OPL2_STATE_ATTR
}

extern "C" void
PalMameOpl2_Init(void)
{
	using namespace PalMameOpl2Core;

	memset(&pal_mame_opl2_state, 0, sizeof(pal_mame_opl2_state));
	pal_mame_opl2_state.type = OPL_TYPE_YM3812;
	pal_mame_opl2_state.clock_changed(
		PAL_MAME_OPL2_CLOCK_HZ,
		PAL_MAME_OPL2_SAMPLE_RATE
	);
	pal_mame_opl2_state.ResetChip();
}

extern "C" void
PalMameOpl2_Reset(void)
{
	/*
	 * Copl::init() promises a full reinitialization. ResetChip() mirrors the
	 * MAME device reset but intentionally leaves phase/LFO fields untouched,
	 * which would make a newly selected RIX depend on the previous track.
	 * The fixed instance is small, so restore its complete power-on state.
	 */
	PalMameOpl2_Init();
}

extern "C" void
PalMameOpl2_Write(uint8_t reg, uint8_t value)
{
	PalMameOpl2Core::pal_mame_opl2_state.Write(0, reg);
	PalMameOpl2Core::pal_mame_opl2_state.Write(1, value);
}

extern "C" void
PalMameOpl2_Render(int16_t *samples, size_t frames)
{
	if (samples == NULL)
	{
		return;
	}

	while (frames > 0)
	{
		const int block = frames > 32767u ? 32767 : static_cast<int>(frames);
		PalMameOpl2Core::ym3812_update_one(
			&PalMameOpl2Core::pal_mame_opl2_state,
			samples,
			block
		);
		samples += block;
		frames -= static_cast<size_t>(block);
	}
}

extern "C" size_t
PalMameOpl2_StateBytes(void)
{
	return sizeof(PalMameOpl2Core::pal_mame_opl2_state);
}

extern "C" size_t
PalMameOpl2_TableBytes(void)
{
	return sizeof(PalMameOpl2Core::pal_mame_opl2_fixed_tl_tab) +
		sizeof(PalMameOpl2Core::pal_mame_opl2_fixed_sin_tab) +
		sizeof(PalMameOpl2Core::pal_mame_opl2_fixed_fn_tab) +
		sizeof(PalMameOpl2Core::pal_mame_opl2_fixed_ksl_tab) +
		PalMameOpl2Core::FM_OPL::FixedMemberTableBytes() +
		sizeof(PalMameOpl2Core::eg_rate_shift) +
		sizeof(PalMameOpl2Core::eg_rate_select) +
		sizeof(PalMameOpl2Core::slot_array);
}
