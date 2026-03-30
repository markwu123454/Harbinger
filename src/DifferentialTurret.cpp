#include "DifferentialTurret.h"

DifferentialTurret::DifferentialTurret()
    : _motorA(11, 11.1f)   // defaults, overwritten in begin()
    , _motorB(11, 11.1f)
{}

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

void DifferentialTurret::setTarget(const float pan, const float tilt) {
    _pan_target = pan;
    _tilt_target = tilt;
}

void DifferentialTurret::mixAndApply() {
    // Differential mixing: both spin same way = pan, opposite = tilt
    // Scale by gear ratio to convert output commands to motor-side values
    const float pan_motor  = _pan_target  * _config.gear_ratio_pan;
    const float tilt_motor = _tilt_target * _config.gear_ratio_tilt;

    _motorA.target = pan_motor + tilt_motor;
    _motorB.target = pan_motor - tilt_motor;
}

float DifferentialTurret::getPan() const {
    // Average of both motors / gear ratio gives output pan
    return (_motorA.shaft_angle + _motorB.shaft_angle) / 2.0f / _config.gear_ratio_pan;
}

float DifferentialTurret::getTilt() const {
    // Difference of both motors / gear ratio gives output tilt
    return (_motorA.shaft_angle - _motorB.shaft_angle) / 2.0f / _config.gear_ratio_tilt;
}

void DifferentialTurret::setVoltageLimit(const float volts) {
    _motorA.voltage_limit = volts;
    _motorB.voltage_limit = volts;
}

BLDCMotor& DifferentialTurret::motorA() { return _motorA; }
BLDCMotor& DifferentialTurret::motorB() { return _motorB; }