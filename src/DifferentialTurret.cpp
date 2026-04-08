#include "DifferentialTurret.h"

DifferentialTurret::DifferentialTurret()
    : _motorA(11, 11.1f)
      , _motorB(11, 11.1f)
      , _config() {
    Serial.printf("[TURRET] Constructor called with default motor params (poles=11, resistance=11.1)\n");
}

void DifferentialTurret::begin(const TurretPins &pins, const TurretConfig &config) {
    Serial.printf(
        "[TURRET] begin() called - poles=%d, resistance=%.2f, vps=%.2f, vlim=%.2f, vel_lim=%.2f, gr_hdg=%.3f, gr_elv=%.3f\n",
        config.pole_pairs, config.phase_resistance,
        config.voltage_power_supply, config.voltage_limit,
        config.velocity_limit, config.gear_ratio_heading, config.gear_ratio_elevation);

    _config = config;

    _motorA = BLDCMotor(config.pole_pairs, config.phase_resistance);
    _motorB = BLDCMotor(config.pole_pairs, config.phase_resistance);
    Serial.printf("[TURRET] Motors reconstructed with poles=%d, resistance=%.2f\n",
                  config.pole_pairs, config.phase_resistance);

    _driverA = new BLDCDriver3PWM(pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    _driverB = new BLDCDriver3PWM(pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);
    Serial.printf("[TURRET] Driver A pins: pwm=(%d,%d,%d), en=%d\n", pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    Serial.printf("[TURRET] Driver B pins: pwm=(%d,%d,%d), en=%d\n", pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

    _driverA->voltage_power_supply = config.voltage_power_supply;
    _driverA->voltage_limit = config.voltage_power_supply;
    _driverA->init();
    Serial.printf("[TURRET] Driver A init complete - vps=%.2f\n", config.voltage_power_supply);

    _driverB->voltage_power_supply = config.voltage_power_supply;
    _driverB->voltage_limit = config.voltage_power_supply;
    _driverB->init();
    Serial.printf("[TURRET] Driver B init complete - vps=%.2f\n", config.voltage_power_supply);

    _motorA.linkDriver(_driverA);
    _motorA.voltage_limit = config.voltage_limit;
    _motorA.velocity_limit = config.velocity_limit;
    _motorA.controller = velocity_openloop;
    _motorA.init();
    _motorA.initFOC();
    Serial.printf("[TURRET] Motor A init+FOC complete - vlim=%.2f, vel_lim=%.2f, mode=velocity_openloop\n",
                  config.voltage_limit, config.velocity_limit);

    _motorB.linkDriver(_driverB);
    _motorB.voltage_limit = config.voltage_limit;
    _motorB.velocity_limit = config.velocity_limit;
    _motorB.controller = velocity_openloop;
    _motorB.init();
    _motorB.initFOC();
    Serial.printf("[TURRET] Motor B init+FOC complete - vlim=%.2f, vel_lim=%.2f, mode=velocity_openloop\n",
                  config.voltage_limit, config.velocity_limit);

    Serial.printf("[TURRET] begin() complete\n");
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
                                     ? velocity_openloop
                                     : angle_openloop;

    _motorA.controller = ct;
    _motorB.controller = ct;

    Serial.printf("[TURRET] setMode() - mode=%s, controller=%s\n",
                  (mode == TurretMode::VELOCITY) ? "VELOCITY" : "ANGLE",
                  (ct == velocity_openloop) ? "velocity_openloop" : "angle_openloop");
}

TurretMode DifferentialTurret::getMode() const {
    return _mode;
}

void DifferentialTurret::setTarget(const float heading, const float elevation) {
    if (heading != _heading_target || elevation != _elevation_target) {
        Serial.printf("[TURRET] setTarget() - heading=%.4f, elevation=%.4f\n", heading, elevation);
        _heading_target = heading;
        _elevation_target = elevation;
    }
}

void DifferentialTurret::mixAndApply() {
    const float hdg_motor = _heading_target * _config.gear_ratio_heading;
    const float elv_motor = _elevation_target * _config.gear_ratio_elevation;

    const float tgtA = hdg_motor + elv_motor;
    const float tgtB = hdg_motor - elv_motor;

    // Only log when targets actually change
    if (tgtA != _motorA.target || tgtB != _motorB.target) {
        Serial.printf("[TURRET] mixAndApply() - hdg_motor=%.4f, elv_motor=%.4f, tgtA=%.4f, tgtB=%.4f\n",
                      hdg_motor, elv_motor, tgtA, tgtB);
    }

    _motorA.target = tgtA;
    _motorB.target = tgtB;
}

// Remove Serial.printf from both - called every loop, zero value at steady state
float DifferentialTurret::getHeading() const {
    return (_motorA.shaft_angle + _motorB.shaft_angle) / 2.0f / _config.gear_ratio_heading;
}

float DifferentialTurret::getElevation() const {
    return (_motorA.shaft_angle - _motorB.shaft_angle) / 2.0f / _config.gear_ratio_elevation;
}

void DifferentialTurret::enable() {
    _driverA->enable();
    _driverB->enable();
    _enabled = true;
    Serial.printf("[TURRET] enable() - drivers enabled\n");
}

void DifferentialTurret::disable() {
    _driverA->disable();
    _driverB->disable();
    _enabled = false;
    Serial.printf("[TURRET] disable() - drivers disabled\n");
}

bool DifferentialTurret::getEnabled() const {
    return _enabled;
}

void DifferentialTurret::setVoltageLimit(const float volts) {
    Serial.printf("[TURRET] setVoltageLimit() - volts=%.2f\n", volts);
    _motorA.voltage_limit = volts;
    _motorB.voltage_limit = volts;
}

BLDCMotor &DifferentialTurret::motorA() { return _motorA; }
BLDCMotor &DifferentialTurret::motorB() { return _motorB; }
