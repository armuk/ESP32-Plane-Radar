# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](../../releases)

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display (240×240). Shows a circular **ADS-B radar** around your configured location, with flight routes, detailed aircraft models, local weather/time, browser settings, and authenticated OTA updates.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid
3. **Useful labels** — route (for example `BOS-IND`), detailed aircraft model, and altitude
4. **Readable footer** — current conditions, temperature, humidity, local time, and date

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~3 s).

## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Short tap** | Cycle range preset (5 → 10 → 15 → 25 → 50 → 75 → 100 km); saved to flash. While the screen is off for the night, a tap wakes it for a minute instead |
| **Hold 3 s** | Factory reset: erases Wi‑Fi, location and every setting, the downloaded airports and any crash dump, then reboots into the setup portal with a new settings password |
| **Tap while the settings password is shown** | Confirms you've noted it down; the radar appears |

You can also hold BOOT for 3 s while the **Connecting** screen is shown to reset and open the setup portal. Don't hold it while plugging in or pressing RESET: GPIO 9 is the chip's boot-mode pin, so that starts the USB flashing mode instead of the firmware.

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`PlaneRadar-Setup`** using the Wi‑Fi password shown on the yellow setup screen (random, new at every boot)
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, then save
4. Once the device joins your Wi‑Fi it shows its **settings login**: user `admin` and a random 10-character password. Note it down, then tap BOOT. It's needed for every settings page on your home network and for firmware updates. It comes back at each boot until BOOT is tapped or someone logs in with it

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://plane-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. Choose **Setup** and log in as `admin` with the settings password
3. Change coordinates, range, aircraft filter, display options, units, device name, or OTA password; save

Every portal page has a row of links at the top: **Home · Wi-Fi · Setup · Info · Firmware · About**.

