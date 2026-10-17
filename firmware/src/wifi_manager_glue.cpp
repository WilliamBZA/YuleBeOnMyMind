#include "wifi_manager_glue.h"

#include <WiFi.h>
#include <esp_random.h>

#include <vector>

#include "log.h"
#include "yule_config.h"

using yule::WifiSupervisor;

void WifiGlue::begin(const std::string& hostname, const std::string& apName, uint32_t nowMs) {
  hostname_ = hostname;
  apName_ = apName;

  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // the supervisor owns retries, with back-off
  WiFi.setHostname(hostname_.c_str());

  // Log why a connection failed (wrong password, network not found, ...).
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        yuleLog("wifi", "disconnected: %s",
                WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason)));
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  wm_.setConfigPortalBlocking(false);  // loop() keeps animating; update() calls wm_.process()
  wm_.setTitle("Yule Be On My Mind");
  // Joining the network the user typed blocks inside WiFiManager; a wrong password returns after this long.
  wm_.setSaveConnectTimeout(15);
  // The setup page offers only "Configure WiFi": no firmware-upload, erase or restart buttons.
  static std::vector<const char*> menu = {"wifi"};
  wm_.setMenu(menu);
  wm_.setShowInfoErase(false);
  wm_.setShowInfoUpdate(false);

  WifiSupervisor::Config cfg;
  cfg.attemptTimeoutMs = YULE_WIFI_ATTEMPT_TIMEOUT_MS;
  cfg.portalTimeoutMs = YULE_PORTAL_TIMEOUT_MS;
  cfg.backoffBaseMs = YULE_BACKOFF_BASE_MS;
  cfg.backoffCapMs = YULE_BACKOFF_CAP_MS;
  cfg.seed = esp_random();
  sup_ = WifiSupervisor(cfg);
  lastState_ = sup_.state();

  const bool saved = hasNetworkToReturnTo();
  if (saved) {
    yuleLog("wifi", "saved network '%s'", wm_.getWiFiSSID().c_str());
  } else {
    yuleLog("wifi", "no saved network");
  }
  apply(sup_.start(nowMs, saved), nowMs);
  logTransitions(nowMs);
}

void WifiGlue::update(uint32_t nowMs) {
  // WiFiManager serves the portal's pages and finishes a "save" from here. It returns true when the network
  // the user entered has been joined; it then closes the portal itself.
  if (wm_.getConfigPortalActive()) wm_.process();

  const bool connected = WiFi.status() == WL_CONNECTED;
  apply(sup_.update(nowMs, connected, hasNetworkToReturnTo()), nowMs);

  // The portal could not start (rare); keep trying while the supervisor wants it open.
  if (sup_.portalActive() && !wm_.getConfigPortalActive() && nowMs - portalTriedAtMs_ >= 5000) {
    openPortal(nowMs);
  }
  logTransitions(nowMs);
}

void WifiGlue::openPortalOnDemand(uint32_t nowMs) {
  apply(sup_.requestPortal(nowMs), nowMs);
  logTransitions(nowMs);
}

String WifiGlue::ssid() const { return WiFi.SSID(); }

String WifiGlue::localIp() const { return WiFi.localIP().toString(); }

void WifiGlue::apply(const WifiSupervisor::Actions& actions, uint32_t nowMs) {
  if (actions.closePortal) closePortal();
  if (actions.openPortal) openPortal(nowMs);
  if (actions.beginConnect) beginConnect();
}

void WifiGlue::openPortal(uint32_t nowMs) {
  portalTriedAtMs_ = nowMs;

  if (sup_.portalReason() == WifiSupervisor::PortalReason::OnDemand && !held_.valid && wm_.getWiFiIsSaved()) {
    // A very long press clears the saved Wi-Fi network, and only that. Remember it until a new network is
    // joined, so that an accidental press (nobody finishes the setup) can go back to it.
    held_.ssid = wm_.getWiFiSSID();
    held_.password = wm_.getWiFiPass();
    held_.valid = true;
    storage_.eraseWifiCredentials();
    yuleLog("wifi", "forgot the saved network '%s' for now; it comes back if setup is not finished",
            held_.ssid.c_str());
  }

  WiFi.disconnect(false, false);  // leave the old network; the credentials were handled above
  wm_.startConfigPortal(apName_.c_str());  // non-blocking: returns at once
  if (wm_.getConfigPortalActive()) {
    yuleLog("wifi", "setup portal open: join the Wi-Fi network '%s' (page at http://192.168.4.1)",
            apName_.c_str());
  } else {
    yuleLog("wifi", "the setup portal did not start; retrying");
  }
}

void WifiGlue::closePortal() {
  // WiFiManager's shutdown pauses for about a second, during which the animations stand still.
  if (wm_.getConfigPortalActive()) wm_.stopConfigPortal();
  if (WiFi.status() == WL_CONNECTED) held_.valid = false;  // a new network was joined: the old one is gone
  yuleLog("wifi", "setup portal closed");
}

void WifiGlue::beginConnect() {
  WiFi.mode(WIFI_STA);
  if (held_.valid) {
    yuleLog("wifi", "going back to the saved network '%s'", held_.ssid.c_str());
    WiFi.persistent(true);
    WiFi.begin(held_.ssid.c_str(), held_.password.c_str());  // saves it again
    held_ = Held();
  } else {
    WiFi.begin();  // the network saved in flash
    yuleLog("wifi", "connecting to '%s' (attempt %u)", wm_.getWiFiSSID().c_str(),
            static_cast<unsigned>(sup_.attemptsStarted()));
  }
  savedChecked_ = false;
}

bool WifiGlue::hasNetworkToReturnTo() {
  if (held_.valid) return true;
  // Asking the driver is cheap but not free, and this runs every tick.
  if (!savedChecked_ || millis() - savedCheckedAtMs_ >= 1000) {
    savedCache_ = wm_.getWiFiIsSaved();
    savedCheckedAtMs_ = millis();
    savedChecked_ = true;
  }
  return savedCache_;
}

void WifiGlue::logTransitions(uint32_t nowMs) {
  const WifiSupervisor::State state = sup_.state();
  if (state == lastState_) return;
  switch (state) {
    case WifiSupervisor::State::Connected:
      yuleLog("wifi", "connected to '%s', IP %s, %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
              WiFi.RSSI());
      break;
    case WifiSupervisor::State::BackingOff:
      yuleLog("wifi", "%s; trying again in %.1f s", lastState_ == WifiSupervisor::State::Connected
                                                       ? "connection lost"
                                                       : "could not connect",
              sup_.currentBackoffMs() / 1000.0);
      break;
    case WifiSupervisor::State::Attempting:
    case WifiSupervisor::State::Portal:
      break;  // logged where the action is carried out
  }
  (void)nowMs;
  lastState_ = state;
}
