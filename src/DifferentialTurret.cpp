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

// ── AS5600 health diagnostic ─────────────────────────────────────────────────
//
// AS5600 STATUS register (0x0B):
//   bit 5 — MH: AGC at minimum → field too strong (magnet too close)
//   bit 4 — ML: AGC at maximum → field too weak   (magnet too far)
//   bit 3 — MD: magnet detected
//
// AGC (0x1A):  0 = saturated (too close)  255 = no signal (too far)
//                  typical good range ≈ 80–180
//
static void logAS5600Health(TCA9548A& mux, uint8_t channel, char label) {
    mux.selectChannel(channel);

    Wire.beginTransmission(0x36);
    Wire.write(0x0B);
    uint8_t txErr = Wire.endTransmission(false);
    if (txErr != 0) {
        logWrite(LOG_ERROR,
                 "Sensor%c AS5600 ch%d: no I2C ACK (err=%d) — "
                 "check mux wiring, mux address (0x70?), and sensor power",
                 label, channel, (int)txErr);
        return;
    }
    Wire.requestFrom((uint8_t)0x36, (uint8_t)1);
    uint8_t status = Wire.available() ? Wire.read() : 0x00;

    Wire.beginTransmission(0x36);
    Wire.write(0x1A);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x36, (uint8_t)1);
    uint8_t agc = Wire.available() ? Wire.read() : 0xFF;

    Wire.beginTransmission(0x36);
    Wire.write(0x1B);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x36, (uint8_t)2);
    uint16_t magnitude = 0;
    if (Wire.available()) magnitude  = (uint16_t)Wire.read() << 8;
    if (Wire.available()) magnitude |= Wire.read();
    magnitude &= 0x0FFF;

    bool md = (status >> 3) & 1;
    bool ml = (status >> 4) & 1;
    bool mh = (status >> 5) & 1;

    uint8_t     lvl;
    const char* hint;
    if (!md) {
        lvl  = LOG_ERROR;
        hint = "NO MAGNET DETECTED — sensor will not work";
    } else if (mh) {
        lvl  = LOG_WARN;
        hint = "MAGNET TOO STRONG — move magnet farther from chip";
    } else if (ml) {
        lvl  = LOG_WARN;
        hint = "MAGNET TOO WEAK — move magnet closer to chip";
    } else {
        lvl  = LOG_INFO;
        hint = "magnet OK";
    }

    logWrite(lvl,
             "Sensor%c AS5600: status=0x%02x  md=%d mh=%d ml=%d  "
             "agc=%d/255  magnitude=%d/4095  [%s]",
             label, status,
             (int)md, (int)mh, (int)ml,
             (int)agc, (int)magnitude,
             hint);
}

// ── Step 1: motor.init() + enable ────────────────────────────────────────────
// Call for BOTH motors before calling alignMotor() on either, so the idle motor
// can be held at a static phase voltage during the other's alignment sweep.

bool DifferentialTurret::setupMotor(
    BLDCMotor& motor, BLDCDriver3PWM* driver, MuxedMagneticSensorI2C* sensor,
    float voltageLimit, uint8_t enPin, char label)
{
    motor.linkDriver(driver);
    motor.linkSensor(sensor);
    motor.voltage_limit  = voltageLimit;
    motor.velocity_limit = _config.velocity_limit;
    motor.controller     = angle;
    // Do NOT set voltage_sensor_align here — motor.init() resets it to voltage_limit

    int ok = motor.init();
    motor.enable();
    logWrite(ok ? LOG_INFO : LOG_ERROR,
             "Motor%c setup: init=%s  GPIO%d=%s  enable_active_high=%d",
             label, ok ? "OK" : "FAIL",
             enPin, digitalRead(enPin) ? "HI" : "LO",
             (int)driver->enable_active_high);
    return ok != 0;
}

// ── Step 2: FOC alignment ─────────────────────────────────────────────────────
// Caller must apply a static holding voltage to the OTHER motor via
// setPhaseVoltage() before calling this, then release it after.

