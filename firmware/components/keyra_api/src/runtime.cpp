#include "runtime.hpp"

#include <sys/time.h>

#include <atomic>
#include <cstring>

#include "clock.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keyra/hid.hpp"
#include "keyra/io.hpp"
#include "keyra/net.hpp"
#include "keyra/settings.hpp"
#include "keyra/vault.hpp"

namespace keyra::api {
namespace {

const char* TAG = "actions";
constexpr const char* kTestString = "Keyra test 123 !@#";
constexpr int64_t kNetDelayMs = 3000;
constexpr int kRestartWaitTicks = 150;  // × 100 ms = 15 s

using actions::Code;
using actions::What;

TaskHandle_t g_typeTask = nullptr;
actions::TypeRequest g_job;  // written by the actions task only while no job runs
std::atomic<int64_t> g_netAt{0};

void fillRandom(uint8_t* p, size_t n) { esp_fill_random(p, n); }

Code fromHid(hid::Result r) {
  switch (r) {
    case hid::Result::Ok: return Code::Typed;
    case hid::Result::NotMounted: return Code::NoUsb;
    case hid::Result::Unsupported: return Code::UnsupportedChar;
    case hid::Result::Busy:
    case hid::Result::Failed: return Code::Failed;
  }
  return Code::Failed;
}

io::Led toLed(actions::Indicator i) {
  switch (i) {
    case actions::Indicator::Setup: return io::Led::Setup;
    case actions::Indicator::Locked: return io::Led::Locked;
    case actions::Indicator::Idle: return io::Led::Idle;
    case actions::Indicator::Pending: return io::Led::Pending;
    case actions::Indicator::Typing: return io::Led::Typing;
    case actions::Indicator::AwaitPresence: return io::Led::AwaitPresence;
    case actions::Indicator::Success: return io::Led::Success;
    case actions::Indicator::Error: return io::Led::Error;
    case actions::Indicator::Off: return io::Led::Off;
  }
  return io::Led::Off;
}

// Types one string; refuses up front if any character is untypeable so a
// password is never half-typed.
Code typeOne(const std::string& text, const hid::Options& o) {
  if (text.empty()) return Code::Failed;
  if (!hid::typeable(text.c_str())) return Code::UnsupportedChar;
  return fromHid(hid::typeText(text.c_str(), o));
}

Code runJob(const actions::TypeRequest& job) {
  if (!hid::mounted()) return Code::NoUsb;
  const settings::Settings s = settings::get();
  const hid::Options o{s.keyDelayMs};
  if (job.what == What::Test) return fromHid(hid::typeText(kTestString, o));

  vault::Entry e;
  if (vault::get(job.id, e) != vault::Status::Ok) return Code::Failed;
  Code c = Code::Failed;
  switch (job.what) {
    case What::Username: c = typeOne(e.username, o); break;
    case What::Password: c = typeOne(e.password, o); break;
    case What::Both:
      if (!hid::typeable(e.username.c_str()) || !hid::typeable(e.password.c_str())) {
        c = Code::UnsupportedChar;
        break;
      }
      c = typeOne(e.username, o);
      if (c == Code::Typed)
        c = fromHid(hid::tapKey(s.bothSeparator == settings::Separator::Enter ? hid::KEY_ENTER : hid::KEY_TAB, o));
      if (c == Code::Typed) c = typeOne(e.password, o);
      break;
    case What::Totp: {
      char code[11] = {};
      if (timeValid() && totp::code(e.totp, unixMs() / 1000, code, nullptr, nullptr)) c = typeOne(code, o);
      std::memset(code, 0, sizeof code);
      break;
    }
    case What::Test: break;
  }
  vault::wipe(e);
  if (c == Code::Typed && job.submit) c = fromHid(hid::tapKey(hid::KEY_ENTER, o));
  if (c == Code::Typed) {
    const int64_t now = unixSecondsOrZero();
    if (now != 0 && vault::touch(job.id, now) != vault::Status::Ok) ESP_LOGW(TAG, "touch failed");
  }
  return c;
}

void typeTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const actions::TypeRequest job = g_job;
    const Code c = runJob(job);
    ESP_LOGI(TAG, "type %s: %s", actions::whatName(job.what), actions::codeName(c));
    machine().typingFinished(job, c);
    sessions().activity(monoMs());
  }
}

