#pragma once
#include "DifferentialTurret.h"

// ── Bluetooth ──────────────────────────────────────────────
 inline constexpr const char* BT_NAME = "Harbinger";

// ── Turret ──────────────────────────────────────────────
inline constexpr TurretConfig DEFAULT_TURRET_CONFIG {
    .voltage_power_supply = 24.0f,
    .voltage_limit        = 3.0f,
    .velocity_limit       = 15.0f,
    .pole_pairs           = 11,
    .phase_resistance     = 11.1f,
    .gear_ratio_heading   = 120.0f / 20.0f,
    .gear_ratio_elevation = 120.0f / 20.0f * 15.0f / 110.0f,
    // Closed-loop PID gains – tune these for your system
    .angle_P       = 20.0f,
    .velocity_P    =  0.5f,
    .velocity_I    = 10.0f,
    .velocity_D    =  0.0f,
    .velocity_ramp = 1000.0f,
    .velocity_lpf  = 0.01f,
    // 12V gives ~1.1A through 11.1 Ohm phases — enough torque to rotate the
    // differential mechanism during initFOC alignment.  6V was insufficient
    // (caused rawDelta < SimpleFOC MIN_ANGLE_DETECT_MOVEMENT).
    .sensor_align_voltage = 12.0f,
};

inline constexpr TurretPins DEFAULT_TURRET_PINS {
    .pwmA_a = 25, .pwmA_b = 26, .pwmA_c = 27, .enA = 14,
    .pwmB_a = 17, .pwmB_b = 5,  .pwmB_c = 19, .enB = 23,
    // TCA9548A mux on standard ESP32 I2C pins (change if your wiring differs)
    .sda     = 21,
    .scl     = 22,
    .muxAddr = 0x70,  // A0/A1/A2 all tied to GND
    .chanA   = 1,     // Motor A AS5600 on mux channel 1 (swapped: ch1 has agc≈114, ch0 has agc≈15)
    .chanB   = 0,     // Motor B AS5600 on mux channel 0
};
