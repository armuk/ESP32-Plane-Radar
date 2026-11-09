#pragma once

// Set by scripts/firmware_version.py (release tag, git describe, or "dev").
#ifndef PLANE_RADAR_VERSION
#define PLANE_RADAR_VERSION "dev"
#endif

namespace config {
constexpr char kFirmwareVersion[] = PLANE_RADAR_VERSION;
}  // namespace config
