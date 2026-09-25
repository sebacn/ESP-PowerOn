#include <Arduino.h>

#include "device.h"
#include "event_log.h"
#include "settings_store.h"
#include "storage.h"
#include "web_ui.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("ESP-PowerOn");
  if (!storageBegin()) {
    Serial.println("Filesystem did not mount. Certificates and saved logs are unavailable.");
  }
  settingsBegin();
  eventLogBegin();
  deviceBegin();
  webBegin();
}

void loop() {
  deviceLoop();
  webLoop();
}
