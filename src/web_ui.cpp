#include "web_ui.h"

#include <WebServer.h>
#include <WiFi.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "device.h"
#include "event_log.h"
#include "https_server.h"
#include "pages.h"
#include "settings_store.h"
#include "storage.h"

namespace {

WebServer server(80);
bool sslActive = false;
bool sslFailed = false;
bool certsUpdated = false;

void appendJsonString(String& out, const char* value) {
  out += '"';
  if (!value) {
    out += '"';
    return;
  }
  for (const char* cursor = value; *cursor; ++cursor) {
    const unsigned char ch = static_cast<unsigned char>(*cursor);
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += static_cast<char>(ch);
    } else if (ch >= 0x20) {
      out += static_cast<char>(ch);
    }
  }
  out += '"';
}

String jsonMessage(bool ok, bool restart, const char* message) {
  String out = "{\"ok\":";
  out += ok ? "true" : "false";
  out += ",\"restart\":";
  out += restart ? "true" : "false";
  out += ",\"message\":";
  appendJsonString(out, message);
  out += "}";
  return out;
}

WebResult jsonResult(int code, bool ok, bool restart, const char* message) {
  WebResult result;
  result.code = code;
  result.type = "application/json";
  result.restart = restart;
  result.body = jsonMessage(ok, restart, message);
  return result;
}

bool urlDecode(const char* in, char* out, size_t outLen) {
  size_t written = 0;
  for (size_t i = 0; in[i] != '\0'; ++i) {
    char ch = in[i];
    if (ch == '+') {
      ch = ' ';
    } else if (ch == '%' && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
               std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      char hex[3] = {in[i + 1], in[i + 2], '\0'};
      ch = static_cast<char>(std::strtol(hex, nullptr, 16));
      i += 2;
    }
    if (written + 1 >= outLen) {
      return false;
    }
    out[written++] = ch;
  }
  out[written] = '\0';
  return true;
}

bool formValue(const char* body, const char* key, char* out, size_t outLen, bool* found) {
  *found = false;
  out[0] = '\0';
  if (!body) {
    return true;
  }
  const size_t keyLen = std::strlen(key);
  const char* cursor = body;
  while (*cursor) {
    const char* amp = std::strchr(cursor, '&');
    const size_t pairLen = amp ? static_cast<size_t>(amp - cursor) : std::strlen(cursor);
    const char* eq = static_cast<const char*>(std::memchr(cursor, '=', pairLen));
    if (eq && static_cast<size_t>(eq - cursor) == keyLen && std::strncmp(cursor, key, keyLen) == 0) {
      char raw[256];
      const size_t valueLen = pairLen - static_cast<size_t>(eq + 1 - cursor);
      if (valueLen >= sizeof(raw)) {
        return false;
      }
      std::memcpy(raw, eq + 1, valueLen);
      raw[valueLen] = '\0';
      *found = true;
      return urlDecode(raw, out, outLen);
    }
    if (!amp) {
      break;
    }
    cursor = amp + 1;
  }
  return true;
}

