#pragma once

/**
 * Cross-site request forgery (CSRF) check, without cookies or tokens.
 *
 * Once a browser has logged in, it re-sends the Digest login with every
 * request to the device, including requests a page on another site makes it
 * send (a hidden form posting to http://plane-radar.local/paramsave, an
 * <img> pointing at /restart). Browsers mark where such a request came from
 * and a page cannot forge it: Origin on every POST (or "null" when the page
 * hides it), Referer on links and GET forms (or nothing when hidden).
 *
 * A request that changes something is accepted only when that source names
 * the same host the browser addressed (the Host header). A request with
 * neither header, or "Origin: null", is refused: the device's own pages
 * always send one.
 */
namespace services::request_origin {

/**
 * origin: the Origin header, or empty. referer: the Referer header, or
 * empty. host: the Host header ("plane-radar.local", "192.168.2.172",
 * "192.168.2.172:8080"). Comparison ignores case; only http:// sources
 * match (the device serves no HTTPS).
 */
bool sameOrigin(const char* origin, const char* referer, const char* host);

}  // namespace services::request_origin
