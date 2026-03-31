#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "DifferentialTurret.h"

// ── Config ────────────────────────────────────────────────
auto AP_SSID = "Harbinger";
auto AP_PASS = "";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);
constexpr uint8_t DNS_PORT = 53;

// ── Shared state ──────────────────────────────────────────
SemaphoreHandle_t dataMutex;

struct SharedData {
    // wifi → motor
    float targetHeading   = 0;    // deg, 0–360
    float targetElevation = 0;    // deg
    bool  masterArm       = false;
    bool  turretArm       = false;
    bool  gunArm          = false;
    bool  fireRequested   = false;
    float targetVoltage   = 0;    // V, 0–120

    // motor → wifi
    float currentHeading   = 0;
    float currentElevation = 0;
    float motorA_vel       = 0;
    float motorA_acc       = 0;
    float motorB_vel       = 0;
    float motorB_acc       = 0;

    // TODO: coilgun hardware state
    // float   capVoltages[N];
    // uint8_t coilStates[N];
    // bool    sensors[N];

    bool stateChanged = false; // flag to broadcast State msg
} shared;

// ── Snapshot types ────────────────────────────────────────

struct MotorSnapshot {
    float targetHeading, targetElevation;
    bool  turretArm, masterArm;
    bool  fireRequested, gunArm;
};

MotorSnapshot motorRead() {
    MotorSnapshot s;
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

void motorWrite(float heading, float elevation, float aVel, float aAcc, float bVel, float bAcc) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    shared.currentHeading   = heading;
    shared.currentElevation = elevation;
    shared.motorA_vel       = aVel;
    shared.motorA_acc       = aAcc;
    shared.motorB_vel       = bVel;
    shared.motorB_acc       = bAcc;
    shared.fireRequested    = false; // consumed
    xSemaphoreGive(dataMutex);
}

struct WifiSnapshot {
    float currentHeading, currentElevation;
    float motorA_vel, motorA_acc;
    float motorB_vel, motorB_acc;
    bool  masterArm, turretArm, gunArm;
    float targetVoltage;
    bool  stateChanged;
};

WifiSnapshot wifiRead() {
    WifiSnapshot s;
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
    shared.stateChanged = false; // consumed
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
    // disarming master kills everything
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
    if (shared.gunArm && shared.masterArm) {
        shared.fireRequested = true;
    }
    xSemaphoreGive(dataMutex);
}

// ── Motor task (Core 1, ~100Hz) ───────────────────────────
DifferentialTurret turret;

[[noreturn]] void motorTask(void* param) {
    turret.begin({
        .pwmA_a = 25, .pwmA_b = 26, .pwmA_c = 27, .enA = 12,
        .pwmB_a = 14, .pwmB_b = 15, .pwmB_c = 16, .enB = 17
    });
    turret.setMode(TurretMode::POSITION);

    for (;;) {
        MotorSnapshot snap = motorRead();

        if (!snap.masterArm || !snap.turretArm) {
            if (turret.getEnabled()) turret.disable();
        } else {
            if (!turret.getEnabled()) turret.enable();
            turret.setTarget(
                snap.targetHeading   * DEG_TO_RAD,
                snap.targetElevation * DEG_TO_RAD
            );
        }

        turret.update();

        // TODO: if snap.fireRequested && snap.gunArm → fire coilgun

        motorWrite(
            turret.getHeading()   * RAD_TO_DEG,
            turret.getElevation() * RAD_TO_DEG,
            turret.motorA().shaft_velocity,
            turret.motorA().shaft_velocity_sp - turret.motorA().shaft_velocity, // crude acc proxy
            turret.motorB().shaft_velocity,
            turret.motorB().shaft_velocity_sp - turret.motorB().shaft_velocity
        );

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ── Wi-Fi / web globals ────────────────────────────────────
DNSServer dns;
AsyncWebServer http(80);
AsyncWebSocket ws("/ws");

// ── WS message handler ───────────────────────────────────
void handleWsMessage(const String& msg, AsyncWebSocketClient* client) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) {
        client->text(R"({"type":"error","msg":"bad json"})");
        return;
    }

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "ping") == 0) {
        client->text(R"({"type":"pong"})");
    }
    else if (strcmp(type, "aim") == 0) {
        wifiWriteAim(
            doc["heading"]   | 0.0f,
            doc["elevation"] | 0.0f
        );
    }
    else if (strcmp(type, "arm") == 0) {
        wifiWriteArm(
            doc["master"].is<bool>()  ? (int)doc["master"].as<bool>()  : -1,
            doc["turret"].is<bool>()  ? (int)doc["turret"].as<bool>()  : -1,
            doc["gun"].is<bool>()     ? (int)doc["gun"].as<bool>()     : -1
        );
    }
    else if (strcmp(type, "set_voltage") == 0) {
        float v = doc["voltage"] | 0.0f;
        v = constrain(v, 0.0f, 120.0f);
        wifiWriteVoltage(v);
    }
    else if (strcmp(type, "fire") == 0) {
        wifiWriteFire();
    }
}

