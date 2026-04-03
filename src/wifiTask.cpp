#include "WifiTask.h"
#include "SharedData.h"
#include "Config.h"

// ── Task plumbing ─────────────────────────────────────────

void WifiTask::start(int core, int priority) {
    xTaskCreatePinnedToCore(taskEntry, "WiFi", 8192, this, priority, &handle_, core);
}

void WifiTask::taskEntry(void* param) {
    static_cast<WifiTask*>(param)->run();
}

// ── Main loop ─────────────────────────────────────────────

void WifiTask::run() {
    if (!LittleFS.begin(true))
        Serial.println("[FS] LittleFS mount failed");

    WiFiClass::mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP \"%s\" at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    dns_.start(DNS_PORT, "*", AP_IP);
    setupRoutes();
    setupAPI();
    server_.begin();
    Serial.println("[HTTP] server started");

    unsigned long lastCleanup   = 0;
    unsigned long lastTelemetry = 0;

    for (;;) {
        dns_.processNextRequest();
        unsigned long now = millis();

        if (now - lastCleanup > 1000) {
            ws_.cleanupClients();
            lastCleanup = now;
        }

        WifiSnapshot snap = wifiRead();

        if (snap.stateChanged) {
            JsonDocument doc;
            doc["type"]       = "state";
            doc["master_arm"] = snap.masterArm;
            doc["gun_arm"]    = snap.gunArm;
            doc["target_v"]   = snap.targetVoltage;
            String out;
            serializeJson(doc, out);
            ws_.textAll(out);
        }

        if (now - lastTelemetry > 100) {
            JsonDocument doc;
            doc["type"]      = "telemetry";
            doc["heading"]   = round(snap.currentHeading   * 100) / 100;
            doc["elevation"] = round(snap.currentElevation * 100) / 100;

            JsonObject ma = doc["motor_a"].to<JsonObject>();
            ma["vel"] = snap.motorA_vel;
            ma["acc"] = snap.motorA_acc;

            JsonObject mb = doc["motor_b"].to<JsonObject>();
            mb["vel"] = snap.motorB_vel;
            mb["acc"] = snap.motorB_acc;

            String out;
            serializeJson(doc, out);
            ws_.textAll(out);
            lastTelemetry = now;
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

// ── Routes ────────────────────────────────────────────────

bool WifiTask::captivePortalRedirect(AsyncWebServerRequest* req) {
    if (req->host() != AP_IP.toString()) {
        req->redirect("http://" + AP_IP.toString());
        return true;
    }
    return false;
}

void WifiTask::setupRoutes() {
    // Lambda captures `this` so we can reach ws_ and captivePortalRedirect
    ws_.onEvent([this](AsyncWebSocket* svr, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg,
                       const uint8_t* data, size_t len) {
        onWsEvent(svr, client, type, arg, data, len);
    });
    server_.addHandler(&ws_);

    for (const char* path : {
        "/generate_204", "/gen_204", "/hotspot-detect.html",
        "/fwlink", "/connecttest.txt"
    }) {
        server_.on(path, HTTP_GET, [](AsyncWebServerRequest* req) {
            req->redirect("http://" + AP_IP.toString());
        });
    }

    server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server_.onNotFound([this](AsyncWebServerRequest* req) {
        if (!captivePortalRedirect(req))
            req->send(404, "text/plain", "Not found");
    });
}

void WifiTask::setupAPI() {
    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        WifiSnapshot snap = wifiRead();
        JsonDocument doc;
        doc["heading"]   = snap.currentHeading;
        doc["elevation"] = snap.currentElevation;
        doc["master"]    = snap.masterArm;
        doc["turret"]    = snap.turretArm;
        doc["gun"]       = snap.gunArm;
        doc["clients"]   = ws_.count();
        doc["heap"]      = ESP.getFreeHeap();
        doc["uptime"]    = millis() / 1000;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });
}

// ── WebSocket events ──────────────────────────────────────

void WifiTask::onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg,
                         const uint8_t* data, size_t len)
{
    switch (type) {
    case WS_EVT_CONNECT: {
        Serial.printf("[WS] client #%lu connected\n", client->id());
        client->text(R"({"type":"hello","msg":"Connected to Harbinger"})");
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

void WifiTask::handleWsMessage(const String& msg, AsyncWebSocketClient* client) {
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
        wifiWriteAim(doc["heading"] | 0.0f, doc["elevation"] | 0.0f);
    }
    else if (strcmp(type, "arm") == 0) {
        wifiWriteArm(
            doc["master"].is<bool>() ? static_cast<int>(doc["master"].as<bool>()) : -1,
            doc["turret"].is<bool>() ? static_cast<int>(doc["turret"].as<bool>()) : -1,
            doc["gun"].is<bool>()    ? static_cast<int>(doc["gun"].as<bool>())    : -1
        );
    }
    else if (strcmp(type, "set_voltage") == 0) {
        wifiWriteVoltage(constrain(doc["voltage"] | 0.0f, 0.0f, 120.0f));
    }
    else if (strcmp(type, "fire") == 0) {
        wifiWriteFire();
    }
}