#include "cores3se_board.h"
#include "cores3se_hw.h"

#include "../../embedded/pal_memory.h"
#include "../../embedded/pal_video_static.h"

#include <stddef.h>
#include <string.h>

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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

static const char *TAG = "cores3se_board";

static const uint32_t I2C_TIMEOUT_MS = 1000u;
static const spi_host_device_t LCD_HOST = SPI3_HOST;
static const uint32_t LCD_PIXEL_CLOCK_HZ = 40000000u;
static const uint32_t TF_SPI_CLOCK_KHZ = 25000u;
static const uint8_t BACKLIGHT_BRIGHTNESS = 60u;
static const char *TF_MOUNT_POINT = "/sdcard";

static const uint8_t AXP_REG_ENABLE0 = 0x90u;
static const uint8_t AXP_REG_ALDO1 = 0x92u;
static const uint8_t AXP_REG_ALDO2 = 0x93u;
static const uint8_t AXP_REG_ALDO3 = 0x94u;
static const uint8_t AXP_REG_ALDO4 = 0x95u;
static const uint8_t AXP_REG_DLDO1 = 0x99u;
static const uint8_t AXP_REG_POWER_KEY = 0x27u;
static const uint8_t AXP_REG_CHARGE_LED = 0x69u;
static const uint8_t AXP_REG_COMMON_CONFIG = 0x10u;
static const uint8_t AXP_REG_ADC_ENABLE = 0x30u;

static const uint8_t TOUCH_REG_DEV_MODE = 0x00u;
static const uint8_t TOUCH_REG_POINTS = 0x02u;
static const uint8_t TOUCH_REG_POINT1 = 0x03u;
static const uint8_t TOUCH_REG_CIPHER = 0xA3u;
static const uint8_t TOUCH_REG_INT_MODE = 0xA4u;

static const uint8_t LCD_MADCTL = LCD_CMD_BGR_BIT;
static const uint8_t LCD_COLMOD = 0x55u;
static const uint8_t LCD_CMD_SETEXTC = 0xC8u;
static const uint8_t LCD_CMD_PWCTR1 = 0xC0u;
static const uint8_t LCD_CMD_PWCTR2 = 0xC1u;
static const uint8_t LCD_CMD_VMCTR1 = 0xC5u;
static const uint8_t LCD_CMD_DFUNCTR = 0xB6u;

static const uint8_t LCD_SETEXTC[] = {0xFFu, 0x93u, 0x42u};
static const uint8_t LCD_PWCTR1[] = {0x12u, 0x12u};
static const uint8_t LCD_PWCTR2[] = {0x03u};
static const uint8_t LCD_VMCTR1[] = {0xF2u};
static const uint8_t LCD_B0[] = {0xE0u};
static const uint8_t LCD_F6[] = {0x01u, 0x00u, 0x00u};
static const uint8_t LCD_GAMMA_POS[] = {
    0x00u, 0x0Cu, 0x11u, 0x04u, 0x11u, 0x08u, 0x37u, 0x89u, 0x4Cu, 0x06u, 0x0Cu, 0x0Au, 0x2Eu, 0x34u, 0x0Fu,
};
static const uint8_t LCD_GAMMA_NEG[] = {
    0x00u, 0x0Bu, 0x11u, 0x05u, 0x13u, 0x09u, 0x33u, 0x67u, 0x48u, 0x07u, 0x0Eu, 0x0Bu, 0x2Eu, 0x33u, 0x0Fu,
};
static const uint8_t LCD_DFUNCTR[] = {0x08u, 0x82u, 0x1Du, 0x04u};

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t aw9523_dev;
static i2c_master_dev_handle_t axp_dev;
static i2c_master_dev_handle_t touch_dev;
static esp_lcd_panel_io_handle_t lcd_io;
static sdmmc_card_t *tf_card;
static bool touch_ready;
static bool tf_mounted;

static bool log_error(esp_err_t err, const char *what)
{
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    return false;
}

static bool aw_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return aw9523_dev != NULL && log_error(i2c_master_transmit(aw9523_dev, data, sizeof(data), I2C_TIMEOUT_MS), "write AW9523");
}

static bool aw_read(uint8_t reg, uint8_t *value)
{
    return aw9523_dev != NULL && value != NULL &&
           log_error(i2c_master_transmit_receive(aw9523_dev, &reg, 1, value, 1, I2C_TIMEOUT_MS), "read AW9523");
}

