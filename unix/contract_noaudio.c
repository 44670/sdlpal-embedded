#include "audio.h"
#include "palcfg.h"

#include <string.h>

AUDIODEVICE gAudioDevice;

static void
PalNoAudio_InitSpec(
   void
)
{
   memset(&gAudioDevice.spec, 0, sizeof(gAudioDevice.spec));
   gAudioDevice.spec.freq = gConfig.iSampleRate > 0 ? gConfig.iSampleRate : 44100;
   gAudioDevice.spec.format = AUDIO_S16SYS;
   gAudioDevice.spec.channels = gConfig.iAudioChannels > 0 ? (Uint8)gConfig.iAudioChannels : 2;
#if !SDL_VERSION_ATLEAST(3,0,0)
   gAudioDevice.spec.samples = gConfig.wAudioBufferSize != 0 ? gConfig.wAudioBufferSize : PAL_AUDIO_DEFAULT_BUFFER_SIZE;
#endif
}

INT
AUDIO_OpenDevice(
   VOID
)
{
   PalNoAudio_InitSpec();
   gAudioDevice.fOpened = TRUE;
   gAudioDevice.fMusicEnabled = FALSE;
   gAudioDevice.fSoundEnabled = FALSE;
   gAudioDevice.iMusicVolume = 0;
   gAudioDevice.iSoundVolume = 0;
   return 0;
}

BOOL
AUDIO_CD_Available(
   VOID
)
{
   return FALSE;
}

VOID
AUDIO_CloseDevice(
   VOID
)
{
   gAudioDevice.fOpened = FALSE;
}

SDL_AudioSpec *
AUDIO_GetDeviceSpec(
   VOID
)
{
   if (gAudioDevice.spec.freq == 0) {
      PalNoAudio_InitSpec();
   }
   return &gAudioDevice.spec;
}

VOID
AUDIO_IncreaseVolume(
   VOID
)
{
}

VOID
AUDIO_DecreaseVolume(
   VOID
)
{
}

VOID
AUDIO_PlayMusic(
   INT       iNumRIX,
   BOOL      fLoop,
   FLOAT     flFadeTime
)
{
   (void)iNumRIX;
   (void)fLoop;
   (void)flFadeTime;
}

BOOL
AUDIO_PlayCDTrack(
   INT    iNumTrack
)
{
   (void)iNumTrack;
   return FALSE;
}

VOID
AUDIO_PlaySound(
   INT    iSoundNum
)
{
   (void)iSoundNum;
}

VOID
AUDIO_EnableMusic(
   BOOL   fEnable
)
{
   (void)fEnable;
   gAudioDevice.fMusicEnabled = FALSE;
}

BOOL
AUDIO_MusicEnabled(
   VOID
)
{
   return FALSE;
}

VOID
AUDIO_EnableSound(
   BOOL   fEnable
)
{
   (void)fEnable;
   gAudioDevice.fSoundEnabled = FALSE;
}

BOOL
AUDIO_SoundEnabled(
   VOID
)
{
   return FALSE;
}

void
AUDIO_Lock(
   void
)
{
}

void
AUDIO_Unlock(
   void
)
{
}
