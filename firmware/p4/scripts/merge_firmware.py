# PlatformIO post-build script: publish the single flash-at-0x0 image.
# Usage: pio run -t merge -e p4
#
# The pioarduino platform already writes ${PROGNAME}.factory.bin after every
# build (bootloader, partitions, boot_app0 and app at this chip's offsets,
# via esptool merge-bin). This target checks it is current and copies it to
# firmware-merged.bin, the name the release workflow and web flasher use.

Import("env")

import os
import shutil
from os.path import getmtime, isfile, join


def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("${PROGNAME}")
    firmware = join(build_dir, f"{progname}.bin")
    factory = join(build_dir, f"{progname}.factory.bin")
    merged = join(build_dir, "firmware-merged.bin")

    if not isfile(factory):
        raise FileNotFoundError(
            f"Missing {factory}: the platform's merge-bin step did not run or "
            "failed (see the build output)."
        )
    if getmtime(factory) < getmtime(firmware):
        raise RuntimeError(
            f"{os.path.basename(factory)} is older than {progname}.bin; the "
            "platform's merge-bin step failed (see the build output)."
        )
    shutil.copyfile(factory, merged)
    print(f"Flash image -> {merged} ({os.path.getsize(merged)} bytes, flash at 0x0)")
    return None


env.AddCustomTarget(
    name="merge",
    dependencies="${BUILD_DIR}/${PROGNAME}.bin",
    actions=env.Action(merge_firmware, "Copying flash image for web flasher"),
    title="Merge firmware",
    description="Create firmware-merged.bin (bootloader + partitions + app)",
)
