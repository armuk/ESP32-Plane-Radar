# Plane Radar — Necessary Fixes

Found by reading the code; nothing here has been compiled or tested on hardware yet.
Ordered by priority. Tick each box when the fix has been made and checked on the device.

## Priority 1 — Confirmed bugs

> **Status (2026-11-03):** Fixes 1–4 are written but haven't been compiled for the ESP32 or tested on hardware yet.
> - #2 also gives up on an upload after 30 s with no data, and redraws the radar as soon as an upload ends ([src/main.cpp](src/main.cpp)).
> - #3 uses the "better" option: a password check registered in front of all settings and maintenance pages (`SettingsAuthGate` in [src/services/ota_update.cpp](src/services/ota_update.cpp)). It applies on the home network only; the first-time setup hotspot stays open. Note: once a browser has logged in, it keeps sending the password automatically, so a malicious web page open in that same browser could still submit changes (CSRF). Fixed in T5.
> - #4 adds the shared helper [src/ui/radar_projection.cpp](src/ui/radar_projection.cpp), which both the radar and runway code now use.

### [x] 1. Weather line shows the wrong temperature
- **File:** [src/services/weather_time.cpp:201](src/services/weather_time.cpp:201)
- **Problem:** The temperature is rounded with `lroundf()`, which returns a whole number (`long`). It's then printed with `%.0f`, which expects a decimal number (`double`). That mismatch makes the output unreliable: the temperature will likely show as `0`, and the unit letter and humidity that follow get scrambled too.
- **Fix:** Print the rounded value as a whole number:
  ```cpp
  snprintf(out, out_len, "%s %ld%c RH%d%%", conditionLabel(s_weather_code),
           lroundf(temperature), unit, s_humidity_percent);
  ```
- **Check:** The footer shows a sensible temperature, unit and humidity in both °C and °F.

### [x] 2. A failed firmware upload freezes the radar
- **File:** [src/services/ota_update.cpp:89](src/services/ota_update.cpp:89) and the `UPLOAD_FILE_ABORTED` branch (~line 121)
- **Problem:** `s_in_progress` is set to true when an upload starts. Only `handleUploadDone()` sets it back to false, and the web server doesn't call that function when an upload is aborted (browser closed, Wi‑Fi drop, timeout). `loop()` returns early while the flag is true, so the screen stays on "Updating firmware" until the device is power-cycled.
- **Fix:** In the `UPLOAD_FILE_ABORTED` branch, set `s_in_progress = false` and redraw the radar. Also, if an upload starts but no new data arrives for about 30 s, clear the flag.
- **Check:** Start an OTA upload, close the browser tab partway through, and confirm the radar comes back.

### [x] 3. Anyone on the network can get around the firmware password
- **Files:** [src/services/wifi_setup.cpp:218](src/services/wifi_setup.cpp:218) (`handleSettingsSaved`), [src/services/display_settings.cpp:176](src/services/display_settings.cpp:176)
- **Problem:** `/paramsave` needs no password but can change the OTA password. An attacker can set a new password and then upload their own firmware. Nothing checks that a save really came from the Setup page, so any web page the user visits can also submit this change to `plane-radar.local`. The Wi‑Fi credentials page (`/wifisave`) has the same problem.
- **Fix (minimum):** Only accept a new OTA password if the request also includes the correct *current* password. Otherwise ignore it and keep the other settings.
- **Fix (better):** Require HTTP Basic auth (`admin` + OTA password) on `/param`, `/paramsave`, `/wifi` and `/wifisave` once the device is on the home network. Leave them open only on the first-time setup hotspot.
- **Check:** POSTing `ota_password=x` to `/paramsave` without credentials doesn't change the password.

### [x] 4. The map is stretched east–west
- **Files:** [src/ui/radar_display.cpp:233](src/ui/radar_display.cpp:233), [src/ui/runway_overlay.cpp:80](src/ui/runway_overlay.cpp:80)
- **Problem:** Both use 111 km for every degree of longitude. A degree of longitude actually shrinks with latitude (`111 × cos(latitude)` km), so at 52°N east–west distances come out about 1.6× too large. Aircraft and runways sit too far east or west, aircraft drop off the radar early on the east and west sides, and the check for which airports are in range is wrong too.
- **Fix:** Multiply the longitude difference by `cosf(center_lat * π/180)`. Move `offsetKmFromCenter` / `latLonToScreen` into one shared helper so both files use the same fix.
- **Check:** Runway layout at a known airport matches a map. Aircraft at equal distances to the north and east sit at equal radius on screen.

## Priority 2 — Likely problems (busy airspace or depends on server behaviour)

### [x] 5. Memory can run out on large aircraft responses
> **Done:** The aircraft JSON is parsed straight from the network (`PollingStreamReader`, which keeps the web portal responsive and times out) with a field filter. Host test with a 100-aircraft, 74 KB response: peak memory went from about 257 KB to about 54 KB (64-bit host; smaller on the ESP32).
- **File:** [src/services/adsb_client.cpp:540-575](src/services/adsb_client.cpp:540)
- **Problem:** The whole response is buffered as text in a `String` and then parsed into a full `JsonDocument`, while the off-screen screen buffer already takes about 115 KB of memory. Busy areas on the 25 km range can run out of memory (`NoMemory`), and updates silently stop.
- **Fix:** Parse straight from the network connection with an ArduinoJson filter so only the needed fields are kept: `lat`, `lon`, `true_heading`, `mag_heading`, `track`, `gs`, `alt_baro`, `alt_geom`, `flight`, `hex`, `t`. Log `ESP.getFreeHeap()` after each fetch while testing.

### [x] 6. Responses split into chunks aren't handled
> **Done:** `http.useHTTP10(true)` on the ADS-B, ADSBDB and weather requests.
- **File:** [src/services/adsb_client.cpp:69](src/services/adsb_client.cpp:69) (`readResponseBodyWithPoll`)
- **Problem:** The code reads the raw network data. If a server sends its reply in chunks (`Transfer-Encoding: chunked`), the chunk-size lines end up inside the JSON and parsing fails.
- **Fix:** Call `http.useHTTP10(true)` before `GET()` for the adsb.fi and ADSBDB requests. This also makes the server close the connection at the end of the reply, so the read loop finishes promptly.

### [x] 7. The 64-aircraft limit drops nearby aircraft
> **Done:** `NearestSet<kMaxAircraft>` ([include/services/adsb_stream.h](include/services/adsb_stream.h)) keeps the 64 nearest while parsing, replacing the farthest stored aircraft when a nearer one arrives. Serial logs `kept nearest X of Y` when it drops any.
- **File:** [src/services/adsb_client.cpp:583](src/services/adsb_client.cpp:583)
- **Problem:** The first 64 aircraft in the API's order are kept, not the 64 nearest.
- **Fix:** When the list is full, replace the farthest stored aircraft whenever a nearer one is found (or sort by distance after parsing).

