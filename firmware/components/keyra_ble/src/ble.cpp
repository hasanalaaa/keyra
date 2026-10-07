// keyra::ble on NimBLE (SPEC §8.1). The NimBLE host task owns the stack: GAP
// events, the pairing-window timer and every advertising or bond change run
// there, so two tasks never drive NimBLE at once. Other tasks update the
// shared state under g_mu and post a "re-evaluate" event (reconcile()).
#include "keyra/ble.hpp"

#include <sys/time.h>

#include <mutex>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gatt.hpp"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "peer_store.hpp"
#include "policy.hpp"
#include "report_map.hpp"
#include "sdkconfig.h"
#include "services/gap/ble_svc_gap.h"

extern "C" void ble_store_config_init(void);  // NimBLE ships no header for it

namespace keyra::ble {
namespace {

constexpr const char* TAG = "keyra_ble";
static_assert(CONFIG_BT_NIMBLE_MAX_BONDS == kMaxBonds, "sdkconfig must store exactly kMaxBonds bonds");
static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS == 1, "Keyra serves one host at a time");

constexpr uint16_t kAppearanceKeyboard = 0x03C1;
constexpr int64_t kSendTimeoutUs = 100 * 1000;  // same budget as the USB transport
constexpr TickType_t kForgetWait = pdMS_TO_TICKS(3000);
// Advertising intervals (0.625 ms units): brisk while pairing so the host's
// device list fills quickly, relaxed for reconnects to leave airtime to Wi-Fi.
constexpr uint16_t kOpenItvlMin = 48, kOpenItvlMax = 80;        // 30–50 ms
constexpr uint16_t kBondedItvlMin = 160, kBondedItvlMax = 240;  // 100–150 ms
// Room for the name: 31 bytes minus flags (3), appearance (4), one 16-bit
// UUID (4) and the name's own header (2). The scan response carries 31 − 2.
constexpr size_t kAdvNameMax = 18;
constexpr size_t kRspNameMax = 29;
// Same "clock was set" threshold as keyra_api's clock::kValidAfterMs (2024-01-01).
constexpr time_t kClockSetAfter = 1704067200;

const ble_uuid16_t kUuidHidService = BLE_UUID16_INIT(0x1812);
const ble_uuid16_t kUuidDeviceName = BLE_UUID16_INIT(0x2A00);

struct Link {
  uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
  peers::Key peer;
  bool encrypted = false;
  bool bonded = false;
  bool subInput = false;  // notifications on the Report characteristic
  bool subBoot = false;   // ... on Boot Keyboard Input
  bool trusted() const { return conn != BLE_HS_CONN_HANDLE_NONE && encrypted && bonded; }
};

struct ForgetReq {
  bool all = false;
  Addr addr{};
};

// ---- shared state (any task), guarded by g_mu; never held across NimBLE calls
std::mutex g_mu;
bool g_started = false;  // nimble_port_init() succeeded and the events exist
bool g_synced = false;   // host and controller in sync: advertising possible
bool g_enabled = false;
Connect g_mode = Connect::OnDemand;
Demand g_demand;
std::string g_name;
Window g_window;
Link g_link;
std::vector<peers::Key> g_bondKeys;  // NimBLE's bond store, refreshed on the host task
std::vector<Peer> g_bonds;           // same order, with names for status()
ForgetReq g_forgetReq;
esp_err_t g_forgetResult = ESP_OK;
SemaphoreHandle_t g_forgetMu = nullptr;    // one forget at a time
SemaphoreHandle_t g_forgetDone = nullptr;  // host task → caller

// ---- host-task-only state
uint8_t t_ownAddrType = BLE_OWN_ADDR_PUBLIC;
Adv t_adv = Adv::Off;
std::string t_advName;
std::vector<peers::Key> t_advKeys;
std::string t_gapName;
uint16_t t_repairing = BLE_HS_CONN_HANDLE_NONE;  // link whose old bond was dropped to re-pair
ble_npl_event t_kickEv;
ble_npl_event t_forgetEv;
ble_npl_callout t_windowEnd;
ble_npl_callout t_lingerEnd;

int64_t monoMs() { return esp_timer_get_time() / 1000; }

int64_t unixNow() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return tv.tv_sec >= kClockSetAfter ? int64_t{tv.tv_sec} : 0;
}

