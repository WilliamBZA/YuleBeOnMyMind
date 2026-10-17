#include "text_util.h"

namespace yule {

namespace {
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }
}  // namespace

std::string trimWhitespace(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && isSpace(s[a])) ++a;
  while (b > a && isSpace(s[b - 1])) --b;
  return std::string(s.substr(a, b - a));
}

size_t utf8Length(std::string_view s) {
  size_t n = 0;
  for (char c : s) {
    if (!isContinuation(c)) ++n;
  }
  return n;
}

std::string truncateUtf8(std::string_view s, size_t maxBytes) {
  if (s.size() <= maxBytes) return std::string(s);
  size_t cut = maxBytes;
  while (cut > 0 && isContinuation(s[cut])) --cut;
  return std::string(s.substr(0, cut));
}

}  // namespace yule
