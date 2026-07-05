#include "aviplay.h"
#include "players.h"

LPAUDIOPLAYER TIMIDITY_Init(VOID)
{
    return NULL;
}

LPAUDIOPLAYER TSF_Init(VOID)
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
