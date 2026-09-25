#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct EventView {
  time_t epoch;
  uint32_t uptimeSec;
  char message[48];
};

void eventLogBegin();
void eventLogAdd(const char* message);
size_t eventLogCopy(EventView* out, size_t maxOut);
