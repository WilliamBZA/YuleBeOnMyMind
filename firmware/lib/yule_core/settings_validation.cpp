#include "settings_validation.h"

#include <ArduinoJson.h>

#include "rtttl.h"
#include "song_library.h"
#include "text_util.h"
#include "tz_table.h"
#include "yule_limits.h"

namespace yule {

namespace {

bool printableAscii(std::string_view s) {
  for (char c : s) {
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

bool validHostname(std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  if (host.front() == '.' || host.back() == '.' || host.front() == '-' || host.back() == '-') return false;
  size_t labelLen = 0;
  for (char c : host) {
    if (c == '.') {
      if (labelLen == 0) return false;
      labelLen = 0;
      continue;
    }
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
    if (++labelLen > 63) return false;
  }
  return labelLen > 0;
}

std::string secondsText(uint32_t ms) {
  const uint32_t tenths = (ms + 50) / 100;
  std::string s = std::to_string(tenths / 10);
  if (tenths % 10) s += "." + std::to_string(tenths % 10);
  return s;
}

}  // namespace

bool ValidationResult::hasError(std::string_view field) const {
  for (const FieldIssue& e : errors) {
    if (field == e.field) return true;
  }
  return false;
}

bool ValidationResult::hasWarning(std::string_view field) const {
  for (const FieldIssue& w : warnings) {
    if (field == w.field) return true;
  }
  return false;
}

void ValidationResult::merge(const ValidationResult& other) {
  errors.insert(errors.end(), other.errors.begin(), other.errors.end());
  warnings.insert(warnings.end(), other.warnings.begin(), other.warnings.end());
}

std::string encodeValidation(const ValidationResult& r) {
  JsonDocument doc;
  doc["ok"] = r.ok();
  JsonArray errors = doc["errors"].to<JsonArray>();
  for (const FieldIssue& e : r.errors) {
    JsonObject o = errors.add<JsonObject>();
    o["field"] = e.field;
    o["message"] = e.message;
  }
  JsonArray warnings = doc["warnings"].to<JsonArray>();
  for (const FieldIssue& w : r.warnings) {
    JsonObject o = warnings.add<JsonObject>();
    o["field"] = w.field;
    o["message"] = w.message;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool validateDisplayName(std::string_view raw, std::string& trimmedOut, ValidationResult& out) {
  const std::string trimmed = trimWhitespace(raw);
  const size_t chars = utf8Length(trimmed);
  if (chars < 1) {
    out.error("name", "Display name can't be empty. Enter 1 to 24 characters.");
    return false;
  }
  if (chars > limits::kMaxDisplayNameChars) {
    out.error("name", "Display name is too long (" + std::to_string(chars) + " characters). Use 24 or fewer.");
    return false;
  }
  trimmedOut = trimmed;
  return true;
}

bool validateCustomSong(std::string_view rtttl, ValidationResult& out) {
  if (rtttl.size() > limits::kMaxSongChars) {
    out.error("song", "The custom song is too long (" + std::to_string(rtttl.size()) + " characters). The limit is " +
                          std::to_string(limits::kMaxSongChars) + ".");
    return false;
  }
  const RtttlResult parsed = parseRtttl(rtttl);
  if (!parsed.ok) {
    out.error("song", "The custom song isn't valid RTTTL: " + parsed.error.message + " (at character " +
                          std::to_string(parsed.error.position) + ").");
    return false;
  }
  const uint32_t total = parsed.melody.totalMs();
  if (total > limits::kMelodyMaxMs) {
    out.warn("song", "This song plays for " + secondsText(total) + " seconds. Only the first 30 seconds will be played.");
  }
  return true;
}

bool validateLibrarySong(std::string_view id, ValidationResult& out) {
  if (!findCarol(id)) {
    out.error("song", "That built-in song doesn't exist.");
    return false;
  }
  return true;
}

bool validateEffectName(std::string_view name, Effect& effectOut, ValidationResult& out) {
  Effect e;
  if (!effectFromName(name, e)) {
    out.error("pattern.effect", "Unknown light effect.");
    return false;
  }
  effectOut = e;
  return true;
}

bool validateColors(const std::vector<std::string>& colors, Pattern& patternOut, ValidationResult& out) {
  if (colors.empty() || colors.size() > limits::kMaxPatternColors) {
    out.error("pattern.colors", "Choose 1 to 3 colours.");
    return false;
  }
  Rgb parsed[limits::kMaxPatternColors];
  for (size_t i = 0; i < colors.size(); ++i) {
    if (!parseHexColor(colors[i], parsed[i])) {
      out.error("pattern.colors", "Colour " + std::to_string(i + 1) + " isn't a valid colour (expected #RRGGBB).");
      return false;
    }
  }
  for (size_t i = 0; i < colors.size(); ++i) patternOut.colors[i] = parsed[i];
  patternOut.colorCount = static_cast<uint8_t>(colors.size());
  return true;
}

bool validateSpeed(int speed, ValidationResult& out) {
  if (speed < limits::kMinSpeed || speed > limits::kMaxSpeed) {
    out.error("pattern.speed", "Speed must be slow, medium or fast.");
    return false;
  }
  return true;
}

bool validateBrightness(int percent, ValidationResult& out) {
  if (percent < limits::kMinBrightnessPercent || percent > limits::kMaxBrightnessPercent) {
    out.error("brightness", "Brightness must be between 5% and 100%.");
    return false;
  }
  return true;
}

bool validateQuietTimes(std::string_view start, std::string_view end, QuietHours& quietOut, ValidationResult& out) {
  const int s = parseClockTime(start);
  const int e = parseClockTime(end);
  bool ok = true;
  if (s < 0) {
    out.error("quiet.start", "Start time must be a 24-hour time such as 21:30.");
    ok = false;
  }
  if (e < 0) {
    out.error("quiet.end", "End time must be a 24-hour time such as 07:00.");
    ok = false;
  }
  if (ok) {
    quietOut.startMinute = s;
    quietOut.endMinute = e;
  }
  return ok;
}

bool validateTimeZoneRule(std::string_view rule, ValidationResult& out) {
  const std::string trimmed = trimWhitespace(rule);
  if (trimmed.empty()) {
    out.error("tz.rule", "The time-zone rule can't be empty. Pick a time zone or enter a POSIX rule.");
    return false;
  }
  if (trimmed.size() > 63 || !printableAscii(trimmed)) {
    out.error("tz.rule", "The time-zone rule is too long or has unusual characters.");
    return false;
  }
  return true;
}

bool validateBrokerOverride(std::string_view host, int port, ValidationResult& out) {
  bool ok = true;
  if (!host.empty() && !validHostname(host)) {
    out.error("device.host", "That isn't a valid host name or address.");
    ok = false;
  }
  if (port < 0 || port > 65535) {
    out.error("device.port", "Port must be between 1 and 65535.");
    ok = false;
  }
  return ok;
}

bool validatePassword(std::string_view password, ValidationResult& out) {
  if (password.empty() || password.size() > 128 || !printableAscii(password)) {
    out.error("device.password", "The broker password must be 1 to 128 ordinary characters.");
    return false;
  }
  return true;
}

// ---- request helpers ----

namespace {

SettingsUpdate invalidJson(const Profile& current) {
  SettingsUpdate u;
  u.profile = current;
  u.validation.error("", "That isn't valid settings data.");
  return u;
}

}  // namespace

SettingsUpdate applySettingsJson(const Profile& current, std::string_view json) {
  JsonDocument doc;
  if (json.empty() || json.size() > 4096 || deserializeJson(doc, json.data(), json.size())) return invalidJson(current);
  const JsonDocument& root = doc;
  if (!root.is<JsonObjectConst>()) return invalidJson(current);

  SettingsUpdate u;
  u.profile = current;
  Profile& p = u.profile;
  ValidationResult& v = u.validation;

  JsonVariantConst name = root["name"];
  if (!name.isNull()) {
    if (!name.is<const char*>()) {
      v.error("name", "Display name must be text.");
    } else {
      std::string trimmed;
      if (validateDisplayName(name.as<const char*>(), trimmed, v)) p.displayName = trimmed;
    }
  }

  JsonVariantConst song = root["song"];
  if (!song.isNull()) {
    if (!song.is<JsonObjectConst>()) {
      v.error("song", "Song must be a built-in song or a custom tune.");
    } else {
      const std::string kind = song["kind"].is<const char*>() ? song["kind"].as<const char*>() : "";
      if (kind == "custom") {
        const std::string rtttl = song["rtttl"].is<const char*>() ? song["rtttl"].as<const char*>() : "";
        if (validateCustomSong(rtttl, v)) {
          p.song.kind = SongChoice::Kind::Custom;
          p.song.rtttl = rtttl;
          p.song.libraryId.clear();
        }
      } else if (kind == "library") {
        const std::string id = song["id"].is<const char*>() ? song["id"].as<const char*>() : "";
        if (validateLibrarySong(id, v)) {
          p.song.kind = SongChoice::Kind::Library;
          p.song.libraryId = id;
          p.song.rtttl.clear();
        }
      } else {
        v.error("song", "Song must be a built-in song or a custom tune.");
      }
    }
  }

  JsonVariantConst pattern = root["pattern"];
  if (!pattern.isNull()) {
    if (!pattern.is<JsonObjectConst>()) {
      v.error("pattern", "Light pattern must have an effect, colours and a speed.");
    } else {
      JsonVariantConst effect = pattern["effect"];
      if (!effect.isNull()) {
        Effect e;
        if (!effect.is<const char*>()) {
          v.error("pattern.effect", "Unknown light effect.");
        } else if (validateEffectName(effect.as<const char*>(), e, v)) {
          p.pattern.effect = e;
        }
      }
      JsonVariantConst colors = pattern["colors"];
      if (!colors.isNull()) {
        if (!colors.is<JsonArrayConst>()) {
          v.error("pattern.colors", "Choose 1 to 3 colours.");
        } else {
          // A non-text entry becomes an empty string, which fails the colour check.
          std::vector<std::string> list;
          for (JsonVariantConst c : colors.as<JsonArrayConst>()) {
            list.push_back(c.is<const char*>() ? c.as<const char*>() : "");
          }
          validateColors(list, p.pattern, v);
        }
      }
      JsonVariantConst speed = pattern["speed"];
      if (!speed.isNull()) {
        if (!speed.is<int>()) {
          v.error("pattern.speed", "Speed must be slow, medium or fast.");
        } else if (validateSpeed(speed.as<int>(), v)) {
          p.pattern.speed = static_cast<uint8_t>(speed.as<int>());
        }
      }
    }
  }

  JsonVariantConst brightness = root["brightness"];
  if (!brightness.isNull()) {
    if (!brightness.is<int>()) {
      v.error("brightness", "Brightness must be between 5% and 100%.");
    } else if (validateBrightness(brightness.as<int>(), v)) {
      p.brightness = static_cast<uint8_t>(brightness.as<int>());
    }
  }

  JsonVariantConst muted = root["muted"];
  if (!muted.isNull()) {
    if (muted.is<bool>()) {
      p.muted = muted.as<bool>();
    } else {
      v.error("muted", "Mute must be on or off.");
    }
  }

  JsonVariantConst quiet = root["quiet"];
  if (!quiet.isNull()) {
    if (!quiet.is<JsonObjectConst>()) {
      v.error("quiet", "Quiet hours need a start and an end time.");
    } else {
      JsonVariantConst enabled = quiet["enabled"];
      if (!enabled.isNull()) {
        if (enabled.is<bool>()) {
          p.quiet.enabled = enabled.as<bool>();
        } else {
          v.error("quiet.enabled", "Quiet hours must be on or off.");
        }
      }
      JsonVariantConst start = quiet["start"];
      JsonVariantConst end = quiet["end"];
      if (!start.isNull() || !end.isNull()) {
        const std::string s = start.is<const char*>() ? start.as<const char*>() : (start.isNull() ? formatClockTime(p.quiet.startMinute) : "");
        const std::string e = end.is<const char*>() ? end.as<const char*>() : (end.isNull() ? formatClockTime(p.quiet.endMinute) : "");
        validateQuietTimes(s, e, p.quiet, v);
      }
    }
  }

  JsonVariantConst tz = root["tz"];
  if (!tz.isNull()) {
    if (!tz.is<JsonObjectConst>()) {
      v.error("tz.rule", "Pick a time zone or enter a POSIX rule.");
    } else {
      std::string tzName = p.timeZone.name;
      std::string rule = p.timeZone.rule;
      JsonVariantConst n = tz["name"];
      JsonVariantConst r = tz["rule"];
      if (n.is<const char*>()) tzName = trimWhitespace(n.as<const char*>());
      if (r.is<const char*>()) rule = trimWhitespace(r.as<const char*>());
      // A zone from the menu with no explicit rule takes the table's rule.
      if (r.isNull() || (r.is<const char*>() && rule.empty())) {
        if (const TimeZoneInfo* z = findTimeZone(tzName)) rule = z->rule;
      }
      if (validateTimeZoneRule(rule, v)) {
        p.timeZone.rule = trimWhitespace(rule);
        p.timeZone.name = tzName.empty() ? "custom" : tzName.substr(0, 64);
      }
    }
  }

  if (!v.ok()) u.profile = current;  // all or nothing
  return u;
}

DeviceUpdate applyDeviceJson(const Ident& current, std::string_view json) {
  DeviceUpdate u;
  u.ident = current;
  JsonDocument doc;
  if (json.empty() || json.size() > 1024 || deserializeJson(doc, json.data(), json.size()) || !doc.is<JsonObject>()) {
    u.validation.error("", "That isn't valid device setup data.");
    return u;
  }
  const JsonDocument& root = doc;
  ValidationResult& v = u.validation;

  JsonVariantConst password = root["password"];
  if (!password.isNull()) {
    if (!password.is<const char*>()) {
      v.error("device.password", "The broker password must be 1 to 128 ordinary characters.");
    } else if (std::string_view(password.as<const char*>()).empty()) {
      // empty means "keep the stored password"
    } else if (validatePassword(password.as<const char*>(), v)) {
      u.ident.password = password.as<const char*>();
    }
  }

  std::string host = current.brokerHost;
  int port = current.brokerPort;
  JsonVariantConst h = root["host"];
  JsonVariantConst pr = root["port"];
  if (!h.isNull()) {
    if (h.is<const char*>()) {
      host = trimWhitespace(h.as<const char*>());
    } else {
      v.error("device.host", "That isn't a valid host name or address.");
    }
  }
  if (!pr.isNull()) {
    if (pr.is<int>()) {
      port = pr.as<int>();
    } else {
      v.error("device.port", "Port must be between 1 and 65535.");
    }
  }
  if (validateBrokerOverride(host, port, v) && v.ok()) {
    u.ident.brokerHost = host;
    u.ident.brokerPort = static_cast<uint16_t>(port);
  }
  if (!v.ok()) u.ident = current;  // all or nothing
  return u;
}

}  // namespace yule
