#include "runtime.hpp"

#include <sys/time.h>

#include <atomic>
#include <cstring>
#include <mutex>

#include "clock.hpp"
#include "handlers_kbd.hpp"
#include "sequence_run.hpp"
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
// For layouts without Latin letters (Arabic): "Keyra test" in Arabic.
constexpr const char* kTestStringArabic =
    "\xD8\xA7\xD8\xAE\xD8\xAA\xD8\xA8\xD8\xA7\xD8\xB1 \xD9\x83\xD9\x8A\xD8\xB1\xD8\xA7 123";
constexpr int64_t kNetDelayMs = 3000;

using actions::Code;
using actions::What;

TaskHandle_t g_typeTask = nullptr;
actions::TypeRequest g_job;  // written by the actions task only while no job runs
std::atomic<bool> g_typing{false};
bool g_bleArmed = false;  // actions task only: an armed action asked keyra_ble for its host

using Kind = Target::Kind;
std::atomic<int64_t> g_netAt{0};
// Auto-lock when the computer goes away (SPEC §12.4). Fed by the actions task
// and (bleUsed) the type task.
std::mutex g_watchMu;
actions::HostWatch g_watch;

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
  if (!hid::typeable(text, o)) return Code::UnsupportedChar;
  return fromHid(hid::typeText(text.c_str(), o), o);
}

// Sequences type through the same engine: every call releases all keys.
class HidKeys final : public seqrun::Keys {
 public:
  explicit HidKeys(const hid::Options& o) : o_(o) {}
  bool typeable(const std::string& t) override { return hid::typeable(t, o_); }
  Code text(const std::string& t) override { return typeOne(t, o_); }
  Code key(uint8_t k) override { return fromHid(hid::tapKey(k, o_), o_); }
  void delayMs(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }

 private:
  const hid::Options& o_;
};

Code runSequence(const actions::TypeRequest& job, const vault::Entry& e, const hid::Options& o) {
  seqrun::Fields f;
  f.username = e.username;
  f.password = e.password;
  char code[11] = {};
  if (seqrun::needs(*job.seq).totp) {
    if (!timeValid() || !totp::code(e.totp, unixMs() / 1000, code, nullptr, nullptr)) return Code::Failed;
    f.totp = code;
    std::memset(code, 0, sizeof code);
  }
  HidKeys keys(o);
  Code c = job.part == 0 ? seqrun::checkAll(*job.seq, f, keys) : Code::Typed;
  if (c == Code::Typed) c = seqrun::runPart(*job.seq, job.part, f, keys);
  vault::wipe(f.username);
  vault::wipe(f.password);
  vault::wipe(f.totp);
  return c;
}

bool bleReadyFor(const BtAddr& addr) { return hid::bleConnected() && ble::linked() == addr; }

// Free text (SPEC §9.2), optionally twice with Tab/Enter between (confirm fields).
Code typeFree(const actions::FreeText& t, const hid::Options& o) {
  Code c = typeOne(t.text, o);
  if (c == Code::Typed && t.twice) {
    c = fromHid(hid::tapKey(t.enterBetween ? hid::KEY_ENTER : hid::KEY_TAB, o), o);
    if (c == Code::Typed) c = typeOne(t.text, o);
  }
  return c;
}

