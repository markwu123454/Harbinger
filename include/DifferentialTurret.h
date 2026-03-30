#pragma once

#include <Arduino.h>
#include <SimpleFOC.h>

enum class TurretMode {
    VELOCITY,   // joystick-style: pan/tilt rate commands
    POSITION    // point-to-angle: pan/tilt angle commands
};

struct TurretPins {
    // Motor A driver pins
    int pwmA_a;
    int pwmA_b;
    int pwmA_c;
    int enA;

    // Motor B driver pins
    int pwmB_a;
    int pwmB_b;
    int pwmB_c;
    int enB;
};

struct TurretConfig {
    float voltage_power_supply = 24.0f;
    float voltage_limit        = 3.0f;     // start low, raise after testing
    float velocity_limit       = 20.0f;    // rad/s safety cap

    // GM4108H-120T defaults
    int   pole_pairs           = 11;
    float phase_resistance     = 11.1f;

    // Gear ratios (motor turns : output turns)
    float gear_ratio_pan       = 6.0f;
    float gear_ratio_tilt      = 6.0f;     // update once decided
};

class DifferentialTurret {
public:
    DifferentialTurret();

    /// Call once in setup(). Initializes both motors and drivers.
    void begin(const TurretPins& pins, const TurretConfig& config = TurretConfig());

    /// Call every loop iteration. Runs FOC and motion control for both motors.
    void update();

    /// Set turret mode (velocity or position control)
    void setMode(TurretMode mode);
    [[nodiscard]] TurretMode getMode() const;

    /// Set target in current mode:
    ///   VELOCITY mode: pan_rate [rad/s], tilt_rate [rad/s] at the OUTPUT
    ///   POSITION mode: pan_angle [rad], tilt_angle [rad] at the OUTPUT
    void setTarget(float pan, float tilt);

    /// Read current output angles (estimated from open-loop integration)
    [[nodiscard]] float getPan() const;
    [[nodiscard]] float getTilt() const;

    /// Adjust voltage limit at runtime (for thermal safety tuning)
    void setVoltageLimit(float volts);

    /// Access underlying motors for Commander integration
    BLDCMotor& motorA();
    BLDCMotor& motorB();

private:
    /// Convert pan/tilt targets to individual motor targets
    /// Differential mixing:
    ///   motorA = pan + tilt
    ///   motorB = pan - tilt
    void mixAndApply();

    BLDCMotor       _motorA;
    BLDCMotor       _motorB;
    BLDCDriver3PWM* _driverA = nullptr;
    BLDCDriver3PWM* _driverB = nullptr;

    TurretMode  _mode = TurretMode::VELOCITY;
    TurretConfig _config;

    float _pan_target  = 0.0f;
    float _tilt_target = 0.0f;
};