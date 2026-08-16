#include "pal_target_audio.h"

#include <stddef.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <driver/ledc.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hal/ledc_ll.h>
#include <soc/ledc_struct.h>

/*
 * The Xiaomiao has a bare passive piezo buzzer, not a speaker or DAC.  Match
 * the board's known Retro-Go topology: LEDC generates one 11-bit PWM period
 * per PCM sample and GPTimer updates its duty at that same physical rate. The
 * renderer remains logically 16.384 kHz; both peripherals share the nearest
 * representable APB-clock period. A fixed four-block SPSC ring replaces
 * Retro-Go's allocator-backed queue.
 */
#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "The Xiaomiao audio sink requires classic ESP32"
#endif

#if defined(__GNUC__)
#define PAL_XIAOMIAO_AUDIO_SRAM \
    __attribute__((section(".bss.pal_audio"), aligned(16)))
#else
#define PAL_XIAOMIAO_AUDIO_SRAM
#endif

static const char *TAG = "xiaomiao_audio";
static const gpio_num_t PIN_BUZZER_AUDIO = GPIO_NUM_14;

enum {
    AUDIO_RING_TICKS = 4,
    AUDIO_OUTPUT_GAIN = 3,
    AUDIO_PWM_DUTY_BITS = 11,
    AUDIO_PWM_DUTY_MIDPOINT = 1 << (AUDIO_PWM_DUTY_BITS - 1),
    AUDIO_SAMPLE_TIMER_RESOLUTION_HZ = 40000000,
    AUDIO_TASK_STACK_BYTES = 4096,
    AUDIO_STOP_TIMEOUT_MS = 2000,
    AUDIO_RUNTIME_REPORT_SECONDS = 10,
    AUDIO_RUNTIME_REPORT_US =
        1000000u * AUDIO_RUNTIME_REPORT_SECONDS,
    AUDIO_TICK_DEADLINE_US =
        (1000000u * PAL_TARGET_AUDIO_BLOCK_SAMPLES) /
            PAL_TARGET_AUDIO_SAMPLE_RATE,
    AUDIO_TICK_PERIOD_CEIL_US =
        (1000000u * PAL_TARGET_AUDIO_BLOCK_SAMPLES +
            PAL_TARGET_AUDIO_SAMPLE_RATE - 1u) /
            PAL_TARGET_AUDIO_SAMPLE_RATE,
};

static gptimer_handle_t audio_sample_timer;
static SemaphoreHandle_t audio_stopped_semaphore;
static TaskHandle_t audio_task_handle;
static PalTargetAudioRenderCallback audio_render;
static void *audio_render_user;

static StaticSemaphore_t
    pal_audio_stopped_semaphore_object PAL_XIAOMIAO_AUDIO_SRAM;
static StaticTask_t pal_audio_task_object PAL_XIAOMIAO_AUDIO_SRAM;
static uint8_t
    pal_sram_audio_task_stack_bytes[AUDIO_TASK_STACK_BYTES]
    PAL_XIAOMIAO_AUDIO_SRAM;
static int16_t
    pal_sram_audio_ring[AUDIO_RING_TICKS][PAL_TARGET_AUDIO_BLOCK_SAMPLES]
    PAL_XIAOMIAO_AUDIO_SRAM;
static PalTargetAudioTelemetry
    pal_audio_telemetry PAL_XIAOMIAO_AUDIO_SRAM;
static int64_t pal_audio_next_report_us PAL_XIAOMIAO_AUDIO_SRAM;

typedef char xiaomiao_audio_stack_bytes_are_uint8[
    sizeof(StackType_t) == sizeof(uint8_t) ? 1 : -1];
typedef char xiaomiao_audio_block_is_even[
    (PAL_TARGET_AUDIO_BLOCK_SAMPLES & 1u) == 0u ? 1 : -1];

