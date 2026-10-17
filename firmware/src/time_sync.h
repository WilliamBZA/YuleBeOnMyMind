// Wall-clock time from SNTP, in the owner's time zone (device-provisioning, do-not-disturb).
#pragma once

#include <Arduino.h>

#include <string>

class TimeSync {
 public:
  // Applies the time zone (a POSIX TZ rule such as "NZST-12NZDT,M9.5.0,M4.1.0/3"). SNTP starts when
  // Wi-Fi comes up (see onWifiUp).
  void begin(const std::string& tzRule);

  // The owner changed their zone.
  void setTimeZone(const std::string& tzRule);

  // Call each time Wi-Fi connects: (re)starts SNTP.
  void onWifiUp();

  // True once the clock holds a real date, not the 1970 an unsynchronised ESP32 reports.
  bool synced() const;

  // UTC seconds since 1970, or 0 while the clock is not synchronised (what the receive filters expect).
  int64_t epochSeconds() const;

  // "Sat 2026-10-17 15:42:10 NZDT" for the serial log.
  std::string localTimeString() const;

  const std::string& rule() const { return rule_; }

 private:
  std::string rule_ = "UTC0";
};
