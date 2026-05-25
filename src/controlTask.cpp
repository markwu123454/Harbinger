#include "ControlTask.h"
#include "SharedData.h"
#include "Config.h"

void ControlTask::start(int core, int priority) {
    xTaskCreatePinnedToCore(taskEntry, "Motor", 8192, this, priority, &handle_, core);
}

void ControlTask::taskEntry(void* param) {
    static_cast<ControlTask*>(param)->run();
}

void ControlTask::run() {
    turret_.begin(DEFAULT_TURRET_PINS, DEFAULT_TURRET_CONFIG);
    turret_.setMode(TurretMode::CLOSED_LOOP_POSITION);

    for (;;) {
        const MotorSnapshot snap = motorRead();

        if (!snap.masterArm || !snap.turretArm) {
            if (turret_.getEnabled()) turret_.disable();
        } else {
            if (!turret_.getEnabled()) turret_.enable();
            turret_.setTarget(
                snap.targetHeading   * DEG_TO_RAD,
                snap.targetElevation * DEG_TO_RAD
            );
        }

        turret_.update();

        motorWrite(
            turret_.getHeading()           * RAD_TO_DEG,
            turret_.getElevation()         * RAD_TO_DEG,
            turret_.motorA().shaft_velocity,
            turret_.getMotorAAcceleration(),
            turret_.motorB().shaft_velocity,
            turret_.getMotorBAcceleration()
        );

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
