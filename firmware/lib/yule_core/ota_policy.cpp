#include "ota_policy.h"

#include <algorithm>

#include <ArduinoJson.h>

namespace yule {

const char* installDecisionName(InstallDecision d) {
  switch (d) {
    case InstallDecision::Install: return "install";
    case InstallDecision::SkipBoardMismatch: return "wrong-board";
    case InstallDecision::SkipBadVersion: return "bad-version";
    case InstallDecision::SkipNotNewer: return "not-newer";
    case InstallDecision::SkipRolledBack: return "rolled-back";
    case InstallDecision::SkipNotIdle: return "not-idle";
  }
  return "unknown";
}

InstallDecision decideInstall(const OtaManifest& manifest, const InstallContext& ctx) {
  if (manifest.board != ctx.board) return InstallDecision::SkipBoardMismatch;

  SemVer offered, running;
  if (!parseSemVer(manifest.version, offered) || !parseSemVer(ctx.runningVersion, running)) {
    return InstallDecision::SkipBadVersion;
  }
  if (compareSemVer(offered, running) <= 0) return InstallDecision::SkipNotNewer;

  for (const std::string& v : ctx.rolledBackVersions) {
    SemVer rolled;
    if (parseSemVer(v, rolled) ? compareSemVer(rolled, offered) == 0 : v == manifest.version) {
      return InstallDecision::SkipRolledBack;
    }
  }
  if (!ctx.idle) return InstallDecision::SkipNotIdle;
  return InstallDecision::Install;
}

// ---- OtaResult ----

std::string OtaResult::toString() const {
  switch (kind) {
    case Kind::Ok: return "ok";
    case Kind::Failed: return "failed:" + detail;
    case Kind::RolledBack: return "rolled-back:" + detail;
    case Kind::None: break;
  }
  return "none";
}

OtaResult OtaResult::fromString(std::string_view text) {
  OtaResult r;
  if (text == "ok") {
    r.kind = Kind::Ok;
  } else if (text.substr(0, 7) == "failed:") {
    r.kind = Kind::Failed;
    r.detail = std::string(text.substr(7));
  } else if (text.substr(0, 12) == "rolled-back:") {
    r.kind = Kind::RolledBack;
    r.detail = std::string(text.substr(12));
  }
  return r;
}

// ---- OtaState ----

bool OtaState::isRolledBack(std::string_view version) const {
  return std::find(rolledBack.begin(), rolledBack.end(), version) != rolledBack.end();
}

void OtaState::markInstalled() {
  provisional = true;
  bootAttempts = 0;
}

BootAction OtaState::onBoot(const std::string& runningVersion) {
  if (!provisional) return BootAction::Continue;
  if (bootAttempts < 255) ++bootAttempts;
  if (bootAttempts > limits::kMaxProvisionalBoots) {
    recordRollback(runningVersion);
    return BootAction::Revert;
  }
  return BootAction::Continue;
}

void OtaState::markHealthy() {
  provisional = false;
  bootAttempts = 0;
  lastResult.kind = OtaResult::Kind::Ok;
  lastResult.detail.clear();
}

void OtaState::recordRollback(const std::string& version) {
  provisional = false;
  bootAttempts = 0;
  if (!isRolledBack(version)) rolledBack.push_back(version);
  while (rolledBack.size() > limits::kRolledBackListSize) rolledBack.erase(rolledBack.begin());
  lastResult.kind = OtaResult::Kind::RolledBack;
  lastResult.detail = version;
}

void OtaState::recordFailure(const std::string& reason) {
  lastResult.kind = OtaResult::Kind::Failed;
  lastResult.detail = reason;
}

std::string OtaState::toJson() const {
  JsonDocument doc;
  doc["prov"] = provisional;
  doc["att"] = bootAttempts;
  JsonArray rb = doc["rb"].to<JsonArray>();
  for (const std::string& v : rolledBack) rb.add(v);
  doc["last"] = lastResult.toString();
  std::string out;
  serializeJson(doc, out);
  return out;
}

OtaState OtaState::fromJson(std::string_view json) {
  OtaState s;
  JsonDocument doc;
  if (json.empty() || deserializeJson(doc, json.data(), json.size())) return s;
  const JsonDocument& root = doc;
  if (!root.is<JsonObjectConst>()) return s;
  if (root["prov"].is<bool>()) s.provisional = root["prov"].as<bool>();
  if (root["att"].is<int>()) {
    const int a = root["att"].as<int>();
    s.bootAttempts = static_cast<uint8_t>(a < 0 ? 0 : (a > 255 ? 255 : a));
  }
  if (root["rb"].is<JsonArrayConst>()) {
    for (JsonVariantConst v : root["rb"].as<JsonArrayConst>()) {
      if (v.is<const char*>() && s.rolledBack.size() < limits::kRolledBackListSize) s.rolledBack.push_back(v.as<const char*>());
    }
  }
  if (root["last"].is<const char*>()) s.lastResult = OtaResult::fromString(root["last"].as<const char*>());
  return s;
}

// ---- HealthMonitor ----

void HealthMonitor::onWifi(bool up, uint32_t nowMs) {
  if (up == wifiUp_) return;
  if (up) {
    wifiEverUp_ = true;
    wifiUpSinceMs_ = nowMs;
  } else {
    wifiUpAccumMs_ += nowMs - wifiUpSinceMs_;
  }
  wifiUp_ = up;
}

void HealthMonitor::onBrokerConnected() { brokerConnected_ = true; }

HealthMonitor::Verdict HealthMonitor::update(uint32_t nowMs) {
  const bool aliveLongEnough = nowMs >= limits::kHealthyUptimeMs;
  if (!wifiEverUp_) return aliveLongEnough ? Verdict::Healthy : Verdict::Pending;  // no Wi-Fi: nothing more to test
  if (brokerConnected_) return aliveLongEnough ? Verdict::Healthy : Verdict::Pending;
  const uint32_t wifiTotalMs = wifiUpAccumMs_ + (wifiUp_ ? nowMs - wifiUpSinceMs_ : 0);
  if (wifiTotalMs >= limits::kBrokerGraceAfterWifiMs) return Verdict::Revert;
  return Verdict::Pending;
}

}  // namespace yule
