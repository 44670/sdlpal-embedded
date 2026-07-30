#include "cardputer_extreme_audio.h"

#include "cardputer_extreme_board_internal.h"

#include <stddef.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

/*
 * Project-owned audio storage is fixed and visible in the ELF.  ESP-IDF's I2S
 * drivers still create the ES8311 device, I2S channel, and DMA descriptors
 * internally; Begin() records before/after heap telemetry so that hidden
 * driver residency is part of the extreme-profile hardware budget.
 */
#if defined(__GNUC__)
#define PAL_EXTREME_AUDIO_SRAM \
    __attribute__((section(".bss.pal_audio"), aligned(16)))
#else
#define PAL_EXTREME_AUDIO_SRAM
#endif

static const char *TAG = "cardputer_audio";

static const gpio_num_t PIN_I2S_BCLK = GPIO_NUM_41;
static const gpio_num_t PIN_I2S_WS = GPIO_NUM_43;
static const gpio_num_t PIN_I2S_DOUT = GPIO_NUM_42;
static const uint8_t ES8311_ADDRESS = 0x18u;
static const uint32_t ES8311_I2C_CLOCK_HZ = 400000u;
static const uint32_t ES8311_I2C_TIMEOUT_MS = 1000u;

enum {
    AUDIO_DMA_DESCRIPTOR_COUNT = 4,
    AUDIO_TASK_STACK_BYTES = 4096,
    AUDIO_COMMAND_QUEUE_LENGTH = 4,
    AUDIO_STOP_TIMEOUT_MS = 2000,
    AUDIO_WRITE_TIMEOUT_MS = 1000,
    AUDIO_WRITE_RETRY_DELAY_MS =
        1000u / CARDPUTER_EXTREME_AUDIO_TICK_HZ,
    AUDIO_RUNTIME_REPORT_SECONDS = 10,
    AUDIO_RUNTIME_REPORT_TICKS =
        CARDPUTER_EXTREME_AUDIO_TICK_HZ *
        AUDIO_RUNTIME_REPORT_SECONDS,
    AUDIO_TICK_DEADLINE_US =
        1000000u / CARDPUTER_EXTREME_AUDIO_TICK_HZ,
};

typedef enum CardputerExtremeAudioCommandType {
    CARDPUTER_EXTREME_AUDIO_COMMAND_PAUSE,
    CARDPUTER_EXTREME_AUDIO_COMMAND_RESUME,
    CARDPUTER_EXTREME_AUDIO_COMMAND_STOP,
} CardputerExtremeAudioCommandType;

typedef struct CardputerExtremeAudioCommand {
    CardputerExtremeAudioCommandType type;
} CardputerExtremeAudioCommand;

typedef struct Es8311Register {
    uint8_t reg;
    uint8_t value;
} Es8311Register;

/*
 * This is the Cardputer ADV speaker sequence used by both M5Unified and the
 * locally proven esp-walkie-talkie port.  Register 0x01 selects BCLK as MCLK,
 * which is why the I2S GPIO configuration deliberately omits an MCLK pin.
 */
static const Es8311Register es8311_speaker_enable[] = {
    {0x00, 0x80},
    {0x01, 0xB5},
    {0x02, 0x18},
    {0x0D, 0x01},
    {0x12, 0x00},
    {0x13, 0x10},
    {0x32, 0xBF},
    {0x37, 0x08},
};

static i2c_master_dev_handle_t es8311_device;
static i2s_chan_handle_t i2s_tx_channel;
static QueueHandle_t audio_command_queue;
static SemaphoreHandle_t audio_stopped_semaphore;
static TaskHandle_t audio_task_handle;
static CardputerExtremeAudioRenderCallback audio_render;
static void *audio_render_user;

static StaticQueue_t pal_audio_command_queue_object PAL_EXTREME_AUDIO_SRAM;
static uint8_t
    pal_audio_command_queue_storage[
        AUDIO_COMMAND_QUEUE_LENGTH * sizeof(CardputerExtremeAudioCommand)]
    PAL_EXTREME_AUDIO_SRAM;
