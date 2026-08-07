#include "xiaomiao_board.h"

#include "cardputer_extreme_native_view.h"
#include "pal_guru_screen.h"
#include "xiaomiao_memory.h"

#include <stddef.h>

#include <driver/gpio.h>
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_io.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <ff.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

static const char *TAG = "xiaomiao";
static const char *TF_TAG = "xiaomiao_tf";

static const gpio_num_t PIN_LCD_SCLK = GPIO_NUM_18;
static const gpio_num_t PIN_LCD_MOSI = GPIO_NUM_23;
static const gpio_num_t PIN_LCD_CS = GPIO_NUM_5;
static const gpio_num_t PIN_LCD_DC = GPIO_NUM_4;
static const gpio_num_t PIN_LCD_RESET_TF_MISO = GPIO_NUM_19;
static const gpio_num_t PIN_TF_CS = GPIO_NUM_22;

static const gpio_num_t key_pins[] = {
    GPIO_NUM_2, GPIO_NUM_13, GPIO_NUM_27,
    GPIO_NUM_35, GPIO_NUM_34, GPIO_NUM_12,
};
static const uint8_t key_ascii[] = {'i', 'k', 'j', 'l', '\r', '\b'};

static const spi_host_device_t SHARED_HOST = SPI3_HOST;
static const uint32_t LCD_PIXEL_CLOCK_HZ = 20000000u;
static const uint32_t TF_SPI_CLOCK_KHZ = 20000u;

enum {
    LCD_CMD_FRMCTR1 = 0xB1,
    LCD_CMD_FRMCTR2 = 0xB2,
    LCD_CMD_FRMCTR3 = 0xB3,
    LCD_CMD_INVCTR = 0xB4,
    LCD_CMD_PWCTR1 = 0xC0,
    LCD_CMD_PWCTR2 = 0xC1,
    LCD_CMD_PWCTR3 = 0xC2,
    LCD_CMD_PWCTR4 = 0xC3,
    LCD_CMD_PWCTR5 = 0xC4,
    LCD_CMD_VMCTR1 = 0xC5,
};

typedef struct XiaomiaoKeyEvent {
    uint8_t ascii;
    bool pressed;
} XiaomiaoKeyEvent;

#define KEY_COUNT ((uint8_t)(sizeof(key_pins) / sizeof(key_pins[0])))
#define KEY_EVENT_CAPACITY 12u

static esp_lcd_panel_io_handle_t lcd_io;
static sdmmc_card_t *tf_card;
static bool tf_mounted;
static uint32_t tf_transaction_sequence;
static uint32_t tf_data_transaction_count;
static uint64_t tf_data_transaction_bytes;
static uint32_t tf_transaction_failure_count;
static uint32_t tf_max_transaction_us;
static bool key_down[KEY_COUNT];
static XiaomiaoKeyEvent key_events[KEY_EVENT_CAPACITY];
static uint8_t key_event_read;
static uint8_t key_event_count;
static bool key_event_yield_pending;

static bool
log_error(
    esp_err_t err,
    const char *what)
{
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    return false;
}

/*
 * Keep the normal driver quiet so UART traffic does not perturb the shared
 * bus timing.  On failure, retain the command-level information which FatFS
 * otherwise collapses into FR_DISK_ERR at the caller.
 */