// NimBLE keeps addresses least significant byte first.
peers::Key keyOf(const ble_addr_t& a) {
  peers::Key k;
  k.type = a.type;
  for (size_t i = 0; i < 6; ++i) k.addr[i] = a.val[5 - i];
  return k;
}

ble_addr_t addrOf(const peers::Key& k) {
  ble_addr_t a{};
  a.type = k.type;
  for (size_t i = 0; i < 6; ++i) a.val[5 - i] = k.addr[i];
  return a;
}

bool knownLocked(const peers::Key& k) {
  for (const peers::Key& b : g_bondKeys) {
    if (b == k) return true;
  }
  return false;
}

void kick() {
  bool started;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    started = g_started;
  }
  if (started) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &t_kickEv);
}

// ---------- host task ----------

void refreshBonds() {
  ble_addr_t ids[kMaxBonds];
  int n = 0;
  const int rc = ble_store_util_bonded_peers(ids, &n, kMaxBonds);
  if (rc != 0) {
    ESP_LOGE(TAG, "reading bonds failed: %d", rc);
    n = 0;
  }
  std::vector<peers::Key> keys;
  std::vector<Peer> list;
  for (int i = 0; i < n; ++i) {
    keys.push_back(keyOf(ids[i]));
    list.push_back(peers::get(keys.back()));
  }
  std::lock_guard<std::mutex> lock(g_mu);
  g_bondKeys = std::move(keys);
  g_bonds = std::move(list);
}

int onGap(ble_gap_event* ev, void* arg);

int startAdvertising(Adv mode, const std::string& name, const std::vector<peers::Key>& keys) {
  const std::string_view advName = fitName(name, kAdvNameMax);
  ble_hs_adv_fields f{};
  f.flags = BLE_HS_ADV_F_BREDR_UNSUP | (mode == Adv::Open ? BLE_HS_ADV_F_DISC_GEN : 0);
  f.appearance = kAppearanceKeyboard;
  f.appearance_is_present = 1;
  f.uuids16 = &kUuidHidService;
  f.num_uuids16 = 1;
  f.uuids16_is_complete = 1;
  f.name = reinterpret_cast<const uint8_t*>(advName.data());
  f.name_len = static_cast<uint8_t>(advName.size());
  f.name_is_complete = advName.size() == name.size();
  int rc = ble_gap_adv_set_fields(&f);

  const std::string_view rspName = fitName(name, kRspNameMax);
  ble_hs_adv_fields r{};
  r.name = reinterpret_cast<const uint8_t*>(rspName.data());
  r.name_len = static_cast<uint8_t>(rspName.size());
  r.name_is_complete = rspName.size() == name.size();
  if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&r);

  ble_gap_adv_params p{};
  p.conn_mode = BLE_GAP_CONN_MODE_UND;
  if (mode == Adv::Open) {
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = kOpenItvlMin;
    p.itvl_max = kOpenItvlMax;
    p.filter_policy = BLE_HCI_ADV_FILT_NONE;
  } else {
    // Only hosts on the accept list may scan or connect. Their resolving keys
    // are in the controller (NimBLE restores them at sync), so phones with
    // rotating private addresses still match their identity address.
    std::vector<ble_addr_t> wl;
    for (const peers::Key& k : keys) wl.push_back(addrOf(k));
    if (rc == 0) rc = ble_gap_wl_set(wl.data(), static_cast<uint8_t>(wl.size()));
    p.disc_mode = BLE_GAP_DISC_MODE_NON;
    p.itvl_min = kBondedItvlMin;
    p.itvl_max = kBondedItvlMax;
    p.filter_policy = BLE_HCI_ADV_FILT_BOTH;
  }
  if (rc == 0) rc = ble_gap_adv_start(t_ownAddrType, nullptr, BLE_HS_FOREVER, &p, onGap, nullptr);
  if (rc != 0) ESP_LOGE(TAG, "advertising (%s) failed: %d", mode == Adv::Open ? "pairing" : "bonded", rc);
  return rc;
}

