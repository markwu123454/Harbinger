#pragma once
#include <Wire.h>

// Driver for TCA9548A / PCA9548A 8-channel I2C multiplexer.
// Selects one channel by writing its bitmask to the mux address.
class TCA9548A {
public:
    explicit TCA9548A(uint8_t address = 0x70) : _addr(address) {}

    void begin(TwoWire& wire = Wire) { _wire = &wire; }

    void selectChannel(uint8_t channel) {
        if (channel > 7) return;
        _wire->beginTransmission(_addr);
        _wire->write(1 << channel);
        _wire->endTransmission();
    }

    void disableAll() {
        _wire->beginTransmission(_addr);
        _wire->write(0x00);
        _wire->endTransmission();
    }

private:
    uint8_t  _addr;
    TwoWire* _wire = &Wire;
};
