// Wi-Fi glue: the "net" task applies Link's decisions (link.hpp) to the driver
// and is the only caller of esp_wifi_* after start(), so joins, AP on/off,
// credential changes and scans never interleave.
#include "keyra/net.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>

#include "addr.hpp"
#include "dns_server.hpp"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "link.hpp"
#include "lwip/sockets.h"
#include "mdns.h"
#include "scan_list.hpp"
#include "sdkconfig.h"

static_assert(CONFIG_LWIP_SNTP_MAX_SERVERS >= 2, "SPEC §8.2 names two SNTP servers");

namespace keyra::net {
namespace {

const char* TAG = "net";
constexpr TickType_t kTick = pdMS_TO_TICKS(250);
constexpr int64_t kLinkCheckMs = 1000;
// lwIP SNTP re-syncs hourly; after a few missed rounds the phone's clock is
// allowed to correct the device again.
constexpr int64_t kSyncFreshMs = 3LL * 60 * 60 * 1000;
constexpr uint16_t kMaxScanRecords = 40;
constexpr TickType_t kScanWait = pdMS_TO_TICKS(40000);  // > a join timeout + one scan

enum class Ev : uint8_t { Wake, StaConnected, GotIp, Disconnected };
struct Msg {
  Ev ev = Ev::Wake;
  uint32_t ip = 0;  // GotIp, host order
  uint8_t channel = 0;
  uint16_t reason = 0;
};

struct ScanJob {
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  esp_err_t result = ESP_FAIL;
  std::vector<Network> networks;
  ~ScanJob() {
    if (done) vSemaphoreDelete(done);
  }
};

int64_t monoMs() { return esp_timer_get_time() / 1000; }

// ---- handed between tasks, under g_mu ----
std::mutex g_mu;
std::optional<Config> g_wantAp;
std::optional<Home> g_wantHome;
std::shared_ptr<ScanJob> g_scan;
Status g_status;
std::atomic<int64_t> g_syncedAt{0};

// ---- start() once, then net task only ----
bool g_started = false;
QueueHandle_t g_q = nullptr;
Link g_link;
Config g_ap;
esp_netif_t* g_apNetif = nullptr;
esp_netif_t* g_staNetif = nullptr;
Home g_home;  // what the driver is configured with
bool g_apOn = false;
uint8_t g_homeChannel = 0;
uint32_t g_ip = 0;
int g_rssi = 0;
bool g_sntp = false;

esp_err_t apConfig(const Config& c, uint8_t channel, wifi_config_t& out) {
  const size_t ssidLen = c.ssid.size(), passLen = c.password.size();
  ESP_RETURN_ON_FALSE(ssidLen >= 1 && ssidLen <= sizeof out.ap.ssid, ESP_ERR_INVALID_ARG, TAG, "bad ssid length");
  // WPA2-PSK passphrase rule; an open AP is never acceptable for a password vault.
  ESP_RETURN_ON_FALSE(passLen >= 8 && passLen <= 63, ESP_ERR_INVALID_ARG, TAG, "bad password length");
  ESP_RETURN_ON_FALSE(channel >= 1 && channel <= 13, ESP_ERR_INVALID_ARG, TAG, "bad channel");
  out = {};
  std::memcpy(out.ap.ssid, c.ssid.data(), ssidLen);
  out.ap.ssid_len = static_cast<uint8_t>(ssidLen);
  std::memcpy(out.ap.password, c.password.data(), passLen);
  out.ap.channel = channel;
  out.ap.authmode = WIFI_AUTH_WPA2_PSK;
  out.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
  out.ap.max_connection = 4;
  out.ap.beacon_interval = 100;
  out.ap.pmf_cfg.required = false;
  return ESP_OK;
}

esp_err_t staConfig(const Home& h, wifi_config_t& out) {
  ESP_RETURN_ON_FALSE(!h.ssid.empty() && h.ssid.size() <= sizeof out.sta.ssid, ESP_ERR_INVALID_ARG, TAG, "bad home ssid");
  ESP_RETURN_ON_FALSE(h.password.size() >= 8 && h.password.size() <= 63, ESP_ERR_INVALID_ARG, TAG, "bad home password");
  out = {};
  std::memcpy(out.sta.ssid, h.ssid.data(), h.ssid.size());
  std::memcpy(out.sta.password, h.password.data(), h.password.size());
  // Never settle for an open/WEP/WPA1 network that happens to share the name.
  out.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  out.sta.pmf_cfg.capable = true;
  out.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  // Mesh and multi-AP homes: pick the strongest access point with that name.
  out.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  out.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  out.sta.failure_retry_cnt = 0;  // retries and backoff belong to Link
  return ESP_OK;
}

void post(const Msg& m) {
  if (xQueueSend(g_q, &m, 0) != pdTRUE) ESP_LOGE(TAG, "event queue full; dropped event %d", int(m.ev));
}

// Apple and Windows resolvers ask mDNS for both A and AAAA for "keyra.local"
// and wait ~5 s on the AAAA question when nobody answers it (the mdns component
// sends no negative/NSEC reply). A link-local IPv6 address on each interface
// lets mDNS answer both at once, so the name resolves instantly.
void addLinkLocal(esp_netif_t* netif, const char* which) {
  const esp_err_t err = esp_netif_create_ip6_linklocal(netif);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "%s IPv6 link-local: %s", which, esp_err_to_name(err));
}

void onEvent(void*, esp_event_base_t base, int32_t id, void* data) {
  Msg m;
  if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
    addLinkLocal(g_apNetif, "AP");
    return;
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
    addLinkLocal(g_staNetif, "home");
    m.ev = Ev::StaConnected;
    m.channel = static_cast<const wifi_event_sta_connected_t*>(data)->channel;
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    m.ev = Ev::Disconnected;
    m.reason = static_cast<const wifi_event_sta_disconnected_t*>(data)->reason;
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    m.ev = Ev::GotIp;
    m.ip = ntohl(static_cast<const ip_event_got_ip_t*>(data)->ip_info.ip.addr);
  } else {
    return;
  }
  post(m);
}

