#ifndef FREERTOS_FREERTOS_H
#define FREERTOS_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef unsigned int UBaseType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif
