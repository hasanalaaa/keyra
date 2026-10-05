// keyra::hid public API on top of TinyUSB (SPEC §4.1).
#include "keyra/hid.hpp"

#include <atomic>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "typer.hpp"
#include "usb_desc.hpp"
#include "usb_dev.hpp"

namespace keyra::hid {
namespace {

constexpr const char* TAG = "keyra_hid";
// Upper bound for the host to poll the previous report (bInterval is 5 ms).
// Longer means the host stopped listening: abort rather than queue keys.
constexpr int64_t kReportReadyTimeoutUs = 100 * 1000;

std::atomic<uint8_t> s_leds{0};
std::atomic<bool> s_started{false};

void sleepMs(uint32_t ms) {
  if (ms == 0) return;
  // Round up so a short delay never collapses to zero ticks at 100 Hz.
  vTaskDelay((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
}

class TinyUsbTransport final : public Transport {
 public:
  bool ready() override { return s_started.load() && tud_mounted() && !tud_suspended(); }
  bool capsLock() override { return (s_leds.load() & desc::kLedCapsLockBit) != 0; }
  void delayMs(uint32_t ms) override { sleepMs(ms); }

  bool send(uint8_t modifier, uint8_t keycode) override {
    const int64_t deadline = esp_timer_get_time() + kReportReadyTimeoutUs;
    while (!tud_hid_ready()) {
      if (!tud_mounted() || tud_suspended() || esp_timer_get_time() > deadline) return false;
      vTaskDelay(1);
    }
    uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
    return tud_hid_keyboard_report(0, modifier, keys);
  }
};

TinyUsbTransport s_transport;
Typer s_typer{s_transport};

}  // namespace

void init(bool devCdc) {
  bool expected = false;
  if (!s_started.compare_exchange_strong(expected, true)) {
    ESP_LOGE(TAG, "init called twice");
    abort();
  }
  usbStart(devCdc);
}

void forgetHostLeds() { s_leds.store(0); }

bool mounted() { return s_transport.ready(); }

bool capsLock() { return s_transport.capsLock(); }

Result typeText(const char* text, const Options& opt) { return s_typer.type(text, opt); }

Result tapKey(uint8_t hidKeycode, const Options& opt) { return s_typer.tap(hidKeycode, opt); }

}  // namespace keyra::hid

// ---- TinyUSB HID class callbacks (C linkage, called from the USB task) ----

extern "C" uint8_t const* tud_hid_descriptor_report_cb(uint8_t /*instance*/) {
  return keyra::hid::desc::kHidReport.data();
}

extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t*, uint16_t) {
  return 0;  // GET_REPORT unsupported → STALL; hosts read input via the interrupt EP
}

extern "C" void tud_hid_set_report_cb(uint8_t, uint8_t report_id, hid_report_type_t type, uint8_t const* buf,
                                      uint16_t len) {
  // LED output report (no report ID): one byte, bit 1 = Caps Lock.
  if (type == HID_REPORT_TYPE_OUTPUT && report_id == 0 && len >= 1) keyra::hid::s_leds.store(buf[0]);
}