bool parseInt(const char* text, int* out) {
  if (!text || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(text, &end, 10);
  if (end == text || *end != '\0') {
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

String urlEncode(const String& value) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (unsigned i = 0; i < value.length(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(value[i]);
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out += static_cast<char>(ch);
    } else if (ch == ' ') {
      out += '+';
    } else {
      out += '%';
      out += hex[ch >> 4];
      out += hex[ch & 0x0F];
    }
  }
  return out;
}

WebResult pageResult(const char* page) {
  WebResult result;
  result.code = 200;
  result.type = "text/html";
  result.body = page;
  return result;
}

WebResult statusResult() {
  const DeviceSnapshot snap = deviceSnapshot();
  String body = "{\"powerOn\":";
  body += snap.powerOn ? "true" : "false";
  body += ",\"adc\":";
  body += snap.adc;
  body += ",\"holding\":";
  body += snap.holding ? "true" : "false";
  body += ",\"apMode\":";
  body += snap.apMode ? "true" : "false";
  body += ",\"timeValid\":";
  body += snap.timeValid ? "true" : "false";
  body += ",\"time\":";
  appendJsonString(body, snap.timeText);
  body += ",\"uptimeSec\":";
  body += snap.uptimeSec;
  body += ",\"ip\":";
  appendJsonString(body, snap.ip);
  body += ",\"wifi\":";
  appendJsonString(body, snap.wifi);
  body += "}";
  WebResult result;
  result.type = "application/json";
  result.body = body;
  return result;
}

WebResult settingsResult() {
  const Settings settings = settingsCopy();
  const DeviceSnapshot snap = deviceSnapshot();
  String body = "{\"restAngle\":";
  body += settings.restAngle;
  body += ",\"pushAngle\":";
  body += settings.pushAngle;
  body += ",\"pushSeconds\":";
  body += settings.pushSeconds;
  body += ",\"apMode\":";
  body += snap.apMode ? "true" : "false";
  body += ",\"wifiSsid\":";
  appendJsonString(body, settings.wifiSsid);
  body += ",\"sslEnabled\":";
  body += settings.sslEnabled ? "true" : "false";
  body += ",\"sslActive\":";
  body += sslActive ? "true" : "false";
  body += ",\"sslFailed\":";
  body += sslFailed ? "true" : "false";
  body += ",\"certStored\":";
  body += storageExists("/cert.pem") ? "true" : "false";
  body += ",\"keyStored\":";
  body += storageExists("/key.pem") ? "true" : "false";
  body += ",\"authUser\":";
  appendJsonString(body, settings.authUser);
  body += "}";
  WebResult result;
  result.type = "application/json";
  result.body = body;
  return result;
}

WebResult logsResult() {
  EventView* events = static_cast<EventView*>(malloc(sizeof(EventView) * kEventLogCapacity));
  if (!events) {
    return jsonResult(400, false, false, "Out of memory.");
  }
  const size_t count = eventLogCopy(events, kEventLogCapacity);
  String body = "{\"events\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i) {
      body += ',';
    }
    char when[24] = {};
    const bool dated = deviceFormatTime(events[i].epoch, when, sizeof(when));
    body += "{\"uptimeSec\":";
    body += events[i].uptimeSec;
    body += ",\"time\":";
    appendJsonString(body, dated ? when : "");
    body += ",\"message\":";
    appendJsonString(body, events[i].message);
    body += '}';
  }
  body += "]}";
  free(events);
  WebResult result;
  result.type = "application/json";
  result.body = body;
  return result;
}

WebResult savePem(const char* body, const char* path, const char* marker, const char* storedMessage) {
  if (!body || std::strstr(body, marker) == nullptr || std::strstr(body, "-----END") == nullptr) {
    return jsonResult(400, false, false, "Upload a PEM file.");
  }
  const size_t length = std::strlen(body);
  if (length == 0 || length > kPemMaxBytes) {
    return jsonResult(400, false, false, "That PEM file is empty or too large.");
  }
  if (!storageWrite(path, reinterpret_cast<const uint8_t*>(body), length)) {
    return jsonResult(400, false, false, "Could not store the file.");
  }
  certsUpdated = true;
  const bool reload = settingsCopy().sslEnabled;
  return jsonResult(200, true, false,
                    reload ? storedMessage : "Stored. Enable SSL and save to use it.");
}

