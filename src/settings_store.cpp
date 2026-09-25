#include "settings_store.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#include "config.h"

namespace {

SemaphoreHandle_t mutex = nullptr;
Settings current = {};
Preferences preferences;

void copyDefaultStrings() {
  std::strncpy(current.wifiSsid, WIFI_SSID, sizeof(current.wifiSsid) - 1);
  std::strncpy(current.wifiPassword, WIFI_PASSWORD, sizeof(current.wifiPassword) - 1);
  std::strncpy(current.authUser, AUTH_USER, sizeof(current.authUser) - 1);
  std::strncpy(current.authPassword, AUTH_PASSWORD, sizeof(current.authPassword) - 1);
}

void loadString(const char* key, char* dest, size_t destLen) {
  if (!preferences.isKey(key)) {
    return;
  }
  preferences.getString(key, dest, destLen);
  dest[destLen - 1] = '\0';
}

}  // namespace

void settingsBegin() {
  mutex = xSemaphoreCreateMutex();
  current.restAngle = kDefaultRestAngle;
  current.pushAngle = kDefaultPushAngle;
  current.pushSeconds = kDefaultPushSeconds;
  current.sslEnabled = false;
  copyDefaultStrings();

  if (!preferences.begin("poweron", false)) {
    return;
  }
  current.restAngle = preferences.getInt("rest", current.restAngle);
  current.pushAngle = preferences.getInt("push", current.pushAngle);
  current.pushSeconds = preferences.getInt("hold", current.pushSeconds);
  current.sslEnabled = preferences.getBool("ssl", current.sslEnabled);
  loadString("ssid", current.wifiSsid, sizeof(current.wifiSsid));
  loadString("pass", current.wifiPassword, sizeof(current.wifiPassword));
  loadString("user", current.authUser, sizeof(current.authUser));
  loadString("apass", current.authPassword, sizeof(current.authPassword));
  preferences.end();
}

Settings settingsCopy() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  Settings copy = current;
  xSemaphoreGive(mutex);
  return copy;
}

void settingsSave(const Settings& next) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  current = next;
  Settings stored = current;
  xSemaphoreGive(mutex);

  if (!preferences.begin("poweron", false)) {
    return;
  }
  preferences.putInt("rest", stored.restAngle);
  preferences.putInt("push", stored.pushAngle);
  preferences.putInt("hold", stored.pushSeconds);
  preferences.putBool("ssl", stored.sslEnabled);
  preferences.putString("ssid", stored.wifiSsid);
  preferences.putString("pass", stored.wifiPassword);
  preferences.putString("user", stored.authUser);
  preferences.putString("apass", stored.authPassword);
  preferences.end();
}
