// Serial log (design D12): one line per event, "[  12.345] [tag] message", at 115200 baud.
#pragma once

#include <stdarg.h>

// `tag` names the subsystem: boot, wifi, time, mqtt, member, setup, ...
void yuleLog(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
