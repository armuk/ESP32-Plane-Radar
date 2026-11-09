#pragma once

#include <cstddef>

/** True when the next boot should show the setup screen first (after credential reset). */
bool wifiShowsSetupScreenOnBoot();

/** Length of the setup hotspot's WPA2 password (WPA2 needs at least 8). */
constexpr size_t kSetupApPasswordLen = 8;
/** WPA2 password of the setup hotspot (random per boot, shown on screen). */
const char* wifiSetupApPassword();
void wifiResetCredentialsAndReboot();

/**
 * Restart, first telling the access point the device is leaving. Without
 * that, the device comes back with the same MAC address while the access
 * point may still hold its old session. Keeps the saved network.
 */
[[noreturn]] void wifiLeaveAndRestart();

/**
 * Call from loop(): if the signal stays below -78 dBm for 3 minutes, leave
 * and rejoin the network (strongest access point). At most every 15 min,
 * backing off to 4 h while that finds nothing better.
 */
void wifiCheckSignal();

/** True while a rejoin started by wifiCheckSignal() is still connecting. */
bool wifiRejoinInProgress();
/** Boot flow: connect with UI, open portal only if saved creds fail. */
bool wifiSetupConnect();
/** Reconnect using saved creds; never opens the captive portal. */
bool wifiReconnect();
/** Keeps the LAN config portal alive; call every loop() iteration. */
void wifiLoop();
/**
 * Light poll for use inside long HTTP I/O: BOOT button only. Deliberately
 * does not run the web server, so uploads and settings saves never execute
 * in the middle of a TLS request.
 */
void wifiPollDuringIo();
bool wifiBootButtonPressed();
/** GPIO + interrupt setup; call once early in setup(). */
void bootButtonInit();
/** Latched short tap (survives blocking HTTP/display work). */
bool bootButtonConsumeTap();
/** Call each loop iteration; triggers WiFi reset on long hold. */
void bootButtonPollLongPress();
