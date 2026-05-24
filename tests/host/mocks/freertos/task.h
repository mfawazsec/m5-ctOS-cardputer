/* Mock: freertos/task.h */
#pragma once
#include "FreeRTOS.h"

extern int g_task_create_calls;
extern BaseType_t g_task_create_ret;

static inline BaseType_t xTaskCreatePinnedToCore(
    void (*fn)(void*), const char *name, uint32_t stack,
    void *arg, UBaseType_t prio, TaskHandle_t *h, int core)
{
    (void)fn; (void)name; (void)stack;
    (void)arg; (void)prio; (void)core;
    g_task_create_calls++;
    if (h) *h = (void*)0x1;
    return g_task_create_ret;
}
static inline void vTaskDelay(TickType_t t) { (void)t; }
static inline void vTaskDelete(TaskHandle_t h) { (void)h; }
