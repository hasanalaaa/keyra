// REST endpoints of SPEC §5. Routing/auth policy lives in routes.cpp; this file
// turns requests into vault/actions/settings calls and back into JSON.
#include "handlers.hpp"

#include <algorithm>
#include <ctime>
#include <set>
#include <vector>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "handlers_fido.hpp"
#include "handlers_gen.hpp"
#include "handlers_net.hpp"
#include "http.hpp"
#include "keyra/ble.hpp"
#include "keyra/hid.hpp"
#include "keyra/io.hpp"
#include "keyra/settings.hpp"
#include "keyra/vault.hpp"
#include "runtime.hpp"
#include "trusted.hpp"
#include "validate.hpp"

namespace keyra::api {
namespace {

const char* TAG = "api";
using json::Field;
using vault::Status;

constexpr size_t kMaxImportBatch = 50;
constexpr size_t kMaxPassphraseBytes = 1024;  // generous; bounds PBKDF2 input work
constexpr UBaseType_t kSlowQueueLen = 2;

struct Ctx {
  httpd_req_t* r;
  Match match;
  bool session;
  std::string token;
  json::Ptr body;  // parsed JSON object, when the route takes one
  net::Via via = net::Via::Home;
};

// ---------- small helpers ----------

esp_err_t badRequest(httpd_req_t* r, const char* message) {
  return http::sendError(r, http::k400, "invalid", message);
}

// Maps vault failures onto the API's error vocabulary.
esp_err_t sendVaultError(httpd_req_t* r, Status s) {
  switch (s) {
    case Status::NotInitialized: return http::sendError(r, http::k409, "not_initialized", "Keyra is not set up yet");
    case Status::AlreadyInitialized: return http::sendError(r, http::k409, "already_initialized", "Keyra is already set up");
    case Status::Locked: return http::sendError(r, http::k401, "locked", "Vault is locked");
    case Status::WrongPassphrase: return http::sendError(r, http::k401, "wrong", "Wrong passphrase");
    case Status::RateLimited: return http::sendError(r, http::k429, "rate_limited", "Too many attempts");
    case Status::NotFound: return http::sendError(r, http::k404, "not_found", "No such entry");
    case Status::Invalid: return badRequest(r, "Invalid data");
    case Status::Full: return http::sendError(r, http::k507, "full", "Vault is full");
    case Status::Corrupt: return http::sendError(r, http::k500, "corrupt", "Stored data is corrupt");
    case Status::StorageError:
    case Status::Ok: break;
  }
  return http::sendError(r, http::k500, "storage", "Storage error");
}

esp_err_t sendAwaitingButton(httpd_req_t* r, int64_t expiresIn) {
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(expiresIn));
  return http::sendJson(r, http::k202, o.get());
}

esp_err_t sendBusy(httpd_req_t* r) {
  return http::sendError(r, http::k409, "busy",
                         "Keyra is waiting for another request; long-press its button to cancel it");
}

esp_err_t sendId(httpd_req_t* r, const char* status, uint32_t id) {
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddNumberToObject(o.get(), "id", id);
  return http::sendJson(r, status, o.get());
}

esp_err_t sendRetry(httpd_req_t* r, const char* status, const char* code, const char* message, uint32_t retryMs) {
  json::Ptr o(http::errorBody(code, message));
  cJSON_AddNumberToObject(o.get(), "retryAfterMs", retryMs);
  return http::sendJson(r, status, o.get());
}

// Reads a required string field; on failure the error response is already sent.
bool requireString(Ctx& c, const char* key, std::string& out, esp_err_t& err) {
  if (json::getString(c.body.get(), key, out) == Field::Ok) return true;
  std::string msg = std::string("\"") + key + "\" (string) is required";
  err = badRequest(c.r, msg.c_str());
  return false;
}

void addEntrySummary(cJSON* o, const vault::Entry& e) {
  cJSON_AddNumberToObject(o, "id", e.id);
  cJSON_AddStringToObject(o, "title", e.title.c_str());
  cJSON_AddStringToObject(o, "url", e.url.c_str());
  cJSON_AddStringToObject(o, "username", e.username.c_str());
  cJSON_AddBoolToObject(o, "favorite", e.favorite);
  cJSON_AddBoolToObject(o, "hasPassword", !e.password.empty());
  cJSON_AddBoolToObject(o, "hasTotp", !e.totp.empty());
  cJSON_AddNumberToObject(o, "updated", static_cast<double>(e.updated));
  cJSON_AddNumberToObject(o, "lastUsed", static_cast<double>(e.lastUsed));
}

// Copies the entry fields present in `src` onto `e`. Timestamps are accepted only
// when creating/importing (to keep history from other managers).
bool readEntry(const cJSON* src, vault::Entry& e, bool withTimestamps, std::string& err) {
  struct StrField {
    const char* key;
    std::string* dst;
  } fields[] = {{"title", &e.title}, {"url", &e.url},   {"username", &e.username},
                {"password", &e.password}, {"totp", &e.totp}, {"notes", &e.notes}};
  for (const StrField& f : fields) {
    if (json::getString(src, f.key, *f.dst) == Field::BadType) {
      err = std::string("\"") + f.key + "\" must be a string";
      return false;
    }
  }
  if (json::getBool(src, "favorite", e.favorite) == Field::BadType) {
    err = "\"favorite\" must be a boolean";
    return false;
  }
  if (!withTimestamps) return true;
  struct IntField {
    const char* key;
    int64_t* dst;
  } times[] = {{"created", &e.created}, {"updated", &e.updated}, {"lastUsed", &e.lastUsed}};
  for (const IntField& f : times) {
    if (json::getInt(src, f.key, 0, INT64_C(1) << 40, *f.dst) == Field::BadType) {
      err = std::string("\"") + f.key + "\" must be unix seconds";
      return false;
    }
  }
  return true;
}

const char* outputName(settings::Output o) {
  switch (o) {
    case settings::Output::Usb: return "usb";
    case settings::Output::Ble: return "ble";
    case settings::Output::Auto: break;
  }
  return "auto";
}

std::optional<settings::Output> parseOutput(const std::string& s) {
  if (s == "auto") return settings::Output::Auto;
  if (s == "usb") return settings::Output::Usb;
  if (s == "ble") return settings::Output::Ble;
  return std::nullopt;
}

ble::Connect toBle(settings::BleConnect c) {
  return c == settings::BleConnect::Always ? ble::Connect::Always : ble::Connect::OnDemand;
}

std::vector<Bond> bondsSeen(const ble::Status& st) {
  std::vector<Bond> out;
  for (const ble::Peer& p : st.bonds) out.push_back({p.addr, p.lastSeen});
  return out;
}

// Where a new action would type now, with nothing named in the request.
Target defaultTarget(const settings::Settings& s, const ble::Status& st) {
  return pickTarget(s.output, s.bleEnabled, hid::mounted(), bondsSeen(st), ble::linked());
}

void addTarget(cJSON* o, const char* key, const Target& t) {
  switch (t.kind) {
    case Target::Kind::Usb: cJSON_AddStringToObject(o, key, "usb"); break;
    case Target::Kind::Ble: cJSON_AddStringToObject(o, key, ble::formatAddr(t.addr).c_str()); break;
    case Target::Kind::None: cJSON_AddNullToObject(o, key); break;
  }
}

std::string bondName(const ble::Status& st, const BtAddr& a) {
  for (const ble::Peer& p : st.bonds) {
    if (p.addr == a) return p.name;
  }
  return {};
}

std::string dedupeKey(const vault::Entry& e) { return e.title + '\x1f' + e.username + '\x1f' + e.url; }

cJSON* settingsJson(const settings::Settings& s) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "deviceName", s.deviceName.c_str());
  cJSON_AddStringToObject(o, "wifiSsid", settings::ssid(s).c_str());
  cJSON_AddNumberToObject(o, "autoLockMin", s.autoLockMin);
  cJSON_AddNumberToObject(o, "keyDelayMs", s.keyDelayMs);
  cJSON_AddStringToObject(o, "bothSeparator", s.bothSeparator == settings::Separator::Enter ? "enter" : "tab");
  cJSON_AddBoolToObject(o, "submitAfterBoth", s.submitAfterBoth);
  cJSON_AddNumberToObject(o, "ledBrightness", s.ledBrightness);
  cJSON_AddBoolToObject(o, "bleEnabled", s.bleEnabled);
  cJSON_AddStringToObject(o, "output", outputName(s.output));
  cJSON_AddStringToObject(o, "bleConnect", s.bleConnect == settings::BleConnect::Always ? "always" : "on_demand");
  netapi::addSettings(o, s);
  return o;
}