// Brings the radio in line with the shared state. Idempotent; runs after
// every event that could change what Keyra should be doing.
void reconcile() {
  bool enabled, pairing, trusted;
  uint16_t conn;
  int64_t leftMs, lingerMs;
  std::string name;
  std::vector<peers::Key> keys;
  std::optional<Addr> wanted;
  Connect mode;
  Addr peer;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_synced) return;
    const int64_t now = monoMs();
    enabled = g_enabled;
    if (!enabled) {
      g_window.close();
      g_demand.drop();
    }
    pairing = g_window.active(now);
    leftMs = g_window.leftMs(now);
    lingerMs = g_demand.lingerLeftMs(now);
    conn = g_link.conn;
    trusted = g_link.trusted();
    peer = g_link.peer.addr;
    name = g_name;
    keys = g_bondKeys;
    mode = g_mode;
    wanted = g_demand.target(now);
  }
  // A wanted host that is no longer bonded (forgotten meanwhile) is nobody.
  std::vector<peers::Key> accept;
  for (const peers::Key& k : keys) {
    if (!wanted || k.addr == *wanted) accept.push_back(k);
  }
  if (wanted && accept.empty()) wanted.reset();

  if (pairing) {
    ble_npl_callout_reset(&t_windowEnd, ble_npl_time_ms_to_ticks32(static_cast<uint32_t>(leftMs)));
  } else {
    ble_npl_callout_stop(&t_windowEnd);
  }
  if (lingerMs > 0) {
    ble_npl_callout_reset(&t_lingerEnd, ble_npl_time_ms_to_ticks32(static_cast<uint32_t>(lingerMs)));
  } else {
    ble_npl_callout_stop(&t_lingerEnd);
  }
  if (name != t_gapName) {
    const int rc = ble_svc_gap_device_name_set(name.c_str());
    if (rc == 0) t_gapName = name;
    else ESP_LOGE(TAG, "device name rejected: %d", rc);
  }

  const bool connected = conn != BLE_HS_CONN_HANDLE_NONE;
  // A link can outlive the reason it was let in: Bluetooth was switched off,
  // the window closed on a host that never finished pairing, the action that
  // wanted it is over (on demand), or another host is wanted now.
  if (connected && (!enabled || !keepLink(mode, pairing, trusted, wanted, peer))) {
    ESP_LOGI(TAG, "letting the link go");
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
  }

  const Adv want = advertising(enabled, pairing, keys.size(), connected, mode, wanted.has_value());
  const bool same = want == t_adv && name == t_advName && accept == t_advKeys;
  if (same && (want == Adv::Off || ble_gap_adv_active())) return;
  if (ble_gap_adv_active()) ble_gap_adv_stop();
  t_adv = Adv::Off;
  if (want == Adv::Off) return;
  if (startAdvertising(want, name, accept) == 0) {
    t_adv = want;
    t_advName = name;
    t_advKeys = accept;
  }
}

void onKick(ble_npl_event*) { reconcile(); }

void onLingerEnd(ble_npl_event*) { reconcile(); }

void onWindowEnd(ble_npl_event*) {
  ESP_LOGI(TAG, "pairing window closed");
  reconcile();
}

