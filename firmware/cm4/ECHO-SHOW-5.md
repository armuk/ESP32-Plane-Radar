# Echo Show 5 (2nd gen) enclosure: hardware notes

Goal: put Plane Radar inside the case of an **Amazon Echo Show 5, 2nd gen (2021)**, keeping its
original 5.5" screen, touch layer, glass and outside look, and replacing everything else with a
**Raspberry Pi Compute Module 4 or 5** on a board of our own. It shows the radar on one page and the full Home
Assistant dashboard on the next, a swipe apart. The enclosure matters more than any of the original
electronics.

Status: **research and measurement.** Nothing here is built or tested yet. Items marked *unconfirmed*
are inferred from markings or photos and still need checking on the unit.

The unit was fully opened (mainboard out, shield lids off for chip IDs), reassembled and powered on: it still
works. It stays usable as a working reference for the powered measurements.

## Decision: a Raspberry Pi CM4 or CM5 on our own board

The objection to running Linux on the Echo's own hardware was never Linux or network exposure as such: it
was exposure from a system we can't easily audit, secure, update or configure. The Echo should show the
**full Home Assistant dashboard** (any card, custom cards, camera streams), which needs a real web browser.
Three routes were considered:

| Route | For | Against |
|---|---|---|
| Keep the Echo's insides and run [TECHO5](https://github.com/HuskerMinion/techo5) (Alpine Linux, unlocked with the amonet exploit), porting the radar to a TECHO5 "custom screen" ([techo5#447](https://github.com/HuskerMinion/techo5/pull/447)) | No hardware work; perfect fit | End-of-life downstream Linux 4.9 kernel; closed MediaTek MT7668 Wi-Fi driver and firmware; services we didn't choose; we can't audit or patch it |
| Gut the Echo; drive its panel and touch from an ESP32-P4 | Small firmware we understand; one codebase with the C3, S3 and P4 builds; boots in about a second; well under 1 W | Only native controls built one at a time, no Home Assistant dashboard; no hardware H.264 decoder, so no camera streams |
| **Gut the Echo; Raspberry Pi CM4 or CM5 on our own carrier board** | Full Home Assistant dashboard in a browser; an OS we build, configure and patch ourselves; mainline-supported panel and touch drivers; GPU and RAM to spare | An OS to keep patched; a harder board (high-density connectors, impedance-controlled DSI); a few watts of heat in a sealed case; 10–30 s boot; the radar is rewritten for this build |

**Chosen: a carrier that takes a CM4 or a CM5.** The ESP32-P4 stays the target for the round 4C board ([firmware/p4](../p4/README.md)). TECHO5 is
still useful on a bench unit (kept off the home network) to identify the panel variant and measure voltages.

### CM4 or CM5
The carrier takes either module unchanged (see [CARRIER.md](CARRIER.md), "CM4 or CM5"): the display on
DSI1 and the camera on CAM1, since the CM4's DSI0 and CAM0 pins carry USB 3.0 on a CM5.
- **Power:** USB-C PD (up to 45 W) covers either. A CM4 under load draws about 7 W; a CM5's supply should
  allow up to 12.5 W (2.5 A at 5 V).
- **Heat decides which to fit.** The case is sealed with no fan and can shed roughly 8–12 W in total
  (estimate). The SoC's heat goes to the speaker module's copper plate. A CM5 renders the dashboard more
  smoothly and finishes bursts sooner; for sustained loads its clock is capped in software. The bench stage
  measures both in the closed case.
- **Variant:** on-board Wi-Fi, eMMC and **4 GB RAM** (a browser on Home Assistant manages in 2 GB; 4 GB
  leaves room for the radar and image caching).

## Software

Kept small, auditable and patchable:

| Layer | Choice |
|---|---|
| OS | **Raspberry Pi OS Lite** (Debian), only the packages needed, unattended security updates. Patching through Debian is far less work for one person than rebuilding a Buildroot image for each fix. Add a read-only root (overlayfs) if power-loss corruption shows up |
| Panel | Device tree overlay using the mainline `panel-sitronix-st7701` driver with this panel's init sequence. Amazon's driver is GPL: take values from our own DCS reads and measurements, don't copy its tables |
| Touch | Mainline `goodix` driver, GT911 at 0x5d, axes rotated to match the panel. Never write its config block |
| Display stack | The `cage` single-app Wayland compositor, running our own launcher |
| Launcher | A small native program on **WPE WebKit** holding **two web views**: the radar and Home Assistant. A horizontal swipe switches between them. Each view keeps its own origin and storage, so Home Assistant's frame protection (`use_x_frame_options`) stays on |
| Radar | A local web app (HTML canvas and JavaScript) served **on loopback only** by a small local service that does the ADS-B, route and weather fetches. Keeps working when Home Assistant is down. The C3 firmware's projection, range and label-layout logic is the reference for the port |
| Home Assistant | Logged in as a **dedicated non-admin user**, over HTTPS |
| Network | Firewall denying all inbound connections; no SSH by default (key-only, behind a switch, when needed); nothing listening off loopback |
| Buttons and LEDs | J100's buttons as range, brightness and screen-off through `gpio-keys`; LEDs for stale data or Wi-Fi down |
| Sound | I²S amplifier on the carrier to the original speaker (J2), for alerts and Home Assistant media |
| Optional hardware | Wi-Fi, Bluetooth, USB data, microphones (voice control) and camera all fitted, plus optional wired Ethernet in a printed back plate; each **physically switched off by removing a jumper**; see [CARRIER.md](CARRIER.md), "Jumper block" |

## Screen size and layout

The bezel window is cut for the original panel, so the screen can't grow without cutting the case.

| | Echo Show 5 panel | 4" round (4C target) |
|---|---|---|
| Resolution | 960×480 (2:1) | 720×720 |
| Active area | 125 × 63 mm | about 100 mm diameter |
| Largest radar circle | about 62 mm | about 100 mm |

Proposed layout: radar disc on the left (480×480), flight list or selected-aircraft card on the right (480×480).

## The panel

