#include "cores3se_board.h"

#include "../../embedded/pal_memory.h"
#include "../../embedded/pal_video_static.h"

#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>

static const char *TAG = "sdlpal_cores3se";

static void load_demo_palette(void)
{
    uint8_t rgb[256u * 3u];
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        rgb[i * 3u + 0u] = (uint8_t)i;
        rgb[i * 3u + 1u] = (uint8_t)((i * 5u) >> 2);
        rgb[i * 3u + 2u] = (uint8_t)(255u - i);
    }
    (void)PalVideo_SetPaletteRgb(0, 256, rgb);
}

static void draw_demo_frame(uint32_t tick, bool touched, uint16_t tx, uint16_t ty)
{
    uint32_t x;
    uint32_t y;
    for (y = 0; y < 200u; y++) {
        uint8_t *dst = pal_sram_framebuffer + y * 320u;
        for (x = 0; x < 320u; x++) {
            uint32_t value = (x + y + tick) & 0xFFu;
            if (((x / 16u) ^ (y / 16u)) & 1u) {
                value = (value + 64u) & 0xFFu;
            }
            dst[x] = (uint8_t)value;
        }
    }

    if (touched && ty >= CORES3SE_PAL_Y_OFFSET && ty < CORES3SE_PAL_Y_OFFSET + 200u) {
        const int cx = (int)tx;
        const int cy = (int)ty - (int)CORES3SE_PAL_Y_OFFSET;
        int dy;
        for (dy = -10; dy <= 10; dy++) {
            int yy = cy + dy;
            int dx;
            if (yy < 0 || yy >= 200) {
                continue;
            }
            for (dx = -10; dx <= 10; dx++) {
                int xx = cx + dx;
                if (xx >= 0 && xx < 320 && dx * dx + dy * dy <= 100) {
                    pal_sram_framebuffer[(uint32_t)yy * 320u + (uint32_t)xx] = 255u;
                }
            }
        }
    }
}

void app_main(void)
{
    const esp_partition_t *pal_nor;
    uint32_t tick = 0;

    if (!CoreS3Se_Begin()) {
        CoreS3Se_ShowError("BOARD FAIL", "CORES3SE INIT");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    pal_nor = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "pal_nor");
    ESP_LOGI(TAG, "pal_nor partition %s", pal_nor != NULL ? "present" : "missing");
    if (pal_nor != NULL) {
        ESP_LOGI(TAG, "pal_nor offset=0x%08" PRIx32 " size=%" PRIu32, pal_nor->address, pal_nor->size);
    }

    load_demo_palette();
    for (;;) {
        uint16_t tx = 0;
        uint16_t ty = 0;
        bool touched = CoreS3Se_TouchPoint(&tx, &ty);
        draw_demo_frame(tick, touched, tx, ty);
        if (!CoreS3Se_FlushPalFramebuffer()) {
            CoreS3Se_ShowError("LCD FAIL", "FLUSH");
        }
        tick += touched ? 7u : 1u;
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}
