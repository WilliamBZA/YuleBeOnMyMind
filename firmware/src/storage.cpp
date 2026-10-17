#include "storage.h"

#include <Preferences.h>
#include <WiFi.h>

#include "log.h"

namespace {

constexpr const char* kNsProfile = "profile";
constexpr const char* kNsIdent = "ident";
constexpr const char* kNsMembership = "membership";
constexpr const char* kNsOta = "ota";
constexpr const char* kKey = "json";

}  // namespace

std::string Storage::read(const char* ns) {
  Preferences prefs;
  if (!prefs.begin(ns, false)) {
    yuleLog("nvs", "cannot open namespace '%s'", ns);
    return {};
  }
  const String value = prefs.getString(kKey, "");
  prefs.end();
  return std::string(value.c_str());
}

void Storage::write(const char* ns, const std::string& json) {
  Preferences prefs;
  if (!prefs.begin(ns, false)) {
    yuleLog("nvs", "cannot open namespace '%s' to save", ns);
    return;
  }
  // Skip the write when nothing changed, to spare the flash.
  const String current = prefs.getString(kKey, "");
  if (json != current.c_str()) {
    if (prefs.putString(kKey, json.c_str()) == 0) yuleLog("nvs", "saving '%s' failed", ns);
  }
  prefs.end();
}

yule::Profile Storage::loadProfile() {
  const std::string json = read(kNsProfile);
  return json.empty() ? yule::defaultProfile() : yule::decodeProfile(json);
}

void Storage::saveProfile(const yule::Profile& profile) { write(kNsProfile, yule::encodeProfile(profile)); }

yule::Ident Storage::loadIdent() { return yule::decodeIdent(read(kNsIdent)); }

void Storage::saveIdent(const yule::Ident& ident) { write(kNsIdent, yule::encodeIdent(ident)); }

yule::Membership Storage::loadMembership() {
  const std::string json = read(kNsMembership);
  if (json.empty()) return yule::Membership();
  const yule::ConfigResult result = yule::decodeDeviceConfig(json);
  if (!result.ok) {
    yuleLog("nvs", "saved membership is not valid (%s); starting with no groups", result.error.c_str());
    return yule::Membership();
  }
  return result.membership;
}

void Storage::saveMembership(const yule::Membership& membership) {
  write(kNsMembership, yule::encodeMembership(membership));
}

yule::OtaState Storage::loadOtaState() { return yule::OtaState::fromJson(read(kNsOta)); }

void Storage::saveOtaState(const yule::OtaState& state) { write(kNsOta, state.toJson()); }

void Storage::eraseWifiCredentials() {
  WiFi.mode(WIFI_STA);  // the Wi-Fi driver must be running to touch its saved configuration
  WiFi.persistent(true);
  WiFi.eraseAP();
}
