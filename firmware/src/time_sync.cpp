#include "time_sync.h"

#include <time.h>

#include "connection_policy.h"

void TimeSync::begin(const std::string& tzRule) { setTimeZone(tzRule); }

void TimeSync::setTimeZone(const std::string& tzRule) {
  rule_ = tzRule.empty() ? "UTC0" : tzRule;
  setenv("TZ", rule_.c_str(), 1);
  tzset();
}

void TimeSync::onWifiUp() {
  // configTzTime keeps pointers to the server names, so they must be string literals. It also (re)applies
  // the TZ rule. SNTP then refreshes the clock hourly.
  configTzTime(rule_.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

bool TimeSync::synced() const { return yule::isClockSynced(static_cast<int64_t>(time(nullptr))); }

int64_t TimeSync::epochSeconds() const {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  return yule::isClockSynced(now) ? now : 0;
}

std::string TimeSync::localTimeString() const {
  const time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  char buf[48];
  strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S %Z", &local);
  return buf;
}
