#include "backup_format.hpp"

#include <cstring>

#include "text.hpp"

namespace keyra::vault::backup {
namespace {

using Type = json::Value::Type;

void key(SecureString& out, const char* k) {
  out += '"';
  out += k;
  out += "\":";
}

bool readB64(const json::Value* v, std::vector<uint8_t>& out) {
  if (!v || v->type != Type::String) return false;
  return text::base64Decode(std::string(v->s.data(), v->s.size()), out);
}

bool readString(const json::Value& obj, const char* k, std::string& out) {
  const json::Value* v = obj.find(k);
  if (!v) {
    out.clear();
    return true;
  }
  if (v->type != Type::String) return false;
  out.assign(v->s.data(), v->s.size());
  return true;
}

bool readInt(const json::Value& obj, const char* k, int64_t& out) {
  const json::Value* v = obj.find(k);
  if (!v) {
    out = 0;
    return true;
  }
  if (v->type != Type::Int) return false;
  out = v->i;
  return true;
}

}  // namespace

std::string writeEnvelope(const Envelope& env) {
  std::string s;  // nothing secret: KDF parameters and ciphertext only
  s += "{\"format\":\"keyra-backup\",\"v\":" + std::to_string(kVersion);
  s += ",\"kdf\":{\"alg\":\"pbkdf2-sha256\",\"iter\":";
  s += std::to_string(env.iterations);
  s += ",\"salt\":\"" + text::base64Encode(env.salt, sizeof env.salt);
  s += "\"},\"iv\":\"" + text::base64Encode(env.iv, sizeof env.iv);
  s += "\",\"data\":\"" + text::base64Encode(env.data.data(), env.data.size());
  s += "\"}";
  return s;
}

bool readEnvelope(const std::string& textIn, Envelope& out) {
  json::Value root;
  if (!json::parse(textIn.data(), textIn.size(), root) || root.type != Type::Object) return false;
  const json::Value* format = root.find("format");
  const json::Value* v = root.find("v");
  const json::Value* kdf = root.find("kdf");
  if (!format || format->type != Type::String || format->s != "keyra-backup") return false;
  if (!v || v->type != Type::Int || v->i < 1 || v->i > kVersion) return false;
  out.version = v->i;
  if (!kdf || kdf->type != Type::Object) return false;
  const json::Value* alg = kdf->find("alg");
  const json::Value* iter = kdf->find("iter");
  if (!alg || alg->type != Type::String || alg->s != "pbkdf2-sha256") return false;
  if (!iter || iter->type != Type::Int || iter->i < 1 || iter->i > int64_t(kMaxIterations))
    return false;
  out.iterations = uint32_t(iter->i);

  std::vector<uint8_t> salt, iv;
  if (!readB64(kdf->find("salt"), salt) || salt.size() != sizeof out.salt) return false;
  if (!readB64(root.find("iv"), iv) || iv.size() != sizeof out.iv) return false;
  if (!readB64(root.find("data"), out.data) || out.data.size() < 16) return false;
  std::memcpy(out.salt, salt.data(), sizeof out.salt);
  std::memcpy(out.iv, iv.data(), sizeof out.iv);
  return true;
}

void writeEntry(SecureString& out, const Entry& e) {
  out += '{';
  key(out, "id");
  json::writeInt(out, e.id);
  const std::pair<const char*, const std::string*> fields[] = {
      {"title", &e.title},       {"url", &e.url},   {"username", &e.username},
      {"password", &e.password}, {"totp", &e.totp}, {"notes", &e.notes},
  };
  for (const auto& f : fields) {
    out += ',';
    key(out, f.first);
    json::writeString(out, *f.second);
  }
  out += ',';
  key(out, "favorite");
  out += e.favorite ? "true" : "false";
  out += ',';
  key(out, "created");
  json::writeInt(out, e.created);
  out += ',';
  key(out, "updated");
  json::writeInt(out, e.updated);
  out += ',';
  key(out, "lastUsed");
  json::writeInt(out, e.lastUsed);
  out += ',';
  key(out, "history");
  out += '[';
  for (size_t i = 0; i < e.history.size(); ++i) {
    if (i) out += ',';
    out += '{';
    key(out, "password");
    json::writeString(out, e.history[i].password);
    out += ',';
    key(out, "changedAt");
    json::writeInt(out, e.history[i].changedAt);
    out += '}';
  }
  out += ']';
  if (!e.sequence.empty()) {
    out += ',';
    key(out, "sequence");
    json::writeString(out, e.sequence);
  }
  if (e.burnAfter) {
    out += ',';
    key(out, "burnAfter");
    json::writeInt(out, e.burnAfter);
  }
  out += '}';
}

bool readEntry(const json::Value& v, Entry& out) {
  if (v.type != Type::Object) return false;
  int64_t id;
  if (!readInt(v, "id", id) || id < 0 || id > int64_t(UINT32_MAX)) return false;
  out.id = uint32_t(id);
  if (!readString(v, "title", out.title) || !readString(v, "url", out.url) ||
      !readString(v, "username", out.username) || !readString(v, "password", out.password) ||
      !readString(v, "totp", out.totp) || !readString(v, "notes", out.notes))
    return false;
  const json::Value* fav = v.find("favorite");
  if (fav && fav->type != Type::Bool) return false;
  out.favorite = fav && fav->b;
  if (!readInt(v, "created", out.created) || !readInt(v, "updated", out.updated) ||
      !readInt(v, "lastUsed", out.lastUsed) || !readString(v, "sequence", out.sequence))
    return false;
  int64_t burn = 0;
  if (!readInt(v, "burnAfter", burn) || burn < 0 || burn > int64_t(kMaxBurnAfter)) return false;
  out.burnAfter = uint8_t(burn);

  for (OldPassword& h : out.history) wipe(h.password);
  out.history.clear();
  const json::Value* history = v.find("history");
  if (!history) return true;
  if (history->type != Type::Array || history->items.size() > kMaxHistory) return false;
  out.history.reserve(history->items.size());  // no regrowth: see codec::decode
  for (const json::Value& item : history->items) {
    if (item.type != Type::Object) return false;
    out.history.emplace_back();
    if (!readString(item, "password", out.history.back().password) ||
        !readInt(item, "changedAt", out.history.back().changedAt))
      return false;
  }
  return true;
}

void writePasskeys(SecureString& out, const Passkeys& p) {
  out += ',';
  key(out, "passkeys");
  out += '{';
  key(out, "keys");
  out += '[';
  for (size_t i = 0; i < p.keys.size(); ++i) {
    if (i) out += ',';
    std::string b64 = text::base64Encode(p.keys[i].data(), p.keys[i].size());  // one allocation, wiped below
    out += '"';
    out.append(b64.data(), b64.size());
    out += '"';
    wipe(b64);
  }
  out += "],";
  key(out, "records");
  out += '[';
  for (size_t i = 0; i < p.records.size(); ++i) {
    if (i) out += ',';
    out += '"';
    const std::string b64 = text::base64Encode(p.records[i].data(), p.records[i].size());
    out.append(b64.data(), b64.size());
    out += '"';
  }
  out += "],";
  key(out, "counter");
  json::writeInt(out, p.counter);
  out += '}';
}

bool readPasskeys(const json::Value& v, Passkeys& out) {
  out = Passkeys{};
  if (v.type != Type::Object) return false;
  const json::Value* keys = v.find("keys");
  const json::Value* records = v.find("records");
  const json::Value* counter = v.find("counter");
  if (!keys || keys->type != Type::Array || keys->items.size() > kMaxPasskeyWrapKeys) return false;
  if (!records || records->type != Type::Array || records->items.size() > kMaxPasskeys) return false;
  if (!counter || counter->type != Type::Int || counter->i < 0 || counter->i > int64_t(UINT32_MAX)) return false;
  out.keys.reserve(keys->items.size());  // no regrowth: the allocator wipes, but only what it frees
  for (const json::Value& k : keys->items) {
    if (k.type != Type::String) return false;
    std::string b64(k.s.data(), k.s.size());
    std::vector<uint8_t> raw;
    const bool ok = text::base64Decode(b64, raw) && raw.size() == 32;
    if (ok) {
      out.keys.emplace_back();
      std::memcpy(out.keys.back().data(), raw.data(), 32);
    }
    wipe(b64);
    if (!raw.empty()) mem::zeroize(raw.data(), raw.size());
    if (!ok) return false;
    for (size_t i = 0; i + 1 < out.keys.size(); ++i)
      if (out.keys[i] == out.keys.back()) return false;  // never written: a hand-made file
  }
  out.records.reserve(records->items.size());
  for (const json::Value& r : records->items) {
    out.records.emplace_back();
    if (!readB64(&r, out.records.back()) || out.records.back().empty() ||
        out.records.back().size() > kMaxPasskeyRecord)
      return false;
  }
  out.counter = uint32_t(counter->i);
  out.present = true;
  return true;
}

}  // namespace keyra::vault::backup