static esp_err_t
xiaomiao_tf_do_transaction(
    int slot,
    sdmmc_command_t *command)
{
    int64_t start_us;
    int64_t elapsed_us_64;
    uint32_t elapsed_us;
    uint32_t sequence;
    esp_err_t result;

    if (command == NULL) {
        ESP_LOGE(TF_TAG, "null SDSPI command: slot=%d", slot);
        return ESP_ERR_INVALID_ARG;
    }

    sequence = ++tf_transaction_sequence;
    start_us = esp_timer_get_time();
    result = sdspi_host_do_transaction(slot, command);
    elapsed_us_64 = esp_timer_get_time() - start_us;
    elapsed_us = elapsed_us_64 <= 0 ? 0u :
        elapsed_us_64 > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us_64;
    if (elapsed_us > tf_max_transaction_us) {
        tf_max_transaction_us = elapsed_us;
    }
    if (command->datalen != 0u) {
        tf_data_transaction_count++;
        tf_data_transaction_bytes += command->datalen;
    }

    if (result != ESP_OK) {
        tf_transaction_failure_count++;
        ESP_LOGE(TF_TAG,
            "%s transaction failed: seq=%lu slot=%d CMD%lu arg=0x%08lx "
            "flags=0x%x data=%p datalen=%u buflen=%u blklen=%u "
            "result=%s (0x%x) response0=0x%08lx elapsed_us=%lu "
            "data_cmds=%lu data_bytes=%llu failures=%lu max_us=%lu",
            tf_mounted ? "runtime" : "mount",
            (unsigned long)sequence,
            slot,
            (unsigned long)command->opcode,
            (unsigned long)command->arg,
            command->flags,
            command->data,
            (unsigned)command->datalen,
            (unsigned)command->buflen,
            (unsigned)command->blklen,
            esp_err_to_name(result),
            (unsigned)result,
            (unsigned long)command->response[0],
            (unsigned long)elapsed_us,
            (unsigned long)tf_data_transaction_count,
            (unsigned long long)tf_data_transaction_bytes,
            (unsigned long)tf_transaction_failure_count,
            (unsigned long)tf_max_transaction_us);
    }
    return result;
}

static uint16_t
wire_rgb565(
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    uint16_t color = (uint16_t)(((uint16_t)(r & 0xf8u) << 8) |
                                ((uint16_t)(g & 0xfcu) << 3) |
                                ((uint16_t)b >> 3));
    return (uint16_t)((color << 8) | (color >> 8));
}

static bool
lcd_tx(
    int command,
    const void *data,
    size_t size,
    const char *what)
{
    return lcd_io != NULL &&
        log_error(esp_lcd_panel_io_tx_param(lcd_io, command, data, size), what);
}

static bool
lcd_set_window(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height)
{
    uint16_t x1;
    uint16_t y1;
    uint8_t columns[4];
    uint8_t rows[4];

    if (width == 0u || height == 0u ||
        x >= XIAOMIAO_LCD_WIDTH || y >= XIAOMIAO_LCD_HEIGHT ||
        width > XIAOMIAO_LCD_WIDTH - x ||
        height > XIAOMIAO_LCD_HEIGHT - y) {
        return false;
    }
    x1 = (uint16_t)(x + width - 1u);
    y1 = (uint16_t)(y + height - 1u);
    columns[0] = (uint8_t)(x >> 8);
    columns[1] = (uint8_t)x;
    columns[2] = (uint8_t)(x1 >> 8);
    columns[3] = (uint8_t)x1;
    rows[0] = (uint8_t)(y >> 8);
    rows[1] = (uint8_t)y;
    rows[2] = (uint8_t)(y1 >> 8);
    rows[3] = (uint8_t)y1;
    return lcd_tx(LCD_CMD_CASET, columns, sizeof(columns), "LCD CASET") &&
        lcd_tx(LCD_CMD_RASET, rows, sizeof(rows), "LCD RASET");
}

static bool
lcd_send_region(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t rows)
{
    if (!lcd_set_window(x, y, width, rows) ||
        !log_error(esp_lcd_panel_io_tx_color(
            lcd_io, LCD_CMD_RAMWR, pal_sram_display_dma,
            (uint32_t)width * rows * 2u), "LCD strip")) {
        return false;
    }
    return lcd_tx(-1, NULL, 0u, "wait LCD idle");
}

static bool
lcd_send_strip(
    uint16_t y,
    uint16_t rows)
{
    return lcd_send_region(0u, y, XIAOMIAO_LCD_WIDTH, rows);
}

