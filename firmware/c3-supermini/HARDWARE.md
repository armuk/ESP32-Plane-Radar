# Hardware: ESP32-C3 Super Mini + 1.28″ GC9A01 display

The parts this firmware is built for, how they connect, and which pins are still free. The pin numbers in the firmware are in [`include/config.h`](include/config.h); the display driver settings are in [`include/hardware/lgfx_config.hpp`](include/hardware/lgfx_config.hpp).

## Parts

| Part | Details |
|------|---------|
| Controller | ESP32-C3 Super Mini: ESP32-C3 (single RISC-V core, 400 KB SRAM, no PSRAM), 4 MB flash, USB-C wired to the chip's own USB (serial and flashing, no separate USB-UART chip), BOOT and RESET buttons, blue LED on GPIO 8 |
| Display | 1.28″ round IPS TFT module, 240×240, **GC9A01** controller, SPI, 7-pin header |
| Case | 3D-printed, see [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) |

## Wiring

| Display pin | Super Mini pin | Firmware name |
|-------------|----------------|---------------|
| VCC | 3V3 | |
| GND | GND | |
| RST | GPIO **0** | `kDisplayPinRst` |
| CS | GPIO **1** | `kDisplayPinCs` |
| DC | GPIO **10** | `kDisplayPinDc` |
| SDA (data in) | GPIO **3** | `kDisplayPinMosi` |
| SCL (clock) | GPIO **4** | `kDisplayPinSclk` |

The display is write-only (no MISO wire). The BOOT button on the Super Mini (GPIO 9) is the radar's only button.

## ESP32-C3 Super Mini pins

| GPIO | Used for | Notes |
|------|----------|-------|
| 0 | Display RST | Also the 32 kHz crystal pin (unused on this board) |
| 1 | Display CS | Also the 32 kHz crystal pin |
| 2 | Free | **Strapping pin:** must not be pulled low at power-on; avoid |
| 3 | Display SDA | |
| 4 | Display SCL | |
| 5 | **Free** | Good choice for an extra output (e.g. a backlight) |
| 6 | **Free** | |
| 7 | **Free** | |
| 8 | On-board blue LED | **Strapping pin:** must be high at power-on for USB flashing to work; avoid |
| 9 | BOOT button | **Strapping pin:** held low at power-on enters the USB download mode; the radar reads it as its button (input, pull-up, active low) |
| 10 | Display DC | |
| 20, 21 | Free | The chip's UART0 (RX, TX). The firmware logs over USB instead, but the boot ROM prints on GPIO 21 at power-on |
| 18, 19 | USB D−/D+ | Not on the header pins |

Power pins: **5V** (from USB-C), **3V3** (from the on-board regulator, which also feeds the display) and **GND**.

## Display module

### Markings

- Back: "1.28″ TFT VER1.0, 240\*240, IC: GC9A01".
- Front, under the glass: "1.28″TFT-240\*240" and the flex-cable pads **U2** (12 pads, numbered 12 to 1).
- Header (front, top to bottom): RST, CS, DC, SDA, SCL, GND, VCC.

### Notes printed on the back

- **R8** is a pull-down resistor on CS, so CS is held low (selected) when it is not connected.
- "此模块可不接CS,RST": the module works **without CS and RST** connected. Leaving them off would free GPIO 0 and 1, at the cost of not being able to reset the panel from the firmware. Keep them connected unless pins run short.

### Driver settings

| Setting | Value | Why |
|---------|-------|-----|
| Driver | LovyanGFX `Panel_GC9A01` | |
| SPI clock | 40 MHz (`kDisplaySpiWriteHz`) | |
| Colour inversion | on (`kDisplayInvert`) | These modules show inverted colours without it |
| Colour order | BGR (`kDisplayRgbOrder = true`) | Red and blue are swapped otherwise |

If a different GC9A01 module shows wrong colours, these two settings are the ones to change.

### Construction

The glass panel is held on the board by adhesive, and connects through a thin 12-way flex cable soldered to the U2 pads. The flex tears easily where it meets the pads and can't be repaired: if the panel is lifted, press it back gently and don't let the flex kink.

## Backlight and night mode

This 7-pin module has **no backlight pin**. The backlight LED is wired to VCC and GND on the module, so it is lit at full brightness whenever the module has power.

The night screen-off setting (Setup → "Turn the screen off at night") therefore fills the screen with black and puts the GC9A01 to sleep, but the backlight stays on: in a dark room the screen is a faint grey glow rather than fully dark. The GC9A01's own brightness command has no effect, because the backlight isn't connected to the controller.

Ways to get real dimming or a true off:

1. **Use the 8-pin version of the module** (recommended). It is the same GC9A01 module with an extra **BLK** (or BL) pin that switches the backlight. Wire BLK to a free GPIO (5, 6 or 7); the firmware can then dim it with PWM.
2. **Modify this module.** Find the backlight's cathode (LEDK) pad among the U2 pads: with the power off, the pad that reads a few to tens of ohms to the header's GND (through the backlight's current-limiting resistor) rather than about 0 Ω is LEDK. Break its path to ground (lift one end of the resistor, or cut the trace), and switch it to ground through a small N-MOSFET (for example an AO3400 or 2N7002) with its gate on a free GPIO through about 100 Ω. This risks the fragile flex cable.
3. **Switch the whole module's power** from a GPIO through a P-MOSFET. This gives a true off but no dimming, and the panel has to be set up again each time it is switched on.

The firmware does not support a backlight pin yet. Adding it means a backlight pin setting in `config.h` (none by default, so existing radars behave as now), LovyanGFX's PWM backlight in `lgfx_config.hpp`, and a choice of dimming or turning off at night.
