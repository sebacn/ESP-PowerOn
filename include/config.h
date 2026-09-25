#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// Station credentials used on first boot, before anything is saved on the device.
// An empty SSID starts the setup access point instead.
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Login used on first boot. Change it on the settings page after that.
#ifndef AUTH_USER
#define AUTH_USER "admin"
#endif

#ifndef AUTH_PASSWORD
#define AUTH_PASSWORD "esp-poweron"
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
constexpr int kDefaultRestAngle = 20;
constexpr int kDefaultPushAngle = 70;
constexpr int kDefaultPushSeconds = 1;
constexpr int kMinPushSeconds = 1;
constexpr int kMaxPushSeconds = 30;

// Pulse range for a typical SG90. Widen these if the arm does not reach.
constexpr int kServoMinPulseUs = 500;
constexpr int kServoMaxPulseUs = 2400;

// Photoresistor divider on ADC1. ADC2 pins do not work while Wi-Fi is on.
// Wiring: 3V3 -- photoresistor -- GPIO 34 -- 10k -- GND, aimed at the power LED.
// More light then raises the ADC reading.
constexpr int kPhotoresistorPin = 34;
constexpr int kPowerOnThreshold = 1500;
constexpr bool kPowerOnWhenAbove = true;

// POSIX TZ string. UTC0 is UTC. Change this and rebuild for another zone.
constexpr char kTimeZone[] = "UTC0";

constexpr size_t kEventLogCapacity = 48;
constexpr size_t kPemMaxBytes = 8192;