std::string macString() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char buf[18];
  snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

// ---------- presence-op payloads (wiped when dropped or done) ----------

struct SetupJob {
  json::Secret passphrase, wifiPassword;
  std::string deviceName;
};

bool commitSetup(SetupJob& j) {
  const Status st = vault::setup(j.passphrase.s);
  if (st != Status::Ok) {
    ESP_LOGE(TAG, "vault setup: %s", vault::statusName(st));
    return false;
  }
  settings::Settings s = settings::get();
  s.wifiPassword = j.wifiPassword.s;
  if (!j.deviceName.empty()) s.deviceName = j.deviceName;
  if (settings::save(s) != ESP_OK) return false;
  ble::setName(s.deviceName);
  sessions().activity(monoMs());  // the vault is left unlocked for the client's unlock call
  reconfigureNetSoon();
  return true;
}

struct WifiJob {
  std::string ssid;  // empty = keep
  json::Secret password;  // empty = keep
};

bool commitWifi(WifiJob& j) {
  settings::Settings s = settings::get();
  if (!j.ssid.empty()) s.wifiSsid = j.ssid == settings::defaultSsid() ? std::string() : j.ssid;
  if (!j.password.s.empty()) s.wifiPassword = j.password.s;
  if (settings::save(s) != ESP_OK) return false;
  reconfigureNetSoon();
  return true;
}

struct RestoreJob {
  json::Secret passphrase, backup;
};

bool commitRestoreReplace(RestoreJob& j) {
  size_t added = 0, updated = 0;
  const Status st = vault::importBackup(j.passphrase.s, j.backup.s, true, &added, &updated);
  if (st != Status::Ok) {
    ESP_LOGE(TAG, "restore(replace): %s", vault::statusName(st));
    return false;
  }
  ESP_LOGI(TAG, "restore(replace): %u added", unsigned(added));
  return true;
}

[[noreturn]] void commitFactoryReset() {
  lockAll();
  // A reset Keyra may be given away: no computer it knew may reconnect.
  const esp_err_t berr = ble::forgetAll();
  if (berr != ESP_OK) ESP_LOGE(TAG, "forgetting Bluetooth hosts: %s", esp_err_to_name(berr));
  const Status st = vault::factoryReset();
  if (st != Status::Ok) ESP_LOGE(TAG, "vault factory reset: %s", vault::statusName(st));
  const esp_err_t err = settings::erase();
  if (err != ESP_OK) ESP_LOGE(TAG, "settings erase: %s", esp_err_to_name(err));
  safeRestart();
}

// ---------- endpoints ----------

