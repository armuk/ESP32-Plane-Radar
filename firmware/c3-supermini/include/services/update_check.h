#pragma once

/**
 * "Update available" check against the project's GitHub releases.
 *
 * Once about two minutes after boot and then once a day, the device asks
 * https://api.github.com/repos/<repo>/releases/latest for the newest
 * release's tag. Only the tag and the release page link are read. Nothing
 * is downloaded until the user presses "Install" on the Status page; that
 * download (ota_update.cpp) gets the same signature, version and rollback
 * checks as a manual upload on the Firmware update page.
 *
 * <repo> is fixed at build time (PLANE_RADAR_RELEASE_REPO, set by the
 * release workflow to the repository that built it). Builds without it
 * never contact GitHub.
 */
#ifndef PLANE_RADAR_RELEASE_REPO
#define PLANE_RADAR_RELEASE_REPO ""
#endif

#include <cstddef>

namespace services::updates {

using PollFn = void (*)();

/**
 * True when latest is a newer release than running. Both must be release
 * versions ("v1.2.3"); a "dev" build or an odd tag is never told to update.
 */
bool newerRelease(const char* running, const char* latest);

/**
 * Writes the download address of a release's signed OTA image:
 * https://github.com/<repo>/releases/download/<tag>/plane-radar-<tag>-ota.bin
 * (the name the release workflow gives it). False, with out untouched,
 * when repo is empty or has odd characters, tag is not a release version
 * ("v1.2.3"), or out is too small.
 */
bool releaseAssetUrl(const char* repo, const char* tag, char* out, size_t out_len);

#ifdef ARDUINO
void setPollFn(PollFn fn);

/** True when a check should run now (enabled, Wi-Fi up, interval passed). */
bool due();

/** Runs one check (one HTTPS request). */
void check();

/** False when this build has no release repository. */
bool enabled();
/** Newest release tag seen, or "" before the first successful check. */
const char* latestTag();
/** That release's page (https://github.com/...), or "". */
const char* releasePage();
bool updateAvailable();
/** Seconds since the last successful check, or -1 for never. */
long lastCheckAgeS();
#endif

}  // namespace services::updates
