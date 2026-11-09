# PlatformIO post-build script: sign firmware.bin for over-the-air updates.
#
# The device only installs an update whose ECDSA P-256 / SHA-256 signature
# checks out against keys/ota-signing-public.pem (built into the firmware).
# Output: .pio/build/<env>/firmware-signed.bin = firmware.bin + the DER
# signature padded to 512 bytes (the Arduino-ESP32 Update library format,
# same as tools/bin_signing.py in the core).
#
# The private key is never kept in the repository. It is read from:
#   1. $PLANE_RADAR_SIGNING_KEY (a file path), else
#   2. ~/.config/plane-radar/ota-signing-key.pem
# Without a key the build still succeeds (USB flashing does not need a
# signature) but no firmware-signed.bin is produced. Set
# PLANE_RADAR_REQUIRE_SIGNING=1 (the release workflow does) to fail instead.

Import("env")

import os
from os.path import expanduser, getmtime, isfile, join

PUBLIC_KEY = join(env.subst("$PROJECT_DIR"), "keys", "ota-signing-public.pem")
SIGNATURE_SLOT = 512


def _key_path():
    path = os.environ.get("PLANE_RADAR_SIGNING_KEY", "").strip()
    if path:
        return expanduser(path)
    return expanduser("~/.config/plane-radar/ota-signing-key.pem")


def _fail_or_warn(message):
    if os.environ.get("PLANE_RADAR_REQUIRE_SIGNING") == "1":
        raise RuntimeError(message)
    print(f"Signing skipped: {message}")


def _app_slot_size(env):
    """Size of the smallest app partition in the board's partition CSV."""
    csv_path = join(env.subst("$PROJECT_DIR"),
                    env.GetProjectOption("board_build.partitions"))
    sizes = []
    with open(csv_path) as f:
        for line in f:
            fields = [x.strip() for x in line.split("#")[0].split(",")]
            if len(fields) >= 5 and fields[1] == "app":
                sizes.append(int(fields[4], 0))
    return min(sizes) if sizes else None


def check_fits(firmware, env):
    # PlatformIO's "Flash:" figure counts code and data sections, not the
    # .bin, and missed an image 5 KB too big: the bootloader then refused
    # it and the board boot-looped. Fail the build instead.
    slot = _app_slot_size(env)
    if slot is None:
        return
    needed = os.path.getsize(firmware) + SIGNATURE_SLOT
    if needed > slot:
        raise RuntimeError(
            f"{os.path.basename(firmware)} + signature is {needed} bytes but "
            f"the app slot is {slot}: it would not boot.")
    print(f"App slot: {needed} of {slot} bytes ({slot - needed} spare)")


def sign_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    firmware = join(build_dir, env.subst("${PROGNAME}") + ".bin")
    signed = join(build_dir, "firmware-signed.bin")
    if isfile(signed):
        os.remove(signed)  # never leave a stale signed image behind
    check_fits(firmware, env)

    key_file = _key_path()
    if not isfile(key_file):
        _fail_or_warn(f"no signing key at {key_file}; OTA uploads of this "
                      "build will be refused (USB flashing still works).")
        return None

    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
    except ImportError:
        _fail_or_warn("the Python 'cryptography' package is not installed.")
        return None

    with open(key_file, "rb") as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or \
            private_key.curve.name != "secp256r1":
        raise RuntimeError(f"{key_file} is not an ECDSA P-256 private key")

    with open(PUBLIC_KEY, "rb") as f:
        expected = serialization.load_pem_public_key(f.read())
    der = serialization.Encoding.DER
    spki = serialization.PublicFormat.SubjectPublicKeyInfo
    if private_key.public_key().public_bytes(der, spki) != \
            expected.public_bytes(der, spki):
        raise RuntimeError(
            f"{key_file} does not match {PUBLIC_KEY}: devices running this "
            "firmware would refuse every update signed with it.")

    with open(firmware, "rb") as f:
        image = f.read()
    # The device reads this marker to refuse downgrades; without it every
    # older image would be accepted.
    if b"PLANE-RADAR-FW-VERSION=" not in image:
        raise RuntimeError(f"{firmware} has no PLANE-RADAR-FW-VERSION= "
                           "marker (removed by the linker?)")
    signature = private_key.sign(image, ec.ECDSA(hashes.SHA256()))
    # Check it round-trips before publishing anything.
    expected.verify(signature, image, ec.ECDSA(hashes.SHA256()))
    with open(signed, "wb") as f:
        f.write(image)
        f.write(signature.ljust(SIGNATURE_SLOT, b"\0"))
    print(f"Signed OTA image -> {signed} ({len(image) + SIGNATURE_SLOT} bytes)")
    return None


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", sign_firmware)
