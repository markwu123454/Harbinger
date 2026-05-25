#include "DifferentialTurret.h"
#include "SharedData.h"
#include "Calibration.h"
#include "proto.h"
#include <Wire.h>

DifferentialTurret::DifferentialTurret()
    : _motorA(11, 11.1f)
    , _motorB(11, 11.1f)
    , _config() {}

void DifferentialTurret::clearCalibration() {
    CalibrationStore::clear();
}

// ── Helpers ─────────────────────────────────────────────────────────────────────────────

// Apply a static phase voltage for durationMs, then check if the linked
// sensor moved.  Returns the absolute angle delta in radians.
static float motorMoveTest(BLDCMotor& motor, MuxedMagneticSensorI2C* sensor,
                           float voltage, uint32_t durationMs) {
    sensor->update();
    float ang0 = sensor->getAngle();
    motor.setPhaseVoltage(voltage, 0, _3PI_2);
    delay(durationMs);
    sensor->update();
    float ang1 = sensor->getAngle();
    motor.setPhaseVoltage(0, 0, 0);
    return fabsf(ang1 - ang0);
}

// ── Per-motor init ────────────────────────────────────────────────────────────────

bool DifferentialTurret::initOneMotor(
    BLDCMotor& motor, BLDCDriver3PWM* driver, MuxedMagneticSensorI2C* sensor,
    float storedZea, int8_t storedDir, bool useStored,
    float alignVoltage, float voltageLimit, uint8_t enPin, char label)
{
    motor.linkDriver(driver);
    motor.linkSensor(sensor);
    motor.voltage_limit        = voltageLimit;
    motor.velocity_limit       = _config.velocity_limit;
    motor.voltage_sensor_align = alignVoltage;
    motor.controller           = angle;

    int ok = motor.init();
    logWrite(ok ? LOG_INFO : LOG_ERROR, "Motor%c init %s", label, ok ? "OK" : "FAILED");
    motor.enable();

    // Movement self-test: verify the gate driver is sourcing current.
    // "NOT-MOVED" usually means the enable pin is active-LOW on this hardware
    // while SimpleFOC defaults to active-HIGH; auto-detect and flip if needed.
    {
        motor.voltage_limit = alignVoltage;
        float delta = motorMoveTest(motor, sensor, alignVoltage, 800);
        logWrite(delta > 0.01f ? LOG_INFO : LOG_WARN,
                 "Motor%c self-test: delta=%.4f rad (%.1f deg)  en=GPIO%d=%s  [%s]",
                 label, delta, delta * 57.2958f,
                 enPin, digitalRead(enPin) ? "HI" : "LO",
                 delta > 0.01f ? "MOVED" : "NOT-MOVED");

        if (delta <= 0.01f) {
            logWrite(LOG_WARN, "Motor%c: flipping enable_active_high to LOW and retrying", label);
            driver->enable_active_high = LOW;
            driver->enable();
            float delta2 = motorMoveTest(motor, sensor, alignVoltage, 800);
            logWrite(delta2 > 0.01f ? LOG_INFO : LOG_ERROR,
                     "Motor%c active-LOW retry: delta=%.4f rad  [%s]",
                     label, delta2,
                     delta2 > 0.01f ? "MOVED — active-LOW gate driver confirmed"
                                    : "STILL NOT MOVING — check wiring and 24V supply");
            if (delta2 <= 0.01f)
                driver->enable_active_high = HIGH;
        }
        motor.voltage_limit = voltageLimit;
    }

    sensor->update();
    float sensorAngle = sensor->getAngle();
    logWrite(LOG_INFO, "Motor%c pre-initFOC sensor angle: %.4f rad (%.1f deg)",
             label, sensorAngle, sensorAngle * 57.2958f);

    // Pre-set zero_electric_angle so initFOC() skips the physical alignment
    // sweep when a valid NVS calibration was loaded.
    if (useStored) {
        motor.zero_electric_angle = storedZea;
        motor.sensor_direction    = (Direction)storedDir;
    }

    // alignSensor() clamps Uq to voltage_limit; raise it for initFOC and restore.
    motor.voltage_limit = alignVoltage;
    bool result = (bool)motor.initFOC();
    motor.voltage_limit = voltageLimit;
    logWrite(result ? LOG_INFO : LOG_ERROR,
             "Motor%c initFOC: %s  zero_elec_angle=%.4f rad  shaft_angle=%.4f rad (%.1f deg)",
             label, result ? "OK" : "FAILED",
             motor.zero_electric_angle,
             motor.shaft_angle, motor.shaft_angle * 57.2958f);

    return result;
}

