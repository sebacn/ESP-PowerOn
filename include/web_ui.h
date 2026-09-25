#pragma once

#include <Arduino.h>

struct WebResult {
  int code = 200;
  String type = "text/plain";
  String body;
  bool restart = false;
};

void webBegin();
void webLoop();
bool webSslActive();
bool webSslFailed();
WebResult webDispatch(const char* method, const char* path, const char* body);