void onTimeSync(timeval*) {
  g_syncedAt = monoMs();
  ESP_LOGI(TAG, "clock set by SNTP");
}

void startSntpOnce() {
  if (g_sntp) return;
  esp_sntp_config_t cfg = {};
  cfg.start = true;
  cfg.sync_cb = onTimeSync;
  cfg.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
  cfg.num_of_servers = 2;
  cfg.servers[0] = "pool.ntp.org";
  cfg.servers[1] = "time.google.com";
  const esp_err_t err = esp_netif_sntp_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SNTP init: %s", esp_err_to_name(err));
    return;
  }
  g_sntp = true;  // lwIP keeps polling on its own from here, across reconnects
}

void handle(const Msg& m, int64_t now) {
  switch (m.ev) {
    case Ev::Wake: break;
    case Ev::StaConnected:
      g_homeChannel = m.channel;
      ESP_LOGI(TAG, "joined the home network on channel %u", m.channel);
      break;
    case Ev::GotIp:
      g_link.connected(now);
      if (!g_link.up()) break;  // home Wi-Fi was disabled meanwhile
      g_ip = m.ip;
      ESP_LOGI(TAG, "home network up: http://%s (keyra.local)", toString(m.ip).c_str());
      startSntpOnce();
      break;
    case Ev::Disconnected: {
      const bool wasUp = g_link.up(), wasTrying = g_link.attempting();
      g_link.disconnected(now);
      if (wasUp && !g_link.up()) {
        ESP_LOGW(TAG, "home network lost (reason %u); retrying", m.reason);
      } else if (wasTrying && !g_link.attempting()) {
        ESP_LOGW(TAG, "joining the home network failed (reason %u); next try in %lld s", m.reason,
                 (g_link.nextAttemptAt() - now + 999) / 1000);
      }
      if (!g_link.up()) g_ip = 0;
      break;
    }
  }
}

void setApOn(bool on) {
  if (on == g_apOn) return;
  const esp_err_t err = esp_wifi_set_mode(on ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set_mode(%s): %s", on ? "APSTA" : "STA", esp_err_to_name(err));
    return;  // retried on the next tick
  }
  g_apOn = on;
  ESP_LOGI(TAG, "Keyra's own Wi-Fi %s", on ? "on" : "off (home network connected)");
}

