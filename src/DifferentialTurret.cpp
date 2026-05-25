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

// ── Per-motor init ────────────────────────────────────────────────────────────────

bool DifferentialTurret::initOneMotor(
    BLDCMotor& motor, BLDCDriver3PWM* driver, MuxedMagneticSensorI2C* sensor,
    float storedZea, int8_t storedDir, bool useStored,
    float alignVoltage, float voltageLimit, uint8_t enPin, char label)
{
    logWrite(LOG_INFO,
             "Motor%c init: alignV=%.1fV voltL=%.1fV enPin=%d "
             "driver_vps=%.1fV driver_vlim=%.1fV GPIO%d=%s",
             label, alignVoltage, voltageLimit, enPin,
             driver->voltage_power_supply, driver->voltage_limit,
             enPin, digitalRead(enPin) ? "HI" : "LO");

    motor.linkDriver(driver);
    motor.linkSensor(sensor);
    motor.voltage_limit        = voltageLimit;
    motor.velocity_limit       = _config.velocity_limit;
    motor.voltage_sensor_align = alignVoltage;
    motor.controller           = angle;

    int ok = motor.init();
    motor.enable();
    logWrite(ok ? LOG_INFO : LOG_ERROR,
             "Motor%c motor.init()=%s  after enable(): GPIO%d=%s  enable_active_high=%d  motor.enabled=%d",
             label, ok ? "OK" : "FAIL",
             enPin, digitalRead(enPin) ? "HI" : "LO",
             (int)driver->enable_active_high, (int)motor.enabled);

    // ── Sensor snapshot before initFOC ────────────────────────────────────
    // getSensorAngle() = raw [0, 2π] reading (no accumulated history)
    // getAngle()       = accumulated angle tracking full rotations
    // Both are logged so we can confirm the sensor is updating during initFOC.
    sensor->update();
    float rawBefore = sensor->getSensorAngle();
    float angBefore = sensor->getAngle();
    logWrite(LOG_INFO,
             "Motor%c pre-initFOC sensor: raw=%.4f rad (%.1f deg)  accum=%.4f  GPIO%d=%s",
             label, rawBefore, rawBefore * 57.2958f, angBefore,
             enPin, digitalRead(enPin) ? "HI" : "LO");

    // Pre-set zero_electric_angle so initFOC() skips the physical alignment
    // sweep when a valid NVS calibration was loaded.
    if (useStored) {
        logWrite(LOG_INFO,
                 "Motor%c: loading stored cal zea=%.4f rad  dir=%+d  "
                 "(physical alignment sweep skipped)",
                 label, storedZea, (int)storedDir);
        motor.zero_electric_angle = storedZea;
        motor.sensor_direction    = (Direction)storedDir;
    } else {
        logWrite(LOG_INFO,
                 "Motor%c: no stored cal — live alignment starting.  "
                 "sensor_align_voltage=%.1fV  GPIO%d=%s (must be HI to enable driver)",
                 label, motor.voltage_sensor_align,
                 enPin, digitalRead(enPin) ? "HI" : "LO");
    }

    // alignSensor() clamps Uq to voltage_limit; raise it to match
    // sensor_align_voltage so the full alignment voltage reaches the phases.
    motor.voltage_limit = alignVoltage;
    logWrite(LOG_INFO,
             "Motor%c initFOC start: voltage_limit=%.1fV  sensor_align_voltage=%.1fV  "
             "GPIO%d=%s",
             label, motor.voltage_limit, motor.voltage_sensor_align,
             enPin, digitalRead(enPin) ? "HI" : "LO");

    bool result = (bool)motor.initFOC();
    motor.voltage_limit = voltageLimit;

    // ── Sensor snapshot after initFOC ────────────────────────────────────
    sensor->update();
    float rawAfter  = sensor->getSensorAngle();
    float angAfter  = sensor->getAngle();
    float rawDelta  = rawAfter - rawBefore;
    // Handle wrap-around so delta is always the shortest arc
    while (rawDelta >  M_PI) rawDelta -= 2.0f * M_PI;
    while (rawDelta < -M_PI) rawDelta += 2.0f * M_PI;

    logWrite(result ? LOG_INFO : LOG_ERROR,
             "Motor%c initFOC: %s  zea=%.4f rad  shaft=%.4f rad (%.1f deg)  sensor_dir=%+d",
             label, result ? "OK" : "FAIL",
             motor.zero_electric_angle,
             motor.shaft_angle, motor.shaft_angle * 57.2958f,
             (int)motor.sensor_direction);
    logWrite(LOG_INFO,
             "Motor%c post-initFOC sensor: raw=%.4f rad (%.1f deg)  rawDelta=%.4f rad (%.1f deg)  "
             "accum=%.4f  motor.shaft=%.4f",
             label, rawAfter, rawAfter * 57.2958f,
             rawDelta, rawDelta * 57.2958f,
             angAfter, motor.shaft_angle);

    if (!result) {
        if (fabsf(rawDelta) > 0.05f) {
            logWrite(LOG_ERROR,
                     "Motor%c FAIL: sensor DID track (rawDelta=%.4f rad) but movement "
                     "< SimpleFOC MIN_ANGLE_DETECT_MOVEMENT threshold.  "
                     "Increase sensor_align_voltage (currently %.1fV) for more torque.",
                     label, rawDelta, alignVoltage);
        } else if (fabsf(rawDelta) < 0.002f) {
            logWrite(LOG_ERROR,
                     "Motor%c FAIL: sensor NOT tracking (rawDelta=%.4f rad ~0).  "
                     "Motor did not rotate at all.  "
                     "Check 24V on motor driver rail and phase wiring.",
                     label, rawDelta);
        } else {
            logWrite(LOG_ERROR,
                     "Motor%c FAIL: sensor delta=%.4f rad (ambiguous).  "
                     "zea=%s  Check wiring and 24V supply.",
                     label, rawDelta,
                     motor.zero_electric_angle < -12344.0f
                         ? "NOT_SET (alignment never started)" : "set");
        }
    }

    return result;
}

