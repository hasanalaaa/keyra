#include "handlers_agent.hpp"

#include <map>
#include <memory>
#include <mutex>

#include "activity.hpp"
#include "companion.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "handlers_gen.hpp"
#include "handlers_protect.hpp"
#include "host_match.hpp"
#include "http.hpp"
#include "keyra/vault.hpp"
#include "psa/crypto.h"
#include "runtime.hpp"
#include "type_request.hpp"

namespace keyra::api::agent {
namespace {

const char* TAG = "agent";
using json::Field;
using tokens::Token;
constexpr size_t kMaxAuthHeader = 128;

// What a token asked for last, so GET /api/agent/status can follow it.
struct LastRequest {
  uint32_t serial = 0;
  bool save = false;
  uint32_t id = 0;  // the entry (type) or, once saved, the new entry (save)
  std::string title;
  actions::What what = actions::What::Username;
};

std::mutex g_mu;  // the stored record's read-modify-write, the maps below
tokens::RateLimit g_rate;
std::map<uint32_t, LastRequest> g_last;
std::map<uint32_t, std::string> g_names;  // for state.pending.by, without a vault read per poll

bool sha256(std::string_view s, tokens::Digest& out) {
  size_t len = 0;
  return psa_crypto_init() == PSA_SUCCESS &&
         psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(s.data()), s.size(), out.data(),
                          out.size(), &len) == PSA_SUCCESS &&
         len == out.size();
}

// Caller holds g_mu.
vault::Status loadLocked(tokens::Store& out) {
  std::vector<uint8_t> raw;
  const vault::Status st = vault::tokensRead(raw);
  if (st != vault::Status::Ok) return st;
  auto parsed = tokens::Store::parse(raw.data(), raw.size());
  if (!parsed) {
    // Fails safe: no token works until a new one is made (which rewrites it).
    ESP_LOGE(TAG, "stored tokens unreadable; treating as none");
    out = tokens::Store();
    return vault::Status::Ok;
  }
  out = std::move(*parsed);
  g_names.clear();
  for (const Token& t : out.all()) g_names[t.id] = t.name;
  return vault::Status::Ok;
}

vault::Status saveLocked(const tokens::Store& s) {
  const vault::Status st = vault::tokensWrite(s.serialize());
  if (st != vault::Status::Ok) ESP_LOGE(TAG, "saving tokens: %s", vault::statusName(st));
  g_names.clear();
  for (const Token& t : s.all()) g_names[t.id] = t.name;
  return st;
}

esp_err_t badRequest(httpd_req_t* r, const char* message) { return http::sendError(r, http::k400, "invalid", message); }

esp_err_t sendRateLimited(httpd_req_t* r, int64_t retryMs) {
  const std::string secs = std::to_string((retryMs + 999) / 1000);
  httpd_resp_set_hdr(r, "Retry-After", secs.c_str());
  json::Ptr o(http::errorBody("rate_limited", "Too many requests for this token"));
  cJSON_AddNumberToObject(o.get(), "retryAfterMs", static_cast<double>(retryMs));
  return http::sendJson(r, http::k429, o.get());
}

// Unknown or malformed tokens share one budget, so guessing is refused cheaply.
esp_err_t refuseToken(httpd_req_t* r) {
  int64_t retry = 0;
  bool allowed;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    allowed = g_rate.allow(0, monoMs(), retry);
  }
  if (!allowed) return sendRateLimited(r, retry);
  httpd_resp_set_hdr(r, "WWW-Authenticate", "Bearer");
  return http::sendError(r, http::k401, "invalid_token", "Unknown or revoked access token");
}

uint8_t whatDetail(actions::What w) {
  switch (w) {
    case actions::What::Password: return 1;
    case actions::What::Both: return 2;
    case actions::What::Totp: return 3;
    default: return 0;
  }
}

cJSON* scopeJson(const Token& t) {
  if (t.all) return cJSON_CreateString("all");
  cJSON* a = cJSON_CreateArray();
  for (uint32_t id : t.scope) cJSON_AddItemToArray(a, cJSON_CreateNumber(id));
  return a;
}

void addToken(cJSON* o, const Token& t) {
  cJSON_AddNumberToObject(o, "id", t.id);
  cJSON_AddStringToObject(o, "name", t.name.c_str());
  cJSON_AddStringToObject(o, "kind", tokens::kindName(t.kind));
  cJSON_AddItemToObject(o, "scope", scopeJson(t));
  cJSON_AddNumberToObject(o, "created", static_cast<double>(t.created));
  cJSON_AddNumberToObject(o, "lastUsed", static_cast<double>(t.lastUsed));
}

