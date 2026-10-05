// keyra::io — BOOT button + WS2812 status LED, one task (SPEC §2, §4.1).
#include "keyra/io.hpp"

#include <atomic>
#include <cstdlib>

#include "button_classifier.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_anim.hpp"
#include "led_strip.h"
#include "sdkconfig.h"

namespace keyra::io {
namespace {

constexpr const char* TAG = "keyra_io";
constexpr gpio_num_t kButtonGpio = GPIO_NUM_0;
constexpr uint32_t kTickMs = 10;        // button sampling period
constexpr uint32_t kLedEveryTicks = 2;  // LED frame every 20 ms (50 Hz)
constexpr UBaseType_t kQueueDepth = 8;

QueueHandle_t s_buttons = nullptr;
led_strip_handle_t s_strip = nullptr;

// Writers are other tasks; the IO task is the only reader. The sequence
// number lets a repeated led(Success) restart the flash.
std::atomic<uint32_t> s_ledReq{static_cast<uint32_t>(Pattern::Off)};
std::atomic<uint32_t> s_ledSeq{0};
std::atomic<uint8_t> s_brightness{100};

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

Pattern toPattern(Led l) {
  switch (l) {
    case Led::Off:           return Pattern::Off;
    case Led::Locked:        return Pattern::Locked;
    case Led::Idle:          return Pattern::Idle;
    case Led::Pending:       return Pattern::Pending;
    case Led::Typing:        return Pattern::Typing;
    case Led::Success:       return Pattern::Success;
    case Led::Error:         return Pattern::Error;
    case Led::AwaitPresence: return Pattern::AwaitPresence;
    case Led::Setup:         return Pattern::Setup;
  }
  return Pattern::Off;
}

void initLed() {
  led_strip_config_t strip = {};
  strip.strip_gpio_num = CONFIG_KEYRA_LED_GPIO;
  strip.max_leds = 1;
  strip.led_model = LED_MODEL_WS2812;
  strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  led_strip_rmt_config_t rmt = {};
  rmt.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt.resolution_hz = 10 * 1000 * 1000;
  const esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_strip);
  if (err != ESP_OK) {
    // The button (the security-critical half) must keep working without it.
    ESP_LOGE(TAG, "status LED on GPIO%d unavailable: %s", CONFIG_KEYRA_LED_GPIO, esp_err_to_name(err));
    s_strip = nullptr;
    return;
  }
  led_strip_clear(s_strip);
}

void render(const Rgb& c) {
  if (s_strip == nullptr) return;
  if (led_strip_set_pixel(s_strip, 0, c.r, c.g, c.b) != ESP_OK || led_strip_refresh(s_strip) != ESP_OK) {
    ESP_LOGW(TAG, "LED refresh failed");
  }
}

void ioTask(void*) {
  ButtonClassifier button;
  LedAnimator anim;
  uint32_t seenSeq = s_ledSeq.load();
  uint32_t tick = 0;
  Rgb last{1, 1, 1};  // force the first frame out
  TickType_t wake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(kTickMs) > 0 ? pdMS_TO_TICKS(kTickMs) : 1;

  for (;;) {
    const uint32_t now = nowMs();

    const Press p = button.update(gpio_get_level(kButtonGpio) == 0, now);
    if (p != Press::None) {
      const Button ev = p == Press::Short ? Button::Short : Button::Long;
      if (xQueueSend(s_buttons, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "button queue full, press dropped");
    }

    if (++tick % kLedEveryTicks == 0) {
      const uint32_t seq = s_ledSeq.load();
      if (seq != seenSeq) {
        seenSeq = seq;
        anim.set(static_cast<Pattern>(s_ledReq.load()), now);
      }
      anim.brightness(s_brightness.load());
      const Rgb c = anim.frame(now);
      if (c.r != last.r || c.g != last.g || c.b != last.b) {  // skip redundant RMT traffic
        render(c);
        last = c;
      }
    }
    vTaskDelayUntil(&wake, period);
  }
}

}  // namespace

void init() {
  if (s_buttons != nullptr) {
    ESP_LOGE(TAG, "init called twice");
    abort();
  }
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kButtonGpio;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&io));

  s_buttons = xQueueCreate(kQueueDepth, sizeof(Button));
  if (s_buttons == nullptr) {
    ESP_LOGE(TAG, "no memory for button queue");
    abort();
  }
  initLed();
  if (xTaskCreate(ioTask, "keyra_io", 3072, nullptr, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "no memory for IO task");
    abort();
  }
}

bool nextButton(Button& out, TickType_t wait) {
  if (s_buttons == nullptr) return false;
  return xQueueReceive(s_buttons, &out, wait) == pdTRUE;
}

void led(Led state) {
  s_ledReq.store(static_cast<uint32_t>(toPattern(state)));
  s_ledSeq.fetch_add(1);
}

void brightness(uint8_t pct) { s_brightness.store(pct > 100 ? 100 : pct); }

bool bootPinHigh() { return gpio_get_level(kButtonGpio) == 1; }

}  // namespace keyra::io
