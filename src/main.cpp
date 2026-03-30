#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include "DifferentialTurret.h"

// ── Config ────────────────────────────────────────────────
auto AP_SSID = "Harbinger";
auto AP_PASS = ""; // blank = open
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);
constexpr uint8_t DNS_PORT = 53;

DifferentialTurret turret;

// ── Globals ───────────────────────────────────────────────
DNSServer dns;
AsyncWebServer http(80);
AsyncWebSocket ws("/ws");

// ── WebSocket event handler ───────────────────────────────
void onWsEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               const AwsEventType type,
               void* arg, const uint8_t* data, const size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("[WS] client #%lu connected from %s\n",
                      client->id(),
                      client->remoteIP().toString().c_str());
        client->text(R"({"event":"hello","msg":"Connected to Harbinger"})");
        break;

    case WS_EVT_DISCONNECT:
        Serial.printf("[WS] client #%lu disconnected\n", client->id());
        break;

    case WS_EVT_DATA:
        {
            const auto* info = static_cast<AwsFrameInfo*>(arg);
            if (info->final && info->index == 0 && info->len == len)
            {
                // Single-frame text message
                if (info->opcode == WS_TEXT)
                {
                    String msg;
                    msg.reserve(len + 1);
                    for (size_t i = 0; i < len; i++) msg += static_cast<char>(data[i]);
                    Serial.printf("[WS] text from #%lu: %s\n", client->id(), msg.c_str());

                    // Echo back + broadcast to all others
                    const String reply = R"({"event":"echo","msg":")" + msg + "\"}";
                    ws.textAll(reply);
                }
            }
            break;
        }

    case WS_EVT_ERROR:
        Serial.printf("[WS] error on client #%lu: %u\n",
                      client->id(), *static_cast<uint16_t*>(arg));
        break;

    default:
        break;
    }
}

// ── Captive portal redirect ───────────────────────────────
// Returns true and redirects if request is NOT aimed at our IP
bool captivePortalRedirect(AsyncWebServerRequest* req)
{
    if (req->host() != AP_IP.toString())
    {
        req->redirect("http://" + AP_IP.toString());
        return true;
    }
    return false;
}

void setupRoutes()
{
    // ── WebSocket ──────────────────────────────────────────
    ws.onEvent(onWsEvent);
    http.addHandler(&ws);

    // ── Captive portal detection URLs ─────────────────────
    for (const char* path : {
        "/generate_204",
        "/gen_204",
        "/hotspot-detect.html",
        "/fwlink",
        "/connecttest.txt"
    })
    {
        http.on(path, HTTP_GET, [](AsyncWebServerRequest* req)
        {
            req->redirect("http://" + AP_IP.toString());
        });
    }

    // ── Static files from LittleFS ───────────────────────────
    http.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // ── 404 → also redirect to portal ─────────────────────
    http.onNotFound([](AsyncWebServerRequest* req)
    {
        if (!captivePortalRedirect(req))
            req->send(404, "text/plain", "Not found");
    });
}

// ── REST API example (optional, easy to extend) ───────────
void setupAPI()
{
    http.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req)
    {
        const String json = "{\"clients\":" + String(ws.count()) +
            ",\"heap\":" + String(ESP.getFreeHeap()) +
            ",\"uptime\":" + String(millis() / 1000) + "}";
        req->send(200, "application/json", json);
    });
}

// ── Periodic WS cleanup (call in loop) ────────────────────
unsigned long lastCleanup = 0;

void cleanupWs()
{
    if (millis() - lastCleanup > 1000)
    {
        ws.cleanupClients(); // free closed connections
        lastCleanup = millis();
    }
}

// ─────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);

    turret.begin({
        .pwmA_a = 25, .pwmA_b = 26, .pwmA_c = 27, .enA = 12,
        .pwmB_a = 14, .pwmB_b = 15, .pwmB_c = 16, .enB = 17
    });

    // LittleFS
    if (!LittleFS.begin(true))
    {
        Serial.println("[FS] LittleFS mount failed");
    }
    else
    {
        Serial.println("[FS] LittleFS mounted");
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

void loop()
{
    dns.processNextRequest();
    turret.update();
    cleanupWs();
}
