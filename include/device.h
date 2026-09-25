#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct DeviceSnapshot {
  bool powerOn;
  int adc;
  bool holding;
  bool apMode;
  bool timeValid;
  char timeText[24];
  char ip[40];
  char wifi[48];
  uint32_t uptimeSec;
};

void deviceBegin();
void deviceLoop();
DeviceSnapshot deviceSnapshot();
bool deviceRequestPush();
void deviceRequestRest();
void deviceRequestRestart();
bool deviceFormatTime(time_t epoch, char* out, size_t outLen);