### [x] 8. The route/model cache keeps evicting itself
> **Done:** `kEnrichmentCacheSize = kMaxAircraft`, enforced with a `static_assert`.
- **File:** [src/services/adsb_client.cpp:21](src/services/adsb_client.cpp:21) (`kEnrichmentCacheSize = 48`)
- **Problem:** The cache holds 48 entries but up to 64 aircraft are tracked, so with more than 48 aircraft, entries keep pushing each other out. The device then sends ADSBDB a lookup about every 0.75 s indefinitely.
- **Fix:** Make `kEnrichmentCacheSize` at least `kMaxAircraft` (each entry is about 50 bytes, so 64 entries is about 3 KB).

### [x] 9. Old aircraft stay on screen when fetches fail
> **Done:** After `kAircraftStaleMs` (30 s) without a successful fetch the list is cleared and the radar shows **NO DATA**. See also *Self-recovery restart* below.
- **Files:** [src/services/adsb_client.cpp:533](src/services/adsb_client.cpp:533), [src/main.cpp:76](src/main.cpp:76)
- **Problem:** If adsb.fi stops responding while Wi‑Fi is still up, the last aircraft stay on screen frozen, as if they were live.
- **Fix:** Record the time of the last successful fetch. After about 30 s without one, clear the list (`s_aircraft_count = 0`) or dim the aircraft and show a small "NO DATA" marker.

## Priority 3 — Minor

### [x] 10. Wi‑Fi connect timeout breaks after about 49.7 days of uptime
> **Done:** `waitForLinkWithUi` now measures elapsed time (`millis() - start`).
- **File:** [src/services/wifi_setup.cpp:427](src/services/wifi_setup.cpp:427)
- **Problem:** `deadline = millis() + attempt_ms; while (millis() < deadline)` breaks when the `millis()` counter wraps around.
- **Fix:** `const unsigned long start = millis(); while (millis() - start < attempt_ms)`.

### [x] 11. The weather request freezes the web page
> **Superseded by M3:** the web server is now deliberately *not* run during any HTTP request (ADS-B or weather), so requests wait up to a few seconds by design.
- **File:** [src/services/weather_time.cpp:118-127](src/services/weather_time.cpp:118)
- **Problem:** `http.GET()` and `getString()` wait without servicing the web page, for up to 6 s each.
- **Fix:** Reuse the polling read used by the ADS-B client (move it into a shared HTTP helper).

### [x] 12. A failed weather fetch isn't retried for 15 minutes
> **Done:** After a failure the next try is 60 s later (`kWeatherRetryMs`); after a success it stays at 15 min.
- **File:** [src/services/weather_time.cpp:168](src/services/weather_time.cpp:168)
- **Problem:** `s_last_attempt_ms` is updated even when the fetch fails, so the footer shows `WEATHER --` for 15 minutes.
- **Fix:** After a failure, retry sooner (for example after 60 s).

### [x] 13. The backup clock can be up to 15 minutes off
> **Done:** Until NTP syncs, the clock is set from the weather response's HTTP `Date` header (to the second), parsed by [include/services/http_date.h](include/services/http_date.h) and unit tested. Open-Meteo's `current.time` is only used if the header is missing.
- **File:** [src/services/weather_time.cpp:149](src/services/weather_time.cpp:149)
- **Problem:** If time servers are unreachable, the clock is set from Open-Meteo's `current.time`, which is rounded down to a 15-minute step.
- **Fix:** Use the HTTP `Date` response header instead (accurate to the second), or accept the error and leave a comment explaining it.

### [x] 14. README: "Hold BOOT at power-on"
> **Done:** Reworded, with a warning that holding BOOT at power-on/reset enters USB flashing mode.
- **File:** [README.md](README.md) (Controls section)
- **Problem:** GPIO 9 is the chip's boot-mode pin, so holding it at power-on starts the chip's USB flashing mode instead of running the firmware.
- **Fix:** Reword it to "hold BOOT for 3 s while the Connecting screen is shown".

### [x] 15. The "…" character for long Wi‑Fi names probably won't display
> **Done:** Uses `...` and never cuts inside a UTF-8 character; the line buffer now has room for a 32-byte SSID plus the dots.
- **File:** [src/ui/status_screens.cpp:121](src/ui/status_screens.cpp:121)
- **Problem:** The embedded font probably doesn't include the "…" character. Shortening the name can also cut a multi-byte (UTF-8) character in half.
- **Fix:** Use `"..."` instead, and only cut at character boundaries.

### [x] 16. Remove unused code
> **Done:** All three removed.
- ~~`isInsideOuterRing()` in radar_display.cpp~~ (removed: it triggered the only `-Wall` warning)
- `s_scale_label_max_w`, `s_scale_label_h` — [src/ui/radar_display.cpp:58](src/ui/radar_display.cpp:58)
- `kAdsbFetchRadiusScale` — [include/config.h:54](include/config.h:54)

## Memory review (2026-11-03)

No true leaks were found in project code or in the library code it calls (WiFiManager 2.0.17, ESP32 Arduino core 2.0.14, LovyanGFX 1.2.7). The risk is peak usage and heap fragmentation.

### [x] M1. HTTPS sessions stayed open during JSON parsing
- **Files:** [src/services/adsb_client.cpp](src/services/adsb_client.cpp), [src/services/weather_time.cpp](src/services/weather_time.cpp)
- **Problem:** HTTPClient keeps connections alive by default, so `http.end()` left the ~40 KB TLS session allocated while the JSON was parsed.
- **Done:** `http.setReuse(false)` on all three requests (ADS-B, ADSBDB, weather).

### [x] M2. No way to measure memory on the device
- **File:** [src/main.cpp](src/main.cpp) (`logHeapIfDue`), interval `kHeapLogIntervalMs` in [include/config.h](include/config.h)
- **Done:** A serial line every 60 s: `heap: free … min … largest … (lowest …) stack-free … up …`.
- **How to read it:** If `largest` falls over hours while `free` stays steady, that's fragmentation. If `free` falls steadily, that's a leak. If `stack-free` drops below about 1000, the loop task stack is close to overflowing.

### [x] M3. Firmware uploads can run in the middle of an ADS-B request
> **Done:** The poll hook used during HTTP I/O is now `wifiPollDuringIo()`, which only checks the BOOT button. Web requests (settings, uploads) wait until the fetch ends, then run from `wifiLoop()` in `loop()`.
- **Problem:** `pollNetwork()` → `wifiLoop()` runs the web server while an HTTPS read is in progress. An upload that arrives then is processed with the TLS session and partial response still allocated: the highest peak in both heap and stack.
- **Fix idea:** Only run `wifiLoop()` from the poll hook when no upload is starting, or defer heavy requests until the fetch finishes.