static bool
lcd_fill(
    uint16_t color)
{
    const uint16_t max_rows = (uint16_t)(
        PAL_EXTREME_DISPLAY_DMA_BYTES / (XIAOMIAO_LCD_WIDTH * 2u));
    uint16_t y;

    for (y = 0u; y < XIAOMIAO_LCD_HEIGHT;) {
        uint16_t rows = (uint16_t)(XIAOMIAO_LCD_HEIGHT - y);
        uint32_t count;
        uint32_t i;
        uint16_t *pixels = (uint16_t *)(void *)pal_sram_display_dma;

        if (rows > max_rows) {
            rows = max_rows;
        }
        count = (uint32_t)XIAOMIAO_LCD_WIDTH * rows;
        for (i = 0u; i < count; i++) {
            pixels[i] = color;
        }
        if (!lcd_send_strip(y, rows)) {
            return false;
        }
        y = (uint16_t)(y + rows);
    }
    return true;
}

static bool
pulse_lcd_reset(void)
{
    gpio_config_t cfg = {0};

    cfg.pin_bit_mask = 1ULL << PIN_LCD_RESET_TF_MISO;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    if (!log_error(gpio_config(&cfg), "configure LCD reset")) {
        return false;
    }
    gpio_set_level(PIN_LCD_CS, 0);
    gpio_set_level(PIN_LCD_RESET_TF_MISO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_LCD_RESET_TF_MISO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_LCD_RESET_TF_MISO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(PIN_LCD_CS, 1);
    return true;
}

static bool
init_lcd(void)
{
    static const uint8_t frmctr[] = {0x01, 0x2c, 0x2d};
    static const uint8_t frmctr3[] = {0x01, 0x2c, 0x2d, 0x01, 0x2c, 0x2d};
    static const uint8_t invctr[] = {0x07};
    static const uint8_t pwctr1[] = {0xa2, 0x02, 0x84};
    static const uint8_t pwctr2[] = {0xc5};
    static const uint8_t pwctr3[] = {0x0a, 0x00};
    static const uint8_t pwctr4[] = {0x8a, 0x2a};
    static const uint8_t pwctr5[] = {0x8a, 0xee};
    static const uint8_t vmctr1[] = {0x0e};
    static const uint8_t gamma_positive[] = {
        0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
        0x29, 0x25, 0x2b, 0x39, 0x00, 0x01, 0x03, 0x10,
    };
    static const uint8_t gamma_negative[] = {
        0x03, 0x1d, 0x07, 0x06, 0x2e, 0x2c, 0x29, 0x2d,
        0x2e, 0x2e, 0x37, 0x3f, 0x00, 0x00, 0x02, 0x10,
    };
    gpio_config_t cs_cfg = {0};
    spi_bus_config_t bus_cfg = {0};
    esp_lcd_panel_io_spi_config_t io_cfg = {0};
    uint8_t value;

    cs_cfg.pin_bit_mask = (1ULL << PIN_LCD_CS) | (1ULL << PIN_TF_CS);
    cs_cfg.mode = GPIO_MODE_OUTPUT;
    cs_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cs_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cs_cfg.intr_type = GPIO_INTR_DISABLE;
    if (!log_error(gpio_config(&cs_cfg), "configure SPI chip selects")) {
        return false;
    }
    gpio_set_level(PIN_LCD_CS, 1);
    gpio_set_level(PIN_TF_CS, 1);
    if (!pulse_lcd_reset()) {
        return false;
    }

    bus_cfg.sclk_io_num = PIN_LCD_SCLK;
    bus_cfg.mosi_io_num = PIN_LCD_MOSI;
    bus_cfg.miso_io_num = PIN_LCD_RESET_TF_MISO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = PAL_EXTREME_DISPLAY_DMA_BYTES;
    if (!log_error(spi_bus_initialize(
            SHARED_HOST, &bus_cfg, SPI_DMA_CH_AUTO), "initialize shared SPI")) {
        return false;
    }

    io_cfg.cs_gpio_num = PIN_LCD_CS;
    io_cfg.dc_gpio_num = PIN_LCD_DC;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    io_cfg.trans_queue_depth = 1;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.flags.sio_mode = 0;
    if (!log_error(esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)SHARED_HOST, &io_cfg, &lcd_io),
            "create ST7735 IO")) {
        return false;
    }

    if (!lcd_tx(LCD_CMD_SWRESET, NULL, 0u, "LCD SWRESET")) return false;
    vTaskDelay(pdMS_TO_TICKS(150));
    if (!lcd_tx(LCD_CMD_SLPOUT, NULL, 0u, "LCD SLPOUT")) return false;
    vTaskDelay(pdMS_TO_TICKS(1));
    if (!lcd_tx(LCD_CMD_FRMCTR1, frmctr, sizeof(frmctr), "LCD FRMCTR1")) return false;
    if (!lcd_tx(LCD_CMD_FRMCTR2, frmctr, sizeof(frmctr), "LCD FRMCTR2")) return false;
    if (!lcd_tx(LCD_CMD_FRMCTR3, frmctr3, sizeof(frmctr3), "LCD FRMCTR3")) return false;
    if (!lcd_tx(LCD_CMD_INVCTR, invctr, sizeof(invctr), "LCD INVCTR")) return false;
    if (!lcd_tx(LCD_CMD_PWCTR1, pwctr1, sizeof(pwctr1), "LCD PWCTR1")) return false;
    if (!lcd_tx(LCD_CMD_PWCTR2, pwctr2, sizeof(pwctr2), "LCD PWCTR2")) return false;
    if (!lcd_tx(LCD_CMD_PWCTR3, pwctr3, sizeof(pwctr3), "LCD PWCTR3")) return false;
    if (!lcd_tx(LCD_CMD_PWCTR4, pwctr4, sizeof(pwctr4), "LCD PWCTR4")) return false;
    if (!lcd_tx(LCD_CMD_PWCTR5, pwctr5, sizeof(pwctr5), "LCD PWCTR5")) return false;
    if (!lcd_tx(LCD_CMD_VMCTR1, vmctr1, sizeof(vmctr1), "LCD VMCTR1")) return false;
    value = 0x05;
    if (!lcd_tx(LCD_CMD_COLMOD, &value, 1u, "LCD COLMOD")) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    /*
     * The framebuffer is sent as wire-order RGB565 (MSB first).  Xiaomiao's
     * ST7735 reference configuration uses RGB channel order in landscape;
     * setting MADCTL_BGR here would swap red and blue a second time.
     */
    value = 0x60;
    if (!lcd_tx(LCD_CMD_MADCTL, &value, 1u, "LCD MADCTL")) return false;
    if (!lcd_tx(LCD_CMD_INVOFF, NULL, 0u, "LCD INVOFF")) return false;
    if (!lcd_tx(0xe0, gamma_positive, sizeof(gamma_positive), "LCD GMCTRP1")) return false;
    if (!lcd_tx(0xe1, gamma_negative, sizeof(gamma_negative), "LCD GMCTRN1")) return false;
    if (!lcd_tx(LCD_CMD_NORON, NULL, 0u, "LCD NORON")) return false;
    if (!lcd_tx(LCD_CMD_DISPON, NULL, 0u, "LCD DISPON")) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}

