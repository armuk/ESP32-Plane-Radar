#include "services/request_origin.h"

#include <cctype>
#include <cstddef>
#include <cstring>
#include <strings.h>  // strncasecmp

namespace services::request_origin {
namespace {

bool equalsIgnoreCase(const char* a, size_t a_len, const char* b) {
  if (strlen(b) != a_len) {
    return false;
  }
  for (size_t i = 0; i < a_len; ++i) {
    if (tolower(static_cast<unsigned char>(a[i])) !=
        tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

/** "http://host[:port][/path...]" -> host[:port] matches host. */
bool sourceMatchesHost(const char* source, const char* host) {
  static const char kScheme[] = "http://";
  constexpr size_t kSchemeLen = sizeof(kScheme) - 1;
  if (strncasecmp(source, kScheme, kSchemeLen) != 0) {
    return false;
  }
  const char* authority = source + kSchemeLen;
  size_t len = strcspn(authority, "/?#");
  // Browsers never send user:password@ in these headers; refuse it rather
  // than parse it.
  if (memchr(authority, '@', len) != nullptr) {
    return false;
  }
  // Default port: "host:80" and "host" are the same origin.
  if (len > 3 && strncmp(authority + len - 3, ":80", 3) == 0) {
    len -= 3;
  }
  const size_t host_len = strlen(host);
  if (host_len > 3 && strcmp(host + host_len - 3, ":80") == 0) {
    char trimmed[256];
    if (host_len - 3 >= sizeof(trimmed)) {
      return false;
    }
    memcpy(trimmed, host, host_len - 3);
    trimmed[host_len - 3] = '\0';
    return equalsIgnoreCase(authority, len, trimmed);
  }
  return equalsIgnoreCase(authority, len, host);
}

}  // namespace

bool sameOrigin(const char* origin, const char* referer, const char* host) {
  if (host == nullptr || host[0] == '\0') {
    return false;
  }
  // Origin wins when present; "null" means the page hid where it came from.
  if (origin != nullptr && origin[0] != '\0') {
    return sourceMatchesHost(origin, host);
  }
  if (referer != nullptr && referer[0] != '\0') {
    return sourceMatchesHost(referer, host);
  }
  return false;
}

}  // namespace services::request_origin
