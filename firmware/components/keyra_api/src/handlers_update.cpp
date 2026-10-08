#include "handlers_update.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http.hpp"
#include "keyra/net.hpp"
#include "runtime.hpp"
#include "sdkconfig.h"
#include "version.hpp"

namespace keyra::api::update {
namespace {

const char* TAG = "update";
constexpr size_t kChunk = 4096;
constexpr int kRecvRetries = 3;
constexpr size_t kMaxReleaseJson = 96 * 1024;
constexpr size_t kMaxNotes = 2000;
constexpr int kMaxRedirects = 5;
constexpr int64_t kDownloadBudgetUs = 5LL * 60 * 1000000;  // 3 MB on a slow link

enum class Phase { Idle, Receiving, Staged, Failed };

// One image at a time, from either source.
std::atomic<bool> g_busy{false};
std::mutex g_mu;  // guards everything below
Phase g_phase = Phase::Idle;
bool g_fromGithub = false;
size_t g_done = 0, g_total = 0;
std::string g_version, g_error;
const esp_partition_t* g_staged = nullptr;

void setReceiving(size_t total, bool github) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_phase = Phase::Receiving;
  g_fromGithub = github;
  g_done = 0;
  g_total = total;
  g_version.clear();
  g_error.clear();
  g_staged = nullptr;
}

void setProgress(size_t done, size_t total) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_done = done;
  g_total = total;
}

void setFailed(const char* code) {
  ESP_LOGW(TAG, "failed: %s", code);
  std::lock_guard<std::mutex> lock(g_mu);
  g_phase = Phase::Failed;
  g_error = code;
  g_staged = nullptr;
}

// Writes one image into the idle app partition and checks it.
class Stage {
 public:
  // total 0 = unknown length.
  const char* begin(size_t total) {
    part_ = esp_ota_get_next_update_partition(nullptr);
    if (part_ == nullptr) return "no_partition";
    if (total > part_->size) return "too_large";
    const esp_err_t err = esp_ota_begin(part_, total ? total : OTA_SIZE_UNKNOWN, &h_);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "begin: %s", esp_err_to_name(err));
      return "flash_failed";
    }
    open_ = true;
    return nullptr;
  }
  const char* write(const char* p, size_t n) {
    const esp_err_t err = esp_ota_write(h_, p, n);
    if (err == ESP_OK) return nullptr;
    ESP_LOGW(TAG, "write: %s", esp_err_to_name(err));
    return err == ESP_ERR_OTA_VALIDATE_FAILED ? "bad_image" : "flash_failed";
  }
  // On success the image is staged (g_phase = Staged) and its version returned.
  const char* finish(std::string& version) {
    open_ = false;
    // Checks the image and its signature against the key the running firmware
    // was signed with (CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT).
    const esp_err_t err = esp_ota_end(h_);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "image rejected: %s", esp_err_to_name(err));
      return err == ESP_ERR_OTA_VALIDATE_FAILED ? "bad_signature" : "flash_failed";
    }
    esp_app_desc_t next{};
    if (esp_ota_get_partition_description(part_, &next) != ESP_OK || std::string_view(next.project_name) != "keyra")
      return "bad_image";
    const char* running = esp_app_get_description()->version;
    if (!version::mayInstall(next.version, running)) return "downgrade";
    version = next.version;
    ESP_LOGI(TAG, "%s verified and staged in %s (running %s)", next.version, part_->label, running);
    std::lock_guard<std::mutex> lock(g_mu);
    g_phase = Phase::Staged;
    g_version = version;
    g_staged = part_;
    return nullptr;
  }
  ~Stage() {
    if (open_) esp_ota_abort(h_);
  }

 private:
  const esp_partition_t* part_ = nullptr;
  esp_ota_handle_t h_ = 0;
  bool open_ = false;
};

struct BusyGuard {
  ~BusyGuard() { g_busy = false; }
};

