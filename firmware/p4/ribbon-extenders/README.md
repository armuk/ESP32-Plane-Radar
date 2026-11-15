# Echo Show 5 ribbon extenders

Flex extension cables that make the Echo's short ribbons about **100 mm longer**. With them, the
[breakout board](../echo-breakout/README.md) (and later the P4 board) can sit on the bench beside the
open Echo instead of screwed into its case. See [ECHO-SHOW-5.md](../ECHO-SHOW-5.md) for the hardware.

| Extender | Echo ribbon | Socket at end A (LCSC) | Width: socket end / body / tip |
|---|---|---|---|
| [`j44-ext`](j44-ext) | J44 display, 33 pins, 17.5 mm | FH26W-33S-0.3SHW(60) (C424654) | 21.0 / 10.6 / 10.2 mm |
| [`j46-ext`](j46-ext) | J46 IO board, 31 pins, 44 mm | FH26W-31S-0.3SHW(60) (C2973806) | 19.8 / 10.0 / 9.6 mm |
| [`j100-ext`](j100-ext) | J100 top board, 23 pins, 23 mm | FH26W-23S-0.3SHW(60) (C3168948) | 15.0 / 7.6 / 7.2 mm |

![J44 extender, top](j44-ext/renders/top.png)

## How they work

Each extender is a 2-layer flex PCB, 100 mm from end to end:

- **End A (wide end):** an FH26W socket on the top face, on a 0.8 mm FR4 stiffener. The Echo's own
  ribbon plugs into it exactly as it plugged into the mainboard.
- **End B (tip):** contacts made to Hirose's recommended FPC dimensions for the FH26:
  - width 0.3 × pins + 0.3 mm
  - 0.3 mm pitch, starting 0.3 mm from each edge
  - exposed from 0.3 to 2.5 mm from the tip
  - a 0.1 mm polyimide stiffener behind them makes the tip 0.2 mm thick
- **Plugging in:** the tip goes into the breakout's socket (or the P4 board's) in place of the Echo ribbon.

**The pin order is unchanged end to end.**
- Echo pin 1 is on the edge marked `PIN 1` at both ends, so the breakout's header labels stay correct.
- This needs the contacts on the opposite face from the socket. Otherwise the pin order comes out
  mirrored, so each line changes layer once.
- All the vias are on the stiffened end A, none in the part that bends:
  - front-row pins drop through vias in front of the socket
  - rear-row pins fan out to 1.2 mm pitch behind it, then drop through vias

**Using one:**
1. Plug the Echo ribbon into end A with its contacts facing the extender, as it went into the
   mainboard. The ribbon comes in from the narrow side of the socket, at the `1` end.
2. Hold the extender with `THIS SIDE UP` facing up: the contacts at the tip then face down. Insert the
   tip into the breakout socket, then close the actuator.
3. **Before the first powered test**, beep Echo pin 1 through the cable: from the ribbon's first
   contact to the breakout header's pin 1.

## Limits

- **The lines aren't impedance-controlled.**
  - They are plain parallel lines with no ground layer, because the ground pins of J44, J46 and J100
    aren't mapped yet.
  - Neighbouring lines stay neighbours all the way along, so a DSI pair stays side by side.
  - Continuity mapping and slow signals (I²C, GPIO, power enable) are fine.
  - The **display's MIPI-DSI** (2 lanes at about 424 Mbit/s) over an extra 100 mm should work, but it's
    the least certain part. If the picture is noisy, try without the J44 extender.
  - Once J44 is mapped, a version with a ground layer and matched pairs can replace it.
- **J46 carries the Echo's 12 V input** on 0.1 mm lines. These extenders are for unpowered mapping and
  for 3.3 V/5 V testing from the ESP32. **Don't run the Echo from its own adapter through them.**
- **J100 (C3168948) was out of stock at LCSC on 2026-11-15**, the same as for the breakout. Pre-order it,
  or have the flex made without assembly and fit the socket yourself.

## Ordering at JLCPCB (flex PCB)

Each extender is a separate order. In each `jlcpcb/` folder: `<name>-gerbers.zip`, `bom.csv` and
`cpl.csv`.

| Option | Setting |
|---|---|
| Product | **Flex PCB**, 2 layers |
| Thickness | **0.11 mm** (25 µm polyimide, 12 µm copper) |
| Size | 100 mm × 21.0 / 19.8 / 15.0 mm (J44 / J46 / J100) |
| Surface finish | ENIG (the contacts mate with gold-plated sockets) |
| Coverlay | Yellow (any colour), white silkscreen |
| Outline tolerance | **±0.05 mm** (the tip width must be within ±0.05 mm) |
| Stiffener 1 | **FR4 0.8 mm**, bottom face, under the socket (`Stiffener_FR4_0.8mm_bottom.gbr`) |
| Stiffener 2 | **Polyimide 0.1 mm**, top face, behind the contacts (`Stiffener_PI_0.1mm_top.gbr`) |
| Assembly | Top side: the FH26W socket only (BOM and CPL) |

Before paying:
- **Ask JLCPCB to confirm the contact end is 0.2 ± 0.03 mm thick** in total. The FH26 needs that
  (0.11 mm flex + 0.1 mm PI stiffener ≈ 0.21 mm). If their stackup comes out different, change the PI
  stiffener thickness, not the flex.
- In the assembly preview, check that the socket's opening faces the **narrow end of the wide pad**,
  away from the body. That's the side with the two via rows.
- The stiffener outlines are also on User.1 and User.2 in the KiCad files, with notes. JLCPCB asks
  for the stiffeners to be described on the order as well as in the files.

## The touch ribbon (J30): buy it off the shelf

J30 is 0.5 mm pitch, 8 pins, so standard parts work:

1. An **8-pin, 0.5 mm FFC/FPC extension board**: two sockets back to back, joined pin for pin. Its
   sockets must take a **0.3 mm thick** ribbon, like the Echo's touch ribbon; most do.
2. An **8-pin, 0.5 mm FFC cable, "same side" contacts (type A)**, about 100 mm long.

With a straight-through extension board, a type A cable keeps pin 1 as pin 1 whichever contact type
the board's sockets are. Beep it through before plugging the panel in anyway.

## Rebuilding

```bash
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
$PY gen_extenders.py      # all three boards, routed (no autorouter) -> <name>/<name>.kicad_pcb + bom.csv
for n in j44-ext j46-ext j100-ext; do kicad-cli pcb drc $n/$n.kicad_pcb; done
sh make_jlcpcb.sh         # Gerbers, drill, CPL, renders
```

`LENGTH` at the top of `gen_extenders.py` sets the length (100 mm). Every board passes DRC with no
errors.
- **Rules:** 0.1 mm tracks and clearance, regular 0.3/0.55 mm vias, 0.2 mm copper-to-edge.
- **One check is turned off on purpose:** solder mask bridges. Coverlay can't form 0.1 mm strips
  between pads, so each pad row and the contact area have one opening each.

Sources:
- Hirose FH26 catalogue (recommended FPC dimensions and construction):
  https://www.farnell.com/datasheets/13429.pdf
- JLCPCB flex capabilities: https://jlcpcb.com/capabilities/flex-pcb-capabilities
