#include "cardputer_extreme_board.h"

#include "cardputer_extreme_board_internal.h"
#include "cardputer_extreme_memory.h"
#include "cardputer_extreme_scaler.h"

#include <stddef.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_io.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <ff.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

/*
 * This file intentionally has no audio, allocator, or full-screen RGB565
 * storage.  ESP-IDF drivers may allocate their own startup objects, while all
 * project-owned pixel and keyboard state remains fixed static storage.
 */

static const char *TAG = "cardputer_extreme";

static const gpio_num_t PIN_BUTTON_A = GPIO_NUM_0;
static const gpio_num_t PIN_I2C_SCL = GPIO_NUM_9;
static const gpio_num_t PIN_I2C_SDA = GPIO_NUM_8;
static const gpio_num_t PIN_LCD_MOSI = GPIO_NUM_35;
static const gpio_num_t PIN_LCD_SCLK = GPIO_NUM_36;
static const gpio_num_t PIN_LCD_DC = GPIO_NUM_34;
static const gpio_num_t PIN_LCD_CS = GPIO_NUM_37;
static const gpio_num_t PIN_LCD_RST = GPIO_NUM_33;
static const gpio_num_t PIN_LCD_BL = GPIO_NUM_38;
static const gpio_num_t PIN_TF_MISO = GPIO_NUM_39;
static const gpio_num_t PIN_TF_MOSI = GPIO_NUM_14;
static const gpio_num_t PIN_TF_SCLK = GPIO_NUM_40;
static const gpio_num_t PIN_TF_CS = GPIO_NUM_12;

static const spi_host_device_t LCD_HOST = SPI3_HOST;
static const spi_host_device_t TF_HOST = SPI2_HOST;
static const uint32_t LCD_PIXEL_CLOCK_HZ = 40000000u;
static const uint32_t TF_SPI_CLOCK_KHZ = 20000u;
static const uint32_t I2C_TIMEOUT_MS = 1000u;
static const uint32_t TCA8418_CLOCK_HZ = 400000u;
static const uint8_t TCA8418_ADDRESS = 0x34u;
static const uint32_t TF_MAX_TRANSFER_BYTES = 4096u;

enum {
    LCD_PANEL_WIDTH = 135,
    LCD_PANEL_HEIGHT = 240,
    LCD_MEMORY_WIDTH = 240,
    LCD_MEMORY_HEIGHT = 320,
    LCD_OFFSET_X = 52,
    LCD_OFFSET_Y = 40,
    LCD_ROTATION = 1,
};

enum {
    LCD_CMD_RAMCTRL = 0xB0,
    LCD_CMD_GCTRL = 0xB7,
    LCD_CMD_VCOMS = 0xBB,
    LCD_CMD_LCMCTRL = 0xC0,
    LCD_CMD_VDVVRHEN = 0xC2,
    LCD_CMD_VRHS = 0xC3,
    LCD_CMD_VDVSET = 0xC4,
    LCD_CMD_PWCTRL1 = 0xD0,
    LCD_CMD_PVGAMCTRL = 0xE0,
    LCD_CMD_NVGAMCTRL = 0xE1,
};

enum {
    TCA_REG_CFG = 0x01,
    TCA_REG_INT_STAT = 0x02,
    TCA_REG_KEY_LCK_EC = 0x03,
    TCA_REG_KEY_EVENT_A = 0x04,
    TCA_REG_GPIO_INT_EN1 = 0x1A,
    TCA_REG_GPIO_INT_EN2 = 0x1B,
    TCA_REG_GPIO_INT_EN3 = 0x1C,
    TCA_REG_KP_GPIO1 = 0x1D,
    TCA_REG_KP_GPIO2 = 0x1E,
    TCA_REG_KP_GPIO3 = 0x1F,
    TCA_REG_GPI_EM1 = 0x20,
    TCA_REG_GPI_EM2 = 0x21,
    TCA_REG_GPI_EM3 = 0x22,
    TCA_REG_GPIO_DIR1 = 0x23,
    TCA_REG_GPIO_DIR2 = 0x24,
    TCA_REG_GPIO_DIR3 = 0x25,
    TCA_REG_GPIO_INT_LVL1 = 0x26,
    TCA_REG_GPIO_INT_LVL2 = 0x27,
    TCA_REG_GPIO_INT_LVL3 = 0x28,
    TCA_REG_DEBOUNCE_DIS1 = 0x29,
    TCA_REG_DEBOUNCE_DIS2 = 0x2A,
    TCA_REG_DEBOUNCE_DIS3 = 0x2B,
    TCA_CFG_GPI_IEN = 0x02,
    TCA_CFG_KE_IEN = 0x01,
};

enum {
    KEYBOARD_ROWS = 4,
    KEYBOARD_COLUMNS = 14,
    KEYBOARD_MAX_DRAIN_EVENTS = 32,
    KEY_EVENT_QUEUE_CAPACITY = 32,
    PAL_LOGICAL_WIDTH = 320,
    PAL_LOGICAL_HEIGHT = 200,
};

typedef char cardputer_extreme_dma_line_fits[
    (PAL_EXTREME_DISPLAY_DMA_BYTES >= CARDPUTER_EXTREME_LCD_WIDTH * 2u) ? 1 : -1];
typedef char cardputer_extreme_view_fits[
    (CARDPUTER_EXTREME_PAL_VIEW_X + CARDPUTER_EXTREME_PAL_VIEW_WIDTH <=
     CARDPUTER_EXTREME_LCD_WIDTH) ? 1 : -1];
typedef char cardputer_extreme_scaler_view_matches[
    (CARDPUTER_EXTREME_PAL_VIEW_WIDTH ==
         CARDPUTER_EXTREME_SCALER_VIEW_WIDTH &&
     CARDPUTER_EXTREME_PAL_VIEW_HEIGHT ==
         CARDPUTER_EXTREME_SCALER_VIEW_HEIGHT) ? 1 : -1];
