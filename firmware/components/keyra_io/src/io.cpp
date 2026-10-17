// keyra::io — confirmation button + status LED, one task (SPEC §2, §4.1).
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
#include "sdkconfig.h"
#if CONFIG_KEYRA_LED_WS2812
#include "led_strip.h"
#endif

namespace keyra::io {
namespace {

constexpr const char* TAG = "keyra_io";
constexpr gpio_num_t kButtonGpio = static_cast<gpio_num_t>(CONFIG_KEYRA_BUTTON_GPIO);
#if CONFIG_KEYRA_BUTTON_ACTIVE_LOW
constexpr int kPressedLevel = 0;
#else
constexpr int kPressedLevel = 1;
#endif
constexpr uint32_t kTickMs = 10;        // button sampling period
constexpr uint32_t kLedEveryTicks = 2;  // LED frame every 20 ms (50 Hz)
constexpr UBaseType_t kQueueDepth = 8;

struct ButtonEvent {
  Button button;
  int64_t atMs;
};

QueueHandle_t s_buttons = nullptr;
#if CONFIG_KEYRA_LED_WS2812
led_strip_handle_t s_strip = nullptr;
#endif

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
    case Led::Pairing:       return Pattern::Pairing;
    case Led::Fido:          return Pattern::Fido;
  }
  return Pattern::Off;
}

#if !CONFIG_KEYRA_LED_NONE
// Boards that gate the LED's supply (TinyS3: LDO2 on GPIO17) keep it off until asked.
void powerLed() {
#if CONFIG_KEYRA_LED_POWER_GPIO >= 0
  constexpr gpio_num_t pin = static_cast<gpio_num_t>(CONFIG_KEYRA_LED_POWER_GPIO);
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << pin;
  io.mode = GPIO_MODE_OUTPUT;
  const esp_err_t err = gpio_config(&io);
  if (err == ESP_OK) gpio_set_level(pin, 1);
  else ESP_LOGE(TAG, "LED power on GPIO%d unavailable: %s", int(pin), esp_err_to_name(err));
#endif
}
#endif

#if CONFIG_KEYRA_LED_WS2812
void initLed() {
  powerLed();
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

// Only real changes go out: no redundant RMT traffic.
void showFrame(LedAnimator& anim, uint32_t now) {
  static Rgb last{1, 1, 1};  // force the first frame out
  const Rgb c = anim.frame(now);
  if (c.r != last.r || c.g != last.g || c.b != last.b) {
    render(c);
    last = c;
  }
}
#elif CONFIG_KEYRA_LED_PLAIN
constexpr gpio_num_t kLedGpio = static_cast<gpio_num_t>(CONFIG_KEYRA_LED_GPIO);
#if CONFIG_KEYRA_LED_ACTIVE_LOW
constexpr int kLedOnLevel = 0;
#else
constexpr int kLedOnLevel = 1;
#endif
bool s_ledOk = false;

void initLed() {
  powerLed();
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kLedGpio;
  io.mode = GPIO_MODE_OUTPUT;
  const esp_err_t err = gpio_config(&io);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "status LED on GPIO%d unavailable: %s", CONFIG_KEYRA_LED_GPIO, esp_err_to_name(err));
    return;
  }
  gpio_set_level(kLedGpio, !kLedOnLevel);
  s_ledOk = true;
}

void showFrame(LedAnimator& anim, uint32_t now) {
  if (s_ledOk) gpio_set_level(kLedGpio, anim.on(now) ? kLedOnLevel : !kLedOnLevel);
}
#else
void initLed() {}
void showFrame(LedAnimator&, uint32_t) {}
#endif

void ioTask(void*) {
  ButtonClassifier button;
  LedAnimator anim;
  uint32_t seenSeq = s_ledSeq.load();
  uint32_t tick = 0;
  TickType_t wake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(kTickMs) > 0 ? pdMS_TO_TICKS(kTickMs) : 1;

  for (;;) {
    const uint32_t now = nowMs();

    const Press p = button.update(gpio_get_level(kButtonGpio) == kPressedLevel, now);
    if (p != Press::None) {
      const ButtonEvent ev{p == Press::Short ? Button::Short : Button::Long, esp_timer_get_time() / 1000};
      if (xQueueSend(s_buttons, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "button queue full, press dropped");
    }

    if (++tick % kLedEveryTicks == 0) {
      const uint32_t seq = s_ledSeq.load();
      if (seq != seenSeq) {
        seenSeq = seq;
        anim.set(static_cast<Pattern>(s_ledReq.load()), now);
      }
      anim.brightness(s_brightness.load());
      showFrame(anim, now);
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
  io.pull_up_en = kPressedLevel == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  io.pull_down_en = kPressedLevel == 0 ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&io));

  s_buttons = xQueueCreate(kQueueDepth, sizeof(ButtonEvent));
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

bool nextButton(Button& out, int64_t& atMs, TickType_t wait) {
  if (s_buttons == nullptr) return false;
  ButtonEvent ev{};
  if (xQueueReceive(s_buttons, &ev, wait) != pdTRUE) return false;
  out = ev.button;
  atMs = ev.atMs;
  return true;
}

void led(Led state) {
  s_ledReq.store(static_cast<uint32_t>(toPattern(state)));
  s_ledSeq.fetch_add(1);
}

void brightness(uint8_t pct) { s_brightness.store(pct > 100 ? 100 : pct); }

bool bootPinHigh() {
#if CONFIG_KEYRA_BUTTON_GPIO == 0
  return gpio_get_level(kButtonGpio) == 1;
#else
  // GPIO0 is not Keyra's button on this board (and not configured as an input), so nothing Keyra
  // asks the user to hold keeps it low.
  return true;
#endif
}

}  // namespace keyra::io