const char* statusFor(const char* code) {
  const std::string_view c(code);
  if (c == "downgrade" || c == "busy" || c == "offline") return http::k409;
  if (c == "too_large") return http::k413;
  if (c == "flash_failed" || c == "no_partition") return http::k500;
  if (c == "network" || c == "no_release") return http::k503;
  return http::k400;
}

const char* messageFor(const char* code) {
  const std::string_view c(code);
  if (c == "bad_signature") return "That firmware is not signed with this Keyra's key (or this Keyra was not installed signed)";
  if (c == "bad_image") return "That file is not Keyra firmware";
  if (c == "downgrade") return "That firmware is older than the one installed";
  if (c == "too_large") return "That file is too big to be Keyra firmware";
  if (c == "offline") return "Keyra is not on the internet; join your home Wi-Fi first";
  if (c == "network") return "Could not reach GitHub";
  if (c == "no_release") return "No published Keyra firmware was found";
  if (c == "busy") return "Another update is in progress";
  return "Could not write the update";
}

esp_err_t sendCode(httpd_req_t* r, const char* code) { return http::sendError(r, statusFor(code), code, messageFor(code)); }

// Cuts at most `max` bytes without splitting a UTF-8 character.
std::string clip(const char* s, size_t max) {
  std::string out(s ? s : "");
  if (out.size() <= max) return out;
  size_t n = max;
  while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;
  out.resize(n);
  return out;
}

esp_http_client_handle_t client(const std::string& url, bool binary) {
  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 20000;
  cfg.buffer_size = 4096;     // GitHub's signed redirect URLs make long header lines
  cfg.buffer_size_tx = 2048;
  cfg.user_agent = "Keyra-firmware";
  cfg.disable_auto_redirect = true;  // followed below, so each hop is opened the same way
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (c) esp_http_client_set_header(c, "Accept", binary ? "application/octet-stream" : "application/vnd.github+json");
  return c;
}

// Opens `c` and follows redirects; the body is ready to read. Returns the
// status code (0 on a network failure) and the content length (-1 unknown).
int openFollowing(esp_http_client_handle_t c, int64_t& length) {
  for (int hop = 0;; ++hop) {
    if (esp_http_client_open(c, 0) != ESP_OK) return 0;
    length = esp_http_client_fetch_headers(c);
    const int status = esp_http_client_get_status_code(c);
    const bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
    if (!redirect || hop >= kMaxRedirects) return status;
    esp_http_client_flush_response(c, nullptr);
    esp_http_client_close(c);
    if (esp_http_client_set_redirection(c) != ESP_OK) return 0;
  }
}

struct Release {
  std::string version, url, notes;
  size_t size = 0;
};