// Restarts the driver with new AP credentials: the path proven on hardware for
// getting them in place before the AP beacons again. A live home link drops
// for a moment and is rejoined at once with the backoff reset.
void applyAp(const Config& c, int64_t now) {
  wifi_config_t wc;
  if (apConfig(c, g_homeChannel ? g_homeChannel : c.channel, wc) != ESP_OK) return;
  const bool live = g_link.up() || g_link.attempting();
  esp_err_t err = esp_wifi_stop();
  if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_APSTA);  // the AP interface must be enabled to configure it
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &wc);
  if (err == ESP_OK) err = esp_wifi_set_mode(g_apOn ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  const esp_err_t started = esp_wifi_start();
  if (err != ESP_OK || started != ESP_OK) {
    ESP_LOGE(TAG, "AP reconfigure failed: %s / start %s", esp_err_to_name(err), esp_err_to_name(started));
    return;
  }
  g_ap = c;
  if (live) {
    g_link.configure(g_home.enabled, g_home.apMode, true, now);
    g_ip = 0;
  }
  ESP_LOGI(TAG, "AP reconfigured as \"%s\"", c.ssid.c_str());
}

// Returns false when the driver refused the new station config (retry later).
bool applyHome(const Home& h, int64_t now) {
  const bool rejoin = h.enabled && (!g_home.enabled || h.ssid != g_home.ssid || h.password != g_home.password);
  const bool leave = rejoin || (!h.enabled && g_home.enabled);
  if (leave && (g_link.up() || g_link.attempting())) {
    const esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) ESP_LOGW(TAG, "disconnect: %s", esp_err_to_name(err));
  }
  if (rejoin) {
    wifi_config_t wc;
    esp_err_t err = staConfig(h, wc);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "home network config: %s", esp_err_to_name(err));
      g_link.configure(false, h.apMode, false, now);  // stop joining with the old config meanwhile
      return false;
    }
    g_homeChannel = 0;
  }
  g_link.configure(h.enabled, h.apMode, rejoin, now);
  if (!g_link.up()) g_ip = 0;
  g_home = h;
  ESP_LOGI(TAG, "home Wi-Fi %s%s%s, Keyra's own Wi-Fi: %s", h.enabled ? "on: \"" : "off", h.enabled ? h.ssid.c_str() : "",
           h.enabled ? "\"" : "", h.apMode == ApMode::Always ? "always" : "fallback");
  return true;
}

void applyWanted(int64_t now) {
  std::optional<Config> ap;
  std::optional<Home> home;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    ap.swap(g_wantAp);
    home.swap(g_wantHome);
  }
  if (home && !applyHome(*home, now)) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_wantHome) g_wantHome = std::move(home);
  }
  if (ap) applyAp(*ap, now);
}

scanlist::Security securityOf(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_OWE: return scanlist::Security::Open;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK: return scanlist::Security::Psk;
    default: return scanlist::Security::Other;
  }
}

esp_err_t doScan(std::vector<Network>& out) {
  wifi_scan_config_t sc = {};
  sc.show_hidden = false;
  sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  sc.scan_time.active.min = WIFI_ACTIVE_SCAN_MIN_DEFAULT_TIME;
  sc.scan_time.active.max = WIFI_ACTIVE_SCAN_MAX_DEFAULT_TIME;
  // Return to the AP's channel between scanned channels so its phones stay joined.
  sc.home_chan_dwell_time = WIFI_SCAN_HOME_CHANNEL_DWELL_DEFAULT_TIME;
  ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&sc, true), TAG, "scan");
  uint16_t n = 0;
  esp_wifi_scan_get_ap_num(&n);
  n = std::min(n, kMaxScanRecords);
  std::vector<wifi_ap_record_t> recs(n);
  esp_err_t err = n > 0 ? esp_wifi_scan_get_ap_records(&n, recs.data()) : ESP_OK;
  esp_wifi_clear_ap_list();  // frees whatever the capped read left behind
  ESP_RETURN_ON_ERROR(err, TAG, "scan records");
  std::vector<scanlist::Record> raw;
  raw.reserve(n);
  for (uint16_t i = 0; i < n; ++i) {
    const auto* ssid = reinterpret_cast<const char*>(recs[i].ssid);
    raw.push_back({std::string(ssid, strnlen(ssid, sizeof recs[i].ssid)), recs[i].rssi, securityOf(recs[i].authmode),
                   recs[i].primary});
  }
  out.clear();
  for (const scanlist::Item& it : scanlist::tidy(raw)) out.push_back({it.ssid, it.rssi, it.secure, it.channel});
  return ESP_OK;
}

bool scanWaiting() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_scan != nullptr;
}

// A scan cannot run while the station is mid-join (ESP_ERR_WIFI_STATE), so it
// waits for the attempt to end and holds the next one back (Link::tick hold).
void runScanIfDue() {
  if (g_link.attempting()) return;
  std::shared_ptr<ScanJob> job;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    job.swap(g_scan);
  }
  if (!job) return;
  job->result = doScan(job->networks);
  xSemaphoreGive(job->done);
}

