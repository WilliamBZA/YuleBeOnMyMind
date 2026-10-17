// Group membership and update channel, as pushed by the server in the retained device-config message.
// Hardware independent. Wire format (design D4): {"v":1,"groups":[{"id":"g-7f3k2abc","name":"..."}],"channel":"stable"}
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace yule {

struct Group {
  std::string id;    // opaque, "g-" plus 8 characters from [a-z0-9]
  std::string name;  // 1-40 characters
  bool operator==(const Group& o) const { return id == o.id && name == o.name; }
};

struct Membership {
  std::vector<Group> groups;
  std::string channel = "stable";  // "stable" or "beta"

  bool hasGroup(std::string_view id) const;
  // Names of the groups, for the settings page.
  std::vector<std::string> groupNames() const;
  bool operator==(const Membership& o) const { return groups == o.groups && channel == o.channel; }
};

bool isValidGroupId(std::string_view id);
bool isValidChannel(std::string_view channel);

struct ConfigResult {
  bool ok = false;
  Membership membership;
  std::string error;  // why it was rejected, for the serial log
};

// Validates and decodes a config payload. Unknown fields are ignored. Any invalid entry rejects the whole
// message, so the caller keeps its previous membership. Duplicate group ids keep the first entry.
ConfigResult decodeDeviceConfig(std::string_view payload);

// Encodes in the same format; used to persist the last-known membership in NVS.
std::string encodeMembership(const Membership& membership);

struct MembershipDiff {
  std::vector<std::string> subscribe;    // group ids to subscribe to
  std::vector<std::string> unsubscribe;  // group ids to unsubscribe from
  bool channelChanged = false;           // re-subscribe the firmware manifest topic

  bool empty() const { return subscribe.empty() && unsubscribe.empty() && !channelChanged; }
};

MembershipDiff diffMembership(const Membership& before, const Membership& after);

}  // namespace yule
