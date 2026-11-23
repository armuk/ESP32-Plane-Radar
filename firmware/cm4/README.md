# Plane Radar: Raspberry Pi CM4 or CM5 in an Echo Show 5 case (research)

Target: the case, 5.5" 960×480 screen, touch layer and glass of an **Amazon Echo Show 5 (2nd gen, 2021)**,
with everything else replaced by a **Raspberry Pi Compute Module 4 or 5** on a drop-in carrier board of our own.
One page shows the radar, the next the full Home Assistant dashboard, a swipe apart.

Status: **hardware research and measurement.** No software or carrier board yet. Unlike the other boards,
this one won't be an ESP32 firmware image: it runs Raspberry Pi OS Lite with a small launcher and the radar
as a local web app.

## What's here

| Path | What |
|---|---|
| [ECHO-SHOW-5.md](ECHO-SHOW-5.md) | Findings, measurements, the CM4 decision, the software stack, the build plan and open questions |
| [CARRIER.md](CARRIER.md) | Plan for the CM4/CM5 carrier board and the new IO board: constraints, power, pin map, display, antenna, heat, stages and open decisions |
| [echo-breakout/](echo-breakout/README.md) | Drop-in breakout board (KiCad 10, JLCPCB): maps the Echo's connectors and runs the touch ribbon, buttons and LEDs from an ESP32-C3 SuperMini |
| [ribbon-extenders/](ribbon-extenders/README.md) | Flex cables that lengthen the Echo's ribbons so the breakout can sit on the bench |
| `parts-documentation/` | Datasheets and 3D models for the connectors and sockets |
| `echo-show-5-dumps/` | Device tree, logs and eMMC image from the unit. **Local only** (excluded through `.git/info/exclude`): it holds the unit's serial, MACs and unlock code |

## Next steps

See "Build plan" and "Open questions" in [ECHO-SHOW-5.md](ECHO-SHOW-5.md). In short: order the breakout
and map J44, J30, J46 and J100; prove the launcher and radar web app on a stock Pi; then a DSI adapter on
the CM4 IO Board, and finally the carrier.

The round 4" ESP32-P4 board is a separate build: [firmware/p4](../p4/README.md).
