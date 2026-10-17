// Validation of owner settings and device-setup input. Each validator reports per-field errors and
// warnings in the device's own words, which the settings page shows as-is. Hardware independent.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "settings_model.h"

namespace yule {

struct FieldIssue {
  std::string field;    // e.g. "name", "song", "pattern.colors", "quiet.start", "device.port"
  std::string message;
};

struct ValidationResult {
  std::vector<FieldIssue> errors;
  std::vector<FieldIssue> warnings;

  bool ok() const { return errors.empty(); }
  bool hasError(std::string_view field) const;
  bool hasWarning(std::string_view field) const;
  void error(std::string field, std::string message) { errors.push_back({std::move(field), std::move(message)}); }
  void warn(std::string field, std::string message) { warnings.push_back({std::move(field), std::move(message)}); }
  void merge(const ValidationResult& other);
};

// {"ok":true,"errors":[{"field":"name","message":"..."}],"warnings":[...]}  (the POST response body)
std::string encodeValidation(const ValidationResult& result);

// ---- Field validators. Each appends to `out`; on success they also set the parsed value. ----

// 1-24 characters after trimming whitespace.
bool validateDisplayName(std::string_view raw, std::string& trimmedOut, ValidationResult& out);

// A custom RTTTL tune: at most 512 characters, valid RTTTL (the error says where the first problem is).
// A tune that plays for more than 30 s is accepted with a warning that it will be cut off.
bool validateCustomSong(std::string_view rtttl, ValidationResult& out);

// A library choice must name one of the built-in songs.
bool validateLibrarySong(std::string_view id, ValidationResult& out);

// Effect name, 1-3 colours as "#RRGGBB", and speed 1-3.
bool validateEffectName(std::string_view name, Effect& effectOut, ValidationResult& out);
bool validateColors(const std::vector<std::string>& colors, Pattern& patternOut, ValidationResult& out);
bool validateSpeed(int speed, ValidationResult& out);

// 5-100 percent.
bool validateBrightness(int percent, ValidationResult& out);

// Both times as 24-hour "HH:MM". (start == end is allowed and means "no quiet hours".)
bool validateQuietTimes(std::string_view start, std::string_view end, QuietHours& quietOut, ValidationResult& out);

// A non-empty POSIX time-zone rule (at most 63 characters, printable ASCII).
bool validateTimeZoneRule(std::string_view rule, ValidationResult& out);

// Broker override: an empty host means "use the default"; a port of 0 means "use the default".
bool validateBrokerOverride(std::string_view host, int port, ValidationResult& out);

// A broker password: 1-128 printable characters (no control characters).
bool validatePassword(std::string_view password, ValidationResult& out);

// ---- Whole-request helpers used by the settings API. ----

struct SettingsUpdate {
  Profile profile;           // current profile with the request applied (meaningful only when validation.ok())
  ValidationResult validation;
  bool applied() const { return validation.ok(); }
};

// Applies a POST /api/settings body on top of `current`. Fields that are absent keep their current value.
// Unknown fields are ignored. If anything is invalid nothing changes: the caller keeps `current`.
SettingsUpdate applySettingsJson(const Profile& current, std::string_view json);

struct DeviceUpdate {
  Ident ident;
  ValidationResult validation;
  bool applied() const { return validation.ok(); }
};

// Applies a POST /api/device body ({"password":...,"host":...,"port":...}). An absent or empty password keeps
// the stored one, because the page never shows it.
DeviceUpdate applyDeviceJson(const Ident& current, std::string_view json);

}  // namespace yule