// ---------- /api/agent/… ----------

esp_err_t listEntries(httpd_req_t* r, const Token& t) {
  std::vector<vault::Entry> all;
  const vault::Status st = vault::list(all);
  if (st != vault::Status::Ok) {
    for (vault::Entry& e : all) vault::wipe(e);
    return http::sendError(r, st == vault::Status::Locked ? http::k401 : http::k500,
                           st == vault::Status::Locked ? "locked" : "storage", "Could not read the accounts");
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "entries");
  for (vault::Entry& e : all) {
    if (tokens::inScope(t, e.id)) {
      cJSON* j = cJSON_CreateObject();
      cJSON_AddNumberToObject(j, "id", e.id);
      cJSON_AddStringToObject(j, "title", e.title.c_str());
      cJSON_AddStringToObject(j, "host", tokens::urlHost(e.url).c_str());
      cJSON_AddItemToArray(arr, j);
    }
    vault::wipe(e);
  }
  activity::log({activity::Kind::AgentListed, 0, t.id, 1, 0, t.name}, true);
  return http::sendJson(r, http::k200, o.get());
}

// The page's host from a request body; false when refused (the 400 is sent).
bool readHost(httpd_req_t* r, const cJSON* body, std::string& host, esp_err_t& err) {
  if (json::getString(body, "host", host) != Field::Ok || !hostmatch::validHost(host)) {
    err = badRequest(r, "\"host\" (the page's host name) is required");
    return false;
  }
  host = hostmatch::normalize(host);
  return true;
}

// Host binding for an extension (SPEC §9.4): it types only into a page the
// login is for, unless the user chose another site's login ("anyHost"); then
// `elsewhere` is the page's host. False when refused (the reply is sent).
bool checkHost(httpd_req_t* r, const cJSON* body, uint32_t id, std::string& elsewhere, esp_err_t& err) {
  std::string page;
  if (!readHost(r, body, page, err)) return false;
  bool anyHost = false;
  if (json::getBool(body, "anyHost", anyHost) == Field::BadType) {
    err = badRequest(r, "\"anyHost\" must be true or false");
    return false;
  }
  vault::Entry e;
  // An unknown entry is answered by entryRequest(), like for every other kind.
  if (vault::get(id, e) != vault::Status::Ok) {
    vault::wipe(e);
    return true;
  }
  const companion::HostCheck check = companion::checkHost(e.url, page, anyHost);
  vault::wipe(e);
  switch (check) {
    case companion::HostCheck::Same: return true;
    case companion::HostCheck::Elsewhere: elsewhere = page; return true;
    case companion::HostCheck::Refused: break;
  }
  err = http::sendError(r, http::k409, "host_mismatch", "This login is for another site");
  return false;
}

