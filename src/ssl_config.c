#include "ssl_config.h"

void espPoweronSslConfig(httpd_ssl_config_t* conf) {
  httpd_ssl_config_t defaults = HTTPD_SSL_CONFIG_DEFAULT();
  *conf = defaults;
  conf->httpd.max_open_sockets = 2;
  conf->httpd.max_uri_handlers = 16;
  conf->httpd.stack_size = 16384;
  conf->port_secure = 443;
}
