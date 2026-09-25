#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Station credentials. An empty SSID starts the setup access point instead.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Access point used when station Wi-Fi is not configured or does not connect.
#ifndef AP_SSID
#define AP_SSID "ESP-PowerOn"
#endif

#ifndef AP_PASSWORD
#define AP_PASSWORD "esp-poweron"
#endif

// SG90 (or similar) signal pin. Power the servo from 5 V and share ground
// with the ESP32. GPIO 18 is a normal PWM pin on the classic ESP32.
constexpr int kServoPin = 18;
constexpr int kServoReleaseAngle = 20;
constexpr int kServoPressAngle = 70;

// How long a Tap holds the power button down before releasing it.
constexpr uint32_t kTapHoldMs = 500;

// Pulse range for a typical SG90. Widen these if the arm does not reach.
constexpr int kServoMinPulseUs = 500;
constexpr int kServoMaxPulseUs = 2400;

// Photoresistor divider on ADC1. ADC2 pins do not work while Wi-Fi is on.
// Wiring: 3V3 -- photoresistor -- GPIO 34 -- 10k -- GND, aimed at the power LED.
// More light then raises the ADC reading.
constexpr int kPhotoresistorPin = 34;
constexpr int kPowerOnThreshold = 1500;
constexpr bool kPowerOnWhenAbove = true;
