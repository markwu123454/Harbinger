#pragma once

#include <Arduino.h>
#include <SimpleFOC.h>
#include "TCA9548A.h"
#include "MuxedMagneticSensor.h"

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

    // TCA9548A I2C multiplexer (both AS5600s share address 0x36 via mux)
    int     sda;      ///< I2C SDA
    int     scl;      ///< I2C SCL
    uint8_t muxAddr;  ///< TCA9548A address – A0/A1/A2 all GND = 0x70
    uint8_t chanA;    ///< Mux channel wired to Motor A AS5600
    uint8_t chanB;    ///< Mux channel wired to Motor B AS5600
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
    float sensor_align_voltage; ///< voltage used during initFOC() alignment (must be high enough to physically rotate the motor)
};

class DifferentialTurret {
public:
    DifferentialTurret();

    /// Call once in setup(). Initializes motors, drivers, mux, and AS5600 encoders.
    void begin(const TurretPins& pins, const TurretConfig& config);

    /// Call every loop iteration. Runs FOC and motion control for both motors.
    void update();

    /// Set turret mode
    void setMode(TurretMode mode);
    [[nodiscard]] TurretMode getMode() const;

    /// Set target in current mode:
    ///   VELOCITY:             heading/elevation rate [rad/s] at output
    ///   POSITION:             heading/elevation angle [rad] at output (open-loop)
    ///   CLOSED_LOOP_POSITION: heading/elevation angle [rad] at output (encoder-corrected)
    void setTarget(float heading, float elevation);

    /// Per-motor acceleration (rad/s²), computed as d(shaft_velocity)/dt each update().
    [[nodiscard]] float getMotorAAcceleration() const { return _motorA_acc; }
    [[nodiscard]] float getMotorBAcceleration() const { return _motorB_acc; }

    /// Read current output angles (sensor-based in closed-loop, estimated otherwise)
    [[nodiscard]] float getHeading() const;
    [[nodiscard]] float getElevation() const;

    /// Adjust voltage limit at runtime (for thermal safety tuning)
    void setVoltageLimit(float volts);

    void enable();
    void disable();
    [[nodiscard]] bool getEnabled() const;

    /// True when both motors have a valid FOC zero-electric-angle (from live
    /// alignment or NVS calibration).  False means initFOC failed and drivers
    /// are disabled; send MSG_CLEAR_CALIBRATION via BT and power on with 24 V
    /// to trigger a fresh alignment sweep.
    [[nodiscard]] bool isCalibrated() const { return _calibrated; }

    /// Erase NVS calibration so the next boot runs a live alignment sweep.
    static void clearCalibration();

    /// Access underlying motors for Commander integration
    BLDCMotor& motorA();
    BLDCMotor& motorB();

private:
    void mixAndApply();
    void applyPIDConfig();

    BLDCMotor               _motorA;
    BLDCMotor               _motorB;
    BLDCDriver3PWM*         _driverA  = nullptr;
    BLDCDriver3PWM*         _driverB  = nullptr;
    TCA9548A                _mux;
    MuxedMagneticSensorI2C* _sensorA  = nullptr;
    MuxedMagneticSensorI2C* _sensorB  = nullptr;

    TurretMode   _mode       = TurretMode::VELOCITY;
    bool         _enabled    = true;
    bool         _calibrated = false;
    TurretConfig _config     = {};

    float _heading_target   = 0.0f;
    float _elevation_target = 0.0f;

    float         _motorA_acc        = 0.0f;
    float         _motorB_acc        = 0.0f;
    float         _prevMotorA_vel    = 0.0f;
    float         _prevMotorB_vel    = 0.0f;
    unsigned long _lastUpdateUs      = 0;
};