void onForget(ble_npl_event*) {
  ForgetReq req;
  std::vector<peers::Key> keys;
  uint16_t conn;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    req = g_forgetReq;
    keys = g_bondKeys;
    conn = g_link.conn;
  }
  // The controller's resolving list cannot change while advertising.
  if (ble_gap_adv_active()) ble_gap_adv_stop();
  t_adv = Adv::Off;

  esp_err_t result = ESP_OK;
  if (req.all) {
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    if (ble_store_clear() != 0) result = ESP_FAIL;
    peers::clear();
  } else {
    result = ESP_ERR_NOT_FOUND;
    for (const peers::Key& k : keys) {
      if (k.addr != req.addr) continue;
      const ble_addr_t a = addrOf(k);
      const int rc = ble_gap_unpair(&a);  // also ends that host's link
      if (rc != 0) ESP_LOGE(TAG, "unpair %s failed: %d", formatAddr(k.addr).c_str(), rc);
      result = rc == 0 ? ESP_OK : ESP_FAIL;
      peers::forget(k);
    }
  }
  refreshBonds();
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_forgetResult = result;
  }
  xSemaphoreGive(g_forgetDone);
  reconcile();
}

void onConnect(int status, uint16_t conn) {
  t_adv = Adv::Off;  // a connectable advertisement ends with the connection attempt
  ble_gap_conn_desc d{};
  if (status != 0 || ble_gap_conn_find(conn, &d) != 0) {
    reconcile();
    return;
  }
  // The controller already resolved private addresses, so peer_id_addr is
  // the identity a bond is stored under.
  const peers::Key k = keyOf(d.peer_id_addr);
  ESP_LOGI(TAG, "connection from %s (encrypted=%d bonded=%d)", formatAddr(k.addr).c_str(),
           int(d.sec_state.encrypted), int(d.sec_state.bonded));
  bool allow;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    allow = mayConnect(g_enabled, g_window.active(monoMs()), knownLocked(k));
    if (allow) {
      g_link = Link{};
      g_link.conn = conn;
      g_link.peer = k;
    }
  }
  if (!allow) {
    ESP_LOGW(TAG, "refusing %s: not bonded and not pairing", formatAddr(k.addr).c_str());
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    return;
  }
  gatt::resetLink();
  // Bonded hosts re-encrypt with their stored keys; a new host starts pairing.
  const int rc = ble_gap_security_initiate(conn);
  if (rc != 0) ESP_LOGW(TAG, "security request failed: %d", rc);
}

void onDisconnect(uint16_t conn, int reason) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_link.conn == conn) g_link = Link{};
  }
  if (t_repairing == conn) t_repairing = BLE_HS_CONN_HANDLE_NONE;
  gatt::resetLink();
  ESP_LOGI(TAG, "disconnected (reason 0x%x)", reason);
  reconcile();
}

int onPeerName(uint16_t conn, const ble_gatt_error* err, ble_gatt_attr* attr, void*) {
  if (err == nullptr || err->status != 0 || attr == nullptr) return 0;  // BLE_HS_EDONE ends the read
  char buf[64];
  uint16_t len = 0;
  ble_hs_mbuf_to_flat(attr->om, buf, sizeof buf, &len);  // a longer name is cut; cleanName() trims it
  peers::Key k;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_link.conn != conn) return 0;
    k = g_link.peer;
  }
  peers::named(k, std::string(buf, len));
  refreshBonds();
  return 0;
}

