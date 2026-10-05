// The HTTP front door: captive probes, foreign-Host redirects, static assets
// and the /api/ dispatch, all through one wildcard handler.
#include "server.hpp"

#include <cstring>
#include <string>

#include "clock.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "handlers.hpp"
#include "http.hpp"
#include "runtime.hpp"

#include <sys/time.h>

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const uint8_t manifest_start[] asm("_binary_manifest_webmanifest_start");
extern const uint8_t manifest_end[] asm("_binary_manifest_webmanifest_end");
extern const uint8_t icon192_start[] asm("_binary_icon_192_png_start");
extern const uint8_t icon192_end[] asm("_binary_icon_192_png_end");
extern const uint8_t icon512_start[] asm("_binary_icon_512_png_start");
extern const uint8_t icon512_end[] asm("_binary_icon_512_png_end");
extern const uint8_t touch_start[] asm("_binary_apple_touch_icon_png_start");
extern const uint8_t touch_end[] asm("_binary_apple_touch_icon_png_end");
extern const uint8_t favicon_start[] asm("_binary_favicon_svg_start");
extern const uint8_t favicon_end[] asm("_binary_favicon_svg_end");

namespace keyra::api {
namespace {

const char* TAG = "server";
constexpr size_t kMaxHostLen = 128;

struct Asset {
  const char* path;
  const uint8_t* start;
  const uint8_t* end;
  const char* type;
  bool gzip;  // also means "the app shell": revalidate on every load
};

const Asset kAssets[] = {
    {"/", index_html_gz_start, index_html_gz_end, "text/html; charset=utf-8", true},
    {"/index.html", index_html_gz_start, index_html_gz_end, "text/html; charset=utf-8", true},
    {"/manifest.webmanifest", manifest_start, manifest_end, "application/manifest+json", false},
    {"/icon-192.png", icon192_start, icon192_end, "image/png", false},
    {"/icon-512.png", icon512_start, icon512_end, "image/png", false},
    {"/apple-touch-icon.png", touch_start, touch_end, "image/png", false},
    {"/favicon.svg", favicon_start, favicon_end, "image/svg+xml", false},
};

Method toMethod(int m) {
  switch (m) {
    case HTTP_GET: return Method::Get;
    case HTTP_POST: return Method::Post;
    case HTTP_PUT: return Method::Put;
    case HTTP_DELETE: return Method::Delete;
    default: return Method::Other;
  }
}

void adoptClientClock(httpd_req_t* r) {
  const auto client = clock::parse(http::header(r, "X-Keyra-Time", 20));
  if (!client || !clock::shouldAdopt(unixMs(), *client)) return;
  const timeval tv{static_cast<time_t>(*client / 1000), static_cast<suseconds_t>((*client % 1000) * 1000)};
  if (settimeofday(&tv, nullptr) == 0) {
    ESP_LOGI(TAG, "clock set from client");
  } else {
    ESP_LOGW(TAG, "settimeofday failed");
  }
}

esp_err_t sendProbe(httpd_req_t* r, const Probe& p) {
  httpd_resp_set_status(r, p.status);
  httpd_resp_set_type(r, p.contentType);
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, p.body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t redirectHome(httpd_req_t* r) {
  httpd_resp_set_status(r, "302 Found");
  httpd_resp_set_hdr(r, "Location", "http://keyra.local/");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, nullptr, 0);
}

esp_err_t sendAsset(httpd_req_t* r, const Asset& a) {
  httpd_resp_set_type(r, a.type);
  if (a.gzip) {
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
  } else {
    httpd_resp_set_hdr(r, "Cache-Control", "public, max-age=31536000");
  }
  http::securityHeaders(r);
  return httpd_resp_send(r, reinterpret_cast<const char*>(a.start), a.end - a.start);
}

esp_err_t handleAny(httpd_req_t* r) {
  const char* q = std::strchr(r->uri, '?');
  const std::string_view path(r->uri, q ? static_cast<size_t>(q - r->uri) : std::strlen(r->uri));
  adoptClientClock(r);

  if (const auto probe = probeFor(path)) return sendProbe(r, *probe);
  // Requests for other hosts (via the catch-all DNS) go to the app; this also
  // keeps DNS-rebinding pages from reaching the API under a foreign origin.
  const size_t hostLen = httpd_req_get_hdr_value_len(r, "Host");
  if (hostLen > kMaxHostLen || !isOwnHost(http::header(r, "Host", kMaxHostLen))) return redirectHome(r);

  const Method method = toMethod(r->method);
  if (path.substr(0, 5) == "/api/") return handleApi(r, method, path);
  if (method == Method::Get) {
    for (const Asset& a : kAssets) {
      if (path == a.path) return sendAsset(r, a);
    }
  }
  return http::sendError(r, http::k404, "not_found", "Not found");
}

}  // namespace

esp_err_t startServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 12288;  // handlers parse JSON and run vault crypto (PBKDF2, AES-GCM)
  cfg.max_open_sockets = 7;
  cfg.lru_purge_enable = true;
  cfg.recv_wait_timeout = 5;
  cfg.send_wait_timeout = 5;
  cfg.max_uri_handlers = 1;
  cfg.max_resp_headers = 12;
  cfg.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t server = nullptr;
  ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "httpd_start");
  const httpd_uri_t any = {
      .uri = "/*",
      .method = static_cast<httpd_method_t>(HTTP_ANY),
      .handler = handleAny,
      .user_ctx = nullptr,
  };
  ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &any), TAG, "register");
  return ESP_OK;
}

}  // namespace keyra::api