// ── begin() ─────────────────────────────────────────────────────────────────────────────

void DifferentialTurret::begin(const TurretPins& pins, const TurretConfig& config) {
    SimpleFOCDebug::enable(&Serial);

    _config = config;
    _motorA = BLDCMotor(config.pole_pairs, config.phase_resistance);
    _motorB = BLDCMotor(config.pole_pairs, config.phase_resistance);

    logWrite(LOG_INFO,
             "begin(): vps=%.1fV vlim=%.1fV align=%.1fV poles=%d Rphase=%.2f",
             config.voltage_power_supply, config.voltage_limit,
             config.sensor_align_voltage, config.pole_pairs, config.phase_resistance);

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
    logWrite(drvOkA ? LOG_INFO : LOG_ERROR,
             "DriverA init %s (pwm=%d,%d,%d en=%d) vps=%.1fV GPIO%d=%s",
             drvOkA ? "OK" : "FAIL",
             pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA,
             _driverA->voltage_power_supply,
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");

    _driverB->voltage_power_supply = config.voltage_power_supply;
    _driverB->voltage_limit        = config.voltage_power_supply;
    int drvOkB = _driverB->init();
    logWrite(drvOkB ? LOG_INFO : LOG_ERROR,
             "DriverB init %s (pwm=%d,%d,%d en=%d) vps=%.1fV GPIO%d=%s",
             drvOkB ? "OK" : "FAIL",
             pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB,
             _driverB->voltage_power_supply,
             pins.enB, digitalRead(pins.enB) ? "HI" : "LO");

    // ── Sensors via TCA9548A mux ────────────────────────────────────────────────
    Wire.begin(pins.sda, pins.scl);
    _mux = TCA9548A(pins.muxAddr);
    _mux.begin(Wire);
    logWrite(LOG_INFO,
             "I2C mux TCA9548A at 0x%02x — sda=%d scl=%d chanA=%d chanB=%d",
             pins.muxAddr, pins.sda, pins.scl, pins.chanA, pins.chanB);

    if (!_sensorA) _sensorA = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanA);
    if (!_sensorB) _sensorB = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanB);
    _sensorA->init(&Wire);
    _sensorB->init(&Wire);

    float rawA = _sensorA->getSensorAngle();
    float rawB = _sensorB->getSensorAngle();
    logWrite(rawA == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorA raw angle: %.4f rad (%.1f deg)%s",
             rawA, rawA * 57.2958f,
             rawA == 0.0f ? "  [WARN: reads zero — check mux/wiring]" : "");
    logWrite(rawB == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorB raw angle: %.4f rad (%.1f deg)%s",
             rawB, rawB * 57.2958f,
             rawB == 0.0f ? "  [WARN: reads zero — check mux/wiring]" : "");

    // ── Motor A ───────────────────────────────────────────────────────────────────
    // Disable Driver B before Motor A's alignment so it cannot resist Motor A
    // through the coupled differential mechanism.
    logWrite(LOG_INFO,
             "--- Motor A init --- Disabling DriverB first to reduce coupling.  "
             "GPIO%d=%s",
             pins.enB, digitalRead(pins.enB) ? "HI" : "LO");
    _driverB->disable();
    logWrite(LOG_INFO, "DriverB disabled: GPIO%d=%s",
             pins.enB, digitalRead(pins.enB) ? "HI" : "LO");

    bool okA = initOneMotor(_motorA, _driverA, _sensorA,
                            cal.zea_A, cal.dir_A, cal.valid,
                            config.sensor_align_voltage, config.voltage_limit,
                            pins.enA, 'A');

    // ── Motor B ───────────────────────────────────────────────────────────────────
    // Disable Driver A before Motor B's alignment for the same reason.
    logWrite(LOG_INFO,
             "--- Motor B init --- Disabling DriverA to reduce coupling.  "
             "GPIO%d=%s",
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");
    _driverA->disable();
    logWrite(LOG_INFO, "DriverA disabled: GPIO%d=%s",
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");

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

    // Re-enable both drivers.
    logWrite(LOG_INFO, "Re-enabling both drivers: GPIO%d GPIO%d", pins.enA, pins.enB);
    _driverA->enable();
    _driverB->enable();
    logWrite(LOG_INFO, "Drivers re-enabled: GPIO%d=%s  GPIO%d=%s",
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO",
             pins.enB, digitalRead(pins.enB) ? "HI" : "LO");

    // ── Handle calibration failure ────────────────────────────────────────────
    _calibrated = okA && okB;
    if (!_calibrated) {
        _driverA->disable();
        _driverB->disable();
        _enabled = false;
        logWrite(LOG_ERROR,
                 "FOC init failed (okA=%d okB=%d) — drivers disabled.  "
                 "If sensor rawDelta > 0 above: increase sensor_align_voltage (currently %.1fV).  "
                 "If rawDelta ~ 0: check 24V motor supply and phase wiring.",
                 (int)okA, (int)okB, config.sensor_align_voltage);
        logWrite(LOG_ERROR, "After disable: GPIO%d(A)=%s  GPIO%d(B)=%s",
                 pins.enA, digitalRead(pins.enA) ? "HI" : "LO",
                 pins.enB, digitalRead(pins.enB) ? "HI" : "LO");
    } else {
        logWrite(LOG_INFO,
                 "Both motors calibrated OK — drivers active.  "
                 "GPIO%d(A)=%s  GPIO%d(B)=%s",
                 pins.enA, digitalRead(pins.enA) ? "HI" : "LO",
                 pins.enB, digitalRead(pins.enB) ? "HI" : "LO");
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
    if (!_calibrated) {
        static uint32_t skipCount = 0;
        if (skipCount % 500 == 0) {
            logWrite(LOG_WARN,
                     "update() skipped — not calibrated (call #%lu).  "
                     "Send CLEAR_CAL and reboot with 24V to re-align.",
                     (unsigned long)skipCount);
        }
        ++skipCount;
        return;
    }

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