static bool aw_update(uint8_t reg, uint8_t set_mask, uint8_t clear_mask)
{
    uint8_t value = 0;
    if (!aw_read(reg, &value)) {
        return false;
    }
    value = (uint8_t)((value | set_mask) & (uint8_t)~clear_mask);
    return aw_write(reg, value);
}

static bool axp_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return axp_dev != NULL && log_error(i2c_master_transmit(axp_dev, data, sizeof(data), I2C_TIMEOUT_MS), "write AXP2101");
}

static bool axp_read(uint8_t reg, uint8_t *value)
{
    return axp_dev != NULL && value != NULL &&
           log_error(i2c_master_transmit_receive(axp_dev, &reg, 1, value, 1, I2C_TIMEOUT_MS), "read AXP2101");
}

static bool touch_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return touch_dev != NULL && log_error(i2c_master_transmit(touch_dev, data, sizeof(data), I2C_TIMEOUT_MS), "write FT6336");
}

static bool touch_read(uint8_t reg, void *data, size_t size)
{
    return touch_dev != NULL && data != NULL && size != 0 &&
           log_error(i2c_master_transmit_receive(touch_dev, &reg, 1, (uint8_t *)data, size, I2C_TIMEOUT_MS), "read FT6336");
}

static void clear_touch_interrupt(void)
{
    uint8_t ignored = 0;
    if (aw9523_dev != NULL) {
        aw_read(CORES3SE_AW9523_REG_INPUT0, &ignored);
        aw_read(CORES3SE_AW9523_REG_INPUT1, &ignored);
    }
}

static bool set_backlight(uint8_t brightness)
{
    uint8_t enable0 = 0;
    uint8_t raw = 0;
    if (!axp_read(AXP_REG_ENABLE0, &enable0)) {
        return false;
    }
    if (brightness != 0) {
        raw = (uint8_t)(((uint32_t)brightness + 641u) >> 5);
        if (raw > 0x1Fu) {
            raw = 0x1Fu;
        }
        enable0 = (uint8_t)(enable0 | 0x80u);
    } else {
        enable0 = (uint8_t)(enable0 & (uint8_t)~0x80u);
    }
    return axp_write(AXP_REG_ENABLE0, enable0) && axp_write(AXP_REG_DLDO1, raw);
}

static bool init_i2c(void)
{
    i2c_master_bus_config_t cfg = {0};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = CORES3SE_PIN_I2C_SDA;
    cfg.scl_io_num = CORES3SE_PIN_I2C_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true;
    return log_error(i2c_new_master_bus(&cfg, &i2c_bus), "create I2C bus");
}

static bool add_i2c_device(uint8_t addr, i2c_master_dev_handle_t *dev, const char *what)
{
    i2c_device_config_t cfg = {0};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = CORES3SE_I2C_SPEED_HZ;
    return log_error(i2c_master_bus_add_device(i2c_bus, &cfg, dev), what);
}

static bool init_io_expander(void)
{
    uint8_t id = 0;
    uint8_t output0_mask = 0x01u | CORES3SE_AW9523_SPEAKER_ENABLE_MASK;
    bool enable_bus_5v;

    if (!add_i2c_device(CORES3SE_AW9523_ADDR, &aw9523_dev, "add AW9523")) {
        return false;
    }
    if (!aw_read(CORES3SE_AW9523_REG_ID, &id) || id != CORES3SE_AW9523_EXPECTED_ID) {
        ESP_LOGE(TAG, "unexpected AW9523 id: 0x%02X", id);
        return false;
    }

    gpio_config_t spi_probe_cfg = {0};
    spi_probe_cfg.pin_bit_mask = (1ULL << CORES3SE_PIN_LCD_DC) | (1ULL << CORES3SE_PIN_LCD_SCLK) | (1ULL << CORES3SE_PIN_LCD_MOSI);
    spi_probe_cfg.mode = GPIO_MODE_INPUT;
    spi_probe_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    spi_probe_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    spi_probe_cfg.intr_type = GPIO_INTR_DISABLE;
    if (!log_error(gpio_config(&spi_probe_cfg), "probe LCD SPI pullups")) {
        return false;
    }

    enable_bus_5v = gpio_get_level(CORES3SE_PIN_LCD_DC) == 0 &&
                    gpio_get_level(CORES3SE_PIN_LCD_SCLK) == 0 &&
                    gpio_get_level(CORES3SE_PIN_LCD_MOSI) == 0;
    if (enable_bus_5v) {
        output0_mask = (uint8_t)(output0_mask | CORES3SE_AW9523_BUS_ENABLE_MASK);
    }

    if (!aw_update(CORES3SE_AW9523_REG_OUTPUT0, output0_mask, 0)) return false;
    if (!aw_update(CORES3SE_AW9523_REG_OUTPUT1, 0x03u | CORES3SE_AW9523_BOOST_ENABLE_MASK, 0)) return false;
    if (!aw_write(CORES3SE_AW9523_REG_CONFIG0, 0x18u)) return false;
    if (!aw_write(CORES3SE_AW9523_REG_CONFIG1, 0x0Cu)) return false;
    if (!aw_write(CORES3SE_AW9523_REG_GLOBAL_CONTROL, 0x10u)) return false;
    if (!aw_write(CORES3SE_AW9523_REG_LED_MODE0, 0xFFu)) return false;
    if (!aw_write(CORES3SE_AW9523_REG_LED_MODE1, 0xFFu)) return false;
    ESP_LOGI(TAG, "AW9523 ready: bus_5v=%s boost=on", enable_bus_5v ? "enabled" : "off");
    clear_touch_interrupt();
    return true;
}