void onEncrypted(uint16_t conn, int status) {
  ble_gap_conn_desc d{};
  if (status != 0 || ble_gap_conn_find(conn, &d) != 0) {
    // Wrong or missing keys (the host forgot Keyra) or a refused pairing:
    // free the only connection slot.
    ESP_LOGW(TAG, "encryption failed: %d", status);
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    return;
  }
  const peers::Key k = keyOf(d.peer_id_addr);  // identity, now that keys were exchanged
  const bool repaired = t_repairing == conn;
  t_repairing = BLE_HS_CONN_HANDLE_NONE;
  bool known, window;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    known = knownLocked(k);
    window = g_window.active(monoMs());
  }
  if (d.sec_state.bonded && !known && !window) {
    // Cannot happen through the accept list and the pairing checks; if it
    // ever does, the new bond is not honoured.
    ESP_LOGE(TAG, "new bond outside the pairing window: removing it");
    ble_gap_unpair(&d.peer_id_addr);
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_link.peer = k;
    g_link.encrypted = d.sec_state.encrypted;
    g_link.bonded = d.sec_state.bonded;
    // One approval, one pairing: close the window once a host has paired.
    // Keep the new link a little (its name is read next; a first action may
    // follow) before on-demand mode lets it go.
    if (d.sec_state.bonded && (!known || repaired)) {
      g_window.close();
      if (!g_demand.target(monoMs())) {
        g_demand.want(k.addr);
        g_demand.done(monoMs());
      }
    }
  }
  if (!d.sec_state.bonded) return;
  ESP_LOGI(TAG, "%s %s", known && !repaired ? "reconnected" : "paired", formatAddr(k.addr).c_str());
  peers::seen(k, unixNow());
  refreshBonds();
  // The host's own name ("Hasan's iPad") for the device list in the app.
  const int rc = ble_gattc_read_by_uuid(conn, 1, 0xFFFF, &kUuidDeviceName.u, onPeerName, nullptr);
  if (rc != 0) ESP_LOGW(TAG, "reading the host's name failed: %d", rc);
  reconcile();
}

int onRepeatPairing(uint16_t conn) {
  ble_gap_conn_desc d{};
  if (ble_gap_conn_find(conn, &d) != 0) return BLE_GAP_REPEAT_PAIRING_IGNORE;
  bool window;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    window = g_window.active(monoMs());
  }
  // A bonded host asking for new keys is a new pairing: only inside the window.
  if (!mayPair(window, true, 0)) {
    ESP_LOGW(TAG, "re-pairing refused outside the pairing window");
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
  }
  ble_store_util_delete_peer(&d.peer_id_addr);
  t_repairing = conn;
  return BLE_GAP_REPEAT_PAIRING_RETRY;
}

void onSubscribe(const ble_gap_event& ev) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (ev.subscribe.conn_handle != g_link.conn) return;
  if (ev.subscribe.attr_handle == gatt::inputHandle()) g_link.subInput = ev.subscribe.cur_notify;
  if (ev.subscribe.attr_handle == gatt::bootInputHandle()) g_link.subBoot = ev.subscribe.cur_notify;
  ESP_LOGI(TAG, "host %s notifications on handle %u (reason %d): input=%d boot=%d",
           ev.subscribe.cur_notify ? "enabled" : "disabled", unsigned(ev.subscribe.attr_handle),
           int(ev.subscribe.reason), int(g_link.subInput), int(g_link.subBoot));
}

int onGap(ble_gap_event* ev, void*) {
  switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT: onConnect(ev->connect.status, ev->connect.conn_handle); return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      onDisconnect(ev->disconnect.conn.conn_handle, ev->disconnect.reason);
      return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: onEncrypted(ev->enc_change.conn_handle, ev->enc_change.status); return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: return onRepeatPairing(ev->repeat_pairing.conn_handle);
    case BLE_GAP_EVENT_SUBSCRIBE: onSubscribe(*ev); return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      t_adv = Adv::Off;
      reconcile();
      return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
      // NoInputNoOutput means Just Works: no passkey or comparison to answer.
      ESP_LOGW(TAG, "unexpected passkey action %d", ev->passkey.params.action);
      return 0;
    default: return 0;
  }
}

// Store capacity (NimBLE asks before a pairing and on a failed write). Never
// evict a bond to make room: the user chooses which host to forget.
int onStoreStatus(ble_store_status_event* ev, void*) {
  if (ev->event_code == BLE_STORE_EVENT_FULL) {
    ble_gap_conn_desc d{};
    if (ble_gap_conn_find(ev->full.conn_handle, &d) == 0) {
      std::lock_guard<std::mutex> lock(g_mu);
      if (knownLocked(keyOf(d.peer_id_addr))) return 0;  // re-pairing replaces its own bond
    }
    ESP_LOGW(TAG, "pairing refused: %u hosts already paired", static_cast<unsigned>(kMaxBonds));
  }
  return BLE_HS_ESTORE_CAP;
}