bool DifferentialTurret::alignMotor(
    BLDCMotor& motor, MuxedMagneticSensorI2C* sensor,
    float storedZea, int8_t storedDir, bool useStored,
    float alignVoltage, float voltageLimit, uint8_t enPin, char label)
{
    sensor->update();
    float rawBefore = sensor->getSensorAngle();
    logWrite(LOG_INFO,
             "Motor%c pre-align sensor: raw=%.4f rad (%.1f deg)  GPIO%d=%s",
             label, rawBefore, rawBefore * 57.2958f,
             enPin, digitalRead(enPin) ? "HI" : "LO");

    if (useStored) {
        logWrite(LOG_INFO,
                 "Motor%c: loading stored cal zea=%.4f rad  dir=%+d  "
                 "(physical alignment sweep skipped)",
                 label, storedZea, (int)storedDir);
        motor.zero_electric_angle = storedZea;
        motor.sensor_direction    = (Direction)storedDir;
    }

    // MUST set voltage_sensor_align AFTER motor.init() (called in setupMotor) —
    // init() resets it to voltage_limit internally.
    // Raise voltage_limit too so setPhaseVoltage() inside alignSensor() isn't clamped.
    motor.voltage_sensor_align = alignVoltage;
    motor.voltage_limit        = alignVoltage;
    logWrite(LOG_INFO,
             "Motor%c calling initFOC: voltage_limit=%.1fV  sensor_align_voltage=%.1fV  "
             "GPIO%d=%s  useStored=%d",
             label, motor.voltage_limit, motor.voltage_sensor_align,
             enPin, digitalRead(enPin) ? "HI" : "LO", (int)useStored);

    bool result = (bool)motor.initFOC();
    motor.voltage_limit = voltageLimit;

    sensor->update();
    float rawAfter = sensor->getSensorAngle();
    float rawDelta = rawAfter - rawBefore;
    while (rawDelta >  (float)M_PI) rawDelta -= 2.0f * (float)M_PI;
    while (rawDelta < -(float)M_PI) rawDelta += 2.0f * (float)M_PI;

    logWrite(result ? LOG_INFO : LOG_ERROR,
             "Motor%c initFOC: %s  zea=%.4f rad  shaft=%.4f rad (%.1f deg)  sensor_dir=%+d",
             label, result ? "OK" : "FAIL",
             motor.zero_electric_angle,
             motor.shaft_angle, motor.shaft_angle * 57.2958f,
             (int)motor.sensor_direction);
    logWrite(LOG_INFO,
             "Motor%c post-align sensor: raw=%.4f  rawDelta=%.4f rad (%.1f deg)",
             label, rawAfter, rawDelta, rawDelta * 57.2958f);

    if (!result) {
        if (fabsf(rawDelta) > 0.05f) {
            logWrite(LOG_ERROR,
                     "Motor%c FAIL: sensor tracked (rawDelta=%.3f rad = %.1f deg) but < "
                     "SimpleFOC MIN_ANGLE_DETECT_MOVEMENT.  "
                     "Increase sensor_align_voltage (currently %.1fV).",
                     label, rawDelta, rawDelta * 57.2958f, alignVoltage);
        } else if (fabsf(rawDelta) < 0.005f) {
            logWrite(LOG_ERROR,
                     "Motor%c FAIL: sensor did not move at all (rawDelta=%.4f rad).  "
                     "Check 24V on motor driver power rail.  GPIO%d=%s.",
                     label, rawDelta,
                     enPin, digitalRead(enPin) ? "HI" : "LO");
        } else {
            logWrite(LOG_ERROR,
                     "Motor%c FAIL: tiny movement (rawDelta=%.4f rad).  "
                     "Increase sensor_align_voltage (currently %.1fV) or check for "
                     "mechanical binding.",
                     label, rawDelta, alignVoltage);
        }
    }

    return result;
}

// ── begin() ─────────────────────────────────────────────────────────────────