// The logins offered on a page (SPEC §9.4). `sameUser` tells the extension
// whether a login it is about to save already exists; passwords are never
// compared, so this cannot be used to test a guess.
esp_err_t postMatch(httpd_req_t* r, const Token& t, const cJSON* body) {
  if (!tokens::isExtension(t))
    return http::sendError(r, http::k403, "forbidden", "Only a browser extension's token can match logins");
  std::string page;
  esp_err_t err = ESP_OK;
  if (!readHost(r, body, page, err)) return err;
  json::Secret user;  // half a credential: wiped like one
  const Field got = json::getString(body, "username", user.s);
  if (got == Field::BadType || user.s.size() > vault::kMaxUsername)
    return badRequest(r, "\"username\" must be a string within the vault's limits");
  const bool withUser = got == Field::Ok;

  std::vector<vault::Entry> all;
  const vault::Status st = vault::list(all);
  const std::vector<companion::Offer> offers =
      st == vault::Status::Ok ? companion::match(t, all, page, withUser ? &user.s : nullptr)
                              : std::vector<companion::Offer>{};
  for (vault::Entry& e : all) vault::wipe(e);
  if (st != vault::Status::Ok) {
    return http::sendError(r, st == vault::Status::Locked ? http::k401 : http::k500,
                           st == vault::Status::Locked ? "locked" : "storage", "Could not read the accounts");
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "entries");
  for (const companion::Offer& m : offers) {
    cJSON* j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "id", m.id);
    cJSON_AddStringToObject(j, "title", m.title.c_str());
    cJSON_AddStringToObject(j, "host", m.host.c_str());
    if (withUser) cJSON_AddBoolToObject(j, "sameUser", m.sameUser);
    cJSON_AddItemToArray(arr, j);
  }
  activity::log({activity::Kind::AgentListed, 0, t.id, 1, 0, t.name}, true);
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t postType(httpd_req_t* r, const Token& t, const cJSON* body) {
  int64_t id = 0;
  if (json::getInt(body, "id", 1, UINT32_MAX, id) != Field::Ok) return badRequest(r, "\"id\" (entry id) is required");
  std::string whatStr;
  const auto what = json::getString(body, "what", whatStr) == Field::Ok ? actions::parseWhat(whatStr) : std::nullopt;
  if (!what || *what == actions::What::Sequence) return badRequest(r, "\"what\" must be username, password, both or totp");
  // Outside the scope answers like an unknown id: no hint which ids exist.
  if (!tokens::inScope(t, static_cast<uint32_t>(id))) return http::sendError(r, http::k404, "not_found", "No such entry");
  std::string elsewhere;  // the page's host, when the extension types a login for another site
  if (tokens::isExtension(t)) {
    esp_err_t err = ESP_OK;
    if (!checkHost(r, body, static_cast<uint32_t>(id), elsewhere, err)) return err;
  }
  Target target;
  actions::TypeRequest req;
  esp_err_t err = ESP_OK;
  if (!typereq::readTarget(r, body, target, err)) return err;
  if (!typereq::entryRequest(r, body, static_cast<uint32_t>(id), *what, target, req, err)) return err;
  req.host = elsewhere;
  const std::string title = req.title;
  const auto pending = machine().arm(std::move(req), tokens::owner(t.id));
  if (!pending) {
    return http::sendError(r, http::k409, "busy",
                           "Keyra is waiting for another request; long-press its button to cancel it");
  }
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_last[t.id] = {pending->req.serial, false, pending->req.id, title, *what};
  }
  if (elsewhere.empty()) {
    activity::log(activity::Kind::AgentArmed, pending->req.id, t.name, whatDetail(*what));
  } else {
    activity::log(activity::Kind::AgentArmed, pending->req.id, activity::elsewhereTitle(t.name, elsewhere),
                  whatDetail(*what) + activity::kArmedElsewhere);
  }
  json::Ptr o(cJSON_CreateObject());
  typereq::addPending(cJSON_AddObjectToObject(o.get(), "pending"), *pending);
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(pending->expiresInMs));
  return http::sendJson(r, http::k202, o.get());
}

const char* typeState(actions::Code c) {
  switch (c) {
    case actions::Code::Typed: return "typed";
    case actions::Code::Cancelled: return "cancelled";
    case actions::Code::Expired: return "expired";
    default: return "failed";
  }
}

const char* saveState(actions::OpCode c) {
  switch (c) {
    case actions::OpCode::Done: return "saved";
    case actions::OpCode::Cancelled: return "cancelled";
    case actions::OpCode::Expired: return "expired";
    case actions::OpCode::Failed: break;
  }
  return "failed";
}

esp_err_t getStatus(httpd_req_t* r, const Token& t) {
  LastRequest last;
  bool has = false;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_last.find(t.id);
    if (it != g_last.end()) {
      last = it->second;
      has = true;
    }
  }
  json::Ptr o(cJSON_CreateObject());
  if (!has) {
    cJSON_AddStringToObject(o.get(), "state", "none");
    return http::sendJson(r, http::k200, o.get());
  }
  using Stage = actions::Machine::Track::Stage;
  const actions::Machine::Track tr = machine().track(last.serial);
  cJSON_AddStringToObject(o.get(), "request", last.save ? "save" : "type");
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
    case Stage::Finished:
      if (last.save) {
        cJSON_AddStringToObject(o.get(), "state", saveState(tr.opCode));
      } else {
        cJSON_AddStringToObject(o.get(), "state", typeState(tr.code));
        cJSON_AddStringToObject(o.get(), "code", actions::codeName(tr.code));
      }
      break;
    case Stage::Unknown:  // more than a few outcomes ago: no longer known
      cJSON_AddStringToObject(o.get(), "state", "none");
      break;
  }
  if (last.id != 0) cJSON_AddNumberToObject(o.get(), "id", last.id);
  if (!last.save) {
    cJSON_AddStringToObject(o.get(), "title", last.title.c_str());
    cJSON_AddStringToObject(o.get(), "what", actions::whatName(last.what));
  }
  return http::sendJson(r, http::k200, o.get());
}

