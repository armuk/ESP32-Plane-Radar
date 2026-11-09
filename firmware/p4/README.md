# Plane Radar: ESP32-P4 4" round touch (port in progress)

Target board: **Waveshare ESP32-P4-WIFI6-Touch-LCD-4C**. It has a 4" round 720x720 touch screen (MIPI-DSI), 32 MB PSRAM and 32 MB flash, with Wi-Fi 6 through an on-board ESP32-C6 and a LiPo battery connector.

Builds today for a generic ESP32-P4 (`board = esp32-p4`), using the C3 Super Mini's code nearly unchanged. It is not usable on hardware yet: the display code still drives a GC9A01 over SPI.

```bash
cd firmware/p4
pio run -e p4                 # build
pio test -e native            # host unit tests
pio check -e p4               # cppcheck
```

## Porting checklist

- [ ] **Board definition:** confirm the chip revision on the board (`esp32-p4` for early silicon, `esp32-p4_r3` for revision 3) and the flash and PSRAM settings.
- [ ] **Wi-Fi through the ESP32-C6:** check the link works with Arduino's `WiFi` (ESP-Hosted), and how the C6's own firmware is kept up to date. Remove the `CONFIG_ESP32_PHY_MAX_WIFI_TX_POWER` stand-in in `platformio.ini` once the real setting is known.
- [ ] **Display (the big job):** the 720x720 MIPI-DSI panel is not something LovyanGFX drives well. Likely move drawing to ESP-IDF's `esp_lcd` with LVGL, keeping the radar logic (`ui/radar_projection`, `ui/radar_range`, label layout) and replacing the drawing layer. Scale the layout from 240 to 720 pixels.
- [ ] **Touch:** tap an aircraft or airport for a detail card; swipe or pinch to change range.
- [ ] **Memory:** frame buffers (about 1 MB each) and caches in PSRAM; parallel HTTPS requests.
- [ ] **Partitions:** a layout for 32 MB flash (update slots of several MB, large file storage for offline map data and icons).
- [ ] **Power:** night mode with the low-power core and screen off; later a "portable" mode on the LiPo battery.
- [ ] **Update check and release workflow** for `p4-` tags (see below).

## Shared with the C3 Super Mini

This folder started as a copy of [`firmware/c3-supermini`](../c3-supermini) (v2.2.1). Most of the network, update, security and settings code should stay the same, so fixes made there usually belong here too, and the other way round. Once the port settles, the shared parts (`src/services/`, most of `include/services/`, `test/`) can move to a common folder that each board builds against.

## Release and safety rules (all boards)

- **Tags:** this board releases with `p4-vX.Y.Z` tags; release files are named `plane-radar-p4-vX.Y.Z-...`. Its releases must never be marked GitHub's "Latest": that belongs to the C3 Super Mini, whose radars in the field check `releases/latest`.
- **Update check:** off (`custom_update_check = no`) until it looks for this board's `p4-` tags instead of the latest release.
- **Signing:** the same release key as the C3 (`keys/ota-signing-public.pem`). A wrong-chip image is still refused: the update checks the chip ID in the image header, as does esptool.
- **Board check:** before the first release, add a board marker next to the version marker, and refuse an OTA image built for another board with the same chip (e.g. two different S3 boards), so a wrong image can't install with the wrong pins.
- **Partition layout:** a new board starts a new layout of its own; once released it can only change by USB flash (see the C3 README, "Partition layout").