static StaticSemaphore_t
    pal_audio_stopped_semaphore_object PAL_EXTREME_AUDIO_SRAM;
static StaticTask_t pal_audio_task_object PAL_EXTREME_AUDIO_SRAM;
static uint8_t
    pal_sram_audio_task_stack_bytes[AUDIO_TASK_STACK_BYTES]
    PAL_EXTREME_AUDIO_SRAM;
static uint8_t
    pal_sram_audio_tick_bytes[
        CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES * sizeof(int16_t)]
    PAL_EXTREME_AUDIO_SRAM;
static CardputerExtremeAudioTelemetry
    pal_audio_telemetry PAL_EXTREME_AUDIO_SRAM;

typedef char cardputer_extreme_audio_stack_bytes_are_uint8[
    sizeof(StackType_t) == sizeof(uint8_t) ? 1 : -1];
typedef char cardputer_extreme_audio_tick_rate_is_integral[
    CARDPUTER_EXTREME_AUDIO_SAMPLE_RATE %
                CARDPUTER_EXTREME_AUDIO_TICK_HZ ==
            0
        ? 1
        : -1];
typedef char cardputer_extreme_audio_tick_size_matches_rate[
    CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES ==
            CARDPUTER_EXTREME_AUDIO_SAMPLE_RATE /
                CARDPUTER_EXTREME_AUDIO_TICK_HZ
        ? 1
        : -1];

#define PAL_AUDIO_TICK_BUFFER \
    ((int16_t *)(void *)pal_sram_audio_tick_bytes)

static portMUX_TYPE audio_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool audio_started;
static bool audio_task_running;
static bool audio_paused;
static bool audio_sync_initialized;

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

static bool
audio_destroy_i2s(void)
{
    esp_err_t error;

    if (i2s_tx_channel == NULL) {
        return true;
    }

    error = i2s_channel_disable(i2s_tx_channel);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        (void)audio_log_error(error, "disable I2S1 TX channel");
    }
    error = i2s_del_channel(i2s_tx_channel);
    if (!audio_log_error(error, "release I2S1 TX channel")) {
        return false;
    }
    i2s_tx_channel = NULL;
    return true;
}

static bool
es8311_write_register(
    uint8_t reg,
    uint8_t value)
{
    const uint8_t bytes[] = {reg, value};

    return es8311_device != NULL &&
           audio_log_error(
               i2c_master_transmit(
                   es8311_device,
                   bytes,
                   sizeof(bytes),
                   ES8311_I2C_TIMEOUT_MS),
               "write ES8311");
}

static bool
es8311_add_to_shared_bus(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device = NULL;
    i2c_device_config_t config = {0};
    esp_err_t error;

    if (es8311_device != NULL) {
        return true;
    }

    bus = CardputerExtreme_I2cBus();
    if (bus == NULL) {
        ESP_LOGE(TAG, "shared Cardputer I2C bus is not initialized");
        return false;
    }

    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = ES8311_ADDRESS;
    config.scl_speed_hz = ES8311_I2C_CLOCK_HZ;
    error = i2c_master_bus_add_device(bus, &config, &device);
    if (!audio_log_error(error, "add ES8311")) {
        return false;
    }
    es8311_device = device;
    return true;
}

static bool
es8311_enable_speaker(void)
{
    size_t i;

    for (i = 0;
         i < sizeof(es8311_speaker_enable) /
                 sizeof(es8311_speaker_enable[0]);
         i++) {
        if (!es8311_write_register(
                es8311_speaker_enable[i].reg,
                es8311_speaker_enable[i].value)) {
            return false;
        }
    }
    return true;
}

static void
es8311_mute_speaker(void)
{
    /*
     * DAC volume 0 is -95.5 dB.  Keep the already-added I2C device registered
     * so shutdown does not need to destroy/recreate project-visible state.
     */
    (void)es8311_write_register(0x32, 0x00);
}

