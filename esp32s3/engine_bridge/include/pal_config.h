#ifndef PAL_CORES3SE_ENGINE_BRIDGE_PAL_CONFIG_H
#define PAL_CORES3SE_ENGINE_BRIDGE_PAL_CONFIG_H

#ifndef PAL_HAS_JOYSTICKS
#define PAL_HAS_JOYSTICKS 0
#endif

#ifndef PAL_HAS_TOUCH
#define PAL_HAS_TOUCH 0
#endif

#define PAL_DEFAULT_WINDOW_WIDTH 320
#define PAL_DEFAULT_WINDOW_HEIGHT 240
#define PAL_DEFAULT_FULLSCREEN_HEIGHT 240
#define PAL_DEFAULT_TEXTURE_WIDTH 320
#define PAL_DEFAULT_TEXTURE_HEIGHT 240

#ifdef PAL_CORES3SE_NATIVE_BRIDGE
#define PAL_PREFIX "./"
#define PAL_SAVE_PREFIX "./"
#else
#define PAL_PREFIX "0:/"
#define PAL_SAVE_PREFIX "0:/"
#endif
#define PAL_CONFIG_PREFIX PAL_PREFIX

#define PAL_VIDEO_INIT_FLAGS (SDL_WINDOW_SHOWN)
#define PAL_SDL_INIT_FLAGS (SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE)

#define PAL_PLATFORM "M5Stack CoreS3 SE"
#define PAL_CREDIT NULL
#define PAL_PORTYEAR NULL

#define PAL_HAS_CONFIG_PAGE 0
#define PAL_HAS_NATIVEMIDI 0
#define PAL_HAS_MP3 0
#define PAL_HAS_OGG 0
#define PAL_HAS_OPUS 0
#define PAL_HAS_GLSL 0

#define PAL_FATAL_OUTPUT(s)

#include <sys/time.h>

#if PAL_DETERMINISTIC
PAL_C_LINKAGE Uint32 PAL_DeterministicGetTicks(void);
PAL_C_LINKAGE Uint64 PAL_DeterministicGetPerformanceCounter(void);
PAL_C_LINKAGE Uint64 PAL_DeterministicGetPerformanceFrequency(void);
PAL_C_LINKAGE void PAL_DeterministicDelay(Uint32 ms);
PAL_C_LINKAGE time_t PAL_DeterministicTime(time_t *timer);
PAL_C_LINKAGE int PAL_DeterministicPollEvent(SDL_Event *event);
#define SDL_GetTicks PAL_DeterministicGetTicks
#define SDL_GetPerformanceCounter PAL_DeterministicGetPerformanceCounter
#define SDL_GetPerformanceFrequency PAL_DeterministicGetPerformanceFrequency
#define SDL_Delay PAL_DeterministicDelay
#define SDL_PollEvent PAL_DeterministicPollEvent
#define time(timer) PAL_DeterministicTime((timer))
#endif

#endif
