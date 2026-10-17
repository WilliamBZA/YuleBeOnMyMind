// Which MQTT topics the device subscribes to, derived from its membership (family-groups). Hardware
// independent; src/app turns the lists into subscribe/unsubscribe calls on the broker link.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "membership_config.h"
#include "topics.h"

namespace yule {

struct SubscriptionChange {
  std::vector<std::string> subscribe;
  std::vector<std::string> unsubscribe;
  bool empty() const { return subscribe.empty() && unsubscribe.empty(); }
};

// Everything the device listens to while connected: its own config topic, the firmware manifest for its
// channel and board, then the press topic of every group (`.../press/+`). Subscribing again to a topic
// already subscribed is harmless, so this whole list is used after every (re)connect.
std::vector<std::string> desiredSubscriptions(const Topics& topics, std::string_view deviceId,
                                              std::string_view board, const Membership& membership);

// What to change when a new membership arrives: subscribe to the press topics of added groups, unsubscribe
// from removed ones, and swap the manifest topic when the channel changed. A rename alone changes nothing.
SubscriptionChange planMembershipChange(const Topics& topics, std::string_view board,
                                        const Membership& before, const Membership& after);

}  // namespace yule