static bool
init_keys(void)
{
    gpio_config_t normal = {0};
    gpio_config_t input_only = {0};
    uint8_t i;

    normal.pin_bit_mask = (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_12) |
        (1ULL << GPIO_NUM_13) | (1ULL << GPIO_NUM_27);
    normal.mode = GPIO_MODE_INPUT;
    normal.pull_up_en = GPIO_PULLUP_ENABLE;
    normal.pull_down_en = GPIO_PULLDOWN_DISABLE;
    normal.intr_type = GPIO_INTR_DISABLE;
    input_only.pin_bit_mask = (1ULL << GPIO_NUM_34) | (1ULL << GPIO_NUM_35);
    input_only.mode = GPIO_MODE_INPUT;
    input_only.pull_up_en = GPIO_PULLUP_DISABLE;
    input_only.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input_only.intr_type = GPIO_INTR_DISABLE;
    if (!log_error(gpio_config(&normal), "configure keys") ||
        !log_error(gpio_config(&input_only), "configure input-only keys")) {
        return false;
    }
    for (i = 0u; i < KEY_COUNT; i++) {
        key_down[i] = gpio_get_level(key_pins[i]) == 0;
    }
    return true;
}

bool
Xiaomiao_Begin(
    void)
{
    if (!init_lcd() || !lcd_fill(wire_rgb565(0u, 0u, 0u)) || !init_keys()) {
        return false;
    }
    ESP_LOGI(TAG, "ready: ESP32-WROVER-B, ST7735 160x128, shared VSPI");
    return true;
}

