#include "send_tracker.h"

#include <algorithm>

namespace yule {

void SendTracker::begin(uint32_t nowMs, const std::vector<int>& copyIds) {
  outstanding_ = copyIds;
  startMs_ = nowMs;
  state_ = copyIds.empty() ? State::Failed : State::Pending;
}

void SendTracker::failNow() {
  outstanding_.clear();
  state_ = State::Failed;
}

SendTracker::State SendTracker::update(uint32_t nowMs) {
  if (state_ == State::Pending && nowMs - startMs_ > timeoutMs_) {
    outstanding_.clear();
    state_ = State::Failed;
  }
  return state_;
}

void SendTracker::onAck(int messageId, uint32_t nowMs) {
  if (update(nowMs) != State::Pending) return;  // also applies the timeout, so a late ack counts for nothing
  auto it = std::find(outstanding_.begin(), outstanding_.end(), messageId);
  if (it == outstanding_.end()) return;
  outstanding_.erase(it);
  if (outstanding_.empty()) state_ = State::Succeeded;
}

bool SendTracker::takeResult(bool& success) {
  if (state_ != State::Succeeded && state_ != State::Failed) return false;
  success = state_ == State::Succeeded;
  state_ = State::Idle;
  return true;
}

}  // namespace yule