// ── begin() ─────────────────────────────────────────────────────────────────────────────

void DifferentialTurret::begin(const TurretPins& pins, const TurretConfig& config) {
    SimpleFOCDebug::enable(&Serial);

    _config = config;
    _motorA = BLDCMotor(config.pole_pairs, config.phase_resistance);
    _motorB = BLDCMotor(config.pole_pairs, config.phase_resistance);

    // Try to load NVS calibration.  Valid entry means previous live alignment
    // succeeded; use stored angles so initFOC() skips the physical sweep.
    MotorCalibration cal = CalibrationStore::load();
    if (cal.valid) {
        logWrite(LOG_INFO,
                 "NVS cal loaded: zeaA=%.4f dir=%+d  zeaB=%.4f dir=%+d",
                 cal.zea_A, (int)cal.dir_A, cal.zea_B, (int)cal.dir_B);
    } else {
        logWrite(LOG_WARN,
                 "No NVS cal — live FOC alignment will run (requires 24V motor power)");
    }

    // ── Drivers ───────────────────────────────────────────────────────────────
    _driverA = new BLDCDriver3PWM(pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    _driverB = new BLDCDriver3PWM(pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

    _driverA->voltage_power_supply = config.voltage_power_supply;
    _driverA->voltage_limit        = config.voltage_power_supply;
    int drvOkA = _driverA->init();
    logWrite(drvOkA ? LOG_INFO : LOG_ERROR, "DriverA init %s (pwm=%d,%d,%d en=%d)",
             drvOkA ? "OK" : "FAILED",
             pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);

    _driverB->voltage_power_supply = config.voltage_power_supply;
    _driverB->voltage_limit        = config.voltage_power_supply;
    int drvOkB = _driverB->init();
    logWrite(drvOkB ? LOG_INFO : LOG_ERROR, "DriverB init %s (pwm=%d,%d,%d en=%d)",
             drvOkB ? "OK" : "FAILED",
             pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

    // ── Sensors via TCA9548A mux ────────────────────────────────────────────────
    // Both AS5600s share address 0x36; the mux channel is selected in every
    // update() and getSensorAngle() call.
    Wire.begin(pins.sda, pins.scl);
    _mux = TCA9548A(pins.muxAddr);
    _mux.begin(Wire);

    if (!_sensorA) _sensorA = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanA);
    if (!_sensorB) _sensorB = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanB);
    _sensorA->init(&Wire);
    _sensorB->init(&Wire);

    float rawA = _sensorA->getSensorAngle();
    float rawB = _sensorB->getSensorAngle();
    logWrite(rawA == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorA raw angle: %.4f rad (%.1f deg)", rawA, rawA * 57.2958f);
    logWrite(rawB == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorB raw angle: %.4f rad (%.1f deg)", rawB, rawB * 57.2958f);

    // ── Motor A ───────────────────────────────────────────────────────────────────
    bool okA = initOneMotor(_motorA, _driverA, _sensorA,
                            cal.zea_A, cal.dir_A, cal.valid,
                            config.sensor_align_voltage, config.voltage_limit,
                            pins.enA, 'A');

    // Disable Motor A's driver before Motor B alignment — its cogging otherwise
    // resists Motor B through the coupled differential mechanism.
    _driverA->disable();

    // ── Motor B ───────────────────────────────────────────────────────────────────
    bool okB = initOneMotor(_motorB, _driverB, _sensorB,
                            cal.zea_B, cal.dir_B, cal.valid,
                            config.sensor_align_voltage, config.voltage_limit,
                            pins.enB, 'B');

    // ── Save calibration after a successful live alignment ────────────────────
    if (!cal.valid && okA && okB) {
        CalibrationStore::save(
            _motorA.zero_electric_angle, _motorA.sensor_direction,
            _motorB.zero_electric_angle, _motorB.sensor_direction);
        logWrite(LOG_INFO,
                 "Calibration saved to NVS (zeaA=%.4f dir=%+d  zeaB=%.4f dir=%+d)",
                 _motorA.zero_electric_angle, (int)_motorA.sensor_direction,
                 _motorB.zero_electric_angle, (int)_motorB.sensor_direction);
    }

    _driverA->enable();

    // ── Handle calibration failure ────────────────────────────────────────────
    // Running loopFOC() with an invalid zero_electric_angle produces garbage
    // phase voltages.  Disable both drivers until the user clears NVS and reboots
    // with 24 V connected.
    _calibrated = okA && okB;
    if (!_calibrated) {
        _driverA->disable();
        _driverB->disable();
        _enabled = false;
        logWrite(LOG_ERROR,
                 "FOC init failed — motors disabled. "
                 "Connect 24V power and send CLEAR_CAL via app to re-align.");
    }

    logWrite(LOG_INFO, "begin() complete — calibrated=%s", _calibrated ? "YES" : "NO");
}

// ── Runtime ─────────────────────────────────────────────────────────────────────────────

void DifferentialTurret::applyPIDConfig() {
    _motorA.P_angle.P                = _config.angle_P;
    _motorA.PID_velocity.P           = _config.velocity_P;
    _motorA.PID_velocity.I           = _config.velocity_I;
    _motorA.PID_velocity.D           = _config.velocity_D;
    _motorA.PID_velocity.output_ramp = _config.velocity_ramp;
    _motorA.LPF_velocity.Tf          = _config.velocity_lpf;

    _motorB.P_angle.P                = _config.angle_P;
    _motorB.PID_velocity.P           = _config.velocity_P;
    _motorB.PID_velocity.I           = _config.velocity_I;
    _motorB.PID_velocity.D           = _config.velocity_D;
    _motorB.PID_velocity.output_ramp = _config.velocity_ramp;
    _motorB.LPF_velocity.Tf          = _config.velocity_lpf;
}

void DifferentialTurret::update() {
    if (!_calibrated) return;

    _motorA.loopFOC();
    _motorB.loopFOC();
    mixAndApply();
    _motorA.move();
    _motorB.move();

    unsigned long now = micros();
    if (_lastUpdateUs != 0) {
        float dt = (now - _lastUpdateUs) * 1e-6f;
        if (dt > 0.0f) {
            _motorA_acc = (_motorA.shaft_velocity - _prevMotorA_vel) / dt;
            _motorB_acc = (_motorB.shaft_velocity - _prevMotorB_vel) / dt;
        }
    }
    _prevMotorA_vel = _motorA.shaft_velocity;
    _prevMotorB_vel = _motorB.shaft_velocity;
    _lastUpdateUs   = now;
}

void DifferentialTurret::setMode(const TurretMode mode) {
    _mode = mode;

    MotionControlType ct = velocity_openloop;
    switch (mode) {
        case TurretMode::VELOCITY:             ct = velocity_openloop; break;
        case TurretMode::POSITION:             ct = angle_openloop;    break;
        case TurretMode::CLOSED_LOOP_POSITION: ct = angle; applyPIDConfig(); break;
    }

    _motorA.controller = ct;
    _motorB.controller = ct;
}

TurretMode DifferentialTurret::getMode() const {
    return _mode;
}

void DifferentialTurret::setTarget(const float heading, const float elevation) {
    _heading_target   = heading;
    _elevation_target = elevation;
}

void DifferentialTurret::mixAndApply() {
    const float hdg_motor = _heading_target * _config.gear_ratio_heading;
    const float elv_motor = _elevation_target * _config.gear_ratio_elevation;
    _motorA.target = hdg_motor + elv_motor;
    _motorB.target = hdg_motor - elv_motor;
}

float DifferentialTurret::getHeading() const {
    return (_motorA.shaft_angle + _motorB.shaft_angle) / 2.0f / _config.gear_ratio_heading;
}

float DifferentialTurret::getElevation() const {
    return (_motorA.shaft_angle - _motorB.shaft_angle) / 2.0f / _config.gear_ratio_elevation;
}

void DifferentialTurret::enable() {
    if (!_calibrated) return;
    _driverA->enable();
    _driverB->enable();
    _enabled = true;
}

void DifferentialTurret::disable() {
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
