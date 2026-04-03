#pragma once
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class WifiTask {
public:
    void start(int core, int priority);

private:
    static void taskEntry(void* param);
    [[noreturn]] void run();

    // WebSocket + message handling
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg,
                   const uint8_t* data, size_t len);
    void handleWsMessage(const String& msg, AsyncWebSocketClient* client);

    // Route setup (split for clarity)
    void setupRoutes();
    void setupAPI();

    bool captivePortalRedirect(AsyncWebServerRequest* req);

    // Owned resources
    AsyncWebServer server_{80};
    AsyncWebSocket ws_{"/ws"};
    DNSServer      dns_;
    TaskHandle_t   handle_ = nullptr;
};