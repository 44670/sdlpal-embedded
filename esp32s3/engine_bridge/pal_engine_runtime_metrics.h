#ifndef PAL_ENGINE_RUNTIME_METRICS_H
#define PAL_ENGINE_RUNTIME_METRICS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host builds provide a no-op implementation.  On the ESP32-S3 Cardputer
 * extreme profile this logs general internal-RAM and DMA-capable internal-RAM
 * free/minimum/largest-block values, plus the current task's stack high-water
 * mark, without allocating storage.
 */
void PalEngineBridge_LogRuntimeMemory(const char *stage);

#ifdef __cplusplus
}
#endif

#endif