WebResult saveSettings(const char* body) {
  char restText[16];
  char pushText[16];
  char holdText[16];
  char user[32];
  char authPassword[65];
  char ssid[33];
  char wifiPassword[65];
  bool hasRest = false;
  bool hasPush = false;
  bool hasHold = false;
  bool hasUser = false;
  bool hasAuthPassword = false;
  bool hasSsid = false;
  bool hasWifiPassword = false;
  bool hasSsl = false;
  char ignored[8];

  if (!formValue(body, "restAngle", restText, sizeof(restText), &hasRest) ||
      !formValue(body, "pushAngle", pushText, sizeof(pushText), &hasPush) ||
      !formValue(body, "pushSeconds", holdText, sizeof(holdText), &hasHold) ||
      !formValue(body, "authUser", user, sizeof(user), &hasUser) ||
      !formValue(body, "authPassword", authPassword, sizeof(authPassword), &hasAuthPassword) ||
      !formValue(body, "wifiSsid", ssid, sizeof(ssid), &hasSsid) ||
      !formValue(body, "wifiPassword", wifiPassword, sizeof(wifiPassword), &hasWifiPassword) ||
      !formValue(body, "sslEnabled", ignored, sizeof(ignored), &hasSsl)) {
    return jsonResult(400, false, false, "A setting is too long.");
  }

  int restAngle = 0;
  int pushAngle = 0;
  int pushSeconds = 0;
  if (!hasRest || !hasPush || !hasHold || !parseInt(restText, &restAngle) || !parseInt(pushText, &pushAngle) ||
      !parseInt(holdText, &pushSeconds)) {
    return jsonResult(400, false, false, "Enter the servo angles and hold time.");
  }
  if (restAngle < 0 || restAngle > 180 || pushAngle < 0 || pushAngle > 180) {
    return jsonResult(400, false, false, "Servo angles must be from 0 to 180.");
  }
  if (pushSeconds < kMinPushSeconds || pushSeconds > kMaxPushSeconds) {
    return jsonResult(400, false, false, "Hold time must be from 1 to 30 seconds.");
  }
  if (!hasUser || user[0] == '\0' || std::strchr(user, ':') != nullptr) {
    return jsonResult(400, false, false, "Enter a login user without a colon.");
  }
  if (hasAuthPassword && authPassword[0] != '\0' && std::strlen(authPassword) < 4) {
    return jsonResult(400, false, false, "Login password must be at least 4 characters.");
  }
  const DeviceSnapshot snap = deviceSnapshot();
  if (snap.apMode && hasWifiPassword && wifiPassword[0] != '\0' && std::strlen(wifiPassword) < 8) {
    return jsonResult(400, false, false, "Wi-Fi password must be at least 8 characters.");
  }
  if (hasSsl && (!storageExists("/cert.pem") || !storageExists("/key.pem"))) {
    return jsonResult(400, false, false, "Upload a certificate and a private key before enabling SSL.");
  }

  Settings next = settingsCopy();
  const bool sslChanged = next.sslEnabled != hasSsl;
  bool wifiChanged = false;
  next.restAngle = restAngle;
  next.pushAngle = pushAngle;
  next.pushSeconds = pushSeconds;
  next.sslEnabled = hasSsl;
  std::strncpy(next.authUser, user, sizeof(next.authUser) - 1);
  next.authUser[sizeof(next.authUser) - 1] = '\0';
  if (hasAuthPassword && authPassword[0] != '\0') {
    std::strncpy(next.authPassword, authPassword, sizeof(next.authPassword) - 1);
    next.authPassword[sizeof(next.authPassword) - 1] = '\0';
  }
  if (snap.apMode && hasSsid) {
    if (std::strcmp(ssid, next.wifiSsid) != 0) {
      wifiChanged = true;
    }
    std::strncpy(next.wifiSsid, ssid, sizeof(next.wifiSsid) - 1);
    next.wifiSsid[sizeof(next.wifiSsid) - 1] = '\0';
    if (hasWifiPassword && wifiPassword[0] != '\0' && std::strcmp(wifiPassword, next.wifiPassword) != 0) {
      wifiChanged = true;
      std::strncpy(next.wifiPassword, wifiPassword, sizeof(next.wifiPassword) - 1);
      next.wifiPassword[sizeof(next.wifiPassword) - 1] = '\0';
    }
  }

  settingsSave(next);
  deviceRequestRest();

  const bool restart = wifiChanged || sslChanged || (certsUpdated && next.sslEnabled);
  const char* message = "Saved.";
  if (restart && wifiChanged) {
    message = "Saved. Restarting to join Wi-Fi.";
  } else if (restart && sslChanged && next.sslEnabled) {
    message = "Saved. Restarting with HTTPS on port 443.";
  } else if (restart && sslChanged) {
    message = "Saved. Restarting with HTTP on port 80.";
  } else if (restart) {
    message = "Saved. Restarting to load the certificates.";
  }
  if (restart) {
    certsUpdated = false;
  }
  return jsonResult(200, true, restart, message);
}