bool
Xiaomiao_MountTf(
    void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16u * 1024u,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    esp_err_t err;

    if (tf_mounted) {
        return true;
    }
    Xiaomiao_PrepareTfAccess();
    host.slot = SHARED_HOST;
    host.max_freq_khz = TF_SPI_CLOCK_KHZ;
    host.do_transaction = xiaomiao_tf_do_transaction;
    host.unaligned_multi_block_rw_max_chunk_size = 8;
    slot.host_id = SHARED_HOST;
    slot.gpio_cs = PIN_TF_CS;
    err = esp_vfs_fat_sdspi_mount(
        XIAOMIAO_TF_MOUNT_POINT, &host, &slot, &mount_config, &tf_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount SD: %s", esp_err_to_name(err));
        return false;
    }
    tf_mounted = true;
    ESP_LOGI(TF_TAG,
        "mounted at %s: requested=%lu kHz actual=%d kHz "
        "transactions=%lu data_cmds=%lu data_bytes=%llu max_us=%lu",
        XIAOMIAO_TF_MOUNT_POINT,
        (unsigned long)TF_SPI_CLOCK_KHZ,
        tf_card != NULL ? tf_card->real_freq_khz : 0,
        (unsigned long)tf_transaction_sequence,
        (unsigned long)tf_data_transaction_count,
        (unsigned long long)tf_data_transaction_bytes,
        (unsigned long)tf_max_transaction_us);
    return true;
}

bool
Xiaomiao_TfMounted(
    void)
{
    return tf_mounted;
}

void
Xiaomiao_PrepareTfAccess(
    void)
{
    gpio_set_level(PIN_LCD_CS, 1);
    /* GPIO19 becomes SD MISO permanently after the one boot-time LCD reset. */
}

static void
scan_keys(
    void)
{
    uint8_t i;

    for (i = 0u; i < KEY_COUNT; i++) {
        bool down = gpio_get_level(key_pins[i]) == 0;

        if (down != key_down[i] && key_event_count < KEY_EVENT_CAPACITY) {
            uint8_t write = (uint8_t)((key_event_read + key_event_count) %
                KEY_EVENT_CAPACITY);
            key_events[write].ascii = key_ascii[i];
            key_events[write].pressed = down;
            key_event_count++;
            key_down[i] = down;
        }
    }
}

bool
Xiaomiao_PollKey(
    uint8_t *ascii,
    bool *pressed)
{
    XiaomiaoKeyEvent *event;

    if (ascii == NULL || pressed == NULL) {
        return false;
    }
    if (key_event_yield_pending) {
        key_event_yield_pending = false;
        return false;
    }
    if (key_event_count == 0u) {
        scan_keys();
    }
    if (key_event_count == 0u) {
        return false;
    }
    event = &key_events[key_event_read];
    *ascii = event->ascii;
    *pressed = event->pressed;
    key_event_read = (uint8_t)((key_event_read + 1u) % KEY_EVENT_CAPACITY);
    key_event_count--;
    key_event_yield_pending = true;
    return true;
}