typedef char cardputer_extreme_logical_screen_matches[
    (PAL_EXTREME_SCREEN_BYTES == PAL_LOGICAL_WIDTH * PAL_LOGICAL_HEIGHT &&
     PAL_LOGICAL_WIDTH == CARDPUTER_EXTREME_SCALER_SOURCE_WIDTH &&
     PAL_LOGICAL_HEIGHT == CARDPUTER_EXTREME_SCALER_SOURCE_HEIGHT) ? 1 : -1];

typedef struct CardputerExtremeKeyEvent {
    uint8_t ascii;
    bool pressed;
} CardputerExtremeKeyEvent;

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t tca8418_dev;
static esp_lcd_panel_io_handle_t lcd_io;
static sdmmc_card_t *tf_card;

static uint16_t lcd_colstart;
static uint16_t lcd_rowstart;
static bool keyboard_ready;
static bool tf_bus_ready;
static bool tf_mounted;
static bool pressed_cells[KEYBOARD_ROWS][KEYBOARD_COLUMNS];
static uint8_t emitted_key[KEYBOARD_ROWS][KEYBOARD_COLUMNS];
static CardputerExtremeKeyEvent key_event_queue[KEY_EVENT_QUEUE_CAPACITY];
static uint8_t key_event_read;
static uint8_t key_event_write;
static uint8_t key_event_count;
static bool key_event_yield_pending;
static bool button_a_down;
static uint64_t physical_key_mask;
static uint32_t action_mask;
static uint32_t action_pressed_mask;
static uint32_t action_released_mask;

i2c_master_bus_handle_t
CardputerExtreme_I2cBus(void)
{
    return i2c_bus;
}

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

