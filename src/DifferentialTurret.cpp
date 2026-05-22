#include "DifferentialTurret.h"
#include "SharedData.h"
#include <Wire.h>

DifferentialTurret::DifferentialTurret()
    : _motorA(11, 11.1f)
    , _motorB(11, 11.1f)
    , _config() {
    Serial.printf("[TURRET] Constructor called with default motor params (poles=11, resistance=11.1)\n");
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

    _driverA = new BLDCDriver3PWM(pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    _driverB = new BLDCDriver3PWM(pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);
    Serial.printf("[TURRET] Driver A pins: pwm=(%d,%d,%d), en=%d\n", pins.pwmA_a, pins.pwmA_b, pins.pwmA_c, pins.enA);
    Serial.printf("[TURRET] Driver B pins: pwm=(%d,%d,%d), en=%d\n", pins.pwmB_a, pins.pwmB_b, pins.pwmB_c, pins.enB);

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

    // Enable the driver before initFOC so the motor phases are powered during
    // the alignment sequence. BLDCDriver3PWM::init() leaves the enable pin LOW
    // (driver off); if initFOC runs with the driver disabled no current flows,
    // the rotor can't move, alignSensor() detects zero movement and returns 0.
    _motorA.enable();

    // Read the sensor through the motor's own linked sensor path (goes through
    // update() → correct mux channel) to confirm sensor data reaches the motor.
    _sensorA->update();
    float sensorAngleA = _sensorA->getAngle();
    logWrite(LOG_INFO, "MotorA pre-initFOC sensor angle (via update()): %.4f rad (%.1f deg)",
             sensorAngleA, sensorAngleA * 57.2958f);

    bool okA = _motorA.initFOC();
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

    _sensorB->update();
    float sensorAngleB = _sensorB->getAngle();
    logWrite(LOG_INFO, "MotorB pre-initFOC sensor angle (via update()): %.4f rad (%.1f deg)",
             sensorAngleB, sensorAngleB * 57.2958f);

    bool okB = _motorB.initFOC();
    logWrite(okB ? LOG_INFO : LOG_ERROR,
             "MotorB initFOC: %s  zero_elec_angle=%.4f rad  shaft_angle=%.4f rad (%.1f deg)",
             okB ? "OK" : "FAILED",
             _motorB.zero_electric_angle,
             _motorB.shaft_angle, _motorB.shaft_angle * 57.2958f);

    // Restore Motor A's driver. Both motor objects remain enabled so loopFOC()
    // runs for both. The control task calls turret_.disable() immediately on
    // its first iteration (nothing armed at startup) which drives both enable
    // pins low via the driver hardware.
    _driverA->enable();

    Serial.printf("[TURRET] begin() complete\n");
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