- **Setup** is split into tabs: **Location**, **Aircraft**, **Map**, **Screen**, **Clock & weather** and **Device**. All tabs are one form, so **Save** stores every tab; the open tab is remembered while you browse. Without JavaScript the sections are shown one after another.
- **Receiver** (linked from Setup's Aircraft tab) chooses where aircraft come from: adsb.fi or your own receiver. See *Local receiver* below.
- **Wi-Fi** opens without scanning; **Show visible networks** scans and adds the list. **Forget this Wi-Fi** at the bottom asks for confirmation, then erases only the saved network (location and settings are kept) and restarts into the `PlaneRadar-Setup` hotspot. It is no longer on the Info page.
- **About** lists the firmware, core and library versions and the data sources, with links (WiFiManager's help table and GitHub link are no longer on Info).

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname defaults to `plane-radar` → **plane-radar.local**; change it with **Device name** in Setup. Some clients resolve `.local` slowly; use the IP if needed.

**Several access points (mesh / UniFi):** the radar joins the strongest access point for your network, not the first one it hears. While connected it supports 802.11k/v, so a network with **BSS Transition** enabled (UniFi: the network's advanced Wi‑Fi settings) can move it to a better access point. If the signal still stays below −78 dBm for 3 minutes, it leaves and rejoins. That happens at most every 15 minutes, backing off to every 4 hours while no better access point is found. Rejoins are listed on the **Security** page.

Changing coordinates no longer requires a credential reset. The new position is validated in the browser and firmware, saved to NVS, and used by the next aircraft/weather refresh.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center and ADS-B query position (defaults in `config.h` until set). Values copied from Google Maps can be pasted as is: extra decimals are rounded to 6 (~0.1 m), and pasting a `lat, lon` pair into either box fills both |
| **Range (ring label)** | Same presets as tapping BOOT (5 / 10 / 15 / 25 / 50 / 75 / 100 km, or the miles equivalents) |
| **Display distances in miles** | Ring scale label in **mi** instead of **km** (e.g. `6mi` vs `10km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |
| **Show medium airports** | Downloads the medium airports within 140 km of the saved location (see Runways). On by default; the line under it shows what is saved |
| **Show aircraft on the ground** | Include taxiing/parked aircraft (`alt_baro` "ground"). Off by default |
| **Lowest / Highest altitude shown (ft)** | Hide airborne aircraft outside this band; blank = no limit (e.g. highest `10000` hides cruising traffic). Ground aircraft follow the checkbox above; aircraft with no altitude are always shown |
| **Move or shorten tags so they don't overlap** | Nearest aircraft are tagged first; a tag that would overlap another tag or aircraft moves above, below or to the other side, then shrinks to one line (route or callsign), then is left off. On by default; off restores the old fixed placement |
| **Look up routes and aircraft types (ADSBDB)** | Off stops sending callsigns to ADSBDB; tags then show the callsign and the type from the ADS-B data |
| **Show airline names on tags** | Adds the airline (from ADSBDB, e.g. `British Airways`) as a fourth tag line when known. On by default |
| **Light aircraft small/yellow, military green** | Light aircraft (ADS-B category A1, gliders, ultralights, or a common light type such as `P28A` / `C172` when the transponder sends no category) drawn smaller in yellow; aircraft adsb.fi marks as military drawn green. On by default |
| **Compass bearing at the top of the screen** | 0 = north up. E.g. `90` puts east at the top, to match a radar facing an east window. Rings, runways, aircraft and the N/E/S/W letters all turn |
| **Turn the screen off at night** + **Off from / Back on at (hour)** | Blanks and sleeps the screen between these local hours (default 23–7; same hour = never). A BOOT tap wakes it for a minute. Our wiring has no backlight pin, so the backlight stays lit behind a black screen |
| **Show weather and clock** | Enables/disables the complete bottom footer |
| **Show current weather** | Shows current condition, temperature, and humidity |
| **Temperature in Fahrenheit** | Uses °F instead of °C |
| **Use 24-hour clock** | Uses 24-hour instead of compact 12-hour time |
| **Time server (NTP)** | Primary NTP host name or IPv4 address (default `pool.ntp.org`; blank restores it). `time.nist.gov` stays as the fallback. Applied immediately, no reboot |
| **Time zone (POSIX TZ)** | Optional fixed time zone for the clock, e.g. `GMT0BST,M3.5.0/1,M10.5.0` (UK). Blank = automatic offset from Open-Meteo for the radar location. A set zone switches daylight saving at the exact minute and doesn't depend on the weather service |
| **Radar text size (%)** | Scales radar labels and footer text from 80–130%; default is 110% |
| **Device name (name.local)** | mDNS / DHCP host name, e.g. `kitchen-radar` → `http://kitchen-radar.local`. Lower-case letters, digits and hyphens. Saving a new name restarts the device; give each radar on a network its own name |
| **Settings password** | Login for all settings pages and firmware updates (username `admin`). Starts as a random password shown on the screen; at least 8 characters if you change it |

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Radar display

### Grid

- Dark blue background, subdued green rings and crosshairs
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius)
- White center dot

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |
| 50 km / 31 mi | ~66.7 km |
| 75 km / 47 mi | ~100 km |
| 100 km / 62 mi | ~133 km |

At the wide ranges only the 64 nearest aircraft are shown, and each fetch downloads more data, so it takes longer.

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

### Runways

- Major airports from OurAirports (`large_airport`); all open runway strips in range (helipads excluded)
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update the embedded list: `python3 scripts/build_large_airports.py`
- **Medium airports** (`medium_airport`, e.g. `EGLF`, `EGKB`) are downloaded instead of built in, for the area round your location only:
  - When the location is first set or moves more than 5 km, the radar streams OurAirports' `airports.csv` (~13 MB) and `runways.csv` (~4 MB) and keeps the nearest 64 medium airports within 140 km (the 100 km ring's corners plus margin) and their runways.
  - Takes about 1.5–3 minutes, with `UPDATING AIRPORTS` on screen; aircraft fetching and the web portal pause until it finishes. It waits until the portal has been unused for a minute, so saving a new location returns you to Setup first (the Saved page says the download is coming). A failed download is retried after 15 minutes.
  - Saved as `/airports.bin` (a few KB; London: 25 airports, 30 runways) on LittleFS in the `spiffs` partition, so it survives reboots and OTA updates.
  - Drawn as thinner grey runway lines (or a dot when OurAirports has no runway data) with an ICAO label.
  - Small airfields are not included: there are 400+ near London and most have no runway data.

### Aircraft

- **Inside the outer ring** — red heading triangle, magenta speed vector (clipped at the ring), route / detailed type / altitude / airline tags
- **Light and military aircraft** — light aircraft smaller and yellow, military green (rim dots too); can be turned off in Setup
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**. With overlap avoidance on (default), aircraft nearest the centre are tagged first and later tags move (above, below, other side), shrink to one line, or are left off rather than overlap
- **Route fallback** — the callsign is shown until route data arrives, and remains the fallback when no route is known
- **Detailed type** — aircraft data is compacted for the display (for example `Boeing 737-800` → `B737-800`)

The firmware version (release tag, `git describe`, or `dev` for local builds) is printed at boot and shown on the Setup home page, the **Info** page and the **Firmware update** page.

As range decreases (or aircraft approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

Origin/destination is not transmitted in ADS-B messages. The firmware enriches each active callsign through [ADSBDB](https://www.adsbdb.com/), rate-limits lookups, and caches successful results for six hours (misses for ten minutes). It first requests aircraft and route together, then retries the callsign-only endpoint when ADSBDB does not recognize the aircraft hex code. Route databases are based on known/scheduled callsigns, so private, repositioning, diverted, or recently changed flights may still have no route or an imperfect match.

### Weather and time

The bottom overlay uses the radar coordinates. Current conditions come from [Open-Meteo](https://open-meteo.com/) every 15 minutes; its location timezone offset and NTP provide the clock (until NTP syncs, the clock is set from the response's HTTP `Date` header; a failed weather fetch is retried after 60 s). The NTP server can be changed under **Setup** (for example to your router). By default the clock's UTC offset comes from Open-Meteo for the radar location; set **Time zone (POSIX TZ)** in Setup to use a fixed zone with exact daylight-saving changes instead. The two rows use plain-language conditions and label relative humidity (`RH`):

```text
OVERCAST 82F RH100%
21:45 25 JUL
```

Disable just the weather row or the complete footer in **Setup**. Radar and
footer text defaults to 110% and can be adjusted from 80–130% in the same page.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/`
- Route and aircraft enrichment: `https://api.adsbdb.com/v0/`
- Fetch radius: `ui::radar::fetchRadiusKm()` — scales with the active preset to roughly the screen edge (so rim dots have data)
- Poll interval: `kAdsbFetchIntervalMs` (3 s) in `config.h`
- Ground aircraft hidden by default (Setup checkbox; default `kAdsbShowGroundAircraft`)
- Up to 64 aircraft are shown; in busy airspace the **64 nearest** are kept
- The response is parsed one aircraft at a time as it arrives, so memory use doesn't grow with the number of aircraft in range (important at the 50–100 km ranges)
- If no fetch succeeds for 30 s (`kAircraftStaleMs`), aircraft are cleared and the radar shows **NO DATA** until data returns
- Settings saves and firmware uploads wait while an HTTP request is in progress (a few seconds at most), so they never run alongside a TLS session

### Local receiver (optional)

If you run your own ADS-B receiver (readsb, tar1090, dump1090-fa, ultrafeeder), the radar can read it instead of adsb.fi: **Setup → Aircraft → Receiver settings**.

- **Receiver address**: e.g. `192.168.1.50:8080` or `http://pi.local/tar1090/data/aircraft.json`. Without a scheme `http://` is added; without a path `/data/aircraft.json` is added.
- A plain `http://` address needs no TLS session, so about 30 KB more heap is free during the fetch. `https://` also works.
- The receiver sends every aircraft it hears; the radar applies the same area as the adsb.fi query and skips positions older than 30 s.
- **Use adsb.fi when the receiver can't be reached** (default on): after 3 failed reads in a row, adsb.fi is used for 5 minutes, then the receiver is tried again.
- The Receiver page shows the last read, aircraft count and any error.
- Route lookups (ADSBDB) and weather still use the internet. Light-aircraft recognition by type needs the receiver to include `t` (readsb with its aircraft database).

### Self-recovery

The device restarts itself if it can't recover on its own:

- **No data:** Wi‑Fi has been up for 10 min (`kNoDataRestartMs`) with no successful aircraft fetch. Repeated no-data restarts back off (10 → 20 → 40 → 80 min) so a long adsb.fi outage doesn't cause a reboot loop.
- **Low memory:** the largest free heap block stays under 20 KB (`kMinLargestFreeBlockBytes`) for three heap reports in a row.

The serial log at boot shows the reset reason, and `Self-recovery restart: …` after one of these.

The device sends the configured coordinates to adsb.fi and Open-Meteo, and sends active callsign/Mode-S identifiers to ADSBDB.

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` (default device name; mDNS needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |
| Flight enrichment | lookup interval, timeout, and cache durations |
| Weather | endpoint, request timeout, and refresh interval |
| Time sync | `kDefaultNtpServer` (default primary, overridable in Setup), `kFallbackNtpServer` |
| Defaults | initial OTA credentials |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

## Project layout

```
include/
  config.h
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    large_airports.h
  ui/
    radar_theme.h
    radar_range.h
    radar_projection.h     — lat/lon → km / screen pixels
    radar_display.h
    label_layout.h         — tag placement without overlaps (header-only, unit tested)
    runway_overlay.h
    status_screens.h
  services/
    wifi_setup.h
    radar_location.h
    adsb_client.h
    http_date.h            — HTTP Date header parser (backup clock)
    adsb_stream.h          — streamed JSON reader, field filter, nearest-64 (header-only, unit tested)
    display_settings.h
    ota_update.h
    weather_time.h
    local_airports.h       — medium-airport download and saved file
    portal_style.h         — shared portal theme, page-links bar, Setup tabs
    portal_pages.h         — Receiver, About and Forget Wi-Fi pages
    airport_area.h         — CSV reader, area filter, runway ends (header-only, unit tested)
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
scripts/
  build_large_airports.py
src/
  main.cpp
  data/
    large_airports_data.cpp
  hardware/
  ui/
  services/
test/
  stubs/                   — minimal Arduino/Preferences/GPIO headers for host builds
  test_adsb_stream/
  test_http_date/
  test_projection/
  test_range/
  test_settings/
```

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO env: **`supermini`** (default)
- Serial: **115200** baud. A memory line is printed every 60 s: `heap: free … min … largest … (lowest …) stack-free … up …`, and `heap low: N after <step>` whenever the lowest-ever free heap drops (names the request that caused it)
- USB CDC on boot enabled in `platformio.ini` for the Super Mini

### Tests and static analysis

```bash
pio test -e native            # host unit tests, no board needed
pio check -e supermini        # cppcheck over src/ and include/
```

The tests live in `test/` (one folder per suite; `test/stubs/` stands in for the few Arduino headers they need). CI runs both before every build.

### Heap soak test

`scripts/heap_monitor.py` logs the board's USB serial output for a set time. It saves every line plus a CSV of the once-a-minute `heap:` lines in `heap-logs/`, and prints a summary at the end: lowest free heap, lowest largest block against the 20 KB restart threshold, reboots, crashes and Wi‑Fi drops. Opening the port may reboot the board once.

```bash
mkdir -p heap-logs && nohup ~/.platformio/penv/bin/python scripts/heap_monitor.py --hours 4 --quiet > heap-logs/monitor.out 2>&1 &
```

Follow it with `tail -f heap-logs/monitor.out`. Stop it early with `pkill -f heap_monitor.py`, which still writes the summary. `--replay FILE` parses a saved log instead of the port.

### Crash dumps

After a crash (panic, watchdog, failed assert) the firmware saves a core dump to the `coredump` partition. Read and decode it over USB with:

```bash
~/.platformio/penv/bin/python scripts/read_coredump.py
```

It prints the crashed task, registers and backtrace, and keeps the raw dump in `crash-dumps/`. Decoding needs the `firmware.elf` that crashed. By default that's the latest local build; for a release build, pass the release's `.elf` with `--elf`. `--erase` clears the partition after reading, and `--file` decodes a saved dump. Reading flash resets the board, so don't run it during a soak test.

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and similar tools (ESP32-C3, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/plane-radar-merged.bin` (a copy of the platform's `firmware.factory.bin`). Skip rebuild if firmware is already built:

```bash
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/supermini/firmware-merged.bin`):

```bash
pio run -e supermini
pio run -t merge -e supermini
```

Put the board in download mode (hold **BOOT**, tap **RESET**), then flash with Chrome/Edge over USB.

### OTA firmware updates

The firmware uses two 1.75 MB application slots. After the OTA-capable partition table is installed, updates can be uploaded without USB:

1. Open `http://plane-radar.local`
2. Choose **Firmware update**
3. Sign in with username `admin` and the settings password
4. Upload the release file ending in **`-ota.bin`** (or, for your own build, `.pio/build/supermini/firmware-signed.bin`)
5. Keep power connected while the device writes flash and restarts

**Signed updates only:** the device checks the image's signature before installing it, and refuses unsigned or modified images and older release numbers. Plain `firmware.bin` is refused. Builds signed with your own key are covered in [SECURITY.md](SECURITY.md#update-signing-key). USB flashing never checks signatures, so it always works for recovery.

Devices upgraded from firmware that used the old shared default password `plane-radar` switch to a random password and show it on the screen once.

> **One-time migration:** firmware built with the old single-app partition cannot install this new partition table through app-only OTA. Flash the new **`-full.bin`**/merged image over USB once. Every later update can use the OTA image.

**Automatic rollback:** a newly installed firmware is only kept once it has fetched aircraft data successfully. If it resets before then, for example from a crash, a watchdog, a self-recovery restart or a power cut, the bootloader goes back to the previous firmware. The About page then says which update was rolled back. A Wi‑Fi or adsb.fi outage lasting through the 10-minute no-data restart straight after an update also rolls back; upload it again once the network is back. Firmware flashed over USB is never rolled back.

Never upload the merged/full image to the OTA form; it contains the bootloader and partition table and is only for flashing at offset `0x0`.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Unit tests + static analysis, then artifact `plane-radar-supermini` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release: `-full.bin`, signed `-ota.bin`, `.elf` (crash-dump symbols), CycloneDX SBOM (`.cdx.json`) and checksums |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

Releases need the `OTA_SIGNING_KEY` repository secret (the PEM private key); the workflow fails without it. Manual runs and branch builds are not signed and get no `-ota.bin`. The release workflow attaches both images. Use `-full.bin` at offset `0x0` for first install/recovery and `-ota.bin` in the device's authenticated firmware page.

## Security

See [SECURITY.md](SECURITY.md) for the support period, how to report a vulnerability, every network interface and endpoint, the update signing key, and known limitations.

- **Status** (`/status`, or `/status.json` for scripts): firmware version and build, update key ID, whether an update is waiting to be confirmed, uptime and reset reason, start-up time (Wi‑Fi joined, first aircraft data), heap, Wi‑Fi signal, data-feed health, failed logins. Linked from About; login required.
- **Update available:** release builds check the GitHub repository they were built from once a day. When a newer release exists, the Status page says so and links to it. Installing it is still up to you, using the signed `-ota.bin` on the Firmware update page.
- **Security** (`/security`): the last 24 security events (boots, logins, lockouts, settings changes, updates, resets), kept across restarts but cleared by a power cut. Below that are the key events (updates, rollbacks, password changes, lockouts, blocked cross-site requests, Wi‑Fi erased, factory reset, settings restored), which are kept in flash through power cuts.
- **Backup & restore** (`/backup`, linked from Status): download the settings as a JSON file, or restore them from one. The file never includes the settings password or Wi‑Fi. It does include your location, so keep it private.

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

Runtime data services:

- [adsb.fi](https://opendata.adsb.fi/) — nearby aircraft
- [ADSBDB](https://www.adsbdb.com/) — route and detailed aircraft data
- [Open-Meteo](https://open-meteo.com/) — current weather and local timezone offset

## Origins and credits

This firmware builds on two earlier projects, both MIT-licensed:

- [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar), the original project by Mateusz Juszczyk and contributors.
- [ironicbadger/ESP32-Plane-Radar](https://github.com/ironicbadger/ESP32-Plane-Radar), Alex Kretzschmar's fork, which added flight routes, weather and OTA updates. This repository starts from its `main` branch (commit `d0892a1`), and their commits are kept in the history.

Useful later changes from MatixYo were brought in by hand; [FIXES.md](FIXES.md) lists which ones. Everything after that point (security hardening, signed updates, Wi‑Fi reliability, the portal pages and tests) is described in [FIXES.md](FIXES.md) and [SECURITY.md](SECURITY.md). The original MIT licence and copyright notice are in [LICENSE](LICENSE).