static bool init_pmu(void)
{
    static const uint8_t init_pairs[] = {
        AXP_REG_ENABLE0, 0xBFu,
        AXP_REG_ALDO1, 13u,
        AXP_REG_ALDO2, 28u,
        AXP_REG_ALDO3, 28u,
        AXP_REG_ALDO4, 28u,
        AXP_REG_POWER_KEY, 0x00u,
        AXP_REG_CHARGE_LED, 0x11u,
        AXP_REG_COMMON_CONFIG, 0x30u,
        AXP_REG_ADC_ENABLE, 0x0Fu,
    };
    uint8_t status = 0;
    size_t i;

    if (!add_i2c_device(CORES3SE_AXP2101_ADDR, &axp_dev, "add AXP2101")) {
        return false;
    }
    if (!axp_read(0x03u, &status)) {
        return false;
    }
    for (i = 0; i < sizeof(init_pairs); i += 2) {
        if (!axp_write(init_pairs[i], init_pairs[i + 1])) {
            return false;
        }
    }
    return set_backlight(BACKLIGHT_BRIGHTNESS);
}

static bool lcd_tx(int cmd, const void *data, size_t size, const char *what)
{
    return log_error(esp_lcd_panel_io_tx_param(lcd_io, cmd, data, size), what);
}

static bool set_lcd_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t x1 = (uint16_t)(x + width - 1u);
    const uint16_t y1 = (uint16_t)(y + height - 1u);
    const uint8_t col[] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)(x1 >> 8), (uint8_t)x1};
    const uint8_t row[] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)(y1 >> 8), (uint8_t)y1};
    return lcd_tx(LCD_CMD_CASET, col, sizeof(col), "LCD CASET") &&
           lcd_tx(LCD_CMD_RASET, row, sizeof(row), "LCD RASET");
}

