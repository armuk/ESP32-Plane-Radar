#pragma once

#include <WString.h>

class WiFiManager;

namespace services::ota {

using AdditionalRoutesFn = void (*)();

/**
 * Add an authenticated firmware page to the WiFiManager portal. The optional
 * callback runs while the portal server is being created, before WiFiManager
 * installs its default routes.
 */
void configure(WiFiManager& manager,
               AdditionalRoutesFn additional_routes = nullptr);

/** millis() of the last portal request (0 = none since boot). */
unsigned long lastPortalRequestMs();

/** True while an OTA upload is actively writing flash. */
bool inProgress();

/**
 * True after the Status page's "Install" button, until runPendingInstall()
 * runs. loop() checks it first.
 */
bool installPending();

/**
 * Downloads the requested release's signed -ota.bin into the update slot,
 * with the same signature, size, downgrade and rollback checks as an
 * upload, and restarts on success. Blocks for about a minute; the screen
 * shows progress. On failure it returns and installError() says why.
 */
void runPendingInstall();

/** Why the last Install failed ("v1.2.3: reason"), or empty. */
const String& installError();

/**
 * Call once early in setup(). Notes whether this boot is a new image waiting
 * to be confirmed, and whether an earlier update was rolled back.
 */
void checkBootImage();

/**
 * Marks a new image as good so the bootloader keeps it. Call once the
 * firmware has proven itself (first aircraft fetch); later calls do nothing.
 * Until then, any reset (crash, watchdog, self-recovery restart, power cut)
 * boots the previous firmware.
 */
void confirmRunningFirmware();

/** A new image is running but not confirmed yet (rollback still armed). */
bool awaitingConfirmation();

/** Text about a rolled-back update, or empty. Shown on the About page. */
const String& rollbackNote();

}  // namespace services::ota