void DifferentialTurret::begin(const TurretPins& pins, const TurretConfig& config) {
    SimpleFOCDebug::enable(&Serial);

    _config = config;
    _motorA = BLDCMotor(config.pole_pairs, config.phase_resistance);
    _motorB = BLDCMotor(config.pole_pairs, config.phase_resistance);

    logWrite(LOG_INFO, "begin(): vps=%.1fV vlim=%.1fV align=%.1fV poles=%d Rphase=%.2f",
             config.voltage_power_supply, config.voltage_limit,
             config.sensor_align_voltage, config.pole_pairs, config.phase_resistance);

    MotorCalibration cal = CalibrationStore::load();
    if (cal.valid) {
        logWrite(LOG_INFO,
                 "NVS cal loaded: zeaA=%.4f dir=%+d  zeaB=%.4f dir=%+d",
                 cal.zea_A, (int)cal.dir_A, cal.zea_B, (int)cal.dir_B);
    } else {
        logWrite(LOG_WARN, "No NVS cal — live FOC alignment will run (requires 24V motor power)");
    }

    // ── Drivers ───────────────────────────────────────────────────────────────
    _driverA = new BLDCDriver3PWM(pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    _driverB = new BLDCDriver3PWM(pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

    _driverA->voltage_power_supply = config.voltage_power_supply;
    _driverA->voltage_limit        = config.voltage_power_supply;
    int drvOkA = _driverA->init();
    logWrite(drvOkA ? LOG_INFO : LOG_ERROR, "DriverA init %s (pwm=%d,%d,%d en=%d) vps=%.1fV",
             drvOkA ? "OK" : "FAILED",
             pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA,
             config.voltage_power_supply);

    _driverB->voltage_power_supply = config.voltage_power_supply;
    _driverB->voltage_limit        = config.voltage_power_supply;
    int drvOkB = _driverB->init();
    logWrite(drvOkB ? LOG_INFO : LOG_ERROR, "DriverB init %s (pwm=%d,%d,%d en=%d) vps=%.1fV",
             drvOkB ? "OK" : "FAILED",
             pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB,
             config.voltage_power_supply);

    // ── Sensors via TCA9548A mux ──────────────────────────────────────────────
    Wire.begin(pins.sda, pins.scl);
    _mux = TCA9548A(pins.muxAddr);
    _mux.begin(Wire);
    logWrite(LOG_INFO, "I2C mux TCA9548A at 0x%02x — sda=%d scl=%d chanA=%d chanB=%d",
             pins.muxAddr, pins.sda, pins.scl, pins.chanA, pins.chanB);

    if (!_sensorA) _sensorA = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanA);
    if (!_sensorB) _sensorB = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanB);
    _sensorA->init(&Wire);
    _sensorB->init(&Wire);

    logWrite(LOG_INFO, "--- AS5600 magnet health check ---");
    logAS5600Health(_mux, pins.chanA, 'A');
    logAS5600Health(_mux, pins.chanB, 'B');

    float rawA = _sensorA->getSensorAngle();
    float rawB = _sensorB->getSensorAngle();
    logWrite(rawA == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorA raw angle: %.4f rad (%.1f deg)", rawA, rawA * 57.2958f);
    logWrite(rawB == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorB raw angle: %.4f rad (%.1f deg)", rawB, rawB * 57.2958f);

    // ── Setup both motors (init only, no FOC alignment yet) ──────────────────
    // Both must be fully initialised before alignment so either can hold the
    // other at a static phase voltage while its counterpart runs initFOC().
    logWrite(LOG_INFO, "--- Setting up both motors ---");
    setupMotor(_motorA, _driverA, _sensorA, config.voltage_limit, pins.enA, 'A');
    setupMotor(_motorB, _driverB, _sensorB, config.voltage_limit, pins.enB, 'B');

    // ── Align Motor A ─────────────────────────────────────────────────────────
    // Hold Motor B at a fixed electrical angle so the differential coupling
    // cannot absorb Motor A's torque.  Without this, Motor B (the mechanically
    // free end) rotates instead of Motor A, and Motor A's sensor reads delta≈0.
    logWrite(LOG_INFO,
             "--- Motor A FOC align (Motor B held at %.1fV DC to resist coupling) ---",
             config.sensor_align_voltage);
    _motorB.setPhaseVoltage(config.sensor_align_voltage, 0.0f, 0.0f);
    bool okA = alignMotor(_motorA, _sensorA,
                          cal.zea_A, cal.dir_A, cal.valid,
                          config.sensor_align_voltage, config.voltage_limit, pins.enA, 'A');
    _motorB.setPhaseVoltage(0.0f, 0.0f, 0.0f);

    // ── Align Motor B ─────────────────────────────────────────────────────────
    logWrite(LOG_INFO,
             "--- Motor B FOC align (Motor A held at %.1fV DC to resist coupling) ---",
             config.sensor_align_voltage);
    _motorA.setPhaseVoltage(config.sensor_align_voltage, 0.0f, 0.0f);
    bool okB = alignMotor(_motorB, _sensorB,
                          cal.zea_B, cal.dir_B, cal.valid,
                          config.sensor_align_voltage, config.voltage_limit, pins.enB, 'B');
    _motorA.setPhaseVoltage(0.0f, 0.0f, 0.0f);

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

    // ── Handle calibration failure ────────────────────────────────────────────
    _calibrated = okA && okB;
    if (!_calibrated) {
        _driverA->disable();
        _driverB->disable();
        _enabled = false;
        logWrite(LOG_ERROR,
                 "FOC init failed (okA=%d okB=%d) — motors disabled.  "
                 "If rawDelta above was tiny: increase sensor_align_voltage (%.1fV).  "
                 "Connect 24V and send CLEAR_CAL via app to re-align.",
                 (int)okA, (int)okB, config.sensor_align_voltage);
        logWrite(LOG_ERROR, "After disable: GPIO%d(A)=%s  GPIO%d(B)=%s",
                 pins.enA, digitalRead(pins.enA) ? "HI" : "LO",
                 pins.enB, digitalRead(pins.enB) ? "HI" : "LO");
    }

    logWrite(LOG_INFO, "begin() complete — calibrated=%s", _calibrated ? "YES" : "NO");
}

// ── Runtime ──────────────────────────────────────────────────────────────────

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
    unsigned long now = micros();

    if (!_calibrated) {
        // Stream encoder-based velocity/acceleration even without FOC so the
        // app bars show live data while diagnosing calibration issues.
        if (_sensorA) {
            _sensorA->update();
            _motorA.shaft_angle    = _sensorA->getAngle();
            _motorA.shaft_velocity = _sensorA->getVelocity();
        }
        if (_sensorB) {
            _sensorB->update();
            _motorB.shaft_angle    = _sensorB->getAngle();
            _motorB.shaft_velocity = _sensorB->getVelocity();
        }

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

        _uncalLoopCount++;
        if (_uncalLoopCount >= 500) {
            logWrite(LOG_WARN,
                     "FOC disabled (not calibrated) — sensors active, drivers off.  "
                     "Send CLEAR_CAL and reboot with 24V to calibrate.");
            _uncalLoopCount = 0;
        }
        return;
    }

    _motorA.loopFOC();
    _motorB.loopFOC();
    mixAndApply();
    _motorA.move();
    _motorB.move();

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

    if (_mode == TurretMode::CLOSED_LOOP_POSITION) {
        // Proportional velocity synchronization: scale each motor's velocity_limit
        // to its share of the remaining distance so both arrive simultaneously.
        // Without this the heading (gear=6) and elevation (gear=0.82) axes finish
        // at very different times, causing the trajectory to curve.
        float errA = fabsf(_motorA.target - _motorA.shaft_angle);
        float errB = fabsf(_motorB.target - _motorB.shaft_angle);
        float maxErr = fmaxf(errA, errB);

        if (maxErr > 0.05f) {
            // Floor at 5% so the leading motor can still make small corrections
            _motorA.velocity_limit = _config.velocity_limit * fmaxf(0.05f, errA / maxErr);
            _motorB.velocity_limit = _config.velocity_limit * fmaxf(0.05f, errB / maxErr);
        } else {
            // Both near target — restore full speed for final settling
            _motorA.velocity_limit = _config.velocity_limit;
            _motorB.velocity_limit = _config.velocity_limit;
        }
    }
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