// The repository's latest release that carries version::kAssetName.
const char* latestRelease(Release& out) {
  const std::string url = std::string("https://api.github.com/repos/") + CONFIG_KEYRA_UPDATE_REPO + "/releases/latest";
  esp_http_client_handle_t c = client(url, false);
  if (!c) return "network";
  int64_t len = 0;
  const int status = openFollowing(c, len);
  std::string body;
  if (status == 200) {
    char buf[1024];
    for (;;) {
      const int n = esp_http_client_read(c, buf, sizeof buf);
      if (n <= 0) break;
      if (body.size() + n > kMaxReleaseJson) {
        body.clear();  // not a release answer we can use
        break;
      }
      body.append(buf, n);
    }
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  if (status == 0) return "network";
  if (status == 404) return "no_release";
  if (status != 200) {
    ESP_LOGW(TAG, "GitHub answered %d", status);
    return "network";
  }
  json::Ptr j(cJSON_ParseWithLength(body.data(), body.size()));
  const cJSON* tag = cJSON_GetObjectItemCaseSensitive(j.get(), "tag_name");
  out.version = version::fromTag(cJSON_IsString(tag) ? tag->valuestring : "");
  if (out.version.empty()) return "no_release";
  const cJSON* notes = cJSON_GetObjectItemCaseSensitive(j.get(), "body");
  out.notes = clip(cJSON_IsString(notes) ? notes->valuestring : "", kMaxNotes);
  const cJSON* asset = nullptr;
  cJSON_ArrayForEach(asset, cJSON_GetObjectItemCaseSensitive(j.get(), "assets")) {
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(asset, "name");
    if (!cJSON_IsString(name) || version::kAssetName != name->valuestring) continue;
    const cJSON* link = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
    const cJSON* size = cJSON_GetObjectItemCaseSensitive(asset, "size");
    if (!cJSON_IsString(link) || std::string_view(link->valuestring).rfind("https://", 0) != 0) continue;
    out.url = link->valuestring;
    out.size = cJSON_IsNumber(size) && size->valuedouble > 0 ? static_cast<size_t>(size->valuedouble) : 0;
    return nullptr;
  }
  return "no_release";
}

bool online() { return !net::homeIp().empty(); }

void downloadTask(void*) {
  Release rel;
  const char* err = latestRelease(rel);
  if (!err) {
    ESP_LOGI(TAG, "downloading %s (%u bytes)", rel.version.c_str(), unsigned(rel.size));
    esp_http_client_handle_t c = client(rel.url, true);
    int64_t len = 0;
    const int status = c ? openFollowing(c, len) : 0;
    if (status != 200) {
      ESP_LOGW(TAG, "download answered %d", status);
      err = "network";
    } else {
      const size_t total = len > 0 ? static_cast<size_t>(len) : rel.size;
      setProgress(0, total);
      Stage stage;
      err = stage.begin(total);
      std::unique_ptr<char[]> buf(new char[kChunk]);
      size_t got = 0;
      const int64_t deadline = esp_timer_get_time() + kDownloadBudgetUs;
      while (!err) {
        if (esp_timer_get_time() > deadline) {
          err = "network";
          break;
        }
        const int n = esp_http_client_read(c, buf.get(), kChunk);
        if (n < 0) err = "network";
        if (n <= 0) break;
        err = stage.write(buf.get(), n);
        got += n;
        setProgress(got, total);
      }
      if (!err && (got == 0 || (total && got != total))) err = "network";  // cut short
      std::string ver;
      if (!err) err = stage.finish(ver);
    }
    if (c) {
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
    }
  }
  if (err) setFailed(err);
  g_busy = false;
  vTaskDelete(nullptr);
}

}  // namespace

size_t maxImage() {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;
}

