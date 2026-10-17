#include "ota_manifest.h"

#include <ArduinoJson.h>

namespace yule {

namespace {

constexpr size_t kMaxPayloadBytes = 2048;
constexpr size_t kMaxUrlChars = 256;
constexpr size_t kMaxSignatureChars = 128;

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool parsePart(std::string_view s, int& out) {
  if (s.empty() || s.size() > 4) return false;
  int v = 0;
  for (char c : s) {
    if (!isDigit(c)) return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

bool isHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool isBase64(std::string_view s) {
  if (s.empty() || s.size() % 4 != 0) return false;
  size_t pad = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '=') {
      if (i + 2 < s.size()) return false;  // padding only at the very end
      ++pad;
    } else if (pad > 0) {
      return false;
    } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/')) {
      return false;
    }
  }
  return pad <= 2;
}

bool isBoardName(std::string_view s) {
  if (s.empty() || s.size() > 32) return false;
  for (char c : s) {
    if (!((c >= 'a' && c <= 'z') || isDigit(c) || c == '-')) return false;
  }
  return true;
}

bool isHttpsUrl(std::string_view url) {
  constexpr std::string_view kScheme = "https://";
  if (url.size() <= kScheme.size() || url.size() > kMaxUrlChars) return false;
  if (url.substr(0, kScheme.size()) != kScheme) return false;
  for (char c : url) {
    if (c <= 0x20 || c >= 0x7f) return false;  // no spaces, control characters or non-ASCII
  }
  return url[kScheme.size()] != '/';  // a host must follow
}

ManifestResult reject(const std::string& why) {
  ManifestResult r;
  r.error = why;
  return r;
}

}  // namespace

bool parseSemVer(std::string_view text, SemVer& out) {
  const size_t dot1 = text.find('.');
  if (dot1 == std::string_view::npos) return false;
  const size_t dot2 = text.find('.', dot1 + 1);
  if (dot2 == std::string_view::npos) return false;
  SemVer v;
  if (!parsePart(text.substr(0, dot1), v.major) || !parsePart(text.substr(dot1 + 1, dot2 - dot1 - 1), v.minor) ||
      !parsePart(text.substr(dot2 + 1), v.patch)) {
    return false;
  }
  out = v;
  return true;
}

int compareSemVer(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

ManifestResult decodeManifest(std::string_view payload) {
  if (payload.empty() || payload.size() > kMaxPayloadBytes) return reject("manifest empty or too large");
  JsonDocument doc;
  if (deserializeJson(doc, payload.data(), payload.size())) return reject("manifest is not valid JSON");
  const JsonDocument& root = doc;
  if (!root.is<JsonObjectConst>()) return reject("manifest is not a JSON object");

  OtaManifest m;
  JsonVariantConst v = root["v"];
  if (!v.is<int>() || v.as<int>() < 1) return reject("manifest version (v) missing");
  m.v = v.as<int>();

  JsonVariantConst version = root["version"];
  SemVer sv;
  if (!version.is<const char*>() || !parseSemVer(version.as<const char*>(), sv)) return reject("version missing or not MAJOR.MINOR.PATCH");
  m.version = version.as<const char*>();

  JsonVariantConst board = root["board"];
  if (!board.is<const char*>() || !isBoardName(board.as<const char*>())) return reject("board missing or invalid");
  m.board = board.as<const char*>();

  JsonVariantConst size = root["size"];
  if (!size.is<uint32_t>() || size.as<uint32_t>() == 0) return reject("size missing or not a positive number");
  m.size = size.as<uint32_t>();

  JsonVariantConst sha = root["sha256"];
  if (!sha.is<const char*>()) return reject("sha256 missing");
  const std::string_view shaText = sha.as<const char*>();
  if (shaText.size() != 64) return reject("sha256 must be 64 hex digits");
  for (char c : shaText) {
    if (!isHex(c)) return reject("sha256 must be 64 hex digits");
    m.sha256.push_back(c >= 'A' && c <= 'F' ? static_cast<char>(c - 'A' + 'a') : c);
  }

  JsonVariantConst sig = root["sig"];
  if (!sig.is<const char*>()) return reject("sig missing");
  const std::string_view sigText = sig.as<const char*>();
  if (sigText.size() > kMaxSignatureChars || !isBase64(sigText)) return reject("sig is not valid base64");
  m.signature = std::string(sigText);

  JsonVariantConst url = root["url"];
  if (!url.is<const char*>()) return reject("url missing");
  if (!isHttpsUrl(url.as<const char*>())) return reject("url must be an https:// address");
  m.url = url.as<const char*>();

  ManifestResult r;
  r.ok = true;
  r.manifest = std::move(m);
  return r;
}

}  // namespace yule