void onSync() {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc == 0) rc = ble_hs_id_infer_auto(0, &t_ownAddrType);
  if (rc != 0) {
    ESP_LOGE(TAG, "no usable Bluetooth address: %d", rc);
    return;
  }
  refreshBonds();
  size_t bonds;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_synced = true;
    bonds = g_bondKeys.size();
  }
  ESP_LOGI(TAG, "ready, %u bonded host(s)", static_cast<unsigned>(bonds));
  reconcile();
}

void onReset(int reason) {
  ESP_LOGE(TAG, "NimBLE host reset (reason %d)", reason);
  std::lock_guard<std::mutex> lock(g_mu);
  g_synced = false;
  g_link = Link{};
  t_adv = Adv::Off;
}

void hostTask(void*) {
  nimble_port_run();  // returns only after nimble_port_stop(), which Keyra never calls
  nimble_port_freertos_deinit();
}

esp_err_t eraseNamespace(const char* ns) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  err = nvs_erase_all(h);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err;
}

esp_err_t runForget(const ForgetReq& req) {
  bool started;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    started = g_started;
  }
  if (!started) {
    if (!req.all) return ESP_ERR_INVALID_STATE;
    // Bluetooth never came up; still wipe what an earlier boot stored.
    esp_err_t err = eraseNamespace("nimble_bond");
    const esp_err_t err2 = eraseNamespace("keyra_ble");
    return err != ESP_OK ? err : err2;
  }
  xSemaphoreTake(g_forgetMu, portMAX_DELAY);
  xSemaphoreTake(g_forgetDone, 0);  // drop a late "done" from a call that timed out
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_forgetReq = req;
  }
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &t_forgetEv);
  esp_err_t result = ESP_ERR_TIMEOUT;
  if (xSemaphoreTake(g_forgetDone, kForgetWait) == pdTRUE) {
    std::lock_guard<std::mutex> lock(g_mu);
    result = g_forgetResult;
  }
  xSemaphoreGive(g_forgetMu);
  return result;
}

}  // namespace

esp_err_t init(const std::string& deviceName, bool enabled, Connect mode) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_name = deviceName;
    g_enabled = enabled;
    g_mode = mode;
  }
  g_forgetMu = xSemaphoreCreateMutex();
  g_forgetDone = xSemaphoreCreateBinary();
  if (g_forgetMu == nullptr || g_forgetDone == nullptr) return ESP_ERR_NO_MEM;

  esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NimBLE init failed: %s — Bluetooth unavailable", esp_err_to_name(err));
    return err;
  }
  peers::load();

  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;
  ble_hs_cfg.store_status_cb = onStoreStatus;
  // LE Secure Connections only, bonding, Just Works (Keyra has no display or
  // keypad). Both sides hand out their identity key so private addresses resolve.
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_sc_only = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  int rc = gatt::registerServices();
  if (rc == 0) rc = ble_svc_gap_device_appearance_set(kAppearanceKeyboard);
  if (rc == 0) rc = ble_svc_gap_device_name_set(deviceName.c_str());
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT setup failed: %d — Bluetooth unavailable", rc);
    nimble_port_deinit();
    return ESP_FAIL;
  }
  t_gapName = deviceName;
  ble_store_config_init();

  ble_npl_event_init(&t_kickEv, onKick, nullptr);
  ble_npl_event_init(&t_forgetEv, onForget, nullptr);
  ble_npl_callout_init(&t_windowEnd, nimble_port_get_dflt_eventq(), onWindowEnd, nullptr);
  ble_npl_callout_init(&t_lingerEnd, nimble_port_get_dflt_eventq(), onLingerEnd, nullptr);
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_started = true;
  }
  nimble_port_freertos_init(hostTask);
  return ESP_OK;
}

