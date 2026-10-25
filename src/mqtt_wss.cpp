#include "mqtt_wss.h"

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "device.h"
#include "event_log.h"
#include "settings_store.h"

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace {

constexpr char kAvailabilityTopic[] = "esp-poweron/availability";
constexpr char kPowerTopic[] = "esp-poweron/power";
constexpr char kButtonTopic[] = "esp-poweron/button";
constexpr uint16_t kKeepAliveSec = 30;

WebSocketsClient webSocket;
char headerText[512];
char activeHost[96];
char activePath[64];
char activeId[128];
char activeSecret[256];
char activeUser[64];
char activePassword[65];
char clientId[32];
char statusText[24] = "Not configured";
uint8_t packet[768];

bool running = false;
bool wsUp = false;
bool mqttReady = false;
bool connackSeen = false;
bool dropLink = false;
int publishedPower = -1;
uint32_t lastRxMs = 0;
uint32_t lastPingMs = 0;
uint32_t connackDeadline = 0;

void setStatus(const char* text) { std::strncpy(statusText, text, sizeof(statusText) - 1); }

bool startsWith(const char* value, const char* prefix) {
  return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

void trim(const char* input, char* out, size_t outLen) {
  while (*input == ' ' || *input == '\t') {
    ++input;
  }
  std::strncpy(out, input, outLen - 1);
  out[outLen - 1] = '\0';
  size_t length = std::strlen(out);
  while (length > 0 && (out[length - 1] == ' ' || out[length - 1] == '\t' || out[length - 1] == '/')) {
    // A single trailing slash is a path of "/". Only strip spaces here.
    if (out[length - 1] != ' ' && out[length - 1] != '\t') {
      break;
    }
    out[--length] = '\0';
  }
}

bool hostChar(unsigned char ch) { return std::isalnum(ch) || ch == '.' || ch == '-'; }

bool pathChar(unsigned char ch) { return std::isalnum(ch) || ch == '/' || ch == '_' || ch == '-' || ch == '.'; }

size_t writeMqttString(uint8_t* dest, size_t cap, size_t at, const char* value) {
  const size_t length = std::strlen(value);
  if (at + 2 + length > cap) {
    return 0;
  }
  dest[at] = static_cast<uint8_t>(length >> 8);
  dest[at + 1] = static_cast<uint8_t>(length & 0xFF);
  std::memcpy(dest + at + 2, value, length);
  return at + 2 + length;
}

size_t writeRemaining(uint8_t* dest, size_t value) {
  size_t count = 0;
  do {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value > 0) {
      byte |= 0x80;
    }
    dest[count++] = byte;
  } while (value > 0 && count < 4);
  return count;
}

bool sendPacket(uint8_t type, const uint8_t* body, size_t bodyLen) {
  uint8_t header[5];
  header[0] = type;
  const size_t remLen = writeRemaining(header + 1, bodyLen);
  if (1 + remLen + bodyLen > sizeof(packet)) {
    return false;
  }
  std::memcpy(packet, header, 1 + remLen);
  if (bodyLen > 0) {
    std::memcpy(packet + 1 + remLen, body, bodyLen);
  }
  return webSocket.sendBIN(packet, 1 + remLen + bodyLen);
}

bool publish(const char* topic, const char* payload, bool retain) {
  uint8_t body[700];
  size_t at = writeMqttString(body, sizeof(body), 0, topic);
  if (at == 0) {
    return false;
  }
  const size_t payloadLen = std::strlen(payload);
  if (at + payloadLen > sizeof(body)) {
    return false;
  }
  std::memcpy(body + at, payload, payloadLen);
  return sendPacket(retain ? 0x31 : 0x30, body, at + payloadLen);
}

void publishDiscovery() {
  static const char powerConfig[] =
      "{\"name\":\"PC power\",\"uniq_id\":\"esp-poweron-power\",\"stat_t\":\"esp-poweron/power\","
      "\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"avty_t\":\"esp-poweron/availability\","
      "\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\","
      "\"dev\":{\"ids\":[\"esp-poweron\"],\"name\":\"ESP-PowerOn\",\"mdl\":\"ESP32\",\"mf\":\"ESP-PowerOn\"}}";
  static const char buttonConfig[] =
      "{\"name\":\"Enable PC\",\"uniq_id\":\"esp-poweron-enable\",\"cmd_t\":\"esp-poweron/button\","
      "\"pl_prs\":\"PRESS\",\"avty_t\":\"esp-poweron/availability\","
      "\"dev\":{\"ids\":[\"esp-poweron\"],\"name\":\"ESP-PowerOn\",\"mdl\":\"ESP32\",\"mf\":\"ESP-PowerOn\"}}";
  publish("homeassistant/binary_sensor/esp_poweron_power/config", powerConfig, true);
  publish("homeassistant/button/esp_poweron_enable/config", buttonConfig, true);
}

void sendConnect(const Settings& settings) {
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  std::snprintf(clientId, sizeof(clientId), "esp-poweron-%02X%02X%02X", mac[3], mac[4], mac[5]);

  uint8_t body[640];
  size_t at = writeMqttString(body, sizeof(body), 0, "MQTT");
  if (at == 0 || at + 4 > sizeof(body)) {
    return;
  }
  body[at++] = 4;
  uint8_t flags = 0x02 | 0x04 | 0x20;
  if (settings.mqttUser[0] != '\0') {
    flags |= 0x80;
    if (settings.mqttPassword[0] != '\0') {
      flags |= 0x40;
    }
  }
  body[at++] = flags;
  body[at++] = static_cast<uint8_t>(kKeepAliveSec >> 8);
  body[at++] = static_cast<uint8_t>(kKeepAliveSec & 0xFF);
  at = writeMqttString(body, sizeof(body), at, clientId);
  at = at == 0 ? 0 : writeMqttString(body, sizeof(body), at, kAvailabilityTopic);
  at = at == 0 ? 0 : writeMqttString(body, sizeof(body), at, "offline");
  if (settings.mqttUser[0] != '\0') {
    at = at == 0 ? 0 : writeMqttString(body, sizeof(body), at, settings.mqttUser);
    if (settings.mqttPassword[0] != '\0') {
      at = at == 0 ? 0 : writeMqttString(body, sizeof(body), at, settings.mqttPassword);
    }
  }
  if (at == 0 || !sendPacket(0x10, body, at)) {
    Serial.println("MQTT CONNECT packet was not sent.");
    return;
  }
  connackSeen = false;
  connackDeadline = millis() + 10000;
}

void sendSubscribe() {
  uint8_t body[96];
  body[0] = 0x00;
  body[1] = 0x01;
  size_t at = writeMqttString(body, sizeof(body), 2, kButtonTopic);
  if (at == 0) {
    return;
  }
  body[at++] = 0x00;
  sendPacket(0x82, body, at);
}

bool sameText(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

void handlePublish(const uint8_t* data, size_t len) {
  if (len < 2) {
    return;
  }
  const size_t topicLen = (static_cast<size_t>(data[0]) << 8) | data[1];
  if (2 + topicLen > len) {
    return;
  }
  const char* topic = reinterpret_cast<const char*>(data + 2);
  const uint8_t* payload = data + 2 + topicLen;
  const size_t payloadLen = len - 2 - topicLen;
  if (topicLen != std::strlen(kButtonTopic) || std::memcmp(topic, kButtonTopic, topicLen) != 0) {
    return;
  }
  char text[16] = {};
  const size_t copyLen = payloadLen < sizeof(text) - 1 ? payloadLen : sizeof(text) - 1;
  std::memcpy(text, payload, copyLen);
  for (char* cursor = text; *cursor; ++cursor) {
    if (*cursor >= 'a' && *cursor <= 'z') {
      *cursor = static_cast<char>(*cursor - 32);
    }
  }
  if (std::strcmp(text, "PRESS") == 0 || std::strcmp(text, "ON") == 0 || std::strcmp(text, "1") == 0) {
    deviceRequestPush();
    eventLogAdd("Power button pushed from MQTT");
  }
}

void onMqttPacket(uint8_t type, const uint8_t* data, size_t len) {
  lastRxMs = millis();
  if (type == 0x20 && len >= 2) {
    connackSeen = true;
    if (data[1] != 0) {
      Serial.printf("MQTT broker rejected the connection: %u\n", data[1]);
      eventLogAdd("MQTT broker rejected the login");
      dropLink = true;
      return;
    }
    mqttReady = true;
    publishedPower = -1;
    lastPingMs = millis();
    setStatus("Connected");
    Serial.println("MQTT session ready.");
    eventLogAdd("MQTT connected");
    sendSubscribe();
    publishDiscovery();
    publish(kAvailabilityTopic, "online", true);
    return;
  }
  if (type == 0x30 || type == 0x31 || type == 0x32 || type == 0x33) {
    handlePublish(data, len);
  }
}

void onBinary(const uint8_t* data, size_t len) {
  size_t index = 0;
  while (index + 2 <= len) {
    const uint8_t type = data[index] & 0xF0;
    size_t remaining = 0;
    size_t shift = 0;
    size_t header = 1;
    bool complete = false;
    for (int part = 0; part < 4; ++part) {
      if (index + 1 + part >= len) {
        return;
      }
      const uint8_t byte = data[index + 1 + part];
      remaining += static_cast<size_t>(byte & 0x7F) << shift;
      ++header;
      if ((byte & 0x80) == 0) {
        complete = true;
        break;
      }
      shift += 7;
    }
    if (!complete || index + header + remaining > len) {
      return;
    }
    onMqttPacket(type, data + index + header, remaining);
    index += header + remaining;
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    wsUp = true;
    mqttReady = false;
    setStatus("Connecting");
    Serial.println("WebSocket connected. Sending MQTT CONNECT.");
    sendConnect(settingsCopy());
    return;
  }
  if (type == WStype_DISCONNECTED || type == WStype_ERROR) {
    if (mqttReady || wsUp) {
      eventLogAdd("MQTT disconnected");
    }
    mqttReady = false;
    wsUp = false;
    connackSeen = false;
    if (running) {
      setStatus("Connecting");
    }
    return;
  }
  if (type == WStype_BIN && payload && length > 0) {
    onBinary(payload, length);
  }
}

void buildHeaders(const Settings& settings) {
  headerText[0] = '\0';
  if (settings.cfAccessId[0] == '\0' || settings.cfAccessSecret[0] == '\0') {
    webSocket.setExtraHeaders(nullptr);
    return;
  }
  std::snprintf(headerText, sizeof(headerText),
                "CF-Access-Client-Id: %s\r\nCF-Access-Client-Secret: %s\r\n", settings.cfAccessId,
                settings.cfAccessSecret);
  webSocket.setExtraHeaders(headerText);
}

void copyField(char* dest, size_t destLen, const char* src) {
  std::strncpy(dest, src ? src : "", destLen - 1);
  dest[destLen - 1] = '\0';
}

void remember(const char* host, const char* path, const Settings& settings) {
  copyField(activeHost, sizeof(activeHost), host);
  copyField(activePath, sizeof(activePath), path);
  copyField(activeId, sizeof(activeId), settings.cfAccessId);
  copyField(activeSecret, sizeof(activeSecret), settings.cfAccessSecret);
  copyField(activeUser, sizeof(activeUser), settings.mqttUser);
  copyField(activePassword, sizeof(activePassword), settings.mqttPassword);
}

bool configMatches(const char* host, const char* path, const Settings& settings) {
  return sameText(activeHost, host) && sameText(activePath, path) && sameText(activeId, settings.cfAccessId) &&
         sameText(activeSecret, settings.cfAccessSecret) && sameText(activeUser, settings.mqttUser) &&
         sameText(activePassword, settings.mqttPassword);
}

void stopClient() {
  if (running || wsUp || mqttReady) {
    webSocket.disconnect();
  }
  running = false;
  wsUp = false;
  mqttReady = false;
  connackSeen = false;
  activeHost[0] = '\0';
}

void startClient(const char* host, const char* path, const Settings& settings) {
  if (running) {
    webSocket.disconnect();
  }
  wsUp = false;
  mqttReady = false;
  connackSeen = false;
  buildHeaders(settings);
  webSocket.beginSslWithBundle(host, kMqttWebSocketPort, path, rootca_crt_bundle_start, "mqtt");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(10000);
  remember(host, path, settings);
  running = true;
  setStatus("Connecting");
  Serial.printf("MQTT WebSocket connecting to %s%s\n", host, path);
}

}  // namespace

bool mqttParseEndpoint(const char* input, char* host, size_t hostLen, char* path, size_t pathLen) {
  if (!host || !path || hostLen < 2 || pathLen < 2) {
    return false;
  }
  host[0] = '\0';
  path[0] = '\0';
  if (!input || input[0] == '\0') {
    return true;
  }

  char trimmed[160];
  trim(input, trimmed, sizeof(trimmed));
  const char* cursor = trimmed;
  if (startsWith(cursor, "wss://")) {
    cursor += 6;
  } else if (startsWith(cursor, "https://")) {
    cursor += 8;
  } else if (startsWith(cursor, "ws://") || startsWith(cursor, "http://")) {
    return false;
  }
  if (*cursor == '\0' || *cursor == '/' || *cursor == ':') {
    return false;
  }

  size_t hostLength = 0;
  while (cursor[hostLength] != '\0' && cursor[hostLength] != '/' && cursor[hostLength] != ':') {
    if (!hostChar(static_cast<unsigned char>(cursor[hostLength]))) {
      return false;
    }
    ++hostLength;
  }
  if (hostLength == 0 || hostLength >= hostLen) {
    return false;
  }
  std::memcpy(host, cursor, hostLength);
  host[hostLength] = '\0';
  cursor += hostLength;

  if (*cursor == ':') {
    ++cursor;
    if (std::strncmp(cursor, "443", 3) != 0) {
      return false;
    }
    cursor += 3;
    if (*cursor != '\0' && *cursor != '/') {
      return false;
    }
  }

  if (*cursor == '\0') {
    std::strncpy(path, kMqttWebSocketPath, pathLen - 1);
    path[pathLen - 1] = '\0';
    return true;
  }
  if (*cursor != '/') {
    return false;
  }
  size_t pathLength = 0;
  while (cursor[pathLength] != '\0') {
    if (!pathChar(static_cast<unsigned char>(cursor[pathLength]))) {
      return false;
    }
    ++pathLength;
  }
  if (pathLength == 0 || pathLength >= pathLen) {
    return false;
  }
  std::memcpy(path, cursor, pathLength);
  path[pathLength] = '\0';
  return true;
}

void mqttStatus(char* out, size_t outLen) {
  if (!out || outLen == 0) {
    return;
  }
  std::strncpy(out, statusText, outLen - 1);
  out[outLen - 1] = '\0';
}

void mqttLoop(bool stationReady, bool powerKnown, bool powerOn) {
  const Settings settings = settingsCopy();
  char host[96];
  char path[64];
  const bool parsed = mqttParseEndpoint(settings.haHost, host, sizeof(host), path, sizeof(path));
  const bool enabled = parsed && host[0] != '\0';

  if (!enabled || !stationReady) {
    stopClient();
    setStatus(!enabled ? "Not configured" : "Waiting for Wi-Fi");
    return;
  }

  if (!running || !configMatches(host, path, settings)) {
    startClient(host, path, settings);
  }

  webSocket.loop();

  if (dropLink) {
    dropLink = false;
    webSocket.disconnect();
    return;
  }

  if (wsUp && !connackSeen && static_cast<int32_t>(millis() - connackDeadline) >= 0) {
    Serial.println("MQTT CONNECT timed out.");
    webSocket.disconnect();
    connackSeen = false;
    return;
  }

  if (!mqttReady) {
    return;
  }

  if (millis() - lastRxMs > (kKeepAliveSec + 15) * 1000) {
    Serial.println("MQTT keepalive expired.");
    webSocket.disconnect();
    return;
  }
  if (millis() - lastPingMs > 20000) {
    const uint8_t ping[] = {0xC0, 0x00};
    webSocket.sendBIN(ping, sizeof(ping));
    lastPingMs = millis();
  }
  const int powerValue = powerKnown ? (powerOn ? 1 : 0) : -1;
  if (powerValue >= 0 && powerValue != publishedPower) {
    if (publish(kPowerTopic, powerValue == 1 ? "ON" : "OFF", true)) {
      publishedPower = powerValue;
    }
  }
}