// ── WebSocket event handler ───────────────────────────────
void onWsEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               const AwsEventType type,
               void* arg, const uint8_t* data, const size_t len)
{
    switch (type) {
    case WS_EVT_CONNECT: {
        Serial.printf("[WS] client #%lu connected\n", client->id());
        client->text(R"({"type":"hello","msg":"Connected to Harbinger"})");
        // send current state on connect
        WifiSnapshot snap = wifiRead();
        JsonDocument doc;
        doc["type"]       = "state";
        doc["master_arm"] = snap.masterArm;
        doc["gun_arm"]    = snap.gunArm;
        doc["target_v"]   = snap.targetVoltage;
        String out;
        serializeJson(doc, out);
        client->text(out);
        break;
    }

    case WS_EVT_DISCONNECT:
        Serial.printf("[WS] client #%lu disconnected\n", client->id());
        break;

    case WS_EVT_DATA: {
        const auto* info = static_cast<AwsFrameInfo*>(arg);
        if (info->final && info->index == 0 && info->len == len
            && info->opcode == WS_TEXT) {
            String msg;
            msg.reserve(len + 1);
            for (size_t i = 0; i < len; i++) msg += static_cast<char>(data[i]);
            handleWsMessage(msg, client);
        }
        break;
    }

    case WS_EVT_ERROR:
        Serial.printf("[WS] error on client #%lu\n", client->id());
        break;
    default: break;
    }
}

// ── Routes ────────────────────────────────────────────────
bool captivePortalRedirect(AsyncWebServerRequest* req) {
    if (req->host() != AP_IP.toString()) {
        req->redirect("http://" + AP_IP.toString());
        return true;
    }
    return false;
}

void setupRoutes() {
    ws.onEvent(onWsEvent);
    http.addHandler(&ws);

    for (const char* path : {
        "/generate_204", "/gen_204", "/hotspot-detect.html",
        "/fwlink", "/connecttest.txt"
    }) {
        http.on(path, HTTP_GET, [](AsyncWebServerRequest* req) {
            req->redirect("http://" + AP_IP.toString());
        });
    }

    http.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    http.onNotFound([](AsyncWebServerRequest* req) {
        if (!captivePortalRedirect(req))
            req->send(404, "text/plain", "Not found");
    });
}

void setupAPI() {
    http.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        WifiSnapshot snap = wifiRead();
        JsonDocument doc;
        doc["heading"]   = snap.currentHeading;
        doc["elevation"] = snap.currentElevation;
        doc["master"]    = snap.masterArm;
        doc["turret"]    = snap.turretArm;
        doc["gun"]       = snap.gunArm;
        doc["clients"]   = ws.count();
        doc["heap"]      = ESP.getFreeHeap();
        doc["uptime"]    = millis() / 1000;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });
}

// ── WiFi task (Core 0) ───────────────────────────────────
[[noreturn]] void wifiTask(void* param) {
    if (!LittleFS.begin(true))
        Serial.println("[FS] LittleFS mount failed");

    WiFiClass::mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP \"%s\" at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    dns.start(DNS_PORT, "*", AP_IP);
    setupRoutes();
    setupAPI();
    http.begin();
    Serial.println("[HTTP] server started");

    unsigned long lastCleanup   = 0;
    unsigned long lastTelemetry = 0;

    for (;;) {
        dns.processNextRequest();

        unsigned long now = millis();

        // cleanup stale WS clients
        if (now - lastCleanup > 1000) {
            ws.cleanupClients();
            lastCleanup = now;
        }

        WifiSnapshot snap = wifiRead();

        // broadcast State message when arm/voltage changes
        if (snap.stateChanged) {
            JsonDocument doc;
            doc["type"]       = "state";
            doc["master_arm"] = snap.masterArm;
            doc["gun_arm"]    = snap.gunArm;
            doc["target_v"]   = snap.targetVoltage;
            String out;
            serializeJson(doc, out);
            ws.textAll(out);
        }

        // broadcast Telemetry at ~10Hz
        if (now - lastTelemetry > 100) {
            JsonDocument doc;
            doc["type"]      = "telemetry";
            doc["heading"]   = round(snap.currentHeading * 100) / 100;
            doc["elevation"] = round(snap.currentElevation * 100) / 100;

            JsonObject ma = doc["motor_a"].to<JsonObject>();
            ma["angle"] = snap.currentHeading; // TODO: individual motor angle
            ma["vel"]   = snap.motorA_vel;
            ma["acc"]   = snap.motorA_acc;

            JsonObject mb = doc["motor_b"].to<JsonObject>();
            mb["angle"] = snap.currentElevation;
            mb["vel"]   = snap.motorB_vel;
            mb["acc"]   = snap.motorB_acc;

            // TODO: caps[], coils[], sensors[]

            String out;
            serializeJson(doc, out);
            ws.textAll(out);
            lastTelemetry = now;
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

// ── Entry point ───────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    dataMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(motorTask, "Motor", 8192, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(wifiTask, "WiFi", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}