# Plane Radar: ESP32-S3 (port in progress)

Builds today for a generic ESP32-S3 with 16 MB flash and 8 MB PSRAM (`board = esp32-s3-devkitc1-n16r8`), using the C3 Super Mini's display code unchanged. It is not usable on hardware yet.

```bash
cd firmware/s3
pio run -e s3                 # build
pio test -e native            # host unit tests
pio check -e s3               # cppcheck
```

## Porting checklist

- [ ] **Choose the board** and set `board =` to match its flash and PSRAM:
  - Waveshare ESP32-S3-LCD-1.28 (GC9A01 240x240, 2 MB PSRAM): the display code works as is; only pins change.
  - Waveshare ESP32-S3-Touch-LCD-2.1 (ST7701S 480x480 RGB, 8 MB PSRAM, CST820 touch): a new display setup and a layout scaled for 480 pixels.
- [ ] **Display:** pins and panel in `include/hardware/lgfx_config.hpp`; size in `include/config.h` (`kDisplayWidth`).
- [ ] **PSRAM:** keep the frame buffer in PSRAM and stop freeing it between redraws (the C3 only does that to save memory); consider TLS buffers in PSRAM.
- [ ] **Partitions:** a layout for 16 MB flash (larger update slots and file storage).
- [ ] **Two cores:** run fetches on one core so the web pages and screen stay responsive.
- [ ] **Touch** (2.1" board): tap an aircraft or airport for details.
- [ ] **Update check and release workflow** for `s3-` tags (see below).

## Shared with the C3 Super Mini

This folder started as a copy of [`firmware/c3-supermini`](../c3-supermini) (v2.2.1). Most of the network, update, security and settings code should stay the same, so fixes made there usually belong here too, and the other way round. Once the port settles, the shared parts (`src/services/`, most of `include/services/`, `test/`) can move to a common folder that each board builds against.

## Release and safety rules (all boards)

- **Tags:** this board releases with `s3-vX.Y.Z` tags; release files are named `plane-radar-s3-vX.Y.Z-...`. Its releases must never be marked GitHub's "Latest": that belongs to the C3 Super Mini, whose radars in the field check `releases/latest`.
- **Update check:** off (`custom_update_check = no`) until it looks for this board's `s3-` tags instead of the latest release.
- **Signing:** the same release key as the C3 (`keys/ota-signing-public.pem`). A wrong-chip image is still refused: the update checks the chip ID in the image header, as does esptool.
- **Board check:** before the first release, add a board marker next to the version marker, and refuse an OTA image built for another board with the same chip (e.g. two different S3 boards), so a wrong image can't install with the wrong pins.
- **Partition layout:** a new board starts a new layout of its own; once released it can only change by USB flash (see the C3 README, "Partition layout").