static bool IRAM_ATTR
audio_i2s_send_queue_overflow(
    i2s_chan_handle_t channel,
    i2s_event_data_t *event,
    void *user)
{
    (void)channel;
    (void)event;
    (void)user;

    portENTER_CRITICAL_ISR(&audio_state_lock);
    pal_audio_telemetry.driver_send_queue_overflows++;
    portEXIT_CRITICAL_ISR(&audio_state_lock);
    return false;
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
    UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);

    portENTER_CRITICAL(&audio_state_lock);
    if (pal_audio_telemetry.minimum_internal_free == 0 ||
        internal_low_water <
            pal_audio_telemetry.minimum_internal_free) {
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
audio_write_tick(void)
{
    const uint8_t *source = pal_sram_audio_tick_bytes;
    const size_t total_bytes = sizeof(pal_sram_audio_tick_bytes);
    size_t total_written = 0;

    while (total_written < total_bytes) {
        size_t written = 0;
        esp_err_t error = i2s_channel_write(
            i2s_tx_channel,
            source + total_written,
            total_bytes - total_written,
            &written,
            AUDIO_WRITE_TIMEOUT_MS);

        if (error != ESP_OK || written == 0) {
            uint32_t write_error_count;

            portENTER_CRITICAL(&audio_state_lock);
            write_error_count = ++pal_audio_telemetry.write_errors;
            portEXIT_CRITICAL(&audio_state_lock);
            if (write_error_count == 1 ||
                write_error_count %
                        AUDIO_RUNTIME_REPORT_TICKS ==
                    0) {
                if (error != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "I2S write: %s (errors=%u)",
                        esp_err_to_name(error),
                        write_error_count);
                } else {
                    ESP_LOGE(
                        TAG,
                        "I2S write made no progress (errors=%u)",
                        write_error_count);
                }
            }
            return false;
        }
        total_written += written;
    }
    return true;
}

