#pragma once

#include <Arduino.h>

/**
 * One dark theme for every page the device serves: WiFiManager's own pages
 * (added after its built-in stylesheet, so these rules win), the firmware
 * update page and the Saved / Restarting pages. Kept in flash; no RAM.
 */
namespace services::portal {

/**
 * Style tag plus the page-links bar script, for pages we write ourselves.
 */
extern const char kStyleTag[];

/**
 * WiFiManager custom head element: the style tag plus a script that adds the
 * firmware version to the Info page.
 */
extern const char kHeadElement[];

/**
 * A complete small page in the theme: body_html goes inside the card.
 * extra_head (e.g. a refresh meta tag) may be empty.
 */
String page(const char* title, const char* extra_head, const String& body_html);

}  // namespace services::portal