esp_err_t getState(Ctx& c) {
  const settings::Settings s = settings::get();
  json::Ptr o(cJSON_CreateObject());
  cJSON* dev = cJSON_AddObjectToObject(o.get(), "device");
  cJSON_AddStringToObject(dev, "name", s.deviceName.c_str());
  cJSON_AddStringToObject(dev, "version", esp_app_get_description()->version);
  cJSON_AddStringToObject(dev, "model", "ESP32-S3");
  cJSON_AddStringToObject(dev, "mac", macString().c_str());
  cJSON_AddBoolToObject(o.get(), "initialized", vault::initialized());
  cJSON_AddBoolToObject(o.get(), "unlocked", vault::unlocked());
  cJSON_AddBoolToObject(o.get(), "session", c.session);
  cJSON_AddNumberToObject(o.get(), "autoLockMin", s.autoLockMin);
  // Titles of pending/finished actions are only shown to an unlocked session.
  const auto pending = c.session ? machine().pending() : std::nullopt;
  const ble::Status bst = ble::status();
  const Target next = defaultTarget(s, bst);
  const Target* armed = pending ? &pending->req.target : nullptr;
  const bool armedBle = armed != nullptr && armed->kind == Target::Kind::Ble;
  const bool linkUp = armedBle && hid::bleConnected() && ble::linked() == armed->addr;
  cJSON* host = cJSON_AddObjectToObject(o.get(), "host");
  cJSON_AddBoolToObject(host, "usb", hid::mounted());
  cJSON_AddBoolToObject(host, "ble", hid::bleConnected());  // a Bluetooth host is connected right now
  const Target& shown = armed != nullptr ? *armed : next;
  cJSON_AddBoolToObject(host, "capsLock", shown.kind == Target::Kind::Ble ? ble::capsLock() : hid::capsLock());
  // Kind of host a new action would use ("usb" | "ble" | null = none available).
  switch (next.kind) {
    case Target::Kind::Usb: cJSON_AddStringToObject(host, "output", "usb"); break;
    case Target::Kind::Ble: cJSON_AddStringToObject(host, "output", "ble"); break;
    case Target::Kind::None: cJSON_AddNullToObject(host, "output"); break;
  }
  if (armedBle) {  // the host the armed action will type into
    cJSON* bt = cJSON_AddObjectToObject(host, "bleTarget");
    cJSON_AddStringToObject(bt, "addr", ble::formatAddr(armed->addr).c_str());
    cJSON_AddStringToObject(bt, "name", bondName(bst, armed->addr).c_str());
  } else {
    cJSON_AddNullToObject(host, "bleTarget");
  }
  cJSON_AddBoolToObject(host, "connecting", armedBle && !linkUp);

  if (pending) {
    cJSON* p = cJSON_AddObjectToObject(o.get(), "pending");
    cJSON_AddStringToObject(p, "kind", "type");
    cJSON_AddNumberToObject(p, "id", pending->req.id);
    genapi::addTitle(p, pending->req.what, pending->req.title);
    cJSON_AddStringToObject(p, "what", actions::whatName(pending->req.what));
    cJSON_AddBoolToObject(p, "submit", pending->req.submit);
    cJSON_AddNumberToObject(p, "expiresIn", static_cast<double>(pending->expiresInMs));
    addTarget(p, "target", pending->req.target);
  } else {
    cJSON_AddNullToObject(o.get(), "pending");
  }
  const auto last = c.session ? machine().last() : std::nullopt;
  if (last) {
    cJSON* l = cJSON_AddObjectToObject(o.get(), "last");
    cJSON_AddBoolToObject(l, "ok", last->ok);
    cJSON_AddStringToObject(l, "code", actions::codeName(last->code));
    cJSON_AddNumberToObject(l, "at", static_cast<double>(last->agoMs));
    genapi::addTitle(l, last->what, last->title);
    cJSON_AddStringToObject(l, "what", actions::whatName(last->what));
  } else {
    cJSON_AddNullToObject(o.get(), "last");
  }
  const auto presence = machine().presence();
  cJSON* pr = cJSON_AddObjectToObject(o.get(), "presence");
  cJSON_AddBoolToObject(pr, "awaiting", presence && presence->awaiting);
  if (presence) {
    cJSON_AddStringToObject(pr, "op", actions::opName(presence->op));
  } else {
    cJSON_AddNullToObject(pr, "op");
  }
  cJSON_AddNumberToObject(pr, "expiresIn", presence ? static_cast<double>(presence->expiresInMs) : 0);
  if (const auto res = machine().opResult()) {
    cJSON* rj = cJSON_AddObjectToObject(pr, "result");
    cJSON_AddStringToObject(rj, "op", actions::opName(res->op));
    cJSON_AddBoolToObject(rj, "ok", res->code == actions::OpCode::Done);
    cJSON_AddStringToObject(rj, "code", actions::opCodeName(res->code));
    cJSON_AddNumberToObject(rj, "at", static_cast<double>(res->agoMs));
  } else {
    cJSON_AddNullToObject(pr, "result");
  }
  netapi::addState(o.get(), c.via);
  cJSON_AddBoolToObject(o.get(), "timeValid", timeValid());
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t postSetup(Ctx& c) {
  if (vault::initialized()) return sendVaultError(c.r, Status::AlreadyInitialized);
  auto job = std::make_shared<SetupJob>();
  esp_err_t err = ESP_OK;
  if (!requireString(c, "passphrase", job->passphrase.s, err) ||
      !requireString(c, "wifiPassword", job->wifiPassword.s, err))
    return err;
  if (json::getString(c.body.get(), "deviceName", job->deviceName) == Field::BadType ||
      (cJSON_HasObjectItem(c.body.get(), "deviceName") && !validate::deviceName(job->deviceName)))
    return badRequest(c.r, "deviceName must be 1-32 bytes without control characters");
  if (!validate::passphrase(job->passphrase.s)) return badRequest(c.r, "passphrase must be 10-128 characters");
  if (!validate::wifiPassword(job->wifiPassword.s))
    return badRequest(c.r, "wifiPassword must be 8-63 printable ASCII characters and not the default");
  const auto expires = machine().tryAwaitPresence(actions::Op::Setup, [job] { return commitSetup(*job); });
  if (!expires) return sendBusy(c.r);
  return sendAwaitingButton(c.r, *expires);
}

esp_err_t postUnlock(Ctx& c) {
  json::Secret pass;
  esp_err_t err = ESP_OK;
  if (!requireString(c, "passphrase", pass.s, err)) return err;
  if (pass.s.size() > kMaxPassphraseBytes) return badRequest(c.r, "passphrase too long");
  uint32_t retryMs = 0;
  const bool wasUnlocked = vault::unlocked();
  const Status st = vault::unlock(pass.s, &retryMs);
  if (st == Status::WrongPassphrase) return sendRetry(c.r, http::k401, "wrong", "Wrong passphrase", retryMs);
  if (st == Status::RateLimited) {
    char retryAfter[12];
    snprintf(retryAfter, sizeof retryAfter, "%lu", static_cast<unsigned long>((retryMs + 999) / 1000));
    httpd_resp_set_hdr(c.r, "Retry-After", retryAfter);
    return sendRetry(c.r, http::k429, "rate_limited", "Too many attempts", retryMs);
  }
  if (st != Status::Ok) return sendVaultError(c.r, st);

  // Home network (SPEC §8.2): the right passphrase is not enough until this
  // browser has been approved once with the button. Checked after the KDF so
  // the button is only asked for when the passphrase was right (and wrong
  // guesses still hit the rate limit).
  std::string ktToken;
  const uint32_t trustId = trust::recognise(c.r, ktToken);
  if (trust::needsApproval(c.via == net::Via::Home, trustId != 0)) {
    if (!wasUnlocked) vault::lock();
    return trust::requestApproval(c.r);
  }

  const Sessions::Issued s = sessions().create(monoMs(), trustId);
  const std::string cookie = "ks=" + s.token + "; HttpOnly; SameSite=Strict; Path=/";
  httpd_resp_set_hdr(c.r, "Set-Cookie", cookie.c_str());
  const std::string kt = "kt=" + ktToken + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=31536000";
  if (trustId != 0) httpd_resp_set_hdr(c.r, "Set-Cookie", kt.c_str());
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "csrf", s.csrf.c_str());
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t postLock(Ctx& c) {
  lockAll();
  httpd_resp_set_hdr(c.r, "Set-Cookie", "ks=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
  return http::sendEmpty(c.r, http::k204);
}

esp_err_t listEntries(Ctx& c) {
  std::vector<vault::Entry> all;
  const Status st = vault::list(all);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  // Streamed in chunks: a full cJSON tree of 1000 entries would not fit in RAM
  // on boards without PSRAM.
  httpd_resp_set_status(c.r, http::k200);
  httpd_resp_set_type(c.r, "application/json; charset=utf-8");
  httpd_resp_set_hdr(c.r, "Cache-Control", "no-store");
  http::securityHeaders(c.r);
  std::string buf = "{\"entries\":[";
  esp_err_t err = ESP_OK;
  for (size_t i = 0; i < all.size() && err == ESP_OK; ++i) {
    json::Ptr o(cJSON_CreateObject());
    addEntrySummary(o.get(), all[i]);
    char* text = cJSON_PrintUnformatted(o.get());
    if (!text) {
      err = ESP_ERR_NO_MEM;
      break;
    }
    if (i > 0) buf += ',';
    buf += text;
    cJSON_free(text);
    if (buf.size() >= 1024) {
      err = httpd_resp_send_chunk(c.r, buf.data(), buf.size());
      buf.clear();
    }
  }
  for (vault::Entry& e : all) vault::wipe(e);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "entries stream failed: %s", esp_err_to_name(err));
    return ESP_FAIL;  // headers are out; closing the socket is the only honest signal
  }
  buf += "]}";
  err = httpd_resp_send_chunk(c.r, buf.data(), buf.size());
  if (err == ESP_OK) err = httpd_resp_send_chunk(c.r, nullptr, 0);
  return err;
}

