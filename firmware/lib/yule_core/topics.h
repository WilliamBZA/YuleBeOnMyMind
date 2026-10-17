// MQTT topic layout (design D4) and the receive filters that decide whether an incoming press is played.
// Hardware independent.
//
//   <root>/groups/<groupId>/press/<deviceId>     press, written by the sending device
//   <root>/devices/<deviceId>/config             retained, written by the server only
//   <root>/devices/<deviceId>/status             retained, written by the device
//   <root>/firmware/<channel>/<board>            retained manifest, written by the server only
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

#include "membership_config.h"
#include "press_message.h"

namespace yule {

class Topics {
 public:
  explicit Topics(std::string root = "yule/v1") : root_(std::move(root)) {}

  const std::string& root() const { return root_; }

  std::string press(std::string_view group, std::string_view device) const;
  std::string pressAllSenders(std::string_view group) const;  // subscription: .../press/+
  std::string config(std::string_view device) const;
  std::string status(std::string_view device) const;
  std::string manifest(std::string_view channel, std::string_view board) const;

  struct PressTopic {
    std::string group;
    std::string sender;
  };
  // Parses "<root>/groups/<g>/press/<d>". Fails for anything else, including wildcards and bad ids.
  bool parsePress(std::string_view topic, PressTopic& out) const;

 private:
  std::string root_;
};

// Why a received press was not played. Logged on the serial console.
enum class DropReason {
  None,
  BadTopic,    // topic is not groups/<g>/press/<d>
  NotMember,   // group is not in the current membership
  Malformed,   // payload is not a valid press
  Forged,      // payload's sender differs from the sender in the topic
  Own,         // our own press echoed back
  Duplicate,   // message id already seen (also collapses copies arriving through several groups)
  Stale,       // sent more than 5 minutes before it arrived
};

const char* dropReasonName(DropReason reason);

// Remembers the last N message ids (default 32), oldest forgotten first.
class RecentIds {
 public:
  explicit RecentIds(size_t capacity = 32) : capacity_(capacity) {}
  bool contains(const std::string& id) const;
  void add(const std::string& id);
  size_t size() const { return ids_.size(); }

 private:
  size_t capacity_;
  std::deque<std::string> ids_;
};

// Runs the receive pipeline in the order of design D4:
//   topic parses -> group is a member -> payload parses and `from` matches the topic sender ->
//   not our own -> id not seen in the last 32 -> not stale.
class PressReceiver {
 public:
  PressReceiver(Topics topics, std::string ownDeviceId);

  struct Result {
    DropReason drop = DropReason::None;
    PressMessage message;  // valid when drop == None
    std::string group;     // the group it arrived through
    bool accepted() const { return drop == DropReason::None; }
  };

  // nowEpochSeconds is 0 when the clock is not synchronised; staleness is then not judged.
  Result receive(std::string_view topic, std::string_view payload, const Membership& membership,
                 int64_t nowEpochSeconds);

  const std::string& ownId() const { return ownId_; }

 private:
  Topics topics_;
  std::string ownId_;
  RecentIds recent_;
};

}  // namespace yule
