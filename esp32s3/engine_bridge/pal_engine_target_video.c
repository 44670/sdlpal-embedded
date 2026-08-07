#include "pal_target_board.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "pal_engine_video";
static bool pal_engine_logged_first_present;
static bool pal_engine_logged_bad_present;
static uint32_t pal_engine_present_count;
static int64_t pal_engine_present_max_us;

#if defined(PAL_EXTREME_TWO_SCREENS)
void
PalEngineBridge_NotifyPaletteChanged(
   void
)
{
   PalTarget_NotifyPaletteChanged();
}
#endif

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
   const void *palette_rgba,
   int region_x,
   int region_y,
   int region_w,
   int region_h
)
{
   int64_t start_us;
   int64_t flush_us;
#if defined(PAL_CORES3SE_NATIVE_ENGINE_HOST)
   extern uint16_t PalNativeHost_LogicalWidth(void);
   extern uint16_t PalNativeHost_LogicalHeight(void);
   const int target_width = PalNativeHost_LogicalWidth();
   const int target_height = PalNativeHost_LogicalHeight();
#else
   const int target_width = PAL_TARGET_LCD_WIDTH;
   const int target_height = PAL_TARGET_LCD_HEIGHT;
#endif

   if (pixels == NULL || palette_rgba == NULL || pitch < w ||
       w != target_width || h != target_height)
   {
      if (!pal_engine_logged_bad_present)
      {
         ESP_LOGE(TAG,
            "native indexed present rejected: pixels=%p palette=%p "
            "got=%dx%d pitch=%d expected=%dx%d",
            pixels, palette_rgba, w, h, pitch,
            target_width, target_height);
         pal_engine_logged_bad_present = true;
      }
      return;
   }

   start_us = esp_timer_get_time();
   if (region_x < 0 || region_y < 0 || region_w <= 0 || region_h <= 0)
   {
      if (!PalTarget_FlushIndexedFramebuffer((const uint8_t *)pixels,
         (uint16_t)pitch,
         (const uint8_t *)palette_rgba))
      {
         ESP_LOGE(TAG,
            "native indexed LCD present failed: w=%d h=%d pitch=%d",
            w, h, pitch);
         return;
      }
   }
   else if (!PalTarget_FlushIndexedFramebufferRegion(
      (const uint8_t *)pixels,
      (uint16_t)pitch,
      (const uint8_t *)palette_rgba,
      (uint16_t)region_x,
      (uint16_t)region_y,
      (uint16_t)region_w,
      (uint16_t)region_h))
   {
      ESP_LOGE(TAG,
         "native indexed LCD region present failed: frame=%dx%d "
         "region=%d,%d %dx%d",
         w, h, region_x, region_y, region_w, region_h);
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
         (unsigned)target_width,
         (unsigned)target_height,
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
