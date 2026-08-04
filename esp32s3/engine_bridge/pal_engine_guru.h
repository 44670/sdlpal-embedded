#ifndef PAL_ENGINE_GURU_H
#define PAL_ENGINE_GURU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(PAL_TARGET_GURU_MEDITATION)
void PalEngineBridge_GuruMeditationAt(
    const char *file,
    uint32_t line,
    const char *reason) __attribute__((noreturn));

#define PAL_GURU_MEDITATION(reason) \
    PalEngineBridge_GuruMeditationAt(__FILE__, __LINE__, (reason))
#endif

#ifdef __cplusplus
}
#endif

#endif
