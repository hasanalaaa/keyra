#include "http.hpp"

#include "esp_log.h"
#include "esp_timer.h"

namespace keyra::api::http {
namespace {
const char* TAG = "http";
constexpr int kRecvRetries = 3;
}  // namespace

void securityHeaders(httpd_req_t* r) {
  // 'unsafe-inline' only because the single-file web build inlines its JS/CSS.
  httpd_resp_set_hdr(r, "Content-Security-Policy",
                     "default-src 'self'; script-src 'self' 'unsafe-inline'; "
                     "style-src 'self' 'unsafe-inline'; img-src 'self' data:");
  httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
}

esp_err_t sendJson(httpd_req_t* r, const char* status, const cJSON* body) {
  char* text = cJSON_PrintUnformatted(body);
  if (!text) {
    ESP_LOGE(TAG, "JSON print out of memory");
    httpd_resp_set_status(r, k500);
    return httpd_resp_send(r, nullptr, 0);
  }
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json; charset=utf-8");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  securityHeaders(r);
  const esp_err_t err = httpd_resp_send(r, text, HTTPD_RESP_USE_STRLEN);
  cJSON_free(text);
  return err;
}

cJSON* errorBody(const char* code, const char* message) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "error", code);
  cJSON_AddStringToObject(o, "message", message);
  return o;
}

esp_err_t sendError(httpd_req_t* r, const char* status, const char* code, const char* message) {
  json::Ptr body(errorBody(code, message));
  return sendJson(r, status, body.get());
}

esp_err_t sendEmpty(httpd_req_t* r, const char* status) {
  httpd_resp_set_status(r, status);
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  securityHeaders(r);
  return httpd_resp_send(r, nullptr, 0);
}

esp_err_t readBody(httpd_req_t* r, std::string& out) {
  out.assign(r->content_len, '\0');
  size_t got = 0;
  int timeouts = 0;
  // The httpd task serves one request at a time: a client trickling a byte
  // every few seconds must not hold it. 10 s, plus 1 s per 32 KiB (a 2 MiB
  // restore gets ~74 s).
  const int64_t deadline = esp_timer_get_time() + (10 + int64_t(r->content_len / 32768)) * 1000000;
  while (got < r->content_len) {
    if (esp_timer_get_time() > deadline) {
      ESP_LOGW(TAG, "body too slow: %u/%u bytes", unsigned(got), unsigned(r->content_len));
      return ESP_FAIL;
    }
    const int n = httpd_req_recv(r, out.data() + got, r->content_len - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= kRecvRetries) continue;
    if (n <= 0) {
      ESP_LOGW(TAG, "body recv failed (%d) after %u/%u bytes", n, unsigned(got), unsigned(r->content_len));
      return ESP_FAIL;
    }
    got += static_cast<size_t>(n);
  }
  return ESP_OK;
}

std::string header(httpd_req_t* r, const char* name, size_t maxLen) {
  const size_t len = httpd_req_get_hdr_value_len(r, name);
  if (len == 0 || len > maxLen) return {};
  std::string v(len + 1, '\0');
  if (httpd_req_get_hdr_value_str(r, name, v.data(), v.size()) != ESP_OK) return {};
  v.resize(len);
  return v;
}

}  // namespace keyra::api::http
