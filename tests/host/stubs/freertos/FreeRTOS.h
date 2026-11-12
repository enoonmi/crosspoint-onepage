#pragma once
// Host stub: the real FreeRTOS kernel. The UI layer only needs the task/mutex
// types and a few macros; nothing here runs a scheduler.

#include <cstdint>

typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY 0xFFFFFFFFu
#define portTICK_PERIOD_MS 1