// The account a token sent, held (and wiped) until the press stores it or the op is dropped.
struct SaveJob {
  vault::Entry entry;
  uint32_t replace = 0;  // update this entry instead of creating one
  uint32_t tokenId = 0;
  std::string tokenName;
  ~SaveJob() { vault::wipe(entry); }
};

// Create: a new entry. Update: the username (when one was sent) and the
// password; the vault moves the old password into the entry's history (§9.3).
bool commitSave(SaveJob& job, uint32_t serial) {
  if (!vault::unlocked()) return false;
  vault::Entry e;
  if (job.replace != 0) {
    // The account may have been deleted while the press was awaited.
    if (vault::get(job.replace, e) != vault::Status::Ok) {
      vault::wipe(e);
      return false;
    }
    companion::applyReplace(e, job.entry, unixSecondsOrZero());
  } else {
    e = job.entry;
    e.id = 0;
    e.created = e.updated = unixSecondsOrZero();
  }
  const vault::Status st = vault::put(e);
  const uint32_t id = e.id;
  vault::wipe(e);
  if (st != vault::Status::Ok) {
    ESP_LOGW(TAG, "save failed: %s", vault::statusName(st));
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_last.find(job.tokenId);
    if (it != g_last.end() && it->second.serial == serial) it->second.id = id;
    tokens::Store s;
    // A scoped token may type what it just saved (while its scope has room).
    if (job.replace == 0 && loadLocked(s) == vault::Status::Ok && s.addToScope(job.tokenId, id)) saveLocked(s);
  }
  activity::log(activity::Kind::AgentSaved, id, job.tokenName, job.replace != 0 ? 1 : 0);
  return true;
}

esp_err_t postSave(httpd_req_t* r, const Token& t, const cJSON* body) {
  if (!tokens::mayWrite(t)) return http::sendError(r, http::k403, "forbidden", "This token cannot save accounts");
  auto job = std::make_shared<SaveJob>();
  job->tokenId = t.id;
  job->tokenName = t.name;
  struct StrField {
    const char* key;
    std::string* dst;
    size_t max;
  } fields[] = {{"title", &job->entry.title, vault::kMaxTitle},
                {"url", &job->entry.url, vault::kMaxUrl},
                {"username", &job->entry.username, vault::kMaxUsername},
                {"password", &job->entry.password, vault::kMaxPassword}};
  for (const StrField& f : fields) {
    const Field got = json::getString(body, f.key, *f.dst);
    if (got == Field::BadType || f.dst->size() > f.max) {
      const std::string msg = std::string("\"") + f.key + "\" must be a string within the vault's limits";
      return badRequest(r, msg.c_str());
    }
  }
  int64_t replace = 0;
  if (json::getInt(body, "replace", 1, UINT32_MAX, replace) == Field::BadType)
    return badRequest(r, "\"replace\" must be an entry id");
  job->replace = static_cast<uint32_t>(replace);
  if (job->replace != 0) {
    // title and url are ignored: an update changes only the login itself.
    if (job->entry.password.empty()) return badRequest(r, "\"password\" (string) is required to update an account");
    // Outside the scope answers like an unknown id: no hint which ids exist.
    vault::Entry e;
    const vault::Status st = tokens::inScope(t, job->replace) ? vault::get(job->replace, e) : vault::Status::NotFound;
    vault::wipe(e);
    if (st == vault::Status::NotFound) return http::sendError(r, http::k404, "not_found", "No such entry");
    if (st != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not read the account");
  } else if (job->entry.title.empty()) {
    return badRequest(r, "\"title\" (string) is required");
  }
  // The serial is known only once armed; the commit reads it from this cell.
  auto serial = std::make_shared<uint32_t>(0);
  const auto armed = machine().awaitPresence(
      actions::Op::AgentSave, [job, serial] { return commitSave(*job, *serial); }, tokens::owner(t.id));
  if (!armed) {
    return http::sendError(r, http::k409, "busy",
                           "Keyra is waiting for another request; long-press its button to cancel it");
  }
  *serial = armed->serial;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_last[t.id] = {armed->serial, true, 0, {}, actions::What::Username};
  }
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "awaiting", "button");
  cJSON_AddStringToObject(o.get(), "op", actions::opName(actions::Op::AgentSave));
  cJSON_AddStringToObject(o.get(), "mode", job->replace != 0 ? "update" : "create");
  cJSON_AddNumberToObject(o.get(), "expiresIn", static_cast<double>(armed->expiresIn));
  return http::sendJson(r, http::k202, o.get());
}

