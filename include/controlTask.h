#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DifferentialTurret.h"

class ControlTask {
public:
    void start(int core, int priority);

private:
    static void taskEntry(void* param);
    [[noreturn]] void run();

    DifferentialTurret turret_;
    TaskHandle_t handle_ = nullptr;
};