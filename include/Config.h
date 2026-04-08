#pragma once
#include <IPAddress.h>
#include "DifferentialTurret.h"

// ── Network ───────────────────────────────────────────────
inline constexpr const char* AP_SSID = "Harbinger";
inline constexpr const char* AP_PASS = "";
inline const IPAddress AP_IP(192, 168, 4, 1);
inline const IPAddress AP_MASK(255, 255, 255, 0);
constexpr uint8_t DNS_PORT = 53;

// ── Turret ────────────────────────────────────────────────
inline constexpr TurretConfig DEFAULT_TURRET_CONFIG {
    .voltage_power_supply = 24.0f,
    .voltage_limit        = 8.0f,
    .velocity_limit       = 5.0f,
    .pole_pairs           = 11,
    .phase_resistance     = 11.1f,
    .gear_ratio_heading   = 120.0f / 20.0f,
    .gear_ratio_elevation = 120.0f / 20.0f * 15.0f / 110.0f,
};

inline constexpr TurretPins DEFAULT_TURRET_PINS {
    .pwmA_a = 25, .pwmA_b = 26, .pwmA_c = 27, .enA = 14,
    .pwmB_a = 17, .pwmB_b = 5, .pwmB_c = 19, .enB = 23
};