#ifndef UI_LOCK_H
#define UI_LOCK_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// LVGL and the settings belong to loop(), which runs holding uiMutex. Other
// tasks (the web server) take it only around short accesses to them, never
// around network I/O, so a slow client cannot freeze the screen.
extern SemaphoreHandle_t uiMutex;

struct UiLock
{
    UiLock() { xSemaphoreTake(uiMutex, portMAX_DELAY); }
    ~UiLock() { xSemaphoreGive(uiMutex); }
    UiLock(const UiLock &) = delete;
    UiLock &operator=(const UiLock &) = delete;
};

#endif // UI_LOCK_H