// Detects a link that vanished without a DISCONNECTED event and samples RSSI.
void checkLink(int64_t now) {
  if (!g_link.up()) return;
  wifi_ap_record_t ap;
  const esp_err_t err = esp_wifi_sta_get_ap_info(&ap);
  if (err == ESP_OK) {
    g_rssi = ap.rssi;
  } else if (err == ESP_ERR_WIFI_NOT_CONNECT) {
    ESP_LOGW(TAG, "home network gone without an event");
    g_link.disconnected(now);
    g_ip = 0;
  }
}

void publish() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_status.apOn = g_apOn;
  g_status.homeEnabled = g_home.enabled;
  g_status.homeSsid = g_home.enabled ? g_home.ssid : std::string();
  g_status.homeConnected = g_link.up();
  g_status.homeIp = g_link.up() && g_ip ? toString(g_ip) : std::string();
  g_status.rssi = g_link.up() ? g_rssi : 0;
}

void netTask(void*) {
  int64_t lastCheck = 0;
  for (;;) {
    Msg m;
    if (xQueueReceive(g_q, &m, kTick) == pdTRUE) {
      do {
        handle(m, monoMs());
      } while (xQueueReceive(g_q, &m, 0) == pdTRUE);
    }
    const int64_t now = monoMs();
    applyWanted(now);
    runScanIfDue();
    const Link::Plan plan = g_link.tick(now, scanWaiting());
    if (plan.abort) {
      ESP_LOGW(TAG, "joining the home network timed out; next try in %lld s", (g_link.nextAttemptAt() - now + 999) / 1000);
      esp_wifi_disconnect();
    }
    if (plan.connect) {
      const esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect: %s", esp_err_to_name(err));
        g_link.disconnected(now);
      }
    }
    setApOn(g_link.apOn());
    if (now - lastCheck >= kLinkCheckMs) {
      lastCheck = now;
      checkLink(now);
    }
    publish();
  }
}

void wake() { post(Msg{}); }

esp_err_t startMdns() {
  // The mdns component follows the default STA and AP netifs by itself
  // (CONFIG_MDNS_PREDEF_NETIF_STA/AP), so keyra.local answers on both.
  ESP_RETURN_ON_ERROR(mdns_init(), TAG, "mdns_init");
  ESP_RETURN_ON_ERROR(mdns_hostname_set("keyra"), TAG, "mdns hostname");
  ESP_RETURN_ON_ERROR(mdns_instance_name_set("Keyra"), TAG, "mdns instance");
  ESP_RETURN_ON_ERROR(mdns_service_add("Keyra", "_http", "_tcp", 80, nullptr, 0), TAG, "mdns service");
  return ESP_OK;
}

}  // namespace

esp_err_t start(const Config& c, const Home& home) {
  ESP_RETURN_ON_FALSE(!g_started, ESP_ERR_INVALID_STATE, TAG, "already started");
  wifi_config_t apWc, staWc;
  ESP_RETURN_ON_ERROR(apConfig(c, c.channel, apWc), TAG, "ap config");
  if (home.enabled) ESP_RETURN_ON_ERROR(staConfig(home, staWc), TAG, "home config");

  g_q = xQueueCreate(16, sizeof(Msg));
  ESP_RETURN_ON_FALSE(g_q, ESP_ERR_NO_MEM, TAG, "queue");
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
  const esp_err_t loop = esp_event_loop_create_default();
  ESP_RETURN_ON_FALSE(loop == ESP_OK || loop == ESP_ERR_INVALID_STATE, loop, TAG, "event loop");
  g_apNetif = esp_netif_create_default_wifi_ap();
  ESP_RETURN_ON_FALSE(g_apNetif != nullptr, ESP_FAIL, TAG, "ap netif");
  esp_netif_t* sta = esp_netif_create_default_wifi_sta();
  g_staNetif = sta;
  ESP_RETURN_ON_FALSE(sta != nullptr, ESP_FAIL, TAG, "sta netif");
  // How Keyra shows up in the router's client list.
  if (esp_netif_set_hostname(sta, "keyra") != ESP_OK) ESP_LOGW(TAG, "DHCP hostname not set");

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  // Never load a Wi-Fi config persisted by earlier firmware: the AP must come up
  // exactly once, already secured, with the config below; the home network
  // credentials live only in Keyra's settings.
  init.nvs_enable = 0;
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr), TAG, "wifi events");
  ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr), TAG, "ip events");

  g_link.boot(home.enabled, home.apMode, monoMs());
  g_ap = c;
  g_home = home;
  g_apOn = g_link.apOn();
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &apWc), TAG, "ap config");
  if (home.enabled) ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &staWc), TAG, "sta config");
  if (!g_apOn) ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
  // mDNS before Wi-Fi starts so it sees AP_START and binds to the AP netif.
  ESP_RETURN_ON_ERROR(startMdns(), TAG, "mdns");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
  const esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
  if (ps != ESP_OK) ESP_LOGW(TAG, "set_ps(NONE): %s", esp_err_to_name(ps));
  ESP_RETURN_ON_ERROR(startDns(), TAG, "dns");
  // Joins (esp_wifi_connect), scans and SNTP setup run here.
  ESP_RETURN_ON_FALSE(xTaskCreate(netTask, "net", 4096, nullptr, 5, nullptr) == pdPASS, ESP_ERR_NO_MEM, TAG, "task");

  g_started = true;
  publish();
  ESP_LOGI(TAG, "AP \"%s\" %s on channel %u; home Wi-Fi %s", c.ssid.c_str(), g_apOn ? "up" : "held back", c.channel,
           home.enabled ? "on" : "off");
  return ESP_OK;
}

