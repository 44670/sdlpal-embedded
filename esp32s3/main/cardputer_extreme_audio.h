#ifndef CARDPUTER_EXTREME_AUDIO_H
#define CARDPUTER_EXTREME_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PAL's RIX sequencer advances at 70 Hz.  22050 / 70 is exactly 315, so each
 * callback is one complete sequencer tick without a fractional-rate
 * accumulator or resampler.
 */
#define CARDPUTER_EXTREME_AUDIO_SAMPLE_RATE 22050u
#define CARDPUTER_EXTREME_AUDIO_TICK_HZ 70u
#define CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES 315u

typedef void (*CardputerExtremeAudioRenderCallback)(
    void *user,
    int16_t *samples,
    size_t sample_count);

typedef struct CardputerExtremeAudioTelemetry {
    uint32_t rendered_ticks;
    uint32_t rendered_samples;
    uint32_t render_deadline_misses;
    uint32_t write_errors;
    uint32_t command_queue_overflows;
    uint32_t driver_send_queue_overflows;
    uint32_t max_render_us;
    uint32_t max_write_us;
    /* Smallest free audio-task stack observed, in bytes. */
    uint32_t task_stack_high_water_bytes;
    uint32_t driver_dma_bytes;
    uint32_t internal_free_before_driver;
    uint32_t internal_free_after_driver;
    uint32_t dma_free_before_driver;
    uint32_t dma_free_after_driver;
    /* ESP-IDF heap low-water marks for the requested capability sets. */
    uint32_t minimum_internal_free;
    uint32_t minimum_dma_free;
    bool started;
    bool paused;
} CardputerExtremeAudioTelemetry;

/*
 * Start the Cardputer ADV ES8311/I2S music sink.
 *
 * The caller must first finish CardputerExtreme_Begin(), which creates the
 * shared GPIO8/GPIO9 I2C bus.  The callback executes only in the dedicated
 * audio task and must fill exactly sample_count signed mono PCM16 samples.
 * It must not block on TF I/O or call CardputerExtremeAudio_* recursively.
 * Begin() and Stop() are lifecycle calls and must be serialized by their
 * owner; SetPaused() may be sent while the audio task is running.
 */
bool CardputerExtremeAudio_Begin(
    CardputerExtremeAudioRenderCallback render,
    void *user);

/*
 * Pausing keeps BCLK/LRCK and the I2S DMA stream alive, outputs zero samples,
 * and stops invoking the renderer.  This freezes RIX/OPL playback state.
 */
bool CardputerExtremeAudio_SetPaused(bool paused);

/*
 * Stop the current backend instance.  This is the AUDIO_CloseDevice lifecycle
 * operation, not the way to stop an individual music track.  A later Begin()
 * reuses the same static task storage.  If Stop() reports a driver shutdown
 * failure, retry Stop() before calling Begin().
 */
bool CardputerExtremeAudio_Stop(void);

bool CardputerExtremeAudio_Started(void);
void CardputerExtremeAudio_GetTelemetry(
    CardputerExtremeAudioTelemetry *telemetry);
/*
 * The backend emits this snapshot itself about every ten seconds while
 * running.  Lifecycle code may call it for additional named checkpoints.
 */
void CardputerExtremeAudio_LogTelemetry(const char *stage);

#ifdef __cplusplus
}
#endif

#endif
