#include "pal_engine_guru.h"

#if defined(PAL_TARGET_GURU_MEDITATION)

#include "pal_target_board.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>

#include <stdbool.h>

void AUDIO_CloseDevice(void);

void
PalEngineBridge_GuruMeditationAt(
    const char *file,
    uint32_t line,
    const char *reason)
{
    static bool entering;

    ESP_LOGE("pal_guru", "fatal request at %s:%lu: %s",
        file != NULL ? file : "?", (unsigned long)line,
        reason != NULL ? reason : "FATAL ERROR");
    if (!entering) {
        entering = true;
        AUDIO_CloseDevice();
    }
    PalTarget_GuruMeditation(file, line, reason);

    /* Keep the diagnostic pixels visible and stop the game task permanently. */
    vTaskSuspend(NULL);
    for (;;) {
    }
}

#endif
