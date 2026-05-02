#pragma once
#include <BluetoothSerial.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "proto.h"

class BtTask {
public:
    void start(int core, int priority);

private:
    static void taskEntry(void* param);
    [[noreturn]] void run();

    void processMessage(uint8_t type, const uint8_t* payload, size_t len);
    void sendState(bool masterArm, bool turretArm, bool gunArm, float targetV);
    void sendTelemetry(float heading, float elevation,
                       float aVel, float aAcc, float bVel, float bAcc);
    void sendRaw(uint8_t type, const void* payload, size_t len);

    BluetoothSerial bt_;
    TaskHandle_t    handle_ = nullptr;

    // RX byte-stream parser
    enum class RxState { TYPE, PAYLOAD };
    RxState rxState_    = RxState::TYPE;
    uint8_t rxType_     = 0;
    size_t  rxExpected_ = 0;
    size_t  rxGot_      = 0;
    uint8_t rxBuf_[8]   = {};  // large enough for the biggest fixed-size incoming payload (AIM = 8)
};