esp_err_t reconfigure(const Config& c) {
  ESP_RETURN_ON_FALSE(g_started, ESP_ERR_INVALID_STATE, TAG, "not started");
  wifi_config_t wc;
  ESP_RETURN_ON_ERROR(apConfig(c, c.channel, wc), TAG, "config");
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_wantAp = c;
  }
  wake();
  return ESP_OK;
}

esp_err_t setHome(const Home& h) {
  ESP_RETURN_ON_FALSE(g_started, ESP_ERR_INVALID_STATE, TAG, "not started");
  if (h.enabled) {
    wifi_config_t wc;
    ESP_RETURN_ON_ERROR(staConfig(h, wc), TAG, "home config");
  }
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_wantHome = h;
  }
  wake();
  return ESP_OK;
}

int stations() {
  wifi_sta_list_t list{};
  return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

Status status() {
  Status s;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    s = g_status;
  }
  s.apClients = s.apOn ? stations() : 0;
  return s;
}

std::string homeIp() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_status.homeIp;
}

esp_err_t scan(std::vector<Network>& out) {
  ESP_RETURN_ON_FALSE(g_started, ESP_ERR_INVALID_STATE, TAG, "not started");
  auto job = std::make_shared<ScanJob>();
  ESP_RETURN_ON_FALSE(job->done, ESP_ERR_NO_MEM, TAG, "semaphore");
  {
    std::lock_guard<std::mutex> lock(g_mu);
    // A second caller joins nothing: it gets busy rather than a stale list.
    ESP_RETURN_ON_FALSE(!g_scan, ESP_ERR_INVALID_STATE, TAG, "scan already queued");
    g_scan = job;
  }
  wake();
  if (xSemaphoreTake(job->done, kScanWait) != pdTRUE) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_scan == job) g_scan.reset();
    ESP_LOGW(TAG, "scan timed out");
    return ESP_ERR_TIMEOUT;  // the job is shared, so a late finish writes into its own copy
  }
  if (job->result == ESP_OK) out = std::move(job->networks);
  return job->result;
}

Via viaForSocket(int fd) {
  sockaddr_storage ss{};
  socklen_t len = sizeof ss;
  if (fd < 0 || getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return Via::Home;
  if (ss.ss_family == AF_INET) return classify(ntohl(reinterpret_cast<const sockaddr_in*>(&ss)->sin_addr.s_addr));
  if (ss.ss_family == AF_INET6) {
    const uint8_t* a = reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_addr.s6_addr;
    if (const uint32_t v4 = fromV4Mapped(a)) return classify(v4);
    // Native IPv6 (link-local): it came in on the AP only if it is the AP's own address.
    esp_ip6_addr_t ap{};
    if (g_apNetif && esp_netif_get_ip6_linklocal(g_apNetif, &ap) == ESP_OK && std::memcmp(ap.addr, a, 16) == 0)
      return Via::Ap;
  }
  return Via::Home;
}

bool timeSynced() {
  const int64_t at = g_syncedAt.load();
  return at != 0 && monoMs() - at < kSyncFreshMs;
}

}  // namespace keyra::net
