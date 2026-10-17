// Tracks one send of a press to every group: one MQTT publish per group, all of which must be acknowledged
// (PUBACK) within 5 s for the send to count as successful. Hardware independent.
#pragma once

#include <cstdint>
#include <vector>

#include "yule_limits.h"

namespace yule {

class SendTracker {
 public:
  enum class State { Idle, Pending, Succeeded, Failed };

  explicit SendTracker(uint32_t timeoutMs = limits::kAckTimeoutMs) : timeoutMs_(timeoutMs) {}

  // Starts tracking a send whose copies were published with these MQTT message ids. An empty list (no
  // groups) fails at once.
  void begin(uint32_t nowMs, const std::vector<int>& copyIds);

  // The send failed before it could be published (not connected, publish error).
  void failNow();

  // A PUBACK arrived. Ignored if the send has already finished or timed out, or if the id is unknown.
  void onAck(int messageId, uint32_t nowMs);

  // Applies the timeout. Call regularly.
  State update(uint32_t nowMs);

  State state() const { return state_; }
  bool pending() const { return state_ == State::Pending; }
  size_t outstanding() const { return outstanding_.size(); }

  // Once the send has finished, returns the result a single time and returns to Idle.
  // Returns false (leaving `success` untouched) while Idle or still Pending.
  bool takeResult(bool& success);

 private:
  uint32_t timeoutMs_;
  State state_ = State::Idle;
  uint32_t startMs_ = 0;
  std::vector<int> outstanding_;
};

}  // namespace yule
