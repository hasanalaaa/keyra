#pragma once
#include <string_view>

#include "esp_http_server.h"
#include "routes.hpp"

namespace keyra::api {
// Handles one request under /api/ (path without query string).
esp_err_t handleApi(httpd_req_t* r, Method method, std::string_view path);
// Worker for KDF-heavy routes (unlock, passphrase, backup, restore) so the
// single httpd task keeps answering state polls while they run.
esp_err_t startSlowWorker();
}
