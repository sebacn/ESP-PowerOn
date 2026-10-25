#pragma once

#include <stdbool.h>

struct Settings {
  int restAngle;
  int pushAngle;
  int pushSeconds;
  char wifiSsid[33];
  char wifiPassword[65];
  bool sslEnabled;
  char authUser[32];
  char authPassword[65];
  char haHost[96];
  char cfAccessId[128];
  char cfAccessSecret[256];
  char mqttUser[64];
  char mqttPassword[65];
};

void settingsBegin();
Settings settingsCopy();
void settingsSave(const Settings& next);