static portMUX_TYPE audio_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool audio_started;
static bool audio_task_running;
static bool audio_paused;
static bool audio_stop_requested;
static bool audio_sync_initialized;
static bool audio_ledc_initialized;
static volatile uint8_t audio_ring_read_tick;
static volatile uint8_t audio_ring_write_tick;
static volatile uint8_t audio_ring_ready_ticks;
static volatile uint16_t audio_ring_read_sample;

static bool
audio_log_error(
    esp_err_t error,
    const char *what)
{
    if (error == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(error));
    return false;
}

static void
audio_drive_buzzer_low(void)
{
    (void)gpio_reset_pin(PIN_BUZZER_AUDIO);
    (void)gpio_set_direction(PIN_BUZZER_AUDIO, GPIO_MODE_OUTPUT);
    (void)gpio_set_level(PIN_BUZZER_AUDIO, 0);
}

static uint32_t IRAM_ATTR
audio_sample_to_duty(
    int16_t sample)
{
    int32_t amplified = (int32_t)sample * AUDIO_OUTPUT_GAIN;

    if (amplified > INT16_MAX) {
        amplified = INT16_MAX;
    } else if (amplified < INT16_MIN) {
        amplified = INT16_MIN;
    }
    return (uint32_t)(amplified - INT16_MIN) >>
        (16 - AUDIO_PWM_DUTY_BITS);
}

static bool
audio_destroy_pwm(void)
{
    esp_err_t error;
    bool ok = true;

    if (audio_sample_timer != NULL) {
        error = gptimer_stop(audio_sample_timer);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            (void)audio_log_error(error, "stop buzzer sample timer");
            ok = false;
        }
        error = gptimer_disable(audio_sample_timer);
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            (void)audio_log_error(error, "disable buzzer sample timer");
            ok = false;
        }
        error = gptimer_del_timer(audio_sample_timer);
        if (!audio_log_error(error, "release buzzer sample timer")) {
            ok = false;
        }
        audio_sample_timer = NULL;
    }
    if (audio_ledc_initialized) {
        if (!audio_log_error(
                ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0),
                "stop buzzer PWM")) {
            ok = false;
        }
        audio_ledc_initialized = false;
    }
    audio_drive_buzzer_low();
    return ok;
}

static bool IRAM_ATTR
audio_sample_alarm(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *event,
    void *user)
{
    BaseType_t task_woken = pdFALSE;
    int16_t sample = 0;
    bool consumed_tick = false;

    (void)timer;
    (void)event;
    (void)user;

    portENTER_CRITICAL_ISR(&audio_state_lock);
    if (audio_ring_ready_ticks != 0u) {
        sample = pal_sram_audio_ring[audio_ring_read_tick]
            [audio_ring_read_sample++];
        if (audio_ring_read_sample == PAL_TARGET_AUDIO_BLOCK_SAMPLES) {
            audio_ring_read_sample = 0u;
            audio_ring_read_tick =
                (uint8_t)((audio_ring_read_tick + 1u) % AUDIO_RING_TICKS);
            audio_ring_ready_ticks--;
            consumed_tick = true;
        }
    } else {
        pal_audio_telemetry.driver_send_queue_overflows++;
    }
    portEXIT_CRITICAL_ISR(&audio_state_lock);

    /*
     * IDF's public ledc_update_duty() waits for duty_start to clear.  At one
     * update per PWM period that wait can deadlock inside this ISR.  This
     * target is classic ESP32-only, so publish the same three register fields
     * without waiting; the low-speed update is latched on the next PWM cycle.
     */
    ledc_ll_set_duty_int_part(
        &LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
        audio_sample_to_duty(sample));
    LEDC.channel_group[LEDC_LOW_SPEED_MODE]
        .channel[LEDC_CHANNEL_0].conf1.duty_start = 1;
    ledc_ll_ls_channel_update(
        &LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (consumed_tick && audio_task_handle != NULL) {
        vTaskNotifyGiveFromISR(audio_task_handle, &task_woken);
    }
    return task_woken == pdTRUE;
}

static void
audio_sample_runtime_metrics(void)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    size_t internal_low_water =
        heap_caps_get_minimum_free_size(internal_caps);
    size_t dma_low_water =
        heap_caps_get_minimum_free_size(dma_caps);
    UBaseType_t stack_high_water =
        audio_task_handle != NULL
            ? uxTaskGetStackHighWaterMark(audio_task_handle)
            : 0;

    portENTER_CRITICAL(&audio_state_lock);
    if (pal_audio_telemetry.minimum_internal_free == 0 ||
        internal_low_water < pal_audio_telemetry.minimum_internal_free) {
        pal_audio_telemetry.minimum_internal_free =
            (uint32_t)internal_low_water;
    }
    if (pal_audio_telemetry.minimum_dma_free == 0 ||
        dma_low_water < pal_audio_telemetry.minimum_dma_free) {
        pal_audio_telemetry.minimum_dma_free =
            (uint32_t)dma_low_water;
    }
    pal_audio_telemetry.task_stack_high_water_bytes =
        (uint32_t)stack_high_water * sizeof(StackType_t);
    portEXIT_CRITICAL(&audio_state_lock);
}