void setEnabled(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_enabled = enabled;
  }
  kick();
}

void setName(const std::string& deviceName) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_name = deviceName;
  }
  kick();
}

void setConnect(Connect mode) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_mode = mode;
  }
  kick();
}

void want(const Addr& host) {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto cur = g_demand.target(monoMs());
    g_demand.want(host);
    if (cur == host) return;  // already wanted (polled every 100 ms): nothing to redo
  }
  kick();
}

void done() {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_demand.done(monoMs());
  }
  kick();
}

void drop() {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_demand.target(monoMs())) return;
    g_demand.drop();
  }
  kick();
}

std::optional<Addr> linked() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_link.trusted()) return std::nullopt;
  return g_link.peer.addr;
}

PairResult canPair() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_started || !g_synced) return PairResult::Unavailable;
  if (!g_enabled) return PairResult::Disabled;
  if (g_bondKeys.size() >= kMaxBonds) return PairResult::BondsFull;
  return PairResult::Ok;
}

PairResult openPairing() {
  const PairResult r = canPair();
  if (r != PairResult::Ok) return r;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_window.open(monoMs());
  }
  ESP_LOGI(TAG, "pairing window open for %u s", static_cast<unsigned>(kPairingWindowMs / 1000));
  kick();
  return PairResult::Ok;
}

void closePairing() {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_window.active(monoMs())) return;
    g_window.close();
  }
  kick();
}

bool pairing() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_window.active(monoMs());
}

Status status() {
  std::lock_guard<std::mutex> lock(g_mu);
  const int64_t now = monoMs();
  Status s;
  s.enabled = g_enabled;
  s.pairing = g_window.active(now);
  s.pairingLeftMs = g_window.leftMs(now);
  s.bonds = g_bonds;
  if (g_link.trusted()) {
    Peer p;
    p.addr = g_link.peer.addr;
    for (const Peer& b : g_bonds) {
      if (b.addr == p.addr) p = b;
    }
    s.connected = p;
  }
  return s;
}

bool ready() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_enabled || !g_link.trusted()) return false;
  return gatt::bootProtocol() ? g_link.subBoot : g_link.subInput;
}

bool capsLock() {
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_link.trusted()) return false;
  }
  return (gatt::leds() & kLedCapsLockBit) != 0;
}

bool sendKey(uint8_t modifier, uint8_t keycode) {
  const uint8_t report[kInputReportLen] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
  const int64_t deadline = esp_timer_get_time() + kSendTimeoutUs;
  for (;;) {
    uint16_t conn;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      const bool sub = gatt::bootProtocol() ? g_link.subBoot : g_link.subInput;
      if (!g_enabled || !g_link.trusted() || !sub) {
        ESP_LOGW(TAG, "key not sent: enabled=%d trusted=%d subscribed=%d boot=%d", int(g_enabled),
                 int(g_link.trusted()), int(sub), int(gatt::bootProtocol()));
        return false;
      }
      conn = g_link.conn;
    }
    const uint16_t handle = gatt::bootProtocol() ? gatt::bootInputHandle() : gatt::inputHandle();
    // A null mbuf would make NimBLE send the attribute's stored value instead.
    os_mbuf* om = ble_hs_mbuf_from_flat(report, sizeof report);
    if (om != nullptr) {
      const int rc = ble_gatts_notify_custom(conn, handle, om);  // consumes om either way
      if (rc == 0) return true;
      if (rc != BLE_HS_ENOMEM) {
        ESP_LOGW(TAG, "notify failed: %d", rc);
        return false;
      }
    }
    if (esp_timer_get_time() > deadline) return false;  // the host stopped taking reports
    vTaskDelay(1);
  }
}

esp_err_t forget(const Addr& addr) {
  ForgetReq req;
  req.addr = addr;
  return runForget(req);
}

esp_err_t forgetAll() {
  ForgetReq req;
  req.all = true;
  return runForget(req);
}

}  // namespace keyra::ble