esp_err_t postGenerate(httpd_req_t* r, const Token& t, const cJSON* body) {
  if (!tokens::mayWrite(t)) return http::sendError(r, http::k403, "forbidden", "This token cannot generate passwords");
  activity::log({activity::Kind::AgentGenerated, 0, t.id, 1, 0, t.name}, true);
  return genapi::postGenerate(r, body);
}

esp_err_t postCancel(httpd_req_t* r, const Token& t) {
  if (!machine().cancelOwned(tokens::owner(t.id)))
    return http::sendError(r, http::k409, "not_cancelled", "Nothing of this token is waiting for the button");
  return http::sendEmpty(r, http::k204);
}

// Body of POST /api/tokens; false when refused (the 400 is sent).
bool readNew(httpd_req_t* r, const cJSON* body, Token& t, esp_err_t& err) {
  std::string kind;
  if (json::getString(body, "name", t.name) != Field::Ok || !tokens::validName(t.name)) {
    err = badRequest(r, "\"name\" must be 1-48 bytes of text");
    return false;
  }
  const auto k = json::getString(body, "kind", kind) == Field::Ok ? tokens::parseKind(kind) : std::nullopt;
  if (!k) {
    err = badRequest(r, "\"kind\" must be \"agent\", \"app\" or \"extension\"");
    return false;
  }
  t.kind = *k;
  const cJSON* scope = cJSON_GetObjectItemCaseSensitive(body, "scope");
  if (cJSON_IsString(scope) && std::string(scope->valuestring) == "all") {
    t.all = true;
    return true;
  }
  if (!cJSON_IsArray(scope) || cJSON_GetArraySize(scope) < 1 ||
      static_cast<size_t>(cJSON_GetArraySize(scope)) > tokens::kMaxScope) {
    err = badRequest(r, "\"scope\" must be \"all\" or 1-32 entry ids");
    return false;
  }
  t.all = false;
  const cJSON* item = nullptr;
  cJSON_ArrayForEach(item, scope) {
    const double v = cJSON_IsNumber(item) ? item->valuedouble : 0;
    const bool whole = v >= 1 && v <= UINT32_MAX && static_cast<double>(static_cast<uint32_t>(v)) == v;
    const uint32_t id = whole ? static_cast<uint32_t>(v) : 0;
    vault::Entry e;
    const bool known = whole && vault::get(id, e) == vault::Status::Ok;
    vault::wipe(e);
    if (!known) {
      err = badRequest(r, "\"scope\" names an account that does not exist");
      return false;
    }
    if (!tokens::inScope(t, id)) t.scope.push_back(id);
  }
  return true;
}

}  // namespace

std::optional<Token> authenticate(httpd_req_t* r, esp_err_t& err) {
  // The hashes are sealed with the vault's key; arming needs it unlocked anyway.
  if (!vault::unlocked()) {
    err = http::sendError(r, http::k401, "locked", "Vault is locked");
    return std::nullopt;
  }
  const std::string header = http::header(r, "Authorization", kMaxAuthHeader);
  const std::string_view presented = tokens::bearer(header);
  tokens::Digest d{};
  if (!tokens::wellFormed(presented) || !sha256(presented, d)) {
    err = refuseToken(r);
    return std::nullopt;
  }
  std::optional<Token> found;
  int64_t retry = 0;
  bool allowed = true;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    tokens::Store s;
    if (loadLocked(s) != vault::Status::Ok) {
      err = http::sendError(r, http::k500, "storage", "Could not read the access tokens");
      return std::nullopt;
    }
    if (const auto i = s.find(d)) {
      allowed = g_rate.allow(s.all()[*i].id, monoMs(), retry);
      if (allowed && s.touch(*i, unixSecondsOrZero())) saveLocked(s);
      found = s.all()[*i];
    }
  }
  if (!found) {
    err = refuseToken(r);
    return std::nullopt;
  }
  if (!allowed) {
    err = sendRateLimited(r, retry);
    return std::nullopt;
  }
  return found;
}

