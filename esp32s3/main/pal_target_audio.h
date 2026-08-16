#ifndef PAL_TARGET_AUDIO_H
#define PAL_TARGET_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The renderer/sink contract consumes fixed 256-frame PCM blocks at a logical
 * 16.384 kHz. RIX remains a separate 70 Hz clock and is advanced fractionally
 * by the renderer. Individual hardware clock dividers may quantize the
 * physical rate slightly.
 */
#define PAL_TARGET_AUDIO_SAMPLE_RATE 16384u
#define PAL_TARGET_AUDIO_RIX_HZ 70u
#define PAL_TARGET_AUDIO_BLOCK_SAMPLES 256u
#define PAL_TARGET_SFX_SAMPLE_RATE 8192u
#define PAL_TARGET_SFX_BUFFER_SAMPLES (PAL_TARGET_SFX_SAMPLE_RATE * 5u)

#if defined(PAL_TARGET_CARDPUTER_ADV)

/* Keep the already-proven Cardputer backend ABI behind the common sink. */
#include "cardputer_extreme_audio.h"

#if CARDPUTER_EXTREME_AUDIO_SAMPLE_RATE != PAL_TARGET_AUDIO_SAMPLE_RATE || \
    CARDPUTER_EXTREME_AUDIO_BLOCK_SAMPLES != PAL_TARGET_AUDIO_BLOCK_SAMPLES
#error "Cardputer and common target-audio timing contracts disagree"
#endif

typedef CardputerExtremeAudioRenderCallback PalTargetAudioRenderCallback;
typedef CardputerExtremeAudioTelemetry PalTargetAudioTelemetry;

#define PalTargetAudio_Begin CardputerExtremeAudio_Begin
#define PalTargetAudio_SetPaused CardputerExtremeAudio_SetPaused
#define PalTargetAudio_Stop CardputerExtremeAudio_Stop
#define PalTargetAudio_Started CardputerExtremeAudio_Started
#define PalTargetAudio_GetTelemetry CardputerExtremeAudio_GetTelemetry
#define PalTargetAudio_RecordSourceFault \
    CardputerExtremeAudio_RecordSourceFault
#define PalTargetAudio_PollTelemetry CardputerExtremeAudio_PollTelemetry
#define PalTargetAudio_LogTelemetry CardputerExtremeAudio_LogTelemetry

#elif defined(PAL_TARGET_XIAOMIAO)

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PalTargetAudioRenderCallback)(
    void *user,
    int16_t *samples,
    size_t sample_count);

typedef struct PalTargetAudioTelemetry {
    uint32_t rendered_ticks;
    uint32_t rendered_samples;
    uint32_t nonzero_ticks;
    uint32_t peak_abs_sample;
    uint32_t render_deadline_misses;
    uint32_t max_tick_gap_us;
    uint32_t max_tick_gap_excess_us;
    uint32_t write_errors;
    int32_t last_write_error;
    uint32_t zero_progress_writes;
    uint32_t command_queue_overflows;
    uint32_t driver_send_queue_overflows;
    uint32_t source_faults;
    int32_t last_source_fault;
    uint32_t max_render_us;
    uint32_t max_write_us;
    uint32_t task_stack_high_water_bytes;
    uint32_t driver_dma_bytes;
    uint32_t internal_free_before_driver;
    uint32_t internal_free_after_driver;
    uint32_t dma_free_before_driver;
    uint32_t dma_free_after_driver;
    uint32_t minimum_internal_free;
    uint32_t minimum_dma_free;
    bool started;
    bool paused;
} PalTargetAudioTelemetry;

bool PalTargetAudio_Begin(
    PalTargetAudioRenderCallback render,
    void *user);
bool PalTargetAudio_SetPaused(bool paused);
bool PalTargetAudio_Stop(void);
bool PalTargetAudio_Started(void);
void PalTargetAudio_GetTelemetry(PalTargetAudioTelemetry *telemetry);
void PalTargetAudio_RecordSourceFault(int32_t source_code);
void PalTargetAudio_PollTelemetry(void);
void PalTargetAudio_LogTelemetry(const char *stage);

#ifdef __cplusplus
}
#endif

#else
#error "PAL target audio requires a supported board target"
#endif

#endif
