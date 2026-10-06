#include "dns_server.hpp"

#include <fcntl.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>

#include "addr.hpp"
#include "dns_packet.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"

namespace keyra::net {
namespace {

const char* TAG = "dns";
constexpr std::array<uint8_t, 4> kAnswer = {kApIp >> 24, (kApIp >> 16) & 0xFF, (kApIp >> 8) & 0xFF, kApIp & 0xFF};
// Bounded so a query flood cannot starve other tasks of the same priority.
constexpr int kMaxPerWake = 32;

void dnsTask(void* arg) {
  const int fd = static_cast<int>(reinterpret_cast<intptr_t>(arg));
  uint8_t in[dns::kMaxPacket], out[dns::kMaxPacket];
  for (;;) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    if (select(fd + 1, &readable, nullptr, nullptr, nullptr) < 0) {
      ESP_LOGE(TAG, "select failed: errno %d", errno);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    for (int i = 0; i < kMaxPerWake; ++i) {
      sockaddr_in from{};
      socklen_t fromLen = sizeof from;
      const ssize_t n = recvfrom(fd, in, sizeof in, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (n < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) ESP_LOGW(TAG, "recvfrom failed: errno %d", errno);
        break;
      }
      // Belt and braces next to the AP-only bind: never answer the home network.
      if (!inApSubnet(ntohl(from.sin_addr.s_addr))) continue;
      const size_t len = dns::reply(in, static_cast<size_t>(n), kAnswer, out, sizeof out);
      // Non-blocking send: when the stack is out of buffers the client simply retries.
      if (len > 0) sendto(fd, out, len, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from), fromLen);
    }
  }
}

}  // namespace

esp_err_t startDns() {
  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    ESP_LOGE(TAG, "socket failed: errno %d", errno);
    return ESP_FAIL;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  // Bound to the AP address, not INADDR_ANY: the catch-all resolver must only
  // exist on Keyra's own Wi-Fi, never on the home network (SPEC §8.2).
  addr.sin_addr.s_addr = htonl(kApIp);
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 ||
      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
    ESP_LOGE(TAG, "bind/fcntl failed: errno %d", errno);
    close(fd);
    return ESP_FAIL;
  }
  if (xTaskCreate(dnsTask, "dns", 4096, reinterpret_cast<void*>(static_cast<intptr_t>(fd)), 5, nullptr) != pdPASS) {
    close(fd);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

}  // namespace keyra::net