esp_err_t dispatch(httpd_req_t* r, Route route, const Token& t, const cJSON* body) {
  switch (route) {
    case Route::AgentEntries: return listEntries(r, t);
    case Route::AgentType: return postType(r, t, body);
    case Route::AgentStatus: return getStatus(r, t);
    case Route::AgentCancel: return postCancel(r, t);
    case Route::AgentSave: return postSave(r, t, body);
    case Route::AgentGenerate: return postGenerate(r, t, body);
    case Route::AgentMatch: return postMatch(r, t, body);
    default: break;
  }
  return http::sendError(r, http::k404, "not_found", "No such endpoint");
}

esp_err_t listTokens(httpd_req_t* r) {
  tokens::Store s;
  vault::Status st;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    st = loadLocked(s);
  }
  if (st != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not read the access tokens");
  json::Ptr o(cJSON_CreateObject());
  cJSON* arr = cJSON_AddArrayToObject(o.get(), "tokens");
  for (const Token& t : s.all()) {
    cJSON* j = cJSON_CreateObject();
    addToken(j, t);
    cJSON_AddItemToArray(arr, j);
  }
  cJSON_AddNumberToObject(o.get(), "max", static_cast<double>(tokens::kMaxTokens));
  return http::sendJson(r, http::k200, o.get());
}

// Like the recovery key (SPEC §12.5a): the first call asks for a press, the
// same call again within 60 s creates the token and shows it once.
esp_err_t createToken(httpd_req_t* r, const cJSON* body, const std::string& session) {
  Token t;
  esp_err_t err = ESP_OK;
  if (!readNew(r, body, t, err)) return err;
  std::lock_guard<std::mutex> lock(g_mu);
  tokens::Store s;
  if (loadLocked(s) != vault::Status::Ok)
    return http::sendError(r, http::k500, "storage", "Could not read the access tokens");
  if (s.full()) return http::sendError(r, http::k409, "tokens_full", "Keyra holds at most 8 access tokens");
  if (!sessions().consumeGrace(session, monoMs(), Sessions::Grace::Token))
    return protect::requestPress(r, actions::Op::TokenCreate, session);

  uint8_t raw[tokens::kSecretBytes];
  esp_fill_random(raw, sizeof raw);
  json::Secret secret;
  secret.s = tokens::format(raw);
  for (volatile uint8_t& b : raw) b = 0;
  if (!sha256(secret.s, t.hash)) return http::sendError(r, http::k500, "crypto", "Could not hash the token");
  do {
    esp_fill_random(&t.id, sizeof t.id);
  } while (t.id == 0 || s.contains(t.id));
  t.created = unixSecondsOrZero();
  if (!s.add(t)) return badRequest(r, "Invalid token");
  if (saveLocked(s) != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not save");
  activity::log(activity::Kind::TokenCreated, 0, t.name, static_cast<uint8_t>(t.kind));  // detail = the stored kind
  ESP_LOGI(TAG, "access token %u created (%s)", unsigned(t.id), tokens::kindName(t.kind));
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "token", secret.s.c_str());
  addToken(o.get(), t);
  return http::sendJson(r, http::k201, o.get());
}

// No press: taking power away must never wait for the button.
esp_err_t deleteToken(httpd_req_t* r, uint32_t id) {
  std::string name;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    tokens::Store s;
    if (loadLocked(s) != vault::Status::Ok)
      return http::sendError(r, http::k500, "storage", "Could not read the access tokens");
    for (const Token& t : s.all()) {
      if (t.id == id) name = t.name;
    }
    if (!s.remove(id)) return http::sendError(r, http::k404, "not_found", "No such access token");
    if (saveLocked(s) != vault::Status::Ok) return http::sendError(r, http::k500, "storage", "Could not save");
    g_rate.forget(id);
    g_last.erase(id);
  }
  machine().cancelOwned(tokens::owner(id));  // whatever it armed goes with it
  activity::log(activity::Kind::TokenRevoked, 0, name);
  ESP_LOGI(TAG, "access token %u revoked", unsigned(id));
  return http::sendEmpty(r, http::k204);
}

std::string ownerName(const std::string& owner) {
  const uint32_t id = tokens::ownerId(owner);
  if (id == 0) return {};
  std::lock_guard<std::mutex> lock(g_mu);
  const auto it = g_names.find(id);
  return it == g_names.end() ? std::string("?") : it->second;
}

}  // namespace keyra::api::agent
