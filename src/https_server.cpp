#include "https_server.h"

#include "web_ui.h"

#include <esp_http_server.h>

#include <cstdlib>
#include <cstring>

#include "config.h"
#include "device.h"
#include "settings_store.h"
#include "ssl_config.h"

#include "mbedtls/base64.h"

namespace {

uint8_t* ownedCert = nullptr;
uint8_t* ownedKey = nullptr;
httpd_handle_t server = nullptr;

bool headerMatchesBasic(const char* header, const Settings& settings) {
  if (!header) {
    return false;
  }
  if (strncasecmp(header, "Basic ", 6) != 0) {
    return false;
  }
  const char* encoded = header + 6;
  char expected[100];
  std::snprintf(expected, sizeof(expected), "%s:%s", settings.authUser, settings.authPassword);
  unsigned char decoded[100];
  size_t decodedLen = 0;
  if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decodedLen,
                            reinterpret_cast<const unsigned char*>(encoded), std::strlen(encoded)) != 0) {
    return false;
  }
  decoded[decodedLen] = 0;
  const size_t expectedLen = std::strlen(expected);
  return decodedLen == expectedLen && std::memcmp(decoded, expected, expectedLen) == 0;
}

void reply(httpd_req_t* request, const WebResult& result) {
  if (result.code == 401) {
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"ESP-PowerOn\"");
  } else if (result.code == 400) {
    httpd_resp_set_status(request, "400 Bad Request");
  } else if (result.code == 404) {
    httpd_resp_set_status(request, "404 Not Found");
  } else if (result.code == 204) {
    httpd_resp_set_status(request, "204 No Content");
  }
  httpd_resp_set_type(request, result.type.c_str());
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  if (result.code == 204) {
    httpd_resp_send(request, nullptr, 0);
  } else {
    httpd_resp_send(request, result.body.c_str(), HTTPD_RESP_USE_STRLEN);
  }
  if (result.restart) {
    deviceRequestRestart();
  }
}

void discardBody(httpd_req_t* request) {
  char scratch[128];
  int remaining = request->content_len;
  while (remaining > 0) {
    const int chunk = remaining > static_cast<int>(sizeof(scratch)) ? static_cast<int>(sizeof(scratch)) : remaining;
    const int read = httpd_req_recv(request, scratch, chunk);
    if (read == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (read <= 0) {
      break;
    }
    remaining -= read;
  }
}

esp_err_t dispatch(httpd_req_t* request) {
  if (std::strcmp(request->uri, "/favicon.ico") != 0) {
    char header[200];
    const esp_err_t headerResult = httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header));
    const Settings settings = settingsCopy();
    if (headerResult != ESP_OK || !headerMatchesBasic(header, settings)) {
      WebResult denied;
      denied.code = 401;
      denied.body = "Authentication required";
      reply(request, denied);
      return ESP_OK;
    }
  }

  char* body = nullptr;
  if (request->content_len > 0) {
    if (request->content_len > static_cast<int>(kPemMaxBytes)) {
      discardBody(request);
      WebResult tooLarge;
      tooLarge.code = 400;
      tooLarge.type = "application/json";
      tooLarge.body = "{\"ok\":false,\"restart\":false,\"message\":\"Upload is too large.\"}";
      reply(request, tooLarge);
      return ESP_OK;
    }
    body = static_cast<char*>(malloc(request->content_len + 1));
    if (!body) {
      httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
      return ESP_OK;
    }
    int got = 0;
    while (got < request->content_len) {
      const int read = httpd_req_recv(request, body + got, request->content_len - got);
      if (read == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      if (read <= 0) {
        free(body);
        return ESP_FAIL;
      }
      got += read;
    }
    body[got] = '\0';
  }

  const char* method = request->method == HTTP_GET ? "GET" : "POST";
  WebResult result = webDispatch(method, request->uri, body);
  free(body);
  reply(request, result);
  return ESP_OK;
}

void registerRoute(const char* uri, httpd_method_t method) {
  httpd_uri_t route = {};
  route.uri = uri;
  route.method = method;
  route.handler = dispatch;
  httpd_register_uri_handler(server, &route);
}

}  // namespace

bool httpsBegin(uint8_t* cert, size_t certLen, uint8_t* key, size_t keyLen) {
  httpd_ssl_config_t config = {};
  espPoweronSslConfig(&config);
  config.cacert_pem = cert;
  config.cacert_len = certLen;
  config.prvtkey_pem = key;
  config.prvtkey_len = keyLen;

  const esp_err_t started = httpd_ssl_start(&server, &config);
  if (started != ESP_OK) {
    Serial.printf("HTTPS start failed: %d\n", started);
    server = nullptr;
    return false;
  }
  ownedCert = cert;
  ownedKey = key;

  registerRoute("/", HTTP_GET);
  registerRoute("/settings", HTTP_GET);
  registerRoute("/logs", HTTP_GET);
  registerRoute("/style.css", HTTP_GET);
  registerRoute("/favicon.ico", HTTP_GET);
  registerRoute("/api/status", HTTP_GET);
  registerRoute("/api/settings", HTTP_GET);
  registerRoute("/api/logs", HTTP_GET);
  registerRoute("/api/settings", HTTP_POST);
  registerRoute("/api/enable", HTTP_POST);
  registerRoute("/api/cert", HTTP_POST);
  registerRoute("/api/key", HTTP_POST);
  return true;
}
