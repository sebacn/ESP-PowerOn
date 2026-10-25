#pragma once

#include <stddef.h>
#include <stdbool.h>

// Empty input clears the endpoint and returns true.
// A non-empty value must be a hostname, optionally with a path
// (mqtt.example.com or mqtt.example.com/mqtt). Port 443 may be included.
bool mqttParseEndpoint(const char* input, char* host, size_t hostLen, char* path, size_t pathLen);

void mqttLoop(bool stationReady, bool powerKnown, bool powerOn);
void mqttStatus(char* out, size_t outLen);
