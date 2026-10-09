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
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "handlers_agent.hpp"
#include "handlers_tags.hpp"
#include "handlers_fido.hpp"
#include "handlers_gen.hpp"
#include "handlers_kbd.hpp"
#include "handlers_net.hpp"
#include "handlers_protect.hpp"
#include "handlers_update.hpp"
#include "health.hpp"
#include "activity.hpp"
#include "http.hpp"
#include "keyra/ble.hpp"
#include "keyra/fido.hpp"
#include "keyra/hid.hpp"
#include "keyra/io.hpp"
#include "keyra/settings.hpp"
#include "keyra/vault.hpp"
#include "runtime.hpp"
#include "trusted.hpp"
#include "type_request.hpp"
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
  std::optional<tokens::Token> bearer;  // /api/agent/…: the access token (SPEC §17)
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
    case Status::PasskeysFull:
      return http::sendError(r, http::k409, "passkeys_full",
                             "The backup's passkeys do not fit: Keyra holds at most 50 passkeys and 4 passkey keys");
    case Status::StorageError:
    case Status::Ok: break;
  }
  return http::sendError(r, http::k500, "storage", "Storage error");
}

esp_err_t sendBusy(httpd_req_t* r) {
  return http::sendError(r, http::k409, "busy",
                         "Keyra is waiting for another request; long-press its button to cancel it");
}

// 202 with the secret that lets this requester, and only it, withdraw the op
// (POST /api/presence/cancel); 409 busy when someone else's item is waiting.
// `op` (when given) tells the client which press it is waiting for.
esp_err_t sendAwaitingButton(httpd_req_t* r, const std::optional<actions::Machine::Armed>& armed,
                             const char* op = nullptr) {
  if (!armed) return sendBusy(r);
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  if (op) cJSON_AddStringToObject(o.get(), "op", op);
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(armed->expiresIn));
  cJSON_AddStringToObject(o.get(), "cancel", armed->cancel.c_str());
  return http::sendJson(r, http::k202, o.get());
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
  cJSON_AddBoolToObject(o, "hasSequence", !e.sequence.empty());  // the sequence itself only when revealed
  cJSON_AddNumberToObject(o, "updated", static_cast<double>(e.updated));
  cJSON_AddNumberToObject(o, "lastUsed", static_cast<double>(e.lastUsed));
  cJSON_AddNumberToObject(o, "burnAfter", e.burnAfter);
}

// Copies the entry fields present in `src` onto `e`. Timestamps are accepted only
// when creating/importing (to keep history from other managers).
bool readEntry(const cJSON* src, vault::Entry& e, bool withTimestamps, std::string& err) {
  struct StrField {
    const char* key;
    std::string* dst;
  } fields[] = {{"title", &e.title}, {"url", &e.url},   {"username", &e.username},
                {"password", &e.password}, {"totp", &e.totp}, {"notes", &e.notes}, {"sequence", &e.sequence}};
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
  int64_t burn = e.burnAfter;
  if (json::getInt(src, "burnAfter", 0, vault::kMaxBurnAfter, burn) == Field::BadType) {
    err = "\"burnAfter\" must be 0-99";
    return false;
  }
  e.burnAfter = static_cast<uint8_t>(burn);
  if (!e.sequence.empty()) {
    const seq::Error se = seq::parse(e.sequence, nullptr);
    if (se != seq::Error::None) {
      err = std::string("sequence: ") + seq::message(se);
      return false;
    }
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
  cJSON_AddBoolToObject(o, "protectReveal", s.protectReveal);
  cJSON_AddBoolToObject(o, "passkeysInBackup", s.passkeysInBackup);
  cJSON_AddBoolToObject(o, "lockOnUsb", s.lockOnUsb);
  cJSON_AddBoolToObject(o, "lockOnBle", s.lockOnBle);
  cJSON_AddNumberToObject(o, "lastBackupAt", static_cast<double>(s.lastBackupAt));
  netapi::addSettings(o, s);
  kbdapi::addSettings(o, s);
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
  std::string name;
  if (settings::update([&](settings::Settings& s) {
        s.wifiPassword = j.wifiPassword.s;
        if (!j.deviceName.empty()) s.deviceName = j.deviceName;
        name = s.deviceName;
      }) != ESP_OK)
    return false;
  ble::setName(name);
  sessions().activity(monoMs());  // the vault is left unlocked for the client's unlock call
  reconfigureNetSoon();
  return true;
}

struct WifiJob {
  std::string ssid;  // empty = keep
  json::Secret password;  // empty = keep
};

bool commitWifi(WifiJob& j) {
  if (settings::update([&](settings::Settings& s) {
        if (!j.ssid.empty()) s.wifiSsid = j.ssid == settings::defaultSsid() ? std::string() : j.ssid;
        if (!j.password.s.empty()) s.wifiPassword = j.password.s;
      }) != ESP_OK)
    return false;
  reconfigureNetSoon();
  return true;
}

struct RestoreJob {
  json::Secret passphrase, backup;
};

// Restored passkeys must never sign with a counter below what the sites saw
// from the Keyra the backup came from. Raised whatever the restore's outcome
// once the backup was read: a failed one may still have added passkeys (merge)
// or finish at the next unlock (replace), and a higher counter is always safe.
void raiseCounter(const vault::PasskeyRestore& pk) {
  if (pk.present && !fido::raiseCounterAfterRestore(pk.counter))
    ESP_LOGE(TAG, "could not raise the FIDO signature counter past the backup's (%u)", unsigned(pk.counter));
}

bool commitRestoreReplace(RestoreJob& j) {
  size_t added = 0, updated = 0;
  vault::PasskeyRestore pk;
  const Status st = vault::importBackup(j.passphrase.s, j.backup.s, true, &added, &updated, &pk);
  raiseCounter(pk);
  if (st != Status::Ok) {
    ESP_LOGE(TAG, "restore(replace): %s", vault::statusName(st));
    return false;
  }
  ESP_LOGI(TAG, "restore(replace): %u added, %u passkeys", unsigned(added), unsigned(pk.added));
  activity::log(activity::Kind::Restore, 0, {}, 1, static_cast<uint32_t>(added));
  return true;
}

[[noreturn]] void commitFactoryReset() {
  lockAll(activity::LockWhy::Manual);  // the log goes with the vault a moment later
  // A reset Keyra may be given away: no computer it knew may reconnect.
  const esp_err_t berr = ble::forgetAll();
  if (berr != ESP_OK) ESP_LOGE(TAG, "forgetting Bluetooth hosts: %s", esp_err_to_name(berr));
  const Status st = vault::factoryReset();
  if (st != Status::Ok) ESP_LOGE(TAG, "vault factory reset: %s", vault::statusName(st));
  if (!fido::forgetAttestation()) ESP_LOGE(TAG, "forgetting the U2F attestation key failed");
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
  // The supply dipped below what the chip needs and it restarted: usually a phone's USB port
  // or a thin cable. The app says so; it explains a keyboard that keeps coming and going.
  cJSON_AddBoolToObject(dev, "powerDip", esp_reset_reason() == ESP_RST_BROWNOUT);
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
  if (c.session) cJSON_AddStringToObject(host, "usbOs", s.osUsb.c_str());
  if (c.session) update::addState(o.get());

  if (pending) {
    typereq::addPending(cJSON_AddObjectToObject(o.get(), "pending"), *pending);
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
  // Reveal grace left for this session (SPEC §12.3); 0 without a session.
  cJSON_AddNumberToObject(o.get(), "graceMs",
                          c.session ? static_cast<double>(sessions().graceLeft(c.token, monoMs(), Sessions::Grace::Reveal)) : 0);
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
  return sendAwaitingButton(c.r, machine().tryAwaitPresence(actions::Op::Setup, [job] { return commitSetup(*job); }));
}

// A new session for this browser: `ks` cookie (+ renewed `kt` when trusted) and the CSRF token.
// Records the unlock (and any wrong guesses before it) in the activity log;
// returns how many wrong guesses there were, for the app to point out.
uint32_t logUnlock(uint8_t how) {
  const uint32_t failed = vault::failedBeforeUnlock();
  if (failed > 0) activity::log(activity::Kind::FailedUnlocks, 0, {}, 0, failed);
  activity::log(activity::Kind::Unlock, 0, {}, how);
  return failed;
}

esp_err_t sendSession(httpd_req_t* r, uint32_t trustId, const std::string& ktToken, uint32_t failedBefore) {
  const Sessions::Issued s = sessions().create(monoMs(), trustId, vault::unlockGeneration());
  const std::string cookie = "ks=" + s.token + "; HttpOnly; SameSite=Strict; Path=/";
  httpd_resp_set_hdr(r, "Set-Cookie", cookie.c_str());
  const std::string kt = "kt=" + ktToken + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=31536000";
  if (trustId != 0) httpd_resp_set_hdr(r, "Set-Cookie", kt.c_str());
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "csrf", s.csrf.c_str());
  cJSON_AddNumberToObject(o.get(), "failedAttempts", failedBefore);
  return http::sendJson(r, http::k200, o.get());
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

  return sendSession(c.r, trustId, ktToken, logUnlock(0));
}

// Forgotten passphrase (SPEC §12.2): the recovery key sets a new passphrase and
// unlocks. Same rate limit and home-network trust rule as /api/unlock.
esp_err_t postUnlockRecovery(Ctx& c) {
  json::Secret hex, next;
  esp_err_t err = ESP_OK;
  if (!requireString(c, "recoveryKey", hex.s, err) || !requireString(c, "next", next.s, err)) return err;
  vault::RecoveryKey key{};
  if (!protect::parseRecoveryKey(hex.s, key)) return badRequest(c.r, "recoveryKey must be 40 hex characters");
  if (!validate::passphrase(next.s)) return badRequest(c.r, "next must be 10-128 characters");
  uint32_t retryMs = 0;
  std::string ktToken;
  const uint32_t trustId = trust::recognise(c.r, ktToken);
  Status st = Status::Ok;
  const bool approval = trust::needsApproval(c.via == net::Via::Home, trustId != 0);
  // Prove the key first, so the button is only asked for when it was right.
  st = approval ? vault::checkRecovery(key, &retryMs) : vault::recover(key, next.s, &retryMs);
  if (st == Status::Ok && approval) {
    for (volatile uint8_t& b : key) b = 0;
    return trust::requestApproval(c.r);
  }
  for (volatile uint8_t& b : key) b = 0;
  if (st == Status::WrongPassphrase) return sendRetry(c.r, http::k401, "wrong", "Wrong recovery key", retryMs);
  if (st == Status::RateLimited) return sendRetry(c.r, http::k429, "rate_limited", "Too many attempts", retryMs);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  ESP_LOGI(TAG, "unlocked with the recovery key; passphrase replaced");
  return sendSession(c.r, trustId, ktToken, logUnlock(1));
}

esp_err_t postLock(Ctx& c) {
  lockAll(activity::LockWhy::Manual);
  httpd_resp_set_hdr(c.r, "Set-Cookie", "ks=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
  return http::sendEmpty(c.r, http::k204);
}

// SPEC §15: newest first. There is deliberately no way to clear it from a session.
esp_err_t getActivity(Ctx& c) {
  std::vector<activity::Event> events;
  if (!activity::list(events)) {
    if (!vault::unlocked()) return sendVaultError(c.r, Status::Locked);
    return http::sendError(c.r, http::k500, "storage", "The activity log could not be read");
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "events");
  for (auto it = events.rbegin(); it != events.rend(); ++it) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "kind", activity::kindName(it->kind));
    cJSON_AddNumberToObject(j, "at", static_cast<double>(it->at));
    if (it->id) cJSON_AddNumberToObject(j, "id", it->id);
    if (it->n) cJSON_AddNumberToObject(j, "n", it->n);
    cJSON_AddNumberToObject(j, "detail", it->detail);
    if (!it->title.empty()) cJSON_AddStringToObject(j, "title", it->title.c_str());
    cJSON_AddItemToArray(arr, j);
  }
  cJSON_AddNumberToObject(o.get(), "max", static_cast<double>(activity::kMaxEvents));
  return http::sendJson(c.r, http::k200, o.get());
}

// SPEC §13: ids and flags only; the passwords never leave this function.
esp_err_t getHealth(Ctx& c) {
  std::vector<vault::Entry> all;
  const Status st = vault::list(all);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  std::vector<health::Item> items;
  items.reserve(all.size());
  for (const vault::Entry& e : all) {
    // The current password was set when the newest old one was replaced.
    items.push_back({e.id, e.password, e.history.empty() ? e.created : e.history.front().changedAt});
  }
  const int64_t now = unixSecondsOrZero();
  const health::Report rep = health::check(items, now);
  const int64_t rotateSince = settings::get().rotateSince;
  const std::vector<uint32_t> toChange = rotateSince > 0 ? health::notChangedSince(items, rotateSince)
                                                          : std::vector<uint32_t>{};
  items.clear();
  for (vault::Entry& e : all) vault::wipe(e);

  json::Ptr o(cJSON_CreateObject());
  cJSON_AddNumberToObject(o.get(), "checked", static_cast<double>(rep.checked));
  cJSON_AddBoolToObject(o.get(), "clock", now != 0);
  cJSON* weak = cJSON_AddArrayToObject(o.get(), "weak");
  for (const auto& [id, lv] : rep.weak) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "id", id);
    cJSON_AddNumberToObject(j, "level", lv);
    cJSON_AddItemToArray(weak, j);
  }
  cJSON* reused = cJSON_AddArrayToObject(o.get(), "reused");
  for (const std::vector<uint32_t>& g : rep.reused) {
    cJSON* arr = cJSON_CreateArray();
    for (uint32_t id : g) cJSON_AddItemToArray(arr, cJSON_CreateNumber(id));
    cJSON_AddItemToArray(reused, arr);
  }
  cJSON* old = cJSON_AddArrayToObject(o.get(), "old");
  for (const auto& [id, since] : rep.old) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "id", id);
    cJSON_AddNumberToObject(j, "since", static_cast<double>(since));
    cJSON_AddItemToArray(old, j);
  }
  if (rotateSince > 0) {
    cJSON* rot = cJSON_AddObjectToObject(o.get(), "rotate");
    cJSON_AddNumberToObject(rot, "since", static_cast<double>(rotateSince));
    cJSON* pending = cJSON_AddArrayToObject(rot, "pending");
    for (uint32_t id : toChange) cJSON_AddItemToArray(pending, cJSON_CreateNumber(id));
  }
  return http::sendJson(c.r, http::k200, o.get());
}

// SPEC §13.1: start or end "change every password"; answers like GET /api/health.
esp_err_t postHealthRotate(Ctx& c) {
  bool on = false;
  if (json::getBool(c.body.get(), "on", on) != Field::Ok) return badRequest(c.r, "\"on\" (boolean) is required");
  const int64_t now = unixSecondsOrZero();
  if (on && now == 0) return http::sendError(c.r, http::k409, "no_time", "Device clock is not set");
  const int64_t was = settings::get().rotateSince;
  if (settings::update([&](settings::Settings& s) { s.rotateSince = on ? now : 0; }) != ESP_OK)
    return http::sendError(c.r, http::k500, "storage", "Could not save settings");
  if (on) activity::log(activity::Kind::RotateStarted);
  else if (was > 0) activity::log(activity::Kind::RotateEnded);
  return getHealth(c);
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

// GET: secrets only inside the reveal grace (or with protection off).
// POST …/reveal: the same, but asks for the button first when needed (SPEC §12.3).
esp_err_t getEntry(Ctx& c, bool ask) {
  vault::Entry e;
  const Status st = vault::get(c.match.id, e);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  const bool revealed = protect::mayReveal(c.token);
  if (ask && !revealed) {
    vault::wipe(e);
    return protect::requestPress(c.r, actions::Op::Reveal, c.token);
  }
  if (ask) activity::log(activity::Kind::Revealed, e.id, e.title);
  json::Ptr o(cJSON_CreateObject());
  protect::addEntry(o.get(), e, revealed);
  // A custom sequence may hold literal secrets: it follows the password.
  if (revealed) kbdapi::addEntry(o.get(), e);
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

// Removes the account on the press. The vault may have locked, or the entry
// gone, while the press was awaited: then nothing happens and the op fails.
bool commitDeleteEntry(uint32_t id) {
  if (!vault::unlocked()) return false;
  std::string title;
  if (vault::Entry e; vault::get(id, e) == Status::Ok) {
    title = e.title;
    vault::wipe(e);
  } else {
    return false;
  }
  if (vault::remove(id) != Status::Ok) return false;
  activity::log(activity::Kind::EntryDeleted, id, title);
  return true;
}

// 202: the account is removed only when Keyra's button is pressed (SPEC §5).
esp_err_t deleteEntry(Ctx& c) {
  {
    vault::Entry e;
    const Status st = vault::get(c.match.id, e);
    vault::wipe(e);
    if (st != Status::Ok) return sendVaultError(c.r, st);
  }
  const uint32_t id = c.match.id;
  return sendAwaitingButton(c.r, machine().awaitPresence(actions::Op::DeleteEntry, [id] { return commitDeleteEntry(id); },
                                                         c.token),
                            actions::opName(actions::Op::DeleteEntry));
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

esp_err_t sendPending(httpd_req_t* r, const std::optional<actions::Pending>& pending) {
  if (!pending) return sendBusy(r);
  json::Ptr o(cJSON_CreateObject());
  typereq::addPending(cJSON_AddObjectToObject(o.get(), "pending"), *pending);
  return http::sendJson(r, http::k202, o.get());
}

esp_err_t postType(Ctx& c) {
  actions::TypeRequest req;
  bool test = false, probe = false;
  if (json::getBool(c.body.get(), "test", test) == Field::BadType) return badRequest(c.r, "\"test\" must be a boolean");
  if (json::getBool(c.body.get(), "probe", probe) == Field::BadType)
    return badRequest(c.r, "\"probe\" must be a boolean");
  Target target;
  esp_err_t err = ESP_OK;
  if (!typereq::readTarget(c.r, c.body.get(), target, err)) return err;
  if (cJSON_HasObjectItem(c.body.get(), "text")) {
    if (!genapi::textRequest(c.r, c.body.get(), target, req, err)) return err;
  } else if (test) {
    req = {0, "Keyra test", actions::What::Test, false, target, nullptr, nullptr, 0};
  } else if (probe) {  // Layout Doctor (SPEC §10.3)
    req = {0, "Keyboard check", actions::What::Probe, false, target, nullptr, nullptr, 0};
  } else {
    int64_t id = 0;
    std::string whatStr;
    if (json::getInt(c.body.get(), "id", 1, UINT32_MAX, id) != Field::Ok)
      return badRequest(c.r, "\"id\" (entry id) is required");
    const auto what = json::getString(c.body.get(), "what", whatStr) == Field::Ok ? actions::parseWhat(whatStr)
                                                                                  : std::nullopt;
    if (!what) return badRequest(c.r, "\"what\" must be username, password, both, totp or sequence");
    if (*what == actions::What::Sequence) {
      if (cJSON_HasObjectItem(c.body.get(), "submit"))
        return badRequest(c.r, "a sequence says itself whether to press Enter; \"submit\" is not allowed");
      vault::Entry e;
      const Status st = vault::get(static_cast<uint32_t>(id), e);
      if (st != Status::Ok) return sendVaultError(c.r, st);
      const bool ok = kbdapi::sequenceRequest(c.r, e, target, req, err);
      vault::wipe(e);
      if (!ok) return err;
      return sendPending(c.r, machine().arm(std::move(req), c.token));
    }
    if (!typereq::entryRequest(c.r, c.body.get(), static_cast<uint32_t>(id), *what, target, req, err)) return err;
    return sendPending(c.r, machine().arm(std::move(req), c.token));
  }
  bool switchLang = false;
  if (json::getBool(c.body.get(), "switchLang", switchLang) == Field::BadType)
    return badRequest(c.r, "\"switchLang\" must be a boolean");
  req.switchLang = switchLang;
  return sendPending(c.r, machine().arm(std::move(req), c.token));
}

// A "press Keyra's button" screen was cancelled (SPEC §5): the device must not
// run that op on a later press. Open like setup and factory reset, whose
// screens have no session; it can only withdraw a request, never make one.
esp_err_t postPresenceCancel(Ctx& c) {
  std::string op, token;
  if (json::getString(c.body.get(), "op", op) != Field::Ok || !actions::parseOp(op))
    return badRequest(c.r, "op must name a presence operation");
  if (json::getString(c.body.get(), "cancel", token) != Field::Ok || token.size() > 64)
    return badRequest(c.r, "cancel must be the token from the 202 answer");
  // Without the token handed to the requester, nothing happens: a stranger
  // could otherwise free the slot and arm their own op for the user's press.
  if (!machine().cancelPresence(*actions::parseOp(op), token))
    return http::sendError(c.r, http::k409, "not_cancelled", "Nothing of yours is waiting for the button");
  return http::sendEmpty(c.r, http::k204);
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
  const auto editing = settings::editLock();  // until save(): no other change can slip in between
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
  {
    esp_err_t err = ESP_OK;
    if (!kbdapi::readSettings(c.r, b, next, err)) return err;
  }
  if (json::getBool(b, "lockOnUsb", next.lockOnUsb) == Field::BadType) return badRequest(c.r, "lockOnUsb must be a boolean");
  if (json::getBool(b, "lockOnBle", next.lockOnBle) == Field::BadType) return badRequest(c.r, "lockOnBle must be a boolean");
  if (json::getBool(b, "protectReveal", next.protectReveal) == Field::BadType)
    return badRequest(c.r, "protectReveal must be a boolean");
  // Turning protection off would let a stolen session read everything, so that
  // one change waits for the button (turning it on applies at once).
  const bool unprotect = cur.protectReveal && !next.protectReveal;
  next.protectReveal = cur.protectReveal || next.protectReveal;
  // Likewise letting the passkeys leave in backups again (turning it off applies at once).
  if (json::getBool(b, "passkeysInBackup", next.passkeysInBackup) == Field::BadType)
    return badRequest(c.r, "passkeysInBackup must be a boolean");
  const bool passkeysOn = !cur.passkeysInBackup && next.passkeysInBackup;
  next.passkeysInBackup = cur.passkeysInBackup && next.passkeysInBackup;
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
    return sendAwaitingButton(c.r, machine().awaitPresence(actions::Op::Wifi, [wifi] { return commitWifi(*wifi); }, c.token));
  }
  if (unprotect) {
    return sendAwaitingButton(c.r, machine().awaitPresence(actions::Op::Unprotect, [] {
      return settings::update([](settings::Settings& s) { s.protectReveal = false; }) == ESP_OK;
    }, c.token));
  }
  if (passkeysOn) {
    return sendAwaitingButton(c.r, machine().awaitPresence(actions::Op::PasskeysBackupOn, [] {
      return settings::update([](settings::Settings& s) { s.passkeysInBackup = true; }) == ESP_OK;
    }, c.token));
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
  uint32_t retryMs = 0;
  const Status st = vault::changePassphrase(current.s, next.s, &retryMs);
  // Same answers as unlock, so the app can show the wait.
  if (st == Status::WrongPassphrase) return sendRetry(c.r, http::k401, "wrong", "Wrong passphrase", retryMs);
  if (st == Status::RateLimited) return sendRetry(c.r, http::k429, "rate_limited", "Too many attempts", retryMs);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  activity::log(activity::Kind::Passphrase);
  return http::sendEmpty(c.r, http::k204);
}

esp_err_t postBackup(Ctx& c) {
  json::Secret pass, out;
  esp_err_t err = ESP_OK;
  if (!requireString(c, "passphrase", pass.s, err)) return err;
  const long chars = validate::utf8Length(pass.s);
  if (chars < 12 || pass.s.size() > kMaxPassphraseBytes)
    return badRequest(c.r, "backup passphrase must be at least 12 characters");
  if (!vault::unlocked()) return sendVaultError(c.r, Status::Locked);
  // The whole vault leaves the device: a press first (SPEC §12.3); the client retries.
  if (!protect::mayBackup(c.token)) return protect::requestPress(c.r, actions::Op::Backup, c.token);
  // The setting, not the request, decides (docs/research/PASSKEY-BACKUP.md).
  const bool passkeys = settings::get().passkeysInBackup;
  uint32_t counter = 0;
  if (passkeys && !fido::signatureCounter(counter))
    return http::sendError(c.r, http::k500, "storage", "Could not read the passkey signature counter");
  const Status st = vault::exportBackup(pass.s, out.s, passkeys, counter);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  activity::log(activity::Kind::Backup);
  if (const int64_t now = unixSecondsOrZero(); now != 0) {
    if (settings::update([now](settings::Settings& s) { s.lastBackupAt = now; }) != ESP_OK)
      ESP_LOGW(TAG, "could not record the backup time");
  }

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
    // A wrong passphrase or a bad file is reported now, not after the press.
    if (const Status st = vault::checkBackup(job->passphrase.s, job->backup.s, true); st != Status::Ok)
      return sendVaultError(c.r, st);
    return sendAwaitingButton(
        c.r, machine().awaitPresence(actions::Op::RestoreReplace, [job] { return commitRestoreReplace(*job); }, c.token));
  }
  size_t added = 0, updated = 0;
  vault::PasskeyRestore pk;
  const Status st = vault::importBackup(job->passphrase.s, job->backup.s, false, &added, &updated, &pk);
  raiseCounter(pk);
  if (st != Status::Ok) return sendVaultError(c.r, st);
  activity::log(activity::Kind::Restore, 0, {}, 0, static_cast<uint32_t>(added + updated));
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddNumberToObject(o.get(), "added", static_cast<double>(added));
  cJSON_AddNumberToObject(o.get(), "updated", static_cast<double>(updated));
  cJSON_AddNumberToObject(o.get(), "passkeys", static_cast<double>(pk.added));
  return http::sendJson(c.r, http::k200, o.get());
}

esp_err_t postFactoryReset(Ctx& c) {
  return sendAwaitingButton(c.r, machine().tryAwaitPresence(actions::Op::FactoryReset, [] {
    commitFactoryReset();
    return true;
  }));
}

void addPeer(cJSON* o, const ble::Peer& p) {
  cJSON_AddStringToObject(o, "addr", ble::formatAddr(p.addr).c_str());
  cJSON_AddStringToObject(o, "name", p.name.c_str());
}

esp_err_t getBle(Ctx& c) {
  const ble::Status st = ble::status();
  const settings::Settings s = settings::get();
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
    cJSON_AddStringToObject(b, "os", hostos::name(hostos::get(s.osBle, p.addr)));
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
  return sendAwaitingButton(c.r, machine().awaitPresence(actions::Op::BlePair, [] {
    const bool ok = ble::openPairing() == ble::PairResult::Ok;
    if (ok) activity::log(activity::Kind::BlePairing);
    return ok;
  }, c.token));
}

// Saves (or, with Unknown, drops) a bond's operating system.
esp_err_t saveBleOs(const ble::Addr& addr, hostos::Os os) {
  return settings::update([&](settings::Settings& s) { s.osBle = hostos::set(s.osBle, addr, os); });
}

esp_err_t putBleOs(Ctx& c) {
  std::string str;
  hostos::Os os;
  if (json::getString(c.body.get(), "os", str) != Field::Ok || !hostos::parse(str, os))
    return badRequest(c.r, "os must be \"\", \"mac\", \"ios\", \"windows\", \"android\" or \"linux\"");
  const ble::Status st = ble::status();
  const bool bonded = std::any_of(st.bonds.begin(), st.bonds.end(),
                                  [&](const ble::Peer& p) { return p.addr == c.match.addr; });
  if (!bonded) return http::sendError(c.r, http::k404, "not_found", "No such device");
  if (saveBleOs(c.match.addr, os) != ESP_OK)
    return http::sendError(c.r, http::k500, "storage", "Could not save settings");
  return http::sendEmpty(c.r, http::k204);
}

esp_err_t deleteBleBond(Ctx& c) {
  std::string name = ble::formatAddr(c.match.addr);
  for (const ble::Peer& p : ble::status().bonds) {
    if (p.addr == c.match.addr && !p.name.empty()) name = p.name;
  }
  const esp_err_t err = ble::forget(c.match.addr);
  if (err == ESP_OK) {
    activity::log(activity::Kind::BleForgot, 0, name);
    if (saveBleOs(c.match.addr, hostos::Os::Unknown) != ESP_OK) ESP_LOGW(TAG, "dropping the device's system failed");
    return http::sendEmpty(c.r, http::k204);
  }
  if (err == ESP_ERR_NOT_FOUND) return http::sendError(c.r, http::k404, "not_found", "No such device");
  ESP_LOGE(TAG, "forget bond: %s", esp_err_to_name(err));
  return http::sendError(c.r, http::k500, "ble_failed", "Bluetooth did not respond");
}

bool takesBody(Route r) {
  switch (r) {
    case Route::Setup: case Route::Unlock: case Route::CreateEntry: case Route::UpdateEntry:
    case Route::ImportEntries: case Route::Type: case Route::PutSettings: case Route::Passphrase:
    case Route::Backup: case Route::Restore: case Route::WifiHome: case Route::Generate:
    case Route::BleSetOs:
    case Route::UnlockRecovery:
    case Route::PresenceCancel:
    case Route::HealthRotate:
    case Route::CreateToken:
    case Route::CreateTag: case Route::TagTap: case Route::TagStatus:
    case Route::AgentType: case Route::AgentSave: case Route::AgentGenerate: case Route::AgentMatch:
      return true;
    default:
      return false;
  }
}

bool isSlow(Route r) {
  // WifiScan blocks for seconds while the radio scans.
  // Update streams a whole firmware image into flash.
  return r == Route::Unlock || r == Route::UnlockRecovery || r == Route::Passphrase || r == Route::Backup || r == Route::Restore ||
         r == Route::WifiScan || r == Route::Update || r == Route::UpdateCheck;
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
  Ctx* job = new Ctx{copy, c.match, c.session, std::move(c.token), std::move(c.body), c.via, std::move(c.bearer)};
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
    case Route::Health: return getHealth(c);
    case Route::HealthRotate: return postHealthRotate(c);
    case Route::Activity: return getActivity(c);
    case Route::CreateEntry: return createEntry(c);
    case Route::ImportEntries: return importEntries(c);
    case Route::GetEntry: return getEntry(c, false);
    case Route::RevealEntry: return getEntry(c, true);
    case Route::UnlockRecovery: return postUnlockRecovery(c);
    case Route::GetRecovery: return protect::getRecovery(c.r);
    case Route::CreateRecovery: return protect::createRecovery(c.r, c.token);
    case Route::DeleteRecovery: return protect::deleteRecovery(c.r, c.token);
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
    case Route::BleSetOs: return putBleOs(c);
    case Route::PresenceCancel: return postPresenceCancel(c);
    case Route::WifiScan: return netapi::getScan(c.r);
    case Route::WifiHome: return netapi::putHome(c.r, c.body.get(), c.token);
    case Route::ListTrusted: return trust::sendList(c.r);
    case Route::DeleteTrusted: return trust::revoke(c.r, c.match.id);
    case Route::ListPasskeys: return fidoapi::list(c.r);
    case Route::DeletePasskey: return fidoapi::remove(c.r, c.match.id, c.token);
    case Route::Generate: return genapi::postGenerate(c.r, c.body.get());
    case Route::Keyboard: return kbdapi::getKeyboard(c.r);
    case Route::Update: return update::upload(c.r);
    case Route::UpdateCheck: return update::check(c.r);
    case Route::UpdateDownload: return update::download(c.r);
    case Route::UpdateApply: return update::apply(c.r, c.token);
    case Route::ListTokens: return agent::listTokens(c.r);
    case Route::CreateToken: return agent::createToken(c.r, c.body.get(), c.token);
    case Route::DeleteToken: return agent::deleteToken(c.r, c.match.id);
    case Route::AgentEntries:
    case Route::AgentType:
    case Route::AgentStatus:
    case Route::AgentCancel:
    case Route::AgentSave:
    case Route::AgentMatch:
    case Route::AgentGenerate: return agent::dispatch(c.r, c.match.route, *c.bearer, c.body.get());
    case Route::ListTags: return tagapi::listTags(c.r);
    case Route::CreateTag: return tagapi::createTag(c.r, c.body.get(), c.token);
    case Route::DeleteTag: return tagapi::deleteTag(c.r, c.match.id);
    case Route::TagTap: return tagapi::tap(c.r, c.body.get());
    case Route::TagStatus: return tagapi::status(c.r, c.body.get());
  }
  return http::sendError(c.r, http::k404, "not_found", "No such endpoint");
}

}  // namespace

