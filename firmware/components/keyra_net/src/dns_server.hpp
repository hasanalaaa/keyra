#pragma once
#include "esp_err.h"

namespace keyra::net {
// Binds UDP :53 synchronously (so bind failures surface to start()) and spawns the responder task.
esp_err_t startDns();
}