static uint16_t
swap_u16(
    uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint16_t
lcd_wire_rgb565(
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    uint16_t color = (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                                ((uint16_t)(g & 0xFCu) << 3) |
                                ((uint16_t)b >> 3));
    return swap_u16(color);
}

static uint8_t
lcd_madctl_for_rotation(
    uint8_t rotation)
{
    static const uint8_t table[] = {
        0,
        LCD_CMD_MV_BIT | LCD_CMD_MX_BIT | LCD_CMD_MH_BIT,
        LCD_CMD_MX_BIT | LCD_CMD_MH_BIT | LCD_CMD_MY_BIT | LCD_CMD_ML_BIT,
        LCD_CMD_MV_BIT | LCD_CMD_MY_BIT | LCD_CMD_ML_BIT,
        LCD_CMD_MY_BIT | LCD_CMD_ML_BIT,
        LCD_CMD_MV_BIT,
        LCD_CMD_MX_BIT | LCD_CMD_MH_BIT,
        LCD_CMD_MV_BIT | LCD_CMD_MX_BIT | LCD_CMD_MY_BIT | LCD_CMD_MH_BIT | LCD_CMD_ML_BIT,
    };

    return (uint8_t)(table[rotation & 7u] | LCD_CMD_BGR_BIT);
}

static bool
lcd_tx_param(
    int command,
    const void *data,
    size_t size,
    const char *what)
{
    return lcd_io != NULL &&
           log_error(esp_lcd_panel_io_tx_param(lcd_io, command, data, size), what);
}

static bool
lcd_wait_idle(void)
{
    return lcd_tx_param(-1, NULL, 0, "wait LCD idle");
}

static bool
lcd_set_window(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height)
{
    uint16_t x0;
    uint16_t x1;
    uint16_t y0;
    uint16_t y1;
    uint8_t columns[4];
    uint8_t rows[4];

    if (width == 0 || height == 0 ||
        x >= CARDPUTER_EXTREME_LCD_WIDTH ||
        y >= CARDPUTER_EXTREME_LCD_HEIGHT ||
        width > CARDPUTER_EXTREME_LCD_WIDTH - x ||
        height > CARDPUTER_EXTREME_LCD_HEIGHT - y) {
        return false;
    }

    x0 = (uint16_t)(x + lcd_colstart);
    x1 = (uint16_t)(x0 + width - 1u);
    y0 = (uint16_t)(y + lcd_rowstart);
    y1 = (uint16_t)(y0 + height - 1u);
    columns[0] = (uint8_t)(x0 >> 8);
    columns[1] = (uint8_t)x0;
    columns[2] = (uint8_t)(x1 >> 8);
    columns[3] = (uint8_t)x1;
    rows[0] = (uint8_t)(y0 >> 8);
    rows[1] = (uint8_t)y0;
    rows[2] = (uint8_t)(y1 >> 8);
    rows[3] = (uint8_t)y1;

    return lcd_tx_param(LCD_CMD_CASET, columns, sizeof(columns), "LCD CASET") &&
           lcd_tx_param(LCD_CMD_RASET, rows, sizeof(rows), "LCD RASET");
}

static bool
lcd_send_region(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t rows)
{
    uint32_t bytes = (uint32_t)width * rows * 2u;

    if (!lcd_set_window(x, y, width, rows)) {
        return false;
    }
    if (!log_error(
            esp_lcd_panel_io_tx_color(
                lcd_io,
                LCD_CMD_RAMWR,
                pal_sram_display_dma,
                bytes),
            "send LCD strip")) {
        return false;
    }

    /*
     * There is one project-owned DMA buffer.  Wait before the caller writes
     * the next strip so an asynchronous SPI transfer cannot observe it being
     * overwritten.
     */
    return lcd_wait_idle();
}

static bool
lcd_send_strip(
    uint16_t y,
    uint16_t rows)
{
    return lcd_send_region(0, y, CARDPUTER_EXTREME_LCD_WIDTH, rows);
}

static bool
lcd_fill(
    uint16_t wire_color)
{
    const uint16_t max_rows =
        (uint16_t)(PAL_EXTREME_DISPLAY_DMA_BYTES /
                   (CARDPUTER_EXTREME_LCD_WIDTH * 2u));
    uint16_t y;

    if (lcd_io == NULL || max_rows == 0) {
        return false;
    }

    for (y = 0; y < CARDPUTER_EXTREME_LCD_HEIGHT;) {
        uint16_t rows = (uint16_t)(CARDPUTER_EXTREME_LCD_HEIGHT - y);
        uint32_t pixels;
        uint32_t i;
        uint16_t *dst = (uint16_t *)pal_sram_display_dma;

        if (rows > max_rows) {
            rows = max_rows;
        }
        pixels = (uint32_t)CARDPUTER_EXTREME_LCD_WIDTH * rows;
        for (i = 0; i < pixels; i++) {
            dst[i] = wire_color;
        }
        if (!lcd_send_strip(y, rows)) {
            return false;
        }
        y = (uint16_t)(y + rows);
    }

    return true;
}

static bool
init_lcd(void)
{
    static const uint8_t gamma_positive[] = {
        0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
        0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17,
    };
    static const uint8_t gamma_negative[] = {
        0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
        0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E,
    };
    static const uint8_t ramctrl[] = {0x00, 0xC0};
    static const uint8_t gctrl[] = {0x35};
    static const uint8_t vcoms[] = {0x28};
    static const uint8_t lcmctrl[] = {0x0C};
    static const uint8_t vdvvrhen[] = {0x01, 0xFF};
    static const uint8_t vrhs[] = {0x10};
    static const uint8_t vdvset[] = {0x20};
    static const uint8_t pwctrl1[] = {0xA4, 0xA1};
    gpio_config_t gpio_cfg = {0};
    spi_bus_config_t bus_cfg = {0};
    esp_lcd_panel_io_spi_config_t io_cfg = {0};
    uint8_t madctl;
    uint8_t colmod = 0x55;
    int ox = LCD_OFFSET_X;
    int oy = LCD_OFFSET_Y;
    int panel_width = LCD_PANEL_WIDTH;
    int panel_height = LCD_PANEL_HEIGHT;
    int memory_width = LCD_MEMORY_WIDTH;
    int memory_height = LCD_MEMORY_HEIGHT;

    gpio_cfg.pin_bit_mask = (1ULL << PIN_LCD_BL) | (1ULL << PIN_LCD_RST);
    gpio_cfg.mode = GPIO_MODE_OUTPUT;
    gpio_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_cfg.intr_type = GPIO_INTR_DISABLE;
    if (!log_error(gpio_config(&gpio_cfg), "configure LCD GPIO")) {
        return false;
    }
    if (!CardputerExtreme_SetBacklight(false)) {
        return false;
    }

    bus_cfg.sclk_io_num = PIN_LCD_SCLK;
    bus_cfg.mosi_io_num = PIN_LCD_MOSI;
    bus_cfg.miso_io_num = -1;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = PAL_EXTREME_DISPLAY_DMA_BYTES;
    if (!log_error(
            spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
            "initialize LCD SPI")) {
        return false;
    }

    io_cfg.cs_gpio_num = PIN_LCD_CS;
    io_cfg.dc_gpio_num = PIN_LCD_DC;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    io_cfg.trans_queue_depth = 1;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.flags.sio_mode = 1;
    if (!log_error(
            esp_lcd_new_panel_io_spi(
                (esp_lcd_spi_bus_handle_t)LCD_HOST,
                &io_cfg,
                &lcd_io),
            "create LCD panel IO")) {
        return false;
    }

    if ((LCD_ROTATION & 1) != 0) {
        int temporary;

        temporary = ox;
        ox = oy;
        oy = temporary;
        temporary = panel_width;
        panel_width = panel_height;
        panel_height = temporary;
        temporary = memory_width;
        memory_width = memory_height;
        memory_height = temporary;
    }
    lcd_colstart = (uint16_t)(((LCD_ROTATION & 2) != 0)
                                  ? memory_width - (panel_width + ox)
                                  : ox);
    lcd_rowstart =
        (uint16_t)((((1u << LCD_ROTATION) & 0x96u) != 0)
                       ? memory_height - (panel_height + oy)
                       : oy);

    if (!log_error(gpio_set_level(PIN_LCD_RST, 0), "assert LCD reset")) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(8));
    if (!log_error(gpio_set_level(PIN_LCD_RST, 1), "release LCD reset")) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(64));

    madctl = lcd_madctl_for_rotation(LCD_ROTATION);
    if (!lcd_tx_param(LCD_CMD_GCTRL, gctrl, sizeof(gctrl), "LCD GCTRL")) return false;
    if (!lcd_tx_param(LCD_CMD_VCOMS, vcoms, sizeof(vcoms), "LCD VCOMS")) return false;
    if (!lcd_tx_param(LCD_CMD_LCMCTRL, lcmctrl, sizeof(lcmctrl), "LCD LCMCTRL")) return false;
    if (!lcd_tx_param(LCD_CMD_VDVVRHEN, vdvvrhen, sizeof(vdvvrhen), "LCD VDVVRHEN")) return false;
    if (!lcd_tx_param(LCD_CMD_VRHS, vrhs, sizeof(vrhs), "LCD VRHS")) return false;
    if (!lcd_tx_param(LCD_CMD_VDVSET, vdvset, sizeof(vdvset), "LCD VDVSET")) return false;
    if (!lcd_tx_param(LCD_CMD_PWCTRL1, pwctrl1, sizeof(pwctrl1), "LCD PWCTRL1")) return false;
    if (!lcd_tx_param(LCD_CMD_RAMCTRL, ramctrl, sizeof(ramctrl), "LCD RAMCTRL")) return false;
    if (!lcd_tx_param(LCD_CMD_PVGAMCTRL, gamma_positive, sizeof(gamma_positive), "LCD PVGAMCTRL")) return false;
    if (!lcd_tx_param(LCD_CMD_NVGAMCTRL, gamma_negative, sizeof(gamma_negative), "LCD NVGAMCTRL")) return false;
    if (!lcd_tx_param(LCD_CMD_SLPOUT, NULL, 0, "LCD SLPOUT")) return false;
    vTaskDelay(pdMS_TO_TICKS(130));
    if (!lcd_tx_param(LCD_CMD_COLMOD, &colmod, sizeof(colmod), "LCD COLMOD")) return false;
    if (!lcd_tx_param(LCD_CMD_MADCTL, &madctl, sizeof(madctl), "LCD MADCTL")) return false;
    if (!lcd_tx_param(LCD_CMD_IDMOFF, NULL, 0, "LCD IDMOFF")) return false;
    if (!lcd_tx_param(LCD_CMD_INVON, NULL, 0, "LCD INVON")) return false;
    if (!lcd_tx_param(LCD_CMD_DISPON, NULL, 0, "LCD DISPON")) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