static void
audio_task(
    void *argument)
{
    (void)argument;

    for (;;) {
        bool stopping = false;
        uint32_t report_countdown =
            AUDIO_RUNTIME_REPORT_TICKS;

        while (!stopping) {
            CardputerExtremeAudioCommand command;
            int64_t render_start;
            uint32_t render_us;
            int64_t write_start;
            uint32_t write_us;
            bool write_ok;

            while (xQueueReceive(
                       audio_command_queue,
                       &command,
                       0) == pdTRUE) {
                if (command.type ==
                    CARDPUTER_EXTREME_AUDIO_COMMAND_STOP) {
                    stopping = true;
                } else {
                    audio_paused =
                        command.type ==
                        CARDPUTER_EXTREME_AUDIO_COMMAND_PAUSE;
                    portENTER_CRITICAL(&audio_state_lock);
                    pal_audio_telemetry.paused = audio_paused;
                    portEXIT_CRITICAL(&audio_state_lock);
                }
            }
            if (stopping) {
                break;
            }

            memset(
                pal_sram_audio_tick_bytes,
                0,
                sizeof(pal_sram_audio_tick_bytes));
            render_start = esp_timer_get_time();
            if (!audio_paused && audio_render != NULL) {
                audio_render(
                    audio_render_user,
                    PAL_AUDIO_TICK_BUFFER,
                    CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES);
            }
            render_us =
                (uint32_t)(esp_timer_get_time() - render_start);

            portENTER_CRITICAL(&audio_state_lock);
            pal_audio_telemetry.rendered_ticks++;
            pal_audio_telemetry.rendered_samples +=
                CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES;
            if (render_us > pal_audio_telemetry.max_render_us) {
                pal_audio_telemetry.max_render_us = render_us;
            }
            if (render_us > AUDIO_TICK_DEADLINE_US) {
                pal_audio_telemetry.render_deadline_misses++;
            }
            portEXIT_CRITICAL(&audio_state_lock);

            write_start = esp_timer_get_time();
            write_ok = audio_write_tick();
            write_us =
                (uint32_t)(esp_timer_get_time() - write_start);
            portENTER_CRITICAL(&audio_state_lock);
            if (write_us > pal_audio_telemetry.max_write_us) {
                pal_audio_telemetry.max_write_us = write_us;
            }
            portEXIT_CRITICAL(&audio_state_lock);

            if (!write_ok) {
                /*
                 * A disabled or failed channel can reject writes without
                 * blocking.  Retain command responsiveness without letting
                 * this high-priority task spin and starve the engine.
                 */
                vTaskDelay(
                    pdMS_TO_TICKS(AUDIO_WRITE_RETRY_DELAY_MS));
            }
            if (--report_countdown == 0) {
                audio_sample_runtime_metrics();
                CardputerExtremeAudio_LogTelemetry("runtime");
                report_countdown =
                    AUDIO_RUNTIME_REPORT_TICKS;
            }
        }

        audio_sample_runtime_metrics();
        portENTER_CRITICAL(&audio_state_lock);
        audio_task_running = false;
        portEXIT_CRITICAL(&audio_state_lock);
        xSemaphoreGive(audio_stopped_semaphore);

        /*
         * Keep the statically allocated TCB and stack alive, but block without
         * touching I2S again.  A direct notification is retained even if
         * Begin() sends it before this task reaches ulTaskNotifyTake(), which
         * makes immediate Stop()/Begin() reuse race-free without deleting and
         * recreating the static task.
         */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static bool
audio_init_i2s(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_std_config_t standard_config = {
        .clk_cfg =
            I2S_STD_CLK_DEFAULT_CONFIG(
                CARDPUTER_EXTREME_AUDIO_SAMPLE_RATE),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    i2s_event_callbacks_t callbacks = {
        .on_send_q_ovf = audio_i2s_send_queue_overflow,
    };
    i2s_chan_info_t channel_info = {0};
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    uint32_t driver_dma_bytes = 0;
    uint32_t internal_free_after_driver;
    uint32_t dma_free_after_driver;
    uint32_t minimum_internal_free;
    uint32_t minimum_dma_free;

    if (i2s_tx_channel != NULL) {
        ESP_LOGW(TAG, "retrying stale I2S1 TX channel cleanup");
        if (!audio_destroy_i2s()) {
            return false;
        }
    }

    channel_config.dma_desc_num = AUDIO_DMA_DESCRIPTOR_COUNT;
    channel_config.dma_frame_num =
        CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES;
    channel_config.auto_clear = true;

    standard_config.slot_cfg.slot_bit_width =
        I2S_SLOT_BIT_WIDTH_16BIT;
    standard_config.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    if (!audio_log_error(
            i2s_new_channel(
                &channel_config,
                &i2s_tx_channel,
                NULL),
            "create I2S1 TX channel")) {
        i2s_tx_channel = NULL;
        return false;
    }
    if (!audio_log_error(
            i2s_channel_register_event_callback(
                i2s_tx_channel,
                &callbacks,
                NULL),
            "register I2S callbacks") ||
        !audio_log_error(
            i2s_channel_init_std_mode(
                i2s_tx_channel,
                &standard_config),
            "initialize I2S1 standard mode") ||
        !audio_log_error(
            i2s_channel_enable(i2s_tx_channel),
            "enable I2S1 TX channel")) {
        (void)audio_destroy_i2s();
        return false;
    }

    if (i2s_channel_get_info(i2s_tx_channel, &channel_info) == ESP_OK) {
        driver_dma_bytes = channel_info.total_dma_buf_size;
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
    pal_audio_telemetry.driver_dma_bytes = driver_dma_bytes;
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
CardputerExtremeAudio_Begin(
    CardputerExtremeAudioRenderCallback render,
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
        if (!task_running) {
            ESP_LOGE(TAG, "audio shutdown is incomplete");
            return false;
        }
        return audio_render == render && audio_render_user == user;
    }

    internal_free_before_driver =
        (uint32_t)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    dma_free_before_driver =
        (uint32_t)heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    portENTER_CRITICAL(&audio_state_lock);
    memset(&pal_audio_telemetry, 0, sizeof(pal_audio_telemetry));
    pal_audio_telemetry.internal_free_before_driver =
        internal_free_before_driver;
    pal_audio_telemetry.dma_free_before_driver =
        dma_free_before_driver;
    portEXIT_CRITICAL(&audio_state_lock);
    if (!es8311_add_to_shared_bus()) {
        return false;
    }
    if (!es8311_enable_speaker()) {
        es8311_mute_speaker();
        return false;
    }

    if (!audio_sync_initialized) {
        audio_command_queue = xQueueCreateStatic(
            AUDIO_COMMAND_QUEUE_LENGTH,
            sizeof(CardputerExtremeAudioCommand),
            pal_audio_command_queue_storage,
            &pal_audio_command_queue_object);
        audio_stopped_semaphore = xSemaphoreCreateBinaryStatic(
            &pal_audio_stopped_semaphore_object);
        if (audio_command_queue == NULL ||
            audio_stopped_semaphore == NULL) {
            ESP_LOGE(
                TAG,
                "create static audio synchronization objects");
            es8311_mute_speaker();
            return false;
        }
        audio_sync_initialized = true;
    } else {
        (void)xQueueReset(audio_command_queue);
        (void)xSemaphoreTake(audio_stopped_semaphore, 0);
    }

    audio_render = render;
    audio_render_user = user;
    audio_paused = false;
    if (!audio_init_i2s()) {
        es8311_mute_speaker();
        audio_render = NULL;
        audio_render_user = NULL;
        return false;
    }

    portENTER_CRITICAL(&audio_state_lock);
    audio_started = true;
    audio_task_running = true;
    pal_audio_telemetry.started = true;
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
            (StackType_t *)(void *)
                pal_sram_audio_task_stack_bytes,
            &pal_audio_task_object);
        if (task == NULL) {
            ESP_LOGE(TAG, "create static audio task");
            portENTER_CRITICAL(&audio_state_lock);
            audio_started = false;
            audio_task_running = false;
            pal_audio_telemetry.started = false;
            portEXIT_CRITICAL(&audio_state_lock);
            es8311_mute_speaker();
            (void)audio_destroy_i2s();
            audio_render = NULL;
            audio_render_user = NULL;
            return false;
        }
        audio_task_handle = task;
    }
    ESP_LOGI(
        TAG,
        "ES8311 music ready: %u Hz mono PCM16, %u samples/tick, "
        "I2S1 BCLK=%d WS=%d DOUT=%d, DMA=%u bytes",
        CARDPUTER_EXTREME_AUDIO_SAMPLE_RATE,
        CARDPUTER_EXTREME_AUDIO_TICK_SAMPLES,
        PIN_I2S_BCLK,
        PIN_I2S_WS,
        PIN_I2S_DOUT,
        pal_audio_telemetry.driver_dma_bytes);
    return true;
}

bool
CardputerExtremeAudio_SetPaused(
    bool paused)
{
    CardputerExtremeAudioCommand command = {
        .type = paused
                    ? CARDPUTER_EXTREME_AUDIO_COMMAND_PAUSE
                    : CARDPUTER_EXTREME_AUDIO_COMMAND_RESUME,
    };
    bool started;
    bool task_running;

    portENTER_CRITICAL(&audio_state_lock);
    started = audio_started;
    task_running = audio_task_running;
    portEXIT_CRITICAL(&audio_state_lock);
    if (!started || !task_running ||
        audio_command_queue == NULL) {
        return false;
    }
    if (xQueueSend(audio_command_queue, &command, 0) != pdTRUE) {
        portENTER_CRITICAL(&audio_state_lock);
        pal_audio_telemetry.command_queue_overflows++;
        portEXIT_CRITICAL(&audio_state_lock);
        return false;
    }
    return true;
}

bool
CardputerExtremeAudio_Stop(void)
{
    CardputerExtremeAudioCommand command = {
        .type = CARDPUTER_EXTREME_AUDIO_COMMAND_STOP,
    };
    bool started;
    bool task_running;

    portENTER_CRITICAL(&audio_state_lock);
    started = audio_started;
    task_running = audio_task_running;
    portEXIT_CRITICAL(&audio_state_lock);
    if (!started) {
        return true;
    }
    if (audio_task_handle == NULL || audio_command_queue == NULL ||
        audio_stopped_semaphore == NULL) {
        ESP_LOGE(TAG, "audio task is not available for shutdown");
        return false;
    }
    if (task_running) {
        if (xQueueSend(
                audio_command_queue,
                &command,
                pdMS_TO_TICKS(AUDIO_STOP_TIMEOUT_MS)) != pdTRUE) {
            portENTER_CRITICAL(&audio_state_lock);
            pal_audio_telemetry.command_queue_overflows++;
            portEXIT_CRITICAL(&audio_state_lock);
            ESP_LOGE(TAG, "audio stop command timed out");
            return false;
        }
        if (xSemaphoreTake(
                audio_stopped_semaphore,
                pdMS_TO_TICKS(AUDIO_STOP_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "audio task did not stop");
            return false;
        }
    }

    /*
     * After giving the semaphore, the music task only waits for Begin()'s
     * direct notification.  It cannot touch this channel again, even when a
     * higher-priority Stop() caller preempts it before the wait call.
     */
    es8311_mute_speaker();
    if (!audio_destroy_i2s()) {
        ESP_LOGE(TAG, "audio shutdown can be retried");
        return false;
    }

    portENTER_CRITICAL(&audio_state_lock);
    audio_started = false;
    pal_audio_telemetry.started = false;
    portEXIT_CRITICAL(&audio_state_lock);
    audio_render = NULL;
    audio_render_user = NULL;
    return true;
}

bool
CardputerExtremeAudio_Started(void)
{
    bool started;

    portENTER_CRITICAL(&audio_state_lock);
    started = audio_started;
    portEXIT_CRITICAL(&audio_state_lock);
    return started;
}

void
CardputerExtremeAudio_GetTelemetry(
    CardputerExtremeAudioTelemetry *telemetry)
{
    if (telemetry == NULL) {
        return;
    }

    portENTER_CRITICAL(&audio_state_lock);
    *telemetry = pal_audio_telemetry;
    portEXIT_CRITICAL(&audio_state_lock);
}

void
CardputerExtremeAudio_LogTelemetry(
    const char *stage)
{
    CardputerExtremeAudioTelemetry telemetry;

    CardputerExtremeAudio_GetTelemetry(&telemetry);
    ESP_LOGI(
        TAG,
        "stage=%s started=%u paused=%u ticks=%u samples=%u "
        "render_max_us=%u deadline_miss=%u write_max_us=%u "
        "write_err=%u cmd_q_ovf=%u send_q_ovf=%u stack_free_min=%u "
        "driver_dma=%u internal=%u->%u low=%u dma=%u->%u low=%u",
        stage != NULL ? stage : "?",
        telemetry.started,
        telemetry.paused,
        telemetry.rendered_ticks,
        telemetry.rendered_samples,
        telemetry.max_render_us,
        telemetry.render_deadline_misses,
        telemetry.max_write_us,
        telemetry.write_errors,
        telemetry.command_queue_overflows,
        telemetry.driver_send_queue_overflows,
        telemetry.task_stack_high_water_bytes,
        telemetry.driver_dma_bytes,
        telemetry.internal_free_before_driver,
        telemetry.internal_free_after_driver,
        telemetry.minimum_internal_free,
        telemetry.dma_free_before_driver,
        telemetry.dma_free_after_driver,
        telemetry.minimum_dma_free);
}
