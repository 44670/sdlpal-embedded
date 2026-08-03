#include "pal_target_board.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "pal_engine_video";
static bool pal_engine_logged_first_present;
static uint32_t pal_engine_present_count;
static int64_t pal_engine_present_max_us;

void
PalEngineBridge_RenderPresent(
   const void *pixels,
   int pitch,
   int w,
   int h
)
{
#if defined(PAL_EXTREME_TWO_SCREENS)
   (void)pixels;
   (void)pitch;
   (void)w;
   (void)h;
#else
   int64_t start_us;
   int64_t flush_us;

   if (pitch <= 0 || w <= 0 || h <= 0)
   {
      return;
   }

   start_us = esp_timer_get_time();
   if (!CoreS3Se_FlushArgb8888Texture(pixels, (uint16_t)w, (uint16_t)h, (uint16_t)pitch))
   {
      ESP_LOGE(TAG, "engine LCD present failed: w=%d h=%d pitch=%d", w, h, pitch);
      return;
   }
   flush_us = esp_timer_get_time() - start_us;
   pal_engine_present_count++;
   if (flush_us > pal_engine_present_max_us)
   {
      pal_engine_present_max_us = flush_us;
   }
   if (!pal_engine_logged_first_present)
   {
      ESP_LOGI(TAG, "engine first present: w=%d h=%d pitch=%d flush_us=%lld",
         w,
         h,
         pitch,
         (long long)flush_us);
      pal_engine_logged_first_present = true;
   }
   if (pal_engine_present_count == 60u)
   {
      ESP_LOGI(TAG, "engine present stats: frames=%lu last_us=%lld max_us=%lld",
         (unsigned long)pal_engine_present_count,
         (long long)flush_us,
         (long long)pal_engine_present_max_us);
   }
#endif
}

#if defined(PAL_EXTREME_TWO_SCREENS)
void
PalEngineBridge_RenderPresentIndexed(
   const void *pixels,
   int pitch,
   int w,
   int h,
   const void *palette_rgba
)
{
   int64_t start_us;
   int64_t flush_us;

   if (pixels == NULL || palette_rgba == NULL || pitch < w ||
       w != PAL_TARGET_LCD_WIDTH || h != PAL_TARGET_LCD_HEIGHT)
   {
      return;
   }

   start_us = esp_timer_get_time();
   if (!PalTarget_FlushIndexedFramebuffer((const uint8_t *)pixels,
      (uint16_t)pitch,
      (const uint8_t *)palette_rgba))
   {
      ESP_LOGE(TAG, "native indexed LCD present failed: w=%d h=%d pitch=%d", w, h, pitch);
      return;
   }
   flush_us = esp_timer_get_time() - start_us;
   pal_engine_present_count++;
   if (flush_us > pal_engine_present_max_us)
   {
      pal_engine_present_max_us = flush_us;
   }
   if (!pal_engine_logged_first_present)
   {
      ESP_LOGI(TAG,
         "first native indexed present: %ux%u, flush_us=%lld",
         (unsigned)PAL_TARGET_LCD_WIDTH,
         (unsigned)PAL_TARGET_LCD_HEIGHT,
         (long long)flush_us);
      pal_engine_logged_first_present = true;
   }
   if (pal_engine_present_count == 60u)
   {
      ESP_LOGI(TAG, "native present stats: frames=%lu last_us=%lld max_us=%lld",
         (unsigned long)pal_engine_present_count,
         (long long)flush_us,
         (long long)pal_engine_present_max_us);
   }
}
#endif