WebResult enableResult() {
  if (!deviceRequestPush()) {
    return jsonResult(200, false, false, "The power button is already being held.");
  }
  return jsonResult(200, true, false, "Pressing the power button.");
}

bool authorized() {
  const Settings settings = settingsCopy();
  return server.authenticate(settings.authUser, settings.authPassword);
}

void sendResult(const WebResult& result) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(result.code, result.type.c_str(), result.body);
  if (result.restart) {
    deviceRequestRestart();
  }
}

void requireAndServe(const char* method, const char* path, const char* body) {
  if (!authorized()) {
    server.requestAuthentication(BASIC_AUTH, "ESP-PowerOn", "Authentication required");
    return;
  }
  sendResult(webDispatch(method, path, body));
}

void addField(String& body, const char* key, const String& value) {
  if (body.length()) {
    body += '&';
  }
  body += key;
  body += '=';
  body += urlEncode(value);
}

void handleSettingsPost() {
  String body;
  if (server.hasArg("restAngle")) {
    addField(body, "restAngle", server.arg("restAngle"));
  }
  if (server.hasArg("pushAngle")) {
    addField(body, "pushAngle", server.arg("pushAngle"));
  }
  if (server.hasArg("pushSeconds")) {
    addField(body, "pushSeconds", server.arg("pushSeconds"));
  }
  if (server.hasArg("sslEnabled")) {
    addField(body, "sslEnabled", "1");
  }
  if (server.hasArg("authUser")) {
    addField(body, "authUser", server.arg("authUser"));
  }
  if (server.hasArg("authPassword")) {
    addField(body, "authPassword", server.arg("authPassword"));
  }
  if (server.hasArg("wifiSsid")) {
    addField(body, "wifiSsid", server.arg("wifiSsid"));
  }
  if (server.hasArg("wifiPassword")) {
    addField(body, "wifiPassword", server.arg("wifiPassword"));
  }
  requireAndServe("POST", "/api/settings", body.c_str());
}

void handleFavicon() { server.send(204, "text/plain", ""); }

void handleRedirect() {
  String host = server.hostHeader();
  const int colon = host.lastIndexOf(':');
  if (colon > 0) {
    host = host.substring(0, colon);
  }
  if (host.length() == 0) {
    host = WiFi.localIP().toString();
  }
  server.sendHeader("Location", "https://" + host + server.uri());
  server.send(301, "text/plain", "Use HTTPS");
}

