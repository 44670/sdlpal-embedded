#include "../audio.h"
#include "../global.h"
#include "../palcfg.h"
#include "../players.h"
#include "../embedded/pal_music_cache.h"
#include "../embedded/pal_pack.h"
#include "../adplug/dosbox_opls.h"
#include "../adplug/rix.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PAL_CONTRACT_RIX_TICK_FRAMES ((PAL_MAX_SAMPLERATE + 69) / 70)

#if defined(__GNUC__)
#define PAL_CONTRACT_RIX_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#else
#define PAL_CONTRACT_RIX_SRAM
#endif

class PalContractOpl2 : public Copl {
public:
    PalContractOpl2() : Copl(TYPE_OPL2), sample_rate(22050) { memset(&chip, 0, sizeof(chip)); }

    void set_rate(uint32_t rate)
    {
        sample_rate = rate != 0 ? rate : 22050;
        init();
    }

    void init(void) { DBOPL2::adlib_init(&chip, sample_rate); }
    void write(int reg, int val) { DBOPL2::adlib_write(&chip, (uint32_t)reg & 0xffu, (uint8_t)val); }
    void update(short *buf, int samples) { DBOPL2::adlib_getsample(&chip, buf, samples); }
    bool getstereo() { return false; }

private:
    DBOPL2::opl_chip chip;
    uint32_t sample_rate;
};

static PalPack pal_contract_rix_nor_pack;
static PalMusicTrack pal_contract_rix_track;
static AUDIOPLAYER pal_contract_rix_player;
static PalContractOpl2 pal_contract_rix_opl;
static CrixPlayer pal_contract_rix_decoder(&pal_contract_rix_opl);
static bool pal_contract_rix_pack_ready;
static bool pal_contract_rix_pack_tried;
static bool pal_contract_rix_ready;
static int pal_contract_rix_rate;
static int pal_contract_rix_tick_frames;
static int pal_contract_rix_tick_pos;
static uint8_t pal_sram_contract_rix_tick[PAL_CONTRACT_RIX_TICK_FRAMES * sizeof(int16_t)] PAL_CONTRACT_RIX_SRAM;

static bool
PalContract_RixMapPackPath(
    PalPack *pack,
    const char *path
)
{
    int fd;
    struct stat st;
    const uint8_t *image;

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > UINT32_MAX) {
        close(fd);
        return false;
    }

    image = (const uint8_t *)mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (image == MAP_FAILED) {
        return false;
    }
    if (!PalPack_OpenConst(pack, image, (uint32_t)st.st_size)) {
        munmap((void *)image, (size_t)st.st_size);
        return false;
    }
    return true;
}

static bool
PalContract_RixOpenNorPack(void)
{
    const char *path;

    if (pal_contract_rix_pack_tried) {
        return pal_contract_rix_pack_ready;
    }
    pal_contract_rix_pack_tried = true;

    path = getenv("PAL_CONTRACT_NOR_PACK");
    if (PalContract_RixMapPackPath(&pal_contract_rix_nor_pack, path) ||
        PalContract_RixMapPackPath(&pal_contract_rix_nor_pack, "/tmp/pal_nor_default.pak")) {
        pal_contract_rix_pack_ready = true;
    }
    return pal_contract_rix_pack_ready;
}

static void
PalContract_RixStop(void)
{
    pal_contract_rix_player.iMusic = -1;
    pal_contract_rix_ready = false;
    pal_contract_rix_tick_frames = 0;
    pal_contract_rix_tick_pos = 0;
}

static VOID
PalContract_RixShutdown(
    VOID *player
)
{
    (void)player;
    PalContract_RixStop();
}

static BOOL
PalContract_RixPlay(
    VOID *player,
    INT music_num,
    BOOL loop,
    FLOAT fade_time
)
{
    LPAUDIOPLAYER audio_player = (LPAUDIOPLAYER)player;
    uint32_t rate;

    (void)fade_time;
    if (audio_player == NULL) {
        return FALSE;
    }

    audio_player->fLoop = loop;
    if (music_num <= 0) {
        PalContract_RixStop();
        return TRUE;
    }
    if (!PalContract_RixOpenNorPack() ||
        !PalMusic_MapMus(&pal_contract_rix_nor_pack, (uint16_t)music_num, &pal_contract_rix_track) ||
        !pal_contract_rix_decoder.load_buffer(pal_contract_rix_track.data, pal_contract_rix_track.size)) {
        PalContract_RixStop();
        return FALSE;
    }

    rate = gConfig.iSampleRate > 0 ? (uint32_t)gConfig.iSampleRate : 22050u;
    if (rate > PAL_MAX_SAMPLERATE) {
        rate = PAL_MAX_SAMPLERATE;
    }
    pal_contract_rix_rate = (int)rate;
    pal_contract_rix_opl.set_rate(rate);
    pal_contract_rix_decoder.rewind(0);
    pal_contract_rix_player.iMusic = music_num;
    pal_contract_rix_ready = true;
    pal_contract_rix_tick_frames = 0;
    pal_contract_rix_tick_pos = 0;
    return TRUE;
}

static bool
PalContract_RixRenderTick(void)
{
    int frames;

    if (!pal_contract_rix_decoder.update()) {
        if (!pal_contract_rix_player.fLoop) {
            PalContract_RixStop();
            return false;
        }
        pal_contract_rix_decoder.rewindReInit(0, false);
        if (!pal_contract_rix_decoder.update()) {
            PalContract_RixStop();
            return false;
        }
    }

    frames = pal_contract_rix_rate / 70;
    if (frames <= 0) {
        frames = 1;
    }
    if (frames > PAL_CONTRACT_RIX_TICK_FRAMES) {
        frames = PAL_CONTRACT_RIX_TICK_FRAMES;
    }

    pal_contract_rix_opl.update((short *)pal_sram_contract_rix_tick, frames);
    pal_contract_rix_tick_frames = frames;
    pal_contract_rix_tick_pos = 0;
    return true;
}

static VOID
PalContract_RixFillBuffer(
    VOID *player,
    LPBYTE stream,
    INT len
)
{
    int channels;
    int frame_bytes;
    int frames;
    int frame;
    int16_t *dst;
    const int16_t *tick;

    (void)player;
    if (!pal_contract_rix_ready || stream == NULL || len <= 0) {
        return;
    }

    channels = gAudioDevice.spec.channels > 0 ? gAudioDevice.spec.channels : 1;
    frame_bytes = channels * (int)sizeof(int16_t);
    frames = len / frame_bytes;
    dst = (int16_t *)stream;
    tick = (const int16_t *)pal_sram_contract_rix_tick;

    for (frame = 0; frame < frames && pal_contract_rix_ready; frame++) {
        int channel;
        int16_t sample;

        if (pal_contract_rix_tick_pos >= pal_contract_rix_tick_frames &&
            !PalContract_RixRenderTick()) {
            break;
        }

        sample = tick[pal_contract_rix_tick_pos++];
        for (channel = 0; channel < channels; channel++) {
            dst[frame * channels + channel] = sample;
        }
    }
}

extern "C" LPAUDIOPLAYER
RIX_Init(
    LPCSTR szFileName
)
{
    (void)szFileName;
    pal_contract_rix_player.iMusic = -1;
    pal_contract_rix_player.fLoop = FALSE;
    pal_contract_rix_player.Shutdown = PalContract_RixShutdown;
    pal_contract_rix_player.Play = PalContract_RixPlay;
    pal_contract_rix_player.FillBuffer = PalContract_RixFillBuffer;
    return &pal_contract_rix_player;
}
