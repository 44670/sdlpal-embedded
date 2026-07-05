#include "aviplay.h"
#include "players.h"
#include "resampler.h"

void resampler_init(void)
{
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
