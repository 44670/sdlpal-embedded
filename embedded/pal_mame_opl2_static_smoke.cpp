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
 */

#include "pal_mame_opl2_static.h"
#include "../adplug/mame/mame.h"

#include <stdio.h>
#include <stdlib.h>

namespace PalMameOpl2Reference
{
#include "../adplug/mame/fmopl.cpp.h"
}

static int16_t reference_samples[315];
static int16_t fixed_samples[315];

static void
write_both(void *reference, uint8_t reg, uint8_t value)
{
	PalMameOpl2Reference::ym3812_write(reference, 0, reg);
	PalMameOpl2Reference::ym3812_write(reference, 1, value);
	PalMameOpl2_Write(reg, value);
}

static void
program_melodic_voice(void *reference)
{
	write_both(reference, 0x01, 0x20);
	write_both(reference, 0x20, 0x21);
	write_both(reference, 0x23, 0x01);
	write_both(reference, 0x40, 0x28);
	write_both(reference, 0x43, 0x00);
	write_both(reference, 0x60, 0xf3);
	write_both(reference, 0x63, 0xf2);
	write_both(reference, 0x80, 0x55);
	write_both(reference, 0x83, 0x34);
	write_both(reference, 0xe0, 0x01);
	write_both(reference, 0xe3, 0x02);
	write_both(reference, 0xc0, 0x04);
	write_both(reference, 0xa0, 0x98);
	write_both(reference, 0xb0, 0x31);
}

static void
program_rhythm(void *reference)
{
	static const uint8_t operator_offsets[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };
	for (size_t i = 0; i < sizeof(operator_offsets); ++i)
	{
		const uint8_t offset = operator_offsets[i];
		write_both(reference, static_cast<uint8_t>(0x20 + offset), 0x01);
		write_both(reference, static_cast<uint8_t>(0x40 + offset), static_cast<uint8_t>(0x08 + i));
		write_both(reference, static_cast<uint8_t>(0x60 + offset), 0xf4);
		write_both(reference, static_cast<uint8_t>(0x80 + offset), 0x35);
	}
	write_both(reference, 0xa6, 0x80);
	write_both(reference, 0xb6, 0x0d);
	write_both(reference, 0xa7, 0xa0);
	write_both(reference, 0xb7, 0x0d);
	write_both(reference, 0xa8, 0xc0);
	write_both(reference, 0xb8, 0x0d);
	write_both(reference, 0xbd, 0x3f);
}

int
main(void)
{
	void *reference = PalMameOpl2Reference::ym3812_init(
		NULL,
		PAL_MAME_OPL2_CLOCK_HZ,
		PAL_MAME_OPL2_SAMPLE_RATE
	);
	if (reference == NULL)
	{
		fprintf(stderr, "reference YM3812 initialization failed\n");
		return 1;
	}

	PalMameOpl2_Init();
	program_melodic_voice(reference);

	uint32_t hash = 2166136261u;
	unsigned int nonzero = 0;
	unsigned int sample_index = 0;
	for (unsigned int tick = 0; tick < 140; ++tick)
	{
		if (tick == 28)
		{
			write_both(reference, 0xa0, 0x34);
			write_both(reference, 0xb0, 0x32);
		}
		else if (tick == 52)
		{
			program_rhythm(reference);
		}
		else if (tick == 96)
		{
			write_both(reference, 0xbd, 0x00);
			write_both(reference, 0xb0, 0x12);
		}

		PalMameOpl2Reference::ym3812_update_one(reference, reference_samples, 315);
		PalMameOpl2_Render(fixed_samples, 315);
		for (size_t i = 0; i < 315; ++i, ++sample_index)
		{
			const uint16_t sample = static_cast<uint16_t>(fixed_samples[i]);
			if (fixed_samples[i] != reference_samples[i])
			{
				fprintf(
					stderr,
					"PCM mismatch at sample %u: fixed=%d reference=%d\n",
					sample_index,
					fixed_samples[i],
					reference_samples[i]
				);
				PalMameOpl2Reference::ym3812_shutdown(reference);
				return 1;
			}
			nonzero += sample != 0;
			hash ^= sample & 0xffu;
			hash *= 16777619u;
			hash ^= sample >> 8;
			hash *= 16777619u;
		}
	}

	PalMameOpl2Reference::ym3812_shutdown(reference);
	if (nonzero == 0 || PalMameOpl2_TableBytes() != 25706u)
	{
		fprintf(stderr, "invalid static OPL2 smoke result\n");
		return 1;
	}

	printf(
		"pal_mame_opl2_static_smoke: state=%zu tables=%zu "
		"samples=%u nonzero=%u hash=%08x\n",
		PalMameOpl2_StateBytes(),
		PalMameOpl2_TableBytes(),
		sample_index,
		nonzero,
		hash
	);
	return 0;
}
