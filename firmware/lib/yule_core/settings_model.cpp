#include "settings_model.h"

#include <ArduinoJson.h>

#include "song_library.h"
#include "text_util.h"
#include "tz_table.h"
#include "yule_limits.h"

namespace yule {

bool Profile::operator==(const Profile& o) const {
  // Only the half of the song choice that its kind uses matters.
  const bool sameSong = song.kind == o.song.kind &&
                        (song.kind == SongChoice::Kind::Custom ? song.rtttl == o.song.rtttl : song.libraryId == o.song.libraryId);
  return displayName == o.displayName && sameSong && pattern == o.pattern && brightness == o.brightness && muted == o.muted &&
         quiet.enabled == o.quiet.enabled && quiet.startMinute == o.quiet.startMinute &&
         quiet.endMinute == o.quiet.endMinute && timeZone.name == o.timeZone.name && timeZone.rule == o.timeZone.rule;
}

Profile defaultProfile() {
  Profile p;
  p.song.kind = SongChoice::Kind::Library;
  p.song.libraryId = defaultCarol().id;
  p.timeZone.name = defaultTimeZone().name;
  p.timeZone.rule = defaultTimeZone().rule;
  return p;
}

std::string resolveSongRtttl(const SongChoice& song) {
  if (song.kind == SongChoice::Kind::Custom && !song.rtttl.empty()) return song.rtttl;
  if (const SongInfo* info = findCarol(song.libraryId)) return info->rtttl;
  return defaultCarol().rtttl;
}

std::string encodeProfile(const Profile& p) {
  JsonDocument doc;
  doc["schema"] = kSettingsSchemaVersion;
  doc["name"] = p.displayName;

  JsonObject song = doc["song"].to<JsonObject>();
  if (p.song.kind == SongChoice::Kind::Custom) {
    song["kind"] = "custom";
    song["rtttl"] = p.song.rtttl;
  } else {
    song["kind"] = "library";
    song["id"] = p.song.libraryId;
  }

  JsonObject pattern = doc["pattern"].to<JsonObject>();
  pattern["effect"] = effectName(p.pattern.effect);
  JsonArray colors = pattern["colors"].to<JsonArray>();
  const size_t n = p.pattern.colorCount > limits::kMaxPatternColors ? limits::kMaxPatternColors : p.pattern.colorCount;
  for (size_t i = 0; i < n; ++i) colors.add(formatHexColor(p.pattern.colors[i]));
  pattern["speed"] = p.pattern.speed;

  doc["brightness"] = p.brightness;
  doc["muted"] = p.muted;

  JsonObject quiet = doc["quiet"].to<JsonObject>();
  quiet["enabled"] = p.quiet.enabled;
  quiet["start"] = formatClockTime(p.quiet.startMinute);
  quiet["end"] = formatClockTime(p.quiet.endMinute);

  JsonObject tz = doc["tz"].to<JsonObject>();
  tz["name"] = p.timeZone.name;
  tz["rule"] = p.timeZone.rule;

  std::string out;
  serializeJson(doc, out);
  return out;
}

Profile decodeProfile(std::string_view json) {
  Profile p = defaultProfile();
  JsonDocument doc;
  if (json.empty() || deserializeJson(doc, json.data(), json.size())) return p;
  const JsonDocument& root = doc;
  if (!root.is<JsonObjectConst>()) return p;

  JsonVariantConst name = root["name"];
  if (name.is<const char*>()) {
    const std::string trimmed = trimWhitespace(name.as<const char*>());
    const size_t chars = utf8Length(trimmed);
    if (chars >= 1 && chars <= limits::kMaxDisplayNameChars) p.displayName = trimmed;
  }

  JsonVariantConst song = root["song"];
  if (song.is<JsonObjectConst>()) {
    JsonVariantConst kind = song["kind"];
    if (kind.is<const char*>() && std::string_view(kind.as<const char*>()) == "custom") {
      JsonVariantConst r = song["rtttl"];
      if (r.is<const char*>() && r.as<std::string>().size() > 0 && r.as<std::string>().size() <= limits::kMaxSongChars) {
        p.song.kind = SongChoice::Kind::Custom;
        p.song.rtttl = r.as<const char*>();
        p.song.libraryId.clear();
      }
    } else {
      JsonVariantConst id = song["id"];
      if (id.is<const char*>() && findCarol(id.as<const char*>())) p.song.libraryId = id.as<const char*>();
    }
  }

  JsonVariantConst pattern = root["pattern"];
  if (pattern.is<JsonObjectConst>()) {
    JsonVariantConst effect = pattern["effect"];
    Effect e;
    if (effect.is<const char*>() && effectFromName(effect.as<const char*>(), e)) p.pattern.effect = e;

    JsonVariantConst colors = pattern["colors"];
    if (colors.is<JsonArrayConst>()) {
      Rgb parsed[limits::kMaxPatternColors];
      size_t count = 0;
      bool valid = true;
      for (JsonVariantConst c : colors.as<JsonArrayConst>()) {
        if (count >= limits::kMaxPatternColors || !c.is<const char*>() || !parseHexColor(c.as<const char*>(), parsed[count])) {
          valid = false;
          break;
        }
        ++count;
      }
      if (valid && count >= 1) {
        for (size_t i = 0; i < count; ++i) p.pattern.colors[i] = parsed[i];
        p.pattern.colorCount = static_cast<uint8_t>(count);
      }
    }

    JsonVariantConst speed = pattern["speed"];
    if (speed.is<int>() && speed.as<int>() >= limits::kMinSpeed && speed.as<int>() <= limits::kMaxSpeed) {
      p.pattern.speed = static_cast<uint8_t>(speed.as<int>());
    }
  }

  JsonVariantConst brightness = root["brightness"];
  if (brightness.is<int>()) {
    int b = brightness.as<int>();
    if (b < limits::kMinBrightnessPercent) b = limits::kMinBrightnessPercent;
    if (b > limits::kMaxBrightnessPercent) b = limits::kMaxBrightnessPercent;
    p.brightness = static_cast<uint8_t>(b);
  }

  JsonVariantConst muted = root["muted"];
  if (muted.is<bool>()) p.muted = muted.as<bool>();

  JsonVariantConst quiet = root["quiet"];
  if (quiet.is<JsonObjectConst>()) {
    JsonVariantConst enabled = quiet["enabled"];
    if (enabled.is<bool>()) p.quiet.enabled = enabled.as<bool>();
    JsonVariantConst start = quiet["start"];
    JsonVariantConst end = quiet["end"];
    if (start.is<const char*>()) {
      const int m = parseClockTime(start.as<const char*>());
      if (m >= 0) p.quiet.startMinute = m;
    }
    if (end.is<const char*>()) {
      const int m = parseClockTime(end.as<const char*>());
      if (m >= 0) p.quiet.endMinute = m;
    }
  }

  JsonVariantConst tz = root["tz"];
  if (tz.is<JsonObjectConst>()) {
    JsonVariantConst rule = tz["rule"];
    JsonVariantConst tzName = tz["name"];
    if (rule.is<const char*>() && std::string_view(rule.as<const char*>()).size() > 0) {
      p.timeZone.rule = rule.as<const char*>();
      p.timeZone.name = tzName.is<const char*>() ? tzName.as<const char*>() : "custom";
    }
  }
  return p;
}

std::string encodeIdent(const Ident& ident) {
  JsonDocument doc;
  doc["schema"] = kSettingsSchemaVersion;
  doc["password"] = ident.password;
  doc["host"] = ident.brokerHost;
  doc["port"] = ident.brokerPort;
  std::string out;
  serializeJson(doc, out);
  return out;
}

Ident decodeIdent(std::string_view json) {
  Ident ident;
  JsonDocument doc;
  if (json.empty() || deserializeJson(doc, json.data(), json.size())) return ident;
  const JsonDocument& root = doc;
  if (!root.is<JsonObjectConst>()) return ident;
  JsonVariantConst password = root["password"];
  if (password.is<const char*>()) ident.password = password.as<const char*>();
  JsonVariantConst host = root["host"];
  if (host.is<const char*>()) ident.brokerHost = host.as<const char*>();
  JsonVariantConst port = root["port"];
  if (port.is<int>() && port.as<int>() >= 0 && port.as<int>() <= 65535) ident.brokerPort = static_cast<uint16_t>(port.as<int>());
  return ident;
}

}  // namespace yule
