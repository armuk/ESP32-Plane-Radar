# ESP32-P4 port plan

Target: **Waveshare ESP32-P4-WIFI6-Touch-LCD-4C**. It has a 4" round 720×720 IPS screen on 2-lane MIPI-DSI, GT9271 capacitive touch (10 points, I2C), 32 MB PSRAM and 32 MB flash, and Wi-Fi 6 and Bluetooth through an on-board ESP32-C6 (ESP-Hosted). It also has a microSD slot, audio and a real-time clock with a backup-cell header.

This folder is a copy of the C3 Super Mini firmware (v2.2.1). Most of it carries over; the work is in the display, touch, task layout and board specifics.

## What carries over unchanged (about 70% of the code)

| Area | Files |
|---|---|
| Aircraft data, routes, airports, weather | `services/adsb_client`, `adsb_stream`, `airport_area`, `local_airports`, `weather_time`, `radar_location`, `data/large_airports_data` |
| Web portal, settings, backup | `services/portal_pages`, `portal_style`, `display_settings`, `settings_backup`, `request_origin` |
| Security and updates | `services/security`, `ota_update`, `firmware_image`, `update_check`, `tls_trust`, `net_dns` |
| Radar maths | `ui/radar_projection`, `ui/radar_range`, `ui/label_layout` (the layout logic; its pixel sizes change) |
| Tests | `test/` (all 95 host tests) |

## The board (from Waveshare's schematic)

