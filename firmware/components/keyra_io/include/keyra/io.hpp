#pragma once
// Physical I/O for Keyra (SPEC §2, §4.1): BOOT button (GPIO0) and the onboard
// WS2812 status LED. One background task owns both.
#include <cstdint>
#include "freertos/FreeRTOS.h"

namespace keyra::io {

enum class Button { Short, Long };
enum class Led { Off, Locked, Idle, Pending, Typing, Success, Error, AwaitPresence, Setup, Pairing, Fido };

void init();                                   // LED + button task
// Event queue. `atMs`: when the press was recognised, in esp_timer
// milliseconds (the clock of keyra::api::monoMs), so a press that waited in
// the queue can be told from one made after something was armed.
bool nextButton(Button& out, int64_t& atMs, TickType_t wait);
void led(Led state);
void brightness(uint8_t pct);                  // global LED brightness 0..100 (settings.ledBrightness)
bool bootPinHigh();                            // for safe restarts

}  // namespace keyra::io