esp_err_t upload(httpd_req_t* r) {
  if (g_busy.exchange(true)) return sendCode(r, "busy");
  BusyGuard guard;
  if (r->content_len == 0) return http::sendError(r, http::k400, "invalid", "Send the firmware file as the request body");
  setReceiving(r->content_len, false);
  Stage stage;
  if (const char* e = stage.begin(r->content_len)) {
    setFailed(e);
    return sendCode(r, e);
  }
  // Same pace rule as readBody(): 10 s plus 1 s per 32 KiB.
  const int64_t deadline = esp_timer_get_time() + (10 + int64_t(r->content_len / 32768)) * 1000000;
  std::unique_ptr<char[]> buf(new char[kChunk]);
  size_t got = 0;
  int timeouts = 0;
  while (got < r->content_len) {
    const int n = esp_timer_get_time() > deadline
                      ? -1
                      : httpd_req_recv(r, buf.get(), std::min(kChunk, r->content_len - got));
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= kRecvRetries) continue;
    if (n <= 0) {
      ESP_LOGW(TAG, "upload broke (%d) at %u/%u", n, unsigned(got), unsigned(r->content_len));
      setFailed("network");
      return ESP_FAIL;  // the connection is gone or hopeless: no reply
    }
    timeouts = 0;
    if (const char* e = stage.write(buf.get(), n)) {
      setFailed(e);
      return sendCode(r, e);
    }
    got += static_cast<size_t>(n);
    setProgress(got, r->content_len);
  }
  std::string ver;
  if (const char* e = stage.finish(ver)) {
    setFailed(e);
    return sendCode(r, e);
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "version", ver.c_str());
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t check(httpd_req_t* r) {
  if (!online()) return sendCode(r, "offline");
  Release rel;
  if (const char* e = latestRelease(rel)) return sendCode(r, e);
  const char* running = esp_app_get_description()->version;
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "current", running);
  cJSON_AddStringToObject(o.get(), "latest", rel.version.c_str());
  cJSON_AddBoolToObject(o.get(), "newer",
                        version::mayInstall(rel.version, running) && !version::mayInstall(running, rel.version));
  cJSON_AddNumberToObject(o.get(), "size", static_cast<double>(rel.size));
  cJSON_AddStringToObject(o.get(), "notes", rel.notes.c_str());
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t download(httpd_req_t* r) {
  if (!online()) return sendCode(r, "offline");
  if (g_busy.exchange(true)) return sendCode(r, "busy");
  setReceiving(0, true);
  // TLS and the HTTP client need room; the task ends when the image is staged or failed.
  if (xTaskCreate(downloadTask, "update_dl", 8192, nullptr, 4, nullptr) != pdPASS) {
    setFailed("flash_failed");
    g_busy = false;
    return http::sendError(r, http::k503, "no_memory", "Out of memory");
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddBoolToObject(o.get(), "downloading", true);
  return http::sendJson(r, http::k202, o.get());
}

esp_err_t apply(httpd_req_t* r, const std::string& owner) {
  const esp_partition_t* part;
  std::string ver;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    part = g_phase == Phase::Staged ? g_staged : nullptr;
    ver = g_version;
  }
  if (part == nullptr || g_busy) return http::sendError(r, http::k409, "not_staged", "No verified update is waiting");
  const auto armed = machine().awaitPresence(actions::Op::Update, [part] {
    // A new upload may have started since: only switch to a still-staged image.
    {
      std::lock_guard<std::mutex> lock(g_mu);
      if (g_phase != Phase::Staged || g_staged != part || g_busy) return false;
    }
    const esp_err_t e = esp_ota_set_boot_partition(part);
    if (e != ESP_OK) {
      ESP_LOGE(TAG, "set boot partition: %s", esp_err_to_name(e));
      return false;
    }
    ESP_LOGI(TAG, "installed into %s; restarting", part->label);
    restartSoon();
    return true;
  }, owner);
  if (!armed) {
    return http::sendError(r, http::k409, "busy",
                           "Keyra is waiting for another request; long-press its button to cancel it");
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddStringToObject(o.get(), "op", actions::opName(actions::Op::Update));
  cJSON_AddStringToObject(o.get(), "cancel", armed->cancel.c_str());
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(armed->expiresIn));
  cJSON_AddStringToObject(o.get(), "version", ver.c_str());
  return http::sendJson(r, http::k202, o.get());
}

void addState(cJSON* state) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_phase == Phase::Idle) return;
  cJSON* u = cJSON_AddObjectToObject(state, "update");
  cJSON_AddStringToObject(u, "phase", g_phase == Phase::Receiving ? "receiving"
                                      : g_phase == Phase::Staged  ? "staged"
                                                                  : "failed");
  cJSON_AddStringToObject(u, "source", g_fromGithub ? "github" : "upload");
  cJSON_AddNumberToObject(u, "done", static_cast<double>(g_done));
  cJSON_AddNumberToObject(u, "total", static_cast<double>(g_total));
  cJSON_AddStringToObject(u, "version", g_version.c_str());
  cJSON_AddStringToObject(u, "error", g_error.c_str());
}

void confirmBoot(bool healthy) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return;
  if (healthy) {
    ESP_LOGI(TAG, "update %s started fine: keeping it", esp_app_get_description()->version);
    esp_ota_mark_app_valid_cancel_rollback();
    return;
  }
  ESP_LOGE(TAG, "update failed its self-test: going back to the previous firmware");
  esp_ota_mark_app_invalid_rollback_and_reboot();
}

}  // namespace keyra::api::update
