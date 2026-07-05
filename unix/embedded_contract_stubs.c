#include "aviplay.h"
#include "font.h"
#include "players.h"
#include "resampler.h"
#include "text.h"
#include "video.h"
#include "../embedded/pal_font_cache.h"
#include "../embedded/pal_pack.h"
#include "../embedded/pal_text_cache.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#define PAL_CONTRACT_TEXT_SLOTS 8u
#define PAL_CONTRACT_TEXT_CHARS 1024u

#if defined(__GNUC__)
#define PAL_CONTRACT_SRAM __attribute__((section(".bss.pal_sram"), aligned(4)))
#else
#define PAL_CONTRACT_SRAM
#endif

static PalPack pal_contract_nor_pack;
static PalTextCache pal_contract_text;
static PalFontCache pal_contract_font;
static bool pal_contract_pack_ready;
static bool pal_contract_pack_tried;
static bool pal_contract_text_ready;
static bool pal_contract_font_ready;
static uint8_t pal_sram_contract_text_slots
    [PAL_CONTRACT_TEXT_SLOTS][PAL_CONTRACT_TEXT_CHARS * sizeof(WCHAR)] PAL_CONTRACT_SRAM;
static unsigned int pal_contract_text_slot;
static WCHAR pal_empty_text[1];

static uint16_t
PalContract_ReadLe16(
    const uint8_t *p
)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static bool
PalContract_MapNorPackPath(
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
    if (!PalPack_OpenConst(&pal_contract_nor_pack, image, (uint32_t)st.st_size)) {
        munmap((void *)image, (size_t)st.st_size);
        return false;
    }

    return true;
}

static bool
PalContract_OpenNorPack(
    void
)
{
    const char *path;

    if (pal_contract_pack_tried) {
        return pal_contract_pack_ready;
    }
    pal_contract_pack_tried = true;

    path = getenv("PAL_CONTRACT_NOR_PACK");
    if (PalContract_MapNorPackPath(path) ||
        PalContract_MapNorPackPath("/tmp/pal_nor_default.pak")) {
        pal_contract_pack_ready = true;
    }
    return pal_contract_pack_ready;
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
    pal_contract_font_ready = PalContract_OpenNorPack() && PalFont_Open(&pal_contract_nor_pack, &pal_contract_font);
    return pal_contract_font_ready ? 0 : -1;
}

void PAL_FreeFont(void)
{
}

void PAL_DrawCharOnSurface(uint16_t wChar, SDL_Surface *lpSurface, PAL_POS pos, uint8_t bColor, BOOL fUse8x8Font)
{
    const uint8_t *glyph;
    uint16_t glyph_bytes;

    (void)fUse8x8Font;
    if (!pal_contract_font_ready ||
        !PalFont_FindGlyph(&pal_contract_font, wChar, &glyph, &glyph_bytes) ||
        glyph_bytes < PAL_FONT_GLYPH_BYTES) {
        return;
    }
    PalContract_DrawGlyph16(glyph, lpSurface, PAL_X(pos), PAL_Y(pos), bColor);
}

int PAL_CharWidth(uint16_t wChar)
{
    return (wChar < 0x80) ? 8 : 16;
}

int PAL_FontHeight(void)
{
    return 16;
}

INT PAL_InitText(VOID)
{
    pal_contract_text_ready = PalContract_OpenNorPack() && PalText_Open(&pal_contract_nor_pack, &pal_contract_text);
    if (!pal_contract_text_ready) {
        return -1;
    }
    g_TextLib.nWords = pal_contract_text.word_count;
    g_TextLib.nMsgs = pal_contract_text.message_count;
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
        pos = PAL_XY(x + (fUse8x8Font ? 8 : PAL_CharWidth((uint16_t)*lpszText)), y);
        lpszText++;
    }
    if (fUpdate) {
        VIDEO_UpdateScreen(NULL);
    }
}

VOID PAL_DialogSetDelayTime(INT iDelayTime)
{
    (void)iDelayTime;
}

VOID PAL_StartDialog(BYTE bDialogLocation, BYTE bFontColor, INT iNumCharFace, BOOL fPlayingRNG)
{
    (void)bDialogLocation;
    (void)bFontColor;
    (void)iNumCharFace;
    (void)fPlayingRNG;
}

VOID PAL_StartDialogWithOffset(BYTE bDialogLocation, BYTE bFontColor, INT iNumCharFace, BOOL fPlayingRNG, INT xOff, INT yOff)
{
    (void)bDialogLocation;
    (void)bFontColor;
    (void)iNumCharFace;
    (void)fPlayingRNG;
    (void)xOff;
    (void)yOff;
}

int TEXT_DisplayText(LPCWSTR lpszText, int x, int y, BOOL isDialog)
{
    (void)lpszText;
    (void)x;
    (void)y;
    (void)isDialog;
    return 0;
}

VOID PAL_ShowDialogText(LPCWSTR lpszText)
{
    (void)lpszText;
}

VOID PAL_ClearDialog(BOOL fWaitForKey)
{
    (void)fWaitForKey;
}

VOID PAL_EndDialog(VOID)
{
}

BOOL PAL_IsInDialog(VOID)
{
    return FALSE;
}

BOOL PAL_DialogIsPlayingRNG(VOID)
{
    return FALSE;
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

LPAUDIOPLAYER RIX_Init(LPCSTR szFileName)
{
    (void)szFileName;
    return NULL;
}

LPAUDIOPLAYER SOUND_Init(VOID)
{
    return NULL;
}

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
