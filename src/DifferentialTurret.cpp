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
    // ── Link and configure ─────────────────────────────────────────────────────
    logWrite(LOG_INFO,
             "Motor%c init start: alignV=%.2f voltL=%.2f velL=%.2f enPin=%d "
             "driver_vps=%.1f driver_vlim=%.1f",
             label, alignVoltage, voltageLimit, _config.velocity_limit,
             enPin, driver->voltage_power_supply, driver->voltage_limit);

    motor.linkDriver(driver);
    motor.linkSensor(sensor);
    motor.voltage_limit        = voltageLimit;
    motor.velocity_limit       = _config.velocity_limit;
    motor.voltage_sensor_align = alignVoltage;
    motor.controller           = angle;

    int ok = motor.init();
    logWrite(ok ? LOG_INFO : LOG_ERROR,
             "Motor%c motor.init(): %s  enable_active_high=%d  GPIO%d=%s  motor.enabled=%d",
             label, ok ? "OK" : "FAILED",
             (int)driver->enable_active_high, enPin,
             digitalRead(enPin) ? "HI" : "LO",
             (int)motor.enabled);

    motor.enable();
    logWrite(LOG_INFO,
             "Motor%c after motor.enable(): GPIO%d=%s  enable_active_high=%d  motor.enabled=%d",
             label, enPin, digitalRead(enPin) ? "HI" : "LO",
             (int)driver->enable_active_high, (int)motor.enabled);

    // ── Self-test: verify the gate driver is sourcing current ──────────────────
    // NOT-MOVED most commonly means:
    //   1. 24V motor supply not connected (most likely)
    //   2. Enable pin active-LOW on this hardware while SimpleFOC defaults HIGH
    //   3. Broken wiring on phase or enable line
    // The code auto-detects active-LOW by flipping and retrying once.
    // IMPORTANT: if the flip fails, enable_active_high is restored AND driver->enable()
    // is called to drive the pin back HIGH — without that second call the GPIO
    // stays LOW and initFOC() runs with the driver disabled.
    static constexpr float NOT_MOVED_THRESHOLD = 0.005f;  // rad (~0.29 deg)
    {
        motor.voltage_limit = alignVoltage;

        // First reading
        sensor->update();
        float ang0 = sensor->getAngle();
        logWrite(LOG_INFO,
                 "Motor%c self-test: sensor_before=%.4f rad (%.1f deg)  "
                 "will apply %.2fV for 800ms  GPIO%d=%s  enable_active_high=%d",
                 label, ang0, ang0 * 57.2958f, alignVoltage,
                 enPin, digitalRead(enPin) ? "HI" : "LO",
                 (int)driver->enable_active_high);

        motor.setPhaseVoltage(alignVoltage, 0, _3PI_2);
        delay(800);
        sensor->update();
        float ang1    = sensor->getAngle();
        float delta   = fabsf(ang1 - ang0);
        motor.setPhaseVoltage(0, 0, 0);

        logWrite(delta > NOT_MOVED_THRESHOLD ? LOG_INFO : LOG_WARN,
                 "Motor%c self-test: sensor_after=%.4f rad (%.1f deg)  "
                 "delta=%.4f rad (%.1f deg)  threshold=%.4f  "
                 "GPIO%d=%s  enable_active_high=%d  [%s]",
                 label, ang1, ang1 * 57.2958f,
                 delta, delta * 57.2958f, NOT_MOVED_THRESHOLD,
                 enPin, digitalRead(enPin) ? "HI" : "LO",
                 (int)driver->enable_active_high,
                 delta > NOT_MOVED_THRESHOLD ? "MOVED" : "NOT-MOVED");

        if (delta <= NOT_MOVED_THRESHOLD) {
            // Try flipping enable polarity — some gate drivers are active-LOW
            logWrite(LOG_WARN,
                     "Motor%c: NOT-MOVED (delta=%.4f <= %.4f) — flipping enable_active_high "
                     "to LOW.  GPIO%d currently=%s",
                     label, delta, NOT_MOVED_THRESHOLD,
                     enPin, digitalRead(enPin) ? "HI" : "LO");

            driver->enable_active_high = LOW;
            driver->enable();
            logWrite(LOG_INFO,
                     "Motor%c after active-LOW flip+enable(): GPIO%d=%s  enable_active_high=%d",
                     label, enPin, digitalRead(enPin) ? "HI" : "LO",
                     (int)driver->enable_active_high);

            sensor->update();
            float ang2 = sensor->getAngle();
            logWrite(LOG_INFO,
                     "Motor%c active-LOW retry: sensor_before=%.4f rad (%.1f deg)  "
                     "applying %.2fV for 800ms",
                     label, ang2, ang2 * 57.2958f, alignVoltage);

            motor.setPhaseVoltage(alignVoltage, 0, _3PI_2);
            delay(800);
            sensor->update();
            float ang3   = sensor->getAngle();
            float delta2 = fabsf(ang3 - ang2);
            motor.setPhaseVoltage(0, 0, 0);

            logWrite(delta2 > NOT_MOVED_THRESHOLD ? LOG_INFO : LOG_ERROR,
                     "Motor%c active-LOW retry: sensor_after=%.4f rad (%.1f deg)  "
                     "delta=%.4f rad (%.1f deg)  GPIO%d=%s  [%s]",
                     label, ang3, ang3 * 57.2958f,
                     delta2, delta2 * 57.2958f,
                     enPin, digitalRead(enPin) ? "HI" : "LO",
                     delta2 > NOT_MOVED_THRESHOLD
                         ? "MOVED — active-LOW gate driver confirmed"
                         : "STILL NOT MOVING — check wiring and 24V supply");

            if (delta2 <= NOT_MOVED_THRESHOLD) {
                // Neither polarity moved the motor.  Restore active-HIGH and
                // re-enable so the GPIO is HIGH going into initFOC().  Without
                // calling enable() here the pin stays LOW (left by the retry)
                // and initFOC() runs with the driver disabled — guaranteed fail.
                logWrite(LOG_WARN,
                         "Motor%c: restoring active-HIGH and re-enabling before initFOC.  "
                         "GPIO%d before restore=%s",
                         label, enPin, digitalRead(enPin) ? "HI" : "LO");
                driver->enable_active_high = HIGH;
                driver->enable();  // drives pin HIGH — critical, do not remove
                logWrite(LOG_INFO,
                         "Motor%c after polarity restore+enable(): GPIO%d=%s  enable_active_high=%d",
                         label, enPin, digitalRead(enPin) ? "HI" : "LO",
                         (int)driver->enable_active_high);
            }
        }

        motor.voltage_limit = voltageLimit;
    }

    // ── Pre-initFOC sensor snapshot ────────────────────────────────────────────
    sensor->update();
    float sensorAngle = sensor->getAngle();
    logWrite(LOG_INFO,
             "Motor%c pre-initFOC: sensor=%.4f rad (%.1f deg)  "
             "enable_active_high=%d  GPIO%d=%s  motor.enabled=%d",
             label, sensorAngle, sensorAngle * 57.2958f,
             (int)driver->enable_active_high, enPin,
             digitalRead(enPin) ? "HI" : "LO",
             (int)motor.enabled);

    // Pre-set zero_electric_angle so initFOC() skips the physical alignment
    // sweep when a valid NVS calibration was loaded.
    if (useStored) {
        logWrite(LOG_INFO,
                 "Motor%c: loading stored cal — zea=%.4f rad  dir=%+d  "
                 "(physical alignment sweep will be skipped)",
                 label, storedZea, (int)storedDir);
        motor.zero_electric_angle = storedZea;
        motor.sensor_direction    = (Direction)storedDir;
    } else {
        logWrite(LOG_INFO,
                 "Motor%c: no stored cal — live alignment sweep starting  "
                 "(needs 24V on motor supply rail and driver enabled)",
                 label);
    }

    // alignSensor() clamps Uq to voltage_limit; raise it for initFOC and restore.
    motor.voltage_limit = alignVoltage;
    logWrite(LOG_INFO,
             "Motor%c initFOC start: voltage_limit=%.2f  sensor_align_voltage=%.2f  "
             "GPIO%d=%s  enable_active_high=%d",
             label, motor.voltage_limit, motor.voltage_sensor_align,
             enPin, digitalRead(enPin) ? "HI" : "LO",
             (int)driver->enable_active_high);

    bool result = (bool)motor.initFOC();
    motor.voltage_limit = voltageLimit;

    logWrite(result ? LOG_INFO : LOG_ERROR,
             "Motor%c initFOC: %s  zero_elec_angle=%.4f rad  "
             "shaft_angle=%.4f rad (%.1f deg)  sensor_dir=%+d",
             label, result ? "OK" : "FAILED",
             motor.zero_electric_angle,
             motor.shaft_angle, motor.shaft_angle * 57.2958f,
             (int)motor.sensor_direction);

    if (!result) {
        logWrite(LOG_ERROR,
                 "Motor%c initFOC FAILED — most common causes:\n"
                 "  1. 24V not connected to motor driver power rail\n"
                 "  2. zero_elec_angle=%.4f (NOT_SET sentinel = -12345 means no alignment ran)\n"
                 "  3. Motor mechanically blocked or phase wiring open",
                 label, motor.zero_electric_angle);
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
    logWrite(drvOkA ? LOG_INFO : LOG_ERROR,
             "DriverA init %s (pwm=%d,%d,%d en=%d) vps=%.1f GPIO%d=%s",
             drvOkA ? "OK" : "FAILED",
             pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA,
             _driverA->voltage_power_supply,
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");

    _driverB->voltage_power_supply = config.voltage_power_supply;
    _driverB->voltage_limit        = config.voltage_power_supply;
    int drvOkB = _driverB->init();
    logWrite(drvOkB ? LOG_INFO : LOG_ERROR,
             "DriverB init %s (pwm=%d,%d,%d en=%d) vps=%.1f GPIO%d=%s",
             drvOkB ? "OK" : "FAILED",
             pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB,
             _driverB->voltage_power_supply,
             pins.enB, digitalRead(pins.enB) ? "HI" : "LO");

    // ── Sensors via TCA9548A mux ────────────────────────────────────────────────
    Wire.begin(pins.sda, pins.scl);
    _mux = TCA9548A(pins.muxAddr);
    _mux.begin(Wire);
    logWrite(LOG_INFO, "I2C mux TCA9548A at 0x%02x — sda=%d scl=%d chanA=%d chanB=%d",
             pins.muxAddr, pins.sda, pins.scl, pins.chanA, pins.chanB);

    if (!_sensorA) _sensorA = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanA);
    if (!_sensorB) _sensorB = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanB);
    _sensorA->init(&Wire);
    _sensorB->init(&Wire);

    float rawA = _sensorA->getSensorAngle();
    float rawB = _sensorB->getSensorAngle();
    logWrite(rawA == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorA raw angle: %.4f rad (%.1f deg)%s",
             rawA, rawA * 57.2958f, rawA == 0.0f ? "  [WARN: reads zero — check mux/wiring]" : "");
    logWrite(rawB == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorB raw angle: %.4f rad (%.1f deg)%s",
             rawB, rawB * 57.2958f, rawB == 0.0f ? "  [WARN: reads zero — check mux/wiring]" : "");

    // ── Motor A ───────────────────────────────────────────────────────────────────
    logWrite(LOG_INFO, "--- Motor A init sequence start ---");
    bool okA = initOneMotor(_motorA, _driverA, _sensorA,
                            cal.zea_A, cal.dir_A, cal.valid,
                            config.sensor_align_voltage, config.voltage_limit,
                            pins.enA, 'A');

    // Disable Motor A's driver before Motor B alignment — its cogging otherwise
    // resists Motor B through the coupled differential mechanism.
    logWrite(LOG_INFO,
             "Disabling DriverA before MotorB alignment (prevents coupling resistance).  "
             "GPIO%d before disable=%s",
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");
    _driverA->disable();
    logWrite(LOG_INFO, "DriverA disabled: GPIO%d=%s",
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");

    // ── Motor B ───────────────────────────────────────────────────────────────────
    logWrite(LOG_INFO, "--- Motor B init sequence start ---");
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

    logWrite(LOG_INFO,
             "Re-enabling DriverA after MotorB alignment.  GPIO%d before enable=%s",
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");
    _driverA->enable();
    logWrite(LOG_INFO, "DriverA re-enabled: GPIO%d=%s",
             pins.enA, digitalRead(pins.enA) ? "HI" : "LO");

    // ── Handle calibration failure ────────────────────────────────────────────
    _calibrated = okA && okB;
    if (!_calibrated) {
        _driverA->disable();
        _driverB->disable();
        _enabled = false;
        logWrite(LOG_ERROR,
                 "FOC init failed (okA=%d okB=%d) — motors disabled.  "
                 "Connect 24V power and send CLEAR_CAL via app to re-align.",
                 (int)okA, (int)okB);
        logWrite(LOG_ERROR,
                 "After disable: GPIO%d(A)=%s  GPIO%d(B)=%s",
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
        // Emit a periodic reminder so the app log stays active and shows the
        // system is alive but waiting for calibration.
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