void handleNotFound() {
  if (!authorized()) {
    server.requestAuthentication(BASIC_AUTH, "ESP-PowerOn", "Authentication required");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

void startRoutes(bool redirectOnly) {
  if (redirectOnly) {
    server.onNotFound(handleRedirect);
  } else {
    server.on("/", HTTP_GET, []() { requireAndServe("GET", "/", nullptr); });
    server.on("/settings", HTTP_GET, []() { requireAndServe("GET", "/settings", nullptr); });
    server.on("/logs", HTTP_GET, []() { requireAndServe("GET", "/logs", nullptr); });
    server.on("/style.css", HTTP_GET, []() { requireAndServe("GET", "/style.css", nullptr); });
    server.on("/favicon.ico", HTTP_GET, handleFavicon);
    server.on("/api/status", HTTP_GET, []() { requireAndServe("GET", "/api/status", nullptr); });
    server.on("/api/settings", HTTP_GET, []() { requireAndServe("GET", "/api/settings", nullptr); });
    server.on("/api/logs", HTTP_GET, []() { requireAndServe("GET", "/api/logs", nullptr); });
    server.on("/api/settings", HTTP_POST, handleSettingsPost);
    server.on("/api/enable", HTTP_POST, []() { requireAndServe("POST", "/api/enable", nullptr); });
    server.on("/api/cert", HTTP_POST, []() { requireAndServe("POST", "/api/cert", server.arg("plain").c_str()); });
    server.on("/api/key", HTTP_POST, []() { requireAndServe("POST", "/api/key", server.arg("plain").c_str()); });
    server.onNotFound(handleNotFound);
  }
  server.begin();
}

}  // namespace

bool webSslActive() { return sslActive; }

bool webSslFailed() { return sslFailed; }

WebResult webDispatch(const char* method, const char* path, const char* body) {
  const bool get = method && std::strcmp(method, "GET") == 0;
  const bool post = method && std::strcmp(method, "POST") == 0;
  if (!path) {
    return jsonResult(404, false, false, "Not found");
  }
  if (get && std::strcmp(path, "/") == 0) {
    return pageResult(kMainPage);
  }
  if (get && std::strcmp(path, "/settings") == 0) {
    return pageResult(kSettingsPage);
  }
  if (get && std::strcmp(path, "/logs") == 0) {
    return pageResult(kLogsPage);
  }
  if (get && std::strcmp(path, "/style.css") == 0) {
    WebResult result;
    result.type = "text/css";
    result.body = kStyleCss;
    return result;
  }
  if (get && std::strcmp(path, "/favicon.ico") == 0) {
    WebResult result;
    result.code = 204;
    result.body = "";
    return result;
  }
  if (get && std::strcmp(path, "/api/status") == 0) {
    return statusResult();
  }
  if (get && std::strcmp(path, "/api/settings") == 0) {
    return settingsResult();
  }
  if (get && std::strcmp(path, "/api/logs") == 0) {
    return logsResult();
  }
  if (post && std::strcmp(path, "/api/enable") == 0) {
    return enableResult();
  }
  if (post && std::strcmp(path, "/api/settings") == 0) {
    return saveSettings(body ? body : "");
  }
  if (post && std::strcmp(path, "/api/cert") == 0) {
    return savePem(body, "/cert.pem", "BEGIN CERTIFICATE",
                   "Certificate stored. Save settings to reload HTTPS.");
  }
  if (post && std::strcmp(path, "/api/key") == 0) {
    return savePem(body, "/key.pem", "PRIVATE KEY", "Private key stored. Save settings to reload HTTPS.");
  }
  WebResult missing;
  missing.code = 404;
  missing.body = "Not found";
  return missing;
}

void webBegin() {
  const Settings settings = settingsCopy();
  Serial.print("Login user: ");
  Serial.println(settings.authUser);

  uint8_t* cert = nullptr;
  uint8_t* key = nullptr;
  size_t certLen = 0;
  size_t keyLen = 0;
  const bool files = storageRead("/cert.pem", &cert, &certLen) && storageRead("/key.pem", &key, &keyLen);
  if (settings.sslEnabled && files && httpsBegin(cert, certLen, key, keyLen)) {
    sslActive = true;
    startRoutes(true);
    Serial.println("HTTPS on port 443. HTTP redirects to HTTPS.");
    return;
  }
  free(cert);
  free(key);
  cert = nullptr;
  key = nullptr;
  if (settings.sslEnabled) {
    sslFailed = true;
    Serial.println("HTTPS did not start. Serving HTTP on port 80.");
  } else {
    Serial.println("HTTP on port 80.");
  }
  startRoutes(false);
}

void webLoop() { server.handleClient(); }
