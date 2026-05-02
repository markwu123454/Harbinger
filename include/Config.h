#pragma once
#include "DifferentialTurret.h"

// ── Bluetooth ─────────────────────────────────────────────────
inline constexpr const char* BT_NAME = "Harbinger";

// ── Turret ────────────────────────────────────────────────────
inline constexpr TurretConfig DEFAULT_TURRET_CONFIG {
    .voltage_power_supply = 24.0f,
    .voltage_limit        = 3.0f,
    .velocity_limit       = 15.0f,
    .pole_pairs           = 11,
    .phase_resistance     = 11.1f,
    .gear_ratio_heading   = 120.0f / 20.0f,
    .gear_ratio_elevation = 120.0f / 20.0f * 15.0f / 110.0f,
};

inline constexpr TurretPins DEFAULT_TURRET_PINS {
    .pwmA_a = 25, .pwmA_b = 26, .pwmA_c = 27, .enA = 14,
    .pwmB_a = 17, .pwmB_b = 5,  .pwmB_c = 19, .enB = 23
};
