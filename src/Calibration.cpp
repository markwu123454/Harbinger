#include "Calibration.h"
#include <Preferences.h>

static constexpr const char* NS = "harbinger";

MotorCalibration CalibrationStore::load() {
    Preferences p;
    p.begin(NS, /*readOnly=*/true);
    MotorCalibration c{};
    c.valid = p.getBool("cal_ok", false);
    if (c.valid) {
        c.zea_A = p.getFloat("zea_a", 0.0f);
        c.zea_B = p.getFloat("zea_b", 0.0f);
        c.dir_A = (int8_t)p.getChar("dir_a", 1);
        c.dir_B = (int8_t)p.getChar("dir_b", 1);
    }
    p.end();
    return c;
}

void CalibrationStore::save(float zea_A, Direction dir_A, float zea_B, Direction dir_B) {
    Preferences p;
    p.begin(NS, /*readOnly=*/false);
    p.putFloat("zea_a", zea_A);
    p.putFloat("zea_b", zea_B);
    p.putChar("dir_a", (int8_t)dir_A);
    p.putChar("dir_b", (int8_t)dir_B);
    p.putBool("cal_ok", true);
    p.end();
}

void CalibrationStore::clear() {
    Preferences p;
    p.begin(NS, /*readOnly=*/false);
    p.clear();
    p.end();
}
