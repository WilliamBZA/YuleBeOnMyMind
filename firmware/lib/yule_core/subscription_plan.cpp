#include "subscription_plan.h"

namespace yule {

std::vector<std::string> desiredSubscriptions(const Topics& topics, std::string_view deviceId,
                                              std::string_view board, const Membership& membership) {
  std::vector<std::string> out;
  out.push_back(topics.config(deviceId));
  out.push_back(topics.manifest(membership.channel, board));
  for (const Group& g : membership.groups) out.push_back(topics.pressAllSenders(g.id));
  return out;
}

SubscriptionChange planMembershipChange(const Topics& topics, std::string_view board,
                                        const Membership& before, const Membership& after) {
  const MembershipDiff diff = diffMembership(before, after);
  SubscriptionChange change;
  for (const std::string& id : diff.subscribe) change.subscribe.push_back(topics.pressAllSenders(id));
  for (const std::string& id : diff.unsubscribe) change.unsubscribe.push_back(topics.pressAllSenders(id));
  if (diff.channelChanged) {
    change.unsubscribe.push_back(topics.manifest(before.channel, board));
    change.subscribe.push_back(topics.manifest(after.channel, board));
  }
  return change;
}

}  // namespace yule
