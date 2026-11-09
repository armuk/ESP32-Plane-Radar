#pragma once

#include <cstddef>

/**
 * Bounded DNS lookup before an HTTP request.
 *
 * The core resolves names with lwIP's blocking getaddrinfo, whose retry
 * timer (fixed in the precompiled core) gives up only after about 7 s, and
 * a failing HTTPS request resolves twice. The device serves web pages only
 * between requests, so one failed lookup kept the portal waiting 12-17 s.
 *
 * resolveQuickly() starts the lookup itself and waits at most timeout_ms.
 * A successful answer lands in lwIP's DNS cache, so the request's own
 * lookup that follows returns at once. On failure the caller skips the
 * request; the next cycle tries again.
 */
namespace services::net {

using PollFn = void (*)();

constexpr unsigned long kDnsTimeoutMs = 3000;

/**
 * True when the URL's host resolved within timeout_ms, or needs no lookup
 * here (an IP address, or a .local mDNS name). poll runs while waiting.
 */
bool resolveQuickly(const char* url, unsigned long timeout_ms = kDnsTimeoutMs,
                    PollFn poll = nullptr);

/** Requests skipped because their lookup failed or timed out, since boot. */
unsigned long skippedLookups();

/**
 * Copies the host part of "scheme://host[:port][/...]" into out. False when
 * there is no host or it does not fit. Exposed for unit tests.
 */
bool hostFromUrl(const char* url, char* out, size_t out_len);

}  // namespace services::net
