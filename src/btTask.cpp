#include "BtTask.h"
#include "SharedData.h"
#include "Config.h"
#include "proto.h"
#include <string.h>

// ── Task plumbing ─────────────────────────────────────────────

void BtTask::start(int core, int priority) {
    Serial.printf("[BT] Starting task — core=%d, priority=%d\n", core, priority);
    xTaskCreatePinnedToCore(taskEntry, "BT", 8192, this, priority, &handle_, core);
}

void BtTask::taskEntry(void* param) {
    static_cast<BtTask*>(param)->run();
}

// ── Main loop ─────────────────────────────────────────────────

void BtTask::run() {
    Serial.printf("[BT] Task running on core %d\n", xPortGetCoreID());

    if (!bt_.begin(BT_NAME)) {
        Serial.println("[BT] BluetoothSerial init failed");
        vTaskDelete(nullptr);
        return;
    }
    Serial.printf("[BT] SPP device \"%s\" ready, awaiting connection\n", BT_NAME);

    bool         wasConnected = false;
    unsigned long lastTelemetry = 0;

    for (;;) {
        bool connected = bt_.hasClient();

        // ── RX ──────────────────────────────────────────────────
        while (bt_.available()) {
            uint8_t b = static_cast<uint8_t>(bt_.read());

            if (rxState_ == RxState::TYPE) {
                switch (b) {
                case MSG_PING:
                    processMessage(b, nullptr, 0);
                    break;
                case MSG_FIRE:
                    processMessage(b, nullptr, 0);
                    break;
                case MSG_AIM:
                    rxType_ = b; rxExpected_ = PSIZ_AIM; rxGot_ = 0;
                    rxState_ = RxState::PAYLOAD;
                    break;
                case MSG_ARM:
                    rxType_ = b; rxExpected_ = PSIZ_ARM; rxGot_ = 0;
                    rxState_ = RxState::PAYLOAD;
                    break;
                case MSG_SET_VOLTAGE:
                    rxType_ = b; rxExpected_ = PSIZ_SET_VOLTAGE; rxGot_ = 0;
                    rxState_ = RxState::PAYLOAD;
                    break;
                default:
                    Serial.printf("[BT] Unknown type byte 0x%02x — skipping\n", b);
                    break;
                }
            } else {
                rxBuf_[rxGot_++] = b;
                if (rxGot_ >= rxExpected_) {
                    processMessage(rxType_, rxBuf_, rxExpected_);
                    rxState_ = RxState::TYPE;
                }
            }
        }

        // ── Shared state snapshot ────────────────────────────────
        WifiSnapshot snap = wifiRead();  // always call to drain stateChanged flag

        // ── Connection lifecycle ─────────────────────────────────
        if (connected && !wasConnected) {
            Serial.println("[BT] Client connected — sending initial state + telemetry");
            sendState(snap.masterArm, snap.turretArm, snap.gunArm, snap.targetVoltage);
            sendTelemetry(snap.currentHeading, snap.currentElevation,
                          snap.motorA_vel, snap.motorA_acc,
                          snap.motorB_vel, snap.motorB_acc);
            lastTelemetry = millis();
        } else if (!connected && wasConnected) {
            Serial.println("[BT] Client disconnected");
        }
        wasConnected = connected;

        if (!connected) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        // ── TX: state change ─────────────────────────────────────
        if (snap.stateChanged) {
            Serial.printf("[BT] State change — master=%d turret=%d gun=%d v=%.2f\n",
                snap.masterArm, snap.turretArm, snap.gunArm, snap.targetVoltage);
            sendState(snap.masterArm, snap.turretArm, snap.gunArm, snap.targetVoltage);
        }

        // ── TX: telemetry (50 ms) ────────────────────────────────
        unsigned long now = millis();
        if (now - lastTelemetry >= 50) {
            sendTelemetry(snap.currentHeading, snap.currentElevation,
                          snap.motorA_vel, snap.motorA_acc,
                          snap.motorB_vel, snap.motorB_acc);
            lastTelemetry = now;
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

// ── Message handlers ──────────────────────────────────────────

void BtTask::processMessage(uint8_t type, const uint8_t* payload, size_t) {
    switch (type) {
    case MSG_PING: {
        uint8_t pong = MSG_PONG;
        bt_.write(&pong, 1);
        break;
    }
    case MSG_AIM: {
        PktAim pkt;
        memcpy(&pkt, payload, sizeof(pkt));
        wifiWriteAim(pkt.heading, pkt.elevation);
        break;
    }
    case MSG_ARM: {
        PktArm pkt;
        memcpy(&pkt, payload, sizeof(pkt));
        wifiWriteArm(armDecode(pkt.flags, ARM_SHIFT_MASTER),
                     armDecode(pkt.flags, ARM_SHIFT_TURRET),
                     armDecode(pkt.flags, ARM_SHIFT_GUN));
        break;
    }
    case MSG_SET_VOLTAGE: {
        PktSetVoltage pkt;
        memcpy(&pkt, payload, sizeof(pkt));
        wifiWriteVoltage(constrain(pkt.voltage, 0.0f, 120.0f));
        break;
    }
    case MSG_FIRE:
        wifiWriteFire();
        break;
    default:
        break;
    }
}

// ── TX helpers ────────────────────────────────────────────────

void BtTask::sendState(bool masterArm, bool turretArm, bool gunArm, float targetV) {
    PktState pkt;
    pkt.flags    = (masterArm ? STATE_MASTER_ARM : 0)
                 | (turretArm ? STATE_TURRET_ARM : 0)
                 | (gunArm    ? STATE_GUN_ARM    : 0);
    pkt.target_v = targetV;
    sendRaw(MSG_STATE, &pkt, sizeof(pkt));
}

void BtTask::sendTelemetry(float heading, float elevation,
                           float aVel, float aAcc, float bVel, float bAcc) {
    PktTelemetry pkt { heading, elevation, aVel, aAcc, bVel, bAcc };
    sendRaw(MSG_TELEMETRY, &pkt, sizeof(pkt));
}

void BtTask::sendRaw(uint8_t type, const void* payload, size_t len) {
    if (!bt_.hasClient()) return;
    bt_.write(&type, 1);
    if (payload && len > 0)
        bt_.write(static_cast<const uint8_t*>(payload), len);
}
