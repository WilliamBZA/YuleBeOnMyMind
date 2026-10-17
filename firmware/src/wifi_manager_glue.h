// Wi-Fi onboarding and reconnection on WiFiManager, in non-blocking mode (device-provisioning).
//
// The decisions (when to open the setup portal, when to retry, how long to wait) are made by the core's
// WifiSupervisor and tested on a PC. This class carries them out against the Wi-Fi driver and WiFiManager.
//
//  - No saved network: the portal "YuleBeOnMyMind-<suffix>" opens and stays open.
//  - Saved network: connect with back-off; the portal is never opened automatically.
//  - Very long press: the saved network is set aside and the portal opens for 10 minutes. If nobody
//    configures a network the portal closes and the device goes back to the network it had.
//  - The owner's settings server must not run while portalActive() is true (WiFiManager uses port 80).
#pragma once

#include <Arduino.h>
#include <WiFiManager.h>

#include <string>

#include "storage.h"
#include "wifi_supervisor.h"

class WifiGlue {
 public:
  explicit WifiGlue(Storage& storage) : storage_(storage) {}

  // `hostname` is the DHCP/mDNS name (yule-<suffix>); `apName` the setup access point (YuleBeOnMyMind-<suffix>).
  void begin(const std::string& hostname, const std::string& apName, uint32_t nowMs);

  // Call every loop tick.
  void update(uint32_t nowMs);

  // The very long press: open the setup portal now.
  void openPortalOnDemand(uint32_t nowMs);

  bool connected() const { return sup_.connected(); }
  bool portalActive() const { return sup_.portalActive(); }
  uint32_t connectedForMs(uint32_t nowMs) const { return sup_.connectedForMs(nowMs); }

  String ssid() const;
  String localIp() const;

 private:
  // The network a very long press set aside, restored if the portal times out unused.
  struct Held {
    bool valid = false;
    String ssid;
    String password;
  };

  void apply(const yule::WifiSupervisor::Actions& actions, uint32_t nowMs);
  void openPortal(uint32_t nowMs);
  void closePortal();
  void beginConnect();
  bool hasNetworkToReturnTo();
  void logTransitions(uint32_t nowMs);

  Storage& storage_;
  WiFiManager wm_;
  yule::WifiSupervisor sup_;
  std::string hostname_;
  std::string apName_;
  Held held_;

  bool savedCache_ = false;
  uint32_t savedCheckedAtMs_ = 0;
  bool savedChecked_ = false;
  uint32_t portalTriedAtMs_ = 0;
  yule::WifiSupervisor::State lastState_ = yule::WifiSupervisor::State::Attempting;
};
