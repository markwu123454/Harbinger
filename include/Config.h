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
    // Closed-loop PID gains.
    //
    // Cascade structure: position P → velocity setpoint → velocity PID → voltage
    //
    // Tuning order:
    //   1. angle_P: raise until position response feels responsive but not overshooty
    //   2. velocity_P: raise until velocity tracking is crisp
    //   3. velocity_I: add only after P is stable — removes steady-state drag
    //   4. velocity_lpf: increase if motor hums/rattles (noisy encoder)
    //
    // These are conservative starting values estimated from observed oscillation
    // with the previous gains (angle_P=20, velocity_I=10 caused windup jitter):
    .angle_P       =  5.0f,   // was 20 — lower outer P reduces velocity command magnitude
    .velocity_P    =  0.5f,
    .velocity_I    =  5.0f,   // was 10 — high I winds up fast at 10ms loop, causing overshoot
    .velocity_D    =  0.0f,
    .velocity_ramp = 500.0f,  // was 1000 — gentler acceleration
    .velocity_lpf  =  0.05f,  // was 0.01 — longer LPF smooths noisy encoder velocity
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
