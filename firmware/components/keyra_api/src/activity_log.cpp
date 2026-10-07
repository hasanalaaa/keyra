#include <mutex>

#include "activity.hpp"
#include "esp_log.h"
#include "keyra/vault.hpp"
#include "runtime.hpp"

namespace keyra::api::activity {
namespace {

const char* TAG = "activity";
std::mutex g_mu;  // read-modify-write of the one record

}  // namespace

void log(Event e) {
  if (!vault::unlocked()) return;
  if (e.at == 0) e.at = unixSecondsOrZero();
  std::lock_guard<std::mutex> lock(g_mu);
  std::vector<uint8_t> raw;
  std::vector<Event> events;
  const vault::Status st = vault::activityRead(raw);
  // An unreadable log is started again rather than block logging for good.
  if (st != vault::Status::Ok || !decode(raw, events)) {
    if (st == vault::Status::Locked) return;
    ESP_LOGW(TAG, "stored log unreadable (%s): starting a new one", vault::statusName(st));
    events.clear();
  }
  append(events, std::move(e));
  std::vector<uint8_t> out = encode(events);
  // Titles are short, but 200 events of 64-byte titles would pass the vault's cap.
  while (out.size() > vault::kMaxActivityBytes && events.size() > 1) {
    events.erase(events.begin(), events.begin() + 10 < events.end() ? events.begin() + 10 : events.end() - 1);
    out = encode(events);
  }
  const vault::Status w = vault::activityWrite(out);
  if (w != vault::Status::Ok) ESP_LOGW(TAG, "write failed: %s", vault::statusName(w));
}

void log(Kind k, uint32_t id, std::string title, uint8_t detail, uint32_t n) {
  Event e;
  e.kind = k;
  e.id = id;
  e.title = std::move(title);
  e.detail = detail;
  e.n = n;
  log(std::move(e));
}

bool list(std::vector<Event>& out) {
  std::lock_guard<std::mutex> lock(g_mu);
  std::vector<uint8_t> raw;
  if (vault::activityRead(raw) != vault::Status::Ok) return false;
  return decode(raw, out);
}

}  // namespace keyra::api::activity
