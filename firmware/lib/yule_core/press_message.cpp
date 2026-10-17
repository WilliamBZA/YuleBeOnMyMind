#include "press_message.h"

#include <ArduinoJson.h>

#include "device_id.h"
#include "rtttl.h"
#include "song_library.h"
#include "text_util.h"
#include "yule_limits.h"

namespace yule {

namespace {

constexpr size_t kMaxPayloadBytes = 2048;
constexpr size_t kMaxNameBytes = 64;

bool isAlnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::string normaliseSong(JsonVariantConst song) {
  if (!song.is<const char*>()) return kDefaultChime;
  const std::string text = song.as<const char*>();
  if (text.empty() || text.size() > limits::kMaxSongChars) return kDefaultChime;
  if (!parseRtttl(text).ok) return kDefaultChime;
  return text;
}

Pattern normalisePattern(JsonVariantConst pattern) {
  Pattern p = defaultPattern();
  if (!pattern.is<JsonObjectConst>()) return p;

  // Unknown or missing effect: breathe, keeping the supplied colours.
  JsonVariantConst effect = pattern["effect"];
  Effect e = Effect::Breathe;
  if (effect.is<const char*>()) effectFromName(effect.as<const char*>(), e);
  p.effect = e;

  JsonVariantConst colors = pattern["colors"];
  if (colors.is<JsonArrayConst>() && colors.as<JsonArrayConst>().size() > 0) {
    size_t count = 0;
    for (JsonVariantConst c : colors.as<JsonArrayConst>()) {
      if (count >= limits::kMaxPatternColors) break;
      Rgb rgb = kWarmWhite;
      if (c.is<const char*>()) {
        if (!parseHexColor(c.as<const char*>(), rgb)) rgb = kWarmWhite;
      }
      p.colors[count++] = rgb;
    }
    p.colorCount = static_cast<uint8_t>(count);
  }  // otherwise: the default single warm-white colour

  JsonVariantConst speed = pattern["speed"];
  if (speed.is<int>()) {
    const int s = speed.as<int>();
    if (s >= limits::kMinSpeed && s <= limits::kMaxSpeed) p.speed = static_cast<uint8_t>(s);
  }
  return p;
}

}  // namespace

bool isValidMessageId(std::string_view id) {
  if (id.empty() || id.size() > 32) return false;
  for (char c : id) {
    if (!isAlnum(c)) return false;
  }
  return true;
}

std::string encodePress(const PressMessage& m) {
  JsonDocument doc;
  doc["v"] = limits::kProtocolVersion;
  doc["id"] = m.id;
  doc["from"] = m.from;
  doc["name"] = m.name;
  if (m.ts > 0) doc["ts"] = m.ts;
  doc["song"] = m.song;
  JsonObject pattern = doc["pattern"].to<JsonObject>();
  pattern["effect"] = effectName(m.pattern.effect);
  JsonArray colors = pattern["colors"].to<JsonArray>();
  const size_t count = m.pattern.colorCount > limits::kMaxPatternColors ? limits::kMaxPatternColors : m.pattern.colorCount;
  for (size_t i = 0; i < count; ++i) colors.add(formatHexColor(m.pattern.colors[i]));
  pattern["speed"] = m.pattern.speed;
  std::string out;
  serializeJson(doc, out);
  return out;
}

PressDecodeResult decodePress(std::string_view payload) {
  PressDecodeResult result;
  if (payload.empty() || payload.size() > kMaxPayloadBytes) return result;

  JsonDocument doc;
  if (deserializeJson(doc, payload.data(), payload.size())) return result;
  const JsonDocument& root = doc;
  if (!root.is<JsonObjectConst>()) return result;

  JsonVariantConst from = root["from"];
  if (!from.is<const char*>() || !isValidDeviceId(from.as<const char*>())) {
    result.status = PressDecodeStatus::MissingFrom;
    return result;
  }
  JsonVariantConst id = root["id"];
  if (!id.is<const char*>() || !isValidMessageId(id.as<const char*>())) {
    result.status = PressDecodeStatus::MissingId;
    return result;
  }

  PressMessage& m = result.message;
  m.from = from.as<const char*>();
  m.id = id.as<const char*>();

  JsonVariantConst version = root["v"];
  m.version = version.is<int>() ? version.as<int>() : limits::kProtocolVersion;

  JsonVariantConst name = root["name"];
  m.name = name.is<const char*>() ? truncateUtf8(trimWhitespace(name.as<const char*>()), kMaxNameBytes) : std::string();

  JsonVariantConst ts = root["ts"];
  if (ts.is<int64_t>() || ts.is<double>()) {
    const int64_t t = ts.as<int64_t>();
    m.ts = t > 0 ? t : 0;
  }

  m.song = normaliseSong(root["song"]);
  m.pattern = normalisePattern(root["pattern"]);
  result.status = PressDecodeStatus::Ok;
  return result;
}

}  // namespace yule
