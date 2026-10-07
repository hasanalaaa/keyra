#include "handlers_kbd.hpp"

#include "http.hpp"
#include "keyra/sequence.hpp"
#include "runtime.hpp"
#include "sequence_run.hpp"

namespace keyra::api::kbdapi {
namespace {

using json::Field;

constexpr int kMaxLayoutsSafe = 8;

esp_err_t badRequest(httpd_req_t* r, const char* message) { return http::sendError(r, http::k400, "invalid", message); }

hid::Layout byId(const std::string& id) {
  hid::Layout l = hid::kLayoutUs;
  hid::findLayout(id, l);  // settings are sanitised at load: unknown ids never get here
  return l;
}

bool readLayout(const cJSON* b, const char* key, std::string& out, bool& bad) {
  std::string v;
  const Field f = json::getString(b, key, v);
  hid::Layout l;
  if (f == Field::BadType || (f == Field::Ok && !hid::findLayout(v, l))) {
    bad = true;
    return false;
  }
  if (f == Field::Ok) out = v;
  return true;
}

}  // namespace

hid::Layout layoutFor(const Target& t, const settings::Settings& s) {
  return byId(t.kind == Target::Kind::Ble ? s.layoutBle : s.layoutUsb);
}

esp_err_t getKeyboard(httpd_req_t* r) {
  const settings::Settings s = settings::get();
  json::Ptr o(cJSON_CreateObject());
  cJSON* list = cJSON_AddArrayToObject(o.get(), "layouts");
  for (size_t i = 0; i < hid::layoutCount(); ++i) {
    const auto l = static_cast<hid::Layout>(i);
    const hid::LayoutInfo info = hid::layoutInfo(l);
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "id", info.id);
    cJSON_AddStringToObject(item, "name", info.name);
    cJSON_AddStringToObject(item, "platform", info.platform);
    // Not yet confirmed on real hardware (SPEC §10.1): the UI says so.
    cJSON_AddBoolToObject(item, "experimental", l != hid::kLayoutUs);
    cJSON_AddStringToObject(item, "probe", hid::probeText(l).c_str());
    cJSON_AddItemToArray(list, item);
  }
  cJSON_AddStringToObject(o.get(), "usb", s.layoutUsb.c_str());
  cJSON_AddStringToObject(o.get(), "ble", s.layoutBle.c_str());
  return http::sendJson(r, http::k200, o.get());
}

void addSettings(cJSON* o, const settings::Settings& s) {
  cJSON_AddStringToObject(o, "layoutUsb", s.layoutUsb.c_str());
  cJSON_AddStringToObject(o, "layoutBle", s.layoutBle.c_str());
  cJSON_AddStringToObject(o, "bothSequence", s.bothSequence.c_str());
}

bool readSettings(httpd_req_t* r, const cJSON* b, settings::Settings& next, esp_err_t& err) {
  bool bad = false;
  if (!readLayout(b, "layoutUsb", next.layoutUsb, bad) || !readLayout(b, "layoutBle", next.layoutBle, bad)) {
    err = badRequest(r, "layoutUsb/layoutBle must be a layout id from GET /api/keyboard");
    return false;
  }
  std::string sq;
  const Field f = json::getString(b, "bothSequence", sq);
  if (f == Field::BadType) {
    err = badRequest(r, "bothSequence must be a string");
    return false;
  }
  if (f == Field::Ok) {
    const seq::Error e = sq.empty() ? seq::Error::None : seq::parse(sq, nullptr);
    if (e != seq::Error::None) {
      err = badRequest(r, seq::message(e));
      return false;
    }
    next.bothSequence = sq;
  }
  return true;
}

bool layoutSafeChars(httpd_req_t* r, const cJSON* b, bool& restrict, std::string& allowed, esp_err_t& err) {
  restrict = false;
  if (json::getBool(b, "layoutSafe", restrict) == Field::BadType) {
    err = badRequest(r, "layoutSafe must be a boolean");
    return false;
  }
  const cJSON* list = cJSON_GetObjectItemCaseSensitive(b, "layouts");
  if (!restrict) {
    if (list != nullptr) {
      err = badRequest(r, "layouts needs layoutSafe");
      return false;
    }
    return true;
  }
  hid::Layout chosen[kMaxLayoutsSafe];
  int n = 0;
  if (list == nullptr) {  // the computers Keyra is set up for
    const settings::Settings s = settings::get();
    chosen[n++] = byId(s.layoutUsb);
    if (s.layoutBle != s.layoutUsb) chosen[n++] = byId(s.layoutBle);
  } else {
    const int size = cJSON_IsArray(list) ? cJSON_GetArraySize(list) : -1;
    if (size < 1 || size > kMaxLayoutsSafe) {
      err = badRequest(r, "layouts must be 1-8 layout ids");
      return false;
    }
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, list) {
      if (!cJSON_IsString(item) || !hid::findLayout(item->valuestring, chosen[n])) {
        err = badRequest(r, "layouts must be 1-8 layout ids");
        return false;
      }
      ++n;
    }
  }
  allowed.clear();
  for (uint32_t c = 0x21; c < 0x7F; ++c)
    if (hid::sameOnAll(c, chosen, static_cast<size_t>(n))) allowed += static_cast<char>(c);
  return true;
}

bool sequenceRequest(httpd_req_t* r, const vault::Entry& e, const Target& target, actions::TypeRequest& out,
                     esp_err_t& err) {
  const settings::Settings s = settings::get();
  const std::string src = !e.sequence.empty()      ? e.sequence
                          : !s.bothSequence.empty() ? s.bothSequence
                                                    : seqrun::builtIn(s.bothSeparator == settings::Separator::Enter,
                                                                      s.submitAfterBoth);
  auto job = std::make_shared<actions::SeqJob>();
  const seq::Error pe = seqrun::build(src, *job);
  if (pe != seq::Error::None) {  // cannot happen: every stored sequence passed the same parser
    err = http::sendError(r, http::k500, "corrupt", seq::message(pe));
    return false;
  }
  const seqrun::Needs need = seqrun::needs(*job);
  if ((need.username && e.username.empty()) || (need.password && e.password.empty()) ||
      (need.totp && e.totp.empty())) {
    err = badRequest(r, "Entry has no value for a field its sequence types");
    return false;
  }
  if (need.totp && !timeValid()) {
    err = http::sendError(r, http::k409, "no_time", "Device clock is not set");
    return false;
  }
  out = {e.id, e.title, actions::What::Sequence, false, target, nullptr, std::move(job), 0};
  return true;
}

void addPending(cJSON* p, const actions::TypeRequest& req) {
  if (req.what != actions::What::Sequence || !req.seq) return;
  cJSON_AddStringToObject(p, "preview", req.seq->preview.c_str());
  cJSON_AddNumberToObject(p, "part", req.part + 1);
  cJSON_AddNumberToObject(p, "parts", req.seq->parts);
}

void addEntry(cJSON* o, const vault::Entry& e) { cJSON_AddStringToObject(o, "sequence", e.sequence.c_str()); }

}  // namespace keyra::api::kbdapi