static bool
tca_write_register(
    uint8_t reg,
    uint8_t value)
{
    uint8_t payload[] = {reg, value};

    return tca8418_dev != NULL &&
           log_error(
               i2c_master_transmit(
                   tca8418_dev,
                   payload,
                   sizeof(payload),
                   I2C_TIMEOUT_MS),
               "write TCA8418");
}

static bool
tca_read_register(
    uint8_t reg,
    uint8_t *value)
{
    return tca8418_dev != NULL && value != NULL &&
           log_error(
               i2c_master_transmit_receive(
                   tca8418_dev,
                   &reg,
                   1,
                   value,
                   1,
                   I2C_TIMEOUT_MS),
               "read TCA8418");
}

static bool
init_keyboard(void)
{
    i2c_master_bus_config_t bus_cfg = {0};
    i2c_device_config_t device_cfg = {0};
    uint8_t cfg;
    uint32_t drained;

    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = PIN_I2C_SDA;
    bus_cfg.scl_io_num = PIN_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    if (!log_error(i2c_new_master_bus(&bus_cfg, &i2c_bus), "create keyboard I2C bus")) {
        return false;
    }

    device_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_cfg.device_address = TCA8418_ADDRESS;
    device_cfg.scl_speed_hz = TCA8418_CLOCK_HZ;
    if (!log_error(
            i2c_master_bus_add_device(i2c_bus, &device_cfg, &tca8418_dev),
            "add TCA8418")) {
        return false;
    }

    if (!tca_write_register(TCA_REG_GPIO_DIR1, 0x00)) return false;
    if (!tca_write_register(TCA_REG_GPIO_DIR2, 0x00)) return false;
    if (!tca_write_register(TCA_REG_GPIO_DIR3, 0x00)) return false;
    if (!tca_write_register(TCA_REG_GPI_EM1, 0xFF)) return false;
    if (!tca_write_register(TCA_REG_GPI_EM2, 0xFF)) return false;
    if (!tca_write_register(TCA_REG_GPI_EM3, 0xFF)) return false;
    if (!tca_write_register(TCA_REG_GPIO_INT_LVL1, 0x00)) return false;
    if (!tca_write_register(TCA_REG_GPIO_INT_LVL2, 0x00)) return false;
    if (!tca_write_register(TCA_REG_GPIO_INT_LVL3, 0x00)) return false;
    if (!tca_write_register(TCA_REG_GPIO_INT_EN1, 0xFF)) return false;
    if (!tca_write_register(TCA_REG_GPIO_INT_EN2, 0xFF)) return false;
    if (!tca_write_register(TCA_REG_GPIO_INT_EN3, 0xFF)) return false;
    if (!tca_write_register(TCA_REG_KP_GPIO1, 0x7F)) return false;
    if (!tca_write_register(TCA_REG_KP_GPIO2, 0xFF)) return false;
    if (!tca_write_register(TCA_REG_KP_GPIO3, 0x00)) return false;
    if (!tca_write_register(TCA_REG_DEBOUNCE_DIS1, 0x00)) return false;
    if (!tca_write_register(TCA_REG_DEBOUNCE_DIS2, 0x00)) return false;
    if (!tca_write_register(TCA_REG_DEBOUNCE_DIS3, 0x00)) return false;

    if (!tca_read_register(TCA_REG_CFG, &cfg)) {
        return false;
    }
    cfg = (uint8_t)(cfg | TCA_CFG_GPI_IEN | TCA_CFG_KE_IEN);
    if (!tca_write_register(TCA_REG_CFG, cfg)) {
        return false;
    }

    for (drained = 0; drained < KEYBOARD_MAX_DRAIN_EVENTS; drained++) {
        uint8_t raw = 0;

        if (!tca_read_register(TCA_REG_KEY_EVENT_A, &raw)) {
            return false;
        }
        if (raw == 0) {
            break;
        }
    }
    if (drained == KEYBOARD_MAX_DRAIN_EVENTS) {
        ESP_LOGE(TAG, "TCA8418 event FIFO did not drain");
        return false;
    }

    return tca_write_register(TCA_REG_INT_STAT, 0x03);
}

static bool
keyboard_cell_down(
    uint8_t row,
    uint8_t column)
{
    return row < KEYBOARD_ROWS &&
           column < KEYBOARD_COLUMNS &&
           pressed_cells[row][column];
}

