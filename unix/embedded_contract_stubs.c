#include "aviplay.h"
#include "battle.h"
#include "font.h"
#include "global.h"
#include "input.h"
#include "palette.h"
#include "players.h"
#include "resampler.h"
#include "text.h"
#include "ui.h"
#include "util.h"
#include "video.h"
#include "audio.h"
#ifdef PAL_EXTREME_TWO_SCREENS
#include "../embedded/pal_font10_cache.h"
#include "../embedded/pal_native_ui.h"
#else
#include "../embedded/pal_font_cache.h"
#endif
#include "../embedded/pal_music_cache.h"
#include "../embedded/pal_pack.h"
#include "../embedded/pal_text_cache.h"

#ifndef PAL_CONTRACT_TARGET_PACK_PROVIDER
#include <fcntl.h>
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(PAL_CONTRACT_NO_AUDIO) || defined(PAL_CONTRACT_NO_SFX)
#define PAL_CONTRACT_DISABLE_SFX 1
#endif
#ifndef PAL_CONTRACT_TARGET_PACK_PROVIDER
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <wchar.h>

#define PAL_CONTRACT_TEXT_SLOTS 8u
#define PAL_CONTRACT_TEXT_CHARS 64u
#ifndef PAL_CONTRACT_DISABLE_SFX
# define PAL_CONTRACT_SFX_BYTES (212u * 1024u)
# define PAL_CONTRACT_SFX_MAGIC 0x58465350u
# define PAL_CONTRACT_SFX_VERSION 1u
# define PAL_CONTRACT_SFX_HEADER_SIZE 24u
# define PAL_CONTRACT_SFX_RATE 22050u
#endif

#define FONT_COLOR_DEFAULT 0x4F
#define FONT_COLOR_YELLOW 0x2D
#define FONT_COLOR_RED 0x1A
#define FONT_COLOR_CYAN 0x8D
#define FONT_COLOR_CYAN_ALT 0x8C
#define FONT_COLOR_RED_ALT 0x17

#if defined(__GNUC__)
#define PAL_CONTRACT_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#define PAL_CONTRACT_PSRAM __attribute__((section(".bss.pal_psram"), aligned(4)))
#else
#define PAL_CONTRACT_SRAM
#define PAL_CONTRACT_PSRAM
#endif

static PalPack pal_contract_nor_pack;
static PalPack pal_contract_tf_pack;
static PalTextCache pal_contract_text;
#ifdef PAL_EXTREME_TWO_SCREENS
static PalFont10Cache pal_contract_font10;
static PalNativeUiDialogLayout pal_contract_dialog_layout;
#else
static PalFontCache pal_contract_font;
#endif
static bool pal_contract_pack_ready;
static bool pal_contract_pack_tried;
static bool pal_contract_tf_pack_ready;
static bool pal_contract_tf_pack_tried;
static bool pal_contract_text_ready;
static bool pal_contract_font_ready;
#ifndef PAL_CONTRACT_EXTERNAL_RIX
static PalMusicTrack pal_contract_music_track;
static AUDIOPLAYER pal_contract_music_player;
#endif
#ifndef PAL_CONTRACT_DISABLE_SFX
static AUDIOPLAYER pal_contract_sound_player;
static const uint8_t *pal_contract_sfx_pcm;
static uint32_t pal_contract_sfx_samples;
static uint32_t pal_contract_sfx_cursor;
static bool pal_contract_sfx_active;
#endif
static uint8_t pal_sram_contract_text_slots
    [PAL_CONTRACT_TEXT_SLOTS][PAL_CONTRACT_TEXT_CHARS * sizeof(WCHAR)] PAL_CONTRACT_SRAM;
static uint8_t pal_sram_contract_dialog_palette[256u * sizeof(SDL_Color)] PAL_CONTRACT_SRAM;
#ifndef PAL_CONTRACT_DISABLE_SFX
static uint8_t pal_psram_contract_sfx[PAL_CONTRACT_SFX_BYTES] PAL_CONTRACT_PSRAM;
#endif
static unsigned int pal_contract_text_slot;
static WCHAR pal_empty_text[1];

static uint16_t
PalContract_ReadLe16(
    const uint8_t *p
)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t
PalContract_ReadLe32(
    const uint8_t *p
)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

#ifdef PAL_CONTRACT_TARGET_PACK_PROVIDER
bool PalContract_TargetOpenNorPack(PalPack *pack);
bool PalContract_TargetOpenTfPack(PalPack *pack);
#endif

#ifndef PAL_CONTRACT_DISABLE_SFX
static int16_t
PalContract_ReadI16(
    const uint8_t *p
)
{
    return (int16_t)PalContract_ReadLe16(p);
}

static int16_t
PalContract_ClampI16(
    int32_t sample
)
{
    if (sample > 32767) {
        return 32767;
    }
    if (sample < -32768) {
        return -32768;
    }
    return (int16_t)sample;
}
#endif

