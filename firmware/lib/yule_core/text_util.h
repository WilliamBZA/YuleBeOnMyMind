// Small string helpers shared by the codecs and validators. Hardware independent.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace yule {

// Removes ASCII whitespace from both ends.
std::string trimWhitespace(std::string_view s);

// Number of Unicode code points in a UTF-8 string (counts non-continuation bytes). Invalid UTF-8 never crashes.
size_t utf8Length(std::string_view s);

// Cuts to at most maxBytes without splitting a UTF-8 sequence.
std::string truncateUtf8(std::string_view s, size_t maxBytes);

}  // namespace yule
