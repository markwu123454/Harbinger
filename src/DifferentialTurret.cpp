#include "DifferentialTurret.h"

DifferentialTurret::DifferentialTurret()
    : _motorA(11, 11.1f) // defaults, overwritten in begin()
    , _motorB(11, 11.1f)
    , _config() {
}

void DifferentialTurret::begin(const TurretPins& pins, const TurretConfig& config) {
    _config = config;

    // Reconstruct motors with correct params
    _motorA = BLDCMotor(config.pole_pairs, config.phase_resistance);
    _motorB = BLDCMotor(config.pole_pairs, config.phase_resistance);

    // Create drivers
    _driverA = new BLDCDriver3PWM(pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    _driverB = new BLDCDriver3PWM(pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

    // Driver A
    _driverA->voltage_power_supply = config.voltage_power_supply;
    _driverA->voltage_limit = config.voltage_power_supply;
    _driverA->init();

    // Driver B
    _driverB->voltage_power_supply = config.voltage_power_supply;
    _driverB->voltage_limit = config.voltage_power_supply;
    _driverB->init();

    // Motor A
    _motorA.linkDriver(_driverA);
    _motorA.voltage_limit = config.voltage_limit;
    _motorA.velocity_limit = config.velocity_limit;
    _motorA.controller = velocity_openloop;
    _motorA.init();
    _motorA.initFOC();

    // Motor B
    _motorB.linkDriver(_driverB);
    _motorB.voltage_limit = config.voltage_limit;
    _motorB.velocity_limit = config.velocity_limit;
    _motorB.controller = velocity_openloop;
    _motorB.init();
    _motorB.initFOC();
}

void DifferentialTurret::update() {
    _motorA.loopFOC();
    _motorB.loopFOC();

    mixAndApply();

    _motorA.move();
    _motorB.move();
}

void DifferentialTurret::setMode(const TurretMode mode) {
    _mode = mode;

    const MotionControlType ct = (mode == TurretMode::VELOCITY)
        ? velocity_openloop : angle_openloop;

    _motorA.controller = ct;
    _motorB.controller = ct;
}

TurretMode DifferentialTurret::getMode() const {
    return _mode;
}

void DifferentialTurret::setTarget(const float heading, const float elevation) {
    _heading_target = heading;
    _elevation_target = elevation;
}

void DifferentialTurret::mixAndApply() {
    // Differential mixing: both spin same way = heading, opposite = elevation
    // Scale by gear ratio to convert output commands to motor-side values
    const float hdg_motor = _heading_target   * _config.gear_ratio_heading;
    const float elv_motor = _elevation_target * _config.gear_ratio_elevation;

    _motorA.target = hdg_motor + elv_motor;
    _motorB.target = hdg_motor - elv_motor;
}

float DifferentialTurret::getHeading() const {
    // Average of both motors / gear ratio gives output heading
    return (_motorA.shaft_angle + _motorB.shaft_angle) / 2.0f / _config.gear_ratio_heading;
}

float DifferentialTurret::getElevation() const {
    // Difference of both motors / gear ratio gives output elevation
    return (_motorA.shaft_angle - _motorB.shaft_angle) / 2.0f / _config.gear_ratio_elevation;
}

void DifferentialTurret::enable()
{
    _driverA->enable();
    _driverB->enable();
    _enabled = true;
}

void DifferentialTurret::disable()
{
    _driverA->disable();
    _driverB->disable();
    _enabled = false;
}

bool DifferentialTurret::getEnabled() const {
    return _enabled;
}

void DifferentialTurret::setVoltageLimit(const float volts) {
    _motorA.voltage_limit = volts;
    _motorB.voltage_limit = volts;
}

BLDCMotor& DifferentialTurret::motorA() { return _motorA; }
BLDCMotor& DifferentialTurret::motorB() { return _motorB; }