# PlatformIO pre-build script: sets PLANE_RADAR_VERSION for the firmware.
#
# Order of preference:
#   1. PLANE_RADAR_VERSION environment variable (the release workflow sets it
#      from the git tag, e.g. "v1.2.0")
#   2. `git describe --tags --dirty` when building from a git checkout,
#      matching only this board's tags (see custom_tag_prefix below)
#   3. "dev"
#
# Each board folder releases from its own tags: the C3 Super Mini uses
# "v2.3.0", other boards a prefix ("s3-v0.1.0", "p4-v0.1.0"), set in
# platformio.ini as custom_tag_prefix. The prefix is dropped from the
# version, so every board's firmware reports a plain "v1.2.3".
# custom_update_check = no turns the release check off (boards without
# releases of their own yet).
# The value only changes with the version, so builds are not invalidated
# on every run.

Import("env")

import os
import re
import subprocess


TAG_PREFIX = env.GetProjectOption("custom_tag_prefix", "").strip()
UPDATE_CHECK = env.GetProjectOption("custom_update_check", "yes").strip().lower()


def strip_prefix(value):
    if TAG_PREFIX and value.startswith(TAG_PREFIX):
        return value[len(TAG_PREFIX):]
    return value


def detect_version():
    value = os.environ.get("PLANE_RADAR_VERSION", "").strip()
    if value:
        return strip_prefix(value)
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty",
             "--match", TAG_PREFIX + "v[0-9]*"],
            cwd=env.subst("$PROJECT_DIR"),
            capture_output=True,
            text=True,
            timeout=5,
        )
        if out.returncode == 0 and out.stdout.strip():
            return strip_prefix(out.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        pass
    return "dev"


def detect_release_repo():
    """owner/name of the GitHub repository whose releases the device checks.

    PLANE_RADAR_RELEASE_REPO (the release workflow sets it to the repository
    that built the firmware), else the github.com "origin" remote of a git
    checkout. Empty turns the check off.
    """
    if UPDATE_CHECK in ("no", "off", "0", "false"):
        return ""
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
