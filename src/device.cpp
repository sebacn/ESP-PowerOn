#include "device.h"

#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <ctime>
#include <cstring>

#include "config.h"
#include "event_log.h"
#include "settings_store.h"

namespace {

Servo powerServo;
SemaphoreHandle_t mutex = nullptr;
DeviceSnapshot snapshot = {};

bool pushQueued = false;
bool restQueued = false;
bool holding = false;
bool autoRelease = false;
uint32_t pressStartedMs = 0;
uint32_t holdMs = 1000;

bool restartArmed = false;
uint32_t restartAtMs = 0;

int ledCandidate = -1;
int ledStable = -1;
int ledCount = 0;
uint32_t lastLedMs = 0;
uint32_t lastNetworkMs = 0;
bool apMode = false;

void publishLocked(const DeviceSnapshot& next) { snapshot = next; }

void refreshClock(DeviceSnapshot& next) {
  next.uptimeSec = millis() / 1000;
  const time_t now = time(nullptr);
  next.timeValid = deviceFormatTime(now, next.timeText, sizeof(next.timeText));
  if (!next.timeValid) {
    next.timeText[0] = '\0';
  }
}

void refreshNetwork(DeviceSnapshot& next) {
  next.apMode = apMode;
  if (apMode) {
    WiFi.softAPIP().toString().toCharArray(next.ip, sizeof(next.ip));
    std::snprintf(next.wifi, sizeof(next.wifi), "%s (access point)", AP_SSID);
  } else {
    WiFi.localIP().toString().toCharArray(next.ip, sizeof(next.ip));
    std::strncpy(next.wifi, WiFi.SSID().c_str(), sizeof(next.wifi) - 1);
    next.wifi[sizeof(next.wifi) - 1] = '\0';
  }
}

void publishState() {
  DeviceSnapshot next = {};
  xSemaphoreTake(mutex, portMAX_DELAY);
  next = snapshot;
  const bool isHolding = holding;
  xSemaphoreGive(mutex);

  refreshClock(next);
  next.holding = isHolding;
  next.powerOn = ledStable == 1;
  if (millis() - lastNetworkMs > 1000 || next.ip[0] == '\0') {
    refreshNetwork(next);
    lastNetworkMs = millis();
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  next.adc = snapshot.adc;
  publishLocked(next);
  xSemaphoreGive(mutex);
}

void moveServo(int angle) { powerServo.write(angle); }

void startPush() {
  const Settings settings = settingsCopy();
  const int seconds = settings.pushSeconds < kMinPushSeconds ? kMinPushSeconds : settings.pushSeconds;
  holdMs = static_cast<uint32_t>(seconds) * 1000;
  moveServo(settings.pushAngle);
  autoRelease = true;
  pressStartedMs = millis();
  eventLogAdd("Power button pushed");
}

void releaseButton() {
  const Settings settings = settingsCopy();
  moveServo(settings.restAngle);
  eventLogAdd("Power button released");
  xSemaphoreTake(mutex, portMAX_DELAY);
  holding = false;
  autoRelease = false;
  xSemaphoreGive(mutex);
}

bool connectStation(const Settings& settings) {
  if (settings.wifiSsid[0] == '\0') {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp-poweron");
  WiFi.begin(settings.wifiSsid, settings.wifiPassword);
  Serial.printf("Connecting to %s", settings.wifiSsid);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Station connect failed.");
    return false;
  }
  Serial.print("Station IP: ");
  Serial.println(WiFi.localIP());
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  if (MDNS.begin("esp-poweron")) {
    Serial.println("mDNS name: esp-poweron.local");
  }
  return true;
}

void startAccessPoint() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("Access point ");
  Serial.print(AP_SSID);
  Serial.print(" at ");
  Serial.println(WiFi.softAPIP());
}

}  // namespace

bool deviceFormatTime(time_t epoch, char* out, size_t outLen) {
  if (!out || outLen == 0 || epoch < 1600000000) {
    return false;
  }
  struct tm local = {};
  localtime_r(&epoch, &local);
  return strftime(out, outLen, "%Y-%m-%d %H:%M:%S", &local) > 0;
}

void deviceBegin() {
  mutex = xSemaphoreCreateMutex();
  setenv("TZ", kTimeZone, 1);
  tzset();

  analogReadResolution(12);
  analogSetPinAttenuation(kPhotoresistorPin, ADC_11db);

  ESP32PWM::allocateTimer(0);
  powerServo.setPeriodHertz(50);
  powerServo.attach(kServoPin, kServoMinPulseUs, kServoMaxPulseUs);
  moveServo(settingsCopy().restAngle);

  WiFi.persistent(false);
  const Settings settings = settingsCopy();
  if (!connectStation(settings)) {
    startAccessPoint();
  }
  publishState();
}

void deviceLoop() {
  if (restartArmed && static_cast<int32_t>(millis() - restartAtMs) >= 0) {
    Serial.println("Restarting.");
    delay(50);
    ESP.restart();
  }

  bool doPush = false;
  bool doRest = false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (pushQueued && !holding) {
    pushQueued = false;
    holding = true;
    doPush = true;
  } else if (restQueued && !holding) {
    restQueued = false;
    doRest = true;
  }
  xSemaphoreGive(mutex);

  if (doPush) {
    startPush();
  } else if (doRest) {
    moveServo(settingsCopy().restAngle);
  }

  if (autoRelease && millis() - pressStartedMs >= holdMs) {
    releaseButton();
  }

  if (millis() - lastLedMs >= 200) {
    lastLedMs = millis();
    const int adc = analogRead(kPhotoresistorPin);
    const bool on = kPowerOnWhenAbove ? adc >= kPowerOnThreshold : adc <= kPowerOnThreshold;
    const int sample = on ? 1 : 0;
    if (sample != ledCandidate) {
      ledCandidate = sample;
      ledCount = 1;
    } else if (ledCount < 3) {
      ++ledCount;
    }
    if (ledCount >= 3 && ledCandidate != ledStable) {
      ledStable = ledCandidate;
      eventLogAdd(ledStable == 1 ? "Power LED on" : "Power LED off");
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    snapshot.adc = adc;
    xSemaphoreGive(mutex);
  }

  publishState();
}

DeviceSnapshot deviceSnapshot() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  DeviceSnapshot copy = snapshot;
  xSemaphoreGive(mutex);
  return copy;
}

bool deviceRequestPush() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const bool busy = holding || pushQueued;
  if (!busy) {
    pushQueued = true;
  }
  xSemaphoreGive(mutex);
  return !busy;
}

void deviceRequestRest() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  restQueued = true;
  xSemaphoreGive(mutex);
}

void deviceRequestRestart() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (!restartArmed) {
    restartArmed = true;
    restartAtMs = millis() + 800;
  }
  xSemaphoreGive(mutex);
}
