#include "cores3se_board.h"

#include "../../embedded/pal_memory.h"
#include "../../embedded/pal_pack.h"
#include "../../embedded/pal_video_static.h"

#include <esp_log.h>
#include <esp_partition.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>

static const char *TAG = "sdlpal_cores3se";

static PalPack pal_nor_pack;
static esp_partition_mmap_handle_t pal_nor_mmap_handle;
static bool pal_nor_ready;

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void load_demo_palette(void)
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        pal_sram_palette_work[i * 3u + 0u] = (uint8_t)i;
        pal_sram_palette_work[i * 3u + 1u] = (uint8_t)((i * 5u) >> 2);
        pal_sram_palette_work[i * 3u + 2u] = (uint8_t)(255u - i);
    }
    (void)PalVideo_SetPaletteRgb(0, 256, pal_sram_palette_work);
}

static bool open_nor_pack(void)
{
    const esp_partition_t *partition;
    uint8_t header[32];
    uint32_t pack_size;
    const void *mapped = NULL;
    esp_err_t err;

    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "pal_nor");
    ESP_LOGI(TAG, "pal_nor partition %s", partition != NULL ? "present" : "missing");
    if (partition == NULL) {
        return false;
    }

    ESP_LOGI(TAG, "pal_nor offset=0x%08" PRIx32 " size=%" PRIu32, partition->address, partition->size);
    err = esp_partition_read(partition, 0, header, sizeof(header));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read pal_nor header: %s", esp_err_to_name(err));
        return false;
    }
    if (read_le32(header) != PAL_PACK_MAGIC || read_le16(header + 4) != PAL_PACK_VERSION) {
        ESP_LOGE(TAG, "pal_nor does not contain a PAL pack");
        return false;
    }

    pack_size = read_le32(header + 24);
    if (pack_size > partition->size || pack_size < sizeof(header)) {
        ESP_LOGE(TAG, "bad pal_nor pack size: %" PRIu32, pack_size);
        return false;
    }

    err = esp_partition_mmap(partition, 0, pack_size, ESP_PARTITION_MMAP_DATA, &mapped, &pal_nor_mmap_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mmap pal_nor pack: %s", esp_err_to_name(err));
        return false;
    }
    if (!PalPack_OpenConst(&pal_nor_pack, (const uint8_t *)mapped, pack_size)) {
        ESP_LOGE(TAG, "PAL pack open failed");
        return false;
    }

    ESP_LOGI(TAG, "PAL NOR pack mapped: size=%" PRIu32 " archives=%u", pal_nor_pack.size, (unsigned)pal_nor_pack.archive_count);
    return true;
}

static void load_pack_palette_or_demo(void)
{
    PalPackSpan span;
    if (pal_nor_ready &&
        PalPack_MapConst(&pal_nor_pack, PAL_PACK_ARCHIVE_PAT, 0, &span) &&
        span.format == PAL_PACK_FORMAT_NATIVE &&
        span.size >= 256u * 3u) {
        (void)PalVideo_SetPaletteRgb(0, 256, span.data);
        return;
    }
    load_demo_palette();
}

static uint16_t sprite_frame_count(const uint8_t *sprite)
{
    if (sprite == NULL) {
        return 0;
    }
    return read_le16(sprite);
}

static const uint8_t *sprite_frame(const uint8_t *sprite, uint16_t frame)
{
    uint16_t count;
    uint32_t offset;
    if (sprite == NULL) {
        return NULL;
    }
    count = sprite_frame_count(sprite);
    if (frame >= count) {
        return NULL;
    }
    offset = (uint32_t)read_le16(sprite + (uint32_t)frame * 2u) * 2u;
    if (offset == 0) {
        return NULL;
    }
    return sprite + offset;
}

static uint16_t rle_width(const uint8_t *rle)
{
    if (rle == NULL) {
        return 0;
    }
    if (read_le32(rle) == 2u) {
        rle += 4;
    }
    return read_le16(rle);
}

static uint16_t rle_height(const uint8_t *rle)
{
    if (rle == NULL) {
        return 0;
    }
    if (read_le32(rle) == 2u) {
        rle += 4;
    }
    return read_le16(rle + 2);
}

