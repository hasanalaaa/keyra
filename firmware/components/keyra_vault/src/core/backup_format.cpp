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
  s += "{\"format\":\"keyra-backup\",\"v\":1,\"kdf\":{\"alg\":\"pbkdf2-sha256\",\"iter\":";
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
  if (!v || v->type != Type::Int || v->i != 1) return false;
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
  return readInt(v, "created", out.created) && readInt(v, "updated", out.updated) &&
         readInt(v, "lastUsed", out.lastUsed);
}

}  // namespace keyra::vault::backup
