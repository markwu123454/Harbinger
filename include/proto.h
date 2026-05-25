#pragma once
#include <stdint.h>
#include <stddef.h>

// ── Message type IDs ─────────────────────────────────────────────────────
// Host → Device
constexpr uint8_t MSG_PING              = 0x01;  // no payload
constexpr uint8_t MSG_AIM               = 0x02;  // float heading, float elevation
constexpr uint8_t MSG_ARM               = 0x03;  // uint8_t flags
constexpr uint8_t MSG_SET_VOLTAGE       = 0x04;  // float voltage
constexpr uint8_t MSG_FIRE              = 0x05;  // no payload
constexpr uint8_t MSG_CLEAR_CALIBRATION = 0x06;  // no payload; clears NVS cal and reboots

// Device → Host
constexpr uint8_t MSG_PONG        = 0x81;  // no payload
constexpr uint8_t MSG_STATE       = 0x82;  // uint8_t flags, float target_v
constexpr uint8_t MSG_TELEMETRY   = 0x83;  // 6× float
constexpr uint8_t MSG_SHOT        = 0x84;  // uint32_t total, uint8_t count, [uint32_t t_us, float v, float drain]×count
constexpr uint8_t MSG_LOG         = 0x85;  // uint8_t level, uint8_t slen, char msg[slen]

// ── Fixed payload sizes (bytes, excluding type byte) ────────────────────────
constexpr size_t PSIZ_PING              = 0;
constexpr size_t PSIZ_AIM               = 8;   // 2× float32
constexpr size_t PSIZ_ARM               = 1;   // uint8_t flags
constexpr size_t PSIZ_SET_VOLTAGE       = 4;   // float32
constexpr size_t PSIZ_FIRE              = 0;
constexpr size_t PSIZ_CLEAR_CALIBRATION = 0;
constexpr size_t PSIZ_PONG              = 0;
constexpr size_t PSIZ_STATE             = 5;   // uint8_t + float32
constexpr size_t PSIZ_TELEMETRY         = 24;  // 6× float32
constexpr size_t PSIZ_SHOT_BASE         = 5;   // uint32_t + uint8_t (header before stage data)
constexpr size_t PSIZ_SHOT_STAGE        = 12;  // uint32_t + float32 + float32
constexpr size_t PSIZ_LOG_HEADER        = 2;   // uint8_t level + uint8_t slen; slen more bytes follow

// ── Log levels ────────────────────────────────────────────────────
constexpr uint8_t LOG_INFO  = 0;
constexpr uint8_t LOG_WARN  = 1;
constexpr uint8_t LOG_ERROR = 2;

// ── ARM flags ──────────────────────────────────────────────────────
// 2 bits per field in uint8_t: 0b00=no-change, 0b01=false, 0b10=true
constexpr uint8_t ARM_SHIFT_MASTER = 0;
constexpr uint8_t ARM_SHIFT_TURRET = 2;
constexpr uint8_t ARM_SHIFT_GUN    = 4;
constexpr uint8_t ARM_NO_CHANGE    = 0x00;
constexpr uint8_t ARM_FALSE        = 0x01;
constexpr uint8_t ARM_TRUE         = 0x02;

// Returns -1 (no change), 0 (false), or 1 (true)
inline int armDecode(uint8_t flags, uint8_t shift) {
    uint8_t v = (flags >> shift) & 0x03;
    if (v == ARM_TRUE)  return 1;
    if (v == ARM_FALSE) return 0;
    return -1;
}

// ── STATE flags ──────────────────────────────────────────────────────
constexpr uint8_t STATE_MASTER_ARM = 0x01;
constexpr uint8_t STATE_TURRET_ARM = 0x02;
constexpr uint8_t STATE_GUN_ARM    = 0x04;
constexpr uint8_t STATE_CAL_OK     = 0x08;  // both motors have valid FOC calibration

// ── Packed message structs ──────────────────────────────────────────────────
#pragma pack(push, 1)

struct PktAim {
    float heading;
    float elevation;
};

struct PktArm {
    uint8_t flags;
};

struct PktSetVoltage {
    float voltage;
};

struct PktState {
    uint8_t flags;    // STATE_* bitmask
    float   target_v;
};

struct PktTelemetry {
    float heading;
    float elevation;
    float motorA_vel;
    float motorA_acc;
    float motorB_vel;
    float motorB_acc;
};

struct PktShotHeader {
    uint32_t total_shots;
    uint8_t  stage_count;
};

struct PktShotStage {
    uint32_t t_us;
    float    v_mps;
    float    drain_v;
};

#pragma pack(pop)
