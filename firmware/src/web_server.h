// The owner's settings page and its HTTP API on the home network (design D7):
//
//   GET  /              the page (web/index.html, gzipped into the firmware at build time)
//   GET  /api/status    device id, groups, Wi-Fi, broker, clock, firmware and update status
//   GET  /api/library   built-in songs, effects and their colours, time zones
//   GET  /api/settings  the saved profile
//   POST /api/settings  save (validated by the core; {ok, errors, warnings})
//   POST /api/preview   play a selection here only; nothing is broadcast
//   POST /api/device    broker password (write-only) and the optional broker override; reconnects
//
// The home network is trusted, so there is no login. Two checks keep other web pages out: POSTs must be
// application/json (a cross-site page cannot send that without the browser asking first, and this server
// never says yes), and the Host header must be this device's IP address or yule-<suffix> under some domain
// (which defeats DNS rebinding, short of guessing the 24-bit suffix).
// It must not run while the Wi-Fi setup portal is open: WiFiManager serves port 80 then.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include <string>

// What the pages read and change, implemented by App.
class SettingsApi {
 public:
  virtual ~SettingsApi() = default;
  virtual std::string statusJson() = 0;
  virtual std::string settingsJson() = 0;
  // Each returns the validation document; `ok` is false when nothing was changed.
  virtual std::string saveSettings(const std::string& body, bool& ok) = 0;
  virtual std::string preview(const std::string& body, bool& ok) = 0;
  virtual std::string saveDevice(const std::string& body, bool& ok) = 0;
};

class SettingsServer {
 public:
  explicit SettingsServer(SettingsApi& api) : api_(api), server_(80) {}

  // `hostname` is yule-<suffix>; requests naming it (bare or with a domain, such as .local) or the IP address
  // are served.
  void begin(const std::string& hostname);
  void stop();
  bool running() const { return running_; }

  // Call every loop tick while running.
  void handle();

 private:
  void routes();
  bool hostAllowed();
  bool jsonPost();
  void sendJson(int code, const std::string& body);
  void sendValidation(const std::string& body, bool ok);

  SettingsApi& api_;
  WebServer server_;
  std::string hostname_;
  bool running_ = false;
  bool routed_ = false;
};
