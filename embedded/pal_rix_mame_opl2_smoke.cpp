/*
 * Fixed-memory pack -> RIX sequencer -> MAME OPL2 PCM smoke.
 */

#include "pal_mame_opl2_static.h"
#include "pal_music_cache.h"
#include "../adplug/rix.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

class PalRixSmokeOpl final : public Copl
{
public:
	PalRixSmokeOpl() : Copl(TYPE_OPL2) {}

	void init() override
	{
		PalMameOpl2_Reset();
	}

	void write(int reg, int value) override
	{
		PalMameOpl2_Write(
			static_cast<uint8_t>(reg),
			static_cast<uint8_t>(value)
		);
	}

	void update(short *samples, int frames) override
	{
		if (samples != NULL && frames > 0)
		{
			PalMameOpl2_Render(
				reinterpret_cast<int16_t *>(samples),
				static_cast<size_t>(frames)
			);
		}
	}

	bool getstereo() override
	{
		return false;
	}
};

static int16_t pal_rix_smoke_tick[315];

int
main(int argc, char **argv)
{
	const char *path;
	int expected_tracks;
	int fd;
	struct stat status;
	const uint8_t *image;
	PalPack pack;
	uint16_t chunk_count;
	unsigned mapped_tracks = 0;
	unsigned audible_tracks = 0;
	uint64_t total_nonzero = 0;

	if (argc != 3)
	{
		fprintf(stderr, "usage: %s PAL_NOR.PAK EXPECTED_TRACKS\n", argv[0]);
		return 2;
	}
	path = argv[1];
	expected_tracks = atoi(argv[2]);
	if (expected_tracks <= 0)
	{
		fprintf(stderr, "invalid expected track count\n");
		return 2;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0 ||
		fstat(fd, &status) != 0 ||
		status.st_size <= 0 ||
		static_cast<uint64_t>(status.st_size) > UINT32_MAX)
	{
		if (fd >= 0)
		{
			close(fd);
		}
		fprintf(stderr, "cannot open pack: %s\n", path);
		return 3;
	}
	image = static_cast<const uint8_t *>(
		mmap(
			NULL,
			static_cast<size_t>(status.st_size),
			PROT_READ,
			MAP_PRIVATE,
			fd,
			0
		)
	);
	close(fd);
	if (image == MAP_FAILED ||
		!PalPack_OpenConst(
			&pack,
			image,
			static_cast<uint32_t>(status.st_size)
		) ||
		!PalPack_GetChunkCount(
			&pack,
			PAL_PACK_ARCHIVE_MUS,
			&chunk_count
		))
	{
		fprintf(stderr, "invalid pack or missing MUS archive\n");
		return 4;
	}

	PalMameOpl2_Init();
	PalRixSmokeOpl opl;
	CrixPlayer decoder(&opl);

	for (uint16_t track_number = 0;
		 track_number < chunk_count;
		 track_number++)
	{
		PalMusicTrack track = {};
		uint64_t nonzero = 0;
		unsigned ticks = 0;
		bool completed = false;

		if (!PalMusic_MapMus(&pack, track_number, &track))
		{
			continue;
		}
		if (!decoder.load_buffer(track.data, track.size))
		{
			fprintf(stderr, "invalid RIX track: %u\n", track_number);
			return 5;
		}
		mapped_tracks++;

		for (; ticks < 42001; ticks++)
		{
			if (!decoder.update())
			{
				completed = true;
				break;
			}
			PalMameOpl2_Render(
				pal_rix_smoke_tick,
				sizeof(pal_rix_smoke_tick) /
					sizeof(pal_rix_smoke_tick[0])
			);
			for (size_t i = 0;
				 i < sizeof(pal_rix_smoke_tick) /
					 sizeof(pal_rix_smoke_tick[0]);
				 i++)
			{
				nonzero += pal_rix_smoke_tick[i] != 0;
			}
		}
		if (!completed || ticks == 0 || nonzero == 0)
		{
			fprintf(
				stderr,
				"incomplete/silent RIX track: %u ticks=%u\n",
				track_number,
				ticks
			);
			return 6;
		}
		audible_tracks++;
		total_nonzero += nonzero;
	}

	munmap(
		const_cast<uint8_t *>(image),
		static_cast<size_t>(status.st_size)
	);
	if (mapped_tracks != static_cast<unsigned>(expected_tracks) ||
		audible_tracks != mapped_tracks)
	{
		fprintf(
			stderr,
			"RIX track count mismatch: mapped=%u audible=%u expected=%d\n",
			mapped_tracks,
			audible_tracks,
			expected_tracks
		);
		return 7;
	}

	printf(
		"pal_rix_mame_opl2_smoke: chunks=%u mapped=%u "
		"audible=%u nonzero=%llu\n",
		chunk_count,
		mapped_tracks,
		audible_tracks,
		static_cast<unsigned long long>(total_nonzero)
	);
	return 0;
}
