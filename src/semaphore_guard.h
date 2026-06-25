#pragma once

#include <Arduino.h>

// RAII lock guard for FreeRTOS recursive mutexes (xSemaphoreCreateRecursiveMutex).
// Blocks until the mutex is taken, releases it on scope exit.
// Non-copyable to prevent double-release.
// NOTE: use xSemaphoreCreateRecursiveMutex() to create the handle passed here.
class SemaphoreGuard {
    public:
        explicit SemaphoreGuard(SemaphoreHandle_t handle) : handle_{handle} {
            xSemaphoreTakeRecursive(handle_, portMAX_DELAY);
        }
        ~SemaphoreGuard() {
            xSemaphoreGiveRecursive(handle_);
        }
        SemaphoreGuard(SemaphoreGuard const&) = delete;
        SemaphoreGuard& operator=(SemaphoreGuard const&) = delete;

    private:
        SemaphoreHandle_t handle_;
};
