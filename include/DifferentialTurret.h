#pragma once

#include <Arduino.h>
#include <SimpleFOC.h>

enum class TurretMode {
    VELOCITY,              ///< open-loop velocity (joystick)
    POSITION,              ///< open-loop angle
    CLOSED_LOOP_POSITION   ///< closed-loop angle via AS5600 encoders
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

    // AS5600 encoder I2C – two separate buses (both sensors share address 0x36)
    int sdaA;   ///< Motor A encoder SDA (I2C bus 0)
    int sclA;   ///< Motor A encoder SCL (I2C bus 0)
    int sdaB;   ///< Motor B encoder SDA (I2C bus 1)
    int sclB;   ///< Motor B encoder SCL (I2C bus 1)
};

struct TurretConfig {
    float voltage_power_supply;
    float voltage_limit;
    float velocity_limit;
    int   pole_pairs;
    float phase_resistance;
    float gear_ratio_heading;
    float gear_ratio_elevation;
    // Closed-loop PID gains (tune for your system)
    float angle_P;           ///< position loop P gain
    float velocity_P;        ///< velocity loop P gain
    float velocity_I;        ///< velocity loop I gain
    float velocity_D;        ///< velocity loop D gain
    float velocity_ramp;     ///< velocity command ramp [rad/s^2]
    float velocity_lpf;      ///< velocity measurement LPF time constant [s]
};

class DifferentialTurret {
public:
    DifferentialTurret();

    /// Call once in setup(). Initializes motors, drivers, and AS5600 encoders.
    void begin(const TurretPins& pins, const TurretConfig& config);

    /// Call every loop iteration. Runs FOC and motion control for both motors.
    void update();

    /// Set turret mode
    void setMode(TurretMode mode);
    [[nodiscard]] TurretMode getMode() const;

    /// Set target in current mode:
    ///   VELOCITY:              heading/elevation rate [rad/s] at output
    ///   POSITION:              heading/elevation angle [rad] at output (open-loop)
    ///   CLOSED_LOOP_POSITION:  heading/elevation angle [rad] at output (encoder-corrected)
    void setTarget(float heading, float elevation);

    /// Read current output angles (sensor-based in closed-loop, estimated otherwise)
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
    void mixAndApply();
    void applyPIDConfig();

    BLDCMotor          _motorA;
    BLDCMotor          _motorB;
    BLDCDriver3PWM*    _driverA  = nullptr;
    BLDCDriver3PWM*    _driverB  = nullptr;
    MagneticSensorI2C* _sensorA  = nullptr;
    MagneticSensorI2C* _sensorB  = nullptr;

    TurretMode   _mode    = TurretMode::VELOCITY;
    bool         _enabled = true;
    TurretConfig _config  = {};

    float _heading_target   = 0.0f;
    float _elevation_target = 0.0f;
};