Code typeJob(const actions::TypeRequest& job, const settings::Settings& s, const hid::Options& o) {
  if (job.what == What::Test)
    return typeOne(hid::typeable(kTestString, o) ? kTestString : kTestStringArabic, o);
  if (job.what == What::Probe) return fromHid(hid::typeProbe(o), o);
  if (job.what == What::Text) return job.text ? typeFree(*job.text, o) : Code::Failed;

  vault::Entry e;
  if (vault::get(job.id, e) != vault::Status::Ok) return Code::Failed;
  Code c = Code::Failed;
  switch (job.what) {
    case What::Username: c = typeOne(e.username, o); break;
    case What::Password: c = typeOne(e.password, o); break;
    case What::Both:
      if (!hid::typeable(e.username, o) || !hid::typeable(e.password, o)) {
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
    case What::Sequence: c = job.seq ? runSequence(job, e, o) : Code::Failed; break;
    case What::Test:
    case What::Text:
    case What::Probe: break;
  }
  vault::wipe(e);
  if (c == Code::Typed && job.submit) c = fromHid(hid::tapKey(hid::KEY_ENTER, o), o);
  if (c == Code::Typed) {
    const int64_t now = unixSecondsOrZero();
    if (now != 0 && vault::touch(job.id, now) != vault::Status::Ok) ESP_LOGW(TAG, "touch failed");
  }
  return c;
}

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
  hid::Options o{s.keyDelayMs, job.target.kind == Kind::Ble ? hid::Host::Ble : hid::Host::Usb,
                 kbdapi::layoutFor(job.target, s)};
  // A keyboard sends key positions; the host's input language picks the
  // characters (SPEC §10.5). Windows takes Alt codes whatever the language;
  // macOS/iOS are switched to the previous (Latin) source and back.
  const hostos::Os os = kbdapi::osFor(job.target, s);
  o.altCodes = os == hostos::Os::Windows;
  const bool toggle = job.switchLang && hostos::switchesWithCtrlSpace(os);
  ESP_LOGI(TAG, "host system '%s', switch language %d, alt codes %d", hostos::name(os), int(toggle), int(o.altCodes));
  if (toggle) {
    const Code c = fromHid(hid::tapChord(hid::MOD_LEFT_CTRL, hid::KEY_SPACE, o), o);
    if (c != Code::Typed) return c;
  }
  Code c = typeJob(job, s, o);
  // Back to the user's language even when typing failed partway.
  if (toggle) {
    const Code back = fromHid(hid::tapChord(hid::MOD_LEFT_CTRL, hid::KEY_SPACE, o), o);
    if (c == Code::Typed) c = back;
  }
  return c;
}

void typeTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Moved, not copied: when `job` goes out of scope any free text is released (and wiped).
    const actions::TypeRequest job = std::move(g_job);
    const Code c = runJob(job);
    ESP_LOGI(TAG, "type %s: %s", actions::whatName(job.what), actions::codeName(c));
    // Keep the Bluetooth link a little for a quick second action, then let go.
    if (job.target.kind == Kind::Ble) {
      ble::done();
      if (c == Code::Typed) {
        std::lock_guard<std::mutex> lock(g_watchMu);
        g_watch.bleUsed(job.target.addr);
      }
    }
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
      g_job = std::move(d.run);
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
  const settings::Settings s = settings::get();
  const bool usb = hid::mounted();
  // Before the lock below, so an action bound to the vanished computer reports host_changed.
  machine().setUsbMounted(usb);
  const std::optional<ble::Addr> lost = ble::takeLost();
  bool hostGone = false;
  {
    std::lock_guard<std::mutex> lock(g_watchMu);
    hostGone = g_watch.pollUsb(monoMs(), vault::unlocked(), usb, s.lockOnUsb);
    if (hostGone) ESP_LOGI(TAG, "USB host gone: auto-lock");
    if (lost && g_watch.bleLost(*lost, vault::unlocked(), s.lockOnBle)) {
      ESP_LOGI(TAG, "Bluetooth host gone: auto-lock");
      hostGone = true;
    }
  }
  if (hostGone) {
    lockAll();
    return;
  }
  if (!vault::unlocked()) return;
  const int64_t limit = int64_t{s.autoLockMin} * 60 * 1000;
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
    // Not while a job types: asking for another host would cut the link that
    // is receiving a password. The next pass (every ~100 ms) asks again.
    if (!g_typing) ble::want(p->req.target.addr);
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
  static actions::Machine m(monoMs, [] {
    uint8_t raw[16];
    esp_fill_random(raw, sizeof raw);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string t(32, '0');
    for (size_t i = 0; i < sizeof raw; ++i) {
      t[2 * i] = kHex[raw[i] >> 4];
      t[2 * i + 1] = kHex[raw[i] & 0x0F];
    }
    return t;
  });
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