static void blit_rle_to_framebuffer(const uint8_t *rle, int dx, int dy)
{
    uint32_t i = 0;
    uint32_t src_x = 0;
    uint16_t width;
    uint16_t height;
    uint32_t len;

    if (rle == NULL) {
        return;
    }
    if (read_le32(rle) == 2u) {
        rle += 4;
    }
    width = read_le16(rle);
    height = read_le16(rle + 2);
    if (width == 0 || height == 0 || width > 320u || height > 200u) {
        return;
    }
    if (dx + (int)width <= 0 || dx >= 320 || dy + (int)height <= 0 || dy >= 200) {
        return;
    }

    len = (uint32_t)width * height;
    rle += 4;
    while (i < len) {
        uint8_t t = *rle++;
        if ((t & 0x80u) != 0 && t <= 0x80u + width) {
            uint32_t skip = (uint32_t)t - 0x80u;
            i += skip;
            src_x += skip;
            while (src_x >= width) {
                src_x -= width;
                dy++;
            }
        } else {
            uint32_t j = 0;
            uint32_t sx = src_x;
            int x = dx + (int)src_x;
            int y = dy;
            while (j < t) {
                uint32_t k;
                if (y < 0) {
                    uint32_t skip = (uint32_t)(-y) * width;
                    if (skip >= t - j) {
                        j = t;
                        break;
                    }
                    j += skip;
                    y = 0;
                } else if (y >= 200) {
                    return;
                }
                if (x < 0) {
                    uint32_t skip = (uint32_t)(-x);
                    if (skip >= t - j) {
                        j = t;
                        break;
                    }
                    j += skip;
                    sx += skip;
                    x = 0;
                } else if (x >= 320) {
                    j += width - sx;
                    x -= (int)sx;
                    sx = 0;
                    y++;
                    continue;
                }

                k = (uint32_t)t - j;
                if (320u - (uint32_t)x < k) k = 320u - (uint32_t)x;
                if ((uint32_t)width - sx < k) k = (uint32_t)width - sx;
                for (; k != 0; k--) {
                    pal_sram_framebuffer[(uint32_t)y * 320u + (uint32_t)x] = rle[j];
                    j++;
                    x++;
                    sx++;
                }
                if (sx >= width) {
                    sx -= width;
                    x -= width;
                    y++;
                }
            }
            rle += t;
            i += t;
            src_x += t;
            while (src_x >= width) {
                src_x -= width;
                dy++;
            }
        }
    }
}

static void draw_demo_frame(uint32_t tick, bool touched, uint16_t tx, uint16_t ty)
{
    uint32_t x;
    uint32_t y;
    PalPackSpan ui_sprite;

    if (pal_nor_ready &&
        PalPack_MapConst(&pal_nor_pack, PAL_PACK_ARCHIVE_DATA, 9, &ui_sprite) &&
        ui_sprite.format == PAL_PACK_FORMAT_NATIVE) {
        uint16_t frames = sprite_frame_count(ui_sprite.data);
        uint16_t frame = frames != 0 ? (uint16_t)((tick / 4u) % frames) : 0;
        const uint8_t *rle = sprite_frame(ui_sprite.data, frame);
        uint16_t w = rle_width(rle);
        uint16_t h = rle_height(rle);

        memset(pal_sram_framebuffer, 0, PAL_SRAM_FRAMEBUFFER_BYTES);
        for (y = 0; y < 200u; y += 8u) {
            uint8_t color = (uint8_t)(8u + ((y + tick) & 0x1Fu));
            memset(pal_sram_framebuffer + y * 320u, color, 320u);
        }
        blit_rle_to_framebuffer(rle, (320 - (int)w) / 2, (200 - (int)h) / 2);
    } else {
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
    uint32_t tick = 0;

    if (!CoreS3Se_Begin()) {
        CoreS3Se_ShowError("BOARD FAIL", "CORES3SE INIT");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    pal_nor_ready = open_nor_pack();
    load_pack_palette_or_demo();
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
