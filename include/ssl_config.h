#pragma once

#include "esp_https_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void espPoweronSslConfig(httpd_ssl_config_t* conf);

#ifdef __cplusplus
}
#endif
