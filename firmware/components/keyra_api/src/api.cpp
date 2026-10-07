#include "keyra/api.hpp"

#include "esp_check.h"
#include "handlers.hpp"
#include "handlers_update.hpp"
#include "trusted.hpp"
#include "json.hpp"
#include "runtime.hpp"
#include "server.hpp"

namespace keyra::api {

esp_err_t start() {
  json::installWipingAllocator();
  // Construct the shared singletons before any task can race to do it.
  machine();
  sessions();
  ESP_RETURN_ON_ERROR(trust::load(), "api", "trusted browsers");
  ESP_RETURN_ON_ERROR(startTasks(), "api", "tasks");
  ESP_RETURN_ON_ERROR(startSlowWorker(), "api", "slow worker");
  ESP_RETURN_ON_ERROR(startServer(), "api", "server");
  return ESP_OK;
}

void confirmBoot(bool healthy) { update::confirmBoot(healthy); }

}  // namespace keyra::api