Source: [ESP32-P4-WIFI6-Touch-LCD-XC schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-XC/ESP32-P4-WIFI6-Touch-LCD-XC-Schematic.pdf) (shared by the 3.4C and 4C, dated 2025-05-24), and the [ESP32-P4 datasheet](https://documentation.espressif.com/esp32-p4_datasheet_en.pdf) v0.7.

| Function | Connection |
|---|---|
| Chip | ESP32-P4NRW32: 32 MB PSRAM in the package; 32 MB flash (GD25Q256) |
| Display | 2-lane MIPI-DSI on a 32-pin connector. LCD reset **GPIO27**; backlight boost enable (`BL_EN`) **GPIO33**; brightness PWM (`LCD_BL_PWM`) **GPIO26**. The panel's controller IC is not named. |
| Touch | Shares the I2C bus (SDA **GPIO7**, SCL **GPIO8**); reset **GPIO23**. Its interrupt line only goes to a test pad (TP2), **not to a GPIO**, so touch must be polled. |
| Wi-Fi (ESP32-C6-MINI-1) | SDIO: CLK **GPIO18**, CMD **GPIO19**, D0 **GPIO14**, D1 **GPIO15**, D2 **GPIO16**, D3 **GPIO17**; C6 reset (`CHIP_PU`) **GPIO54**; C6 IO2 from **GPIO6**. These match Arduino-ESP32's default ESP-Hosted pins for the P4. The C6's own UART is on header J4, for reflashing it. |
| Buttons | BOOT **GPIO35** (a strapping pin; fine as an input after boot); RESET is the chip enable. |
| USB | "UART" USB-C: CH343P bridge on **GPIO37** (P4 TX) / **GPIO38** (P4 RX), with auto-reset like the C3. "USB" USB-C: the P4's USB Serial/JTAG on GPIO24/25. A USB-A host port (high-speed OTG) is unused. |
| Clock and RTC | 40 MHz and 32.768 kHz crystals. Header J1 feeds `VBAT` for a **backup cell**, so the real-time clock keeps time through a power cut. |
| Storage | microSD slot (SDMMC 4-bit on GPIO39–44; card power switched by GPIO45). |
| Audio | ES8311 codec + NS4150B speaker amplifier (amp enable GPIO53); ES7210 microphone ADC (I2C 0x40) with two microphones; I2S on GPIO9–13. |
| Camera | MIPI-CSI connector (unused). |
| Power | 5 V from USB-C; 3.3 V regulator (3 A); 1.2 V core regulator. **No LiPo charger or battery-level sense** on this schematic. |

### What this changes in the plan
- **Two chip revisions.** The datasheet now lists only revision **v3.x** for this part number (`ESP32-P4NRW32X`), while earlier boards carry v1.x. Firmware built for one does not run on the other (`board = esp32-p4` vs `esp32-p4_r3`). Read the revision at bring-up, and if boards differ, build and release both.
- **Portable mode needs extra hardware.** There's no battery charger: a USB power bank works as is; a LiPo needs a charger and 5 V boost module. The `VBAT` backup cell only keeps the clock running, which is still useful: the time is right at boot, before NTP.
- **Touch is polled** (about every 20 ms over I2C), since its interrupt isn't wired.
- **One I2C bus** serves touch, the audio codec and the microphone ADC, so all drivers share one bus handle.
- **The microSD slot** can hold large data: worldwide airports, a cache of aircraft photos and logos, longer logs.

## What changes

### 1. Board bring-up (first, when the board arrives)
- [ ] **Chip revision:** `esptool chip-id` prints it; pick `esp32-p4` or `esp32-p4_r3` to match.
- [ ] **Display controller and init sequence:** not on the schematic. Take them from Waveshare's 4C demo code (the 4B uses an ST7703).
- [ ] **Serial:** use the "UART" USB-C (CH343P, UART0), so set `ARDUINO_USB_CDC_ON_BOOT=0`. The native USB port is an option later.
- [ ] **Wi-Fi:** the C6's ESP-Hosted firmware version matches what Arduino-ESP32 3.3 expects; join a network.
- [ ] **BOOT button:** `kBootPin` becomes GPIO35.
- [ ] **A "hello" build:** join Wi-Fi, fill the screen with colour, print touch coordinates on serial.

### 2. Display layer: keep LovyanGFX for drawing, drive the panel with esp_lcd
LovyanGFX has no driver for this MIPI-DSI panel, but its sprites (off-screen canvases) don't care which panel the image ends up on. So:
- Drive the panel with ESP-IDF's `esp_lcd` MIPI-DSI (DPI) driver and the controller's init sequence, which goes in `hardware/`, replacing `lgfx_config.hpp`.
- Draw the radar into a 720×720 16-bit `LGFX_Sprite` in PSRAM (about 1 MB), then hand it to the panel. Use two frame buffers, so the screen never shows a half-drawn frame.
- The existing drawing code (`radar_display`, `runway_overlay`, `status_screens`) keeps its LovyanGFX calls; only its target changes.
- Drop the C3's memory workarounds: an 8-bit sprite, and freeing the sprite after every frame.
- **LVGL instead?** It would suit widget-heavy screens (scrolling lists, settings on the device) but means rewriting all the drawing. Start with LovyanGFX, and revisit if the touch screens outgrow it.

### 3. Scale the layout from 240 to 720 pixels
- `ui/radar_theme.h` and `config.h` hold fixed pixel sizes (ring radius 107, aircraft symbol 8 px, label heights). Derive them from `kSize` with a scale factor, so one set of numbers serves both boards.
- Fonts: embed larger smooth (VLW) fonts at the sizes needed (for example 36 and 45 px) instead of scaling the 15 px one; flash is plentiful. The `FreeSans` bitmap fallbacks pick by pixel size already.
- Revisit what a bigger screen should show: longer labels (registration and type), a legend, a clock, and weather that is easier to read.

### 4. Network on its own task (uses the second core)
On the C3, one loop does everything, so the screen and web pages wait while an HTTPS fetch runs (about 1–2 s each). With touch, that would feel broken.
- Run fetches (aircraft, routes, weather, airports, update check) in a FreeRTOS task on one core; touch, drawing and the web portal on the other.
- Share the aircraft list as a snapshot behind a mutex (swap a pointer after each fetch), so drawing never sees a half-updated list.
- Remove the "poll while waiting" callbacks the C3 needs (`setPollFn`, `wifiPollDuringIo`).

### 5. Touch
- **Driver:** GT9271 over I2C (Goodix protocol, close to the GT911). Its interrupt line isn't wired, so poll it about every 20 ms from the UI task. Small; written here or taken from `esp_lcd_touch`.
- **Gestures:** tap, long-press, swipe, and two-finger pinch.
- **Hit-testing:** keep each drawn aircraft's and airport's screen position from the last frame; a tap picks the nearest within a finger-sized radius (about 40 px).
- **Aircraft card:** callsign, registration, type, airline, route, altitude and trend, speed, distance and bearing; later a photo and logo (PSRAM makes caching these easy). Closes on tap or after a timeout.
- **Airport card:** name, ICAO/IATA codes, runways, current METAR.
- **Range:** pinch or swipe instead of the BOOT button; the button stays as a fallback.

### 6. Board and release specifics
- [ ] **Partitions** for 32 MB: two update slots of 6–8 MB, a file store of 16 MB or more (airports, icons, offline map data), and the core dump.
- [ ] **Board marker:** put `PLANE-RADAR-BOARD=p4-lcd-4c` in the image next to the version marker, and refuse an OTA image for another board.
- [ ] **Update check:** list releases and pick the newest `p4-v*` tag (not `releases/latest`, which belongs to the C3); download `plane-radar-p4-vX.Y.Z-ota.bin`.
- [ ] **Release workflow:** `release-p4.yml` on `p4-v*` tags, with `make_latest: false`.
- [ ] **C6 firmware:** report its version on the Status page; plan how it gets updated.
- [ ] **Wi-Fi checks over ESP-Hosted:** setup hotspot and captive portal, `WiFi.RSSI`, `BSSID`, `setTxPower`, `setSleep`, and the `esp_wifi_get/set_config` calls in `wifi_setup.cpp`. Remove the `CONFIG_ESP32_PHY_MAX_WIFI_TX_POWER` stand-in.
- [ ] **Night mode:** backlight off (`BL_EN` GPIO33 low; dim with PWM on GPIO26), and less frequent fetches.
- [ ] **Clock:** use the RTC (kept by the backup cell) at boot, then NTP.

### 7. Later
- **Portable mode:** on a USB power bank, or a LiPo with an added charger and 5 V boost (the board has neither, nor battery-level sensing). Slower fetches, screen dimming, and the low-power core waking the main chip.
- **Sound** (ES8311 and a speaker amplifier), for example an alert for emergency squawks (7500, 7600, 7700).
- **More data:** worldwide medium airports on flash or the microSD card, aircraft trails, an offline coastline map, and cached aircraft photos.
- **Faster drawing** with the P4's pixel accelerator (PPA: fills, scaling, rotation, blending) and 2D-DMA, if software drawing proves too slow.

## Order of work

1. Bring-up and a "hello" build (needs the board).
2. Display layer and the radar shown at 240×240 scaled up: proves the pipeline.
3. Layout scaled to 720 pixels with new fonts: the radar looks right.
4. Network task and core split.
5. Touch: driver, tap-for-detail, then gestures.
6. Partitions, board marker, update check and release workflow, then the first `p4-v0.1.0` release.
7. Later features.

Steps 1–3 make it usable; 4–5 make it the device we want; 6 makes it releasable.

## Decisions to make

- **Drawing:** LovyanGFX canvas (recommended to start) or LVGL.
- **Look:** keep the C3's radar style scaled up, or redesign for the larger screen.
- **Shared code:** when the port settles, move the unchanged services into a common folder used by every board, so fixes land once.
