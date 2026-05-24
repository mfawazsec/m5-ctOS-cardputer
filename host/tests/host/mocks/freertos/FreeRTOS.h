/* Mock: freertos/FreeRTOS.h */
#pragma once
#include <stdint.h>
#include <stdlib.h>

typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef int   BaseType_t;
typedef unsigned int TickType_t;
typedef unsigned int UBaseType_t;

#define pdPASS  1
#define pdFAIL  0
#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY  0xFFFFFFFFU
#define pdMS_TO_TICKS(ms) (ms)
#define configMAX_PRIORITIES 25