#ifndef PAL_CONTRACT_TARGET_PACK_PROVIDER
static bool
PalContract_MapPackPath(
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
#endif

static bool
PalContract_OpenNorPack(
    void
)
{
#ifdef PAL_CONTRACT_TARGET_PACK_PROVIDER
    if (pal_contract_pack_tried) {
        return pal_contract_pack_ready;
    }
    pal_contract_pack_tried = true;
    pal_contract_pack_ready = PalContract_TargetOpenNorPack(&pal_contract_nor_pack);
    return pal_contract_pack_ready;
#else
    const char *path;

    if (pal_contract_pack_tried) {
        return pal_contract_pack_ready;
    }
    pal_contract_pack_tried = true;

    path = getenv("PAL_CONTRACT_NOR_PACK");
    if (PalContract_MapPackPath(&pal_contract_nor_pack, path) ||
        PalContract_MapPackPath(&pal_contract_nor_pack, "/tmp/pal_nor_default.pak")) {
        pal_contract_pack_ready = true;
    }
    return pal_contract_pack_ready;
#endif
}

static bool
PalContract_OpenTfPack(
    void
)
{
#ifdef PAL_CONTRACT_TARGET_PACK_PROVIDER
    if (pal_contract_tf_pack_tried) {
        return pal_contract_tf_pack_ready;
    }
    pal_contract_tf_pack_tried = true;
    pal_contract_tf_pack_ready = PalContract_TargetOpenTfPack(&pal_contract_tf_pack);
    return pal_contract_tf_pack_ready;
#else
    const char *path;

    if (pal_contract_tf_pack_tried) {
        return pal_contract_tf_pack_ready;
    }
    pal_contract_tf_pack_tried = true;

    path = getenv("PAL_CONTRACT_TF_PACK");
    if (PalContract_MapPackPath(&pal_contract_tf_pack, path) ||
        PalContract_MapPackPath(&pal_contract_tf_pack, "/tmp/pal_tf_default.pak")) {
        pal_contract_tf_pack_ready = true;
    }
    return pal_contract_tf_pack_ready;
#endif
}

static LPWSTR
PalContract_Utf16LeToSlot(
    const uint8_t *utf16le,
    uint32_t byte_size
)
{
    LPWSTR out;
    uint32_t count;
    uint32_t i;

    if (utf16le == NULL || byte_size == 0) {
        return pal_empty_text;
    }

    out = (LPWSTR)pal_sram_contract_text_slots[pal_contract_text_slot];
    pal_contract_text_slot = (pal_contract_text_slot + 1u) % PAL_CONTRACT_TEXT_SLOTS;

    count = byte_size / 2u;
    if (count >= PAL_CONTRACT_TEXT_CHARS) {
        count = PAL_CONTRACT_TEXT_CHARS - 1u;
    }
    for (i = 0; i < count; i++) {
        out[i] = (WCHAR)PalContract_ReadLe16(utf16le + i * 2u);
    }
    out[count] = 0;
    return out;
}

#ifndef PAL_EXTREME_TWO_SCREENS
static void
PalContract_DrawGlyph16(
    const uint8_t *glyph,
    SDL_Surface *surface,
    int x,
    int y,
    uint8_t color
)
{
    int row;
    uint8_t *pixels;

    if (glyph == NULL || surface == NULL || surface->pixels == NULL) {
        return;
    }
    pixels = (uint8_t *)surface->pixels;
    for (row = 0; row < 16; row++) {
        int col;
        int dst_y = y + row;
        uint8_t left = glyph[row * 2];
        uint8_t right = glyph[row * 2 + 1];
        uint8_t *dst;

        if (dst_y < 0 || dst_y >= surface->h) {
            continue;
        }
        dst = pixels + dst_y * surface->pitch;
        for (col = 0; col < 8; col++) {
            int dst_x = x + col;
            if (dst_x >= 0 && dst_x < surface->w && (left & (uint8_t)(1u << (7 - col)))) {
                dst[dst_x] = color;
            }
        }
        for (col = 0; col < 8; col++) {
            int dst_x = x + 8 + col;
            if (dst_x >= 0 && dst_x < surface->w && (right & (uint8_t)(1u << (7 - col)))) {
                dst[dst_x] = color;
            }
        }
    }
}
#endif

#ifndef PAL_CONTRACT_DISABLE_SFX
static bool
PalContract_OpenSfx(
    const uint8_t *payload,
    uint32_t payload_size
)
{
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t pcm_offset;
    uint32_t pcm_size;

    if (payload == NULL || payload_size < PAL_CONTRACT_SFX_HEADER_SIZE) {
        return false;
    }

    version = PalContract_ReadLe16(payload + 4u);
    header_size = PalContract_ReadLe16(payload + 6u);
    sample_rate = PalContract_ReadLe32(payload + 8u);
    sample_count = PalContract_ReadLe32(payload + 12u);
    pcm_offset = PalContract_ReadLe32(payload + 16u);
    pcm_size = PalContract_ReadLe32(payload + 20u);

    if (PalContract_ReadLe32(payload) != PAL_CONTRACT_SFX_MAGIC ||
        version != PAL_CONTRACT_SFX_VERSION ||
        header_size != PAL_CONTRACT_SFX_HEADER_SIZE ||
        sample_rate != PAL_CONTRACT_SFX_RATE ||
        (pcm_size & 1u) != 0 ||
        pcm_size != sample_count * 2u ||
        pcm_offset > payload_size ||
        pcm_size > payload_size - pcm_offset) {
        return false;
    }

    pal_contract_sfx_pcm = payload + pcm_offset;
    pal_contract_sfx_samples = sample_count;
    pal_contract_sfx_cursor = 0;
    pal_contract_sfx_active = true;
    return true;
}
#endif

static VOID
PalContract_PlayerShutdown(
    VOID *player
)
{
    (void)player;
}

#ifndef PAL_CONTRACT_EXTERNAL_RIX
static BOOL
PalContract_MusicPlay(
    VOID *player,
    INT music_num,
    BOOL loop,
    FLOAT fade_time
)
{
    LPAUDIOPLAYER audio_player = (LPAUDIOPLAYER)player;

    (void)fade_time;
    if (audio_player == NULL) {
        return FALSE;
    }
    audio_player->iMusic = -1;
    audio_player->fLoop = loop;
    if (music_num <= 0) {
        return TRUE;
    }
    if (!PalContract_OpenNorPack() ||
        !PalMusic_MapMus(&pal_contract_nor_pack, (uint16_t)music_num, &pal_contract_music_track)) {
        return FALSE;
    }
    audio_player->iMusic = music_num;
    return TRUE;
}

static VOID
PalContract_MusicFillBuffer(
    VOID *player,
    LPBYTE stream,
    INT len
)
{
    (void)player;
    (void)stream;
    (void)len;
}
#endif

#ifndef PAL_CONTRACT_DISABLE_SFX
static BOOL
PalContract_SoundPlay(
    VOID *player,
    INT sound_num,
    BOOL loop,
    FLOAT fade_time
)
{
    PalPackSpan span;
    LPAUDIOPLAYER audio_player = (LPAUDIOPLAYER)player;

    (void)loop;
    (void)fade_time;
    if (audio_player == NULL || sound_num < 0) {
        return FALSE;
    }
    if (!PalContract_OpenTfPack() ||
        !PalPack_MapConst(&pal_contract_tf_pack, PAL_PACK_ARCHIVE_SFX, (uint16_t)sound_num, &span) ||
        span.format != PAL_PACK_FORMAT_SFX_PCM16 ||
        span.size > PAL_CONTRACT_SFX_BYTES) {
        return FALSE;
    }

    memcpy(pal_psram_contract_sfx, span.data, span.size);
    if (!PalContract_OpenSfx(pal_psram_contract_sfx, span.size)) {
        return FALSE;
    }
    audio_player->iMusic = sound_num;
    return TRUE;
}

static VOID
PalContract_SoundFillBuffer(
    VOID *player,
    LPBYTE stream,
    INT len
)
{
    int channels = gAudioDevice.spec.channels > 0 ? gAudioDevice.spec.channels : 1;
    int frames = len / ((int)sizeof(int16_t) * channels);
    int16_t *dst = (int16_t *)stream;
    int frame;

    (void)player;
    if (!pal_contract_sfx_active || pal_contract_sfx_pcm == NULL || stream == NULL || len <= 0) {
        return;
    }

    for (frame = 0; frame < frames && pal_contract_sfx_cursor < pal_contract_sfx_samples; frame++, pal_contract_sfx_cursor++) {
        int channel;
        int16_t sample = PalContract_ReadI16(pal_contract_sfx_pcm + pal_contract_sfx_cursor * 2u);
        for (channel = 0; channel < channels; channel++) {
            int index = frame * channels + channel;
            dst[index] = PalContract_ClampI16((int32_t)dst[index] + sample);
        }
    }
    if (pal_contract_sfx_cursor >= pal_contract_sfx_samples) {
        pal_contract_sfx_active = false;
    }
}
#endif

void resampler_init(void)
{
}

char *font_offset_x;
char *font_offset_y;
TEXTLIB g_TextLib;
LPWSTR g_rcCredits[12];
BOOL g_fUpdatedInBattle;

int PAL_InitFont(const CONFIGURATION *cfg)
{
    (void)cfg;
#ifdef PAL_EXTREME_TWO_SCREENS
    pal_contract_font_ready = PalContract_OpenNorPack() &&
        PalFont10_Open(&pal_contract_nor_pack, &pal_contract_font10);
#if defined(PAL_EXTREME_CHAPTER_CACHE)
    /*
     * The default Cardputer application is data-set independent.  The pack
     * parser validates the complete FONT10 image; only the fixed rendering
     * geometry is an application ABI.
     */
    pal_contract_font_ready = pal_contract_font_ready &&
        pal_contract_font10.cell_width == 10u &&
        pal_contract_font10.cell_height == 10u;
#else
    pal_contract_font_ready = pal_contract_font_ready &&
        PalNativeUi_Font10IdentityMatches(
            pal_contract_font10.glyph_count,
            pal_contract_font10.size,
            pal_contract_font10.payload_crc32,
            pal_contract_font10.cell_width,
            pal_contract_font10.cell_height,
            pal_contract_font10.ascent,
            pal_contract_font10.descent);
#endif
#else
    pal_contract_font_ready = PalContract_OpenNorPack() &&
        PalFont_Open(&pal_contract_nor_pack, &pal_contract_font);
#endif
    return pal_contract_font_ready ? 0 : -1;
}

void PAL_FreeFont(void)
{
}

void PAL_DrawCharOnSurface(uint16_t wChar, SDL_Surface *lpSurface, PAL_POS pos, uint8_t bColor, BOOL fUse8x8Font)
{
#ifdef PAL_EXTREME_TWO_SCREENS
    PalFont10Glyph glyph10;

    (void)fUse8x8Font;
    if (pal_contract_font_ready &&
        lpSurface != NULL && lpSurface->pixels != NULL &&
        PalFont10_FindGlyph(&pal_contract_font10, wChar, &glyph10)) {
        (void)PalNativeUi_DrawFont10Glyph(
            glyph10.bitmap,
            (uint8_t *)lpSurface->pixels,
            (uint16_t)lpSurface->pitch,
            (uint16_t)lpSurface->w,
            (uint16_t)lpSurface->h,
            (int16_t)PAL_X(pos),
            (int16_t)PAL_Y(pos),
            bColor);
    }
#else
    const uint8_t *glyph;
    uint16_t glyph_bytes;

    (void)fUse8x8Font;
    if (!pal_contract_font_ready ||
        !PalFont_FindGlyph(&pal_contract_font, wChar, &glyph, &glyph_bytes) ||
        glyph_bytes < PAL_FONT_GLYPH_BYTES) {
        return;
    }
    PalContract_DrawGlyph16(glyph, lpSurface, PAL_X(pos), PAL_Y(pos), bColor);
#endif
}

int PAL_CharWidth(uint16_t wChar)
{
#ifdef PAL_EXTREME_TWO_SCREENS
    PalFont10Glyph glyph10;
    if (pal_contract_font_ready &&
        PalFont10_FindGlyph(&pal_contract_font10, wChar, &glyph10)) {
        return glyph10.advance;
    }
    return PAL_NATIVE_UI_GENERATED_FONT_CELL_WIDTH;
#endif
    return (wChar < 0x80) ? 8 : 16;
}

int PAL_FontHeight(void)
{
#ifdef PAL_EXTREME_TWO_SCREENS
    return PAL_NATIVE_UI_GENERATED_FONT_CELL_HEIGHT;
#endif
    return 16;
}

#ifdef PAL_EXTREME_TWO_SCREENS
static bool
PalContract_NativeResetDefaultDialogLayout(
    void
)
{
    if (!PalNativeUi_GetDialogLayout(
            false, false, &pal_contract_dialog_layout) ||
        pal_contract_dialog_layout.page_lines == 0u ||
        pal_contract_dialog_layout.line_height == 0u) {
        return false;
    }
    g_TextLib.posDialogTitle = PAL_XY(
        pal_contract_dialog_layout.title_x,
        pal_contract_dialog_layout.title_y);
    g_TextLib.posDialogText = PAL_XY(
        pal_contract_dialog_layout.text.x,
        pal_contract_dialog_layout.text.y);
    g_TextLib.bDialogPosition = kDialogUpper;
    return true;
}
#endif

INT PAL_InitText(VOID)
{
    PalPackSpan span;
    uint32_t icon_bytes;

    pal_contract_text_ready = PalContract_OpenNorPack() && PalText_Open(&pal_contract_nor_pack, &pal_contract_text);
    if (!pal_contract_text_ready) {
        return -1;
    }
    g_TextLib.nWords = pal_contract_text.word_count;
    g_TextLib.nMsgs = pal_contract_text.message_count;
    g_TextLib.bCurrentFontColor = FONT_COLOR_DEFAULT;
    g_TextLib.bIcon = 0;
    g_TextLib.posIcon = 0;
    g_TextLib.nCurrentDialogLine = 0;
    g_TextLib.iDelayTime = 3;
    g_TextLib.posDialogTitle = PAL_XY(12, 8);
    g_TextLib.posDialogText = PAL_XY(44, 26);
    g_TextLib.bDialogPosition = kDialogUpper;
    g_TextLib.fUserSkip = FALSE;
    g_TextLib.fPlayingRNG = FALSE;
    memset(g_TextLib.bufDialogIcons, 0, sizeof(g_TextLib.bufDialogIcons));
#ifdef PAL_EXTREME_TWO_SCREENS
    if (!PalContract_NativeResetDefaultDialogLayout()) {
        return -1;
    }
#endif
    if (PalPack_MapConst(&pal_contract_nor_pack, PAL_PACK_ARCHIVE_DATA, 12, &span) &&
        span.data != NULL && span.size > 0) {
        icon_bytes = span.size < sizeof(g_TextLib.bufDialogIcons) ?
            span.size : (uint32_t)sizeof(g_TextLib.bufDialogIcons);
        memcpy(g_TextLib.bufDialogIcons, span.data, icon_bytes);
    }
    return 0;
}

VOID PAL_FreeText(VOID)
{
}

LPCWSTR PAL_GetWord(int iNumWord)
{
    const uint8_t *utf16le;
    uint32_t byte_size;

    if (!pal_contract_text_ready || iNumWord < 0 ||
        !PalText_GetWord(&pal_contract_text, (uint16_t)iNumWord, &utf16le, &byte_size)) {
        return pal_empty_text;
    }
    return PalContract_Utf16LeToSlot(utf16le, byte_size);
}

LPCWSTR PAL_GetMsg(int iNumMsg)
{
    const uint8_t *utf16le;
    uint32_t byte_size;

    if (!pal_contract_text_ready || iNumMsg < 0 ||
        !PalText_GetMessage(&pal_contract_text, (uint16_t)iNumMsg, &utf16le, &byte_size)) {
        return pal_empty_text;
    }
    return PalContract_Utf16LeToSlot(utf16le, byte_size);
}

int PAL_GetMsgNum(int iIndex, int iSpan, int iOrder)
{
    (void)iSpan;
    if (!pal_contract_text_ready || iIndex < 0 || iIndex >= g_TextLib.nMsgs || iOrder != 0) {
        return -1;
    }
    return iIndex;
}

LPWSTR PAL_UnescapeText(LPCWSTR lpszText)
{
    return (LPWSTR)(lpszText != NULL ? lpszText : pal_empty_text);
}

VOID PAL_DrawText(LPCWSTR lpszText, PAL_POS pos, BYTE bColor, BOOL fShadow, BOOL fUpdate, BOOL fUse8x8Font)
{
    PAL_DrawTextUnescape(lpszText, pos, bColor, fShadow, fUpdate, fUse8x8Font, TRUE);
}

VOID PAL_DrawTextUnescape(
    LPCWSTR lpszText,
    PAL_POS pos,
    BYTE bColor,
    BOOL fShadow,
    BOOL fUpdate,
    BOOL fUse8x8Font,
    BOOL fUnescape)
{
    (void)fUnescape;
    if (lpszText == NULL || gpScreen == NULL) {
        return;
    }
    while (*lpszText != 0) {
        int x = PAL_X(pos);
        int y = PAL_Y(pos);
        if (fShadow) {
            PAL_DrawCharOnSurface((uint16_t)*lpszText, gpScreen, PAL_XY(x + 1, y), 0, fUse8x8Font);
            PAL_DrawCharOnSurface((uint16_t)*lpszText, gpScreen, PAL_XY(x, y + 1), 0, fUse8x8Font);
            PAL_DrawCharOnSurface((uint16_t)*lpszText, gpScreen, PAL_XY(x + 1, y + 1), 0, fUse8x8Font);
        }
        PAL_DrawCharOnSurface((uint16_t)*lpszText, gpScreen, pos, bColor, fUse8x8Font);
#ifdef PAL_EXTREME_TWO_SCREENS
        pos = PAL_XY(x + PAL_CharWidth((uint16_t)*lpszText), y);
#else
        pos = PAL_XY(x + (fUse8x8Font ? 8 : PAL_CharWidth((uint16_t)*lpszText)), y);
#endif
        lpszText++;
    }
    if (fUpdate) {
        VIDEO_UpdateScreen(NULL);
    }
}

VOID PAL_DialogSetDelayTime(INT iDelayTime)
{
    g_TextLib.iDelayTime = iDelayTime;
}

VOID PAL_StartDialog(BYTE bDialogLocation, BYTE bFontColor, INT iNumCharFace, BOOL fPlayingRNG)
{
    PAL_StartDialogWithOffset(bDialogLocation, bFontColor, iNumCharFace, fPlayingRNG, 0, 0);
}

VOID PAL_StartDialogWithOffset(BYTE bDialogLocation, BYTE bFontColor, INT iNumCharFace, BOOL fPlayingRNG, INT xOff, INT yOff)
{
    PalPackSpan face_span;
    SDL_Rect rect;

    if (gpGlobals->fInBattle && !g_fUpdatedInBattle) {
        VIDEO_UpdateScreen(NULL);
        g_fUpdatedInBattle = TRUE;
    }

    g_TextLib.bIcon = 0;
    g_TextLib.posIcon = 0;
    g_TextLib.nCurrentDialogLine = 0;
    g_TextLib.posDialogTitle = PAL_XY(12, 8);
    g_TextLib.posDialogText = PAL_XY(44, 26);
    g_TextLib.fUserSkip = FALSE;
    if (bFontColor != 0) {
        g_TextLib.bCurrentFontColor = bFontColor;
    }
    if (fPlayingRNG && iNumCharFace) {
        VIDEO_BackupScreen(gpScreen);
        g_TextLib.fPlayingRNG = TRUE;
    }

#ifdef PAL_EXTREME_TWO_SCREENS
    if (bDialogLocation == kDialogCenter ||
        bDialogLocation == kDialogCenterWindow) {
        if (!PalNativeUi_GetCenterDialogLayout(
                &pal_contract_dialog_layout)) {
            return;
        }
    } else if (!PalNativeUi_GetDialogLayout(
            bDialogLocation == kDialogLower,
            iNumCharFace > 0,
            &pal_contract_dialog_layout)) {
        return;
    }

    pal_contract_dialog_layout.text.x = (int16_t)(
        pal_contract_dialog_layout.text.x + xOff);
    pal_contract_dialog_layout.text.y = (int16_t)(
        pal_contract_dialog_layout.text.y + yOff);
    pal_contract_dialog_layout.title_x = (int16_t)(
        pal_contract_dialog_layout.title_x + xOff);
    pal_contract_dialog_layout.title_y = (int16_t)(
        pal_contract_dialog_layout.title_y + yOff);
    g_TextLib.posDialogTitle = PAL_XY(
        pal_contract_dialog_layout.title_x,
        pal_contract_dialog_layout.title_y);
    g_TextLib.posDialogText = PAL_XY(
        pal_contract_dialog_layout.text.x,
        pal_contract_dialog_layout.text.y);

    if (iNumCharFace > 0 &&
        (bDialogLocation == kDialogUpper ||
         bDialogLocation == kDialogLower) &&
        PalContract_OpenNorPack() &&
        PalPack_MapConst(
            &pal_contract_nor_pack,
            PAL_PACK_ARCHIVE_RGM,
            (uint16_t)iNumCharFace,
            &face_span) &&
        face_span.data != NULL) {
        PalNativeUiRect face_box = pal_contract_dialog_layout.portrait;
        PalNativeUiRect drawn;
        face_box.x = (int16_t)(face_box.x + xOff);
        face_box.y = (int16_t)(face_box.y + yOff);
        if (PalNativeUi_BlitRleFitIndexed(
                face_span.data,
                face_span.size,
                (uint8_t *)gpScreen->pixels,
                (uint16_t)gpScreen->pitch,
                (uint16_t)gpScreen->w,
                (uint16_t)gpScreen->h,
                face_box,
                &drawn)) {
            rect.x = drawn.x;
            rect.y = drawn.y;
            rect.w = drawn.width;
            rect.h = drawn.height;
            VIDEO_UpdateScreen(&rect);
        }
    }
    g_TextLib.bDialogPosition = bDialogLocation;
    return;
#endif

    switch (bDialogLocation) {
    case kDialogCenter:
        g_TextLib.posDialogText = PAL_XY(80, 40);
        break;
    case kDialogLower:
        if (iNumCharFace > 0 &&
            PalContract_OpenNorPack() &&
            PalPack_MapConst(&pal_contract_nor_pack, PAL_PACK_ARCHIVE_RGM, (uint16_t)iNumCharFace, &face_span) &&
            face_span.data != NULL) {
            LPCBITMAPRLE face = (LPCBITMAPRLE)face_span.data;
            rect.x = 270 - PAL_RLEGetWidth(face) / 2 + xOff;
            rect.y = 144 - PAL_RLEGetHeight(face) / 2 + yOff;
            PAL_RLEBlitToSurface(face, gpScreen, PAL_XY(rect.x, rect.y));
            VIDEO_UpdateScreen(NULL);
        }
        g_TextLib.posDialogTitle = PAL_XY(iNumCharFace > 0 ? 4 : 12, 108);
        g_TextLib.posDialogText = PAL_XY(iNumCharFace > 0 ? 20 : 44, 126);
        break;
    case kDialogCenterWindow:
        g_TextLib.posDialogText = PAL_XY(160, 40);
        break;
    case kDialogUpper:
    default:
        if (iNumCharFace > 0 &&
            PalContract_OpenNorPack() &&
            PalPack_MapConst(&pal_contract_nor_pack, PAL_PACK_ARCHIVE_RGM, (uint16_t)iNumCharFace, &face_span) &&
            face_span.data != NULL) {
            LPCBITMAPRLE face = (LPCBITMAPRLE)face_span.data;
            rect.w = PAL_RLEGetWidth(face);
            rect.h = PAL_RLEGetHeight(face);
            rect.x = 48 - rect.w / 2 + xOff;
            rect.y = 55 - rect.h / 2 + yOff;
            if (rect.x < 0) {
                rect.x = 0;
            }
            if (rect.y < 0) {
                rect.y = 0;
            }
            PAL_RLEBlitToSurface(face, gpScreen, PAL_XY(rect.x, rect.y));
            VIDEO_UpdateScreen(&rect);
        }
        g_TextLib.posDialogTitle = PAL_XY(iNumCharFace > 0 ? 80 : 12, 8);
        g_TextLib.posDialogText = PAL_XY(iNumCharFace > 0 ? 96 : 44, 26);
        break;
    }

    g_TextLib.posDialogTitle = PAL_XY(
        PAL_X(g_TextLib.posDialogTitle) + xOff,
        PAL_Y(g_TextLib.posDialogTitle) + yOff);
    g_TextLib.posDialogText = PAL_XY(
        PAL_X(g_TextLib.posDialogText) + xOff,
        PAL_Y(g_TextLib.posDialogText) + yOff);
    g_TextLib.bDialogPosition = bDialogLocation;
}

static void PalContract_DialogWaitForKey(FLOAT max_seconds)
{
    uint32_t start = SDL_GetTicks();
    SDL_Color *palette = (SDL_Color *)pal_sram_contract_dialog_palette;
    SDL_Color *current_palette;
    bool animate_icon;
    int i;

    animate_icon = g_TextLib.bDialogPosition != kDialogCenterWindow &&
        g_TextLib.bDialogPosition != kDialogCenter;
    current_palette = PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
    if (current_palette != NULL) {
        memcpy(palette, current_palette, sizeof(pal_sram_contract_dialog_palette));
    } else {
        memset(palette, 0, sizeof(pal_sram_contract_dialog_palette));
    }

    if (animate_icon) {
        LPCBITMAPRLE icon = PAL_SpriteGetFrame(g_TextLib.bufDialogIcons, g_TextLib.bIcon);
        if (icon != NULL) {
            SDL_Rect rect;
            rect.x = PAL_X(g_TextLib.posIcon);
            rect.y = PAL_Y(g_TextLib.posIcon);
            rect.w = 16;
            rect.h = 16;
            PAL_RLEBlitToSurface(icon, gpScreen, g_TextLib.posIcon);
            VIDEO_UpdateScreen(&rect);
        }
    }

    PAL_ClearKeyState();
    while (TRUE) {
        UTIL_Delay(100);
        if (animate_icon) {
            SDL_Color t = palette[0xF9];
            for (i = 0xF9; i < 0xFE; i++) {
                palette[i] = palette[i + 1];
            }
            palette[0xFE] = t;
            VIDEO_SetPalette(palette);
        }
        if (max_seconds > 0.0f && SDL_GetTicks() - start > (uint32_t)(max_seconds * 1000.0f)) {
            break;
        }
        if (g_InputState.dwKeyPress != 0) {
            break;
        }
    }
    if (animate_icon) {
        PAL_SetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
    }
    PAL_ClearKeyState();
    g_TextLib.fUserSkip = FALSE;
}

#ifdef PAL_EXTREME_TWO_SCREENS
static bool
PalContract_NativeDialogControl(
    WCHAR value
)
{
    return value == '-' || value == '\'' || value == '@' ||
        value == '"' || value == ')' || value == '(';
}

static size_t
PalContract_NativeDialogToken(
    LPCWSTR text,
    int *visible_width
)
{
    if (visible_width == NULL || text == NULL || text[0] == 0) {
        return 0u;
    }
    *visible_width = 0;
    if (text[0] == '$' || text[0] == '~') {
        size_t count = 1u;
        while (count < 3u && text[count] != 0) {
            count++;
        }
        return count;
    }
    if (PalContract_NativeDialogControl(text[0])) {
        return 1u;
    }
    if (text[0] == '\\' && text[1] != 0) {
        *visible_width = PAL_CharWidth((uint16_t)text[1]);
        return 2u;
    }
    *visible_width = PAL_CharWidth((uint16_t)text[0]);
    return 1u;
}

static int
PalContract_NativeDialogMeasure(
    LPCWSTR text
)
{
    int width = 0;

    while (text != NULL && *text != 0) {
        int token_width;
        bool terminates = text[0] == '~';
        size_t token = PalContract_NativeDialogToken(text, &token_width);
        if (token == 0u) {
            break;
        }
        width += token_width;
        text += token;
        if (terminates) {
            break;
        }
    }
    return width;
}

static bool
PalContract_NativeDrawPopup(
    LPCWSTR text,
    int region_x,
    int region_width,
    int text_y,
    int x_offset,
    int shadow_offset
)
{
    LPCBITMAPRLE single_left = PAL_SpriteGetFrame(
        gpSpriteUI, PAL_NATIVE_UI_GENERATED_SINGLE_LINE_LEFT_FRAME);
    LPCBITMAPRLE single_middle = PAL_SpriteGetFrame(
        gpSpriteUI, PAL_NATIVE_UI_GENERATED_SINGLE_LINE_MIDDLE_FRAME);
    LPCBITMAPRLE single_right = PAL_SpriteGetFrame(
        gpSpriteUI, PAL_NATIVE_UI_GENERATED_SINGLE_LINE_RIGHT_FRAME);
    bool drawn = false;
    int single_left_width;
    int single_middle_width;
    int single_right_width;
    int text_width;

    if (single_left == NULL || single_middle == NULL ||
        single_right == NULL) {
        return false;
    }
    single_left_width = PAL_RLEGetWidth(single_left);
    single_middle_width = PAL_RLEGetWidth(single_middle);
    single_right_width = PAL_RLEGetWidth(single_right);
    if (single_left_width <= 0 || single_middle_width <= 0 ||
        single_right_width <= 0) {
        return false;
    }
    if (shadow_offset < 0) {
        shadow_offset = 0;
    }

    text_width = PalContract_NativeDialogMeasure(text);
    if (text_width <= region_width -
            single_left_width - single_right_width) {
        int box_units = (text_width + single_middle_width - 1) /
            single_middle_width;
        int box_width;
        int box_x;
        int box_y;
        int draw_x;

        if (box_units < 1) {
            box_units = 1;
        }
        box_width = single_left_width + single_right_width +
            box_units * single_middle_width;
        box_x = region_x + (region_width - box_width) / 2 + x_offset;
        box_y = text_y -
            PAL_NATIVE_UI_GENERATED_DIALOG_POPUP_SINGLE_TEXT_INSET_Y;
        draw_x = box_x + (box_width - text_width) / 2;
        (void)PAL_CreateSingleLineBoxWithShadow(
            PAL_XY(box_x, box_y), box_units, FALSE, shadow_offset);
        (void)TEXT_DisplayText(text, draw_x, text_y, TRUE);
        drawn = true;
    }

    return drawn;
}

static void
PalContract_NativeDialogNextPage(
    void
)
{
    PalContract_DialogWaitForKey(0.0f);
    g_TextLib.nCurrentDialogLine = 0;
    VIDEO_RestoreScreen(gpScreen);
    VIDEO_UpdateScreen(NULL);
}

static void
PalContract_NativeShowDialogText(
    LPCWSTR text
)
{
    int x;
    int y;

    if (!g_TextLib.fPlayingRNG &&
        g_TextLib.nCurrentDialogLine == 0) {
        VIDEO_BackupScreen(gpScreen);
    }

    if (g_TextLib.nCurrentDialogLine >=
        pal_contract_dialog_layout.page_lines) {
        PalContract_NativeDialogNextPage();
    }
    x = pal_contract_dialog_layout.text.x;
    y = pal_contract_dialog_layout.text.y +
        g_TextLib.nCurrentDialogLine *
        pal_contract_dialog_layout.line_height;
    x = TEXT_DisplayText(
        text != NULL ? text : pal_empty_text, x, y, FALSE);
    if (g_TextLib.fUserSkip) {
        VIDEO_UpdateScreen(NULL);
    }
    g_TextLib.posIcon = PAL_XY(x, y);
    g_TextLib.nCurrentDialogLine++;
}
#endif

int TEXT_DisplayText(LPCWSTR lpszText, int x, int y, BOOL isDialog)
{
    WCHAR text[2];
    BYTE color;
    BYTE is_number = 0;

    while (lpszText != NULL && *lpszText != 0) {
        switch (*lpszText) {
        case '-':
            g_TextLib.bCurrentFontColor =
                (g_TextLib.bCurrentFontColor == FONT_COLOR_CYAN) ? FONT_COLOR_DEFAULT : FONT_COLOR_CYAN;
            lpszText++;
            break;
        case '\'':
            g_TextLib.bCurrentFontColor =
                (g_TextLib.bCurrentFontColor == FONT_COLOR_RED) ? FONT_COLOR_DEFAULT : FONT_COLOR_RED;
            lpszText++;
            break;
        case '@':
            g_TextLib.bCurrentFontColor =
                (g_TextLib.bCurrentFontColor == FONT_COLOR_RED_ALT) ? FONT_COLOR_DEFAULT : FONT_COLOR_RED_ALT;
            lpszText++;
            break;
        case '"':
            if (!isDialog) {
                g_TextLib.bCurrentFontColor =
                    (g_TextLib.bCurrentFontColor == FONT_COLOR_YELLOW) ? FONT_COLOR_DEFAULT : FONT_COLOR_YELLOW;
            }
            lpszText++;
            break;
        case '$':
            g_TextLib.iDelayTime = (INT)(wcstol(lpszText + 1, NULL, 10) * 10 / 7);
            lpszText += 3;
            break;
        case '~':
            if (g_TextLib.fUserSkip) {
                VIDEO_UpdateScreen(NULL);
            }
            if (!isDialog) {
                UTIL_Delay((uint32_t)(wcstol(lpszText + 1, NULL, 10) * 80 / 7));
            }
            g_TextLib.nCurrentDialogLine = -1;
            g_TextLib.fUserSkip = FALSE;
            return x;
        case ')':
            g_TextLib.bIcon = 1;
            lpszText++;
            break;
        case '(':
            g_TextLib.bIcon = 2;
            lpszText++;
            break;
        case '\\':
            lpszText++;
            /* fall through */
        default:
            text[0] = *lpszText++;
            text[1] = 0;
            color = g_TextLib.bCurrentFontColor;
            if (isDialog) {
                if (color == FONT_COLOR_DEFAULT) {
                    color = 0;
                }
                is_number = (text[0] >= '0' && text[0] <= '9') ? 1 : 0;
            }
            if (is_number) {
                PAL_DrawNumber((UINT)(text[0] - '0'), 1, PAL_XY(x, y + 4), kNumColorYellow, kNumAlignLeft);
            } else {
                PAL_DrawTextUnescape(text, PAL_XY(x, y), color, !isDialog, !isDialog && !g_TextLib.fUserSkip, FALSE, FALSE);
            }
            x += PAL_CharWidth((uint16_t)text[0]);
            if (!isDialog && !g_TextLib.fUserSkip) {
                PAL_ClearKeyState();
                UTIL_Delay((uint32_t)(g_TextLib.iDelayTime * 8));
                if (g_InputState.dwKeyPress & (kKeySearch | kKeyMenu)) {
                    g_TextLib.fUserSkip = TRUE;
                }
            }
            break;
        }
    }
    return x;
}

VOID PAL_ShowDialogText(LPCWSTR lpszText)
{
    int x;
    int y;
    size_t len;

    PAL_ClearKeyState();
    g_TextLib.bIcon = 0;

    if (gpGlobals->fInBattle && !g_fUpdatedInBattle) {
        VIDEO_UpdateScreen(NULL);
        g_fUpdatedInBattle = TRUE;
    }

#ifdef PAL_EXTREME_TWO_SCREENS
    len = lpszText != NULL ? wcslen(lpszText) : 0;
    if (g_TextLib.bDialogPosition == kDialogCenterWindow) {
        int x_offset;

#ifndef PAL_CLASSIC
        if (gpGlobals->fInBattle &&
            g_Battle.BattleResult == kBattleResultOnGoing) {
            PAL_BattleUIShowText(lpszText, 1400);
            return;
        }
#endif
        x_offset = pal_contract_dialog_layout.text.x -
            PAL_NATIVE_UI_GENERATED_DIALOG_CENTER_TEXT_X;
        VIDEO_BackupScreen(gpScreen);
        (void)PalContract_NativeDrawPopup(
            lpszText,
            0,
            PAL_NATIVE_UI_GENERATED_DISPLAY_WIDTH,
            pal_contract_dialog_layout.text.y,
            x_offset,
            g_TextLib.iDialogShadow);
        VIDEO_UpdateScreen(NULL);
        PalContract_DialogWaitForKey(1.4f);
        VIDEO_RestoreScreen(gpScreen);
        VIDEO_UpdateScreen(NULL);
        PAL_EndDialog();
        return;
    }

    if (g_TextLib.nCurrentDialogLine == 0 &&
        g_TextLib.bDialogPosition != kDialogCenter &&
        len > 0 &&
        (lpszText[len - 1] == 0xff1a ||
         lpszText[len - 1] == 0x2236 ||
         lpszText[len - 1] == ':')) {
        PAL_DrawText(
            lpszText,
            g_TextLib.posDialogTitle,
            FONT_COLOR_CYAN_ALT,
            TRUE,
            TRUE,
            FALSE);
        return;
    }

    PalContract_NativeShowDialogText(lpszText);
    return;
#endif

    if (g_TextLib.nCurrentDialogLine > 3) {
        PalContract_DialogWaitForKey(0.0f);
        g_TextLib.nCurrentDialogLine = 0;
        VIDEO_RestoreScreen(gpScreen);
        VIDEO_UpdateScreen(NULL);
    }

    x = PAL_X(g_TextLib.posDialogText);
    y = PAL_Y(g_TextLib.posDialogText) + g_TextLib.nCurrentDialogLine * 18;

    if (g_TextLib.bDialogPosition == kDialogCenterWindow) {
#ifndef PAL_CLASSIC
        if (gpGlobals->fInBattle && g_Battle.BattleResult == kBattleResultOnGoing) {
            PAL_BattleUIShowText(lpszText, 1400);
        } else
#endif
        {
        int width = 0;
        int i;
        LPBOX box;
        SDL_Rect rect;
        PAL_POS pos;

        len = lpszText != NULL ? wcslen(lpszText) : 0;
        for (i = 0; i < (int)len; i++) {
            width += PAL_CharWidth((uint16_t)lpszText[i]) >> 3;
        }
        pos = PAL_XY(PAL_X(g_TextLib.posDialogText) - width * 4, PAL_Y(g_TextLib.posDialogText));
        box = PAL_CreateSingleLineBoxWithShadow(pos, (width + 1) / 2, FALSE, g_TextLib.iDialogShadow);
        rect.x = PAL_X(pos);
        rect.y = PAL_Y(pos);
        rect.w = 320 - rect.x * 2 + 32;
        rect.h = 64;
        VIDEO_UpdateScreen(&rect);
        TEXT_DisplayText(lpszText, PAL_X(pos) + 8 + ((width & 1) << 2), PAL_Y(pos) + 10, TRUE);
        VIDEO_UpdateScreen(&rect);
        PalContract_DialogWaitForKey(1.4f);
        PAL_DeleteBox(box);
        VIDEO_UpdateScreen(&rect);
        PAL_EndDialog();
        }
        return;
    }

    len = lpszText != NULL ? wcslen(lpszText) : 0;
    if (g_TextLib.nCurrentDialogLine == 0 &&
        g_TextLib.bDialogPosition != kDialogCenter &&
        len > 0 &&
        (lpszText[len - 1] == 0xff1a || lpszText[len - 1] == 0x2236 || lpszText[len - 1] == ':')) {
        PAL_DrawText(lpszText, g_TextLib.posDialogTitle, FONT_COLOR_CYAN_ALT, TRUE, TRUE, FALSE);
        return;
    }

    if (!g_TextLib.fPlayingRNG && g_TextLib.nCurrentDialogLine == 0) {
        VIDEO_BackupScreen(gpScreen);
    }
    x = TEXT_DisplayText(lpszText, x, y, FALSE);
    if (g_TextLib.fUserSkip) {
        VIDEO_UpdateScreen(NULL);
    }
    g_TextLib.posIcon = PAL_XY(x, y);
    g_TextLib.nCurrentDialogLine++;
}

VOID PAL_ClearDialog(BOOL fWaitForKey)
{
    if (g_TextLib.nCurrentDialogLine > 0 && fWaitForKey) {
        PalContract_DialogWaitForKey(0.0f);
    }
    g_TextLib.nCurrentDialogLine = 0;
    if (g_TextLib.bDialogPosition == kDialogCenter) {
#ifdef PAL_EXTREME_TWO_SCREENS
        (void)PalContract_NativeResetDefaultDialogLayout();
#else
        g_TextLib.posDialogTitle = PAL_XY(12, 8);
        g_TextLib.posDialogText = PAL_XY(44, 26);
        g_TextLib.bDialogPosition = kDialogUpper;
#endif
        g_TextLib.bCurrentFontColor = FONT_COLOR_DEFAULT;
    }
}

VOID PAL_EndDialog(VOID)
{
    PAL_ClearDialog(TRUE);
#ifdef PAL_EXTREME_TWO_SCREENS
    (void)PalContract_NativeResetDefaultDialogLayout();
#else
    g_TextLib.posDialogTitle = PAL_XY(12, 8);
    g_TextLib.posDialogText = PAL_XY(44, 26);
    g_TextLib.bDialogPosition = kDialogUpper;
#endif
    g_TextLib.bCurrentFontColor = FONT_COLOR_DEFAULT;
    g_TextLib.fUserSkip = FALSE;
    g_TextLib.fPlayingRNG = FALSE;
}

BOOL PAL_IsInDialog(VOID)
{
    return g_TextLib.nCurrentDialogLine != 0;
}

BOOL PAL_DialogIsPlayingRNG(VOID)
{
    return g_TextLib.fPlayingRNG;
}

INT PAL_MultiByteToWideChar(LPCSTR mbs, int mbslength, LPWSTR wcs, int wcslength)
{
    int i;

    if (mbs == NULL || mbslength < 0 || wcslength < 0) {
        return 0;
    }
    if (wcs == NULL) {
        return mbslength;
    }
    for (i = 0; i < mbslength && i < wcslength; i++) {
        wcs[i] = (WCHAR)(uint8_t)mbs[i];
    }
    return i;
}

INT PAL_MultiByteToWideCharCP(CODEPAGE cp, LPCSTR mbs, size_t mbslength, LPWSTR wcs, size_t wcslength)
{
    (void)cp;
    if (mbslength > (size_t)0x7fffffff || wcslength > (size_t)0x7fffffff) {
        return 0;
    }
    return PAL_MultiByteToWideChar(mbs, (int)mbslength, wcs, (int)wcslength);
}

WCHAR PAL_GetInvalidChar(CODEPAGE uCodePage)
{
    (void)uCodePage;
    return (WCHAR)'?';
}

CODEPAGE PAL_GetCodePage(void)
{
    return CP_BIG5;
}

void PAL_SetCodePage(CODEPAGE uCodePage)
{
    (void)uCodePage;
}

CODEPAGE PAL_DetectCodePageForString(const char *text, size_t text_len, CODEPAGE default_cp, int *probability)
{
    (void)text;
    (void)text_len;
    if (probability != NULL) {
        *probability = 100;
    }
    return default_cp;
}

INT PAL_swprintf(LPWSTR buffer, size_t count, LPCWSTR format, ...)
{
    INT result;
    va_list ap;

    if (buffer != NULL && count != 0) {
        buffer[0] = 0;
    }
    if (buffer == NULL || count == 0 || format == NULL) {
        return -1;
    }

    va_start(ap, format);
    result = (INT)vswprintf(buffer, count, format, ap);
    va_end(ap);
    return result;
}

LPAUDIOPLAYER TIMIDITY_Init(VOID)
{
    return NULL;
}

LPAUDIOPLAYER TSF_Init(VOID)
{
    return NULL;
}

LPAUDIOPLAYER MP3_Init(VOID)
{
    return NULL;
}

LPAUDIOPLAYER OGG_Init(VOID)
{
    return NULL;
}

LPAUDIOPLAYER OPUS_Init(VOID)
{
    return NULL;
}

#ifndef PAL_CONTRACT_EXTERNAL_RIX
LPAUDIOPLAYER RIX_Init(LPCSTR szFileName)
{
    (void)szFileName;
    pal_contract_music_player.iMusic = -1;
    pal_contract_music_player.fLoop = FALSE;
    pal_contract_music_player.Shutdown = PalContract_PlayerShutdown;
    pal_contract_music_player.Play = PalContract_MusicPlay;
    pal_contract_music_player.FillBuffer = PalContract_MusicFillBuffer;
    return &pal_contract_music_player;
}
#endif

#ifndef PAL_CONTRACT_DISABLE_SFX
LPAUDIOPLAYER SOUND_Init(VOID)
{
    if (!PalContract_OpenTfPack()) {
        return NULL;
    }
    pal_contract_sound_player.iMusic = -1;
    pal_contract_sound_player.fLoop = FALSE;
    pal_contract_sound_player.Shutdown = PalContract_PlayerShutdown;
    pal_contract_sound_player.Play = PalContract_SoundPlay;
    pal_contract_sound_player.FillBuffer = PalContract_SoundFillBuffer;
    return &pal_contract_sound_player;
}
#endif

VOID PAL_AVIInit(VOID)
{
}

VOID PAL_AVIShutdown(VOID)
{
}

BOOL PAL_PlayAVI(const char *lpszPath)
{
    (void)lpszPath;
    return FALSE;
}

void SDLCALL AVI_FillAudioBuffer(void *udata, uint8_t *stream, int len)
{
    (void)udata;
    (void)stream;
    (void)len;
}

void *AVI_GetPlayState(void)
{
    return NULL;
}