static bool
queue_key_event(
    uint8_t ascii,
    bool pressed)
{
    CardputerExtremeKeyEvent *event;

    if (ascii == 0) {
        return true;
    }
    if (key_event_count == KEY_EVENT_QUEUE_CAPACITY) {
        ESP_LOGW(TAG, "keyboard event queue full; dropping 0x%02X %s",
                 ascii, pressed ? "down" : "up");
        return false;
    }

    event = &key_event_queue[key_event_write];
    event->ascii = ascii;
    event->pressed = pressed;
    key_event_write =
        (uint8_t)((key_event_write + 1u) % KEY_EVENT_QUEUE_CAPACITY);
    key_event_count++;
    return true;
}

static uint8_t
key_for_cell_press(
    uint8_t row,
    uint8_t column)
{
    static const uint8_t normal[KEYBOARD_ROWS][KEYBOARD_COLUMNS] = {
        {'`', '1', '2', '3', '4', '5', '6',
         '7', '8', '9', '0', '-', '=', '\b'},
        {'\t', 'q', 'w', 'e', 'r', 't', 'y',
         'u', 'i', 'o', 'p', '[', ']', '\\'},
        {0, 0, 'a', 's', 'd', 'f', 'g',
         'h', 'j', 'k', 'l', ';', '\'', '\r'},
        {'\r', 0, '\b', 'z', 'x', 'c', 'v',
         'b', 'n', 'm', ',', '.', '/', ' '},
    };
    static const uint8_t shifted[KEYBOARD_ROWS][KEYBOARD_COLUMNS] = {
        {'~', '!', '@', '#', '$', '%', '^',
         '&', '*', '(', ')', '_', '+', '\b'},
        {'\t', 'Q', 'W', 'E', 'R', 'T', 'Y',
         'U', 'I', 'O', 'P', '{', '}', '|'},
        {0, 0, 'A', 'S', 'D', 'F', 'G',
         'H', 'J', 'K', 'L', ':', '"', '\r'},
        {'\r', 0, '\b', 'Z', 'X', 'C', 'V',
         'B', 'N', 'M', '<', '>', '?', ' '},
    };
    bool fn = keyboard_cell_down(2, 0);
    bool shift = keyboard_cell_down(2, 1);

    /*
     * The ADV keycaps print arrows on this Fn layer.  The bridge deliberately
     * accepts i/j/k/l as compact, uint8_t stand-ins for SDL arrow keycodes.
     */
    if (fn) {
        if (row == 2 && column == 11) return 'i';
        if (row == 3 && column == 10) return 'j';
        if (row == 3 && column == 11) return 'k';
        if (row == 3 && column == 12) return 'l';
        if (row == 0 && column == 0) return '\b';
        return 0;
    }

    return shift ? shifted[row][column] : normal[row][column];
}

static bool
apply_tca_event(
    uint8_t raw)
{
    uint16_t code;
    uint16_t zero_based;
    int hardware_row;
    int hardware_column;
    int mapped_row;
    int mapped_column;
    bool pressed;
    uint8_t logical_key;

    code = (uint16_t)(raw & 0x7Fu);
    if (raw == 0 || code == 0) {
        return false;
    }

    zero_based = (uint16_t)(code - 1u);
    hardware_row = (int)(zero_based / 10u);
    hardware_column = (int)(zero_based % 10u);
    mapped_column = hardware_row * 2 + ((hardware_column > 3) ? 1 : 0);
    mapped_row = (hardware_column + 4) % 4;
    if (mapped_row < 0 || mapped_row >= KEYBOARD_ROWS ||
        mapped_column < 0 || mapped_column >= KEYBOARD_COLUMNS) {
        ESP_LOGW(
            TAG,
            "ignore invalid TCA8418 event 0x%02X -> row=%d col=%d",
            raw,
            mapped_row,
            mapped_column);
        return false;
    }

    pressed = (raw & 0x80u) != 0;
    if (pressed_cells[mapped_row][mapped_column] == pressed) {
        return true;
    }

    pressed_cells[mapped_row][mapped_column] = pressed;
    if (pressed) {
        logical_key =
            key_for_cell_press((uint8_t)mapped_row, (uint8_t)mapped_column);
        emitted_key[mapped_row][mapped_column] = logical_key;
    } else {
        logical_key = emitted_key[mapped_row][mapped_column];
        emitted_key[mapped_row][mapped_column] = 0;
    }

    (void)queue_key_event(logical_key, pressed);
    return true;
}

static void
rebuild_physical_key_mask(void)
{
    uint64_t mask = 0;
    uint32_t row;
    uint32_t column;

    for (row = 0; row < KEYBOARD_ROWS; row++) {
        for (column = 0; column < KEYBOARD_COLUMNS; column++) {
            if (pressed_cells[row][column]) {
                mask |= UINT64_C(1) << (row * KEYBOARD_COLUMNS + column);
            }
        }
    }
    physical_key_mask = mask;
}

