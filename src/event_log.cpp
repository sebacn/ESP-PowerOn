#include "event_log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstring>

#include "config.h"
#include "storage.h"

namespace {

struct Event {
  time_t epoch;
  uint32_t uptimeSec;
  char message[48];
};

SemaphoreHandle_t mutex = nullptr;
Event events[kEventLogCapacity];
size_t head = 0;
size_t count = 0;

void persistLocked() {
  if (!storageReady()) {
    return;
  }
  storageLock();
  File file = LittleFS.open("/events.log", "w");
  if (!file) {
    storageUnlock();
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    const size_t index = (head + kEventLogCapacity - count + i) % kEventLogCapacity;
    char line[96];
    std::snprintf(line, sizeof(line), "%ld %lu %s\n", static_cast<long>(events[index].epoch),
                  static_cast<unsigned long>(events[index].uptimeSec), events[index].message);
    file.print(line);
  }
  file.close();
  storageUnlock();
}

}  // namespace

void eventLogBegin() {
  mutex = xSemaphoreCreateMutex();
  if (!storageReady()) {
    return;
  }

  storageLock();
  File file = LittleFS.open("/events.log", "r");
  String text;
  if (file) {
    text = file.readString();
    file.close();
  }
  storageUnlock();
  if (text.length() == 0) {
    return;
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  unsigned start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) {
      end = text.length();
    }
    String line = text.substring(start, end);
    line.trim();
    start = static_cast<unsigned>(end) + 1;
    if (line.length() == 0) {
      continue;
    }
    long epoch = 0;
    unsigned long uptime = 0;
    char message[48] = {};
    if (std::sscanf(line.c_str(), "%ld %lu %47[^\n]", &epoch, &uptime, message) != 3) {
      continue;
    }
    Event& slot = events[head];
    slot.epoch = static_cast<time_t>(epoch);
    slot.uptimeSec = static_cast<uint32_t>(uptime);
    std::strncpy(slot.message, message, sizeof(slot.message) - 1);
    head = (head + 1) % kEventLogCapacity;
    if (count < kEventLogCapacity) {
      ++count;
    }
  }
  xSemaphoreGive(mutex);
}

void eventLogAdd(const char* message) {
  if (!message || message[0] == '\0') {
    return;
  }
  time_t epoch = 0;
  time(&epoch);
  if (epoch < 1600000000) {
    epoch = 0;
  }

  Event entry = {};
  entry.epoch = epoch;
  entry.uptimeSec = millis() / 1000;
  std::strncpy(entry.message, message, sizeof(entry.message) - 1);

  xSemaphoreTake(mutex, portMAX_DELAY);
  events[head] = entry;
  head = (head + 1) % kEventLogCapacity;
  if (count < kEventLogCapacity) {
    ++count;
  }
  persistLocked();
  xSemaphoreGive(mutex);
}

size_t eventLogCopy(EventView* out, size_t maxOut) {
  if (!out || maxOut == 0) {
    return 0;
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  const size_t copyCount = count < maxOut ? count : maxOut;
  for (size_t i = 0; i < copyCount; ++i) {
    const size_t index = (head + kEventLogCapacity - 1 - i) % kEventLogCapacity;
    out[i].epoch = events[index].epoch;
    out[i].uptimeSec = events[index].uptimeSec;
    std::strncpy(out[i].message, events[index].message, sizeof(out[i].message) - 1);
    out[i].message[sizeof(out[i].message) - 1] = '\0';
  }
  xSemaphoreGive(mutex);
  return copyCount;
}
