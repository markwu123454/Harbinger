#include "WifiTask.h"
#include "ControlTask.h"
#include "SharedData.h"

static WifiTask   wifiTask;
static ControlTask controlTask;

void setup() {
    Serial.begin(115200);
    dataMutex = xSemaphoreCreateMutex();
    controlTask.start(1, 2);
    wifiTask.start(0, 1);
}

void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }