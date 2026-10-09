#include "handlers_tags.hpp"

#include <map>
#include <mutex>

#include "activity.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "handlers_protect.hpp"
#include "http.hpp"
#include "keyra/vault.hpp"
#include "psa/crypto.h"
#include "runtime.hpp"
#include "tags.hpp"
#include "tokens.hpp"
#include "type_request.hpp"

namespace keyra::api::tagapi {
namespace {

const char* TAG = "tags";
using json::Field;
using tags::Tag;

// The tap a ticket follows, so the tap page can show "typed" or "expired".
struct LastTap {
  uint32_t serial = 0;
  uint32_t ticket = 0;
};

std::mutex g_mu;  // the stored record's read-modify-write, the maps below
// Taps per tag id; key 0 = every unknown tag, wrong secret or wrong MAC together.
tokens::RateLimit g_rate;
std::map<uint32_t, LastTap> g_last;
std::map<uint32_t, std::string> g_names;  // for state.pending.by, without a vault read per poll

bool sha256(std::string_view s, tags::Digest& out) {
  size_t len = 0;
  return psa_crypto_init() == PSA_SUCCESS &&
         psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(s.data()), s.size(), out.data(),
                          out.size(), &len) == PSA_SUCCESS &&
         len == out.size();
}

// One AES-128 block (ECB) with a volatile PSA key that lives for this call only.
bool aesBlock(bool encrypt, const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  if (psa_crypto_init() != PSA_SUCCESS) return false;
  psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&a, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&a, 128);
  psa_set_key_usage_flags(&a, encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&a, PSA_ALG_ECB_NO_PADDING);
  mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
  const bool imported = psa_import_key(&a, key, 16, &id) == PSA_SUCCESS;
  psa_reset_key_attributes(&a);
  if (!imported) return false;
  size_t len = 0;
  const psa_status_t st = encrypt ? psa_cipher_encrypt(id, PSA_ALG_ECB_NO_PADDING, in, 16, out, 16, &len)
                                  : psa_cipher_decrypt(id, PSA_ALG_ECB_NO_PADDING, in, 16, out, 16, &len);
  psa_destroy_key(id);
  return st == PSA_SUCCESS && len == 16;
}
bool aesEncrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) { return aesBlock(true, key, in, out); }
bool aesDecrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) { return aesBlock(false, key, in, out); }
const tags::Cipher kAes{aesEncrypt, aesDecrypt};

void wipeBytes(std::vector<uint8_t>& v) {
  for (volatile uint8_t& b : v) b = 0;
}

void remember(const tags::Store& s) {
  g_names.clear();
  for (const Tag& t : s.all()) g_names[t.id] = t.name;
}

// Caller holds g_mu.
vault::Status loadLocked(tags::Store& out) {
  std::vector<uint8_t> raw;
  const vault::Status st = vault::tagsRead(raw);
  if (st != vault::Status::Ok) return st;
  auto parsed = tags::Store::parse(raw.data(), raw.size());
  wipeBytes(raw);
  if (!parsed) {
    // Fails safe: no tag works until a new one is made (which rewrites it).
    ESP_LOGE(TAG, "stored tags unreadable; treating as none");
    out = tags::Store();
    return vault::Status::Ok;
  }
  out = std::move(*parsed);
  remember(out);
  return vault::Status::Ok;
}

vault::Status saveLocked(const tags::Store& s) {
  std::vector<uint8_t> raw = s.serialize();
  const vault::Status st = vault::tagsWrite(raw);
  wipeBytes(raw);
  if (st != vault::Status::Ok) ESP_LOGE(TAG, "saving tags: %s", vault::statusName(st));
  remember(s);
  return st;
}

esp_err_t badRequest(httpd_req_t* r, const char* message) { return http::sendError(r, http::k400, "invalid", message); }

esp_err_t sendRateLimited(httpd_req_t* r, int64_t retryMs) {
  const std::string secs = std::to_string((retryMs + 999) / 1000);
  httpd_resp_set_hdr(r, "Retry-After", secs.c_str());
  json::Ptr o(http::errorBody("rate_limited", "Too many taps; wait a moment"));
  cJSON_AddNumberToObject(o.get(), "retryAfterMs", static_cast<double>(retryMs));
  return http::sendJson(r, http::k429, o.get());
}

// Unknown tags, wrong secrets and wrong MACs share one budget.
esp_err_t refuseTag(httpd_req_t* r) {
  int64_t retry = 0;
  bool allowed;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    allowed = g_rate.allow(0, monoMs(), retry);
  }
  if (!allowed) return sendRateLimited(r, retry);
  return http::sendError(r, http::k401, "invalid_tag", "This tag is not known to Keyra, or was revoked");
}

std::string toHex(const uint8_t* p, size_t n) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(2 * n, '0');
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = kHex[p[i] >> 4];
    out[2 * i + 1] = kHex[p[i] & 0x0F];
  }
  return out;
}

void addTag(cJSON* o, const Tag& t) {
  cJSON_AddNumberToObject(o, "id", t.id);
  cJSON_AddStringToObject(o, "name", t.name.c_str());
  cJSON_AddStringToObject(o, "kind", tags::kindName(t.kind));
  cJSON_AddNumberToObject(o, "entry", t.entry);
  cJSON_AddStringToObject(o, "what", tags::whatName(t.what));
  if (t.target.empty()) {
    cJSON_AddNullToObject(o, "target");
  } else {
    cJSON_AddStringToObject(o, "target", t.target.c_str());
  }
  cJSON_AddNumberToObject(o, "created", static_cast<double>(t.created));
  cJSON_AddNumberToObject(o, "lastUsed", static_cast<double>(t.lastUsed));
  if (t.kind == tags::Kind::Secure) {
    cJSON_AddBoolToObject(o, "bound", t.bound);
    cJSON_AddNumberToObject(o, "counter", t.counter);
  }
}

actions::What toAction(tags::What w) { return *actions::parseWhat(tags::whatName(w)); }

uint8_t whatDetail(tags::What w) { return static_cast<uint8_t>(w); }  // same numbers as agent_armed

// Body of POST /api/tags; false when refused (the reply is sent).
bool readNew(httpd_req_t* r, const cJSON* body, Tag& t, esp_err_t& err) {
  std::string s;
  if (json::getString(body, "name", t.name) != Field::Ok || !tags::validName(t.name)) {
    err = badRequest(r, "\"name\" must be 1-48 bytes of text");
    return false;
  }
  const auto kind = json::getString(body, "kind", s) == Field::Ok ? tags::parseKind(s) : std::nullopt;
  if (!kind) {
    err = badRequest(r, "\"kind\" must be \"simple\" or \"secure\"");
    return false;
  }
  t.kind = *kind;
  const auto what = json::getString(body, "what", s) == Field::Ok ? tags::parseWhat(s) : std::nullopt;
  if (!what) {
    err = badRequest(r, "\"what\" must be username, password, both or totp");
    return false;
  }
  t.what = *what;
  int64_t entry = 0;
  if (json::getInt(body, "entry", 1, UINT32_MAX, entry) != Field::Ok) {
    err = badRequest(r, "\"entry\" (account id) is required");
    return false;
  }
  t.entry = static_cast<uint32_t>(entry);
  vault::Entry e;
  const vault::Status st = vault::get(t.entry, e);
  const bool missing = (t.what == tags::What::Username && e.username.empty()) ||
                       (t.what == tags::What::Password && e.password.empty()) ||
                       (t.what == tags::What::Both && (e.username.empty() || e.password.empty())) ||
                       (t.what == tags::What::Totp && e.totp.empty());
  vault::wipe(e);
  if (st != vault::Status::Ok) {
    err = st == vault::Status::NotFound ? badRequest(r, "\"entry\" names an account that does not exist")
                                        : http::sendError(r, http::k500, "storage", "Could not read the account");
    return false;
  }
  if (missing) {
    err = badRequest(r, "Entry has no value for that field");
    return false;
  }
  // target: absent = the device's default when tapped; else "usb" or a bonded device.
  const Field tf = json::getString(body, "target", t.target);
  if (tf == Field::BadType || !tags::validTarget(t.target)) {
    err = badRequest(r, "\"target\" must be \"usb\" or a device address");
    return false;
  }
  if (!t.target.empty()) {
    Target target;
    if (!typereq::readTarget(r, body, target, err)) return false;
  }
  return true;
}

}  // namespace

esp_err_t listTags(httpd_req_t* r) {
  tags::Store s;
  vault::Status st;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    st = loadLocked(s);
  }
  if (st != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not read the tags");
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "tags");
  for (const Tag& t : s.all()) {
    cJSON* j = cJSON_CreateObject();
    addTag(j, t);
    cJSON_AddItemToArray(arr, j);
  }
  cJSON_AddNumberToObject(o.get(), "max", static_cast<double>(tags::kMaxTags));
  return http::sendJson(r, http::k200, o.get());
}

// Like an access token (SPEC §17): the first call asks for a press, the same
// call again within 60 s creates the tag and shows its URL or keys once.
esp_err_t createTag(httpd_req_t* r, const cJSON* body, const std::string& session) {
  Tag t;
  esp_err_t err = ESP_OK;
  if (!readNew(r, body, t, err)) return err;
  std::lock_guard<std::mutex> lock(g_mu);
  tags::Store s;
  if (loadLocked(s) != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not read the tags");
  if (s.full()) return http::sendError(r, http::k409, "tags_full", "Keyra holds at most 16 tags");
  if (!sessions().consumeGrace(session, monoMs(), Sessions::Grace::Tag))
    return protect::requestPress(r, actions::Op::TagCreate, session);

  do {
    esp_fill_random(&t.id, sizeof t.id);
  } while (t.id == 0 || s.contains(t.id));
  t.created = unixSecondsOrZero();
  json::Ptr o(cJSON_CreateObject());
  json::Secret url;
  if (t.kind == tags::Kind::Simple) {
    uint8_t raw[tags::kSecretBytes];
    esp_fill_random(raw, sizeof raw);
    json::Secret secret;
    secret.s = tags::formatSecret(raw);
    for (volatile uint8_t& b : raw) b = 0;
    if (!sha256(secret.s, t.hash)) return http::sendError(r, http::k500, "crypto", "Could not hash the secret");
    url.s = tags::simpleUrl(t.id, secret.s);
  } else {
    esp_fill_random(t.metaKey.data(), t.metaKey.size());
    esp_fill_random(t.fileKey.data(), t.fileKey.size());
    url.s = tags::secureUrl(t.id);
    cJSON* keys = cJSON_AddObjectToObject(o.get(), "keys");
    cJSON_AddStringToObject(keys, "meta", toHex(t.metaKey.data(), t.metaKey.size()).c_str());
    cJSON_AddStringToObject(keys, "file", toHex(t.fileKey.data(), t.fileKey.size()).c_str());
  }
  if (!s.add(t)) return badRequest(r, "Invalid tag");
  if (saveLocked(s) != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not save");
  activity::log(activity::Kind::TagCreated, 0, t.name, t.kind == tags::Kind::Secure ? 1 : 0);
  ESP_LOGI(TAG, "tag %u created (%s)", unsigned(t.id), tags::kindName(t.kind));
  cJSON_AddStringToObject(o.get(), "url", url.s.c_str());
  addTag(o.get(), t);
  return http::sendJson(r, http::k201, o.get());
}

// No press: taking power away must never wait for the button.
esp_err_t deleteTag(httpd_req_t* r, uint32_t id) {
  std::string name;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    tags::Store s;
    if (loadLocked(s) != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not read the tags");
    if (const Tag* t = s.find(id)) name = t->name;
    if (!s.remove(id)) return http::sendError(r, http::k404, "not_found", "No such tag");
    if (saveLocked(s) != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not save");
    g_rate.forget(id);
    g_last.erase(id);
  }
  machine().cancelOwned(tags::owner(id));  // whatever it armed goes with it
  activity::log(activity::Kind::TagRevoked, 0, name);
  ESP_LOGI(TAG, "tag %u revoked", unsigned(id));
  return http::sendEmpty(r, http::k204);
}

esp_err_t tap(httpd_req_t* r, const cJSON* body) {
  // The secrets and keys are sealed with the vault's key; typing needs it unlocked anyway.
  if (!vault::unlocked()) return http::sendError(r, http::k401, "locked", "Vault is locked");
  int64_t id64 = 0;
  if (json::getInt(body, "id", 1, UINT32_MAX, id64) != Field::Ok) return refuseTag(r);
  const auto id = static_cast<uint32_t>(id64);
  json::Secret secret;
  std::string p, m;
  const bool simple = json::getString(body, "secret", secret.s) == Field::Ok;
  tags::Sun sun{};
  const bool secure = !simple && json::getString(body, "p", p) == Field::Ok &&
                      json::getString(body, "m", m) == Field::Ok && tags::parseHex(p, sun.picc, 16) &&
                      tags::parseHex(m, sun.mac, 8);
  tags::Digest digest{};
  if (simple && (!tags::wellFormedSecret(secret.s) || !sha256(secret.s, digest))) return refuseTag(r);
  if (!simple && !secure) return refuseTag(r);

  Tag t;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    tags::Store s;
    if (loadLocked(s) != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not read the tags");
    Tag* stored = s.find(id);
    if (!stored || stored->kind != (simple ? tags::Kind::Simple : tags::Kind::Secure)) {
      // fall through to the shared refusal below, outside the lock
    } else {
      tags::Verdict v = tags::Verdict::Ok;
      tags::Uid uid{};
      uint32_t counter = 0;
      if (simple) {
        v = tags::secretMatches(*stored, digest) ? tags::Verdict::Ok : tags::Verdict::BadMac;
      } else {
        v = tags::verifySun(*stored, kAes, sun, uid, counter);
      }
      if (v == tags::Verdict::Crypto) return http::sendError(r, http::k500, "crypto", "Could not check the tag");
      if (v != tags::Verdict::Ok) {
        const uint8_t why = v == tags::Verdict::Replayed ? 1 : v == tags::Verdict::WrongUid ? 2 : 0;
        activity::log({activity::Kind::TagRefused, 0, stored->id, 1, why, stored->name}, true);
        if (v == tags::Verdict::Replayed) {
          return http::sendError(r, http::k409, "replayed", "This tap was already used; tap the tag again");
        }
        stored = nullptr;  // a wrong secret or MAC answers like an unknown tag
      } else {
        int64_t retry = 0;
        if (!g_rate.allow(stored->id, monoMs(), retry)) return sendRateLimited(r, retry);
        // The counter is used up even when arming fails below: an old URL never works twice.
        const int64_t now = unixSecondsOrZero();
        bool dirty = !simple;
        if (!simple) tags::accept(*stored, uid, counter);
        if (now > 0 && now - stored->lastUsed >= tags::kTouchEverySec) {
          stored->lastUsed = now;
          dirty = true;
        }
        if (dirty && saveLocked(s) != vault::Status::Ok)
          return http::sendError(r, http::k500, "storage", "Could not save");
        t = *stored;
      }
    }
    if (!stored) t.id = 0;
  }
  if (t.id == 0) return refuseTag(r);

  // The same checks and machine as POST /api/type (SPEC §5), with the tag's target.
  json::Ptr req(cJSON_CreateObject());
  if (!t.target.empty()) cJSON_AddStringToObject(req.get(), "target", t.target.c_str());
  Target target;
  actions::TypeRequest typeReq;
  esp_err_t err = ESP_OK;
  if (!typereq::readTarget(r, req.get(), target, err)) return err;
  if (!typereq::entryRequest(r, req.get(), t.entry, toAction(t.what), target, typeReq, err)) return err;
  const std::string title = typeReq.title;
  const auto pending = machine().arm(std::move(typeReq), tags::owner(t.id));
  if (!pending) {
    return http::sendError(r, http::k409, "busy",
                           "Keyra is waiting for another request; long-press its button to cancel it");
  }
  uint32_t ticket = 0;
  while (ticket == 0) esp_fill_random(&ticket, sizeof ticket);
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_last[t.id] = {pending->req.serial, ticket};
  }
  activity::log(activity::Kind::TagTapped, t.entry, t.name, whatDetail(t.what));
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "state", "armed");
  cJSON_AddStringToObject(o.get(), "title", title.c_str());
  cJSON_AddStringToObject(o.get(), "what", tags::whatName(t.what));
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(pending->expiresInMs));
  cJSON_AddNumberToObject(o.get(), "ticket", ticket);
  return http::sendJson(r, http::k202, o.get());
}

esp_err_t status(httpd_req_t* r, const cJSON* body) {
  int64_t id = 0, ticket = 0;
  if (json::getInt(body, "id", 1, UINT32_MAX, id) != Field::Ok ||
      json::getInt(body, "ticket", 1, UINT32_MAX, ticket) != Field::Ok)
    return badRequest(r, "\"id\" and \"ticket\" are required");
  LastTap last;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_last.find(static_cast<uint32_t>(id));
    if (it != g_last.end()) last = it->second;
  }
  json::Ptr o(cJSON_CreateObject());
  if (last.ticket == 0 || last.ticket != static_cast<uint32_t>(ticket)) {
    cJSON_AddStringToObject(o.get(), "state", "none");
    return http::sendJson(r, http::k200, o.get());
  }
  using Stage = actions::Machine::Track::Stage;
  const actions::Machine::Track tr = machine().track(last.serial);
  switch (tr.stage) {
    case Stage::Armed:
      cJSON_AddStringToObject(o.get(), "state", "armed");
      cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(tr.expiresInMs));
      break;
    case Stage::Connecting:
      cJSON_AddStringToObject(o.get(), "state", "waiting");
      cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(tr.expiresInMs));
      break;
    case Stage::Running: cJSON_AddStringToObject(o.get(), "state", "waiting"); break;
    case Stage::Finished: {
      const char* state = tr.code == actions::Code::Typed       ? "typed"
                          : tr.code == actions::Code::Cancelled ? "cancelled"
                          : tr.code == actions::Code::Expired   ? "expired"
                                                                : "failed";
      cJSON_AddStringToObject(o.get(), "state", state);
      cJSON_AddStringToObject(o.get(), "code", actions::codeName(tr.code));
      break;
    }
    case Stage::Unknown: cJSON_AddStringToObject(o.get(), "state", "none"); break;
  }
  return http::sendJson(r, http::k200, o.get());
}

std::string ownerName(const std::string& owner) {
  const uint32_t id = tags::ownerId(owner);
  if (id == 0) return {};
  std::lock_guard<std::mutex> lock(g_mu);
  const auto it = g_names.find(id);
  return it == g_names.end() ? std::string("?") : it->second;
}

}  // namespace keyra::api::tagapi