esp_err_t getEntry(Ctx& c) {
  vault::Entry e;
  const Status st = vault::get(c.match.id, e);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddNumberToObject(o.get(), "id", e.id);
  cJSON_AddStringToObject(o.get(), "title", e.title.c_str());
  cJSON_AddStringToObject(o.get(), "url", e.url.c_str());
  cJSON_AddStringToObject(o.get(), "username", e.username.c_str());
  cJSON_AddStringToObject(o.get(), "password", e.password.c_str());
  cJSON_AddStringToObject(o.get(), "totp", e.totp.c_str());
  cJSON_AddStringToObject(o.get(), "notes", e.notes.c_str());
  cJSON_AddBoolToObject(o.get(), "favorite", e.favorite);
  cJSON_AddNumberToObject(o.get(), "created", static_cast<double>(e.created));
  cJSON_AddNumberToObject(o.get(), "updated", static_cast<double>(e.updated));
  cJSON_AddNumberToObject(o.get(), "lastUsed", static_cast<double>(e.lastUsed));
  genapi::addHistory(o.get(), e);
  vault::wipe(e);
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t createEntry(Ctx& c) {
  vault::Entry e;
  std::string err;
  if (!readEntry(c.body.get(), e, true, err)) return badRequest(c.r, err.c_str());
  e.id = 0;
  const int64_t now = unixSecondsOrZero();
  if (e.created == 0) e.created = now;
  if (e.updated == 0) e.updated = now;
  const Status st = vault::put(e);
  const uint32_t id = e.id;
  vault::wipe(e);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  return sendId(c.r, http::k201, id);
}

esp_err_t updateEntry(Ctx& c) {
  vault::Entry e;
  Status st = vault::get(c.match.id, e);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  std::string err;
  if (!readEntry(c.body.get(), e, false, err)) {
    vault::wipe(e);
    return badRequest(c.r, err.c_str());
  }
  e.id = c.match.id;
  e.updated = unixSecondsOrZero();
  st = vault::put(e);
  vault::wipe(e);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  return sendId(c.r, http::k200, c.match.id);
}

esp_err_t deleteEntry(Ctx& c) {
  const Status st = vault::remove(c.match.id);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  return http::sendEmpty(c.r, http::k204);
}

esp_err_t importEntries(Ctx& c) {
  const cJSON* items = cJSON_GetObjectItemCaseSensitive(c.body.get(), "entries");
  if (!cJSON_IsArray(items)) return badRequest(c.r, "\"entries\" (array) is required");
  if (static_cast<size_t>(cJSON_GetArraySize(items)) > kMaxImportBatch)
    return badRequest(c.r, "at most 50 entries per request");

  std::set<std::string> seen;
  {
    std::vector<vault::Entry> existing;
    const Status st = vault::list(existing);
    if (st != Status::Ok) return sendVaultError(c.r, st);
    for (vault::Entry& e : existing) {
      seen.insert(dedupeKey(e));
      vault::wipe(e);
    }
  }
  size_t added = 0, skipped = 0;
  const int64_t now = unixSecondsOrZero();
  const cJSON* item = nullptr;
  cJSON_ArrayForEach(item, items) {
    vault::Entry e;
    std::string err;
    if (!cJSON_IsObject(item) || !readEntry(item, e, true, err) || !seen.insert(dedupeKey(e)).second) {
      vault::wipe(e);
      ++skipped;
      continue;
    }
    e.id = 0;
    if (e.created == 0) e.created = now;
    if (e.updated == 0) e.updated = now;
    const Status st = vault::put(e);
    vault::wipe(e);
    if (st == Status::Ok) {
      ++added;
    } else if (st == Status::Invalid) {
      ++skipped;
    } else {
      ESP_LOGW(TAG, "import stopped after %u: %s", unsigned(added), vault::statusName(st));
      return sendVaultError(c.r, st);
    }
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddNumberToObject(o.get(), "added", static_cast<double>(added));
  cJSON_AddNumberToObject(o.get(), "skipped", static_cast<double>(skipped));
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t entryTotp(Ctx& c) {
  vault::Entry e;
  const Status st = vault::get(c.match.id, e);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  const bool hasTotp = !e.totp.empty();
  char code[11] = {};
  int period = 0, remaining = 0;
  const bool ok = hasTotp && timeValid() && totp::code(e.totp, unixMs() / 1000, code, &period, &remaining);
  vault::wipe(e);
  if (!hasTotp) return http::sendError(c.r, http::k404, "not_found", "Entry has no 2FA secret");
  if (!timeValid()) return http::sendError(c.r, http::k409, "no_time", "Device clock is not set");
  if (!ok) return badRequest(c.r, "2FA secret is not valid");
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "code", code);
  cJSON_AddNumberToObject(o.get(), "period", period);
  cJSON_AddNumberToObject(o.get(), "remaining", remaining);
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t postType(Ctx& c) {
  actions::TypeRequest req;
  bool test = false;
  if (json::getBool(c.body.get(), "test", test) == Field::BadType) return badRequest(c.r, "\"test\" must be a boolean");
  // Where to type: named in the request ("usb" or a bonded device), else the default.
  const settings::Settings st = settings::get();
  const ble::Status bst = ble::status();
  Target target = defaultTarget(st, bst);
  std::string targetStr;
  const Field tf = json::getString(c.body.get(), "target", targetStr);
  if (tf == Field::BadType) return badRequest(c.r, "\"target\" must be \"usb\" or a device address");
  if (tf == Field::Ok) {
    const auto t = parseTarget(targetStr);
    if (!t) return badRequest(c.r, "\"target\" must be \"usb\" or a device address");
    if (t->kind == Target::Kind::Ble) {
      if (!st.bleEnabled) return http::sendError(c.r, http::k409, "ble_disabled", "Bluetooth is turned off");
      if (std::none_of(bst.bonds.begin(), bst.bonds.end(), [&](const ble::Peer& p) { return p.addr == t->addr; }))
        return http::sendError(c.r, http::k404, "not_found", "No such device");
    }
    target = *t;
  }
  if (cJSON_HasObjectItem(c.body.get(), "text")) {
    esp_err_t err = ESP_OK;
    if (!genapi::textRequest(c.r, c.body.get(), target, req, err)) return err;
  } else if (test) {
    req = {0, "Keyra test", actions::What::Test, false, target, nullptr};
  } else {
    int64_t id = 0;
    std::string whatStr;
    if (json::getInt(c.body.get(), "id", 1, UINT32_MAX, id) != Field::Ok)
      return badRequest(c.r, "\"id\" (entry id) is required");
    const auto what = json::getString(c.body.get(), "what", whatStr) == Field::Ok ? actions::parseWhat(whatStr)
                                                                                  : std::nullopt;
    if (!what) return badRequest(c.r, "\"what\" must be username, password, both or totp");
    bool submit = *what == actions::What::Both && settings::get().submitAfterBoth;
    if (json::getBool(c.body.get(), "submit", submit) == Field::BadType)
      return badRequest(c.r, "\"submit\" must be a boolean");

    vault::Entry e;
    const Status st = vault::get(static_cast<uint32_t>(id), e);
    if (st != Status::Ok) return sendVaultError(c.r, st);
    const bool missing = (*what == actions::What::Username && e.username.empty()) ||
                         (*what == actions::What::Password && e.password.empty()) ||
                         (*what == actions::What::Both && (e.username.empty() || e.password.empty())) ||
                         (*what == actions::What::Totp && e.totp.empty());
    req = {static_cast<uint32_t>(id), e.title, *what, submit, target, nullptr};
    vault::wipe(e);
    if (missing) return badRequest(c.r, "Entry has no value for that field");
    if (*what == actions::What::Totp && !timeValid())
      return http::sendError(c.r, http::k409, "no_time", "Device clock is not set");
  }
  const actions::Pending p = machine().arm(req);
  json::Ptr o(cJSON_CreateObject());
  cJSON* po = cJSON_AddObjectToObject(o.get(), "pending");
  cJSON_AddStringToObject(po, "kind", "type");
  cJSON_AddNumberToObject(po, "id", p.req.id);
  genapi::addTitle(po, p.req.what, p.req.title);
  cJSON_AddStringToObject(po, "what", actions::whatName(p.req.what));
  cJSON_AddBoolToObject(po, "submit", p.req.submit);
  cJSON_AddNumberToObject(po, "expiresIn", static_cast<double>(p.expiresInMs));
  addTarget(po, "target", p.req.target);
  return http::sendJson(c.r, http::k202, o.get());
}

esp_err_t postTypeCancel(Ctx& c) {
  machine().cancel();
  return http::sendEmpty(c.r, http::k204);
}

esp_err_t getSettings(Ctx& c) {
  json::Ptr o(settingsJson(settings::get()));
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t putSettings(Ctx& c) {
  const settings::Settings cur = settings::get();
  settings::Settings next = cur;
  const cJSON* b = c.body.get();
  int64_t n = 0;
  std::string str;
  Field f;

  if ((f = json::getString(b, "deviceName", str)) == Field::BadType || (f == Field::Ok && !validate::deviceName(str)))
    return badRequest(c.r, "deviceName must be 1-32 bytes without control characters");
  if (f == Field::Ok) next.deviceName = str;
  if ((f = json::getInt(b, "autoLockMin", settings::kMinAutoLockMin, settings::kMaxAutoLockMin, n)) == Field::BadType)
    return badRequest(c.r, "autoLockMin must be 1-120");
  if (f == Field::Ok) next.autoLockMin = static_cast<uint8_t>(n);
  if ((f = json::getInt(b, "keyDelayMs", settings::kMinKeyDelayMs, settings::kMaxKeyDelayMs, n)) == Field::BadType)
    return badRequest(c.r, "keyDelayMs must be 1-100");
  if (f == Field::Ok) next.keyDelayMs = static_cast<uint16_t>(n);
  if ((f = json::getString(b, "bothSeparator", str)) == Field::BadType ||
      (f == Field::Ok && str != "tab" && str != "enter"))
    return badRequest(c.r, "bothSeparator must be \"tab\" or \"enter\"");
  if (f == Field::Ok) next.bothSeparator = str == "enter" ? settings::Separator::Enter : settings::Separator::Tab;
  if (json::getBool(b, "submitAfterBoth", next.submitAfterBoth) == Field::BadType)
    return badRequest(c.r, "submitAfterBoth must be a boolean");
  if ((f = json::getInt(b, "ledBrightness", 0, settings::kMaxLedBrightness, n)) == Field::BadType)
    return badRequest(c.r, "ledBrightness must be 0-100");
  if (f == Field::Ok) next.ledBrightness = static_cast<uint8_t>(n);
  if (json::getBool(b, "bleEnabled", next.bleEnabled) == Field::BadType)
    return badRequest(c.r, "bleEnabled must be a boolean");
  if ((f = json::getString(b, "output", str)) == Field::BadType || (f == Field::Ok && !parseOutput(str)))
    return badRequest(c.r, "output must be \"auto\", \"usb\" or \"ble\"");
  if (f == Field::Ok) next.output = *parseOutput(str);
  if ((f = json::getString(b, "bleConnect", str)) == Field::BadType ||
      (f == Field::Ok && str != "on_demand" && str != "always"))
    return badRequest(c.r, "bleConnect must be \"on_demand\" or \"always\"");
  if (f == Field::Ok) next.bleConnect = str == "always" ? settings::BleConnect::Always : settings::BleConnect::OnDemand;
  if ((f = json::getString(b, "apMode", str)) == Field::BadType || (f == Field::Ok && str != "always" && str != "fallback"))
    return badRequest(c.r, "apMode must be \"always\" or \"fallback\"");
  if (f == Field::Ok) next.apMode = str == "fallback" ? net::ApMode::Fallback : net::ApMode::Always;
  // Joining a network changes who can reach Keyra, so it is button-gated there.
  if (cJSON_HasObjectItem(b, "homeWifi")) return badRequest(c.r, "home Wi-Fi changes go through PUT /api/wifi/home");

  auto wifi = std::make_shared<WifiJob>();
  if ((f = json::getString(b, "wifiSsid", wifi->ssid)) == Field::BadType || (f == Field::Ok && !validate::ssid(wifi->ssid)))
    return badRequest(c.r, "wifiSsid must be 1-32 bytes without control characters");
  if (wifi->ssid == settings::ssid(cur)) wifi->ssid.clear();
  if ((f = json::getString(b, "wifiPassword", wifi->password.s)) == Field::BadType ||
      (f == Field::Ok && !validate::wifiPassword(wifi->password.s)))
    return badRequest(c.r, "wifiPassword must be 8-63 printable ASCII characters and not the default");
  if (wifi->password.s == cur.wifiPassword) vault::wipe(wifi->password.s);

  if (settings::save(next) != ESP_OK) return http::sendError(c.r, http::k500, "storage", "Could not save settings");
  if (next.ledBrightness != cur.ledBrightness) io::brightness(next.ledBrightness);
  if (next.bleConnect != cur.bleConnect) ble::setConnect(toBle(next.bleConnect));
  if (next.bleEnabled != cur.bleEnabled) ble::setEnabled(next.bleEnabled);
  if (next.deviceName != cur.deviceName) ble::setName(next.deviceName);
  if (next.apMode != cur.apMode) netapi::apply();

  if (!wifi->ssid.empty() || !wifi->password.s.empty()) {
    const int64_t expires = machine().awaitPresence(actions::Op::Wifi, [wifi] { return commitWifi(*wifi); });
    return sendAwaitingButton(c.r, expires);
  }
  json::Ptr o(settingsJson(next));
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t postPassphrase(Ctx& c) {
  json::Secret current, next;
  esp_err_t err = ESP_OK;
  if (!requireString(c, "current", current.s, err) || !requireString(c, "next", next.s, err)) return err;
  if (current.s.size() > kMaxPassphraseBytes) return badRequest(c.r, "passphrase too long");
  if (!validate::passphrase(next.s)) return badRequest(c.r, "next must be 10-128 characters");
  const Status st = vault::changePassphrase(current.s, next.s);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  return http::sendEmpty(c.r, http::k204);
}

esp_err_t postBackup(Ctx& c) {
  json::Secret pass, out;
  esp_err_t err = ESP_OK;
  if (!requireString(c, "passphrase", pass.s, err)) return err;
  const long chars = validate::utf8Length(pass.s);
  if (chars < 12 || pass.s.size() > kMaxPassphraseBytes)
    return badRequest(c.r, "backup passphrase must be at least 12 characters");
  const Status st = vault::exportBackup(pass.s, out.s);
  if (st != Status::Ok) return sendVaultError(c.r, st);

  char disposition[96];
  const time_t now = static_cast<time_t>(unixMs() / 1000);
  tm utc{};
  gmtime_r(&now, &utc);
  snprintf(disposition, sizeof disposition, "attachment; filename=\"keyra-backup-%04d%02d%02d.json\"",
           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
  httpd_resp_set_status(c.r, http::k200);
  httpd_resp_set_type(c.r, "application/json");
  httpd_resp_set_hdr(c.r, "Content-Disposition", disposition);
  httpd_resp_set_hdr(c.r, "Cache-Control", "no-store");
  http::securityHeaders(c.r);
  return httpd_resp_send(c.r, out.s.data(), static_cast<ssize_t>(out.s.size()));
}

esp_err_t postRestore(Ctx& c) {
  auto job = std::make_shared<RestoreJob>();
  esp_err_t err = ESP_OK;
  std::string mode;
  if (!requireString(c, "passphrase", job->passphrase.s, err) || !requireString(c, "mode", mode, err)) return err;
  if (mode != "merge" && mode != "replace") return badRequest(c.r, "mode must be \"merge\" or \"replace\"");
  const cJSON* backup = cJSON_GetObjectItemCaseSensitive(c.body.get(), "backup");
  if (!cJSON_IsObject(backup)) return badRequest(c.r, "\"backup\" (object) is required");
  char* text = cJSON_PrintUnformatted(backup);
  if (!text) return http::sendError(c.r, http::k500, "no_memory", "Out of memory");
  job->backup.s.assign(text);
  cJSON_free(text);

  if (mode == "replace") {
    const int64_t expires =
        machine().awaitPresence(actions::Op::RestoreReplace, [job] { return commitRestoreReplace(*job); });
    return sendAwaitingButton(c.r, expires);
  }
  size_t added = 0, updated = 0;
  const Status st = vault::importBackup(job->passphrase.s, job->backup.s, false, &added, &updated);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddNumberToObject(o.get(), "added", static_cast<double>(added));
  cJSON_AddNumberToObject(o.get(), "updated", static_cast<double>(updated));
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t postFactoryReset(Ctx& c) {
  const auto expires = machine().tryAwaitPresence(actions::Op::FactoryReset, [] {
    commitFactoryReset();
    return true;
  });
  if (!expires) return sendBusy(c.r);
  return sendAwaitingButton(c.r, *expires);
}

void addPeer(cJSON* o, const ble::Peer& p) {
  cJSON_AddStringToObject(o, "addr", ble::formatAddr(p.addr).c_str());
  cJSON_AddStringToObject(o, "name", p.name.c_str());
}

esp_err_t getBle(Ctx& c) {
  const ble::Status st = ble::status();
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddBoolToObject(o.get(), "enabled", st.enabled);
  cJSON* pairing = cJSON_AddObjectToObject(o.get(), "pairing");
  cJSON_AddBoolToObject(pairing, "active", st.pairing);
  cJSON_AddNumberToObject(pairing, "expiresIn", static_cast<double>(st.pairingLeftMs));
  if (st.connected) {
    addPeer(cJSON_AddObjectToObject(o.get(), "connected"), *st.connected);
  } else {
    cJSON_AddNullToObject(o.get(), "connected");
  }
  cJSON* bonds = cJSON_AddArrayToObject(o.get(), "bonds");
  for (const ble::Peer& p : st.bonds) {
    cJSON* b = cJSON_CreateObject();
    addPeer(b, p);
    cJSON_AddNumberToObject(b, "lastSeen", static_cast<double>(p.lastSeen));
    cJSON_AddItemToArray(bonds, b);
  }
  return http::sendJson(c.r, http::k200, o.get());
}

// Refusals the user can act on, checked before asking for the button.
esp_err_t sendPairRefusal(httpd_req_t* r, ble::PairResult res) {
  switch (res) {
    case ble::PairResult::Disabled:
      return http::sendError(r, http::k409, "ble_disabled", "Bluetooth is turned off");
    case ble::PairResult::BondsFull:
      return http::sendError(r, http::k409, "bonds_full", "Keyra already knows 4 devices; forget one first");
    case ble::PairResult::Unavailable:
    case ble::PairResult::Ok: break;
  }
  return http::sendError(r, http::k503, "ble_unavailable", "Bluetooth is not available");
}

esp_err_t postBlePair(Ctx& c) {
  const ble::PairResult now = ble::canPair();
  if (now != ble::PairResult::Ok) return sendPairRefusal(c.r, now);
  const int64_t expires =
      machine().awaitPresence(actions::Op::BlePair, [] { return ble::openPairing() == ble::PairResult::Ok; });
  return sendAwaitingButton(c.r, expires);
}

esp_err_t deleteBleBond(Ctx& c) {
  const esp_err_t err = ble::forget(c.match.addr);
  if (err == ESP_OK) return http::sendEmpty(c.r, http::k204);
  if (err == ESP_ERR_NOT_FOUND) return http::sendError(c.r, http::k404, "not_found", "No such device");
  ESP_LOGE(TAG, "forget bond: %s", esp_err_to_name(err));
  return http::sendError(c.r, http::k500, "ble_failed", "Bluetooth did not respond");
}

bool takesBody(Route r) {
  switch (r) {
    case Route::Setup: case Route::Unlock: case Route::CreateEntry: case Route::UpdateEntry:
    case Route::ImportEntries: case Route::Type: case Route::PutSettings: case Route::Passphrase:
    case Route::Backup: case Route::Restore: case Route::WifiHome: case Route::Generate:
      return true;
    default:
      return false;
  }
}

bool isSlow(Route r) {
  // WifiScan blocks for seconds while the radio scans.
  return r == Route::Unlock || r == Route::Passphrase || r == Route::Backup || r == Route::Restore ||
         r == Route::WifiScan;
}

esp_err_t dispatch(Ctx& c);

QueueHandle_t g_slowQueue = nullptr;

void slowWorker(void*) {
  for (;;) {
    Ctx* job = nullptr;
    xQueueReceive(g_slowQueue, &job, portMAX_DELAY);
    httpd_req_t* r = job->r;
    if (dispatch(*job) != ESP_OK) ESP_LOGW(TAG, "slow request failed to send");
    delete job;  // wipes the parsed body (cJSON frees are zeroised)
    httpd_req_async_handler_complete(r);
  }
}

// Hands the request to slowWorker; the reply is sent from there.
esp_err_t deferToWorker(Ctx& c) {
  httpd_req_t* copy = nullptr;
  if (httpd_req_async_handler_begin(c.r, &copy) != ESP_OK)
    return http::sendError(c.r, http::k500, "no_memory", "Out of memory");
  Ctx* job = new Ctx{copy, c.match, c.session, std::move(c.token), std::move(c.body), c.via};
  if (xQueueSend(g_slowQueue, &job, 0) != pdTRUE) {
    delete job;
    http::sendError(copy, http::k503, "busy", "Keyra is busy; try again");
    httpd_req_async_handler_complete(copy);
  }
  return ESP_OK;
}

esp_err_t dispatch(Ctx& c) {
  switch (c.match.route) {
    case Route::State: return getState(c);
    case Route::Setup: return postSetup(c);
    case Route::Unlock: return postUnlock(c);
    case Route::Lock: return postLock(c);
    case Route::ListEntries: return listEntries(c);
    case Route::CreateEntry: return createEntry(c);
    case Route::ImportEntries: return importEntries(c);
    case Route::GetEntry: return getEntry(c);
    case Route::UpdateEntry: return updateEntry(c);
    case Route::DeleteEntry: return deleteEntry(c);
    case Route::EntryTotp: return entryTotp(c);
    case Route::Type: return postType(c);
    case Route::TypeCancel: return postTypeCancel(c);
    case Route::GetSettings: return getSettings(c);
    case Route::PutSettings: return putSettings(c);
    case Route::Passphrase: return postPassphrase(c);
    case Route::Backup: return postBackup(c);
    case Route::Restore: return postRestore(c);
    case Route::FactoryReset: return postFactoryReset(c);
    case Route::GetBle: return getBle(c);
    case Route::BlePair: return postBlePair(c);
    case Route::BleForget: return deleteBleBond(c);
    case Route::WifiScan: return netapi::getScan(c.r);
    case Route::WifiHome: return netapi::putHome(c.r, c.body.get());
    case Route::ListTrusted: return trust::sendList(c.r);
    case Route::DeleteTrusted: return trust::revoke(c.r, c.match.id);
    case Route::ListPasskeys: return fidoapi::list(c.r);
    case Route::DeletePasskey: return fidoapi::remove(c.r, c.match.id);
    case Route::Generate: return genapi::postGenerate(c.r, c.body.get());
  }
  return http::sendError(c.r, http::k404, "not_found", "No such endpoint");
}

}  // namespace

esp_err_t handleApi(httpd_req_t* r, Method method, std::string_view path) {
  const bool restore = path == "/api/restore";
  const size_t cap = !restore ? http::kMaxBody
                     : heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? http::kMaxRestoreBodyPsram
                                                                      : http::kMaxRestoreBodyInternal;
  if (r->content_len > cap) {
    http::sendError(r, http::k413, "too_large", "Request body too large");
    return ESP_FAIL;  // closes the socket instead of draining an oversized body
  }
  Ctx c{r, matchApi(method, path), false, {}, nullptr, net::viaForSocket(httpd_req_to_sockfd(r))};
  if (c.match.kind == Match::Kind::NotFound) return http::sendError(r, http::k404, "not_found", "No such endpoint");
  if (c.match.kind == Match::Kind::MethodNotAllowed)
    return http::sendError(r, http::k405, "method_not_allowed", "Method not allowed");

  if (method != Method::Get) {
    const bool hasOrigin = httpd_req_get_hdr_value_len(r, "Origin") > 0;
    if (!isAllowedOrigin(http::header(r, "Origin", 128), hasOrigin, net::homeIp()))
      return http::sendError(r, http::k403, "csrf", "Cross-origin request refused");
  }

  // Session: cookie token must be live AND the vault unlocked.
  std::optional<std::string> csrf;
  char cookie[2 * kTokenBytes + 1] = {};
  size_t cookieLen = sizeof cookie;
  if (httpd_req_get_cookie_val(r, "ks", cookie, &cookieLen) == ESP_OK) {
    c.token = cookie;
    csrf = sessions().csrfFor(c.token, monoMs());
  }
  c.session = csrf.has_value() && vault::unlocked();
  if (needsSession(c.match.route)) {
    if (!c.session) {
      // Why a client got bounced to the unlock screen; no secrets in the line.
      ESP_LOGW(TAG, "401 %s %.*s: cookie=%s session=%s vault=%s", r->method == HTTP_GET ? "GET" : "write",
               int(path.size()), path.data(), c.token.empty() ? "none" : "sent", csrf ? "known" : "unknown",
               vault::unlocked() ? "unlocked" : "locked");
      return http::sendError(r, http::k401, "locked", "Vault is locked");
    }
    if (needsCsrf(method, c.match.route) &&
        !constantTimeEqual(http::header(r, "X-Keyra-CSRF", 2 * kTokenBytes), *csrf))
      return http::sendError(r, http::k403, "csrf", "Missing or invalid CSRF token");
    sessions().activity(monoMs());
  }

  if (takesBody(c.match.route)) {
    json::Secret raw;
    if (http::readBody(r, raw.s) != ESP_OK) return ESP_FAIL;
    c.body.reset(cJSON_ParseWithLength(raw.s.data(), raw.s.size()));
    if (!cJSON_IsObject(c.body.get())) return badRequest(r, "Body must be a JSON object");
  }
  return isSlow(c.match.route) ? deferToWorker(c) : dispatch(c);
}

esp_err_t startSlowWorker() {
  g_slowQueue = xQueueCreate(kSlowQueueLen, sizeof(Ctx*));
  if (!g_slowQueue) return ESP_ERR_NO_MEM;
  // PBKDF2 + AES-GCM and backup JSON (de)serialisation.
  if (xTaskCreate(slowWorker, "apislow", 12288, nullptr, 5, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  return ESP_OK;
}

}  // namespace keyra::api
