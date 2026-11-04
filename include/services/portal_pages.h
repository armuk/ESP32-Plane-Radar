#pragma once

#include <WebServer.h>

/**
 * Portal pages of our own, beside WiFiManager's: the Receiver settings page
 * (a sub-page of Setup), About (versions, libraries and data sources) and a
 * confirmation step in front of erasing the saved Wi-Fi.
 */
namespace services::portal {

/** Erases only the saved Wi-Fi network and restarts into the setup hotspot. */
using EraseWifiFn = void (*)();

/**
 * Registers the routes. Call before WiFiManager registers its own, so our
 * /erase replaces its one-click version.
 */
void attachPages(WebServer& web, EraseWifiFn erase_wifi);

/**
 * Start-up times for the Status page: ms after boot until Wi-Fi joined and
 * until the first aircraft data arrived.
 */
void noteStartup(unsigned long wifi_ms, unsigned long first_data_ms);

}  // namespace services::portal
