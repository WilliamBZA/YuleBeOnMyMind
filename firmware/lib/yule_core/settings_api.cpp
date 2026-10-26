#include "settings_api.h"

#include <ArduinoJson.h>

#include "pattern.h"
#include "rtttl.h"
#include "song_library.h"
#include "tz_table.h"
#include "yule_limits.h"

namespace yule {

std::string encodeLibrary() {
  JsonDocument doc;

  JsonArray songs = doc["songs"].to<JsonArray>();
  for (size_t i = 0; i < carolCount(); ++i) {
    const SongInfo& song = carolAt(i);
    JsonObject o = songs.add<JsonObject>();
    o["id"] = song.id;
    o["title"] = song.title;
    const RtttlResult parsed = parseRtttl(song.rtttl);
    const uint32_t total = parsed.ok ? parsed.melody.totalMs() : 0;
    o["ms"] = total < limits::kMelodyMaxMs ? total : limits::kMelodyMaxMs;
  }
  doc["defaultSong"] = defaultCarol().id;

  JsonArray effects = doc["effects"].to<JsonArray>();
  for (size_t i = 0; i < kEffectCount; ++i) {
    const EffectInfo& e = effectInfoAt(i);
    JsonObject o = effects.add<JsonObject>();
    o["name"] = e.name;
    o["label"] = e.label;
    o["colors"] = e.colorsUsed;
    o["roles"] = e.colorRoles;
  }

  JsonArray zones = doc["timeZones"].to<JsonArray>();
  for (size_t i = 0; i < timeZoneCount(); ++i) {
    const TimeZoneInfo& z = timeZoneAt(i);
    JsonObject o = zones.add<JsonObject>();
    o["name"] = z.name;
    o["rule"] = z.rule;
  }

  JsonObject l = doc["limits"].to<JsonObject>();
  l["nameChars"] = limits::kMaxDisplayNameChars;
  l["songChars"] = limits::kMaxSongChars;
  l["songMs"] = limits::kMelodyMaxMs;
  l["colors"] = limits::kMaxPatternColors;
  l["minBrightness"] = limits::kMinBrightnessPercent;
  l["maxBrightness"] = limits::kMaxBrightnessPercent;

  std::string out;
  serializeJson(doc, out);
  return out;
}

const char* updateStateName(UpdateView::State state) {
  switch (state) {
    case UpdateView::State::UpToDate: return "up-to-date";
    case UpdateView::State::Waiting: return "waiting";
    case UpdateView::State::Downloading: return "downloading";
    case UpdateView::State::Verifying: return "verifying";
    case UpdateView::State::Restarting: return "restarting";
    case UpdateView::State::Checking: return "checking";
    case UpdateView::State::Installed: return "installed";
    case UpdateView::State::Failed: return "failed";
    case UpdateView::State::RolledBack: return "rolled-back";
  }
  return "up-to-date";
}

std::string encodeStatus(const StatusView& s) {
  JsonDocument doc;
  doc["id"] = s.deviceId;
  doc["fw"] = s.firmwareVersion;
  doc["board"] = s.board;
  JsonArray groups = doc["groups"].to<JsonArray>();
  for (const std::string& name : s.groupNames) groups.add(name);
  doc["channel"] = s.channel;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = s.wifiSsid;
  wifi["ip"] = s.ip;
  wifi["hostname"] = s.hostname;

  JsonObject broker = doc["broker"].to<JsonObject>();
  broker["state"] = s.broker;
  broker["connected"] = s.brokerConnected;
  broker["host"] = s.brokerHost;
  broker["port"] = s.brokerPort;
  broker["overrideHost"] = s.brokerOverrideHost;
  broker["overridePort"] = s.brokerOverridePort;

  doc["setupRequired"] = s.setupRequired;
  JsonObject clock = doc["clock"].to<JsonObject>();
  clock["synced"] = s.timeSynced;
  clock["local"] = s.timeSynced ? s.localTime : "";
  doc["muted"] = s.muted;
  doc["quietNow"] = s.quietNow;
  doc["uptime"] = s.uptimeS;

  JsonObject update = doc["update"].to<JsonObject>();
  update["state"] = updateStateName(s.update.state);
  if (s.update.state == UpdateView::State::Downloading) update["percent"] = s.update.percent;
  if (!s.update.version.empty()) update["version"] = s.update.version;
  if (!s.update.detail.empty()) update["detail"] = s.update.detail;

  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace yule
