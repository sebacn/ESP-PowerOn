#include <Arduino.h>
#include <ESPmDNS.h>
#include <ESP32Servo.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstdio>

#include "config.h"
#include "index_html.h"

namespace {

Servo powerServo;
WebServer server(80);

bool servoPressed = false;
bool autoRelease = false;
uint32_t pressStartedMs = 0;
bool stationRequested = false;

int readLight() { return analogRead(kPhotoresistorPin); }

bool powerIsOn(int adc) {
  return kPowerOnWhenAbove ? adc >= kPowerOnThreshold : adc <= kPowerOnThreshold;
}

void writeServo(int angle) { powerServo.write(angle); }

void releaseButton() {
  writeServo(kServoReleaseAngle);
  servoPressed = false;
  autoRelease = false;
}

void pressButton() {
  writeServo(kServoPressAngle);
  servoPressed = true;
  autoRelease = false;
}

void tapButton() {
  writeServo(kServoPressAngle);
  servoPressed = true;
  autoRelease = true;
  pressStartedMs = millis();
}

void serviceServo() {
  if (autoRelease && millis() - pressStartedMs >= kTapHoldMs) {
    releaseButton();
  }
}

void sendStatus() {
  const int adc = readLight();
  char body[192];
  snprintf(body, sizeof(body),
           "{\"powerOn\":%s,\"adc\":%d,\"threshold\":%d,\"pressed\":%s,"
           "\"holding\":%s,\"servoAngle\":%d}",
           powerIsOn(adc) ? "true" : "false", adc, kPowerOnThreshold,
           servoPressed ? "true" : "false", autoRelease ? "true" : "false",
           powerServo.read());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void redirectHome() {
  server.sendHeader("Location", "/");
  server.sendHeader("Cache-Control", "no-store");
  server.send(303, "text/plain", "");
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", kIndexHtml);
}

void handlePress() {
  pressButton();
  redirectHome();
}

void handleRelease() {
  releaseButton();
  redirectHome();
}

void handleTap() {
  tapButton();
  redirectHome();
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }

bool connectStation() {
  if (WIFI_SSID[0] == '\0') {
    return false;
  }

  stationRequested = true;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("esp-poweron");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to %s", WIFI_SSID);

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
    Serial.print(".");
    serviceServo();
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Station connect failed.");
    return false;
  }

  Serial.print("Station IP: ");
  Serial.println(WiFi.localIP());
  if (MDNS.begin("esp-poweron")) {
    Serial.println("mDNS: http://esp-poweron.local");
  }
  return true;
}

void startAccessPoint() {
  WiFi.mode(stationRequested ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("Access point ");
  Serial.print(AP_SSID);
  Serial.print(" at ");
  Serial.println(WiFi.softAPIP());
}

void setupServo() {
  ESP32PWM::allocateTimer(0);
  powerServo.setPeriodHertz(50);
  powerServo.attach(kServoPin, kServoMinPulseUs, kServoMaxPulseUs);
  releaseButton();
}

void setupAdc() {
  analogReadResolution(12);
  analogSetPinAttenuation(kPhotoresistorPin, ADC_11db);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("ESP-PowerOn");

  WiFi.persistent(false);
  setupAdc();
  setupServo();

  if (!connectStation()) {
    startAccessPoint();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, sendStatus);
  server.on("/press", HTTP_POST, handlePress);
  server.on("/release", HTTP_POST, handleRelease);
  server.on("/tap", HTTP_POST, handleTap);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server on port 80");
}

void loop() {
  server.handleClient();
  serviceServo();
}
