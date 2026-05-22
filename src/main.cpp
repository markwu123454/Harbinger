#include "BtTask.h"
#include "ControlTask.h"
#include "SharedData.h"

static BtTask      btTask;
static ControlTask controlTask;

void setup() {
    Serial.begin(115200);
    sharedInit();
    controlTask.start(1, 2);
    btTask.start(0, 1);
}

void loop() { vTaskDelay(1000 / portTICK_PERIOD_MS); }
