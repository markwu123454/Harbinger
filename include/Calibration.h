#pragma once
#include <SimpleFOC.h>

struct MotorCalibration {
    float  zea_A;   ///< zero_electric_angle for motor A (rad)
    float  zea_B;   ///< zero_electric_angle for motor B (rad)
    int8_t dir_A;   ///< sensor_direction for motor A (1=CW, -1=CCW)
    int8_t dir_B;
    bool   valid;
};

namespace CalibrationStore {
    /// Load calibration from NVS.  Returns valid=false if none stored.
    MotorCalibration load();
    /// Persist a successful alignment result to NVS.
    void save(float zea_A, Direction dir_A, float zea_B, Direction dir_B);
    /// Invalidate stored calibration (forces live alignment on next boot).
    void clear();
}