void onButton(io::Button b) {
  sessions().activity(monoMs());
  const auto press = b == io::Button::Short ? actions::Button::Short : actions::Button::Long;
  actions::Decision d = machine().onButton(press, vault::unlocked());
  switch (d.effect) {
    case actions::Effect::Run:
      g_job = d.run;
      xTaskNotifyGive(g_typeTask);
      break;
    case actions::Effect::Approve: {
      ESP_LOGI(TAG, "approved %s", actions::opName(d.op));
      const bool ok = d.commit();
      d.commit = nullptr;  // release captured secrets now, not at scope end
      if (!ok) ESP_LOGE(TAG, "%s failed", actions::opName(d.op));
      machine().commitFinished(ok);
      break;
    }
    case actions::Effect::Lock: lockAll(); break;
    case actions::Effect::None:
    case actions::Effect::Blink:
    case actions::Effect::Cancelled: break;
  }
}

void maybeAutoLock() {
  if (!vault::unlocked()) return;
  const int64_t limit = int64_t{settings::get().autoLockMin} * 60 * 1000;
  if (sessions().idleFor(monoMs(), limit)) {
    ESP_LOGI(TAG, "idle auto-lock");
    lockAll();
  }
}

void maybeReconfigureNet() {
  const int64_t at = g_netAt.load();
  if (at == 0 || monoMs() < at) return;
  g_netAt = 0;
  const settings::Settings s = settings::get();
  const esp_err_t err = net::reconfigure({settings::ssid(s), s.wifiPassword, 6});
  if (err != ESP_OK) ESP_LOGE(TAG, "AP reconfigure failed: %s", esp_err_to_name(err));
}

void actionsTask(void*) {
  bool first = true;
  io::Led shown = io::Led::Off;
  for (;;) {
    io::Button b;
    if (io::nextButton(b, pdMS_TO_TICKS(100))) onButton(b);
    maybeAutoLock();
    maybeReconfigureNet();
    const io::Led want = toLed(machine().indicator(vault::initialized(), vault::unlocked()));
    if (first || want != shown) {
      io::led(want);
      shown = want;
      first = false;
    }
  }
}

}  // namespace

actions::Machine& machine() {
  static actions::Machine m(monoMs);
  return m;
}

Sessions& sessions() {
  static Sessions s(fillRandom);
  return s;
}

int64_t monoMs() { return esp_timer_get_time() / 1000; }

int64_t unixMs() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return int64_t{tv.tv_sec} * 1000 + tv.tv_usec / 1000;
}

bool timeValid() { return clock::valid(unixMs()); }

int64_t unixSecondsOrZero() { return timeValid() ? unixMs() / 1000 : 0; }

void lockAll() {
  vault::lock();
  sessions().clear();
  machine().dropSessionItems();
}

void reconfigureNetSoon() { g_netAt = monoMs() + kNetDelayMs; }

void safeRestart() {
  int waited = 0;
  while (!io::bootPinHigh() && waited++ < kRestartWaitTicks) vTaskDelay(pdMS_TO_TICKS(100));
  if (!io::bootPinHigh()) ESP_LOGW(TAG, "GPIO0 still low after 15 s; restarting anyway");
  esp_restart();
}

esp_err_t startTasks() {
  // Typing calls into the vault (decrypt) and TinyUSB; commits run setup/restore.
  if (xTaskCreate(typeTask, "type", 6144, nullptr, 5, &g_typeTask) != pdPASS) return ESP_ERR_NO_MEM;
  if (xTaskCreate(actionsTask, "actions", 8192, nullptr, 5, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  return ESP_OK;
}

}  // namespace keyra::api