static bool
audio_wait_for_free_tick(
    uint8_t *tick)
{
    if (tick == NULL) {
        return false;
    }
    for (;;) {
        bool stopping;

        portENTER_CRITICAL(&audio_state_lock);
        stopping = audio_stop_requested;
        if (!stopping && audio_ring_ready_ticks < AUDIO_RING_TICKS) {
            *tick = audio_ring_write_tick;
            portEXIT_CRITICAL(&audio_state_lock);
            return true;
        }
        portEXIT_CRITICAL(&audio_state_lock);
        if (stopping) {
            return false;
        }
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static void
audio_publish_tick(void)
{
    portENTER_CRITICAL(&audio_state_lock);
    audio_ring_write_tick =
        (uint8_t)((audio_ring_write_tick + 1u) % AUDIO_RING_TICKS);
    audio_ring_ready_ticks++;
    portEXIT_CRITICAL(&audio_state_lock);
}

static void
audio_task(
    void *argument)
{
    (void)argument;

    for (;;) {
        int64_t previous_tick_start = 0;

        for (;;) {
            int64_t render_start;
            int64_t write_start;
            uint32_t render_us;
            uint32_t write_us;
            uint32_t tick_peak = 0;
            uint32_t tick_gap_us = 0;
            uint32_t tick_gap_excess_us = 0;
            bool paused;
            bool stopping;
            bool write_ok;
            uint8_t write_tick;
            int16_t *samples;
            size_t sample;

            write_start = esp_timer_get_time();
            write_ok = audio_wait_for_free_tick(&write_tick);
            write_us =
                (uint32_t)(esp_timer_get_time() - write_start);
            if (!write_ok) {
                break;
            }
            samples = pal_sram_audio_ring[write_tick];

            portENTER_CRITICAL(&audio_state_lock);
            paused = audio_paused;
            stopping = audio_stop_requested;
            portEXIT_CRITICAL(&audio_state_lock);
            if (stopping) {
                break;
            }

            memset(samples, 0,
                PAL_TARGET_AUDIO_BLOCK_SAMPLES * sizeof(*samples));
            render_start = esp_timer_get_time();
            if (previous_tick_start != 0 &&
                render_start > previous_tick_start) {
                uint64_t gap =
                    (uint64_t)(render_start - previous_tick_start);

                tick_gap_us = gap > UINT32_MAX
                    ? UINT32_MAX : (uint32_t)gap;
                if (tick_gap_us > AUDIO_TICK_PERIOD_CEIL_US) {
                    tick_gap_excess_us =
                        tick_gap_us - AUDIO_TICK_PERIOD_CEIL_US;
                }
            }
            previous_tick_start = render_start;
            if (!paused && audio_render != NULL) {
                audio_render(
                    audio_render_user,
                    samples,
                    PAL_TARGET_AUDIO_BLOCK_SAMPLES);
                for (sample = 0;
                     sample < PAL_TARGET_AUDIO_BLOCK_SAMPLES;
                     sample++) {
                    int32_t value = samples[sample];
                    uint32_t magnitude =
                        value < 0 ? (uint32_t)-value : (uint32_t)value;

                    if (magnitude > tick_peak) {
                        tick_peak = magnitude;
                    }
                }
            }
            render_us =
                (uint32_t)(esp_timer_get_time() - render_start);
            audio_publish_tick();

            portENTER_CRITICAL(&audio_state_lock);
            pal_audio_telemetry.rendered_ticks++;
            pal_audio_telemetry.rendered_samples +=
                PAL_TARGET_AUDIO_BLOCK_SAMPLES;
            if (tick_peak != 0) {
                pal_audio_telemetry.nonzero_ticks++;
            }
            if (tick_peak > pal_audio_telemetry.peak_abs_sample) {
                pal_audio_telemetry.peak_abs_sample = tick_peak;
            }
            if (render_us > pal_audio_telemetry.max_render_us) {
                pal_audio_telemetry.max_render_us = render_us;
            }
            if (tick_gap_us > pal_audio_telemetry.max_tick_gap_us) {
                pal_audio_telemetry.max_tick_gap_us = tick_gap_us;
            }
            if (tick_gap_excess_us >
                pal_audio_telemetry.max_tick_gap_excess_us) {
                pal_audio_telemetry.max_tick_gap_excess_us =
                    tick_gap_excess_us;
            }
            if (render_us > AUDIO_TICK_DEADLINE_US) {
                pal_audio_telemetry.render_deadline_misses++;
            }
            portEXIT_CRITICAL(&audio_state_lock);

            portENTER_CRITICAL(&audio_state_lock);
            if (write_us > pal_audio_telemetry.max_write_us) {
                pal_audio_telemetry.max_write_us = write_us;
            }
            portEXIT_CRITICAL(&audio_state_lock);
        }

        audio_sample_runtime_metrics();
        portENTER_CRITICAL(&audio_state_lock);
        audio_task_running = false;
        portEXIT_CRITICAL(&audio_state_lock);
        xSemaphoreGive(audio_stopped_semaphore);
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static bool
audio_init_pwm(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_11_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = PAL_TARGET_AUDIO_SAMPLE_RATE,
        .clk_cfg = LEDC_USE_APB_CLK,
    };
    ledc_channel_config_t ledc_channel = {
        .gpio_num = PIN_BUZZER_AUDIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = AUDIO_PWM_DUTY_MIDPOINT,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = AUDIO_SAMPLE_TIMER_RESOLUTION_HZ,
    };
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 0,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_event_callbacks_t callbacks = {
        .on_alarm = audio_sample_alarm,
    };
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    uint32_t internal_free_after_driver;
    uint32_t dma_free_after_driver;
    uint32_t minimum_internal_free;
    uint32_t minimum_dma_free;
    uint32_t pwm_frequency_hz;

    if ((audio_sample_timer != NULL || audio_ledc_initialized) &&
        !audio_destroy_pwm()) {
        return false;
    }
    audio_drive_buzzer_low();
    portENTER_CRITICAL(&audio_state_lock);
    audio_ring_read_tick = 0u;
    audio_ring_write_tick = 0u;
    audio_ring_ready_ticks = 0u;
    audio_ring_read_sample = 0u;
    portEXIT_CRITICAL(&audio_state_lock);

    if (!audio_log_error(
            ledc_timer_config(&ledc_timer),
            "configure buzzer PWM timer") ||
        !audio_log_error(
            ledc_channel_config(&ledc_channel),
            "configure buzzer PWM channel")) {
        audio_drive_buzzer_low();
        return false;
    }
    audio_ledc_initialized = true;
    /*
     * LEDC's 8-bit fractional divider cannot represent 16,384 Hz exactly at
     * 11-bit duty resolution. Drive GPTimer from the reported physical PWM
     * rate instead of an independently rounded nominal rate; otherwise their
     * phases drift and one PCM value is periodically held for an extra PWM
     * cycle. With the 80 MHz APB clock this selects 4,880 APB clocks per
     * sample (about 16,393.44 Hz) for both peripherals.
     */
    pwm_frequency_hz = ledc_get_freq(
        LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    if (pwm_frequency_hz == 0u) {
        ESP_LOGE(TAG, "read configured buzzer PWM frequency");
        (void)audio_destroy_pwm();
        return false;
    }
    alarm_config.alarm_count =
        (AUDIO_SAMPLE_TIMER_RESOLUTION_HZ + pwm_frequency_hz / 2u) /
        pwm_frequency_hz;
    if (alarm_config.alarm_count == 0u) {
        ESP_LOGE(TAG, "invalid buzzer sample alarm period");
        (void)audio_destroy_pwm();
        return false;
    }
    ESP_LOGI(
        TAG,
        "aligned PWM/sample clock: PWM=%u Hz GPTimer=%u/%llu Hz",
        (unsigned)pwm_frequency_hz,
        (unsigned)AUDIO_SAMPLE_TIMER_RESOLUTION_HZ,
        (unsigned long long)alarm_config.alarm_count);
    if (!audio_log_error(
            gptimer_new_timer(&timer_config, &audio_sample_timer),
            "create buzzer sample timer") ||
        !audio_log_error(
            gptimer_register_event_callbacks(
                audio_sample_timer, &callbacks, NULL),
            "register buzzer sample callback") ||
        !audio_log_error(
            gptimer_set_alarm_action(
                audio_sample_timer, &alarm_config),
            "configure buzzer sample alarm") ||
        !audio_log_error(
            gptimer_enable(audio_sample_timer),
            "enable buzzer sample timer") ||
        !audio_log_error(
            gptimer_start(audio_sample_timer),
            "start buzzer sample timer")) {
        (void)audio_destroy_pwm();
        return false;
    }
    internal_free_after_driver =
        (uint32_t)heap_caps_get_free_size(internal_caps);
    dma_free_after_driver =
        (uint32_t)heap_caps_get_free_size(dma_caps);
    minimum_internal_free =
        (uint32_t)heap_caps_get_minimum_free_size(internal_caps);
    minimum_dma_free =
        (uint32_t)heap_caps_get_minimum_free_size(dma_caps);
    portENTER_CRITICAL(&audio_state_lock);
    pal_audio_telemetry.driver_dma_bytes = 0u;
    pal_audio_telemetry.internal_free_after_driver =
        internal_free_after_driver;
    pal_audio_telemetry.dma_free_after_driver =
        dma_free_after_driver;
    pal_audio_telemetry.minimum_internal_free =
        minimum_internal_free;
    pal_audio_telemetry.minimum_dma_free =
        minimum_dma_free;
    portEXIT_CRITICAL(&audio_state_lock);
    return true;
}

bool
PalTargetAudio_Begin(
    PalTargetAudioRenderCallback render,
    void *user)
{
    TaskHandle_t task;
    bool started;
    bool task_running;
    bool restarting;
    uint32_t internal_free_before_driver;
    uint32_t dma_free_before_driver;

    if (render == NULL) {
        ESP_LOGE(TAG, "audio renderer is null");
        return false;
    }
    portENTER_CRITICAL(&audio_state_lock);
    started = audio_started;
    task_running = audio_task_running;
    portEXIT_CRITICAL(&audio_state_lock);
    if (started) {
        return task_running &&
            audio_render == render && audio_render_user == user;
    }

    internal_free_before_driver =
        (uint32_t)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    dma_free_before_driver =
        (uint32_t)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    portENTER_CRITICAL(&audio_state_lock);
    memset(&pal_audio_telemetry, 0, sizeof(pal_audio_telemetry));
    pal_audio_next_report_us = 0;
    pal_audio_telemetry.internal_free_before_driver =
        internal_free_before_driver;
    pal_audio_telemetry.dma_free_before_driver =
        dma_free_before_driver;
    portEXIT_CRITICAL(&audio_state_lock);

    if (!audio_sync_initialized) {
        audio_stopped_semaphore = xSemaphoreCreateBinaryStatic(
            &pal_audio_stopped_semaphore_object);
        if (audio_stopped_semaphore == NULL) {
            ESP_LOGE(TAG, "create static audio stop semaphore");
            return false;
        }
        audio_sync_initialized = true;
    } else {
        (void)xSemaphoreTake(audio_stopped_semaphore, 0);
    }

    audio_render = render;
    audio_render_user = user;
    audio_paused = false;
    audio_stop_requested = false;
    if (!audio_init_pwm()) {
        audio_render = NULL;
        audio_render_user = NULL;
        return false;
    }

    portENTER_CRITICAL(&audio_state_lock);
    audio_started = true;
    audio_task_running = true;
    pal_audio_telemetry.started = true;
    pal_audio_next_report_us =
        esp_timer_get_time() + AUDIO_RUNTIME_REPORT_US;
    portEXIT_CRITICAL(&audio_state_lock);

    restarting = audio_task_handle != NULL;
    if (restarting) {
        (void)xTaskNotifyGive(audio_task_handle);
    } else {
        task = xTaskCreateStatic(
            audio_task,
            "pal_music",
            AUDIO_TASK_STACK_BYTES,
            NULL,
            configMAX_PRIORITIES - 3,
            (StackType_t *)(void *)pal_sram_audio_task_stack_bytes,
            &pal_audio_task_object);
        if (task == NULL) {
            ESP_LOGE(TAG, "create static audio task");
            portENTER_CRITICAL(&audio_state_lock);
            audio_started = false;
            audio_task_running = false;
            pal_audio_telemetry.started = false;
            pal_audio_next_report_us = 0;
            portEXIT_CRITICAL(&audio_state_lock);
            (void)audio_destroy_pwm();
            audio_render = NULL;
            audio_render_user = NULL;
            return false;
        }
        audio_task_handle = task;
    }

    ESP_LOGI(
        TAG,
        "audio sink ready: %u Hz mono PCM16 -> 11-bit LEDC PWM "
        "GPIO%d, %u samples/block, ring=%u blocks",
        PAL_TARGET_AUDIO_SAMPLE_RATE,
        PIN_BUZZER_AUDIO,
        PAL_TARGET_AUDIO_BLOCK_SAMPLES,
        AUDIO_RING_TICKS);
    return true;
}

bool
PalTargetAudio_SetPaused(
    bool paused)
{
    bool available;

    portENTER_CRITICAL(&audio_state_lock);
    available = audio_started && audio_task_running;
    if (available) {
        audio_paused = paused;
        pal_audio_telemetry.paused = paused;
    }
    portEXIT_CRITICAL(&audio_state_lock);
    return available;
}

bool
PalTargetAudio_Stop(void)
{
    bool started;
    bool running;

    portENTER_CRITICAL(&audio_state_lock);
    started = audio_started;
    running = audio_task_running;
    if (started && running) {
        audio_stop_requested = true;
    }
    portEXIT_CRITICAL(&audio_state_lock);
    if (!started) {
        return true;
    }
    if (audio_task_handle == NULL || audio_stopped_semaphore == NULL) {
        ESP_LOGE(TAG, "audio task is not available for shutdown");
        return false;
    }
    if (running) {
        (void)xTaskNotifyGive(audio_task_handle);
    }
    if (running &&
        xSemaphoreTake(
            audio_stopped_semaphore,
            pdMS_TO_TICKS(AUDIO_STOP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "audio task did not stop");
        return false;
    }
    if (!audio_destroy_pwm()) {
        ESP_LOGE(TAG, "audio shutdown can be retried");
        return false;
    }

    portENTER_CRITICAL(&audio_state_lock);
    audio_started = false;
    audio_stop_requested = false;
    pal_audio_telemetry.started = false;
    pal_audio_next_report_us = 0;
    portEXIT_CRITICAL(&audio_state_lock);
    audio_render = NULL;
    audio_render_user = NULL;
    return true;
}

bool
PalTargetAudio_Started(void)
{
    bool started;

    portENTER_CRITICAL(&audio_state_lock);
    started = audio_started;
    portEXIT_CRITICAL(&audio_state_lock);
    return started;
}

void
PalTargetAudio_GetTelemetry(
    PalTargetAudioTelemetry *telemetry)
{
    if (telemetry == NULL) {
        return;
    }
    portENTER_CRITICAL(&audio_state_lock);
    *telemetry = pal_audio_telemetry;
    portEXIT_CRITICAL(&audio_state_lock);
}

void
PalTargetAudio_RecordSourceFault(
    int32_t source_code)
{
    portENTER_CRITICAL(&audio_state_lock);
    pal_audio_telemetry.source_faults++;
    pal_audio_telemetry.last_source_fault = source_code;
    portEXIT_CRITICAL(&audio_state_lock);
}

void
PalTargetAudio_PollTelemetry(void)
{
    int64_t now = esp_timer_get_time();
    bool report = false;

    portENTER_CRITICAL(&audio_state_lock);
    if (audio_started &&
        pal_audio_next_report_us != 0 &&
        now >= pal_audio_next_report_us) {
        pal_audio_next_report_us = now + AUDIO_RUNTIME_REPORT_US;
        report = true;
    }
    portEXIT_CRITICAL(&audio_state_lock);

    if (report) {
        audio_sample_runtime_metrics();
        PalTargetAudio_LogTelemetry("runtime");
    }
}

void
PalTargetAudio_LogTelemetry(
    const char *stage)
{
    PalTargetAudioTelemetry telemetry;

    PalTargetAudio_GetTelemetry(&telemetry);
    ESP_LOGI(
        TAG,
        "stage=%s started=%u paused=%u ticks=%u samples=%u "
        "nonzero=%u peak=%u render_max_us=%u deadline_miss=%u "
        "tick_gap_max_us=%u tick_gap_excess_max_us=%u "
        "write_max_us=%u write_err=%u last_write_err=%d "
        "zero_write=%u send_q_ovf=%u source_fault=%u "
        "last_source=%d stack_free_min=%u driver_dma=%u "
        "internal=%u->%u low=%u dma=%u->%u low=%u",
        stage != NULL ? stage : "?",
        telemetry.started,
        telemetry.paused,
        telemetry.rendered_ticks,
        telemetry.rendered_samples,
        telemetry.nonzero_ticks,
        telemetry.peak_abs_sample,
        telemetry.max_render_us,
        telemetry.render_deadline_misses,
        telemetry.max_tick_gap_us,
        telemetry.max_tick_gap_excess_us,
        telemetry.max_write_us,
        telemetry.write_errors,
        telemetry.last_write_error,
        telemetry.zero_progress_writes,
        telemetry.driver_send_queue_overflows,
        telemetry.source_faults,
        telemetry.last_source_fault,
        telemetry.task_stack_high_water_bytes,
        telemetry.driver_dma_bytes,
        telemetry.internal_free_before_driver,
        telemetry.internal_free_after_driver,
        telemetry.minimum_internal_free,
        telemetry.dma_free_before_driver,
        telemetry.dma_free_after_driver,
        telemetry.minimum_dma_free);
}
