#include "handlers_gen.hpp"

#include <cmath>

#include "esp_random.h"
#include "generator.hpp"
#include "http.hpp"
#include "runtime.hpp"
#include "validate.hpp"

namespace keyra::api::genapi {
namespace {

using json::Field;

esp_err_t badRequest(httpd_req_t* r, const char* message) { return http::sendError(r, http::k400, "invalid", message); }

// The ESP32-S3 RNG is a true random source while the radio runs, and Keyra's
// radio is always on (its own AP, or the home network in "fallback" mode).
bool hardwareRandom(uint8_t* out, size_t n) {
  esp_fill_random(out, n);
  return true;
}

}  // namespace

esp_err_t postGenerate(httpd_req_t* r, const cJSON* b) {
  gen::Params p;
  int64_t n = 0;
  if (json::getInt(b, "length", 0, 1000, n) != Field::Ok) return badRequest(r, "\"length\" (8-128) is required");
  p.length = static_cast<int>(n);
  struct Flag {
    const char* key;
    bool* dst;
  } classes[] = {{"lower", &p.lower}, {"upper", &p.upper}, {"digits", &p.digits}, {"symbols", &p.symbols}};
  for (const Flag& f : classes) {
    if (json::getBool(b, f.key, *f.dst) != Field::Ok)
      return badRequest(r, "\"lower\", \"upper\", \"digits\" and \"symbols\" (booleans) are required");
  }
  Field f;
  if ((f = json::getInt(b, "minDigits", 0, gen::kMaxLength, n)) == Field::BadType)
    return badRequest(r, "minDigits must be 0-128");
  if (f == Field::Ok) p.minDigits = static_cast<int>(n);
  if ((f = json::getInt(b, "minSymbols", 0, gen::kMaxLength, n)) == Field::BadType)
    return badRequest(r, "minSymbols must be 0-128");
  if (f == Field::Ok) p.minSymbols = static_cast<int>(n);
  if (json::getBool(b, "avoidAmbiguous", p.avoidAmbiguous) == Field::BadType)
    return badRequest(r, "avoidAmbiguous must be a boolean");
  if ((f = json::getString(b, "symbolSet", p.symbolSet)) == Field::BadType || (f == Field::Ok && p.symbolSet.empty()))
    return badRequest(r, gen::message(gen::Error::SymbolSet));

  gen::Plan plan;
  const gen::Error e = gen::plan(p, plan);
  if (e != gen::Error::None) return badRequest(r, gen::message(e));
  json::Secret password;
  if (!gen::generate(plan, hardwareRandom, password.s))
    return http::sendError(r, http::k500, "failed", "Could not generate a password");

  // Never logged, never stored: it lives in this response (wiped allocator) only.
  json::Ptr o(cJSON_CreateObject());
  cJSON_AddStringToObject(o.get(), "password", password.s.c_str());
  cJSON_AddNumberToObject(o.get(), "entropyBits", std::floor(gen::entropyBits(plan)));
  return http::sendJson(r, http::k200, o.get());
}

esp_err_t postTypeText(httpd_req_t* r, const cJSON* b) {
  for (const char* other : {"id", "what", "test", "submit"}) {
    if (cJSON_HasObjectItem(b, other)) return badRequest(r, "\"text\" cannot be combined with id, what, test or submit");
  }
  auto text = std::make_shared<actions::FreeText>();  // wiped when the last holder drops it
  if (json::getString(b, "text", text->text) != Field::Ok) return badRequest(r, "\"text\" (string) is required");
  if (!validate::typeText(text->text))
    return badRequest(r, "text must be 1-256 characters Keyra can type (printable ASCII, no control characters)");
  int64_t repeat = 1;
  if (json::getInt(b, "repeat", 1, 2, repeat) == Field::BadType) return badRequest(r, "repeat must be 1 or 2");
  std::string sep = "tab";
  Field f;
  if ((f = json::getString(b, "separator", sep)) == Field::BadType || (sep != "tab" && sep != "enter"))
    return badRequest(r, "separator must be \"tab\" or \"enter\"");
  text->twice = repeat == 2;
  text->enterBetween = sep == "enter";

  const actions::Pending p = machine().arm({0, std::string(), actions::What::Text, false, std::move(text)});
  json::Ptr o(cJSON_CreateObject());
  cJSON* po = cJSON_AddObjectToObject(o.get(), "pending");
  cJSON_AddStringToObject(po, "kind", "type");
  cJSON_AddNumberToObject(po, "id", 0);
  cJSON_AddNullToObject(po, "title");
  cJSON_AddStringToObject(po, "what", actions::whatName(actions::What::Text));
  cJSON_AddBoolToObject(po, "submit", false);
  cJSON_AddNumberToObject(po, "expiresIn", static_cast<double>(p.expiresInMs));
  return http::sendJson(r, http::k202, o.get());
}

void addTitle(cJSON* o, actions::What what, const std::string& title) {
  if (what == actions::What::Text) {
    cJSON_AddNullToObject(o, "title");
  } else {
    cJSON_AddStringToObject(o, "title", title.c_str());
  }
}

void addHistory(cJSON* o, const vault::Entry& e) {
  cJSON* list = cJSON_AddArrayToObject(o, "history");
  for (const vault::OldPassword& h : e.history) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "password", h.password.c_str());
    cJSON_AddNumberToObject(item, "changedAt", static_cast<double>(h.changedAt));
    cJSON_AddItemToArray(list, item);
  }
}

}  // namespace keyra::api::genapi
