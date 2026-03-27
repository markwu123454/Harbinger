#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>

// ── Config ────────────────────────────────────────────────
auto AP_SSID   = "Harbinger";
auto AP_PASS   = "";               // blank = open
const IPAddress AP_IP    (192, 168, 4, 1);
const IPAddress AP_MASK  (255, 255, 255, 0);
constexpr uint8_t  DNS_PORT  = 53;

// ── Globals ───────────────────────────────────────────────
DNSServer dns;
AsyncWebServer    http(80);
AsyncWebSocket    ws("/ws");

// ── WebSocket event handler ───────────────────────────────
void onWsEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               const AwsEventType type,
               void* arg, const uint8_t* data, const size_t len)
{
    switch (type) {

    case WS_EVT_CONNECT:
        Serial.printf("[WS] client #%u connected from %s\n",
                      client->id(),
                      client->remoteIP().toString().c_str());
        client->text(R"({"event":"hello","msg":"Connected to Harbinger"})");
        break;

    case WS_EVT_DISCONNECT:
        Serial.printf("[WS] client #%u disconnected\n", client->id());
        break;

    case WS_EVT_DATA: {
        const auto* info = static_cast<AwsFrameInfo *>(arg);
        if (info->final && info->index == 0 && info->len == len) {
            // Single-frame text message
            if (info->opcode == WS_TEXT) {
                String msg;
                msg.reserve(len + 1);
                for (size_t i = 0; i < len; i++) msg += static_cast<char>(data[i]);
                Serial.printf("[WS] text from #%u: %s\n", client->id(), msg.c_str());

                // Echo back + broadcast to all others
                const String reply = R"({"event":"echo","msg":")" + msg + "\"}";
                ws.textAll(reply);
            }
        }
        break;
    }

    case WS_EVT_ERROR:
        Serial.printf("[WS] error on client #%u: %u\n",
                      client->id(), *static_cast<uint16_t *>(arg));
        break;

    default:
        break;
    }
}

// ── Captive portal redirect ───────────────────────────────
// Returns true and redirects if request is NOT aimed at our IP
bool captivePortalRedirect(AsyncWebServerRequest* req) {
    if (req->host() != AP_IP.toString()) {
        req->redirect("http://" + AP_IP.toString());
        return true;
    }
    return false;
}

void setupRoutes() {
    // ── WebSocket ──────────────────────────────────────────
    ws.onEvent(onWsEvent);
    http.addHandler(&ws);

    // ── Captive portal detection URLs ─────────────────────
    // Android
    http.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!captivePortalRedirect(req))
            req->redirect("http://" + AP_IP.toString());
    });
    // Android alt
    http.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("http://" + AP_IP.toString());
    });
    // iOS / macOS
    http.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("http://" + AP_IP.toString());
    });
    // Windows
    http.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("http://" + AP_IP.toString());
    });
    http.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->redirect("http://" + AP_IP.toString());
    });

    // ── Static files from SPIFFS ───────────────────────────
    http.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // ── 404 → also redirect to portal ─────────────────────
    http.onNotFound([](AsyncWebServerRequest* req) {
        if (!captivePortalRedirect(req))
            req->send(404, "text/plain", "Not found");
    });
}

// ── REST API example (optional, easy to extend) ───────────
void setupAPI() {
    http.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        const String json = "{\"clients\":" + String(ws.count()) +
                      ",\"heap\":" + String(ESP.getFreeHeap()) +
                      ",\"uptime\":" + String(millis() / 1000) + "}";
        req->send(200, "application/json", json);
    });
}

// ── Periodic WS cleanup (call in loop) ────────────────────
unsigned long lastCleanup = 0;
void cleanupWs() {
    if (millis() - lastCleanup > 1000) {
        ws.cleanupClients();   // free closed connections
        lastCleanup = millis();
    }
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("[FS] SPIFFS mount failed");
    } else {
        Serial.println("[FS] SPIFFS mounted");
    }

    // Access Point
    WiFiClass::mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP \"%s\" up at %s\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());

    // DNS wildcard → our IP
    dns.start(DNS_PORT, "*", AP_IP);
    Serial.println("[DNS] wildcard started");

    setupRoutes();
    setupAPI();
    http.begin();
    Serial.println("[HTTP] server started");
}

void loop() {
    dns.processNextRequest();
    cleanupWs();
}