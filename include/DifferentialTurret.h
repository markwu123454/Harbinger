#pragma once

#include <Arduino.h>
#include <SimpleFOC.h>

enum class TurretMode {
    VELOCITY,   // joystick-style: heading/elevation rate commands
    POSITION    // point-to-angle: heading/elevation angle commands
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
    float gear_ratio_heading   = 6.0f;
    float gear_ratio_elevation = 6.0f;     // update once decided
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
    ///   VELOCITY mode: heading_rate [rad/s], elevation_rate [rad/s] at the OUTPUT
    ///   POSITION mode: heading_angle [rad], elevation_angle [rad] at the OUTPUT
    void setTarget(float heading, float elevation);

    /// Read current output angles (estimated from open-loop integration)
    [[nodiscard]] float getHeading() const;
    [[nodiscard]] float getElevation() const;

    /// Adjust voltage limit at runtime (for thermal safety tuning)
    void setVoltageLimit(float volts);

    void enable();
    void disable();
    [[nodiscard]] bool getEnabled() const;

    /// Access underlying motors for Commander integration
    BLDCMotor& motorA();
    BLDCMotor& motorB();

private:
    /// Convert heading/elevation targets to individual motor targets
    /// Differential mixing:
    ///   motorA = heading + elevation
    ///   motorB = heading - elevation
    void mixAndApply();

    BLDCMotor       _motorA;
    BLDCMotor       _motorB;
    BLDCDriver3PWM* _driverA = nullptr;
    BLDCDriver3PWM* _driverB = nullptr;

    TurretMode  _mode = TurretMode::VELOCITY;
    bool        _enabled = true;
    TurretConfig _config;

    float _heading_target   = 0.0f;
    float _elevation_target = 0.0f;
};