#include "topics.h"

#include <algorithm>

#include "device_id.h"
#include "yule_limits.h"

namespace yule {

namespace {

std::string join(const std::string& root, std::initializer_list<std::string_view> parts) {
  std::string s = root;
  for (std::string_view p : parts) {
    s += '/';
    s += p;
  }
  return s;
}

}  // namespace

std::string Topics::press(std::string_view group, std::string_view device) const {
  return join(root_, {"groups", group, "press", device});
}

std::string Topics::pressAllSenders(std::string_view group) const { return join(root_, {"groups", group, "press", "+"}); }

std::string Topics::config(std::string_view device) const { return join(root_, {"devices", device, "config"}); }

std::string Topics::status(std::string_view device) const { return join(root_, {"devices", device, "status"}); }

std::string Topics::manifest(std::string_view channel, std::string_view board) const {
  return join(root_, {"firmware", channel, board});
}

bool Topics::parsePress(std::string_view topic, PressTopic& out) const {
  if (topic.size() <= root_.size() + 1 || topic.substr(0, root_.size()) != root_ || topic[root_.size()] != '/') return false;
  std::string_view rest = topic.substr(root_.size() + 1);

  std::string_view parts[4];
  for (int i = 0; i < 4; ++i) {
    const size_t slash = rest.find('/');
    if (i < 3) {
      if (slash == std::string_view::npos) return false;
      parts[i] = rest.substr(0, slash);
      rest = rest.substr(slash + 1);
    } else {
      if (slash != std::string_view::npos) return false;  // no extra levels
      parts[i] = rest;
    }
  }
  if (parts[0] != "groups" || parts[2] != "press") return false;
  if (!isValidGroupId(parts[1]) || !isValidDeviceId(parts[3])) return false;
  out.group = std::string(parts[1]);
  out.sender = std::string(parts[3]);
  return true;
}

const char* dropReasonName(DropReason reason) {
  switch (reason) {
    case DropReason::None: return "none";
    case DropReason::BadTopic: return "bad-topic";
    case DropReason::NotMember: return "not-a-member";
    case DropReason::Malformed: return "malformed";
    case DropReason::Forged: return "forged";
    case DropReason::Own: return "own";
    case DropReason::Duplicate: return "duplicate";
    case DropReason::Stale: return "stale";
  }
  return "unknown";
}

bool RecentIds::contains(const std::string& id) const { return std::find(ids_.begin(), ids_.end(), id) != ids_.end(); }

void RecentIds::add(const std::string& id) {
  if (capacity_ == 0) return;
  ids_.push_back(id);
  while (ids_.size() > capacity_) ids_.pop_front();
}

PressReceiver::PressReceiver(Topics topics, std::string ownDeviceId)
    : topics_(std::move(topics)), ownId_(std::move(ownDeviceId)), recent_(limits::kRecentIdRing) {}

PressReceiver::Result PressReceiver::receive(std::string_view topic, std::string_view payload, const Membership& membership,
                                             int64_t nowEpochSeconds) {
  Result r;

  Topics::PressTopic parsed;
  if (!topics_.parsePress(topic, parsed)) {
    r.drop = DropReason::BadTopic;
    return r;
  }
  if (!membership.hasGroup(parsed.group)) {
    r.drop = DropReason::NotMember;
    return r;
  }

  PressDecodeResult decoded = decodePress(payload);
  if (decoded.status != PressDecodeStatus::Ok) {
    r.drop = DropReason::Malformed;
    return r;
  }
  if (decoded.message.from != parsed.sender) {
    r.drop = DropReason::Forged;
    return r;
  }
  if (decoded.message.from == ownId_) {
    r.drop = DropReason::Own;
    return r;
  }
  if (recent_.contains(decoded.message.id)) {
    r.drop = DropReason::Duplicate;
    return r;
  }
  // Staleness is only judged when both times are known. A negative age (sender's clock ahead) counts as fresh.
  if (nowEpochSeconds > 0 && decoded.message.ts > 0 &&
      nowEpochSeconds - decoded.message.ts > limits::kStaleAfterSeconds) {
    r.drop = DropReason::Stale;
    return r;
  }

  recent_.add(decoded.message.id);
  r.message = std::move(decoded.message);
  r.group = parsed.group;
  return r;
}

}  // namespace yule
