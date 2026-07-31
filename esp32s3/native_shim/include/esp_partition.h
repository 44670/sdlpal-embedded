#ifndef ESP_PARTITION_H
#define ESP_PARTITION_H

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_MMAP_DATA 0

typedef void *esp_partition_mmap_handle_t;

typedef struct esp_partition_t {
    uint32_t address;
    uint32_t size;
    const char *label;
} esp_partition_t;

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
esp_err_t esp_partition_read(const esp_partition_t *partition, uint32_t src_offset, void *dst, size_t size);
esp_err_t esp_partition_mmap(
    const esp_partition_t *partition,
    uint32_t offset,
    uint32_t size,
    int memory,
    const void **out_ptr,
    esp_partition_mmap_handle_t *out_handle);

/*
 * Native contract processes keep their read-only test mapping for their
 * short lifetime.  The target uses ESP-IDF's real esp_partition_munmap().
 */
static inline void esp_partition_munmap(esp_partition_mmap_handle_t handle)
{
    (void)handle;
}

#endif
