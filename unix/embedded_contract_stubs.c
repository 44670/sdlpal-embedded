#include "aviplay.h"
#include "font.h"
#include "players.h"
#include "resampler.h"
#include "text.h"

#include <stdarg.h>

void resampler_init(void)
{
}

char *font_offset_x;
char *font_offset_y;
TEXTLIB g_TextLib;
LPWSTR g_rcCredits[12];
BOOL g_fUpdatedInBattle;
static WCHAR pal_empty_text[1];

int PAL_InitFont(const CONFIGURATION *cfg)
{
    (void)cfg;
    return 0;
}

void PAL_FreeFont(void)
{
}

void PAL_DrawCharOnSurface(uint16_t wChar, SDL_Surface *lpSurface, PAL_POS pos, uint8_t bColor, BOOL fUse8x8Font)
{
    (void)wChar;
    (void)lpSurface;
    (void)pos;
    (void)bColor;
    (void)fUse8x8Font;
}

int PAL_CharWidth(uint16_t wChar)
{
    (void)wChar;
    return 16;
}

int PAL_FontHeight(void)
{
    return 16;
}

INT PAL_InitText(VOID)
{
    return 0;
}

VOID PAL_FreeText(VOID)
{
}

LPCWSTR PAL_GetWord(int iNumWord)
{
    (void)iNumWord;
    return pal_empty_text;
}

LPCWSTR PAL_GetMsg(int iNumMsg)
{
    (void)iNumMsg;
    return pal_empty_text;
}

int PAL_GetMsgNum(int iIndex, int iSpan, int iOrder)
{
    (void)iIndex;
    (void)iSpan;
    (void)iOrder;
    return 0;
}

LPWSTR PAL_UnescapeText(LPCWSTR lpszText)
{
    return (LPWSTR)(lpszText != NULL ? lpszText : pal_empty_text);
}

VOID PAL_DrawText(LPCWSTR lpszText, PAL_POS pos, BYTE bColor, BOOL fShadow, BOOL fUpdate, BOOL fUse8x8Font)
{
    (void)lpszText;
    (void)pos;
    (void)bColor;
    (void)fShadow;
    (void)fUpdate;
    (void)fUse8x8Font;
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
    (void)lpszText;
    (void)pos;
    (void)bColor;
    (void)fShadow;
    (void)fUpdate;
    (void)fUse8x8Font;
    (void)fUnescape;
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
    (void)format;
    if (buffer != NULL && count != 0) {
        buffer[0] = 0;
    }
    return 0;
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