static uint32_t
build_action_mask(void)
{
    bool fn = keyboard_cell_down(2, 0);
    uint32_t mask = 0;

    if (fn && keyboard_cell_down(2, 11)) mask |= CARDPUTER_EXTREME_ACTION_UP;
    if (fn && keyboard_cell_down(3, 11)) mask |= CARDPUTER_EXTREME_ACTION_DOWN;
    if (fn && keyboard_cell_down(3, 10)) mask |= CARDPUTER_EXTREME_ACTION_LEFT;
    if (fn && keyboard_cell_down(3, 12)) mask |= CARDPUTER_EXTREME_ACTION_RIGHT;

    if ((fn && keyboard_cell_down(0, 0)) ||
        keyboard_cell_down(0, 13) ||
        keyboard_cell_down(3, 2)) {
        mask |= CARDPUTER_EXTREME_ACTION_MENU;
    }
    if (keyboard_cell_down(2, 13) ||
        keyboard_cell_down(3, 13) ||
        keyboard_cell_down(3, 0) ||
        gpio_get_level(PIN_BUTTON_A) == 0) {
        mask |= CARDPUTER_EXTREME_ACTION_SEARCH;
    }

    if (!fn) {
        if (keyboard_cell_down(1, 11)) mask |= CARDPUTER_EXTREME_ACTION_PAGE_UP;
        if (keyboard_cell_down(1, 12)) mask |= CARDPUTER_EXTREME_ACTION_PAGE_DOWN;
        if (keyboard_cell_down(1, 4)) mask |= CARDPUTER_EXTREME_ACTION_REPEAT;
        if (keyboard_cell_down(2, 2)) mask |= CARDPUTER_EXTREME_ACTION_AUTO;
        if (keyboard_cell_down(2, 4)) mask |= CARDPUTER_EXTREME_ACTION_DEFEND;
        if (keyboard_cell_down(1, 3)) mask |= CARDPUTER_EXTREME_ACTION_USE_ITEM;
        if (keyboard_cell_down(1, 2)) mask |= CARDPUTER_EXTREME_ACTION_THROW_ITEM;
        if (keyboard_cell_down(1, 1)) mask |= CARDPUTER_EXTREME_ACTION_FLEE;
        if (keyboard_cell_down(2, 5)) mask |= CARDPUTER_EXTREME_ACTION_FORCE;
        if (keyboard_cell_down(2, 3)) mask |= CARDPUTER_EXTREME_ACTION_STATUS;
    }

    return mask;
}

static bool
init_tf_bus(void)
{
    spi_bus_config_t bus_cfg = {0};

    if (tf_bus_ready) {
        return true;
    }

    bus_cfg.mosi_io_num = PIN_TF_MOSI;
    bus_cfg.miso_io_num = PIN_TF_MISO;
    bus_cfg.sclk_io_num = PIN_TF_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = TF_MAX_TRANSFER_BYTES;
    if (!log_error(
            spi_bus_initialize(TF_HOST, &bus_cfg, SDSPI_DEFAULT_DMA),
            "initialize TF SPI")) {
        return false;
    }

    tf_bus_ready = true;
    return true;
}

static uint16_t
scaled_source_coordinate(
    uint16_t destination,
    uint16_t source_size,
    uint16_t destination_size)
{
    uint32_t coordinate =
        ((uint32_t)(destination * 2u + 1u) * source_size) /
        ((uint32_t)destination_size * 2u);

    if (coordinate >= source_size) {
        coordinate = source_size - 1u;
    }
    return (uint16_t)coordinate;
}

