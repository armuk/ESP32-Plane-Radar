# PlatformIO pre-build script: sets PLANE_RADAR_VERSION for the firmware.
#
# Order of preference:
#   1. PLANE_RADAR_VERSION environment variable (the release workflow sets it
#      from the git tag, e.g. "v1.2.0")
#   2. `git describe --tags --dirty` when building from a git checkout
#   3. "dev"
# The value only changes with the version, so builds are not invalidated
# on every run.

Import("env")

import os
import re
import subprocess


def detect_version():
    value = os.environ.get("PLANE_RADAR_VERSION", "").strip()
    if value:
        return value
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),
            capture_output=True,
            text=True,
            timeout=5,
        )
        if out.returncode == 0 and out.stdout.strip():
            return out.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return "dev"


def detect_release_repo():
    """owner/name of the GitHub repository whose releases the device checks.

    PLANE_RADAR_RELEASE_REPO (the release workflow sets it to the repository
    that built the firmware), else the github.com "origin" remote of a git
    checkout. Empty turns the check off.
    """
    value = os.environ.get("PLANE_RADAR_RELEASE_REPO")
    if value is None:
        value = ""
        try:
            out = subprocess.run(
                ["git", "remote", "get-url", "origin"],
                cwd=env.subst("$PROJECT_DIR"),
                capture_output=True,
                text=True,
                timeout=5,
            )
            match = re.search(r"github\.com[:/]([^/\s]+/[^/\s]+?)(?:\.git)?/?$",
                              out.stdout.strip())
            if out.returncode == 0 and match:
                value = match.group(1)
        except (OSError, subprocess.SubprocessError):
            pass
    value = value.strip()
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,39}/[A-Za-z0-9._-]{1,100}", value):
        return ""
    return value


version = detect_version()[:31]
# Keep it to characters that are safe in a C string and in HTML.
version = "".join(c for c in version if c.isalnum() or c in ".-_+") or "dev"
print("Plane Radar firmware version: " + version)
env.Append(CPPDEFINES=[("PLANE_RADAR_VERSION", '\\"%s\\"' % version)])

release_repo = detect_release_repo()
print("Update check against: " + (release_repo or "(off)"))
env.Append(CPPDEFINES=[("PLANE_RADAR_RELEASE_REPO", '\\"%s\\"' % release_repo)])
