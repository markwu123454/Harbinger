#include "WifiTask.h"
#include "SharedData.h"
#include "Config.h"

// ── Task plumbing ─────────────────────────────────────────

void WifiTask::start(int core, int priority) {
    Serial.printf("[WiFi] Starting task — core=%d, priority=%d\n", core, priority);
    xTaskCreatePinnedToCore(taskEntry, "WiFi", 8192, this, priority, &handle_, core);
}

void WifiTask::taskEntry(void* param) {
    static_cast<WifiTask*>(param)->run();
}

// ── Main loop ─────────────────────────────────────────────

void WifiTask::run() {
    Serial.printf("[WiFi] Task running on core %d\n", xPortGetCoreID());

    if (!LittleFS.begin(true))
        Serial.println("[FS] LittleFS mount failed");
    else
        Serial.println("[FS] LittleFS mounted OK");

    WiFiClass::mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP \"%s\" started at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    dns_.start(DNS_PORT, "*", AP_IP);
    Serial.printf("[DNS] Captive portal DNS started on port %d → %s\n", DNS_PORT, AP_IP.toString().c_str());

    setupRoutes();
    setupAPI();
    server_.begin();
    Serial.println("[HTTP] Server started");

    unsigned long lastCleanup   = 0;
    unsigned long lastTelemetry = 0;
    uint32_t      loopCount     = 0;

    for (;;) {
        dns_.processNextRequest();
        unsigned long now = millis();
        loopCount++;

        if (now - lastCleanup > 1000) {
            uint32_t before = ws_.count();
            ws_.cleanupClients();
            uint32_t after = ws_.count();
            if (before != after)
                Serial.printf("[WS] cleanupClients() — clients before=%lu, after=%lu\n", before, after);
            lastCleanup = now;
        }

        WifiSnapshot snap = wifiRead();

        if (snap.stateChanged) {
            Serial.printf("[WiFi] State change detected — master_arm=%d, gun_arm=%d, target_v=%.2f\n",
                snap.masterArm, snap.gunArm, snap.targetVoltage);
            JsonDocument doc;
            doc["type"]       = "state";
            doc["master_arm"] = snap.masterArm;
            doc["turret_arm"] = snap.turretArm;
            doc["gun_arm"]    = snap.gunArm;
            doc["target_v"]   = snap.targetVoltage;
            String out;
            serializeJson(doc, out);
            ws_.textAll(out);
            Serial.printf("[WS] Broadcast state → %s\n", out.c_str());
        }

        if (now - lastTelemetry > 500) {
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
        Serial.printf("[HTTP] Captive portal redirect — host=\"%s\" → http://%s\n",
            req->host().c_str(), AP_IP.toString().c_str());
        req->redirect("http://" + AP_IP.toString());
        return true;
    }
    return false;
}

void WifiTask::setupRoutes() {
    Serial.println("[HTTP] Registering routes");

    ws_.onEvent([this](AsyncWebSocket* svr, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg,
                       const uint8_t* data, size_t len) {
        onWsEvent(svr, client, type, arg, data, len);
    });
    server_.addHandler(&ws_);
    Serial.println("[WS] WebSocket handler registered at /ws");

    for (const char* path : {
        "/generate_204", "/gen_204", "/hotspot-detect.html",
        "/fwlink", "/connecttest.txt"
    }) {
        server_.on(path, HTTP_GET, [](AsyncWebServerRequest* req) {
            req->redirect("http://" + AP_IP.toString());
        });
        Serial.printf("[HTTP] Captive portal probe route: GET %s\n", path);
    }

    server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    Serial.println("[HTTP] Static files served from LittleFS / → index.html");

    server_.onNotFound([this](AsyncWebServerRequest* req) {
        Serial.printf("[HTTP] 404 — method=%s url=%s host=%s\n",
            req->methodToString(), req->url().c_str(), req->host().c_str());
        if (!captivePortalRedirect(req))
            req->send(404, "text/plain", "Not found");
    });

    Serial.println("[HTTP] Routes registered OK");
}

void WifiTask::setupAPI() {
    Serial.println("[HTTP] Registering API routes");

    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        Serial.printf("[API] GET /api/status — from %s\n", req->client()->remoteIP().toString().c_str());
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
        Serial.printf("[API] /api/status response → %s\n", out.c_str());
        req->send(200, "application/json", out);
    });

    Serial.println("[HTTP] API routes registered OK");
}