### [x] M4. Turning Wi‑Fi off and on during long outages
> **Done:** Retries use `WiFi.disconnect()` and reconnect. A full radio off/on happens only on the last attempt of every 5th failed round (`kWifiFullResetEveryRounds`), as a recovery step for a stuck radio.
- **File:** [src/services/wifi_setup.cpp](src/services/wifi_setup.cpp) (`tryConnectWithUi`)
- **Problem:** Each retry calls `WiFi.mode(WIFI_OFF)` and then reconnects. Some IDF versions leak a little per cycle. Not verified; check the heap log during a router outage.
- **Fix idea:** Retry with `WiFi.disconnect()` + `WiFi.begin()` without turning Wi‑Fi fully off.

### [x] M5. Free heap drops to ~15 KB during every HTTPS request (measured 2026-11-03)
> **Done (option a):** The frame sprite is now 8-bit (RGB332), 57.6 KB instead of 115 KB. Measured on the device: idle free heap ~77 KB → ~130 KB, low point during HTTPS 15.2 KB → 68.0 KB, largest idle block ~33 KB → ~63 KB. Trade-off: 256 colours, so anti-aliased text edges and colour shades are coarser. To go back, set `setColorDepth(16)` in `ensureFrameSprite()` in radar_display.cpp.
- **Measured on the device** with the new `heap low: … after …` log: boot 248 KB → Wi‑Fi 201 KB → first radar draw 89 KB (the 240×240 16-bit frame sprite, 115 KB, held permanently) → idle ~77 KB → **~15 KB at the peak of any single HTTPS request** (ADS-B 15.2 KB, weather 15.9 KB).
- **Cause:** the prebuilt Arduino core uses mbedTLS with 16 KB receive *and* 16 KB send buffers, plus the handshake and network buffers: about 62 KB per connection. Requests never overlap. No leak: idle free heap is steady.
- **Risk:** low but non-zero. A burst of incoming Wi‑Fi packets during a handshake could make an allocation fail; the fetch then fails and is retried, and the self-recovery restart covers lasting fragmentation.
- **Fix options:** (a) make the frame sprite 8-bit (RGB332): frees ~58 KB, but reduces colours to 256 and bands anti-aliased text; (b) render the frame in two 240×120 halves: keeps 16-bit colour, frees ~58 KB, but means changing all drawing code to use an offset; (c) leave as is and watch `heap low` and `lowest` over a few days.

