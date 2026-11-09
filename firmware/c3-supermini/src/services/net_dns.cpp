#include "services/net_dns.h"

#include <cstdint>
#include <cstring>
#include <strings.h>  // strcasecmp

#ifdef ARDUINO
#include <Arduino.h>

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/priv/tcpip_priv.h"
#include "lwip/tcpip.h"
#endif

namespace services::net {

bool hostFromUrl(const char* url, char* out, size_t out_len) {
  if (url == nullptr || out == nullptr || out_len == 0) {
    return false;
  }
  const char* start = strstr(url, "://");
  start = start != nullptr ? start + 3 : url;
  // Skip user:password@ if present (not used by this firmware's URLs).
  const size_t authority_len = strcspn(start, "/?#");
  const char* at = static_cast<const char*>(memchr(start, '@', authority_len));
  if (at != nullptr) {
    start = at + 1;
  }
  size_t len = strcspn(start, ":/?#");
  if (len == 0 || len >= out_len) {
    return false;
  }
  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

#ifdef ARDUINO
namespace {

// The lwIP callback can arrive after we stopped waiting; the generation
// number makes a late answer for an older lookup harmless.
volatile uint32_t s_generation = 0;
volatile int s_result = 0;  // 0 waiting, 1 found, -1 failed
unsigned long s_skipped = 0;

void onDnsFound(const char* /*name*/, const ip_addr_t* addr, void* arg) {
  if (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)) != s_generation) {
    return;
  }
  s_result = addr != nullptr ? 1 : -1;
}

bool endsWithLocal(const char* host) {
  const size_t len = strlen(host);
  return len > 6 && strcasecmp(host + len - 6, ".local") == 0;
}

}  // namespace

bool resolveQuickly(const char* url, unsigned long timeout_ms, PollFn poll) {
  char host[64];
  if (!hostFromUrl(url, host, sizeof(host))) {
    return true;  // let the request report its own error
  }
  ip_addr_t literal;
  if (ipaddr_aton(host, &literal) || endsWithLocal(host)) {
    return true;
  }

  const uint32_t generation = s_generation + 1;
  s_generation = generation;
  s_result = 0;
  ip_addr_t addr;
#if CONFIG_LWIP_TCPIP_CORE_LOCKING
  const bool take_lock = !sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER);
  if (take_lock) {
    LOCK_TCPIP_CORE();
  }
#endif
  const err_t err = dns_gethostbyname(
      host, &addr, onDnsFound,
      reinterpret_cast<void*>(static_cast<uintptr_t>(generation)));
#if CONFIG_LWIP_TCPIP_CORE_LOCKING
  if (take_lock) {
    UNLOCK_TCPIP_CORE();
  }
#endif
  if (err == ERR_OK) {
    return true;  // already in the DNS cache
  }
  if (err != ERR_INPROGRESS) {
    ++s_skipped;
    Serial.printf("dns: %s lookup could not start (%d)\n", host,
                  static_cast<int>(err));
    return false;
  }

  const unsigned long started = millis();
  while (s_result == 0 && millis() - started < timeout_ms) {
    if (poll != nullptr) {
      poll();
    }
    delay(10);
  }
  if (s_result == 1) {
    return true;
  }
  ++s_skipped;
  Serial.printf("dns: %s %s after %lu ms, skipping this request\n", host,
                s_result == 0 ? "no answer" : "failed", millis() - started);
  return false;
}

unsigned long skippedLookups() { return s_skipped; }
#endif

}  // namespace services::net