// ── WebSocket events ──────────────────────────────────────

void WifiTask::onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg,
                         const uint8_t* data, size_t len)
{
    switch (type) {
    case WS_EVT_CONNECT: {
        Serial.printf("[WS] Client #%lu connected — ip=%s, total_clients=%u\n",
            client->id(), client->remoteIP().toString().c_str(), ws_.count());
        client->text(R"({"type":"hello","msg":"Connected to Harbinger"})");
        WifiSnapshot snap = wifiRead();
        JsonDocument doc;
        doc["type"]       = "state";
        doc["master_arm"] = snap.masterArm;
        doc["turret_arm"] = snap.turretArm;
        doc["gun_arm"]    = snap.gunArm;
        doc["target_v"]   = snap.targetVoltage;
        String out;
        serializeJson(doc, out);
        client->text(out);
        Serial.printf("[WS] Sent initial state to #%lu → %s\n", client->id(), out.c_str());
        break;
    }
    case WS_EVT_DISCONNECT:
        Serial.printf("[WS] Client #%lu disconnected — remaining_clients=%u\n",
            client->id(), ws_.count());
        break;
    case WS_EVT_DATA: {
        const auto* info = static_cast<AwsFrameInfo*>(arg);
        Serial.printf("[WS] Data from #%lu — opcode=%d, len=%u, final=%d, index=%llu\n",
            client->id(), info->opcode, len, info->final, info->index);
        if (info->final && info->index == 0 && info->len == len
            && info->opcode == WS_TEXT) {
            String msg;
            msg.reserve(len + 1);
            for (size_t i = 0; i < len; i++) msg += static_cast<char>(data[i]);
            Serial.printf("[WS] Message from #%lu → %s\n", client->id(), msg.c_str());
            handleWsMessage(msg, client);
        } else {
            Serial.printf("[WS] Skipping fragmented/non-text frame from #%lu\n", client->id());
        }
        break;
    }
    case WS_EVT_ERROR:
        Serial.printf("[WS] Error on client #%lu\n", client->id());
        break;
    default:
        Serial.printf("[WS] Unhandled event type %d on client #%lu\n", type, client->id());
        break;
    }
}

void WifiTask::handleWsMessage(const String& msg, AsyncWebSocketClient* client) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) {
        Serial.printf("[WS] JSON parse error from #%lu — raw: %s\n", client->id(), msg.c_str());
        client->text(R"({"type":"error","msg":"bad json"})");
        return;
    }

    const char* type = doc["type"];
    if (!type) {
        Serial.printf("[WS] Message missing 'type' field from #%lu\n", client->id());
        return;
    }

    Serial.printf("[WS] Handling message type=\"%s\" from #%lu\n", type, client->id());

    if (strcmp(type, "ping") == 0) {
        client->text(R"({"type":"pong"})");
        Serial.printf("[WS] Pong sent to #%lu\n", client->id());
    }
    else if (strcmp(type, "aim") == 0) {
        float h = doc["heading"]   | 0.0f;
        float e = doc["elevation"] | 0.0f;
        Serial.printf("[WS] aim — heading=%.4f, elevation=%.4f\n", h, e);
        wifiWriteAim(h, e);
    }
    else if (strcmp(type, "arm") == 0) {
        int master = doc["master"].is<bool>() ? static_cast<int>(doc["master"].as<bool>()) : -1;
        int turret = doc["turret"].is<bool>() ? static_cast<int>(doc["turret"].as<bool>()) : -1;
        int gun    = doc["gun"].is<bool>()    ? static_cast<int>(doc["gun"].as<bool>())    : -1;
        Serial.printf("[WS] arm — master=%d, turret=%d, gun=%d\n", master, turret, gun);
        wifiWriteArm(master, turret, gun);
    }
    else if (strcmp(type, "set_voltage") == 0) {
        float raw        = doc["voltage"] | 0.0f;
        float constrained = constrain(raw, 0.0f, 120.0f);
        Serial.printf("[WS] set_voltage — raw=%.2f, constrained=%.2f\n", raw, constrained);
        wifiWriteVoltage(constrained);
    }
    else if (strcmp(type, "fire") == 0) {
        Serial.printf("[WS] FIRE command received from #%lu\n", client->id());
        wifiWriteFire();
    }
    else {
        Serial.printf("[WS] Unknown message type=\"%s\" from #%lu\n", type, client->id());
    }
}