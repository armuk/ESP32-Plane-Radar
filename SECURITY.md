# Security

This page covers how Plane Radar protects itself and its owner's network, what it does not protect against, how long it gets security fixes, and how to report a problem. It follows the UK NCSC [device security principles for manufacturers](https://www.ncsc.gov.uk/collection/device-security-principles-for-manufacturers).

## Support period

> **Draft — the maintainer has not yet confirmed these dates.**

| | |
|---|---|
| Security fixes | The latest release only. Fixes ship as a new release; there are no back-ported patches for older versions. |
| Support ends | Not before **31 December 2028**. Any change to this date will be announced in the release notes at least 12 months in advance. |
| After support ends | The device keeps working, but known problems will no longer be fixed. The last release stays downloadable. |

The firmware depends on [Arduino-ESP32](https://github.com/espressif/arduino-esp32) and [ESP-IDF](https://github.com/espressif/esp-idf). A fix in those projects reaches devices when a Plane Radar release moves to the fixed version.

## Reporting a vulnerability

Report it privately through this repository's **Security** tab → **Report a vulnerability** (GitHub private vulnerability reporting). Please don't open a public issue for it.

> **Draft:** the maintainer aims to acknowledge a report within 7 days and to agree a disclosure date with the reporter; fixes are credited in the release notes unless the reporter prefers otherwise.

Useful details: firmware version (from the **Status** or **About** page), what you did, and what happened. If the device crashed, include the decoded crash dump from `scripts/read_coredump.py` (see the README).

## What is protected

### Settings login

- Every settings and maintenance page on the home network needs a login. The username is `admin`. The password is unique to each device: 10 random characters from a 32-symbol alphabet with no look-alike characters, about 50 bits, from the chip's hardware random number generator.
- The device shows the password on its screen after it first joins Wi‑Fi. It stays there until BOOT is tapped or someone logs in with it, for at most 3 minutes per boot, and comes back at every boot until then. Devices upgraded from older firmware that still had the old shared default `plane-radar` get a new random password the same way.
- Logins use HTTP **Digest** authentication, so the password itself is never sent over the network. Digest uses MD5 and the web interface has no TLS, so someone who can watch traffic on your network can see pages and settings and could try to guess the password offline. Keep the device on a network you trust.
- After 5 failed logins in a row, the device refuses further attempts for 30 seconds. Each further failure doubles the wait, up to 15 minutes. Locked-out requests get HTTP 429 with `Retry-After`. A restart clears the counter.
- You can change the password under **Setup → Device** (8 to 32 characters; a shorter one is ignored and logged). Holding BOOT for 3 s (a factory reset) generates a new one.

### Cross-site requests (CSRF)

Once a browser has logged in, it re-sends the login with every request to the device, including requests that a page on another site makes it send. So every request that changes something must come from the device's own pages: saving Wi‑Fi, settings or the receiver, forgetting Wi‑Fi, restart, exit, close and firmware uploads. The device checks the browser's `Origin` header (or the `Referer` header when there is no `Origin`) against the host the browser addressed. A page on another site can hide these headers but cannot fake them, so a request from another site, or with both headers missing or `Origin: null`, gets HTTP 403 and a "blocked cross-site" entry in the security log. Pages that only display something are not checked, because another site cannot read the replies. The setup hotspot is exempt, as it has no login.

A side effect: the device keeps only its latest Digest challenge, so a forged request usually brings up an unexpected login prompt first. Decline it.

### First-time setup hotspot

- `PlaneRadar-Setup` uses WPA2. Its 8-character password is random, shown only on the device's own screen, and changes at every boot. Joining the hotspot needs someone who can see the screen.
- On the hotspot the settings pages need no login, because the owner may not know the settings password yet. Firmware uploads always need the login.

### Firmware updates

- **Signed images only.** The device installs an over-the-air update only when it carries a valid ECDSA P‑256 / SHA‑256 signature from the Plane Radar release key. The check runs on the device before the new image is marked bootable, so an unsigned, modified or corrupted image is refused and the running firmware stays.
- **No downgrades.** An update whose release number is older than the installed one is refused. Without this, an older image with a known flaw could be put back.
- **Automatic rollback.** A new image is kept only once it has fetched aircraft data. If it crashes or resets before then, the bootloader goes back to the previous image.
- **USB is the recovery path.** Flashing over USB needs physical access and does not check signatures, so a device can always be recovered.

### Factory reset (hold BOOT 3 s)

The reset erases the whole settings store (Wi‑Fi, location, password, all settings), the downloaded-airports filesystem and any saved crash dump, then restarts into the setup hotspot. Nothing from the previous owner is left, apart from the security event log in RTC memory, which is cleared by the next power cut. The kept security events in flash are erased with everything else; the reset then writes one new entry recording that it happened.

### Security event log

The **Security** page (login required) lists the last 24 events: boots with their reset reason, failed and successful logins after failures, lockouts, settings and password changes, Wi‑Fi erase, factory reset, and firmware updates started, refused, installed, confirmed or rolled back. The log is kept in RTC memory, so it survives restarts but not power loss, and causes no flash wear. Each event is also printed on the USB serial console.

Key events are also kept in flash, so they survive power cuts: updates installed, refused, confirmed or rolled back; settings password changed; login lockouts; blocked cross-site requests; Wi‑Fi erased; factory reset; settings restored. The newest 16 are kept. To bound flash wear, at most 20 are written per boot, and an event identical to the newest kept one is not written again; anything beyond that still goes to the RTC log.

### Settings backup

`/backup.json` exports the device's settings, including its location. It never contains the settings password or the Wi‑Fi network, and a restore cannot set them. A restore needs the login and must come from the device's own Backup page (cross-site check). The whole file is checked before anything is written, and each value is checked again when it is loaded at boot.

### Outbound connections

All data services are fetched over HTTPS. **Server certificates are not checked yet** (see [Known limitations](#known-limitations)), so someone who can tamper with your network traffic could feed the radar false aircraft or weather data. They could not use this to change settings or firmware.

**Update check.** Release builds ask GitHub once a day (`api.github.com/repos/<repo>/releases/latest`) whether a newer release exists. `<repo>` is the repository that built the firmware. The request sends the device's IP address, as any web request does, and a User-Agent of `PlaneRadar/<version>`. Only the release tag and the link to its page are read. Nothing is downloaded or installed automatically. Because certificates are not checked, someone on the network path could fake the notice. They still could not install anything, because updates must be signed. Builds without a release repository never make this request.

## Interfaces

| Interface | Direction | Protection |
|-----------|-----------|------------|
| USB-C (serial console + flashing) | Physical | None. Anyone holding the device can read logs and flash any firmware. This is the recovery path. |
| BOOT button | Physical | Tap: range / wake screen / acknowledge password notice. Hold 3 s: factory reset. |
| Wi‑Fi client (home network) | Both | WPA2/WPA3 as set by your router. |
| Setup hotspot `PlaneRadar-Setup` | Inbound | WPA2, random password shown on the screen. Only runs when no Wi‑Fi is saved or the saved Wi‑Fi can't be reached. |
| Web interface, HTTP port 80 | Inbound | See the endpoint table below. No TLS. |
| mDNS (`<name>.local`, UDP 5353) | Both | Announces the web interface. |
| DHCP, DNS | Outbound | Standard; trusts the network. |
| NTP (UDP 123) | Outbound | `pool.ntp.org` (configurable) and `time.nist.gov`. Not authenticated; only sets the clock. |
| adsb.fi, ADSBDB, Open-Meteo, GitHub Pages (airport data), GitHub API (update check, daily) | Outbound HTTPS | TLS without certificate checking (see below). |
| Local receiver (optional) | Outbound HTTP | A `tar1090`/`readsb` `aircraft.json` URL on your network that you configure. |

### Web endpoints

"Login" means the Digest login above. Endpoints marked **open on hotspot** need no login on the setup hotspot, but do need it on the home network.

| Path | Method | Login | Purpose |
|------|--------|-------|---------|
| `/` | GET | No | Menu |
| `/about` | GET | No | Versions, licences, data sources |
| `/wifi`, `/0wifi` | GET | Yes (open on hotspot) | Wi‑Fi page, with and without a scan |
| `/wifisave` | POST | Yes (open on hotspot) | Save Wi‑Fi |
| `/param` | GET | Yes (open on hotspot) | Setup form |
| `/paramsave` | POST | Yes (open on hotspot) | Save settings |
| `/receiver`, `/receiversave` | GET, POST | Yes (open on hotspot) | Local receiver settings |
| `/info` | GET | Yes (open on hotspot) | Device information |
| `/status`, `/status.json` | GET | Yes (open on hotspot) | Status page and the same as JSON |
| `/security` | GET | Yes (open on hotspot) | Security event log |
| `/backup`, `/backup.json` | GET | Yes (open on hotspot) | Backup page; download the settings |
| `/restore` | POST | Yes, always | Upload a settings backup (cross-site check), then restart |
| `/erase` | any | Yes (open on hotspot) | Forget the saved Wi‑Fi (asks to confirm) |
| `/restart`, `/exit`, `/close` | GET | Yes (open on hotspot) | Restart / leave the portal |
| `/firmware`, `/update` | GET | Yes, always | Firmware upload page |
| `/firmware-upload`, `/u` | POST | Yes, always | Upload a signed image |
| anything else | any | No | Captive-portal redirect to `/` |

## Exploit mitigations

Built into the firmware (ESP-IDF 5.5 configuration of the Arduino core, checked in its `sdkconfig.h`):

- Stack-smashing protection (`-fstack-protector`, `CONFIG_COMPILER_STACK_CHECK`)
- Hardware stack guard (`CONFIG_ESP_SYSTEM_HW_STACK_GUARD`) and FreeRTOS stack canaries
- Memory protection, locked at boot: instruction memory is not writable and data memory is not executable (`CONFIG_ESP_SYSTEM_MEMPROT_FEATURE_LOCK`)
- Heap corruption detection (light heap poisoning)
- Task and interrupt watchdogs, brown-out detection
- Flash writes to the bootloader and partition table abort (`CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS`)
- Crash dumps saved to flash for diagnosis

In the project:

- Every incoming field is length- and range-checked before it is stored (unit tests in `test/test_settings`).
- printf-style format mismatches are compile errors (`-Werror=format`).
- cppcheck static analysis runs on every build in CI and must report no defects.
- Library versions are pinned exactly in `platformio.ini`.
- Each release publishes a CycloneDX **SBOM** (`plane-radar-<version>.cdx.json`) listing the core, ESP-IDF, mbedTLS, lwIP, libraries and bundled data, together with SHA-256 checksums for every image.

## Update signing key

| | |
|---|---|
| Algorithm | ECDSA P‑256, SHA‑256 (Arduino-ESP32 `Update` library format: image + DER signature padded to 512 bytes) |
| Public key | [`keys/ota-signing-public.pem`](keys/ota-signing-public.pem), built into every firmware |
| Key ID | `ddf80943b6ae45e6` (first 16 hex digits of the SHA‑256 of the PEM file; shown on the device's **Status** page) |
| Private key | Never stored in the repository. Release builds use the `OTA_SIGNING_KEY` GitHub Actions secret. The maintainer's local copy is kept outside the project directory, readable only by the owner. |

To build your own firmware and still use the over-the-air updater, generate your own key pair, replace `keys/ota-signing-public.pem`, and flash that build over USB once:

```bash
python ~/.platformio/packages/framework-arduinoespressif32/tools/bin_signing.py --generate-key ecdsa-p256 --out ~/.config/plane-radar/ota-signing-key.pem
python ~/.platformio/packages/framework-arduinoespressif32/tools/bin_signing.py --extract-pubkey ~/.config/plane-radar/ota-signing-key.pem --out keys/ota-signing-public.pem
```

`pio run` then also writes `.pio/build/supermini/firmware-signed.bin` (see `scripts/sign_firmware.py`). The key is read from `$PLANE_RADAR_SIGNING_KEY`, or from `~/.config/plane-radar/ota-signing-key.pem` when that is not set. After that, the device accepts only images signed with your key, and refuses official releases.

**If the release key is lost or leaked:** a new key can only reach existing devices through a USB flash, because each device trusts only the key built into it. A leaked key would let its holder push firmware to any device they can log in to, but they would still need the settings password. The announcement would go in the release notes and this file.

## Known limitations

These are deliberate, given the hardware (ESP32-C3, 4 MB flash, about 90 KB free heap) and how the device is used:

- **No TLS on the web interface.** A certificate for a `.local` name on a home network can't be trusted by browsers without manual steps, and a TLS server would need about 30–40 KB of heap. Mitigations: Digest login, login backoff, signed updates.
- **HTTPS certificates are not checked yet.** Checking them was measured on the device: about 1.4 KB more heap at the worst point and about 1 s longer per new connection. It does fit in memory. It needs the 69 KB root certificate bundle in flash, though, and with update signing added the firmware no longer fits its 1.75 MB slot with room to spare. Making room means changing the partition table, which needs one USB flash per device.
- **No secure boot or flash encryption.** The ESP32-C3 supports both, but they are one-way eFuse settings: a mistake permanently bricks the board, and USB recovery is no longer possible. Someone with physical access can therefore read the Wi‑Fi password from flash, or flash other firmware. Owners who need this can enable it themselves with ESP-IDF's tools.
- **Signature checks apply to over-the-air updates only.** USB flashing is not checked; it is the recovery path.
