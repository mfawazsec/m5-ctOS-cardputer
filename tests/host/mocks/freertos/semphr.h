/* Mock: freertos/semphr.h */
#pragma once
#include "FreeRTOS.h"
#include <stdlib.h>

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return malloc(1); /* non-null sentinel */
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) {
    (void)s; (void)t; return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
    (void)s; return pdTRUE;
}
