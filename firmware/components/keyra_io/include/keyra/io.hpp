#pragma once
// Physical I/O for Keyra (SPEC §2, §4.1): the confirmation button (BOOT, GPIO0,
// unless the board profile says otherwise) and the status LED (WS2812, a plain
// LED or none: Kconfig "Keyra I/O"). One background task owns both.
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
bool bootPinHigh();                            // GPIO0 released (always true when it is not the button): for safe restarts

}  // namespace keyra::io
