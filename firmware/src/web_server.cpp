#include "web_server.h"

#include <WiFi.h>

#include "generated/web_index.h"  // kWebIndexGz, written by scripts/embed_web.py before every build
#include "log.h"
#include "settings_api.h"

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

}  // namespace

void SettingsServer::begin(const std::string& hostname) {
  hostname_ = lower(hostname);
  if (!routed_) {
    routes();
    static const char* headers[] = {"Content-Type"};
    server_.collectHeaders(headers, 1);
    routed_ = true;
  }
  server_.begin();
  running_ = true;
  yuleLog("web", "settings page at http://%s.local/ (and http://%s/)", hostname_.c_str(), WiFi.localIP().toString().c_str());
}

void SettingsServer::stop() {
  if (!running_) return;
  server_.stop();
  running_ = false;
  yuleLog("web", "settings page stopped");
}

void SettingsServer::handle() {
  if (running_) server_.handleClient();
}

bool SettingsServer::hostAllowed() {
  std::string host = lower(server_.hostHeader().c_str());
  const size_t colon = host.find(':');
  if (colon != std::string::npos) host.resize(colon);
  if (host.size() > 1 && host.back() == '.') host.pop_back();
  // yule-<suffix> under any domain: .local from mDNS, or the router's own (.lan, .home, .fritz.box, ...).
  const bool ourName = host == hostname_ || host.rfind(hostname_ + ".", 0) == 0;
  if (ourName || host == lower(WiFi.localIP().toString().c_str())) return true;
  server_.send(421, "text/plain", "This device answers only to its own name or address.");
  return false;
}

bool SettingsServer::jsonPost() {
  const std::string type = lower(server_.header("Content-Type").c_str());
  if (type.rfind("application/json", 0) == 0) return true;
  server_.send(415, "text/plain", "Send JSON (Content-Type: application/json).");
  return false;
}

void SettingsServer::sendJson(int code, const std::string& body) {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(code, "application/json", body.c_str());
}

void SettingsServer::sendValidation(const std::string& body, bool ok) { sendJson(ok ? 200 : 400, body); }

void SettingsServer::routes() {
  server_.on("/", HTTP_GET, [this]() {
    if (!hostAllowed()) return;
    server_.sendHeader("Content-Encoding", "gzip");
    server_.sendHeader("Cache-Control", "no-cache");
    server_.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(kWebIndexGz), sizeof(kWebIndexGz));
  });

  server_.on("/api/status", HTTP_GET, [this]() {
    if (hostAllowed()) sendJson(200, api_.statusJson());
  });

  server_.on("/api/library", HTTP_GET, [this]() {
    if (hostAllowed()) sendJson(200, yule::encodeLibrary());
  });

  server_.on("/api/settings", HTTP_GET, [this]() {
    if (hostAllowed()) sendJson(200, api_.settingsJson());
  });

  server_.on("/api/settings", HTTP_POST, [this]() {
    if (!hostAllowed() || !jsonPost()) return;
    bool ok = false;
    const std::string body = api_.saveSettings(server_.arg("plain").c_str(), ok);
    sendValidation(body, ok);
  });

  server_.on("/api/preview", HTTP_POST, [this]() {
    if (!hostAllowed() || !jsonPost()) return;
    bool ok = false;
    const std::string body = api_.preview(server_.arg("plain").c_str(), ok);
    sendValidation(body, ok);
  });

  server_.on("/api/device", HTTP_POST, [this]() {
    if (!hostAllowed() || !jsonPost()) return;
    bool ok = false;
    const std::string body = api_.saveDevice(server_.arg("plain").c_str(), ok);
    sendValidation(body, ok);
  });

  server_.onNotFound([this]() {
    if (hostAllowed()) server_.send(404, "text/plain", "Not found");
  });
}
