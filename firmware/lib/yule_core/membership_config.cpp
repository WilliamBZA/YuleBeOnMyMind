#include "membership_config.h"

#include <ArduinoJson.h>

#include "text_util.h"
#include "yule_limits.h"

namespace yule {

namespace {
constexpr size_t kMaxPayloadBytes = 4096;
}

bool Membership::hasGroup(std::string_view id) const {
  for (const Group& g : groups) {
    if (id == g.id) return true;
  }
  return false;
}

std::vector<std::string> Membership::groupNames() const {
  std::vector<std::string> names;
  names.reserve(groups.size());
  for (const Group& g : groups) names.push_back(g.name);
  return names;
}

bool isValidGroupId(std::string_view id) {
  if (id.size() != 10 || id[0] != 'g' || id[1] != '-') return false;
  for (size_t i = 2; i < id.size(); ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

bool isValidChannel(std::string_view channel) { return channel == "stable" || channel == "beta"; }

namespace {
ConfigResult reject(const std::string& why) {
  ConfigResult r;
  r.error = why;
  return r;
}
}  // namespace

ConfigResult decodeDeviceConfig(std::string_view payload) {
  if (payload.empty() || payload.size() > kMaxPayloadBytes) return reject("payload empty or too large");
  JsonDocument doc;
  if (deserializeJson(doc, payload.data(), payload.size())) return reject("not valid JSON");
  if (!doc.is<JsonObjectConst>()) return reject("not a JSON object");

  JsonVariantConst version = doc["v"];
  if (!version.isNull() && (!version.is<int>() || version.as<int>() < 1)) return reject("bad version");

  JsonVariantConst groups = doc["groups"];
  if (!groups.is<JsonArrayConst>()) return reject("groups missing or not a list");
  if (groups.as<JsonArrayConst>().size() > limits::kMaxGroups) return reject("too many groups");

  ConfigResult result;
  for (JsonVariantConst entry : groups.as<JsonArrayConst>()) {
    if (!entry.is<JsonObjectConst>()) return reject("group entry is not an object");
    JsonVariantConst id = entry["id"];
    JsonVariantConst name = entry["name"];
    if (!id.is<const char*>() || !name.is<const char*>()) return reject("group entry needs id and name");
    const std::string groupId = id.as<const char*>();
    const std::string groupName = name.as<const char*>();
    if (!isValidGroupId(groupId)) return reject("invalid group id");
    const size_t chars = utf8Length(groupName);
    if (chars < 1 || chars > limits::kMaxGroupNameChars) return reject("group name must be 1-40 characters");
    if (!result.membership.hasGroup(groupId)) result.membership.groups.push_back({groupId, groupName});
  }

  JsonVariantConst channel = doc["channel"];
  if (!channel.isNull()) {
    if (!channel.is<const char*>() || !isValidChannel(channel.as<const char*>())) return reject("invalid channel");
    result.membership.channel = channel.as<const char*>();
  }
  result.ok = true;
  return result;
}

std::string encodeMembership(const Membership& membership) {
  JsonDocument doc;
  doc["v"] = limits::kProtocolVersion;
  JsonArray groups = doc["groups"].to<JsonArray>();
  for (const Group& g : membership.groups) {
    JsonObject o = groups.add<JsonObject>();
    o["id"] = g.id;
    o["name"] = g.name;
  }
  doc["channel"] = membership.channel;
  std::string out;
  serializeJson(doc, out);
  return out;
}

MembershipDiff diffMembership(const Membership& before, const Membership& after) {
  MembershipDiff diff;
  for (const Group& g : after.groups) {
    if (!before.hasGroup(g.id)) diff.subscribe.push_back(g.id);
  }
  for (const Group& g : before.groups) {
    if (!after.hasGroup(g.id)) diff.unsubscribe.push_back(g.id);
  }
  diff.channelChanged = before.channel != after.channel;
  return diff;
}

}  // namespace yule
