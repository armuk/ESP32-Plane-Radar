# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

A live **ADS-B radar** on a round display: aircraft around your location with routes, aircraft details, local weather and time, settings in the browser, and signed over-the-air updates.

**Firmware:** [Releases](../../releases) · **3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083)

## Boards

Each board has its own folder, build and releases. **Flash only the files for your board.**

| Folder | Board | Status | Release tags and files |
|--------|-------|--------|------------------------|
| [`firmware/c3-supermini`](firmware/c3-supermini) | ESP32-C3 Super Mini + 1.28″ round GC9A01 (240×240) | **Released** | `v2.2.1`; files `plane-radar-v2.2.1-…`. These are the releases marked **Latest**. |
| [`firmware/s3`](firmware/s3) | ESP32-S3 with PSRAM (board to be chosen) | Port in progress, not usable yet | Will be `s3-v0.1.0`; files `plane-radar-s3-v0.1.0-…` |
| [`firmware/p4`](firmware/p4) | ESP32-P4 4″ round 720×720 touch (Waveshare ESP32-P4-WIFI6-Touch-LCD-4C) | Port in progress, not usable yet | Will be `p4-v0.1.0`; files `plane-radar-p4-v0.1.0-…` |

If you have a radar today, it is the **C3 Super Mini**: see its [README](firmware/c3-supermini/README.md) for wiring, setup, flashing and updates.

A file for a different chip (C3, S3 or P4) is refused: esptool, web flashers and the radar's own update all check the chip type. Before the S3 and P4 are released, their images will also carry a board marker, so a file for another board with the same chip is refused too. USB flashing the right file always recovers a radar.

## Building

Each folder is a separate PlatformIO project:

```bash
cd firmware/c3-supermini && pio run -e supermini
cd firmware/s3 && pio run -e s3
cd firmware/p4 && pio run -e p4
```

The S3 and P4 folders started as copies of the C3 firmware; their READMEs list what each port still needs. The [Build workflow](.github/workflows/build.yml) tests, checks and builds every board on each push; the [Release workflow](.github/workflows/release.yml) releases the C3 Super Mini from `v*` tags.

## Security, credits and licence

- [SECURITY.md](SECURITY.md): support period, reporting a vulnerability, network interfaces and the update signing key (written for the C3 Super Mini; the other boards will follow it).
- Origins and credits: this project builds on [ironicbadger/ESP32-Plane-Radar](https://github.com/ironicbadger/ESP32-Plane-Radar), a fork of [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar). See the C3 README's [Origins and credits](firmware/c3-supermini/README.md#origins-and-credits).
- [LICENSE](LICENSE) (MIT).
