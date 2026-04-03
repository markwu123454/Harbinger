#include "SharedData.h"

SharedData       shared;
SemaphoreHandle_t dataMutex = nullptr;

void sharedInit() {
    dataMutex = xSemaphoreCreateMutex();
}

MotorSnapshot motorRead() {
    MotorSnapshot s{};
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    s.targetHeading   = shared.targetHeading;
    s.targetElevation = shared.targetElevation;
    s.turretArm       = shared.turretArm;
    s.masterArm       = shared.masterArm;
    s.fireRequested   = shared.fireRequested;
    s.gunArm          = shared.gunArm;
    xSemaphoreGive(dataMutex);
    return s;
}

void motorWrite(float heading, float elevation,
                float aVel, float aAcc,
                float bVel, float bAcc) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    shared.currentHeading   = heading;
    shared.currentElevation = elevation;
    shared.motorA_vel       = aVel;
    shared.motorA_acc       = aAcc;
    shared.motorB_vel       = bVel;
    shared.motorB_acc       = bAcc;
    shared.fireRequested    = false;
    xSemaphoreGive(dataMutex);
}

WifiSnapshot wifiRead() {
    WifiSnapshot s{};
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    s.currentHeading   = shared.currentHeading;
    s.currentElevation = shared.currentElevation;
    s.motorA_vel       = shared.motorA_vel;
    s.motorA_acc       = shared.motorA_acc;
    s.motorB_vel       = shared.motorB_vel;
    s.motorB_acc       = shared.motorB_acc;
    s.masterArm        = shared.masterArm;
    s.turretArm        = shared.turretArm;
    s.gunArm           = shared.gunArm;
    s.targetVoltage    = shared.targetVoltage;
    s.stateChanged     = shared.stateChanged;
    shared.stateChanged = false;
    xSemaphoreGive(dataMutex);
    return s;
}

void wifiWriteAim(float heading, float elevation) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    shared.targetHeading   = heading;
    shared.targetElevation = elevation;
    xSemaphoreGive(dataMutex);
}

void wifiWriteArm(int masterArm, int turretArm, int gunArm) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    if (masterArm >= 0) shared.masterArm = masterArm;
    if (turretArm >= 0) shared.turretArm = turretArm;
    if (gunArm    >= 0) shared.gunArm    = gunArm;
    if (masterArm == 0) {
        shared.turretArm = false;
        shared.gunArm    = false;
    }
    shared.stateChanged = true;
    xSemaphoreGive(dataMutex);
}

void wifiWriteVoltage(float v) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    shared.targetVoltage = v;
    shared.stateChanged  = true;
    xSemaphoreGive(dataMutex);
}

void wifiWriteFire() {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    if (shared.gunArm && shared.masterArm)
        shared.fireRequested = true;
    xSemaphoreGive(dataMutex);
}