namespace typereq {

bool readTarget(httpd_req_t* r, const cJSON* body, Target& out, esp_err_t& err) {
  const settings::Settings st = settings::get();
  const ble::Status bst = ble::status();
  out = defaultTarget(st, bst);
  std::string targetStr;
  const Field tf = json::getString(body, "target", targetStr);
  if (tf == Field::Missing) return true;
  const auto t = tf == Field::Ok ? parseTarget(targetStr) : std::nullopt;
  if (!t) {
    err = badRequest(r, "\"target\" must be \"usb\" or a device address");
    return false;
  }
  if (t->kind == Target::Kind::Ble) {
    if (!st.bleEnabled) {
      err = http::sendError(r, http::k409, "ble_disabled", "Bluetooth is turned off");
      return false;
    }
    if (std::none_of(bst.bonds.begin(), bst.bonds.end(), [&](const ble::Peer& p) { return p.addr == t->addr; })) {
      err = http::sendError(r, http::k404, "not_found", "No such device");
      return false;
    }
  }
  out = *t;
  return true;
}

bool entryRequest(httpd_req_t* r, const cJSON* body, uint32_t id, actions::What what, const Target& target,
                  actions::TypeRequest& out, esp_err_t& err) {
  bool submit = what == actions::What::Both && settings::get().submitAfterBoth;
  if (json::getBool(body, "submit", submit) == Field::BadType) {
    err = badRequest(r, "\"submit\" must be a boolean");
    return false;
  }
  bool switchLang = false;
  if (json::getBool(body, "switchLang", switchLang) == Field::BadType) {
    err = badRequest(r, "\"switchLang\" must be a boolean");
    return false;
  }
  vault::Entry e;
  const Status st = vault::get(id, e);
  if (st != Status::Ok) {
    err = sendVaultError(r, st);
    return false;
  }
  const bool missing = (what == actions::What::Username && e.username.empty()) ||
                       (what == actions::What::Password && e.password.empty()) ||
                       (what == actions::What::Both && (e.username.empty() || e.password.empty())) ||
                       (what == actions::What::Totp && e.totp.empty());
  out = {id, e.title, what, submit, target, nullptr, nullptr, 0};
  out.switchLang = switchLang;
  vault::wipe(e);
  if (missing) {
    err = badRequest(r, "Entry has no value for that field");
    return false;
  }
  if (what == actions::What::Totp && !timeValid()) {
    err = http::sendError(r, http::k409, "no_time", "Device clock is not set");
    return false;
  }
  return true;
}

void addPending(cJSON* po, const actions::Pending& p) {
  cJSON_AddStringToObject(po, "kind", "type");
  cJSON_AddNumberToObject(po, "id", p.req.id);
  genapi::addTitle(po, p.req.what, p.req.title);
  cJSON_AddStringToObject(po, "what", actions::whatName(p.req.what));
  cJSON_AddBoolToObject(po, "submit", p.req.submit);
  cJSON_AddNumberToObject(po, "expiresIn", static_cast<double>(p.expiresInMs));
  addTarget(po, "target", p.req.target);
  kbdapi::addPending(po, p.req);
  std::string by = agent::ownerName(p.owner);
  if (by.empty()) by = tagapi::ownerName(p.owner);
  if (!by.empty()) cJSON_AddStringToObject(po, "by", by.c_str());
  if (!p.req.host.empty()) cJSON_AddStringToObject(po, "host", p.req.host.c_str());
}

}  // namespace typereq

