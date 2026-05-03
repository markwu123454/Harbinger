#pragma once
#include <SimpleFOC.h>
#include "TCA9548A.h"

// MagneticSensorI2C that selects the correct TCA9548A channel before every
// I2C read, allowing multiple AS5600s (fixed addr 0x36) on one bus.
class MuxedMagneticSensorI2C : public MagneticSensorI2C {
public:
    MuxedMagneticSensorI2C(MagneticSensorI2CConfig_s config, TCA9548A& mux, uint8_t channel)
        : MagneticSensorI2C(config), _mux(mux), _channel(channel) {}

    float getSensorAngle() override {
        _mux.selectChannel(_channel);
        return MagneticSensorI2C::getSensorAngle();
    }

private:
    TCA9548A& _mux;
    uint8_t   _channel;
};
