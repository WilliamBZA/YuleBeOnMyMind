// Non-volatile storage on NVS (design D6): one namespace per kind of data, each holding a single JSON
// document built by the hardware-independent core. Wi-Fi credentials are kept by the ESP-IDF Wi-Fi stack
// itself and are erased separately, so a Wi-Fi reset leaves everything else alone.
#pragma once

#include <string>

#include "membership_config.h"
#include "ota_policy.h"
#include "settings_model.h"

class Storage {
 public:
  // `profile`: the owner's settings. A missing or damaged document gives the defaults.
  yule::Profile loadProfile();
  void saveProfile(const yule::Profile& profile);

  // `ident`: the broker password and the optional broker override.
  yule::Ident loadIdent();
  void saveIdent(const yule::Ident& ident);

  // `membership`: the last config received from the server (groups and channel). Empty until one arrives.
  yule::Membership loadMembership();
  void saveMembership(const yule::Membership& membership);

  // `ota`: provisional flag, boot attempts, rolled-back versions and the last update result.
  yule::OtaState loadOtaState();
  void saveOtaState(const yule::OtaState& state);

  // Forgets the saved Wi-Fi network and nothing else.
  void eraseWifiCredentials();

 private:
  static std::string read(const char* ns);
  static void write(const char* ns, const std::string& json);
};
