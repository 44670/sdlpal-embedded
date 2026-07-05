#include "pal_music_cache.h"

#include <stddef.h>

static bool has_midi_header(const uint8_t *data, uint32_t size)
{
    return size >= 14u &&
           data[0] == 'M' &&
           data[1] == 'T' &&
           data[2] == 'h' &&
           data[3] == 'd';
}

static bool has_rix_header(const uint8_t *data, uint32_t size)
{
    return size >= 16u && data[0] == 0xaau && data[1] == 0x55u;
}

static bool map_music_track(
    const PalPack *pack,
    uint16_t archive_id,
    uint16_t track_num,
    uint16_t music_format,
    bool (*header_ok)(const uint8_t *, uint32_t),
    PalMusicTrack *track)
{
    PalPackSpan span;

    if (track == NULL) {
        return false;
    }
    track->data = NULL;
    track->size = 0;
    track->track_num = 0;
    track->format = 0;

    if (!PalPack_MapConst(pack, archive_id, track_num, &span)) {
        return false;
    }
    if (span.data == NULL || span.size == 0 || span.format != PAL_PACK_FORMAT_NATIVE) {
        return false;
    }
    if (!header_ok(span.data, span.size)) {
        return false;
    }

    track->data = span.data;
    track->size = span.size;
    track->track_num = track_num;
    track->format = music_format;
    return true;
}

bool PalMusic_MapMidi(const PalPack *nor_pack, uint16_t track_num, PalMusicTrack *track)
{
    return map_music_track(nor_pack, PAL_PACK_ARCHIVE_MIDI, track_num, PAL_MUSIC_FORMAT_MIDI, has_midi_header, track);
}

bool PalMusic_MapMus(const PalPack *nor_pack, uint16_t track_num, PalMusicTrack *track)
{
    return map_music_track(nor_pack, PAL_PACK_ARCHIVE_MUS, track_num, PAL_MUSIC_FORMAT_RIX, has_rix_header, track);
}