(Item #5 above, parsing the aircraft JSON with a filter straight from the network, is the biggest remaining memory reduction.)

## Build safety (2026-11-03)

### [x] B1. Pin exact library versions
- **File:** [platformio.ini](platformio.ini)
- **Done:** LovyanGFX 1.2.33, WiFiManager 2.0.17 and ArduinoJson 7.4.2 are now pinned exactly (they were `^` ranges). LovyanGFX needs to be at least 1.2.24, because the code calls `loadFont(data, font_type)`; 1.2.7 doesn't compile. Several fixes depend on library internals that were checked against these versions, so re-check those before upgrading. (Since bumped: see T1.)
- **Verified (2026-11-03):** `pio run` builds with no warnings in project code. RAM 17.4% (57 KB static), flash 70.9% of the 1.75 MB app slot.

### [x] B2. Treat format mismatches as build errors
- **File:** [platformio.ini](platformio.ini) (`build_src_flags = -Wall -Werror=format`)
- **Done:** Applies to project sources only, not libraries. This would have caught the temperature bug (#1).

## Toolchain upgrade (2026-11-04)

### [x] T1. Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5, LovyanGFX 1.2.36, ArduinoJson 7.4.3
- **Files:** [platformio.ini](platformio.ini), [ota_update.cpp](src/services/ota_update.cpp), [weather_time.cpp](src/services/weather_time.cpp), [adsb_client.cpp](src/services/adsb_client.cpp), [local_airports.cpp](src/services/local_airports.cpp), [merge_firmware.py](scripts/merge_firmware.py)
- **Was:** `espressif32@6.5.0` = core 2.0.14 on ESP-IDF v4.4.6. The `-dirty` in that version string is baked into Espressif's precompiled SDK (`libesp_system.a`), not a local change.
- **Done:**
  - Platform is now pioarduino `55.03.312-1`. The official `espressif32` platform stops at core 2.x. It needs PlatformIO Core 6.2.0 or later; Core reporting `6.2.0-post.1` fails the version check and uninstalls the platform, so reinstall `platformio==6.2.0`.
  - `SettingsAuthGate` uses the core 3.x `RequestHandler` signatures (`const String&`, server-aware `canHandle`).
  - `startTimeSync()` holds the lwIP core lock around `sntp_enabled()`/`sntp_stop()`. Core 3.x enables core locking and asserts on unlocked calls; `configTime()` locks the same way.
  - `#include <WiFi.h>` where `WiFiClient` is used: `WiFiClientSecure.h` no longer pulls it in.
  - Dropped `-std=gnu++17` from the firmware env: core 3.x adds `-std=gnu++2a` after it, so it had no effect. The native test env keeps it.
  - Merge script runs `python -m esptool` with the esptool v5 spellings (`merge-bin`, `--flash-mode` ...). Its output is byte-identical to the platform's own `firmware.factory.bin`.
  - Two cppcheck false positives suppressed inline (linker blob size, grid ring loop).
- **Verified (2026-11-04):** build, merge, `pio test -e native` (62/62) and `pio check` pass. RAM 19.4% (63.5 KB static, was 57 KB). `firmware.bin` is 1,721,328 bytes, 93.8% of the 1.75 MB app slot (was ~1.48 MB): about 110 KB of headroom left.
- **Not yet verified on the device:** settings password gate and OTA upload, first-time setup AP, NTP server change, mDNS, display/font, and runtime heap (`heap:` log vs the 20 KB restart threshold).

### [x] T2. Platform modernisation
- **Rollback after a bad update** ([ota_update.cpp](src/services/ota_update.cpp), [main.cpp](src/main.cpp)): `verifyRollbackLater()` returns `true`, so a new OTA image stays `PENDING_VERIFY` until `confirmRunningFirmware()` runs after the first successful aircraft fetch. Any reset before then boots the previous image. `checkBootImage()` logs the state at boot and the About page shows a rolled-back update. Both the 3.3.12 bootloader and the old 2.0.14 one are built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, so this also works on boards updated over the air from the old firmware. A network outage that outlasts the 10-minute no-data restart right after an update also rolls back; that is deliberate.
- **CI pins PlatformIO Core 6.2.0** ([build.yml](.github/workflows/build.yml), [release.yml](.github/workflows/release.yml)). Releases now include `firmware.elf` for decoding crash dumps.
- **Merge target** ([merge_firmware.py](scripts/merge_firmware.py)) copies the platform's own `firmware.factory.bin` and fails if it is missing or older than `firmware.bin` (the platform only prints a message when its merge step fails).
- **Time sync** ([weather_time.cpp](src/services/weather_time.cpp)) uses `configTzTime()`, which sets the TZ and starts SNTP in one call, instead of `configTime()` + re-applying the TZ.
- **Crash dumps**: [read_coredump.py](scripts/read_coredump.py) reads the `coredump` partition over USB and decodes it with esp_coredump and the PlatformIO RISC-V gdb.
- About page library versions corrected (they still said LovyanGFX 1.2.33 / ArduinoJson 7.4.2).
- **Verified (2026-11-04):** build, merge, 62/62 host tests, cppcheck. `verifyRollbackLater` in the ELF is the override (`li a0,1; ret`). Script error paths tested with a blank and a corrupt dump. **Not yet on the device:** the OTA → confirm and OTA → crash → rollback paths, and decoding a real crash dump.

### [x] T3. NCSC device security principles (2026-11-04)
Measured against the NCSC [device security principles for manufacturers](https://www.ncsc.gov.uk/collection/device-security-principles-for-manufacturers). Full write-up in [SECURITY.md](SECURITY.md).
- **Unique settings password** ([display_settings.cpp](src/services/display_settings.cpp), [security.cpp](src/services/security.cpp), [main.cpp](src/main.cpp)): 10 random characters from the hardware RNG (entropy source on before Wi‑Fi starts), generated when none is saved or when the old shared default `plane-radar` is found. Shown on the screen after Wi‑Fi connects (`statusScreenSettingsPassword`) until BOOT is tapped or someone logs in, for at most 3 min per boot. A changed password must be 8–32 characters; a shorter one is ignored and logged.
- **Setup hotspot WPA2** ([wifi_setup.cpp](src/services/wifi_setup.cpp)): random 8-character password per boot, shown on the yellow setup screen in place of the "Open in browser" line.
- **Digest login, backoff, event log** ([security.cpp](src/services/security.cpp)): `requireAdmin()` replaces Basic auth everywhere. 5 free failures, then 30 s doubling to 15 min (HTTP 429 + `Retry-After`). A request without an `Authorization` header doesn't count as a failure. 24-entry security log in `RTC_NOINIT` memory, also printed on serial.
- **Signed updates, no downgrades** ([ota_update.cpp](src/services/ota_update.cpp), [firmware_image.cpp](src/services/firmware_image.cpp), [sign_firmware.py](scripts/sign_firmware.py)): `-DUPDATE_SIGN`, ECDSA P‑256 public key embedded from `keys/ota-signing-public.pem`, and `Update.installSignature()` before `Update.begin(size)`. The upload page sends the exact file size (`?size=`), because with an unknown size the hash would include the signature. The image's `PLANE-RADAR-FW-VERSION=` marker is read as it streams in, and an older release number is refused. The post-build script writes `firmware-signed.bin` and checks that the key matches the embedded public key. Release CI signs from the `OTA_SIGNING_KEY` secret.
- **Full factory wipe** ([wifi_setup.cpp](src/services/wifi_setup.cpp)): the BOOT long-press erases the whole NVS partition, the `spiffs` partition and the core dump, not only the keys it knew about.
- **Status and security pages** ([portal_pages.cpp](src/services/portal_pages.cpp)): `/status`, `/status.json`, `/security` (login required).
- **SBOM** ([make_sbom.py](scripts/make_sbom.py)): CycloneDX 1.5 from the installed platform, core, IDF `dependencies.lock`, mbedTLS/lwIP headers and `.pio/libdeps`; attached to releases and CI artifacts.
- **Cost:** firmware.bin 1,722,592 → 1,761,968 bytes (+39 KB; signed image 1,762,480 of the 1,835,008-byte slot, 72 KB spare).
- **Certificate checking (measured, not enabled):** the A/B build (`-e supermini_tls_ab`) showed checking costs about 1.4 KB more heap at the worst point and about 1 s more per new connection. It needs the 69 KB IDF root bundle, though, which leaves only 1.5 KB free in the app slot. See SECURITY.md, Known limitations.
- **Verified on the device (2026-11-04):** USB flash, boot and fetches (lowest free heap 23.9 KB). Every protected path answers 401 with a Digest challenge; `/` and `/about` stay open; an upload without a login gets 401. Five wrong logins, then 429 with `Retry-After`. Over the air: the unsigned image and a signed image with one byte changed were both refused; the signed image installed. **Not tested here:** the password notice screen (this device already had its own password).
- **Found on the device:** (1) the security log said "update (no version)": `__attribute__((used))` doesn't survive `--gc-sections`, so the version marker was missing from the image and the downgrade check could never fire. `checkBootImage()` now reads it, and `sign_firmware.py` fails the build if it is missing. (2) The first login after a restart was counted as a failure: the browser re-sends its answer to the previous boot's Digest challenge, and WebServer rejects it on the nonce/opaque before checking the password. Answers to anything but the current challenge are no longer counted. They can never succeed, so this gives a guesser nothing.
- **Re-verified on the device after both fixes:** "dev" → signed v2.0.0 installed and confirmed; signed v1.0.0 refused ("v1.0.0 is older than the installed v2.0.0"); signed dev installed and confirmed 4.6 s after the restart. No failed login after the restarts.
- **Measurement totals:** about 380 adsb.fi and 54 ADSBDB requests. With checking, the lowest free heap was 24.8 KB against 26.1 KB without (worst case +1.6 KB); median time per connection 1.6 s against 0.7 s.

### [x] T4. Slow start after a restart: joined the far access point (2026-11-04)
- **Symptom:** after some restarts (an update, or a USB reset) the radar showed NO DATA for 1-3 minutes. Wi-Fi connected with an address in about 2.6 s, then every DNS lookup failed (error -54) until "WiFi lost — will reconnect", after which everything worked within 2.5 s.
- **Cause:** the UniFi log showed the device joining the distant U6-Pro at -84 dBm rather than the one in the same room (-65 dBm). Both were on channel 11. The core's default `WIFI_FAST_SCAN` joins the first access point it hears with the right name, so which one won was down to chance; the link was too weak for anything but association and DHCP. The reduced TX power (8.5 dBm) makes the far uplink weaker still.
- **Fix** ([wifi_setup.cpp](src/services/wifi_setup.cpp)): `WIFI_ALL_CHANNEL_SCAN` + `WIFI_CONNECT_AP_BY_SIGNAL`, also written into the driver's stored STA config (the no-argument `WiFi.begin()` for the saved network and the core's auto-reconnects use that config, not the Arduino setting). Serial now logs `WiFi: joined <bssid> ch N, -NN dBm` and `WiFi: link down, reason N (name)`.
- **Verified:** 8 USB resets in a row, first aircraft after 5.4-7.1 s each (Wi-Fi about 1 s slower to join because of the full scan). Before: 1 slow start in 3 restarts.

### [x] T5. Cross-site request forgery protection (2026-11-04)
- **Problem:** a logged-in browser re-sends the Digest login with any request to the device, so a page on another site could make it save settings, erase Wi-Fi, restart or upload (noted under fix #3).
- **Fix** ([request_origin.cpp](src/services/request_origin.cpp), `requireSameOrigin()` in [security.cpp](src/services/security.cpp), `SettingsAuthGate` and the upload start in [ota_update.cpp](src/services/ota_update.cpp)): requests to `/wifisave`, `/paramsave`, `/receiversave`, `/erase`, `/restart`, `/exit`, `/close` and firmware uploads must carry an `Origin` (or, failing that, `Referer`) whose `http://host[:port]` equals the `Host` header. A missing source or `Origin: null` is refused. The check is by path, not method, because WiFiManager restarts/exits on a GET and `/wifisave` accepts GET. The WebServer only keeps `Authorization` unless told otherwise, so `Origin` and `Referer` are collected for each new server. Refusals get 403 and a security-log entry. Setup hotspot exempt (no login).
- **Tests:** `test/test_request_origin` (look-alike hosts, `user@host`, https vs http, port 80, null/hidden sources).
- **Verified on the device:** from a page on 127.0.0.1:8000 in the logged-in browser, a form POST (`Origin: http://127.0.0.1:8000`), a `rel=noreferrer` link (no headers) and a sandboxed-frame POST (`Origin: null`) to `/restart` were all blocked and logged. The radar did not restart. Before the login reached the check, the browser first showed a login prompt (stale Digest challenge).

### [x] T6. Web pages no longer stuck behind a failing request (2026-11-04)
- **Symptom:** a settings save sometimes took a long time to show "Saved". The handler itself takes 19 ms (Setup page 115-238 ms), but the device serves the web only between its HTTPS requests. Those normally take 0.7-2 s, but a failing one held the portal for 12-17 s: lwIP's blocking DNS lookup gives up after about 7 s, a failing request resolves twice, and the TLS handshake timeout defaulted to 120 s.
- **Fix:**
  - [net_dns.cpp](src/services/net_dns.cpp) starts each lookup with `dns_gethostbyname` and waits at most 3 s. A success lands in lwIP's DNS cache, so the request's own lookup is instant. On failure the request is skipped until the next cycle. IP addresses and `.local` names are passed straight through.
  - TLS handshake timeout 5 s (`tls_trust.cpp`).
  - adsb.fi connect timeout 5 → 4 s, read timeout 10 → 6 s.
  - Serial prints `web: <path> took N ms` for any handler over 500 ms.
- **Incident during this change:** calling `setHandshakeTimeout()` before the `if (verify_)` branch stopped the compiler from removing the certificate-bundle path, so the 69 KB root bundle was linked into the normal build. The image (1,840,016 bytes) no longer fit its 1,835,008-byte slot; PlatformIO's "Flash: 95%" did not catch it, and the board boot-looped ("doesn't fit in partition") until reflashed over USB. Now the bundle branch is compiled out when `PLANE_RADAR_TLS_VERIFY=0`, and `sign_firmware.py` fails every build whose image + signature exceeds the smallest app slot. It prints `App slot: N of 1835008 bytes` on every build. A consequence: the `supermini_tls_ab` measurement build (1,841,744 bytes) no longer builds.
- **Verified:** 78 host tests, cppcheck. On the device, 2 minutes after a reflash: 38/38 aircraft fetches, weather and 8 route lookups OK, no DNS skips. The size guard fails the oversized `supermini_tls_ab` build. The DNS skip path has not been seen on the device yet (it needs a DNS failure).

### [x] T7. Clean restarts, start-up time in the security log, quieter boot (2026-11-04)
- **Leave Wi-Fi before restarting** (`wifiLeaveAndRestart()` in [wifi_setup.cpp](src/services/wifi_setup.cpp)): disconnects (keeping the saved network), waits up to 300 ms for the link to drop, then restarts. Used after a firmware update, for self-recovery, a device-name change, and the menu's **Restart**. That needed our own `/restart` route ahead of WiFiManager's, which restarted abruptly; it also logs who restarted. Forget Wi-Fi and the factory reset already switch the radio off first. Verified: the menu Restart logged `WiFi: link down, reason 8 (ASSOC_LEAVE)` 5 ms before the reset, then the device rejoined and had data 5.7 s after boot.
- **"first data" security-log entry** ([main.cpp](src/main.cpp), `logFirstData()`): once per boot, e.g. `first data 5.9 s after boot (Wi-Fi 5.0 s, -62 dBm, 0 DNS skips)`, so slow starts are visible without USB. `services::net::skippedLookups()` counts requests skipped by the 3 s DNS limit (T6).
- **Boot log:** the two `nvs_open failed: NOT_FOUND` lines came from read-only Preferences opens of the `wifi` namespace, which only exists after a factory reset. NVS was initialised; the namespace just did not exist. The force-portal flag is now read with `nvs_open(NVS_READONLY)` + `nvs_get_u8` (Preferences stores bools as u8), which fails silently. Writes still use Preferences. Not re-tested: the factory-reset path that sets the flag.

### [x] T8. Wi-Fi steering (802.11k/v) and weak-signal rejoin (2026-11-04)
- **Steering:** the core's libraries are built with `CONFIG_ESP_WIFI_11KV_SUPPORT`. `preferStrongestAp()` now also sets `rm_enabled` and `btm_enabled` in the stored STA config, so an access point that supports BSS Transition can move the device while it is connected. MBO is left off (optional, and it changes connection behaviour). **Not verified:** whether UniFi actually steers it; that needs BSS Transition on for M-IoT and a reason to steer.
- **Weak-signal rejoin** (`wifiCheckSignal()` in [wifi_setup.cpp](src/services/wifi_setup.cpp), called from `loop()`): RSSI sampled every 30 s. 6 samples in a row below −78 dBm (`PLANE_RADAR_WEAK_RSSI_DBM`) → security log, then `WiFi.reconnect()` (leave + full scan, strongest first). The next sample logs `Wi-Fi rejoin: moved/same access point, now N dBm (was M)`. Minimum 15 min between rejoins, doubling to 4 h while the access point doesn't change. `wifiRejoinInProgress()` keeps the main loop's own reconnect (4 s grace) from starting a second reconnect before DHCP completes.
- **Verified with a test build** (threshold −40 dBm, so every sample was weak): the rejoin fired at 180 s, rejoined and logged "same access point" at 210 s. The first run showed a second, redundant reconnect, which is now fixed. Once, the rejoin's scan picked the far access point (`AUTH_EXPIRE` at −86 dBm) and the core's retry joined the near one 2.5 s later: full scans occasionally miss the strongest beacon. Refusing weak access points outright was not done, because a radar where every access point is weak could then never connect.

### [x] T9. Update notice, kept security events, settings backup, start-up time (2026-11-04)
- **Start-up time on the Status page:** a `startup` section with `wifi_joined_s` and `first_data_s` (the same figures as the "first data" log entry, T7), via `services::portal::noteStartup()`.
- **Key security events kept through power cuts** (`logKeyEvent()` in [security.cpp](src/services/security.cpp)): updates installed, refused and confirmed; rollbacks; settings password changed; login lockouts (now with the last address); blocked cross-site requests; Wi-Fi erased; factory reset; settings restored. Each is also written to NVS namespace `seclog` (one 84-byte blob per slot, newest 16, plus a counter), and the Security page shows them as "Kept through power cuts". Flash wear is bounded: at most 20 writes per boot, and an event identical to the newest kept one is not written again. A rollback is reported on every boot after it happens, so it uses `logKeyEventOnce()` (skipped if any kept event has the same text). A factory reset erases the kept log with the rest of NVS, then writes "factory reset: settings and logs erased" as its first new entry.
- **Settings backup and restore** ([settings_backup.cpp](src/services/settings_backup.cpp), pages `/backup`, `/backup.json`, `/restore`): the backup is every key in the `planeradar`, `radar` and `display` namespaces, each with its NVS type, so new settings are included with no extra code. The settings password (`otaPass`, `otaPassNew`), Wi-Fi, the security log and the portal flag are never exported or imported. Restore: login and same-origin check at the start of the upload (like the firmware upload), 4 KB limit, the whole file is checked before anything is written (types, integer ranges, text ≤ 200 characters, blobs ≤ 32 bytes), then a kept security event and a clean restart. The loaders still check each value at boot. Keys missing from the file keep their current values.
- **"Update available" notice** ([update_check.cpp](src/services/update_check.cpp)): about 2 minutes after boot and then every 24 h (1 h after a failure), the device asks `api.github.com/repos/<repo>/releases/latest` for the newest tag. The JSON reply is read through an ArduinoJson filter while it streams, so only `tag_name` and `html_url` are stored. When that tag is newer than the running release, the Status page shows a notice with a link to the release, and the event is logged once. It never downloads or installs anything. `<repo>` is fixed at build time: `PLANE_RADAR_RELEASE_REPO` (the release workflow sets it to `github.repository`), else the GitHub `origin` remote; with neither, the check is off. A `dev` build or an unparseable tag never shows the notice.
- **Cost:** about 24 KB of flash (the image is 1,795,072 of 1,835,008 bytes, so 39.9 KB spare). The heap low point was unchanged (20.6 KB); the GitHub request did not lower it.
- **Verified on the device** with a test build labelled `v1.1.4-test` that checks the upstream repository: the update check ran at about 2 minutes (`latest v1.1.5 ... update available`) and logged it; the new pages return 401 without the login. 9 new host tests (backup file checks, version rule).

### [x] T10. Weak-signal rejoin for hovering links; update check read the whole reply (2026-11-04)
- **Symptom:** the portal was very slow (two loads hung for 30 s). The security log showed the radar had joined a weak access point at boot, and no rejoin followed.
- **Rejoin trigger** ([weak_signal.h](include/services/weak_signal.h)): T8 rejoined after 6 weak samples *in a row* (30 s apart, below −78 dBm). A link hovering around the threshold never fired, because one better reading reset the count. It now rejoins when at least 4 of the last 6 samples are weak. That still takes 2 minutes after a weak join, while an occasional dip on a good link does not trigger it. The log line now reads `Wi-Fi weak (-NN dBm, 4 of last 4 samples): rejoining`. 7 host tests. Verified with a −40 dBm test threshold: the rejoin fired after 4 samples, left with `ASSOC_LEAVE` and rejoined in about 1 s. The core logs `sta is connecting, cannot set config` during that rejoin (its auto-reconnect overlaps `WiFi.reconnect()`); this is harmless.
- **Update check failed part-way** ([update_check.cpp](src/services/update_check.cpp)): `JSON error IncompleteInput after 5503 of 12028 bytes` in 29 ms. `WiFiClientSecure::read()` returns −1 whenever the next TLS record has not arrived, and the JSON parser took that as the end of the reply. The upstream reply (6.5 KB) usually fitted before such a gap; the reply from this repository (12 KB) usually did not. A small reader now waits for data, up to the request timeout, until the server has closed and everything is read. It reads through a 128-byte buffer and logs the bytes read. Verified: 5 restarts in a row each read all 12,028 bytes on the first attempt (51–154 ms). **v2.0.1 and v2.0.2 as released have this bug**: their check often fails and retries hourly.
- `PLANE_RADAR_UPDATE_FIRST_MS` and `PLANE_RADAR_UPDATE_RETRY_MS` let test builds run the check sooner.
- One boot took 45 s to its first data: every HTTPS connect timed out for 40 s after joining, though DNS worked. The next 6 boots took 5.2–9.5 s, so this looks like a one-off on the network side.

### [x] T11. New partition layout and HTTPS certificate checking (2026-11-04, v2.1.0)
- **Partition layout 2** ([partitions/plane_radar.csv](partitions/plane_radar.csv)): app slots 0x1C0000 → 0x1E0000 (1.75 → 1.875 MB each). The LittleFS store shrank from 384 KB to 128 KB: it holds only `/airports.bin` (at most 64 airports and 192 runways, about 5 KB, plus a temporary copy while saving). NVS, otadata and coredump keep their offsets.
- **Certificate checking on by default** (`PLANE_RADAR_TLS_VERIFY` now defaults to 1, in [tls_trust.h](include/services/tls_trust.h)): ESP-IDF's built-in root bundle. The image is 1,866,992 of 1,966,080 bytes, so 99 KB spare (was 40 KB without the bundle). A refused certificate (an mbedTLS X509 or verify error) is written to the RTC security log once per host per boot. Expiry dates are not checked (`MBEDTLS_HAVE_TIME_DATE` is off in the core), so checking does not depend on the clock at boot.
- **Old-layout guard** ([ota_update.cpp](src/services/ota_update.cpp)): an image larger than the update slot is refused at the start of the upload, with a message pointing to the README's "Partition layout" section instead of "Not enough space". v2.0.x devices already refuse the larger image (`Update.begin` fails), with the generic message.
- **Migration:** `pio run -t upload` keeps NVS (verified: Wi-Fi, settings and password survived). The release `-full.bin` fills 0x9000–0xE000 with 0xFF (checked in `firmware.factory.bin`), so web-flashing it erases settings and Wi-Fi; the README says to back up first and restore after. The old LittleFS data does not mount at the new offset (`Corrupted dir pair`, logged once by the core), so it is reformatted and the airports are downloaded again.
- **Verified on the device with checking on:** adsb.fi, ADSBDB, Open-Meteo, GitHub Pages (a 12.7 MB airports download plus a 4 MB runways download) and the GitHub API (update check, 12,028 bytes) all succeeded; first data 6.3 s after boot. The earlier A/B measurement covered adsb.fi and ADSBDB over about 400 requests.
- **Heap during the airports download:** the low point is about 12 KB, whether certificate checking is on or off (A/B with the file store erased: 12,520 B off, 11,972 B on). This was already the case but had not been measured; that download is the radar's lowest point, against about 20 KB in normal running. The largest free block stayed at 55 KB, so the self-recovery restart (largest block < 20 KB) is not near. Candidate for later: smaller download buffers, or pausing route lookups while it runs.

### [x] T12. Memory: screen buffer only while drawing; route lookups free early (2026-11-04)
- **Where memory went (measured):** about 215 KB free after boot; Wi-Fi takes about 55 KB, and the 8-bit screen frame (240×240) 57.6 KB, leaving about 90 KB free while idle. One HTTPS request costs 57–66 KB (median over about 400 requests in the TLS A/B logs), so the low point was about 20 KB, and about 12 KB during the airports download. Static RAM is 64 KB in total; the project's own share is about 17 KB (aircraft list 6 KB, route cache 4.3 KB).
- **Screen frame allocated per redraw** ([radar_display.cpp](src/ui/radar_display.cpp)): the panel keeps the image, and redraws only happen in `loop()`, never during a request. So the sprite is now created for each frame and deleted after `pushSprite`, which gives its 57.6 KB to every request. If the allocation fails (a fragmented heap), the existing fallback draws straight to the panel, and `radar: frame alloc failed (N so far), largest block B` is logged.
- **Route lookups** ([adsb_client.cpp](src/services/adsb_client.cpp)): the parsed first ADSBDB reply is now freed before the fallback callsign request, instead of being held through a second TLS session.
- **Verified on the device** (10 minutes, 190 aircraft fetches, 6 route lookups): the low point rose from about 20 KB to **62.7 KB**, and idle free memory from about 90 KB to about 148 KB. The largest free block held steady at 114.7 KB, so there was no fragmentation and no failed frame allocation. A forced airports download (file store erased) set no new low, against about 12 KB before.
- **Not done: smaller TLS buffers.** The core allocates two 16 KB TLS buffers per connection (`MBEDTLS_SSL_MAX_CONTENT_LEN=16384`). A 4 KB send buffer, plus a 4 KB esp_timer stack, would save about 12.5 KB per request and 4 KB permanently, but only through pioarduino's `custom_sdkconfig`. That rebuilds the shared `framework-arduinoespressif32-libs` package in `~/.platformio`, so other projects reinstall it, and CI rebuilds it on every run. It is kept in reserve for a feature that needs the memory.
- Also considered and not done: a 4-bit frame or drawing in strips (29–43 KB permanently, at the cost of colours or about twice the drawing time); trimming the aircraft list or route cache (a few KB, changes behaviour); portal page buffers (they never coincide with a request).

## Reliability and tooling (2026-11-03)

### [x] R1. Self-recovery restart
- **File:** [src/main.cpp](src/main.cpp) (`checkNoDataRestart`, `checkLowHeapRestart`), limits in [include/config.h](include/config.h)
- **Done:** The device restarts itself when
  - Wi‑Fi has been up for 10 min (`kNoDataRestartMs`) with no successful aircraft fetch. Each consecutive no-data restart doubles the wait (10, 20, 40, then 80 min max) so a long adsb.fi outage doesn't cause a reboot loop; and
  - the largest free heap block stays below 20 KB (`kMinLargestFreeBlockBytes`, too small for TLS) for 3 heap reports in a row.
- The reason survives the restart (RTC memory) and is printed at boot with the reset reason: `Self-recovery restart: …`.

### [x] R2. Allocation-free heap log
- **Done:** The heap line is formatted into a stack buffer with `snprintf` and sent with `Serial.write`; `Serial.printf` would heap-allocate for a line this long.

### [x] R3. Unit tests
- **Files:** [test/](test/), `[env:native]` in [platformio.ini](platformio.ini)
- **Done:** 35 host-side Unity tests: projection, range selection, aircraft filter and device-name settings, HTTP `Date` parsing, NTP/time-zone validation and saving, the streamed + filtered aircraft parse (timeouts, Content-Length, empty input), and nearest-64 selection. Run with `pio test -e native`; CI runs them before building.

### [x] R4. Static analysis
- **Done:** `pio check -e supermini` (cppcheck, project sources only; library headers suppressed). Fixed what it found: shadowed `lat`/`lon` in radar_location.cpp, a duplicated `if` in wifi_setup.cpp, a raw loop in ota_update.cpp; one false positive suppressed inline in display_settings.cpp. CI fails on any new finding.

### [x] R5. More settings on the Setup page
- **Done:** Range dropdown, show ground aircraft, lowest/highest altitude, route lookups on/off, device name (restarts on change). All validated in firmware, saved to NVS, cleared by the long BOOT press, and unit tested. The Setup form now uses one save function for both save paths.

## Taken from upstream MatixYo/ESP32-Plane-Radar (2026-11-04)

### [x] U1. TLS connect retry storm (upstream #94, #104)
- **File:** [src/services/adsb_client.cpp](src/services/adsb_client.cpp)
- **Was:** `performGetWithPoll` used a 200 ms connect timeout and retried `GET()` every 5 ms on "connection refused" until the 10 s deadline. mbedTLS reports "no heap for TLS buffers" the same way, so a low-memory moment became ~170 failed TLS setups per fetch.
- **Done:** `performGet` makes one attempt with a 5 s connect limit (capped at the request timeout). The next fetch or lookup retries. Used by both the ADS-B fetch and the ADSBDB lookup.

### [x] U2. 32-character Wi‑Fi network names never connected (upstream #76)
- **File:** [src/services/wifi_setup.cpp](src/services/wifi_setup.cpp)
- **Was:** WiFiManager's `getWiFiSSID()` builds a `String` straight from ESP-IDF's 32-byte SSID field, which has no NUL when full, so a 32-character name read on into the password.
- **Done:** `readStoredWifiCredentials()` copies the SSID (32 bytes) and password (64 bytes) into terminated buffers.

### [x] U3. Integer overflow in runway clipping (upstream PR #92, open)
- **File:** [src/ui/runway_overlay.cpp](src/ui/runway_overlay.cpp)
- **Was:** `segmentIntersectsDisc` computed `b*b - 4*a*c` in `int`; at small ranges `b*b` passes 2^31, so runways near the rim could be drawn or dropped wrongly.
- **Done:** Computed in `float`.

### [x] U4. Runway overlay measured every airport each frame (upstream PR #104, open)
- **File:** [src/ui/runway_overlay.cpp](src/ui/runway_overlay.cpp)
- **Was:** A `cosf`/`sqrtf` distance for every one of 1,706 runways per frame; an out-of-range airport was measured again for each of its runways. Upstream measured 71 ms per frame on the C3.
- **Done:** A square around the range in 1e-7° units rejects far airports with integer compares (antimeridian-safe, no longitude limit near the poles), and each airport is measured at most once per frame.

## Upstream feature requests (2026-11-04)

### [x] F1. Wider ranges (upstream #110, PR #105)
- 50, 75 and 100 km presets (ring label), on the BOOT tap cycle and the Setup dropdown. Dropdown buffer 480 → 640 bytes.

### [x] F2. ADS-B response parsed one aircraft at a time (needed for F1)
- **File:** [include/services/adsb_stream.h](include/services/adsb_stream.h) `forEachAircraft()`
- **Was:** the whole filtered `ac` array was held in a `JsonDocument` (~250 B per aircraft) while the TLS session was open. At 100 km over a busy area (300+ aircraft) that is 60–90 KB, more than the free heap.
- **Done:** finds the `"ac"` key, then deserializes each aircraft on its own and hands it to the nearest-64 selection. A parse error keeps the aircraft already read (all slots 0..n-1 are fresh). Measured lowest free heap 67.3 KB (was 65.6 KB).

### [x] F3. Firmware version (upstream #64)
- `scripts/firmware_version.py` sets `PLANE_RADAR_VERSION` from the `PLANE_RADAR_VERSION` env var (the release workflow passes the tag), `git describe`, or `dev`. Shown in the boot log, on the Setup home page, the Info page (added by a small script, as WiFiManager has no hook there) and the Firmware update page.

### [x] F4. LED off (upstream #22)
- Nothing to do: the firmware never drives the blue LED on GPIO8, and the red power LED is wired permanently.

### [x] F5. Compass rotation (upstream #34, PR #111)
- Setup: compass bearing at the top of the screen. `ui::radar::setUpBearingDeg()` rotates `latLonToScreen()` (aircraft, runways, labels), rim dots, headings, track vectors, the crosshairs and the N/E/S/W letters. Distances and the nearest-64 selection are unchanged.

### [x] F6. Light and military aircraft (upstream PR #95, PR #61)
- ADS-B `category` A1 / B1 / B4 → light (65% size, yellow); adsb.fi `dbFlags` bit 0 → military (green). Rim dots use the same colours. One Setup checkbox turns both off. +1 byte per aircraft.
- **Follow-up:** about 40% of light aircraft near London send no emitter category (older transponders), so a P28A stayed red. `classifyAircraft()` now also checks the ICAO type against ~75 common light types; in a live sample, light aircraft found went from 34 to 60 of 204, with no airliners caught.

### [x] F7. Airline names (upstream PR #38)
- From ADSBDB's `flightroute.airline.name`, already in the route lookup response: no extra requests or tables. Cached with the route; a fourth tag line when known. Setup checkbox. +2 KB static RAM (16 bytes × 64 aircraft + 64 cache entries).

### [x] F8. Night screen-off (upstream #28, #8)
- Setup: on/off plus off-from / back-on hours (local time, same zone as the footer clock). The panel is blanked and put to sleep and radar drawing is skipped; fetching continues so the self-recovery checks still work. A BOOT tap wakes it for a minute; firmware uploads always wake it. Dimming needs the backlight (BL) pin wired to a GPIO, which our wiring doesn't have.

### [x] F9. Medium airports, downloaded for the local area (upstream #66, PR #103)
- **Files:** [src/services/local_airports.cpp](src/services/local_airports.cpp), [include/services/airport_area.h](include/services/airport_area.h), `drawLocalAirports()` in [src/ui/runway_overlay.cpp](src/ui/runway_overlay.cpp)
- Instead of building every medium airport in (thousands worldwide, ~100+ KB of flash), the radar downloads OurAirports' CSVs when the location is set or moves more than 5 km, keeps the nearest 64 medium airports within 140 km and their runways, and saves them to LittleFS in the unused `spiffs` partition. Setup checkbox plus a status line.
- Measured at 51.47, -0.45: 25 airports and 30 runways, 184 s (earlier runs 91–97 s), lowest free heap 57.4 KB during the download. A reboot loads the saved file without downloading again.
- Small airfields removed: 400+ near London, mostly without runway data.
- **Finding:** writes to LittleFS made while a long TLS stream was open came out as a 0-byte file, even though each write reported success. Records are now collected in RAM and written in one go after both downloads, then the file size is checked before it replaces the old file.

### [x] F10. Tags no longer overlap (upstream #9)
- **Files:** [include/ui/label_layout.h](include/ui/label_layout.h), `drawAircraftTag()` in [src/ui/radar_display.cpp](src/ui/radar_display.cpp)
- Every aircraft symbol is marked as taken, then tags are placed nearest the centre first. Each tries six spots (beside toward the centre, raised, lowered, then the same on the other side); if none is free it tries a one-line tag, then no tag. Setup checkbox, default on.
- Cost: 1 KB static RAM (128 rectangles × 8 bytes), a few hundred bytes of flash; at most 64 × 6 × 128 rectangle checks per frame.

### [x] F11. Optional local receiver (upstream #48)
- **Files:** `fetchAircraft()` / `fetchUpdate()` in [src/services/adsb_client.cpp](src/services/adsb_client.cpp), [src/services/portal_pages.cpp](src/services/portal_pages.cpp), receiver settings in [src/services/display_settings.cpp](src/services/display_settings.cpp)
- Reads a readsb / tar1090 / dump1090-fa `aircraft.json` over plain HTTP. The stream parser now accepts the `"aircraft"` key as well as adsb.fi's `"ac"` (it reads whole key strings, so a value or longer key can't match). Distance filter and a 30 s position-age limit applied on the device. Falls back to adsb.fi for 5 minutes after 3 failures (optional). Own page under Setup, with a status line.
- Not tested against a real receiver here (none on this network); the parser is tested with a readsb-shaped response.

### [x] F12. Setup reorganised; Erase moved; About page
- Setup in six tabs (client-side, one form, so the save path is unchanged and the hotspot portal still works). Inline hint styles replaced by a `hint` class.
- **Erase:** WiFiManager showed a one-click "Erase WiFi config" button on Info that ran on a plain GET. It is hidden; `/erase` is now ours: GET shows a confirmation, only a POST with `confirm=yes` erases, and it erases only the Wi-Fi network (the BOOT long-press still does a full reset). Linked from the bottom of the Wi-Fi page.
- **About:** versions, libraries with licences and links, data sources. Built with `-DWM_NOHELP`, which drops WiFiManager's help table and GitHub link from Info.
- Cost: about 12 KB of flash for F10–F12 together (76.2% used); no static RAM beyond F10's 1 KB and the receiver settings (~130 bytes).

## Hardening (optional)

- Certificates aren't checked on HTTPS requests yet: measured, and blocked by flash space (see T3).
- ~~The OTA password is sent over plain HTTP.~~ Digest login since T3; the web interface itself is still plain HTTP (SECURITY.md).
- ~~Make the default OTA password device-specific.~~ Random per device since T3.
