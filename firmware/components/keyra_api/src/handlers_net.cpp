#include "handlers_net.hpp"

#include <memory>

#include "esp_log.h"
#include "http.hpp"
#include "keyra/vault.hpp"
#include "runtime.hpp"
#include "validate.hpp"

namespace keyra::api::netapi {
namespace {

const char* TAG = "api.net";
using json::Field;

const char* apModeName(net::ApMode m) { return m == net::ApMode::Fallback ? "fallback" : "always"; }

// Owns the new password until the op commits or is dropped (then wiped).
struct HomeJob {
  bool enabled = false;
  std::string ssid;
  json::Secret password;  // empty = keep the stored one
};

bool commitHome(HomeJob& j) {
  settings::Settings s = settings::get();
  s.homeEnabled = j.enabled;
  if (!j.ssid.empty()) s.homeSsid = j.ssid;
  if (!j.password.s.empty()) s.homePassword = j.password.s;
  if (settings::save(s) != ESP_OK) return false;
  const esp_err_t err = net::setHome(settings::home(s));
  if (err != ESP_OK) ESP_LOGE(TAG, "applying home Wi-Fi: %s", esp_err_to_name(err));
  vault::wipe(s.homePassword);
  return err == ESP_OK;
}

esp_err_t badRequest(httpd_req_t* r, const char* message) { return http::sendError(r, http::k400, "invalid", message); }

}  // namespace

void addState(cJSON* state, net::Via via) {
  const net::Status st = net::status();
  const settings::Settings s = settings::get();
  cJSON* n = cJSON_AddObjectToObject(state, "net");
  cJSON* ap = cJSON_AddObjectToObject(n, "ap");
  cJSON_AddBoolToObject(ap, "on", st.apOn);
  cJSON_AddStringToObject(ap, "ssid", settings::ssid(s).c_str());
  cJSON_AddNumberToObject(ap, "clients", st.apClients);
  if (st.homeEnabled) {
    cJSON* h = cJSON_AddObjectToObject(n, "home");
    cJSON_AddBoolToObject(h, "enabled", true);
    cJSON_AddBoolToObject(h, "connected", st.homeConnected);
    cJSON_AddStringToObject(h, "ssid", st.homeSsid.c_str());
    if (st.homeConnected) {
      cJSON_AddStringToObject(h, "ip", st.homeIp.c_str());
      cJSON_AddNumberToObject(h, "rssi", st.rssi);
    } else {
      cJSON_AddNullToObject(h, "ip");
      cJSON_AddNullToObject(h, "rssi");
    }
  } else {
    cJSON_AddNullToObject(n, "home");
  }
  cJSON_AddStringToObject(n, "via", via == net::Via::Ap ? "ap" : "home");
}

void addSettings(cJSON* o, const settings::Settings& s) {
  cJSON* h = cJSON_AddObjectToObject(o, "homeWifi");
  cJSON_AddBoolToObject(h, "enabled", s.homeEnabled);
  cJSON_AddStringToObject(h, "ssid", s.homeSsid.c_str());
  cJSON_AddStringToObject(o, "apMode", apModeName(s.apMode));
}

void apply() {
  settings::Settings s = settings::get();
  const esp_err_t err = net::setHome(settings::home(s));
  if (err != ESP_OK) ESP_LOGE(TAG, "applying home Wi-Fi: %s", esp_err_to_name(err));
  vault::wipe(s.homePassword);
}

esp_err_t getScan(httpd_req_t* r) {
  std::vector<net::Network> list;
  const esp_err_t err = net::scan(list);
  if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE)
    return http::sendError(r, http::k503, "busy", "Keyra is busy with Wi-Fi; try again");
  if (err != ESP_OK) return http::sendError(r, http::k500, "scan_failed", "Wi-Fi scan failed");
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "networks");
  for (const net::Network& n : list) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "ssid", n.ssid.c_str());
    cJSON_AddNumberToObject(j, "rssi", n.rssi);
    cJSON_AddBoolToObject(j, "secure", n.secure);
    cJSON_AddNumberToObject(j, "channel", n.channel);
    cJSON_AddItemToArray(arr, j);
  }
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t putHome(httpd_req_t* r, const cJSON* body) {
  const settings::Settings cur = settings::get();
  auto job = std::make_shared<HomeJob>();
  if (json::getBool(body, "enabled", job->enabled) != Field::Ok) return badRequest(r, "\"enabled\" (boolean) is required");
  Field f = json::getString(body, "ssid", job->ssid);
  if (f == Field::BadType || (f == Field::Ok && !validate::ssid(job->ssid)))
    return badRequest(r, "ssid must be 1-32 bytes without control characters");
  f = json::getString(body, "password", job->password.s);
  if (f == Field::BadType || (f == Field::Ok && !validate::homePassword(job->password.s)))
    return badRequest(r, "password must be 8-63 printable ASCII characters");
  if (!job->enabled) {
    // Turning it off keeps the saved network for a later re-enable, untouched.
    job->ssid.clear();
    vault::wipe(job->password.s);
  } else {
    if (job->ssid.empty()) job->ssid = cur.homeSsid;
    if (job->ssid.empty()) return badRequest(r, "ssid is required");
    // Keyra only joins password-protected networks, so a new network needs one.
    if (job->password.s.empty() && (job->ssid != cur.homeSsid || cur.homePassword.empty()))
      return badRequest(r, "password is required for a new network");
  }
  const int64_t expires = machine().awaitPresence(actions::Op::HomeWifi, [job] { return commitHome(*job); });
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddStringToObject(o.get(), "op", "home_wifi");
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(expires));
  return http::sendJson(r, http::k202, o.get());
}

}  // namespace keyra::api::netapi
