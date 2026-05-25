#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <stdarg.h>

// ── Shared state struct ────────────────────────────────────────
struct SharedData {
    // wifi → motor
    float targetHeading   = 0;
    float targetElevation = 0;
    bool  masterArm       = false;
    bool  turretArm       = false;
    bool  gunArm          = false;
    bool  fireRequested   = false;
    float targetVoltage   = 0;

    // motor → wifi
    float currentHeading   = 0;
    float currentElevation = 0;
    float motorA_vel       = 0;
    float motorA_acc       = 0;
    float motorB_vel       = 0;
    float motorB_acc       = 0;
    bool  calibrated       = false;  // true when both motors have valid FOC calibration

    bool stateChanged = false;
};

// ── Snapshot types ───────────────────────────────────────────
struct MotorSnapshot {
    float targetHeading, targetElevation;
    bool  turretArm, masterArm;
    bool  fireRequested, gunArm;
};

struct WifiSnapshot {
    float currentHeading, currentElevation;
    float motorA_vel, motorA_acc;
    float motorB_vel, motorB_acc;
    bool  masterArm, turretArm, gunArm;
    float targetVoltage;
    bool  calibrated;
    bool  stateChanged;
};

// ── Log queue ────────────────────────────────────────────────
constexpr int LOG_QUEUE_DEPTH = 16;
constexpr int LOG_MSG_MAX     = 120;

struct LogEntry {
    uint8_t level;
    char    msg[LOG_MSG_MAX + 1];
};

extern QueueHandle_t logQueue;

// printf-style; drops silently if queue is full (non-blocking)
void logWrite(uint8_t level, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
bool logRead(LogEntry& out);  // returns false if queue empty

// ── Extern declarations ─────────────────────────────────────────
extern SharedData        shared;
extern SemaphoreHandle_t dataMutex;

// ── Accessor functions ─────────────────────────────────────────
void          sharedInit();   // call once before tasks start

MotorSnapshot motorRead();
void          motorWrite(float heading, float elevation,
                         float aVel, float aAcc,
                         float bVel, float bAcc);
void          motorSetCalibrated(bool cal);

WifiSnapshot  wifiRead();
void          wifiWriteAim(float heading, float elevation);
void          wifiWriteArm(int masterArm, int turretArm, int gunArm);
void          wifiWriteVoltage(float v);
void          wifiWriteFire();
