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
#include "keyra/ble.hpp"
#include "keyra/fido.hpp"
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

using actions::Code;
using actions::What;

TaskHandle_t g_typeTask = nullptr;
actions::TypeRequest g_job;  // written by the actions task only while no job runs
std::atomic<bool> g_typing{false};
bool g_bleArmed = false;  // actions task only: an armed action asked keyra_ble for its host

using Kind = Target::Kind;
std::atomic<int64_t> g_netAt{0};

void fillRandom(uint8_t* p, size_t n) { esp_fill_random(p, n); }

Code fromHid(hid::Result r, const hid::Options& o) {
  switch (r) {
    case hid::Result::Ok: return Code::Typed;
    case hid::Result::NotMounted:  // the chosen host went away mid-job
      return o.via == hid::Host::Usb ? Code::NoUsb : Code::NoHost;
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
    case actions::Indicator::Pairing: return io::Led::Pairing;
  }
  return io::Led::Off;
}

// Types one string; refuses up front if any character is untypeable so a
// password is never half-typed.
Code typeOne(const std::string& text, const hid::Options& o) {
  if (text.empty()) return Code::Failed;
  if (!hid::typeable(text.c_str())) return Code::UnsupportedChar;
  return fromHid(hid::typeText(text.c_str(), o), o);
}

bool bleReadyFor(const BtAddr& addr) { return hid::bleConnected() && ble::linked() == addr; }

Code runJob(const actions::TypeRequest& job) {
  // The computer was chosen when the action was armed: every part of this
  // job (username, Tab, password, Enter) goes to it even if a cable is
  // plugged in halfway.
  switch (job.target.kind) {
    case Kind::None: return Code::NoHost;
    case Kind::Usb:
      if (!hid::mounted()) return Code::NoUsb;
      break;
    case Kind::Ble:
      if (!bleReadyFor(job.target.addr)) return Code::NoHost;
      break;
  }
  const settings::Settings s = settings::get();
  const hid::Options o{s.keyDelayMs, job.target.kind == Kind::Ble ? hid::Host::Ble : hid::Host::Usb};
  if (job.what == What::Test) return fromHid(hid::typeText(kTestString, o), o);

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
        c = fromHid(hid::tapKey(s.bothSeparator == settings::Separator::Enter ? hid::KEY_ENTER : hid::KEY_TAB, o), o);
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
  if (c == Code::Typed && job.submit) c = fromHid(hid::tapKey(hid::KEY_ENTER, o), o);
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
    // Keep the Bluetooth link a little for a quick second action, then let go.
    if (job.target.kind == Kind::Ble) ble::done();
    g_typing = false;
    machine().typingFinished(job, c);
    sessions().activity(monoMs());
  }
}

void onButton(io::Button b) {
  sessions().activity(monoMs());
  // A website waiting for the security-key touch owns the button (its LED
  // pattern is showing); an armed type action stays armed for the next press.
  if (fido::awaitingTouch()) {
    fido::press(b == io::Button::Short);
    return;
  }
  const auto press = b == io::Button::Short ? actions::Button::Short : actions::Button::Long;
  actions::Decision d = machine().onButton(press, vault::unlocked());
  switch (d.effect) {
    case actions::Effect::Run:
      g_job = d.run;
      g_typing = true;
      g_bleArmed = false;  // the job owns the link now; typeTask releases it
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

// Keeps keyra_ble's demand in step with the armed action: ask for its host
// while it waits, let go at once when it is cancelled, expires or is replaced.
void syncBleDemand() {
  const auto p = machine().pending();
  if (p && p->req.target.kind == Kind::Ble) {
    ble::want(p->req.target.addr);
    machine().setLinkReady(bleReadyFor(p->req.target.addr));
    g_bleArmed = true;
  } else if (g_bleArmed && !g_typing) {
    ble::drop();
    g_bleArmed = false;
  }
}

void actionsTask(void*) {
  bool first = true;
  io::Led shown = io::Led::Off;
  for (;;) {
    io::Button b;
    syncBleDemand();
    const bool pressed = io::nextButton(b, pdMS_TO_TICKS(100));
    if (pressed) {
      syncBleDemand();  // the link may have come up while we waited
      onButton(b);
    }
    maybeAutoLock();
    maybeReconfigureNet();
    const io::Led want = fido::ledActive()
                             ? io::Led::Fido
                             : toLed(machine().indicator(vault::initialized(), vault::unlocked(), ble::pairing()));
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
  // Locking means "I'm walking away": no new host may pair after that.
  ble::closePairing();
}

void reconfigureNetSoon() { g_netAt = monoMs() + kNetDelayMs; }

void safeRestart() {
  // Resetting with GPIO0 low latches ROM download mode and the device looks
  // dead, so there is deliberately no timeout: wait for the button release.
  for (int waited = 0; !io::bootPinHigh(); ++waited) {
    if (waited % 50 == 0) ESP_LOGW(TAG, "restart waiting for the button (GPIO0) to be released");
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  esp_restart();
}

esp_err_t startTasks() {
  // Typing calls into the vault (decrypt) and TinyUSB; commits run setup/restore.
  if (xTaskCreate(typeTask, "type", 6144, nullptr, 5, &g_typeTask) != pdPASS) return ESP_ERR_NO_MEM;
  if (xTaskCreate(actionsTask, "actions", 8192, nullptr, 5, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  return ESP_OK;
}

}  // namespace keyra::api