static bool init_lcd(void)
{
    spi_bus_config_t bus_cfg = {0};
    esp_lcd_panel_io_spi_config_t io_cfg = {0};

    if (!aw_update(CORES3SE_AW9523_REG_OUTPUT1, 0, CORES3SE_AW9523_LCD_RESET_MASK)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(8));

    bus_cfg.sclk_io_num = CORES3SE_PIN_LCD_SCLK;
    bus_cfg.mosi_io_num = CORES3SE_PIN_LCD_MOSI;
    bus_cfg.miso_io_num = CORES3SE_PIN_LCD_DC;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = PAL_SRAM_DISPLAY_DMA_BYTES;
    if (!log_error(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), "init LCD SPI")) {
        return false;
    }
    CoreS3Se_PrepareLcdAccess();

    io_cfg.cs_gpio_num = CORES3SE_PIN_LCD_CS;
    io_cfg.dc_gpio_num = CORES3SE_PIN_LCD_DC;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = LCD_PIXEL_CLOCK_HZ;
    io_cfg.trans_queue_depth = 1;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    io_cfg.flags.sio_mode = 1;
    if (!log_error(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &lcd_io), "create LCD IO")) {
        return false;
    }

    if (!aw_update(CORES3SE_AW9523_REG_OUTPUT1, CORES3SE_AW9523_LCD_RESET_MASK, 0)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(64));

    if (!lcd_tx(LCD_CMD_SWRESET, NULL, 0, "LCD SWRESET")) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!lcd_tx(LCD_CMD_SETEXTC, LCD_SETEXTC, sizeof(LCD_SETEXTC), "LCD SETEXTC")) return false;
    if (!lcd_tx(LCD_CMD_PWCTR1, LCD_PWCTR1, sizeof(LCD_PWCTR1), "LCD PWCTR1")) return false;
    if (!lcd_tx(LCD_CMD_PWCTR2, LCD_PWCTR2, sizeof(LCD_PWCTR2), "LCD PWCTR2")) return false;
    if (!lcd_tx(LCD_CMD_VMCTR1, LCD_VMCTR1, sizeof(LCD_VMCTR1), "LCD VMCTR1")) return false;
    if (!lcd_tx(0xB0, LCD_B0, sizeof(LCD_B0), "LCD B0")) return false;
    if (!lcd_tx(0xF6, LCD_F6, sizeof(LCD_F6), "LCD F6")) return false;
    if (!lcd_tx(0xE0, LCD_GAMMA_POS, sizeof(LCD_GAMMA_POS), "LCD GMCTRP1")) return false;
    if (!lcd_tx(0xE1, LCD_GAMMA_NEG, sizeof(LCD_GAMMA_NEG), "LCD GMCTRN1")) return false;
    if (!lcd_tx(LCD_CMD_DFUNCTR, LCD_DFUNCTR, sizeof(LCD_DFUNCTR), "LCD DFUNCTR")) return false;
    if (!lcd_tx(LCD_CMD_SLPOUT, NULL, 0, "LCD SLPOUT")) return false;
    vTaskDelay(pdMS_TO_TICKS(120));
    if (!lcd_tx(LCD_CMD_COLMOD, &LCD_COLMOD, 1, "LCD COLMOD")) return false;
    if (!lcd_tx(LCD_CMD_MADCTL, &LCD_MADCTL, 1, "LCD MADCTL")) return false;
    if (!lcd_tx(LCD_CMD_IDMOFF, NULL, 0, "LCD IDMOFF")) return false;
    if (!lcd_tx(LCD_CMD_INVON, NULL, 0, "LCD INVON")) return false;
    if (!lcd_tx(LCD_CMD_DISPON, NULL, 0, "LCD DISPON")) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

static bool tf_card_present(void)
{
    uint8_t input0 = 0xFFu;

    if (!aw_read(CORES3SE_AW9523_REG_INPUT0, &input0)) {
        return false;
    }
    return (input0 & CORES3SE_AW9523_TF_DETECT_MASK) == 0;
}

static bool init_touch(void)
{
    uint8_t info[6] = {0};
    gpio_config_t int_cfg = {0};
    int_cfg.pin_bit_mask = 1ULL << CORES3SE_PIN_TOUCH_INT;
    int_cfg.mode = GPIO_MODE_INPUT;
    int_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    int_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    int_cfg.intr_type = GPIO_INTR_DISABLE;
    if (!log_error(gpio_config(&int_cfg), "config touch INT")) {
        return false;
    }
    if (!add_i2c_device(CORES3SE_TOUCH_ADDR, &touch_dev, "add FT6336")) {
        touch_dev = NULL;
        return false;
    }
    if (!touch_write(TOUCH_REG_DEV_MODE, 0x00u)) return false;
    if (!touch_read(TOUCH_REG_CIPHER, info, sizeof(info))) return false;
    if (!touch_write(TOUCH_REG_INT_MODE, 0x00u)) return false;
    clear_touch_interrupt();
    ESP_LOGI(TAG, "FT6336 ready: cipher=0x%02X vendor=0x%02X", info[0], info[5]);
    return info[5] != 0;
}

static void fill_line_rgb565(uint16_t color)
{
    uint16_t *dst = (uint16_t *)pal_sram_display_dma;
    uint32_t x;
    for (x = 0; x < CORES3SE_LCD_WIDTH; x++) {
        dst[x] = color;
    }
}

static bool flush_solid_rect(uint16_t y, uint16_t height, uint16_t color)
{
    uint16_t row;
    if (height == 0) {
        return true;
    }
    fill_line_rgb565(color);
    for (row = 0; row < height; row++) {
        if (!set_lcd_window(0, (uint16_t)(y + row), CORES3SE_LCD_WIDTH, 1)) {
            return false;
        }
        if (!log_error(esp_lcd_panel_io_tx_color(lcd_io, LCD_CMD_RAMWR, pal_sram_display_dma, CORES3SE_LCD_WIDTH * 2u), "flush LCD line")) {
            return false;
        }
    }
    return log_error(esp_lcd_panel_io_tx_param(lcd_io, -1, NULL, 0), "wait LCD idle");
}

bool CoreS3Se_Begin(void)
{
    if (!init_i2c() || !init_io_expander() || !init_pmu() || !init_lcd()) {
        return false;
    }
    touch_ready = init_touch();
    if (!touch_ready) {
        ESP_LOGW(TAG, "touch init failed");
    }
    return flush_solid_rect(0, CORES3SE_LCD_HEIGHT, 0x0000u);
}

