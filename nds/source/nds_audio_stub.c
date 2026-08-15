/* Optional silent audio backend for Nintendo DS diagnostic builds.
 *
 * This stub keeps the engine's AUDIO_* surface intact while submitting no
 * sound.  The production Makefile builds the Calico/DBOPL2 implementation in
 * nds_music.cpp instead; this file is retained only for explicit diagnostic
 * source lists.
 */

#include "../../audio.h"
#include "../../palcfg.h"

#include <string.h>

#include "pal_target_board.h"

AUDIODEVICE gAudioDevice;

static int
music_clamped_config_volume(
   void)
{
   int volume = gConfig.iMusicVolume;

   if (volume < 0)
   {
      volume = 0;
   }
   else if (volume > PAL_MAX_VOLUME)
   {
      volume = PAL_MAX_VOLUME;
   }
   return volume;
}

static int
music_sdl_volume(
   void)
{
   return music_clamped_config_volume() *
      SDL_MIX_MAXVOLUME / PAL_MAX_VOLUME;
}

void
NdsTarget_AudioPump(
   void)
{
}

INT
AUDIO_OpenDevice(
   VOID)
{
   if (gAudioDevice.fOpened)
   {
      return 0;
   }
   memset(&gAudioDevice, 0, sizeof(gAudioDevice));
   gAudioDevice.spec.freq = 22050;
   gAudioDevice.spec.format = AUDIO_S16SYS;
   gAudioDevice.spec.channels = 1;
#if !SDL_VERSION_ATLEAST(3, 0, 0)
   gAudioDevice.spec.samples = 1u;
#endif
   gAudioDevice.iMusicVolume = music_sdl_volume();
   gAudioDevice.iSoundVolume = 0;
   gAudioDevice.fSoundEnabled = FALSE;
   gAudioDevice.fMusicEnabled = TRUE;
   gAudioDevice.fOpened = TRUE;
   return 0;
}

BOOL
AUDIO_CD_Available(
   VOID)
{
   return FALSE;
}

VOID
AUDIO_CloseDevice(
   VOID)
{
   gAudioDevice.fOpened = FALSE;
   gAudioDevice.fMusicEnabled = FALSE;
}

SDL_AudioSpec *
AUDIO_GetDeviceSpec(
   VOID)
{
   return &gAudioDevice.spec;
}

VOID
AUDIO_IncreaseVolume(
   VOID)
{
   int volume = music_clamped_config_volume();

   volume = PAL_MAX_VOLUME - volume < 3
      ? PAL_MAX_VOLUME : volume + 3;
   gConfig.iMusicVolume = volume;
   gAudioDevice.iMusicVolume = music_sdl_volume();
}

VOID
AUDIO_DecreaseVolume(
   VOID)
{
   int volume = music_clamped_config_volume();

   volume = volume < 3 ? 0 : volume - 3;
   gConfig.iMusicVolume = volume;
   gAudioDevice.iMusicVolume = music_sdl_volume();
}

VOID
AUDIO_PlayMusic(
   INT track,
   BOOL loop,
   FLOAT fade_time)
{
   (void)track;
   (void)loop;
   (void)fade_time;
}

BOOL
AUDIO_PlayCDTrack(
   INT track)
{
   (void)track;
   return FALSE;
}

VOID
AUDIO_PlaySound(
   INT sound)
{
   (void)sound;
}

VOID
AUDIO_EnableMusic(
   BOOL enable)
{
   gAudioDevice.fMusicEnabled = enable ? TRUE : FALSE;
}

BOOL
AUDIO_MusicEnabled(
   VOID)
{
   return gAudioDevice.fMusicEnabled;
}

VOID
AUDIO_EnableSound(
   BOOL enable)
{
   (void)enable;
   gAudioDevice.fSoundEnabled = FALSE;
}

BOOL
AUDIO_SoundEnabled(
   VOID)
{
   return FALSE;
}

void
AUDIO_Lock(
   void)
{
}

void
AUDIO_Unlock(
   void)
{
}