bool
CardputerExtreme_Begin(void)
{
    gpio_config_t button_cfg = {0};

    button_cfg.pin_bit_mask = 1ULL << PIN_BUTTON_A;
    button_cfg.mode = GPIO_MODE_INPUT;
    button_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    button_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    button_cfg.intr_type = GPIO_INTR_DISABLE;
    if (!log_error(gpio_config(&button_cfg), "configure BtnA")) {
        return false;
    }
    if (!init_lcd()) {
        return false;
    }
    if (!lcd_fill(lcd_wire_rgb565(0, 0, 0))) {
        return false;
    }
    if (!CardputerExtreme_SetBacklight(true)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    keyboard_ready = init_keyboard();
    if (!keyboard_ready) {
        CardputerExtreme_ShowError("keyboard init", "TCA8418 unavailable");
        return false;
    }

    (void)CardputerExtreme_PollKeyboard();
    action_pressed_mask = 0;
    action_released_mask = 0;
    ESP_LOGI(
        TAG,
        "Cardputer ADV ready: ST7789 SPI3, TCA8418 I2C, no PSRAM");
    return true;
}

bool
CardputerExtreme_SetBacklight(
    bool enabled)
{
    return log_error(
        gpio_set_level(PIN_LCD_BL, enabled ? 1 : 0),
        enabled ? "enable backlight" : "disable backlight");
}

bool
CardputerExtreme_MountTf(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16u * 1024u,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    esp_err_t err;

    if (tf_mounted) {
        return true;
    }
    if (!init_tf_bus()) {
        return false;
    }

    host.slot = TF_HOST;
    host.max_freq_khz = TF_SPI_CLOCK_KHZ;
    host.unaligned_multi_block_rw_max_chunk_size = 8;
    slot_config.host_id = TF_HOST;
    slot_config.gpio_cs = PIN_TF_CS;

    err = esp_vfs_fat_sdspi_mount(
        CARDPUTER_EXTREME_TF_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &tf_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount TF: %s", esp_err_to_name(err));
        return false;
    }

    tf_mounted = true;
    ESP_LOGI(
        TAG,
        "TF mounted at %s on SPI2, %lu kHz",
        CARDPUTER_EXTREME_TF_MOUNT_POINT,
        (unsigned long)TF_SPI_CLOCK_KHZ);
    return true;
}

bool
CardputerExtreme_TfMounted(void)
{
    return tf_mounted;
}

void
CardputerExtreme_PrepareTfAccess(void)
{
    /*
     * Cardputer ADV routes TF to SPI2 and LCD to SPI3.  This target hook is
     * intentionally a no-op; it documents that no shared D/C-MISO GPIO
     * handoff (as required on CoreS3 SE) belongs in the FatFS hot path.
    */
}

bool
CardputerExtreme_SaveUnlink(
    const char *path,
    bool missing_ok)
{
    FRESULT result;

    if (path == NULL) {
        return false;
    }
    CardputerExtreme_PrepareTfAccess();
    result = f_unlink(path);
    return result == FR_OK || (missing_ok && result == FR_NO_FILE);
}

bool
CardputerExtreme_SaveRename(
    const char *old_path,
    const char *new_path)
{
    if (old_path == NULL || new_path == NULL) {
        return false;
    }
    CardputerExtreme_PrepareTfAccess();
    return f_rename(old_path, new_path) == FR_OK;
}

bool
CardputerExtreme_KeyboardReady(void)
{
    return keyboard_ready;
}

bool
CardputerExtreme_PollKeyboard(void)
{
    uint32_t previous_actions = action_mask;
    uint32_t drained = 0;
    bool ok = keyboard_ready;

    if (keyboard_ready) {
        while (drained < KEYBOARD_MAX_DRAIN_EVENTS) {
            uint8_t count = 0;
            uint8_t event;

            if (!tca_read_register(TCA_REG_KEY_LCK_EC, &count)) {
                ok = false;
                break;
            }
            count &= 0x0Fu;
            if (count == 0) {
                break;
            }

            while (count-- != 0 && drained < KEYBOARD_MAX_DRAIN_EVENTS) {
                if (!tca_read_register(TCA_REG_KEY_EVENT_A, &event)) {
                    ok = false;
                    break;
                }
                (void)apply_tca_event(event);
                drained++;
            }
            if (!ok) {
                break;
            }
        }
        if (drained == KEYBOARD_MAX_DRAIN_EVENTS) {
            ESP_LOGW(TAG, "TCA8418 event drain capped at %u", (unsigned)drained);
        }
        if (!tca_write_register(TCA_REG_INT_STAT, 0x01)) {
            ok = false;
        }
    }

    rebuild_physical_key_mask();
    action_mask = build_action_mask();
    action_pressed_mask = action_mask & ~previous_actions;
    action_released_mask = previous_actions & ~action_mask;

    {
        bool current_button_a_down = gpio_get_level(PIN_BUTTON_A) == 0;

        if (current_button_a_down != button_a_down) {
            (void)queue_key_event('\r', current_button_a_down);
            button_a_down = current_button_a_down;
        }
    }
    return ok;
}

bool
CardputerExtreme_PollKey(
    uint8_t *ascii,
    bool *pressed)
{
    const CardputerExtremeKeyEvent *event;

    if (ascii == NULL || pressed == NULL) {
        return false;
    }
    if (key_event_yield_pending) {
        key_event_yield_pending = false;
        return false;
    }
    if (key_event_count == 0) {
        (void)CardputerExtreme_PollKeyboard();
    }
    if (key_event_count == 0) {
        return false;
    }

    event = &key_event_queue[key_event_read];
    *ascii = event->ascii;
    *pressed = event->pressed;
    key_event_read =
        (uint8_t)((key_event_read + 1u) % KEY_EVENT_QUEUE_CAPACITY);
    key_event_count--;
    key_event_yield_pending = true;
    return true;
}

uint32_t
CardputerExtreme_ActionMask(void)
{
    return action_mask;
}

uint32_t
CardputerExtreme_ActionPressedMask(void)
{
    return action_pressed_mask;
}

uint32_t
CardputerExtreme_ActionReleasedMask(void)
{
    return action_released_mask;
}

uint64_t
CardputerExtreme_PhysicalKeyMask(void)
{
    return physical_key_mask;
}

bool
CardputerExtreme_FlushIndexedFramebuffer(
    const uint8_t *pixels,
    uint16_t pitch,
    const uint8_t *palette_rgba)
{
    const uint16_t max_rows =
        (uint16_t)(PAL_EXTREME_DISPLAY_DMA_BYTES /
                   (CARDPUTER_EXTREME_PAL_VIEW_WIDTH * 2u));
    uint16_t y;

    if (lcd_io == NULL || pixels == NULL || palette_rgba == NULL ||
        pitch < PAL_LOGICAL_WIDTH || max_rows == 0) {
        return false;
    }

    for (y = 0; y < CARDPUTER_EXTREME_LCD_HEIGHT;) {
        uint16_t rows = (uint16_t)(CARDPUTER_EXTREME_LCD_HEIGHT - y);

        if (rows > max_rows) {
            rows = max_rows;
        }
        if (!CardputerExtreme_ScaleIndexedStrip(
                pixels,
                pitch,
                palette_rgba,
                y,
                rows,
                pal_sram_display_dma,
                PAL_EXTREME_DISPLAY_DMA_BYTES)) {
            return false;
        }

        if (!lcd_send_region(
                CARDPUTER_EXTREME_PAL_VIEW_X,
                y,
                CARDPUTER_EXTREME_PAL_VIEW_WIDTH,
                rows)) {
            return false;
        }
        y = (uint16_t)(y + rows);
    }

    return true;
}

bool
CardputerExtreme_FlushArgb8888Texture(
    const void *pixels,
    uint16_t width,
    uint16_t height,
    uint16_t pitch)
{
    const uint16_t max_rows =
        (uint16_t)(PAL_EXTREME_DISPLAY_DMA_BYTES /
                   (CARDPUTER_EXTREME_LCD_WIDTH * 2u));
    const uint8_t *source_pixels = (const uint8_t *)pixels;
    uint16_t y;

    if (lcd_io == NULL || source_pixels == NULL ||
        width == 0 || height == 0 ||
        (uint32_t)pitch < (uint32_t)width * 4u ||
        max_rows == 0) {
        return false;
    }

    for (y = 0; y < CARDPUTER_EXTREME_LCD_HEIGHT;) {
        uint16_t rows = (uint16_t)(CARDPUTER_EXTREME_LCD_HEIGHT - y);
        uint16_t row;

        if (rows > max_rows) {
            rows = max_rows;
        }
        for (row = 0; row < rows; row++) {
            uint16_t display_y = (uint16_t)(y + row);
            uint16_t source_y = scaled_source_coordinate(
                display_y,
                height,
                CARDPUTER_EXTREME_PAL_VIEW_HEIGHT);
            const uint8_t *source =
                source_pixels + (uint32_t)source_y * pitch;
            uint16_t *destination =
                (uint16_t *)pal_sram_display_dma +
                (uint32_t)row * CARDPUTER_EXTREME_LCD_WIDTH;
            uint16_t display_x;

            for (display_x = 0;
                 display_x < CARDPUTER_EXTREME_LCD_WIDTH;
                 display_x++) {
                if (display_x < CARDPUTER_EXTREME_PAL_VIEW_X ||
                    display_x >=
                        CARDPUTER_EXTREME_PAL_VIEW_X +
                            CARDPUTER_EXTREME_PAL_VIEW_WIDTH) {
                    destination[display_x] = 0;
                } else {
                    uint16_t view_x =
                        (uint16_t)(display_x - CARDPUTER_EXTREME_PAL_VIEW_X);
                    uint16_t source_x = scaled_source_coordinate(
                        view_x,
                        width,
                        CARDPUTER_EXTREME_PAL_VIEW_WIDTH);
                    const uint8_t *pixel = source + (uint32_t)source_x * 4u;

                    /*
                     * SDL's ARGB8888 texture is B,G,R,A in little-endian
                     * memory.  Alpha is intentionally ignored.
                     */
                    destination[display_x] =
                        lcd_wire_rgb565(pixel[2], pixel[1], pixel[0]);
                }
            }
        }

        if (!lcd_send_strip(y, rows)) {
            return false;
        }
        y = (uint16_t)(y + rows);
    }

    return true;
}

#if defined(PAL_EXTREME_CHAPTER_CACHE)
void
CardputerExtreme_ShowLoading(
    uint8_t percent)
{
    static uint8_t last_percent = UINT8_MAX;
    static const uint8_t loading_glyphs[7][7] = {
        {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}, /* L */
        {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, /* O */
        {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, /* A */
        {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}, /* D */
        {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f}, /* I */
        {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}, /* N */
        {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e}, /* G */
    };
    const uint16_t max_rows =
        (uint16_t)(PAL_EXTREME_DISPLAY_DMA_BYTES /
                   (CARDPUTER_EXTREME_LCD_WIDTH * 2u));
    const uint16_t background = lcd_wire_rgb565(0, 8, 24);
    const uint16_t foreground = lcd_wire_rgb565(232, 248, 255);
    const uint16_t bar_empty = lcd_wire_rgb565(24, 48, 72);
    const uint16_t bar_full = lcd_wire_rgb565(0, 184, 248);
    const uint16_t glyph_scale = 3u;
    const uint16_t glyph_x = 57u;
    const uint16_t glyph_y = 25u;
    const uint16_t bar_x = 20u;
    const uint16_t bar_y = 81u;
    const uint16_t bar_width = 200u;
    const uint16_t bar_height = 18u;
    uint16_t y;

    if (percent > 100u) {
        percent = 100u;
    }
    if (percent == last_percent) {
        return;
    }
    last_percent = percent;
    if (lcd_io == NULL || max_rows == 0u) {
        return;
    }

    for (y = 0; y < CARDPUTER_EXTREME_LCD_HEIGHT;) {
        uint16_t rows = (uint16_t)(CARDPUTER_EXTREME_LCD_HEIGHT - y);
        uint16_t row;

        if (rows > max_rows) {
            rows = max_rows;
        }
        for (row = 0; row < rows; row++) {
            uint16_t display_y = (uint16_t)(y + row);
            uint16_t *destination =
                (uint16_t *)pal_sram_display_dma +
                (uint32_t)row * CARDPUTER_EXTREME_LCD_WIDTH;
            uint16_t x;

            for (x = 0; x < CARDPUTER_EXTREME_LCD_WIDTH; x++) {
                uint16_t color = background;

                if (display_y >= glyph_y &&
                    display_y < glyph_y + 7u * glyph_scale &&
                    x >= glyph_x &&
                    x < glyph_x + 7u * 6u * glyph_scale) {
                    uint16_t local_x = (uint16_t)(x - glyph_x);
                    uint16_t character =
                        (uint16_t)(local_x / (6u * glyph_scale));
                    uint16_t column =
                        (uint16_t)((local_x / glyph_scale) % 6u);
                    uint16_t glyph_row =
                        (uint16_t)((display_y - glyph_y) / glyph_scale);

                    if (character < 7u && column < 5u &&
                        (loading_glyphs[character][glyph_row] &
                         (uint8_t)(0x10u >> column)) != 0u) {
                        color = foreground;
                    }
                }

                if (display_y >= bar_y &&
                    display_y < bar_y + bar_height &&
                    x >= bar_x && x < bar_x + bar_width) {
                    uint16_t bar_local_x = (uint16_t)(x - bar_x);
                    uint16_t bar_local_y = (uint16_t)(display_y - bar_y);

                    if (bar_local_x < 2u ||
                        bar_local_x >= bar_width - 2u ||
                        bar_local_y < 2u ||
                        bar_local_y >= bar_height - 2u) {
                        color = foreground;
                    } else if ((uint32_t)(bar_local_x - 2u) * 100u <
                               (uint32_t)(bar_width - 4u) * percent) {
                        color = bar_full;
                    } else {
                        color = bar_empty;
                    }
                }
                destination[x] = color;
            }
        }
        if (!lcd_send_strip(y, rows)) {
            return;
        }
        y = (uint16_t)(y + rows);
    }
}
#endif

void
CardputerExtreme_ShowError(
    const char *line1,
    const char *line2)
{
    ESP_LOGE(
        TAG,
        "%s%s%s",
        line1 != NULL ? line1 : "ERROR",
        line2 != NULL ? ": " : "",
        line2 != NULL ? line2 : "");
    if (lcd_io != NULL) {
        (void)lcd_fill(lcd_wire_rgb565(248, 0, 0));
    }
}