bool CoreS3Se_MountTf(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16u * 1024u,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    esp_err_t err;

    if (tf_mounted) {
        return true;
    }
    if (!tf_card_present()) {
        ESP_LOGW(TAG, "TF card not present");
        return false;
    }

    CoreS3Se_PrepareTfAccess();
    host.slot = LCD_HOST;
    host.max_freq_khz = TF_SPI_CLOCK_KHZ;
    slot_config.host_id = LCD_HOST;
    slot_config.gpio_cs = CORES3SE_PIN_TF_CS;

    err = esp_vfs_fat_sdspi_mount(TF_MOUNT_POINT, &host, &slot_config, &mount_config, &tf_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount TF: %s", esp_err_to_name(err));
        CoreS3Se_PrepareLcdAccess();
        return false;
    }

    tf_mounted = true;
    ESP_LOGI(TAG, "TF mounted at %s", TF_MOUNT_POINT);
    CoreS3Se_PrepareLcdAccess();
    return true;
}

void CoreS3Se_PrepareTfAccess(void)
{
    gpio_set_direction(CORES3SE_PIN_LCD_DC, GPIO_MODE_INPUT);
    gpio_set_pull_mode(CORES3SE_PIN_LCD_DC, GPIO_FLOATING);
}

void CoreS3Se_PrepareLcdAccess(void)
{
    gpio_set_direction(CORES3SE_PIN_LCD_DC, GPIO_MODE_OUTPUT);
}

bool CoreS3Se_FlushPalFramebuffer(void)
{
    uint16_t y;
    const uint16_t *line = NULL;
    uint16_t pixels = 0;

    if (lcd_io == NULL) {
        return false;
    }
    CoreS3Se_PrepareLcdAccess();
    if (!flush_solid_rect(0, CORES3SE_PAL_Y_OFFSET, 0x0000u)) {
        return false;
    }
    for (y = 0; y < 200u; y++) {
        if (!PalVideo_ConvertLineRgb565(y, &line, &pixels) || pixels != CORES3SE_LCD_WIDTH) {
            return false;
        }
        if (!set_lcd_window(0, (uint16_t)(CORES3SE_PAL_Y_OFFSET + y), CORES3SE_LCD_WIDTH, 1)) {
            return false;
        }
        if (!log_error(esp_lcd_panel_io_tx_color(lcd_io, LCD_CMD_RAMWR, line, pixels * 2u), "flush PAL line")) {
            return false;
        }
    }
    if (!flush_solid_rect((uint16_t)(CORES3SE_PAL_Y_OFFSET + 200u), CORES3SE_PAL_Y_OFFSET, 0x0000u)) {
        return false;
    }
    return log_error(esp_lcd_panel_io_tx_param(lcd_io, -1, NULL, 0), "wait LCD idle");
}

bool CoreS3Se_TouchPoint(uint16_t *x, uint16_t *y)
{
    uint8_t points = 0;
    uint8_t raw[6] = {0};
    uint16_t tx;
    uint16_t ty;

    if (!touch_ready || touch_dev == NULL || x == NULL || y == NULL) {
        return false;
    }
    if (gpio_get_level(CORES3SE_PIN_TOUCH_INT) != 0) {
        return false;
    }
    if (!touch_read(TOUCH_REG_POINTS, &points, sizeof(points))) {
        clear_touch_interrupt();
        return false;
    }
    points &= 0x0Fu;
    if (points == 0 || points > 5) {
        clear_touch_interrupt();
        return false;
    }
    if (!touch_read(TOUCH_REG_POINT1, raw, sizeof(raw))) {
        clear_touch_interrupt();
        return false;
    }
    tx = (uint16_t)(((raw[0] & 0x0Fu) << 8) | raw[1]);
    ty = (uint16_t)(((raw[2] & 0x0Fu) << 8) | raw[3]);
    if (tx >= CORES3SE_LCD_WIDTH || ty >= CORES3SE_LCD_HEIGHT) {
        return false;
    }
    *x = tx;
    *y = ty;
    return true;
}

void CoreS3Se_ShowError(const char *line1, const char *line2)
{
    ESP_LOGE(TAG, "%s%s%s", line1 != NULL ? line1 : "ERROR", line2 != NULL ? ": " : "", line2 != NULL ? line2 : "");
    (void)flush_solid_rect(0, CORES3SE_LCD_HEIGHT, 0x00F8u);
}
