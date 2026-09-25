#include "storage.h"

#include <cstdlib>

#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

SemaphoreHandle_t mutex = nullptr;
bool ready = false;

}  // namespace

bool storageBegin() {
  mutex = xSemaphoreCreateMutex();
  ready = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  return ready;
}

bool storageReady() { return ready; }

void storageLock() {
  if (mutex) {
    xSemaphoreTake(mutex, portMAX_DELAY);
  }
}

void storageUnlock() {
  if (mutex) {
    xSemaphoreGive(mutex);
  }
}

bool storageExists(const char* path) {
  if (!ready) {
    return false;
  }
  storageLock();
  const bool exists = LittleFS.exists(path);
  storageUnlock();
  return exists;
}

bool storageRead(const char* path, uint8_t** data, size_t* size) {
  *data = nullptr;
  *size = 0;
  if (!ready) {
    return false;
  }

  storageLock();
  File file = LittleFS.open(path, "r");
  if (!file) {
    storageUnlock();
    return false;
  }
  const size_t length = file.size();
  if (length == 0 || length > 8192) {
    file.close();
    storageUnlock();
    return false;
  }
  uint8_t* buffer = static_cast<uint8_t*>(malloc(length + 1));
  if (!buffer) {
    file.close();
    storageUnlock();
    return false;
  }
  const size_t read = file.read(buffer, length);
  file.close();
  storageUnlock();
  if (read != length) {
    free(buffer);
    return false;
  }
  buffer[length] = 0;
  *data = buffer;
  *size = length + 1;
  return true;
}

bool storageWrite(const char* path, const uint8_t* data, size_t size) {
  if (!ready) {
    return false;
  }
  storageLock();
  File file = LittleFS.open(path, "w");
  if (!file) {
    storageUnlock();
    return false;
  }
  const size_t written = file.write(data, size);
  file.close();
  storageUnlock();
  return written == size;
}