esp_err_t handleApi(httpd_req_t* r, Method method, std::string_view path) {
  const bool restore = path == "/api/restore";
  const size_t cap = path == "/api/update" ? update::maxImage()
                     : !restore ? http::kMaxBody
                     : heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 ? http::kMaxRestoreBodyPsram
                                                                      : http::kMaxRestoreBodyInternal;
  if (r->content_len > cap) {
    http::sendError(r, http::k413, "too_large", "Request body too large");
    return ESP_FAIL;  // closes the socket instead of draining an oversized body
  }
  Ctx c{r, matchApi(method, path), false, {}, nullptr, net::viaForSocket(httpd_req_to_sockfd(r)), std::nullopt};
  if (c.match.kind == Match::Kind::NotFound) return http::sendError(r, http::k404, "not_found", "No such endpoint");
  if (c.match.kind == Match::Kind::MethodNotAllowed)
    return http::sendError(r, http::k405, "method_not_allowed", "Method not allowed");

  if (method != Method::Get) {
    const bool hasOrigin = httpd_req_get_hdr_value_len(r, "Origin") > 0;
    if (!isAllowedOriginFor(c.match.route, http::header(r, "Origin", 128), hasOrigin, net::homeIp()))
      return http::sendError(r, http::k403, "csrf", "Cross-origin request refused");
  }

  // Access tokens (SPEC §17): a bearer token, never the session cookie or CSRF,
  // and not user activity (an agent must not keep the vault from auto-locking).
  if (isAgent(c.match.route)) {
    esp_err_t err = ESP_OK;
    c.bearer = agent::authenticate(r, err);
    if (!c.bearer) return err;
  }

  // Session: cookie token must be live AND the vault unlocked.
  std::optional<std::string> csrf;
  char cookie[2 * kTokenBytes + 1] = {};
  size_t cookieLen = sizeof cookie;
  if (httpd_req_get_cookie_val(r, "ks", cookie, &cookieLen) == ESP_OK) {
    c.token = cookie;
    csrf = sessions().csrfFor(c.token, monoMs(), vault::unlockGeneration());
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
    // What the app polls on its own (a 2FA code refreshing every 30 s, the
    // Bluetooth device list) is not the user being there: it would keep an
    // open account from ever auto-locking.
    if (c.match.route != Route::EntryTotp && c.match.route != Route::GetBle) sessions().activity(monoMs());
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
