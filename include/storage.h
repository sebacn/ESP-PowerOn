#pragma once

#include <stddef.h>
#include <stdint.h>

// LittleFS plus a mutex shared by the log and certificate files.
bool storageBegin();
bool storageReady();
void storageLock();
void storageUnlock();

bool storageExists(const char* path);
bool storageRead(const char* path, uint8_t** data, size_t* size);
bool storageWrite(const char* path, const uint8_t* data, size_t size);
