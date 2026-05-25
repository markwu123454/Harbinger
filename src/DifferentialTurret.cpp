#include "DifferentialTurret.h"
#include "SharedData.h"
#include "Calibration.h"
#include "proto.h"
#include <Wire.h>

DifferentialTurret::DifferentialTurret()
    : _motorA(11, 11.1f)
    , _motorB(11, 11.1f)
    , _config() {
    Serial.printf("[TURRET] Constructor called with default motor params (poles=11, resistance=11.1)\n");
}

void DifferentialTurret::clearCalibration() {
    CalibrationStore::clear();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

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

void DifferentialTurret::begin(const TurretPins& pins, const TurretConfig& config) {
    // Route SimpleFOC's own diagnostic prints to Serial so they appear in
    // the serial monitor alongside our own messages.
    SimpleFOCDebug::enable(&Serial);

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

    // ── Try to load NVS calibration ───────────────────────────────────────
    // A valid entry means a previous live alignment succeeded.  We write the
    // stored angles to the motor objects directly so that initFOC() detects
    // zero_electric_angle != NOT_SET and skips the physical alignment sweep,
    // which requires 24 V motor power and physically rotates the shafts.
    MotorCalibration cal = CalibrationStore::load();
    if (cal.valid) {
        logWrite(LOG_INFO,
                 "NVS cal loaded: zeaA=%.4f dir=%+d  zeaB=%.4f dir=%+d",
                 cal.zea_A, (int)cal.dir_A, cal.zea_B, (int)cal.dir_B);
    } else {
        logWrite(LOG_WARN,
                 "No NVS cal — live FOC alignment will run (requires 24V motor power)");
    }

    _driverA = new BLDCDriver3PWM(pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    _driverB = new BLDCDriver3PWM(pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

    _driverA->voltage_power_supply = config.voltage_power_supply;
    _driverA->voltage_limit = config.voltage_power_supply;
    int drvOkA = _driverA->init();
    logWrite(drvOkA ? LOG_INFO : LOG_ERROR,
             "DriverA init %s (pwm=%d,%d,%d en=%d)",
             drvOkA ? "OK" : "FAILED",
             pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);

    _driverB->voltage_power_supply = config.voltage_power_supply;
    _driverB->voltage_limit = config.voltage_power_supply;
    int drvOkB = _driverB->init();
    logWrite(drvOkB ? LOG_INFO : LOG_ERROR,
             "DriverB init %s (pwm=%d,%d,%d en=%d)",
             drvOkB ? "OK" : "FAILED",
             pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

    // Init TCA9548A mux on a single I2C bus; both AS5600s share address 0x36
    Wire.begin(pins.sda, pins.scl);
    _mux = TCA9548A(pins.muxAddr);
    _mux.begin(Wire);
    Serial.printf("[TURRET] TCA9548A mux init - addr=0x%02X, SDA=%d, SCL=%d, chanA=%d, chanB=%d\n",
                  pins.muxAddr, pins.sda, pins.scl, pins.chanA, pins.chanB);

    // MuxedMagneticSensorI2C selects its channel in both update() and
    // getSensorAngle() so the correct mux channel is always active regardless
    // of which code path SimpleFOC uses internally.
    if (!_sensorA) _sensorA = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanA);
    if (!_sensorB) _sensorB = new MuxedMagneticSensorI2C(AS5600_I2C, _mux, pins.chanB);
    _sensorA->init(&Wire);
    _sensorB->init(&Wire);
    Serial.printf("[TURRET] AS5600 sensors initialized via mux\n");
    Serial.printf("[TURRET] initFOC alignment voltage: %.2f V (runtime limit: %.2f V)\n",
                  config.sensor_align_voltage, config.voltage_limit);

    // Log raw sensor angles via direct getSensorAngle() call.
    // If these are 0.0 the AS5600 is not responding (I2C fault, wrong mux
    // channel, or missing/too-distant magnet).
    float rawA = _sensorA->getSensorAngle();
    float rawB = _sensorB->getSensorAngle();
    logWrite(rawA == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorA raw angle after init: %.4f rad (%.1f deg)", rawA, rawA * 57.2958f);
    logWrite(rawB == 0.0f ? LOG_WARN : LOG_INFO,
             "SensorB raw angle after init: %.4f rad (%.1f deg)", rawB, rawB * 57.2958f);

    // ── Motor A ───────────────────────────────────────────────────────────
    _motorA.linkDriver(_driverA);
    _motorA.linkSensor(_sensorA);
    _motorA.voltage_limit         = config.voltage_limit;
    _motorA.velocity_limit        = config.velocity_limit;
    _motorA.voltage_sensor_align  = config.sensor_align_voltage;
    _motorA.controller            = angle;
    int motOkA = _motorA.init();
    logWrite(motOkA ? LOG_INFO : LOG_ERROR, "MotorA init %s", motOkA ? "OK" : "FAILED");

    // SimpleFOC 2.4 BLDCMotor::init() calls enable() at the end, so the
    // driver is already live here.  The explicit enable() below is redundant
    // but harmless, and makes the intent clear.
    _motorA.enable();

    // ── Motor-A movement self-test ────────────────────────────────────────
    // Apply a static phase voltage and verify the sensor detects rotation.
    // "NOT-MOVED" means the gate driver is not sourcing current — most likely
    // the enable pin is active-LOW on this hardware while SimpleFOC defaults
    // to active-HIGH.  We auto-detect and flip the polarity if needed.
    {
        _motorA.voltage_limit = config.sensor_align_voltage;
        float delta = motorMoveTest(_motorA, _sensorA, config.sensor_align_voltage, 800);
        logWrite(delta > 0.01f ? LOG_INFO : LOG_WARN,
                 "MotorA self-test: delta=%.4f rad (%.1f deg)  en=GPIO%d=%s  [%s]",
                 delta, delta * 57.2958f,
                 pins.enA, digitalRead(pins.enA) ? "HI" : "LO",
                 delta > 0.01f ? "MOVED" : "NOT-MOVED");

        if (delta <= 0.01f) {
            // Try active-LOW polarity
            logWrite(LOG_WARN, "MotorA: flipping enable_active_high to LOW and retrying");
            _driverA->enable_active_high = LOW;
            _driverA->enable();
            float delta2 = motorMoveTest(_motorA, _sensorA, config.sensor_align_voltage, 800);
            logWrite(delta2 > 0.01f ? LOG_INFO : LOG_ERROR,
                     "MotorA active-LOW retry: delta=%.4f rad  [%s]",
                     delta2,
                     delta2 > 0.01f ? "MOVED — active-LOW gate driver confirmed"
                                    : "STILL NOT MOVING — check wiring and 24V supply");
            if (delta2 <= 0.01f) {
                _driverA->enable_active_high = HIGH;  // restore; nothing helped
            }
        }
        // Restore runtime voltage limit — initFOC will re-raise it to sensor_align_voltage
        _motorA.voltage_limit = config.voltage_limit;
    }

    // Read the sensor through the motor's own linked sensor path (goes through
    // update() → correct mux channel) to confirm sensor data reaches the motor.
    _sensorA->update();
    float sensorAngleA = _sensorA->getAngle();
    logWrite(LOG_INFO, "MotorA pre-initFOC sensor angle (via update()): %.4f rad (%.1f deg)",
             sensorAngleA, sensorAngleA * 57.2958f);

    // In SimpleFOC 2.4, initFOC() takes no arguments.  To skip the physical
    // alignment sweep, pre-set zero_electric_angle and sensor_direction directly
    // on the motor object before calling initFOC().  When zero_electric_angle
    // != NOT_SET (-12345), SimpleFOC detects that calibration is already done
    // and skips alignSensor() entirely.
    if (cal.valid) {
        _motorA.zero_electric_angle = cal.zea_A;
        _motorA.sensor_direction    = (Direction)cal.dir_A;
    }
    // alignSensor() calls setPhaseVoltage(voltage_sensor_align, ...) but
    // setPhaseVoltage() clamps Uq to voltage_limit, making sensor_align_voltage
    // ineffective when voltage_limit < sensor_align_voltage.  Raise the limit
    // for the duration of initFOC then restore it.
    _motorA.voltage_limit = config.sensor_align_voltage;
    bool okA = (bool)_motorA.initFOC();
    _motorA.voltage_limit = config.voltage_limit;
    logWrite(okA ? LOG_INFO : LOG_ERROR,
             "MotorA initFOC: %s  zero_elec_angle=%.4f rad  shaft_angle=%.4f rad (%.1f deg)",
             okA ? "OK" : "FAILED",
             _motorA.zero_electric_angle,
             _motorA.shaft_angle, _motorA.shaft_angle * 57.2958f);

    // Disable Motor A's driver before Motor B alignment. Motor A's alignment
    // physically moves the differential, and its cogging then resists Motor B
    // through the coupled mechanism. Disabling just the driver (not the motor
    // object) keeps motor.enabled=true so loopFOC() works post-init.
    _driverA->disable();

    // ── Motor B ───────────────────────────────────────────────────────────
    _motorB.linkDriver(_driverB);
    _motorB.linkSensor(_sensorB);
    _motorB.voltage_limit         = config.voltage_limit;
    _motorB.velocity_limit        = config.velocity_limit;
    _motorB.voltage_sensor_align  = config.sensor_align_voltage;
    _motorB.controller            = angle;
    int motOkB = _motorB.init();
    logWrite(motOkB ? LOG_INFO : LOG_ERROR, "MotorB init %s", motOkB ? "OK" : "FAILED");

    _motorB.enable();

    // ── Motor-B movement self-test ────────────────────────────────────────
    {
        _motorB.voltage_limit = config.sensor_align_voltage;
        float delta = motorMoveTest(_motorB, _sensorB, config.sensor_align_voltage, 800);
        logWrite(delta > 0.01f ? LOG_INFO : LOG_WARN,
                 "MotorB self-test: delta=%.4f rad (%.1f deg)  en=GPIO%d=%s  [%s]",
                 delta, delta * 57.2958f,
                 pins.enB, digitalRead(pins.enB) ? "HI" : "LO",
                 delta > 0.01f ? "MOVED" : "NOT-MOVED");

        if (delta <= 0.01f) {
            logWrite(LOG_WARN, "MotorB: flipping enable_active_high to LOW and retrying");
            _driverB->enable_active_high = LOW;
            _driverB->enable();
            float delta2 = motorMoveTest(_motorB, _sensorB, config.sensor_align_voltage, 800);
            logWrite(delta2 > 0.01f ? LOG_INFO : LOG_ERROR,
                     "MotorB active-LOW retry: delta=%.4f rad  [%s]",
                     delta2,
                     delta2 > 0.01f ? "MOVED — active-LOW gate driver confirmed"
                                    : "STILL NOT MOVING — check wiring and 24V supply");
            if (delta2 <= 0.01f) {
                _driverB->enable_active_high = HIGH;
            }
        }
        _motorB.voltage_limit = config.voltage_limit;
    }

    _sensorB->update();
    float sensorAngleB = _sensorB->getAngle();
    logWrite(LOG_INFO, "MotorB pre-initFOC sensor angle (via update()): %.4f rad (%.1f deg)",
             sensorAngleB, sensorAngleB * 57.2958f);

    if (cal.valid) {
        _motorB.zero_electric_angle = cal.zea_B;
        _motorB.sensor_direction    = (Direction)cal.dir_B;
    }
    _motorB.voltage_limit = config.sensor_align_voltage;
    bool okB = (bool)_motorB.initFOC();
    _motorB.voltage_limit = config.voltage_limit;  // restored (was missing before this fix)
    logWrite(okB ? LOG_INFO : LOG_ERROR,
             "MotorB initFOC: %s  zero_elec_angle=%.4f rad  shaft_angle=%.4f rad (%.1f deg)",
             okB ? "OK" : "FAILED",
             _motorB.zero_electric_angle,
             _motorB.shaft_angle, _motorB.shaft_angle * 57.2958f);

    // ── Save calibration after a successful live alignment ────────────────
    // Only save when we ran live alignment (cal.valid==false) and both motors
    // succeeded.  If cal was already valid we just used the stored values.
    if (!cal.valid && okA && okB) {
        CalibrationStore::save(
            _motorA.zero_electric_angle, _motorA.sensor_direction,
            _motorB.zero_electric_angle, _motorB.sensor_direction
        );
        logWrite(LOG_INFO,
                 "Calibration saved to NVS (zeaA=%.4f dir=%+d  zeaB=%.4f dir=%+d)",
                 _motorA.zero_electric_angle, (int)_motorA.sensor_direction,
                 _motorB.zero_electric_angle, (int)_motorB.sensor_direction);
    }

    // Restore Motor A's driver. Both motor objects remain enabled so loopFOC()
    // runs for both. The control task calls turret_.disable() immediately on
    // its first iteration (nothing armed at startup) which drives both enable
    // pins low via the driver hardware.
    _driverA->enable();

    // ── Handle calibration failure ────────────────────────────────────────
    // If initFOC failed for either motor (zero_electric_angle still NOT_SET),
    // running loopFOC() would produce garbage phase voltages.  Disable both
    // drivers and block the control loop until the user clears NVS calibration
    // (MSG_CLEAR_CALIBRATION via BT), powers on with 24 V, and reboots.
    _calibrated = okA && okB;
    if (!_calibrated) {
        _driverA->disable();
        _driverB->disable();
        _enabled = false;
        logWrite(LOG_ERROR,
                 "FOC init failed — motors disabled. "
                 "Connect 24V power and send CLEAR_CAL via app to re-align.");
    }

    Serial.printf("[TURRET] begin() complete — calibrated=%s\n",
                  _calibrated ? "YES" : "NO");
}

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

    Serial.printf("[TURRET] PID config applied - angle_P=%.2f, vel_P=%.3f, vel_I=%.3f, vel_D=%.4f\n",
                  _config.angle_P, _config.velocity_P, _config.velocity_I, _config.velocity_D);
}

void DifferentialTurret::update() {
    if (!_calibrated) return;  // do not run FOC with invalid zero_electric_angle

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

    MotionControlType ct;
    switch (mode) {
        case TurretMode::VELOCITY:
            ct = velocity_openloop;
            break;
        case TurretMode::POSITION:
            ct = angle_openloop;
            break;
        case TurretMode::CLOSED_LOOP_POSITION:
            ct = angle;
            applyPIDConfig();
            break;
        default:
            ct = velocity_openloop;
    }

    _motorA.controller = ct;
    _motorB.controller = ct;

    const char* modeStr = (mode == TurretMode::VELOCITY)            ? "VELOCITY"
                        : (mode == TurretMode::POSITION)             ? "POSITION"
                        : (mode == TurretMode::CLOSED_LOOP_POSITION) ? "CLOSED_LOOP_POSITION"
                        : "UNKNOWN";
    Serial.printf("[TURRET] setMode() - mode=%s\n", modeStr);
}

TurretMode DifferentialTurret::getMode() const {
    return _mode;
}

void DifferentialTurret::setTarget(const float heading, const float elevation) {
    if (heading != _heading_target || elevation != _elevation_target) {
        Serial.printf("[TURRET] setTarget() - heading=%.4f, elevation=%.4f\n", heading, elevation);
        _heading_target   = heading;
        _elevation_target = elevation;
    }
}

void DifferentialTurret::mixAndApply() {
    const float hdg_motor = _heading_target * _config.gear_ratio_heading;
    const float elv_motor = _elevation_target * _config.gear_ratio_elevation;

    const float tgtA = hdg_motor + elv_motor;
    const float tgtB = hdg_motor - elv_motor;

    if (tgtA != _motorA.target || tgtB != _motorB.target) {
        Serial.printf("[TURRET] mixAndApply() - hdg_motor=%.4f, elv_motor=%.4f, tgtA=%.4f, tgtB=%.4f\n",
                      hdg_motor, elv_motor, tgtA, tgtB);
    }

    _motorA.target = tgtA;
    _motorB.target = tgtB;
}

float DifferentialTurret::getHeading() const {
    return (_motorA.shaft_angle + _motorB.shaft_angle) / 2.0f / _config.gear_ratio_heading;
}

float DifferentialTurret::getElevation() const {
    return (_motorA.shaft_angle - _motorB.shaft_angle) / 2.0f / _config.gear_ratio_elevation;
}

void DifferentialTurret::enable() {
    if (!_calibrated) return;  // refuse to enable with invalid FOC state
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

BLDCMotor& DifferentialTurret::motorA() { return _motorA; }
BLDCMotor& DifferentialTurret::motorB() { return _motorB; }