Sources: TECHO5 [`docs/hardware.md`](https://github.com/HuskerMinion/techo5/blob/main/docs/hardware.md), and
Amazon's kernel driver
[`st7701s_wsvga_dsi_vdo_cronos.c`](https://github.com/amazon-oss/android_kernel_amazon_mt8163/blob/cm-14.1/drivers/misc/mediatek/lcm/st7701s_wsvga_dsi_vdo_cronos/st7701s_wsvga_dsi_vdo_cronos.c)
(branch `cm-14.1` of `amazon-oss/android_kernel_amazon_mt8163`; Amazon's codename for this model is `cronos`).
That driver is GPL: link to it, don't copy its tables into this repository.

### Common to every variant

| Item | Value |
|---|---|
| Native resolution | **480×960 portrait**, mounted rotated to 960×480 landscape |
| Physical size | 63 × 125 mm |
| Interface | MIPI-DSI **video mode, burst**, **2 data lanes**, RGB888, MSB first, 24-bit packed |
| DSI PLL | 212 MHz (about 424 Mbit/s per lane), spread spectrum off; HS_TRAIL 5, HS_PRPR 4 |
| Timings (Truly) | H: sync 6, back porch 20, front porch 30, active 480 (total 536). V: sync 6, back porch 16, front porch 21, active 960 (total 1003). About 32.3 MHz pixel clock at 60 Hz (measured 59.64 Hz) |
| Supplies | **2.8 V** analog (`lcm-vgp`, from the MT6323's VIO28 output) and **1.8 V** logic (`lcm-vio`, from the MT6323's VGP3 output), per the driver's comments. Power-on order: **2.8 V first, then 1.8 V**; power-off is the reverse. No separate power chip on the panel side ("no external lcm-pmic") |
| Reset | high, 10 ms, low, 10 ms, high, then wait **120 ms**. Driven by a processor GPIO, so very likely **1.8 V logic**: from the CM4's 3.3 V GPIO use a level shifter or an open-drain output pulled up to 1.8 V *(confirm the level when powered)* |
| Init | the variant's register table, then sleep-out (0x11) and display-on (0x29) |
| Variant select | the bootloader reads a voltage on the ribbon (MT8163 AUXADC channel 0, presumably a resistor on the panel side) and passes `nt35521_id=<n>` to the kernel |
| Backlight | LED string driven by a boost converter on the mainboard (see J44 below); Linux exposes 0–255 |

### The five variants

All 960×480 on 2-lane DSI, same window, same physical size. Each has its own init table in Amazon's driver.

| ID | Module maker | Glass | Controller | Driver name (suffix after `..._wsvga_dsi_vdo_cronos_`) |
|---|---|---|---|---|
| 0x2 | KD | BOE | Sitronix ST7701S | `st_kd_boe` (also the driver's fallback) |
| 0x3 | **Truly** | Truly | Sitronix ST7701S | `st_truly` |
| 0x4 | KD | INX (Innolux) | Sitronix ST7701S | `st_kd_inx` |
| 0x5 | KD | HSD (HannStar) | Sitronix ST7701S | `st_kd_hsd` |
| 0x6 | KD | BOE | GalaxyCore **GC9503NP** | `gc_kd_boe` |

### This unit: very likely Truly (0x3) — *unconfirmed*

The display ribbon's data matrix reads `CRSTDMBTRU1101322201516328`: plausibly **CR**onos,
**ST** (ST7701S), **TRU**ly, which matches the driver's `cronos_st_truly` name. The TECHO5 author's unit
reports the same variant. Text on the panel's back plate reads `LM5?C34415` (one character unreadable),
probably the module's own label. Through a gap in the frame, a second dot-printed code is partly visible,
reading roughly `U9LJ.A` / `091J.A` (orientation uncertain, partly hidden). It looks like an inkjet lot or
date code, not a part number, so it doesn't identify the variant. Next to it is a strip of copper traces on
green solder mask (probably the panel's or the backlight's flex).

Confirm at bring-up without guessing:
1. Read the display ID over DSI (DCS 0xDA / 0xDB / 0xDC, or 0x04) and log it.
2. Once the ribbon is mapped, read the panel-ID pin through an ADC on the carrier (the CM4 has none of its own).

The firmware can do either at boot and pick the matching init table, so it works with whichever panel a unit has.

## Touch

| Item | Value |
|---|---|
| Controller | **Goodix GT911**, on the touch layer's own ribbon (chip marked `GT911 2140…`, probably week 40 of 2021) |
| Ribbon | `MCT4F5923FPC-A2`, `TY-R 21496` (lot or date code); a second marking `4F5923V11021525008EF` (probably revision V1.1 plus a serial number) |
| Parts on the ribbon | U1 (GT911), C1–C5, R1, R2, D2. R1 and R2 are probably the I²C pull-ups *(unconfirmed)*. The text `C2C3R2C4R1C5` printed beside the chip is only these parts' reference labels, not a chip marking (confirmed under the microscope) |
| Chip markings (microscope) | `GOODIX` logo, `GT911`, then lot and date lines starting `2140-…` and `AP…` (partly legible). This confirms the touch controller; nothing here identifies the **display** panel |
| Connector | **8 pins**, numbered 1 and 8 on the ribbon; plugs into **J30** on the mainboard |
| I²C | address **0x5d** on the Echo (I²C bus 2 in Linux). The address (0x5d or 0x14) depends on INT's level during reset |
| Axes | raw 480×960, portrait like the panel |
| Driver | Linux `goodix` on the CM4 (ESP-IDF `esp_lcd_touch_gt911` for bench tests from an ESP32); same register protocol as the GT9271 in the 4C plan |

**Never write the GT911's configuration block** (registers from 0x8047). The chip stores tuning for this
exact sensor; a generic config from an example sketch can leave touch inaccurate or dead. Read only.

**2026-11-15: touch stopped working after the teardown and reassembly** (display fine; much of the grounding
tape not refitted). Check in this order: J30 ribbon seated square and fully home with the latch closed; no
crease or damage on the touch ribbon near the GT911; grounding tape restored (replace lost tape with
conductive-adhesive copper tape); nothing touching the glass at power-on (the GT911 calibrates then). In
TWRP, `ls /sys/bus/i2c/devices/` showing `2-005d` and the driver's probe messages in `dmesg` separate a
connection fault (chip not answering) from noise or calibration (chip answers, no touches).
Reseating J30 didn't help. The camera (J45) is ruled out: it is on I²C bus 0, touch on bus 2. The
amplifier shares bus 2 (0x3d), so if the Echo still plays sound, bus 2 itself is alive and the fault is
on the touch side (ribbon, the GT911 or its parts). Also inspect the touch ribbon's corner capacitor
that had dark debris on it in the microscope photos.
**Sound works**, so bus 2 is alive: the fault is on the touch path. Suspects, in order: the eight small
parts in front of J30 and U1918 (possibly the touch supply regulator) on the mainboard, knocked while
reseating; the touch ribbon and its parts; the GT911 itself (static). With fine probes and power on, check
for a supply voltage (2.8–3.3 V, maybe 1.8 V) across the touch ribbon's decoupling capacitors.
**Likely cause:** the tool skimmed the mainboard while the shield lids were being popped off, probably
knocking off a small part or scratching through a trace on the touch path. If so, the **touch ribbon
and GT911 are probably undamaged**, and they are the parts this project reuses. The mainboard is only a
reference now. The GT911 config can still be backed up later by reading it from an ESP32 through the J30
breakout (read only), without the Echo.
Photos after the shield work show: solder mask scraped through to copper in several gouges near U1917/NTC3
and the SC1 screw (bottom edge, the likeliest place for a cut touch-path trace); a grey scraped patch right
of L20 next to the fine traces fanning out to the memory; and a raised solder blob on a shield-fence pad
above L20. The GT911's kapton cover was also removed for photos. **Before powering again, cover the bare
GT911 flex and the exposed copper with kapton**, so nothing shorts against the metal frame or the shields.

**Microscope close-ups (2026-11-16, 14 photos):**
- **Most of the bare copper is ground.** The widest gouges run along the edges of the shield-can fences and
  the via-stitch rows beside them. Those are ground pour, so they are cosmetic: a risk only of shorting to
  metal, and kapton covers that.
- **Bottom-edge screw:** one gouge runs diagonally from a via straight to the screw's gold ring. A second
  photo shows two parallel diagonal scrapes under a shield edge, lined up with traces that continue under
  the mask. Copper is exposed along them, but no clean break shows at this magnification. **These are the
  only marks that could be a cut signal trace;** continuity, not photos, settles it.
- **Right of L20** (the "grey patch" above): **the copper foil has been torn off the board.** Higher
  magnification (13 more photos, same day) shows:
  - a salmon-pink flap of copper, still attached on one side, folded back beside a dark-brown patch of
    the same shape;
  - the flap carries a via dimple that matches the patch;
  - in the patch, translucent cross-hatched glass weave with via holes: bare laminate.

  The solder mask edges around it are torn and serrated. So the glossy green is the board's own mask
  pulled up, not a repair. The part above L20 sits in a similar torn hole with bare laminate beside it.

  It was a wide pour stitched with vias (ground or a supply plane), not a fine trace. **The display and
  backlight still work, so the circuits there still connect.** It isn't the touch fault.
- **Bottom-edge screw gouges:** the copper is gouged, not just bared. One spot has a curled sliver of
  copper standing up, one gouge has a loose copper hair lying across it, and loose flakes sit under the
  shield edge. Still no visibly cut trace.
- **J30 close-ups (5 photos, same day): the likeliest touch fault.**
  - All eight J30 legs are soldered cleanly, with no bridges and no lifted legs.
  - The fine traces from each leg to the part in front of it are intact. The parts at the pin-8 end (two
    marked `C5`, plus two resistors) look undamaged.
  - **The two parts at the pin-1 end (by the silkscreen "1") are damaged:**
    - The grey-bodied part, second from the end, has a crumbled, broken-off right end with white debris
      and a rust-coloured speck. That looks like a cracked ceramic capacitor or ferrite.
    - The resistor at the very end has a dark, irregular patch eating into its right end, unlike its
      siblings. That looks like a chipped or scorched resistive layer.
  - **Closer photos (2 more) confirm both are broken, not just marked:**
    - **End resistor:** the dark resistive top is split into separate blocks with a crack across the
      body, and its right end is chipped. Probably **open circuit**.
    - **Grey part:** the right end of the body has broken away from its end cap. The cap is still
      soldered, but crumbled body material, white powder and a void sit between them. The trace from
      the nearest J30 leg runs to this part's left pad. A grey body could be a capacitor, a ferrite bead
      or an ESD diode; the intact grey parts on the other lines look the same. Most likely **open**,
      possibly a short.
    - Both breaks are on the side away from J30, the side a tool sliding along the socket would hit.
  - A broken capacitor can fail **short** (pulling its line or a supply to ground) or open. A broken
    series part fails open. Either would stop the GT911 answering, and these are on the touch connector.
  - A few loose flecks of debris lie between the legs. Brush them away.
  - **Check (unpowered, meter):**
    1. Resistance across each of the two damaged parts, compared with the matching part on the other
       lines.
    2. Each of the two parts' outer pads to ground. A near-0 Ω reading means a shorted capacitor.
    3. Continuity from J30 legs 1 and 2 to the parts' pads.
  - **For the project:** if these two parts are the fault, it sits on the mainboard. The touch ribbon and
    GT911, the parts being reused, are then probably fine, and the breakout's touch test will show it.
- **Before powering the Echo again:**
  1. Lift off loose copper hairs and flakes with tweezers.
  2. Glue the copper flap flat (UV solder mask or a dab of epoxy) rather than cutting it: it may still
     carry current.
  3. Cover every bare area with kapton. A flap or sliver touching a shield, or the fine memory traces
     beside L20, is a new short waiting to happen.
- **Fine traces fanning out to the memory, Q3002/D21, X302, S802/TP1433:** intact as far as the photos
  show.
- **Next step:** with the board unpowered, beep each J30 leg to the part in front of it, and from there
  along the trace as far as it can be followed. A break near the bottom-edge screw would explain the touch
  fault and leave the touch ribbon and GT911 good to reuse.

### J30 pinout (8 pins) — to measure

Pins 1 and 8 run into the wide copper surround on the ribbon's back (a steel stiffener sits behind the
chip), so they are probably GND *(unconfirmed)*.

| Pin | Expected | Continuity (power off) | Voltage (running) | Confirmed |
|---|---|---|---|---|
| 1 | GND? | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |
| 5 | | | | |
| 6 | | | | |
| 7 | | | | |
| 8 | GND? | | | |

Expect among pins 2–7: VDD (2.8–3.3 V), maybe a separate VDDIO (1.8 V or the main supply), SCL and SDA
(idle high at the I/O voltage), INT (changes when the screen is touched) and RST (high). **If the bus runs
at 1.8 V**, either feed VDDIO 3.3 V from the carrier (if it has its own pin) or add an I²C level shifter.
The CM4's GPIOs are 3.3 V.

## The mainboard

**KC19-TI MLB V4.0**, PN `01042JN00-575-G`. KC19 appears to be Amazon's internal project code (it is also on
the IO board, its cable and the inner case). The speaker amplifier on this unit is a **Maxim MAX98396EWB**
(identified under a microscope), so "TI" in the board name does **not** mean the TI TAS5805M amplifier
variant TECHO5 mentions. It may refer to the TI microphone ADC. On the back,
a QFN marked "ADC 3101 TI", just above TP13, TP15 and TP16, is a **TI TLV320ADC3101**: a low-power stereo ADC (analog-to-digital only, no playback side) with microphone bias outputs. TECHO5 and the Linux device tree call it TLV320AIC3101; the part fitted is the ADC-only version.

Under the clip-on shield lids (the lids have snap dimples and lift off without heat):
- a **MediaTek MT8163V** processor. This matches Amazon's `android_kernel_amazon_mt8163` source, so the
  display driver and timings linked above are for this exact chip family. MT8163 boards usually pair it
  with a MediaTek MT6323 power-management chip.
- a **MediaTek MT6323LGA** power-management chip (marked `2133-ACAH`, probably week 33 of 2021, and
  `CTGC1S63`). Confirmed. The panel driver names two of its outputs as the screen's supplies (see "Common to
  every variant"), so the new board needs its own **2.8 V and 1.8 V regulators** for the screen.
- **2 × Samsung K4B4G1646E-BYMA**: 4 Gbit (512 MB) DDR3L memory each, x16, 1.35 V. Together **1 GB of RAM**
  on a 32-bit bus. It goes with the mainboard; the CM4 brings its own RAM.
- next to the MT6323, a **26 MHz TCXO** marked `BN26.0 D047` (probably KDS, DSB211SDN/DSB221SDN family): the
  reference clock MediaTek platforms share between the processor, power chip and Wi-Fi. It goes with the
  mainboard; the CM4 has its own crystals.
- a **Samsung KLM8G1GETF** 8 GB eMMC 5.1 (the Echo's storage). It holds the Echo's software and the
  owner's data, probably including the Wi-Fi password and Amazon account tokens. Before the mainboard is
  thrown away: deregister the device from the Amazon account, and either factory-reset it while it still
  runs or physically destroy this chip.
- a **MediaTek MT7658CSN**, the Wi-Fi and Bluetooth combo chip, which TECHO5's closed `mt7668` driver supports.
It goes with the mainboard, so nothing here carries over; the CM4's own Wi-Fi replaces it. The
power-management chip, the one whose rails would hint at the screen and touch voltages, is still to be found.

### Connectors

| Ref | Pins | Goes to | Keep? |
|---|---|---|---|
| **J44** | 33 | Display panel (pin 1 towards the top of the case, 33 at the bottom) | Yes |
| **J30** | 8 | Touch layer (GT911) | Yes |
| **J46** | 31 | IO board: DC jack and USB | No: a new IO board (USB-C, 3.5 mm jack) with two new cables replaces the IO board and this ribbon |
| **J100** | 23 | Top board: buttons and their LEDs, the two microphones, privacy cover switch and detector | Yes: buttons, LEDs and switch; microphones to the carrier's mic ADC behind the MIC jumper (see Microphones) |
| **J45** | 25 | Camera | Optional, behind the CAM jumper (sensor and Linux driver to identify) |
| J2 | 2 | **Speaker** (confirmed: a plain two-wire connection) | Optional (see Speaker) |

### Backlight boost circuit (next to J44)

Between J44 and the RF shield: an inductor marked **100** (10 µH, probably L20), a small controller chip,
a diode (probably D18), and **R335 / R336**, which are very likely the LED current-sense resistors on the
string returns *(unconfirmed)*. NTC1 sits nearby. **DN2** and **CN3**, between J44 and that circuit, are
probably an ESD protection array and a common-mode filter: the DSI pairs most likely pass through them.

### Microscope findings (around J44 and J30)

**J44 legs are staggered**, so each row is 0.6 mm apart and easy to probe:
- Silkscreen: **pin 1 at the right-hand end** (by the "J44" label), **33 at the left**.
- **Back row** (legs coming out behind the socket body): **17 legs = odd pins 1, 3 … 33**. The rightmost
  back leg sits further right than any front pad, so it is pin 1.
- **Front row** (pads in front of the socket, traces visible): **16 pads = even pins 2, 4 … 32**.
- The **eight right-hand front pads (pins 2–16)** run in closely spaced pairs into the two 10-pad parts
  below, **DN2** and its neighbour (label read as CN3 earlier; may be DN3): very likely the **DSI pairs**.
  Eight lines, so the clock and two data pairs (6) plus two more *(unconfirmed)*.
- The **left-hand front pads (pins 18–32)** route away with wider traces: probably power, ground and the
  backlight *(unconfirmed)*. The odd pins' traces are hidden under the socket.

**DN2 and its neighbour:** identical 10-pad packages (5 per side), marking like `AGS` (hard to read).
"DN" is a diode-network prefix, so these are most likely **4-channel flow-through ESD protection arrays**
on the DSI lines, **not** a common-mode filter as guessed earlier *(unconfirmed)*. Below them: a large
capacitor and a **0 Ω link** (marked `0`).

**Backlight boost (next to J44):**

| Part | Marking | Reading |
|---|---|---|
| L20 | `100` | 10 µH inductor |
| Controller | none visible | 6-lead package (3 per side) between R335/R336 and the inductor |
| D18 | `14A` | Diode in an SMA/SMF-style package: probably the boost's Schottky rectifier *(unconfirmed)* |
| R335, R336 | `01X` or `10X` (EIA-96, read upside down) | **10.0 Ω** (`01X`) or 12.4 Ω (`10X`). Probably the LED current-sense resistors. Many white-LED boost controllers regulate to 0.2 V at the sense pin: 0.2 V ÷ 10 Ω = **20 mA per LED string**, the usual figure *(unconfirmed: needs the controller's part number, or measure the LED current)* |

**J30 (touch):** single row of 8 legs, **pin 1 on the right, 8 on the left** (silkscreen, as
photographed: socket legs towards the camera's bottom). In the board frame used under "Positions from the
photos" that is **pin 1 at the top end (towards J44) and 8 at the bottom end (nearest screw S4)**. Every line
passes a small passive in front of the socket (series resistors or filter capacitors). Nearby **U1918** is
a 4-ball chip-scale part, possibly the touch layer's supply regulator *(unconfirmed)*.

### J44 pinout (33 pins) — to measure

The ribbon side shows wide, via-stitched traces towards pin 33 (power, ground and probably LED+/LED−)
and thin traces fanning out in pairs towards pin 1 (probably the DSI pairs).

**Power off, mainboard alone, continuity mode, probing the solder joints behind J44:**

| Continuity to | Means |
|---|---|
| A mounting hole or shield can | GND |
| The boost output (diode cathode, its output capacitor) | LED+ |
| R335 or R336 (the end away from ground) | LED− (probably two strings) |
| DN2 / CN3 pads | DSI pair (expect 6 pins: clock plus 2 data lanes) |
| Nothing above | power rail, reset or panel ID (find with power on) |

**Power on** (IO board on J46, panel on J44, touch on J30, 12 V adapter, Fire OS on screen, ground clip on a
mounting hole), sharp needle probe, one pin at a time:

| Reading | Probably |
|---|---|
| about 1.8 V | I/O supply, or reset (held high) |
| about 2.8 V | analogue supply |
| steady between 0 and 1.8 V | panel-ID voltage |
| 15–20 V or so | LED+ |
| about 0.2 V average | DSI lanes while streaming |

At this pitch a slipped probe can short two pins: go slowly, and leave the panel unplugged for anything
that doesn't need it lit.

| Pin | Continuity (power off) | Voltage (running) | Function | Confirmed |
|---|---|---|---|---|
| 1 | | | | |
| 2 | | | | |
| 3 | | | | |
| 4 | | | | |
| 5 | | | | |
| 6 | | | | |
| 7 | | | | |
| 8 | | | | |
| 9 | | | | |
| 10 | | | | |
| 11 | | | | |
| 12 | | | | |
| 13 | | | | |
| 14 | | | | |
| 15 | | | | |
| 16 | | | | |
| 17 | | | | |
| 18 | | | | |
| 19 | | | | |
| 20 | | | | |
| 21 | | | | |
| 22 | | | | |
| 23 | | | | |
| 24 | | | | |
| 25 | | | | |
| 26 | | | | |
| 27 | | | | |
| 28 | | | | |
| 29 | | | | |
| 30 | | | | |
| 31 | | | | |
| 32 | | | | |
| 33 | | | | |

**Connector pitch:** to measure. 33 pins at 0.3 mm pitch is a contact row of about 10 mm (ribbon about
11–12 mm wide); at 0.5 mm it would be about 16 mm (ribbon about 18 mm). Photos suggest 0.3 mm.

## Microphones

The two microphones are on the same top board as the buttons, their LEDs and the privacy switch, so
they can't be removed by dropping a cable the way the camera can. TECHO5's notes say they feed the TI
**TLV320ADC3101** ADC on the mainboard (TECHO5 names it TLV320AIC3101) on its differential analog inputs (`DIF1_L` / `DIF1_R`), so they
are **analog** microphones. They need a bias voltage from that ADC to produce any signal, and an ADC to
turn it into data. Neither exists once the mainboard is gone. (The ADC's output went through an FPGA
over SPI to the SoC; that is gone too.)

### The top board (KC19 MIC BD)

| Item | Value |
|---|---|
| Markings | **KC19 MIC BD Ver 2.0**, P/N `01042HE00-421T-G`; `SH10 94V-0 E248779`; date or lot `1222-001`; a data matrix on the back |
| Front (towards the case top) | **SW1, SW2, SW3** tactile buttons; **LED1, LED2** on either side of SW1 (probably the mute button and its red light *(unconfirmed)*); two black rubber rings between the buttons, probably the gaskets sealing each microphone to its hole in the case *(unconfirmed)* |
| Back | **MK1, MK2**: MEMS microphones in metal-lid packages, probably bottom-port through the board *(unconfirmed)*; small parts around each (C, R, and D parts that look like ESD diodes); test points TP1–TP45 |
| Connector | its own **J44** (board-local name, not the mainboard's display J44), pin 1 marked; cable to the mainboard's **J100** (23 pins) |
| Privacy switch | **SW4** on the flexible tail at the left end, with a small metal-can part next to it (near TP45 / C904 / D11), probably the camera-cover slide switch and its detector *(unconfirmed)* |

**On the carrier, the microphones are optional and physically switchable** (see [CARRIER.md](CARRIER.md),
"Audio"): J100's mic pins go to a mic ADC (the same TLV320ADC3101 is the low-risk choice) in its own power
domain. Removing the **MIC** jumper unpowers the ADC and with it the mic bias, so the microphones are dead in
hardware, whatever software does. One top-board LED can show the domain's real state.

For a unit that should never have microphones at all:
1. **Leave the MIC jumper out.** Nothing biases or digitises the microphones.
2. **Also remove MK1 and MK2** from the top board with hot air, if you want them physically gone. Nothing
   else on the board depends on them; the buttons, LEDs and switch keep working.
3. Covering the case's sound holes adds nothing once they are dead electrically.

Repurposing the rest of the top board:
- **Mic-mute button** (Linux `gpio-404` on the Echo) and **volume buttons** (`gpio-393`, `gpio-394`):
  inputs for range, brightness or night mode.
- **Privacy cover detector** (Linux `gpio-499`, `SW_CAMERA_LENS_COVER`): could act as a physical "screen off" switch.
- **LEDs:** an indicator, for example red when aircraft data is stale or Wi-Fi is down.

The J100 pinout is still to map: which pins are buttons, LEDs, the switch and the microphones.

## Speaker

A single speaker on a plain two-wire lead into the mainboard's **J2**. On this unit it was driven by a
**Maxim MAX98396EWB** (confirmed): a digital-input (I²S/TDM) class-DG mono amplifier running from up to
20 V, in a tiny wafer-level package. It leaves with the mainboard; salvaging it isn't practical.

To reuse the speaker: drive it from an I²S class-D amplifier on the carrier
board. A common 5 V I²S part such as the MAX98357A gives about 1–2 W into this 8 Ω speaker, plenty for
alert sounds.

Measured DC resistance: **6.2 Ω**, so a nominal **8 Ω** speaker (a speaker's DC resistance is usually
about 75–80% of its rated impedance).

J2: **Molex Pico-SPOX**, 1.5 mm pitch, 2 circuits (measured from the ruler photos and matched to
Molex's drawings on 2026-11-16; an earlier reading of 1.25 mm PicoBlade was wrong). The plug has no latch
arm, just a friction ramp and a polarising ledge. The header on the mainboard is a **right-angle
surface-mount** type with no metal side tabs, its legs lying flat on two pads marked 1 and 2, at the
board's edge next to C9211. Molex's 87438-0243 header takes the original lead unchanged.

| Pitch | Common families (2-pin) |
|---|---|
| 1.0 mm | JST SH |
| 1.25 mm | Molex PicoBlade, JST GH |
| 1.5 mm | **Molex Pico-SPOX** (this one), JST ZH |
| 2.0 mm | JST PH |
| 2.5 mm | JST XH |

## Power and IO board

| Item | Value |
|---|---|
| Adapter | 15 W, model **LA23UK**: input 100–240 V~ 0.5 A 50/60 Hz, output **12.0 V 1.25 A** |
| IO board | **KC19 IO BD Ver 2.0**, PN `01042HD00-42T-G` (as printed on the board), `SH9 94V-0 E248779`, date or lot `2210-001`. Carries the DC jack and USB (details below) |
| IO cable | **KC19 IO-MLB Version 2.1**, `E495366`, `0722 L 94V-0`, data matrix `CH2LKG0IOM207303YQ`. Plugs into J46 (31 pins) |
| Inner case | QR code `KC19HTL03TLF22052721878` |

For the CM4 build this board and its adapter are replaced: USB-C PD in the micro-USB slot, and a 3.5 mm
headset jack in the barrel-jack opening (see [CARRIER.md](CARRIER.md), "Input: a new IO board with USB-C").
Its outline, mounting and the two openings are what the new IO board must match.

### The IO board in detail

| Ref | What | Notes |
|---|---|---|
| **J3** | DC barrel jack (12 V in) | Metal shell marked `125` |
| **J2** | **Micro-USB** socket, mid-mount (confirmed) | The Echo's service port. Keep it for reflashing the CM4's eMMC (`rpiboot`) |
| **J1** | **Unpopulated** footprint: two locating holes, slotted pads, a jack-shaped outline | Almost certainly a **3.5 mm audio jack** left off this model. **The case has no hole for it:** there's a faint guide mark where it would be, but the plastic is solid (the mould was changed). The CM4 build puts its 3.5 mm jack in the barrel-jack opening instead; this mark is the fallback position |
| **J4** | 31-pin FPC connector, pins 1–31 marked | Cable to the mainboard's **J46** |
| **D305** | SMA diode marked `15A 2147` | Probably an **SMAJ15A TVS diode** (15 V stand-off) clamping surges on the 12 V input *(unconfirmed)*; 2147 is likely a date code (2021, week 47) |
| FB2, C1708, C1709, C9174, C9175 | Ferrite bead and capacitors | Filtering on the 12 V input |
| White outline with `+` and two pads (top right, by C9174) | Polarised capacitor footprint | Possibly an unpopulated bulk capacitor *(unconfirmed)* |
| L13 | Small two-winding part | Probably a common-mode choke on USB D+/D− *(unconfirmed)* |
| FB1 | Ferrite bead | Probably on USB VBUS *(unconfirmed)* |
| D13, D14, D15, D17, D306 | Small diodes | Probably ESD protection on the USB and power lines *(unconfirmed)* |
| R231, R234, R235, R236, R239–R241; C12, C19, C203, C354 | Resistors and capacitors | Support parts for the above |
| TP16, TP17 | Test points | Near J4, on the back |
| CLIP2 and the second metal tab | Grounding clips to the chassis screws | |

The CM4 build doesn't reuse this board (see above). Measure its outline, mounting holes, the positions and
sizes of the barrel-jack opening and micro-USB slot, and the path its ribbon takes, for the new IO board.

### J4 / J46 pinout (31 pins) — reference only

No longer needed for the carrier (the IO board and this ribbon are replaced); kept for the record.

Easy to map on the IO board alone, power off, continuity mode. Expect several pins in parallel for 12 V
and for ground (to share current). 31 pins is more than power and USB need, so the rest may be the
unpopulated J1 jack's lines, the speaker, or unused.

| Signal | Find by continuity to | J4 pin(s) |
|---|---|---|
| 12 V | J3 centre pin (after D305 / FB2) | |
| GND | J3 sleeve, the metal clips | |
| USB VBUS | J2 pin 1 | |
| USB D− | J2 pin 2 | |
| USB D+ | J2 pin 3 | |
| USB ID | J2 pin 4 | |
| J1 pads (unpopulated jack) | each J1 pad | |
| Anything else | | |

## Measurements

### Connector pitch (measured on each ribbon's gold contacts)

Pitch = centre-to-centre distance from the first contact to the last ÷ (pins − 1).

| Ribbon (mainboard socket) | Pins | First-to-last contact, centre to centre (mm) | Tail width (mm) | Thickness at contacts (mm) | Contacts face | Pitch |
|---|---|---|---|---|---|---|
| Display (J44) | 33 | 9.17 (rough) | **10.24** by caliper (2026-11-16); a first reading of 10.5 was rough | 0.2 | Towards the board | **0.3 mm**: Hirose's FH26 ribbon for 33 pins is 10.2 mm wide (33 × 0.3 + 0.3); 9.17 is a little under the 9.6 expected, within hand-measuring error |
| IO board cable (J46) | 31 | 9.0 | 9.5 | 0.2 | Towards the board | **0.3 mm** (9.0 ÷ 30) |
| Top board cable (J100) | 23 | 6.5 | 7.25 | 0.2 | Towards the board | **0.3 mm** (6.5 ÷ 22 ≈ 0.295) |
| Touch (J30) | 8 | 3.5 | | 0.3 | Towards the board | **0.5 mm** (3.5 ÷ 7). A first reading of 4.5 mm had included the wide ground tabs at each end |
| Camera (J45, optional) | 25 | | | | | |

Expected centre-to-centre spans:

| Pins | 0.3 mm pitch | 0.4 mm | 0.5 mm | 1.0 mm |
|---|---|---|---|---|
| 33 | 9.6 | 12.8 | 16.0 | |
| 31 | 9.0 | 12.0 | 15.0 | |
| 23 | 6.6 | 8.8 | 11.0 | |
| 8 | 2.1 | 2.8 | 3.5 | 7.0 |

### Choosing the sockets

| Socket | Spec |
|---|---|
| Display | 33 pins, 0.3 mm pitch, for 0.2 mm thick FPC, **bottom contact** (or dual contact) |
| Touch | 8 pins, 0.5 mm pitch, for 0.3 mm thick FPC, **bottom contact** (or dual contact), **taking only 1.5 mm of ribbon** (Hirose FH34SRJ) |
| IO board (drop-in board only) | 31 pins, 0.3 mm pitch, for 0.2 mm thick FPC, **bottom contact** (or dual contact) |
| Top board (drop-in board only) | 23 pins, 0.3 mm pitch, for 0.2 mm thick FPC, **bottom contact** (or dual contact) |

Three of the four sockets are the same family (0.3 mm pitch, 0.2 mm FPC, bottom contact) in 23, 31 and
33 pins; only the touch socket differs.

"Bottom contact" means the socket's contacts touch the side of the ribbon facing the board, which is how
both ribbons sit in the Echo. A dual-contact socket works either way and removes one way to get it wrong.

**Pin order on the new board:** fit each socket on the same side of the board, facing the same way as
on the Echo, with pin 1 at the same end relative to where the ribbon arrives. Rotating or mirroring a
socket swaps pin 1 and pin 33 (or 1 and 8), and the ribbon then meets the wrong signals.

### Mainboard (template for the drop-in board)

| Item | Value (mm) |
|---|---|
| Outline, width × height | **108 × 60** |
| Overall height, tallest point | **8.8**, at the electrolytic capacitor (C9211) next to the speaker connector |
| Overall height over the shielded processors | **3.1** |
| PCB thickness alone | **1.0** (caliper, 2026-11-16) |
| Mounting holes (centre, from one named corner) | From two photos, see "Positions from the photos" below |
| Each connector's position (pin 1 end, from the same corner) | From two photos, see "Positions from the photos" below |
| Tallest part: shield can height above the board | |
| Clearance from board to case or panel back, each side | |

#### Positions from the photos (2026-11-15)

**Source and method:**
- Two separate straight-down phone photos of the board in the rear case (IMG_5959 and IMG_5963,
  4032 × 3024 each).
- In each, the board's four corners were mapped to the measured 108 × 60 mm outline
  (perspective-corrected), and every feature was read off the corrected image at full resolution.
- The table gives the average of the two photos.

**Accuracy:**
- The two photos agree within 0.2–0.6 mm on every feature, except the far-right screw S6 at 0.8 mm.
- In x, size checks against the eMMC (11.6 vs 11.5 mm) and DDR3 (13.2 vs 13.3 mm) are within 0.1 mm.
- In y the chips read about 0.8 mm long because of shadows from the top lighting, so treat y as
  ±0.5–0.8 mm.
- Good enough for placing sockets. Caliper-check the mounting holes before a drop-in board.
- Close-up IMG_5966 confirms the silkscreen pin numbers on J44 and J45.

**Frame:** component side up, as installed. Origin at the top-left corner. x runs along the 108 mm edge,
y runs down the 60 mm edge. The top edge faces the IO and top-board cables; the left edge faces the
display ribbon.

| Feature | Centre x, y (mm) | Body (mm) | Orientation |
|---|---|---|---|
| **J44** display, 33 pins | 6.2, 39.0 | 4 × 12 | Lock side faces the **left** edge (ribbon arrives from the left; body edge 4.2 mm in). **Pin 1 at the top (−y) end**, 33 at the bottom (silkscreen) |
| **J30** touch, 8 pins | 17.1, 53.8 | 4–5 × 6.4 (with leads) | Opening (under the white actuator) faces the **left** edge; the solder legs are on the right. So the touch ribbon reaches about 15 mm in. **Pin 1 at the top (−y) end**, 8 at the bottom (silkscreen, from close-up photos) |
| **J100** top board, 23 pins | 22.4, 6.4 | 9 × 4 | Lock side faces the **top** edge. **Pin 1 at the right (+x) end**, 23 at the left (silkscreen) |
| **J46** IO board, 31 pins | 42.0, 6.5 | 11.5 × 4 | Lock side faces the **top** edge. **Pin 1 at the right (+x) end**, 31 at the left (silkscreen) |
| **J45** camera, 25 pins | about 6, 10 | | Opening faces left. Pin 1 at the bottom, 25 at the top (silkscreen) |
| **J2** speaker (Pico-SPOX) | 82.0, 3.5 | 4.3 × 5.75 | At the top edge, beside C9211 |
| Screw S1 | 30.35, 6.45 | | Between J100 and J46 |
| Screw S2 | 65.8, 2.85 | | Top edge |
| Screw S3 | 2.8, 29.9 | | Left edge, above J44 |
| Screw S4 | 23.25, 57.85 | | Bottom edge, right of J30 |
| Screw S5 | 70.85, 57.05 | | Bottom edge |
| Screw S6 | 105.1, 31.35 | | Right edge |
| Plated holes (no screw) | 3.0, 5.2 and 2.9, 56.9 | | Top-left and bottom-left corners; probably ground points |
| Touch ground pad | x 2.3–11.6, y 46.0–58.7 | about 9.3 × 12.7 | Bare gold area left of J30, via-stitched to ground, running into the stitched strips at the board edges. The ground pad on the back of the touch ribbon's GT911 rests on it |
| Alignment hole A1 | 3.1, 16.8 | about 2.5 across | Plain hole below J45, for a locating peg in the case |
| Alignment hole A2 | 93.4, 17.05 | about 2.5 across | Plain hole beside the part-number print, for a locating peg |

**Screw holes re-measured (2026-11-16):**
- **Outline by caliper:** 107.96 × 59.91 mm, so 108 × 60 mm stands.
- **Method:** two more straight-down photos of the bare board with a steel rule against one edge. Each
  was perspective-corrected to the calipered outline, and every hole centre read off at full resolution
  (about 23 pixels per mm). The screw rows above now hold the average of the two.
- **Agreement:** the two photos differ by at most 0.17 mm on any hole.
- **Each screw hole** is about 2.2 mm across in a 5 mm exposed ground pad. The two alignment holes are
  plain, about 2.5 mm, with no pad.

| Hole | Photo A | Photo B | Earlier estimate | Moved by |
|---|---|---|---|---|
| S1 | 30.37, 6.56 | 30.34, 6.39 | 30.4, 6.3 | −0.05, +0.17 |
| S2 | 65.81, 2.87 | 65.74, 2.79 | 65.8, 2.5 | −0.02, +0.33 |
| S3 | 2.84, 29.96 | 2.79, 29.84 | 2.6, 29.1 | +0.21, +0.80 |
| S4 | 23.22, 57.84 | 23.26, 57.84 | 22.7, 58.1 | +0.54, −0.26 |
| S5 | 70.80, 57.04 | 70.91, 57.11 | 71.0, 56.8 | −0.15, +0.27 |
| S6 | 105.10, 31.35 | 105.05, 31.31 | 104.8, 31.4 | +0.28, −0.07 |
| A1 | 3.14, 16.91 | 3.02, 16.73 | | |
| A2 | 93.39, 17.10 | 93.38, 16.97 | | |

S6 agrees with the hand reading of 2026-11-15 (3 mm from the right edge, 31 mm from the top).

**Sockets in the same photos (rougher: the legs are harder to pick than a hole):**
- **J44:** rear leg row at 7.9–8.0, 39.1; the breakout has 7.92, 39.0.
- **J100 and J46:** rear leg rows 8.2–8.3 mm from the cable edge, matching the breakout. J46's rear row
  is 16 legs at 0.61 mm pitch, centred at 42.1 mm (the breakout has 42.0). A first by-eye reading that put
  both sockets 0.4–0.6 mm off along the edge was wrong for J46; J100 isn't re-measured.

**The three 0.3 mm ribbons checked against the FH26W (2026-11-16):**

| | J44 (33) | J46 (31) | J100 (23) | FH26W |
|---|---|---|---|---|
| Tip goes into the old socket | 2.5 mm | about 2.45 mm | 2.5 mm | 2.5 ± 0.3 mm of bare contact |
| Stiffened tongue | 6.5 mm | 7.5 mm | 6.5 mm | 3.5 mm or more |
| Old socket width, lock closed | 11.55 mm | 11.55 mm (to re-measure) | about 8.5 mm | 11.4 / 10.8 / 8.4 mm |
| Old socket depth with legs | 3.5 mm | about 3.6 mm | about 3.6 mm | 3.2 mm |
| Ribbon tip width | 10.24 mm | 9.5 mm | 7.25 mm | 10.2 / 9.6 / 7.2 mm |

- **Contact pattern (microscope, J44, J46 and J100):** two staggered rows of pads, the row at the tip holding
  the two edge pads, with each tip pad's thin lead running back between two rear pads. That is Hirose's
  recommended FH26 ribbon. Contacts face the board.
- **J100's ribbon:** 12 pads in the tip row and 11 behind, 23 in all, and about 7.1 mm wide against a
  rule in the same image. It is printed `J2`, its connector's name on the top board; that isn't the
  mainboard's J2 (the speaker).
- **Leg pitch on the old sockets:** 0.60–0.61 mm on all three, in both ruler photos.
- **J30's ribbon tip (microscope):** eight straight fingers in a single row, bare from a sharp coverlay
  line to the tip, on a short tongue with shoulders behind it. That is the FH34SRJ's ribbon, not the
  staggered FH26 kind. The contact wear marks sit in the half nearest the tip, as a 1.5 mm insertion
  would leave them. The two outer fingers are wider and run straight into broad copper at the edges,
  which supports pins 1 and 8 being ground.
- **Which way round the FH26W sockets sit (microscope, J46, lock closed and open):** the dark flip lock
  (with Hirose's triangle and logo), a row of 16 legs and a metal tab at each end are on the board-edge
  side; a row of 15 legs is inboard. The ribbon enters on the lock side. J44 follows the same pattern:
  its inboard leg tips are at 7.9–8.0 mm, which is the smaller row's 0.5 mm leads on a body starting at
  4.2 mm. Earlier versions of the breakout and the extenders had these sockets the other way round.
- **By caliper (2026-11-16):** socket body edge from the board edge: J44 4.2 mm, J100 4.5 mm, J46 4.4 mm,
  J30 15.1 mm. J100's body runs from 17.5–17.75 to 27 mm from the display edge (centre about 22.3).
  J46 is 10.9–11.0 mm wide at the board and 10.6 mm at the lock (the earlier 11.55 was wrong).
- **Mechanical (2026-11-16):** board 1.0 mm thick; screws 3.5 mm across the head and 5.1 mm long
  including it; alignment pegs 1.8–1.9 mm. The frame's gaskets look about 2–3 mm tall uncompressed;
  not measured.
- **Speaker (J2): Molex Pico-SPOX, 1.5 mm pitch.** The header is an 87438-0243 and the plug an
  87439-0200 housing with 87421 crimp terminals. Measured: leads 1.50 and 1.53 mm apart in the two
  flattened ruler photos, housing about 4.5 mm wide and 5.4 mm deep, no metal side tabs. Molex: header
  4.3 mm wide, 5.75 mm deep (7.75 mm with tails), 4.45 mm high; plug 4.1 mm wide, 6.2 mm long, 3.35 mm
  high; 9.4 mm long mated. It is not a 1.25 mm PicoBlade, as first assumed. The drawings are in
  `parts-documentation/` (`874380243_sd.pdf`, `874390200_sd.pdf`).
- **J46's ribbon, plugged in (microscope):** it enters on the lock side, from the board edge. The face
  that shows is the covered one, with traces and vias under the orange cover layer; the bare staggered
  pads are on the other face, against the board. So it is bottom contact, as Hirose lists the FH26W.

**Still to capture on the mainboard:**
- [ ] Ground pins: beep every pin of J44, J46, J100 and J30 against a screw pad; note which are ground.
      On J30, check pins 1 and 8.
- [ ] 12 V pins on J46: beep from the barrel jack's centre pin on the IO board, through the cable.
- [ ] The two damaged parts at J30's pin 1 end: resistance across each, against the matching parts on
      the other lines.
- [ ] Speaker plug, by caliper, against Molex's 87439-0200: 4.1 mm wide, 6.2 mm long, 3.35 mm high.
- [ ] Height from the board's component face to the rear case (for the final board, not this one).

- **Verdict:** the FH26W suits all three ribbons. Open: J46's socket width reads 0.75 mm over an
  FH26W-31, though its 16 rear legs are the 31-pin count; and the sockets read 0.3–0.4 mm deep.
  Neither changes where the ribbon lands.
- **J30:** the solder legs are 18.2–18.7 mm from the display edge and the body is only about 3.4 mm
  deep, with its mouth at about 15 mm. The breakout's FH12 has its mouth at the same place but is
  5.6 mm deep, legs at 21.7 mm. So the touch ribbon must slide roughly 1.5–2 mm further in to reach the
  FH12's contacts.
- **Ribbon insertion (2026-11-16):** the tip goes **1.5 mm** into the old socket, so it reaches about
  16.5 mm in from the display edge.
- **J30 identified (2026-11-16): Hirose FH34SRJ-8S-0.5SH, or a copy of it.** Measured on the board: the
  socket is 6 mm wide and about 4 mm deep with the lock closed; the ribbon's tongue is 3 mm long, of
  which 1.5 mm goes in, up to a white line printed on it. Hirose's FH34SRJ-8S is 6.0 mm wide, 3.8 mm
  deep with leads, 1.0 mm high, back-flip, for 0.3 mm ribbon 4.5 mm wide with 1.5 ± 0.3 mm of bare
  contact and a 2.5 ± 0.3 mm stiffener. Every figure matches. The FH12 can't take this ribbon (it
  needs 4.4 mm of tongue), so the breakout's J30 is now an FH34SRJ-8S-0.5SH(50) (LCSC C88372) at the
  mainboard's position: front face 15.1 mm from the display edge, lands at 17.8–18.6 mm.

**Grounding (2026-11-16, photos of the board's back and of the display frame):**
- **The back of the mainboard has nine bare gold ground pads**, each about 8–10 mm square. The display
  frame behind it carries seven conductive fabric-over-foam gaskets that press on seven of them.
- **The GT911 sits under the board**, between the frame and the board's back, at the J30 corner. Its
  ribbon tail wraps round the display edge and into J30 from the left: 15 mm to the socket mouth plus
  1.5 mm inside it accounts for the 17.5 mm tail. The pad on the back at that corner (B1) has no
  gasket: the GT911's own ground pad rests on it.
- **The button/mic board grounds through a strip of metal-mesh tape** stuck to the bare area above J45
  on the component side. That area runs down the display edge as a sliver to a second bare patch by
  alignment hole A1, above the data-matrix label.

| Back pad | x (mm) | y (mm) | Gasket on the frame |
|---|---|---|---|
| B1 | 1.2–10.0 | 51.8–58.9 | No: the GT911's ground pad rests here |
| B2 | 27.8–37.9 | 51.7–59.5 | Yes |
| B3 | 99.8–107.1 | 50.1–59.5 | Yes |
| B4 | 99.8–107.1 | 34.9–44.7 | Yes |
| B5 | 66.7–74.8 | 24.5–34.5 | Yes |
| B6 | 1.2–10.0 | 18.5–26.6 | No |
| B7 | 6.5–16.5 | 1.8–9.7 | Yes |
| B8 | 74.0–84.2 | 1.6–9.7 | Yes |
| B9 | 90.4–98.6 | 2.0–12.0 | Yes |

Positions are in the board's own top-view frame (x from the display edge, y from the cable edge), read
from a perspective-corrected ruler photo of the back, to about ±0.3 mm. Tape pad on the component side:
x 0.3–8.5, y 0.3–4.6, a sliver at x 0.3–1.7 down to y 14.2, then x 0.3–8.0, y 14.2–18.5 around A1.

What this means for the breakout board:
- **J44 and J30 are about 18 mm apart (centre to centre: 11 mm across, 15 mm down), both opening to the left edge.** The
  display and touch ribbons arrive from the same side, so they must stay together on the breakout.
- **J100 and J46 sit side by side on the top edge, about 20 mm apart, both opening upward.**
- On the breakout, put each socket at these positions and orientations relative to the others, so every
  ribbon lands as it did on the mainboard.
- Still unknown: each ribbon's spare length beyond its socket. Measure it before relying on any offset.

### Optional: unlock the Echo and run LineageOS as a measuring aid

Stock Fire OS gives no shell, so software can't help until the bootloader is unlocked
([amonet-cronos](https://xdaforums.com/t/unlock-root-twrp-unbrick-amazon-echo-show-5-2nd-gen-2021-cronos.4772596/):
a BROM-mode exploit with a shorting trick, so the case is opened again). After that, the unofficial
**LineageOS 18.1** build for `cronos` gives a root shell over USB (adb), with **no Wi-Fi needed**. TECHO5
installs on top of LineageOS and adds nothing for measuring, so stop at LineageOS.

What it would give:

| Gives | How |
|---|---|
| **This unit's panel variant** | kernel command line `lcm=…` (TECHO5's unit: `…cronos_st_truly`) |
| **A backup of the GT911 configuration** (insurance against a corrupted touch chip later) | read only: `i2ctransfer -f -y 2 w2@0x5d 0x80 0x47 r186`. Never write it back unless the chip is proven corrupted |
| **Pin identification by toggling** while probing: backlight on/off and brightness, buttons, privacy latch | `/sys/class/leds/lcd-backlight/brightness`, `getevent`, `/sys/devices/platform/gpio-privacy/` |

What it can't give: **which connector pin is which.** Linux knows processor GPIO numbers (e.g. volume up is
`gpio-393`), not J44/J100 pin numbers, so the continuity map and probing are still needed. TECHO5's
published hardware dump already lists the software side (touch 0x5d, button and privacy GPIOs, amplifier).

Costs: a small risk of bricking (amonet has an unbrick path); the unit can't easily go back to Fire OS;
deregister it from Amazon first. Keep it off the home network (adb over USB only).
**Decision (2026-11-15): go ahead.** The unit won't be used as an Echo again.

#### Procedure

TWRP alone may be enough: the unlock finishes in TWRP, which already has a root shell over USB. Only install
LineageOS if TWRP lacks something below.

From the amonet-cronos thread (read 2026-11-15; current release `amonet-cronos-v2.0.2.zip`):
- **Patched in Fire OS 6575.1 (NS65751/8848).** A unit on that version or later can't be unlocked. Don't let
  it update: keep it off Wi-Fi or unplugged when not in use, and boot straight into fastboot.
- The scripts are for **Windows or Linux only** (`fastbrick.bat` / `fastbrick.sh`), not macOS. Use a native
  Linux or Windows machine; a VM's USB passthrough can drop the device mid-exploit, and **an interruption after
  the 10-second grace period bricks it permanently** (this model has no BROM USB-download rescue).
- After unlocking, never modify LK, Preloader, TEE or other critical partitions. Going back to Fire OS is
  possible (stock `update.bin` from ftvdb.com, installed in TWRP).
- With the exploit installed: Volume Up while powering on → TWRP; Volume Down → fastboot. UART RX is on TP55.

**This unit (checked 2026-11-15):** the Alexa app shows software version **13222532996**, which an XDA post
identifies as **Fire OS 6574.1 (NS65741/8151)**: one release *before* the patched 6575.1 (NS65751/8848), so
the unlock should work. Thread reports: the patched builds carry a 2026 bootloader (`fbacd25-20260415…`)
that amonet v2.0.2 refuses; 8151 units report `lk_build_desc 44072a3-20240709_162755`. **Unplug the Echo
now and keep it offline until the unlock**, so it can't update to 8848. Before running the exploit, check
`fastboot getvar lk_build_desc` shows the 2024 build. Apple Silicon users report a UTM VM getting stuck at
`USB Transfering...`; use a native x86 Linux or Windows machine (a third-party macOS patch exists in the
thread, but isn't from the exploit's authors).

**macOS patch review (2026-11-15)**: `amonet-cronos-v2.0.1-mac-r33-patch.zip` (codeVerine, thread page 28)
compared with the v2.0.1 and v2.0.2 releases:
- v2.0.1 → v2.0.2 changes **only** `fastbrick.sh`/`.ps1`: v2.0.2 adds `check_patched`, which refuses
  bootloaders built in 2026 or later. Payload, TWRP, LK, preloader and Python modules are byte-identical. So
  applying the patch over v2.0.2 equals v2.0.1 + patch; the only loss is that refusal (irrelevant here: the
  2024 LK; check `lk_build_desc` by hand anyway).
- The patched `fastbrick.sh` is a faithful port: same flow, prompts and success test (exit 124 after an
  8-second hang). Changes: no bash-4 `declare -A`; a pure-bash `timeout` fallback (harmless quirk: a failed
  attempt can take up to 8 s to return); `cd` to its own folder; and the flash line uses Google's fastboot with
  `-S 256M` instead of the authors' modified Linux fastboot. The 114 MB `fastbrick.img` exceeds the device's
  109 MB download limit, and `-S 256M` makes stock fastboot send it raw in one oversize download, which is what
  the exploit needs. That equivalence is the poster's claim (one tested unit), not the authors'.
- `profile.sh` only drops an empty `declare -A IMAGE_MAP` (no effect on cronos).
- `bin/fastboot` (SHA-256 `798831fd…28fc`) is a universal macOS binary carrying Google LLC's Developer ID
  signature (team EQHXZ8M8AV), as Google's platform-tools r33.0.3 is. Verify on the Mac before use:
  `codesign --verify --strict -vv bin/fastboot` and `codesign -dvv bin/fastboot` (Authority = Developer ID
  Application: Google LLC). A valid signature means the bytes are Google's.
- `fastbrick-mac.sh` (optional) makes no network calls and needs no sudo; `--check` is a read-only dry run.
- **Verdict:** low risk and usable from the Mac. Host-side failure modes look like "keeps retrying", not a
  brick: the brick risk is on the device during its 5-minute write (keep it on mains, don't touch it).

1. **Check the Fire OS version while it is still registered.** Touch is dead, so use the Alexa app
   (Devices → this Echo → Device settings → About → device software version), or ask "Alexa, what's your
   software version?". Compare with 6575.1 / 8848. Then **deregister** it in the app, and don't set up
   Wi-Fi again.
2. **Unlock** by following the thread exactly: on mains power, hold all three top buttons until
   `=> FASTBOOT mode...` shows, then connect USB to the Linux/Windows machine running `fastbrick`, type
   `YES`, wait up to five minutes, **never interrupt it**. It reboots into TWRP. Check with
   `fastboot getvar unlock_status` → `true`.
3. **In TWRP** (`adb shell`, root):
   - Panel variant: `cat /proc/cmdline | tr ' ' '\n' | grep lcm=`
   - Board facts: `cat /proc/idme/board_id`
   - Backlight test: `ls /sys/class/leds/` and `echo 0 > /sys/class/leds/lcd-backlight/brightness`, then `255`.
   - Touch: `ls /sys/bus/i2c/devices/` (look for `2-005d`) and `which i2ctransfer getevent`.
4. **GT911 config backup:** if TWRP has `i2ctransfer`, run (read only)
   `i2ctransfer -f -y 2 w2@0x5d 0x80 0x47 r186` and save the output here. If not, install the LineageOS
   18.1 `cronos` zip from TWRP, skip Wi-Fi setup, turn on USB debugging and rooted debugging, and run it
   from `adb root; adb shell`.
5. **Wipe:** format data in TWRP (`adb shell twrp format data`). That removes the Wi-Fi password and
   Amazon tokens from the eMMC.

Results:

| Item | Value |
|---|---|
| `lcm=` | **`1-st7701s_wsvga_dsi_vdo_cronos_st_truly`**, `nt35521_id=3`, `bl_level=90`: **Truly panel confirmed** |
| `board_id` | `01E0001430020020` |
| TWRP kernel | Linux 4.9.77 armv7l (32-bit) |
| I²C devices | bus 0: `tlv320aic3101` 0x18, `camera_sub` 0x21, `camera_main` 0x2e, `alsps` 0x44; bus 1: `sym827-regulator` 0x60; bus 2: `max98396` 0x3d, `gt9xx` 0x5d |
| Touch in TWRP | **GT911 not answering, confirmed.** The Goodix driver (V2.4) powers on, resets the chip ("Guitar reset") and tries a 1-byte write to 0x8041 at 0x5d five times; each fails with errcode −6 (no acknowledge), then "I2C communication ERROR!" and the driver doesn't bind. The amplifier on the same bus works, so the bus is fine. Causes, all on the touch branch: no supply to the GT911; reset or INT line cut (a floating INT during reset can make the GT911 come up at **0x14** instead of 0x5d, where this driver never looks); SDA/SCL branch to J30 cut; or a dead GT911. **Decisive test:** power the touch ribbon from an ESP32 through the J30 breakout and scan for 0x5d and 0x14. If it answers, the ribbon and GT911 (the reused parts) are fine and the fault is on the mainboard |
| Tools in TWRP | `getevent` only; **no `i2ctransfer`/`i2cget`** |
| Touch wiring (device tree) | GT911 on I²C bus 2 (`i2c@11009000`) at 0x5d. **RST = GPIO 123, INT = GPIO 122** (MT8163 numbering). **AVDD (`vdd_ana`) = MT6323 `vmch`**, set to **3.0 V**; the touch subsystem's `vtouch` = MT6323 **`vgp1`, 3.3 V**, always on |
| Touch state in TWRP | Both supplies report **enabled** (`vmch` 3.0 V, `vgp1` 3.3 V). RST (GPIO 123) is an output driven **high** and reads back high: reset released. INT (GPIO 122) is an input, no pull, reading **low**. So software has the GT911 powered and out of reset, yet it doesn't acknowledge: the fault is physical. With probes, check at J30 for ~3.0 V and ~3.3 V, RST high, and SDA/SCL idling high |
| **GT911 factory configs (device tree)** | The device tree carries **five 186-byte GT911 configs** (`goodix,cfg-group0`–`4`), one per touch-sensor ID: 480×960, 5 touches, valid checksums. Only three are distinct: group0 (version 0x49, checksum 0xCC), group2 (0x49, 0xED), and groups 1/3/4 (0x54, 0xF4). The driver picks one by the sensor ID the chip reports (register 0x814A). **This is the config backup**: if the GT911 ever loses its config, restore the group matching its sensor ID. Saved locally in `echo-show-5-dumps/cronos-dt.txt` |
| I²C speed | All three buses run at **300 kHz** (`clock-frequency` 300000): GT911 on bus 2 at that speed is a safe setting (100 or 400 kHz also within GT911 spec) |
| Panel reset | `lcm_reset_gpio` = **GPIO 83**: output, high (released) while the panel runs |
| Pin functions (debugfs pinmux) | **Backlight PWM = MT8163 pin 59 `DISP_PWM`**, in its PWM function (mode 1), output. Touch I²C = pins 77/78 (`SDA2`/`SCL2`). Touch INT/RST use pins 122/123, whose primary names are `MSDC1_CLK`/`MSDC1_DAT0` (SD-card pins reused as GPIOs, with the SD-card supply `vmch` as touch AVDD). Panel reset = pin 83 `LCM_RST`; `DSI_TE` (pin 84) unused (video mode) |
| Backlight | `lcd-backlight` uses **`led_mode` 5** (MediaTek display PWM, `disp_pwm0`), default brightness 90, `custom-brightness-table` and a thermal limiter (`thermal_brightness` 88). Its listed `gpios` (GPIO 43) stays an output **low** at both brightness 0 and 255, so it isn't the boost enable; the PWM itself comes from the display block's PWM pin. Find it on J44 by probing with the brightness loop (a PWM line averages to a different DC level as brightness changes) |
| Buttons and privacy (live levels, nothing pressed) | Volume up **GPIO 36**, volume down **GPIO 37**: inputs, pulled up, read high (active low). Lens-cover switch **GPIO 142**: input high. Privacy button **GPIO 47** and privacy state **GPIO 48**: inputs high. Privacy enable **GPIO 27**: output low (latch not engaged). Amplifier reset **GPIO 35**: output high. Mic FPGA `cdone` **GPIO 49**: low (FPGA not loaded in TWRP) |
| Light sensor | **Sensortek STK3x1x** ambient-light + proximity sensor at **0x44 on I²C bus 0**, supply MT6323 `vcn28` (2.8 V), with factory calibration in idme (`alscal`). The TWRP driver fails at start-up with −14 (a software fault, not "no acknowledge"); TECHO5 reports it working on LineageOS. Worth reusing for automatic night dimming of the radar, if it sits on a reusable board (likely the top board via J100: to confirm) |
| Temperatures (TWRP, idle on the bench) | CPU 43.5 °C, PMIC 39.2 °C, SoC sensors 42.6–45.1 °C; board thermistors `therm0_s`/`therm1_s` 32 °C, `therm2_s` 37 °C (the NTC parts on the mainboard) |
| Full capture (2026-11-15) | `echo-show-5-dumps/live/`: `/proc` files, all 32 idme values (`idme.txt`: includes serial, MACs and `unlock_code`, so keep private), debugfs regulator and clock summaries, GPIO and full pinmux/pinconf tables, wakeup sources, sysfs attributes for thermal, power, input, I²C, LEDs, regulators, sound and misc devices (`sysfs-classes.txt`), thermal readings. `echo-show-5-dumps/emmc/`: whole-eMMC image (gzip) plus both boot areas, with SHA-256s from the device and the Mac. RPMB isn't readable this way and wasn't captured. **Verified:** whole eMMC 7,818,182,656 bytes, SHA-256 `4252fab5…0c42` on the device, on arrival, and after decompressing `mmcblk0.img.gz` (1.51 GB); `boot0` `2d2e7866…e0eb` and `boot1` `f4d8723f…004f` match too. Taken after the data wipe and the unlock, so it holds Fire OS's system partition, TWRP, the amonet-modified boot chain and the factory data, but no user data |
| What the image adds (checked 2026-11-15) | Little new. **Partitions** (GPT): kb, dkb, lk, tee1, logo, tee2, expdb, MISC, boot, recovery, swdl, system (3.25 GB), cache, persist, metadata, userdata (4.1 GB, wiped). **`boot`** holds Fire OS's kernel with **11 device trees**, one per board revision (proto, hvt, evt, dvt, dvt_3_1–3_3, pvt, pvt_4_1–4_3); this unit boots **`pvt_4_3`**, identical to the live tree for touch, panel, backlight and regulators (saved in `echo-show-5-dumps/fireos-dtbs/`). **`expdb`** holds no crash logs, so no record of touch working before the damage. **`system`** has no GT911 firmware or panel files. **`persist`** contains Amazon device-identity data (`amzn_dhav2`): keep private |
| Live regulator use | `regulator_summary` confirms the panel driver (`lcm@0`) holds **`vio28` 2.8 V** and **`vgp3` 1.8 V**. `vmch` (3.0 V) is on with no consumer, because the failed touch probe never claimed it. The display PWM runs from a **26 MHz** clock (`mm_disp_pwm026m`); DSI engine clock 364 MHz |
| Hardware dumps | `firmware/cm4/echo-show-5-dumps/` (local only, excluded via `.git/info/exclude`; contains the unit's serial): device tree (`cronos.dtb`, decoded `cronos-dt.txt`), `regulators.txt`, `mt_gpio.txt` (pin mode/direction/level table), full `dmesg.txt`, `cmdline.txt`, `partitions.txt` |
| Backlight in TWRP | `/sys/class/leds/lcd-backlight`, `max_brightness` 255; accepts 0, 255, 90 and reads back. TWRP's screen timeout sets it to 0, so wake the screen (power/mute button) before testing. For the powered J44 probing, toggle it while measuring: `adb shell 'while true; do echo 0 > /sys/class/leds/lcd-backlight/brightness; sleep 2; echo 255 > /sys/class/leds/lcd-backlight/brightness; sleep 2; done'` (Ctrl-C to stop) |
| GT911 config (186 bytes) | |
| `unlock_status` | `false` before the unlock (dry run 2026-11-15); **unlocked 2026-11-15** with amonet-cronos v2.0.2 + the macOS patch (`fastbrick-mac.sh`), booted into TWRP |
| Amazon account | **Deregistered** 2026-11-15, before the wipe |
| Wipe | **Done 2026-11-15**: `twrp format data` and `twrp wipe cache` (mke2fs). `/data` now holds only `lost+found` and an empty `media`; `/data/misc/wifi` is gone. Bootloader, exploit, TWRP and factory data untouched. To re-enter TWRP: hold **Volume Up only** while plugging in power |
| `lk_build_desc` | `44072a3-20240709_162755` (2024: not the patched bootloader) |
| Anti-rollback | TEE `0x0104`, LK `0x0103`, PL `0x0103`; `rpmb_state: 1` |

Dry run from the Mac (`fastbrick-mac.sh --check`, macOS bash 3.2, `/usr/bin/timeout` present, fastboot
33.0.3-8952118): device detected as CRONOS, payload `fastbrick.img` 114,294,812 bytes. The three patched
files' SHA-256s match the patch's own list. For the real run, use `fastbrick-mac.sh` rather than the patched
`fastbrick.sh`: it waits 30 s per attempt instead of 8, so a slow USB transfer can't be cut short and
reported as "most likely successful", and it logs each attempt to `amonet/logs/`.

## Test equipment

### Shopping list (measurement and early tests)

| Item | Spec | Plugs in | What it enables |
|---|---|---|---|
| **Fine needle-tip probe leads** | Sharp tips (ideally replaceable), insulated as close to the tip as possible, plugs to match the meter (usually 4 mm shrouded banana) | The meter | Mapping the mainboard's J44 by continuity; safe powered measurements |
| **FPC breakout, 33-pin, 0.3 mm** | FPC socket to 2.54 mm pins; for 0.2 mm FPC | The panel's ribbon | Panel power, reset, ID and backlight pins for the bench prototype. Not DSI |
| **FPC breakout, 31-pin, 0.3 mm** | as above | The IO board cable's mainboard end | Map the IO board (12 V, ground, USB) by continuity with ordinary probes |
| **FPC breakout, 23-pin, 0.3 mm** | as above | The top board cable's mainboard end | Map buttons, LEDs, privacy switch and microphones; then test buttons and LEDs from any ESP32 |
| **FPC breakout, 8-pin, 0.5 mm** | as above; for 0.3 mm FPC | The touch ribbon | Map J30; then run the GT911 from an existing ESP32 over jumper wires |
| Dupont jumper wires | Female–female and female–male, 10–20 cm | Breakouts to an ESP32 | Early touch, button and LED tests |
| Logic analyser *(optional)* | Cheap 8-channel, 24 MHz USB type | Breakout pins | Confirms I²C on touch (address 0x5d) and identifies button and LED lines |

Search terms: "FPC to DIP adapter 0.3mm 33 pin", "FPC adapter board 0.5mm 8P". 0.3 mm boards are less
common than 0.5 mm. Buy the **exact pin count**, because a ribbon won't seat properly in a larger socket.
If 31 or 23 pins at 0.3 mm can't be found, the fine probes cover those two (they're only needed for the
drop-in board).

**Chosen:** Uxcell 0.3 mm adapters from Harfington, £7 each:
[31-pin](https://www.harfington.com/en-gb/products/p-1066369) (SKU a21032300ux0103) and
[33-pin](https://www.harfington.com/en-gb/products/p-1042710) (SKU a21032300ux0104). Per the photos and
description: the flip-lock socket comes **soldered on**; the 67 × 30 mm board breaks out to rows of plain
holes at 2.0 mm and 2.54 mm pitch (no header pins; probe the holes directly or solder in a strip). The
board is a **51-pin layout with a 33-pin (or 31-pin) socket fitted**, so its silkscreen numbers won't
match the ribbon's 1–33. Map each hole to a ribbon pin by continuity before trusting any label. Not
stated: the FPC thickness the socket takes (0.2 mm is standard for 0.3 mm pitch).

**Contact side doesn't matter on a breakout.** The ribbon can go in either way up to meet the socket's
contacts. Turning it over mirrors the numbering, though (breakout pin 1 = ribbon pin 33). Check against a
known ground pin before trusting the numbers.

**DSI won't work through a breakout.** The display's high-speed pairs need short, matched traces, so the
breakout is for power, reset, ID and backlight only. A breakout also can't reach the mainboard's J44
directly: that would need a 33-pin 0.3 mm jumper cable, and flat cables that fine are rare.

### Custom breakout board (v2, drop-in, to order)

One board for all of it: [echo-breakout/](echo-breakout/README.md) (KiCad 10, JLCPCB-assembled). It
replaces the separate adapters above. It has the mainboard's outline (108 × 60 mm, 4 layers) and screws
onto the original M2 posts, with each socket where the original is. The ribbons are too short to reach
anything else: J44 17.5 mm, J30 about the same, J100 23 mm, J46 44 mm. Socket positions come from
"Positions from the photos" above.

| Connector | Socket on the board (LCSC) | Broken out to |
|---|---|---|
| J44 | Hirose FH26W-33S-0.3SHW (C424654) | 2 × 17 header (last pin GND) |
| J46/J4 | Hirose FH26W-31S-0.3SHW (C2973806) | 2 × 16 (last pin GND) |
| J100 | HCTL HC-FPC-03-10-23RLTAG (C3012331) | 2 × 12 (last pin GND) |
| J30 | Hirose FH34SRJ-8S-0.5SH (C88372) | 1 × 8 |
| Speaker (J2) | Molex Pico-SPOX 87438-0243 (C240853) | 1 × 2 |

Header pin numbers are the **Echo's** pin numbers (mainboard silkscreen pin 1) on every header,
J30 included.

Hirose's J100 socket was **out of stock** on 2026-11-16, so the BOM has a copy with the same lands:
HCTL (Huacan Tianlu) HC-FPC-03-10-23RLTAG (C3012331). Two more copies: Hongcheng HC-FPC-0.3-23P-FH10
(C49166929), XFCN F0302-B-23-10G-R (C25170829).

It also carries:
- an ESP32-C3 SuperMini socket (component side up, USB pointing left, into the board), plus an EXT
  header for any other ESP32
- a switched touch supply (P-MOSFET on IO4, with an LED)
- 220 Ω series resistors and 2.2 kΩ pull-ups (to the switched supply) on the touch I²C, INT and RST
- two `T_VDD` pins on the touch bus, for the touch side's two rails (3.3 V only; a 3.0 V rail, if needed,
  comes from a bench supply)

Notes:
- The screw holes are 2.6 mm plated holes in grounded copper pads, at positions re-measured from ruler
  photos on 2026-11-16 (two photos agree within 0.2 mm). Two 2.5 mm alignment holes take the case's pegs.
- No tracks or vias on the outer layers within 2.6 mm of a hole centre, so a screw head only touches
  solder mask over ground.
- Bare ground pads match the mainboard's: left of J30 and in the top-left corner on the component side
  (touch ribbon, mic board tape), and nine on the back for the display frame's gaskets and the GT911.
- J30 is the same socket type as the original (FH34SRJ-8S-0.5SH) at the original's position.
- While the touch supply is off, firmware must keep SDA, SCL, INT and RST low or high-impedance.
- The back faces the display frame: trim the pins and tape over them, but leave the bare pads uncovered.

The touch bus reaches J30 through jumper wires (a patch field). J30 isn't mapped yet, so a wrong guess
costs a wire, not a board. The board is for unpowered mapping and for running the touch ribbon, buttons
and LEDs from the ESP32. **Never connect the Echo's power adapter while it's fitted.**

#### Ribbon extenders

[ribbon-extenders/](ribbon-extenders/README.md) has 2-layer flex cables that add about 100 mm to J44,
J46 and J100, so the breakout can sit on the bench. Each has an FH26W socket at one end and contacts
to Hirose's FH26 FPC spec at the other, and keeps the Echo pin order. J30 uses an off-the-shelf 8-pin,
0.5 mm extension board and a same-side (type A) FFC cable. The extenders aren't impedance-controlled,
so DSI through the J44 extender is the least certain part.

### Later, for the bench prototype

| Item | Spec |
|---|---|
| Raspberry Pi CM4 and/or CM5 | With Wi-Fi, eMMC and 4 GB RAM (e.g. CM4104032) |
| Official IO Board | Proves the OS, panel overlay, touch and launcher before our own board exists (the CM5 IO Board for both modules *(check)*) |
| 12 V → 5 V buck module | At least 3 A, fixed 5 V or adjustable |
| A spare 12 V supply or the original LA23UK adapter | 12 V, 1.25 A or more |

## Build plan

1. **Software on a stock Pi.** The launcher, the radar web app and its local data service, and Home
   Assistant in the second view, on any Pi 4 or the CM4 IO Board with an HDMI screen. None of this needs
   the Echo.
2. **Bench prototype.** The module on an official IO Board, plus a small adapter PCB from its 22-pin DSI
   connector: a J44-compatible 33-pin socket, 1.8 V and 2.8 V regulators, reset level shifting, a
   backlight boost LED driver with PWM dimming, and an 8-pin socket for the touch ribbon. DSI cannot go
   through jumper wires or a generic breakout: the pairs need short, length-matched, 100 Ω differential
   traces. Proves the panel overlay, touch and backlight.
3. **Drop-in carrier board and a new IO board** (plan: [CARRIER.md](CARRIER.md)). A PCB with the KC19
   board's outline and mounting holes, a CM4 or CM5 on its two 100-pin board-to-board connectors, and **J44,
   J30, J100 and J45 in the same positions** so those original cables plug straight in. The new IO board
   has USB-C PD (power and data) and a 3.5 mm headset jack, either in the original back plate's openings
   or in a 3D-printed back plate that can also take an RJ45 Ethernet jack and vents. It joins the carrier
   by separate power/USB, audio and Ethernet cables. The carrier carries the 5 V
   buck-boost, the panel supplies, the backlight driver, the speaker amplifier and a headphone DAC, the
   optional mic ADC and camera domains behind jumpers, a populated JST SH debug connector, and a heat path
   to the speaker's copper plate. USB goes to the module through a jumper-controlled switch, with an RPIBOOT
   jumper so the USB-C port can reflash the eMMC.

### Drop-in board checks
- **Height:** the mainboard occupied 8.8 mm. Carrier, connector stack height (1.5 mm or 3 mm mated),
  the module and its tallest parts, plus any heat spreader, must fit. Choose the stack height from this.
- **Wi-Fi antenna:** the Echo's antennas were printed on the mainboard's edge. Use the module's U.FL with an
  antenna at the case's top edge in the same place.
- **Power budget:** CM4 (or CM5) at full load, backlight at full brightness, speaker and camera together: about
  11 W, a quarter of a 45 W USB-C PD supply.

## Open questions

- [x] J44 connector pitch: 0.3 mm (33 pins). Mating socket chosen: Hirose FH26W-33S-0.3SHW (see the breakout board).
- [x] J30 pitch: 0.5 mm (8 pins). Mating socket: Hirose FH34SRJ-8S-0.5SH (matches the original; an FH12 is too deep for the ribbon).
- [ ] Order the breakout board ([echo-breakout/](echo-breakout/README.md)), and check the socket rotations in JLCPCB's preview.
- [ ] J44 pinout (table above).
- [ ] J30 pinout and the touch I/O voltage.
- [ ] Panel variant confirmed by DSI ID read (expected Truly, 0x3). The panel's own chip can't be seen without
  removing the screen: both ribbons run straight under the tight front frame. **Don't separate the screen from
  the frame**; the ID read is the way to confirm it. A resistor on the J44 panel-ID pin (see "Variant select")
  is a second check once J44 is mapped.
- [ ] Backlight LED voltage, string count and current (from R335/R336 values).
- [ ] J100 pinout (buttons, LEDs, privacy switch, microphones).
- [ ] IO board outline, mounting, barrel-jack opening and micro-USB slot (for the new IO board).
- [ ] Free space behind the rear wall (for USB-C, RJ45 and the 3.5 mm jack together), and the rear plate's
  clips, bosses and speaker seal (for a printed back plate).
- [ ] Microphones: mic ADC choice and its Linux driver (see CARRIER.md).
- [ ] Camera (J45): sensor identity, Linux driver, pinout, socket position.
- [ ] Internal cavity dimensions, for the drop-in carrier outline and heat spreader.
- [ ] Speaker (8 Ω, Pico-SPOX 2-pin): choose the carrier's I²S amplifier (e.g. MAX98357A at 5 V).
- [ ] Will the carrier, module and connector stack fit within the 8.8 mm the mainboard occupied?
- [ ] Antenna for the module's U.FL, and its place on the case's top edge.
- [ ] Power: measured total on the bench; USB-C opening in the case.