static bool
flush_indexed_framebuffer_region(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height)
{
    uint16_t max_rows;
    size_t row_bytes;
    uint16_t row;

    if (lcd_io == NULL || pixels == NULL || palette_rgba == NULL ||
        pitch < XIAOMIAO_LCD_WIDTH || width == 0u || height == 0u ||
        x >= XIAOMIAO_LCD_WIDTH || y >= XIAOMIAO_LCD_HEIGHT ||
        width > XIAOMIAO_LCD_WIDTH - x ||
        height > XIAOMIAO_LCD_HEIGHT - y) {
        return false;
    }
    max_rows = (uint16_t)(PAL_EXTREME_DISPLAY_DMA_BYTES / (width * 2u));
    row_bytes = (size_t)width * 2u;
    if (max_rows == 0u) {
        return false;
    }
    for (row = y; row < (uint16_t)(y + height);) {
        uint16_t rows = (uint16_t)(y + height - row);

        if (rows > max_rows) {
            rows = max_rows;
        }
        if (!CardputerExtreme_CopyIndexedNativeRegion(
                pixels, pitch, palette_rgba,
                XIAOMIAO_LCD_WIDTH, XIAOMIAO_LCD_HEIGHT,
                x, row, width, rows,
                pal_sram_display_dma, row_bytes,
                PAL_EXTREME_DISPLAY_DMA_BYTES) ||
            !lcd_send_region(x, row, width, rows)) {
            return false;
        }
        row = (uint16_t)(row + rows);
    }
    return true;
}

bool
Xiaomiao_FlushIndexedFramebuffer(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba)
{
    return flush_indexed_framebuffer_region(
        pixels, pitch, palette_rgba, 0u, 0u,
        XIAOMIAO_LCD_WIDTH, XIAOMIAO_LCD_HEIGHT);
}

bool
Xiaomiao_FlushIndexedFramebufferRegion(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba,
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height)
{
    return flush_indexed_framebuffer_region(
        pixels, pitch, palette_rgba, x, y, width, height);
}

void
Xiaomiao_ShowError(
    const char *line1,
    const char *line2)
{
    ESP_LOGE(TAG, "%s%s%s", line1 != NULL ? line1 : "ERROR",
        line2 != NULL ? ": " : "", line2 != NULL ? line2 : "");
    if (lcd_io != NULL) {
        (void)lcd_fill(wire_rgb565(255u, 0u, 0u));
    }
}

void
Xiaomiao_GuruMeditation(
    const char *file,
    uint32_t line,
    const char *reason)
{
    const esp_app_desc_t *description = esp_app_get_description();
    const char *revision = description != NULL ? description->version : "UNKNOWN";
    const uint16_t max_rows = (uint16_t)(
        PAL_EXTREME_DISPLAY_DMA_BYTES / (XIAOMIAO_LCD_WIDTH * 2u));
    PalGuruScreenText text;
    uint16_t y;

    ESP_LOGE("pal_guru", "halted at %s:%lu git=%s reason=%s",
        file != NULL ? file : "?", (unsigned long)line, revision,
        reason != NULL ? reason : "FATAL ERROR");
    if (lcd_io == NULL || max_rows == 0u) {
        return;
    }
    PalGuruScreen_BuildText(&text, file, line, revision, reason);
    for (y = 0u; y < XIAOMIAO_LCD_HEIGHT;) {
        uint16_t rows = (uint16_t)(XIAOMIAO_LCD_HEIGHT - y);

        if (rows > max_rows) {
            rows = max_rows;
        }
        if (!PalGuruScreen_RenderRgb565Strip(
                &text,
                (uint16_t *)(void *)pal_sram_display_dma,
                PAL_EXTREME_DISPLAY_DMA_BYTES / sizeof(uint16_t),
                XIAOMIAO_LCD_WIDTH,
                XIAOMIAO_LCD_HEIGHT,
                y,
                rows,
                wire_rgb565(0u, 0u, 0u),
                wire_rgb565(255u, 24u, 24u),
                wire_rgb565(255u, 255u, 255u)) ||
            !lcd_send_strip(y, rows)) {
            ESP_LOGE("pal_guru", "LCD diagnostic render failed at row %u",
                (unsigned)y);
            return;
        }
        y = (uint16_t)(y + rows);
    }
}
