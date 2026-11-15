"""Generate the Echo Show 5 (cronos) breakout board (v2: drop-in, mainboard outline).

Run with KiCad's bundled Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen_board.py
Writes echo-breakout-unrouted.kicad_pcb and echo-breakout.dsn (for Freerouting).
"""
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
FPDIR = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/'
W, H = 108.0, 60.0           # board size, mm: the Echo mainboard outline (KC19-TI MLB)
mm = pcbnew.FromMM
V = pcbnew.VECTOR2I_MM

board = pcbnew.BOARD()
board.SetCopperLayerCount(4)    # 2 layers can't fan out the 0.3 mm sockets in the mainboard footprint
nets = {}
BOM = {}                      # ref -> (value, footprint name, LCSC part or None)


def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]


def load(lib, name):
    return pcbnew.FootprintLoad(FPDIR + lib + '.pretty', name)


def pads(fp):
    return list(fp.Pads())


def pad_bbox(fp):
    xs = [p.GetPosition().x for p in pads(fp)]
    ys = [p.GetPosition().y for p in pads(fp)]
    return min(xs), min(ys), max(xs), max(ys)


def add(ref, lib, name, rot, value='', lcsc=None, hide_ref=True):
    fp = load(lib, name)
    fp.SetReference(ref)
    fp.SetValue(value or name)
    board.Add(fp)
    fp.SetOrientationDegrees(rot)
    if hide_ref:
        fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    if lcsc is not None:
        BOM[ref] = (value or name, name, lcsc)
    return fp


def centre_pads_at(fp, cx, cy):
    x0, y0, x1, y1 = pad_bbox(fp)
    p = fp.GetPosition()
    fp.SetPosition(pcbnew.VECTOR2I(p.x + mm(cx) - (x0 + x1) // 2, p.y + mm(cy) - (y0 + y1) // 2))


def edge_place(fp, cx, side, margin=1.1, cy=None):
    """Put a socket's body against a board edge ('top', 'bottom', 'left') so a ribbon can enter."""
    fp.SetPosition(V(cx if cx is not None else 0, cy if cy is not None else 0))
    bb = fp.GetBoundingBox(False)
    p = fp.GetPosition()
    if side == 'top':
        fp.SetPosition(pcbnew.VECTOR2I(p.x, p.y + mm(margin) - bb.GetTop()))
    elif side == 'bottom':
        fp.SetPosition(pcbnew.VECTOR2I(p.x, p.y + mm(H - margin) - bb.GetBottom()))
    elif side == 'left':
        fp.SetPosition(pcbnew.VECTOR2I(p.x + mm(margin) - bb.GetLeft(), p.y))


def setnet(fp, padnum, name):
    for p in pads(fp):
        if p.GetNumber() == str(padnum):
            p.SetNet(net(name))


def pad_pos(fp, num):
    for p in pads(fp):
        if p.GetNumber() == str(num):
            return p.GetPosition()


def text(s, x, y, size=1.0, layer=pcbnew.F_SilkS, bold=False, rot=0, halign='c'):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(V(x, y))
    t.SetLayer(layer)
    t.SetTextSize(V(size, size))
    t.SetTextThickness(mm(max(0.18 if bold else 0.15, size * (0.2 if bold else 0.15))))   # JLC: >= 0.15
    t.SetTextAngleDegrees(rot)
    t.SetHorizJustify({'c': pcbnew.GR_TEXT_H_ALIGN_CENTER, 'l': pcbnew.GR_TEXT_H_ALIGN_LEFT,
                       'r': pcbnew.GR_TEXT_H_ALIGN_RIGHT}[halign])
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)
    return t


def label_header(fp, labels, side_for_row=None, size=0.8, gap=2.15):
    """Print a label beside every pad. labels: {padnum: text}. Horizontal headers only.
    For 2-row headers the row nearer the top gets labels above, the other below.
    For 1-row headers side_for_row = 'above' or 'below'."""
    ys = sorted({p.GetPosition().y for p in pads(fp)})
    for p in pads(fp):
        n = p.GetNumber()
        if n not in labels:
            continue
        x, y = pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
        if len(ys) > 1:
            above = p.GetPosition().y == ys[0]
        else:
            above = side_for_row == 'above'
        text(labels[n], x, y - gap if above else y + gap, size)


# Coordinates follow ECHO-SHOW-5.md "Positions from the photos": component side up, origin at the
# mainboard's top-left corner, the top edge towards the IO and top-board cables, the left edge
# towards the display. The ribbons are short (J44/J30 about 17.5 mm and tight), so each socket's
# ribbon-facing edge sits where the original's does and the board mounts on the original screws.


def entry_face(fp, side, v):
    """Move a socket so its body edge on the ribbon side ('left' or 'top') lies at coordinate v."""
    bb = fp.GetBoundingBox(False)
    p = fp.GetPosition()
    if side == 'left':
        fp.SetPosition(pcbnew.VECTOR2I(p.x + mm(v) - bb.GetLeft(), p.y))
    else:
        fp.SetPosition(pcbnew.VECTOR2I(p.x, p.y + mm(v) - bb.GetTop()))


def centre_on(fp, cx=None, cy=None):
    """Centre a socket's signal pads on x=cx and/or y=cy."""
    sig = [q for q in pads(fp) if q.GetNumber() != 'MP']
    xs = [q.GetPosition().x for q in sig]
    ys = [q.GetPosition().y for q in sig]
    p = fp.GetPosition()
    dx = mm(cx) - (min(xs) + max(xs)) // 2 if cx is not None else 0
    dy = mm(cy) - (min(ys) + max(ys)) // 2 if cy is not None else 0
    fp.SetPosition(pcbnew.VECTOR2I(p.x + dx, p.y + dy))


def echo_numbering(fp, n, pin1_end):
    """Map socket pad -> Echo pin number so Echo pin 1 is at the end the mainboard silkscreen marks
    ('top' = smaller y, 'right' = larger x). Returns {pad: echo_pin}."""
    p1, pn = pad_pos(fp, 1), pad_pos(fp, n)
    same = (p1.y < pn.y) if pin1_end == 'top' else (p1.x > pn.x)
    return {i: (i if same else n + 1 - i) for i in range(1, n + 1)}


def header_pin1_near(hdr, last, target):
    """Rotate a horizontal header 180 degrees if its pad 1 is further from target than its last pad."""
    if abs(pad_pos(hdr, 1).x - target.x) > abs(pad_pos(hdr, last).x - target.x):
        hdr.SetOrientationDegrees(hdr.GetOrientationDegrees() + 180)
        return True
    return False


# ---------------------------------------------------------------- outline
r = pcbnew.PCB_SHAPE(board)
r.SetShape(pcbnew.SHAPE_T_RECT)
r.SetStart(V(0, 0))
r.SetEnd(V(W, H))
r.SetLayer(pcbnew.Edge_Cuts)
r.SetWidth(mm(0.1))
board.Add(r)

# ---------------------------------------------------------------- FPC sockets, at the mainboard positions
FH26 = 'Hirose_FH26-{n}S-0.3SHW_2Rows-{n}Pins-1MP_P0.60mm_Horizontal'
FH26W_LCSC = {33: 'C424654', 31: 'C2973806', 23: 'C3168948'}
# The FH26 footprint takes the ribbon from +x: rot 180 -> from the left, rot 90 -> from the top.
SOCKETS = [
    # ref, pins, rot, ribbon side, face coordinate, centre, Echo pin 1 end, header, header pads, header centre, title
    ('J44', 33, 180, 'left', 4.2, (None, 39.0), 'top', 'P1', 34, (36.0, 28.5), 'J44 DISPLAY 33P'),
    ('J100', 23, 90, 'top', 4.4, (22.4, None), 'right', 'P3', 24, (20.0, 17.5), 'J100 TOP BOARD 23P'),
    ('J46', 31, 90, 'top', 4.5, (42.0, None), 'right', 'P2', 32, (58.0, 17.5), 'J46 IO BOARD 31P'),
]
for ref, n, rot, side, face, (cx, cy), p1end, href, hn, (hx, hy), title in SOCKETS:
    sk = add(ref, 'Connector_FFC-FPC', FH26.format(n=n), rot, value='FH26W-%dS-0.3SHW(60)' % n,
             lcsc=FH26W_LCSC[n])
    centre_on(sk, cx, cy)
    entry_face(sk, side, face)
    emap = echo_numbering(sk, n, p1end)
    for pad, e in emap.items():
        setnet(sk, pad, '%s_%02d' % (ref, e))
    setnet(sk, 'MP', 'GND')
    pin1_pad = [pd for pd, e in emap.items() if e == 1][0]

    h = add(href, 'Connector_PinHeader_2.54mm', 'PinHeader_2x%d_P2.54mm_Vertical' % (hn // 2), 90)
    centre_pads_at(h, hx, hy)
    if header_pin1_near(h, hn - 1, pad_pos(sk, pin1_pad)):
        centre_pads_at(h, hx, hy)
    for e in range(1, n + 1):
        setnet(h, e, '%s_%02d' % (ref, e))
    setnet(h, hn, 'GND')
    lab = {str(i): str(i) for i in range(1, n + 1)}
    lab[str(hn)] = 'G'
    label_header(h, lab)
    text(title + ' (Echo pin numbers)', hx, hy + 5.4, 0.9, bold=True)

# J30 touch: Hirose FH12 takes the ribbon from +y; rot -90 -> from the left. The mainboard silkscreen
# marks pin 1 at the top end (nearest J44) and 8 at the bottom (nearest screw S4), so the header
# carries Echo pin numbers like the others.
j30 = add('J30', 'Connector_FFC-FPC', 'Hirose_FH12-8S-0.5SH_1x08-1MP_P0.50mm_Horizontal', -90,
          value='FH12-8S-0.5SH(55)', lcsc='C597995')
centre_on(j30, None, 53.8)
entry_face(j30, 'left', 14.9)
j30map = echo_numbering(j30, 8, 'top')
for pad, e in j30map.items():
    setnet(j30, pad, 'J30_%d' % e)
setnet(j30, 'MP', 'GND')
j30_pin1 = [pd for pd, e in j30map.items() if e == 1][0]
p4 = add('P4', 'Connector_PinHeader_2.54mm', 'PinHeader_1x08_P2.54mm_Vertical', 90)
centre_pads_at(p4, 34.5, 48.5)
if header_pin1_near(p4, 8, pad_pos(j30, j30_pin1)):
    centre_pads_at(p4, 34.5, 48.5)
for i in range(1, 9):
    setnet(p4, i, 'J30_%d' % i)
label_header(p4, {str(i): str(i) for i in range(1, 9)}, 'below')
text('J30 TOUCH 8P (Echo pin numbers)', 36.0, 52.9, 0.9, bold=True)

# Touch bus (ESP32 side of the patch field): jumper these to P4 once J30 is mapped.
TBUS = ['T_VDD', 'T_VDD', 'GND', 'T_SDA', 'T_SCL', 'T_INT', 'T_RST']   # two VDD: the panel has two touch rails
p5 = add('P5', 'Connector_PinHeader_2.54mm', 'PinHeader_1x07_P2.54mm_Vertical', 90)
centre_pads_at(p5, 33.0, 42.0)
if pcbnew.ToMM(pad_pos(p5, 1).x) > 33.0:
    p5.SetOrientationDegrees(-90)
    centre_pads_at(p5, 33.0, 42.0)
for i, nm in enumerate(TBUS, 1):
    setnet(p5, i, nm)
label_header(p5, {'1': 'VDD', '2': 'VDD', '3': 'G', '4': 'SDA', '5': 'SCL', '6': 'INT', '7': 'RST'}, 'above', 0.8)
text('TOUCH BUS > J30 (jumpers)', 33.0, 37.6, 0.9, bold=True)

# ---------------------------------------------------------------- ESP32-C3 SuperMini socket
# USB-C points left, into the board (the case wall is close to the board's right and bottom edges).
# Component side up, USB on the left: top row 5V, GND, 3V3, IO4..IO0; bottom row IO5..IO21; both
# listed from the USB end. (The underside drawings show the rows the other way round.)
ESP_T = ['+5V', 'GND', '+3V3', 'IO4', 'IO3', 'IO2', 'IO1', 'IO0']
ESP_B = ['IO5', 'IO6', 'IO7', 'IO8', 'IO9', 'IO10', 'IO20', 'IO21']
SHORT = {'+5V': '5V', 'GND': 'G', '+3V3': '3V3'}
for ref, y, names in (('U1A', 42.0, ESP_T), ('U1B', 57.24, ESP_B)):
    sk = add(ref, 'Connector_PinSocket_2.54mm', 'PinSocket_1x08_P2.54mm_Vertical', 90)
    centre_pads_at(sk, 78.5 + 3.5 * 2.54, y)
    if pcbnew.ToMM(pad_pos(sk, 1).x) > 80:
        sk.SetOrientationDegrees(-90)
        centre_pads_at(sk, 78.5 + 3.5 * 2.54, y)
    for i, nm in enumerate(names, 1):
        setnet(sk, i, nm)
        pp = pad_pos(sk, i)
        text(SHORT.get(nm, nm.replace('IO', '')), pcbnew.ToMM(pp.x), pcbnew.ToMM(pp.y) + (2.1 if y < 50 else -2.1), 0.8)
text('ESP32-C3 SUPERMINI', 87.4, 48.9, 0.9, bold=True)
text('< USB', 87.4, 50.6, 0.9, bold=True)

# EXT: the ESP32 nets the touch circuit uses, for wiring any other ESP32 board instead.
EXT = [('+3V3', '3V3'), ('GND', 'G'), ('IO4', 'EN'), ('IO5', 'SDA'), ('IO6', 'SCL'),
       ('IO7', 'INT'), ('IO10', 'RST'), ('+5V', '5V')]
p6 = add('P6', 'Connector_PinHeader_2.54mm', 'PinHeader_1x08_P2.54mm_Vertical', 90)
centre_pads_at(p6, 87.0, 35.0)
if pcbnew.ToMM(pad_pos(p6, 1).x) > 87.0:
    p6.SetOrientationDegrees(-90)
    centre_pads_at(p6, 87.0, 35.0)
for i, (nm, lb) in enumerate(EXT, 1):
    setnet(p6, i, nm)
label_header(p6, {str(i): lb for i, (_, lb) in enumerate(EXT, 1)}, 'above', 0.8)
text('EXT MCU (other ESP32)', 87.0, 30.7, 0.9, bold=True)

# SPARE: every other ESP32 pin (IO0-IO3 are ADC inputs: panel-ID resistor, voltage checks).
SPARE = ['IO0', 'IO1', 'IO2', 'IO3', 'IO8', 'IO9', 'IO20', 'IO21']
p7 = add('P7', 'Connector_PinHeader_2.54mm', 'PinHeader_1x08_P2.54mm_Vertical', 90)
centre_pads_at(p7, 87.0, 26.0)
if pcbnew.ToMM(pad_pos(p7, 1).x) > 87.0:
    p7.SetOrientationDegrees(-90)
    centre_pads_at(p7, 87.0, 26.0)
for i, nm in enumerate(SPARE, 1):
    setnet(p7, i, nm)
label_header(p7, {str(i): nm.replace('IO', '') for i, nm in enumerate(SPARE, 1)}, 'above', 0.8)
text('SPARE IO (0-3 ADC)', 89.0, 21.7, 0.9, bold=True)

# ---------------------------------------------------------------- touch power switch + I2C protection
R0603 = ('Resistor_SMD', 'R_0603_1608Metric')
C0603 = ('Capacitor_SMD', 'C_0603_1608Metric')
q1 = add('Q1', 'Package_TO_SOT_SMD', 'SOT-23', 0, 'AO3401A', 'C15127')
q1.SetPosition(V(50.0, 44.0))
setnet(q1, 1, 'IO4')       # gate: low = touch power on
setnet(q1, 2, '+3V3')      # source
setnet(q1, 3, 'T_VDD')     # drain

SMD = [
    # ref, (lib, fp), value, lcsc, x, y, rot, pin1 net, pin2 net
    ('R1', R0603, '10k', 'C25804', 46.5, 44.0, 90, 'IO4', '+3V3'),
    ('R2', R0603, '1k', 'C21190', 53.5, 42.5, 90, 'T_VDD', 'LED_A'),
    ('C1', C0603, '100nF', 'C14663', 53.5, 46.5, 90, 'T_VDD', 'GND'),
    ('C2', C0603, '100nF', 'C14663', 46.5, 48.0, 90, '+3V3', 'GND'),
    ('R3', R0603, '220R', 'C22962', 60.0, 41.0, 0, 'IO5', 'T_SDA'),
    ('R4', R0603, '220R', 'C22962', 60.0, 43.0, 0, 'IO6', 'T_SCL'),
    ('R5', R0603, '220R', 'C22962', 60.0, 45.0, 0, 'IO7', 'T_INT'),
    ('R6', R0603, '220R', 'C22962', 60.0, 47.0, 0, 'IO10', 'T_RST'),
    ('R7', R0603, '2.2k', 'C4190', 50.0, 50.5, 0, 'T_SDA', 'PU_SDA'),
    ('R8', R0603, '2.2k', 'C4190', 50.0, 53.0, 0, 'T_SCL', 'PU_SCL'),
]
for ref, (lib, name), val, lc, x, y, rot, n1, n2 in SMD:
    f = add(ref, lib, name, rot, val, lc)
    f.SetPosition(V(x, y))
    setnet(f, 1, n1)
    setnet(f, 2, n2)

d1 = add('D1', 'LED_SMD', 'LED_0603_1608Metric', 90, 'Green', 'C72043')
d1.SetPosition(V(56.0, 42.5))
setnet(d1, 1, 'GND')       # cathode
setnet(d1, 2, 'LED_A')

for ref, x, y, n1 in (('JP1', 54.5, 50.5, 'PU_SDA'), ('JP2', 54.5, 53.0, 'PU_SCL')):
    j = add(ref, 'Jumper', 'SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm', 0)
    j.SetPosition(V(x, y))
    setnet(j, 1, n1)
    setnet(j, 2, 'T_VDD')
text('TOUCH PWR', 50.5, 39.6, 0.8, bold=True)
text('IO4 LOW=ON', 50.0, 55.6, 0.8)
text('PULL-UPS: cut JP', 52.5, 57.4, 0.8)

# ---------------------------------------------------------------- speaker (J2 lead), where the original sits
j2 = add('J2', 'Connector_Molex', 'Molex_PicoBlade_53261-0271_1x02-1MP_P1.25mm_Horizontal', 180,
         value='53261-0271', lcsc='C177225')
j2.SetPosition(V(82.4, 3.0))
entry_face(j2, 'top', 0.5)
setnet(j2, 1, 'SPK_1')
setnet(j2, 2, 'SPK_2')
p8 = add('P8', 'Connector_PinHeader_2.54mm', 'PinHeader_1x02_P2.54mm_Vertical', 90)
centre_pads_at(p8, 92.0, 4.0)
setnet(p8, 1, 'SPK_1')
setnet(p8, 2, 'SPK_2')
text('SPEAKER', 92.0, 7.0, 0.8, bold=True)

# ---------------------------------------------------------------- mounting (the original screw positions) and ground
HOLE = 2.6
SCREWS = [(30.4, 6.3), (65.8, 2.5), (2.6, 29.1), (22.7, 58.1), (71.0, 56.8), (104.8, 31.4)]
for i, (x, y) in enumerate(SCREWS, 1):
    # Plain holes: S4 is 1.9 mm from the edge and 2.4 mm from J30 pin 8, so no copper ring.
    # Drilled 2.6 mm (not 2.2) for slack: the positions come from photos (+-0.3-0.8 mm).
    mh = add('H%d' % i, 'MountingHole', 'MountingHole_2.2mm_M2', 0)
    for hp in mh.Pads():
        hp.SetDrillSize(V(HOLE, HOLE))
        hp.SetSize(V(HOLE, HOLE))
    mh.SetPosition(V(x, y))
for i, (x, y) in enumerate([(101.0, 12.0), (40.0, 57.5)], 1):
    tp = add('TP%d' % i, 'TestPoint', 'TestPoint_THTPad_D1.5mm_Drill0.7mm', 0)
    tp.SetPosition(V(x, y))
    setnet(tp, 1, 'GND')
    text('GND', x + (2.6 if x < 100 else -2.6), y, 0.8)

# ---------------------------------------------------------------- notes (back: it faces the screen when fitted)
text('ECHO SHOW 5 BREAKOUT v2', 106.2, 17.5, 0.9, bold=True, rot=90)
NOTES = [
    'ECHO SHOW 5 BREAKOUT v2 (KC19 mainboard size)',
    'Sockets sit where the originals do.',
    'Header pin n = Echo pin n.',
    'Check pin 1 by continuity first.',
    'Unpowered only: never on a powered Echo.',
    'Touch: jumper J30 > TOUCH BUS, IO4 low,',
    'then scan I2C 0x5D and 0x14. Touch off:',
    'hold SDA/SCL/INT/RST low (no back-power).',
    'Insulate header pins here: faces the screen.',
]
for i, line in enumerate(NOTES):
    text(line, 58.0, 31.6 + i * 1.25, 0.8, layer=pcbnew.B_SilkS)



# ---------------------------------------------------------------- rules
import sys
sys.path.insert(0, HERE)
from rules import apply_rules, write_project  # noqa: E402

if __name__ == '__main__':
    apply_rules(board)

out = os.path.join(HERE, 'echo-breakout-unrouted.kicad_pcb')
pcbnew.SaveBoard(out, board)
write_project(out)
pcbnew.ExportSpecctraDSN(board, os.path.join(HERE, 'echo-breakout.dsn'))
_dsn = os.path.join(HERE, 'echo-breakout.dsn')   # drop the local path KiCad writes into the header
with open(_dsn) as f:
    _txt = f.read()
with open(_dsn, 'w') as f:
    f.write(_txt.replace('(pcb "' + _dsn + '"', '(pcb "echo-breakout.dsn"', 1))

# BOM for JLCPCB (assembled parts only; through-hole headers and sockets are fitted by hand)
import csv
groups = {}
for ref, (val, fpn, lc) in BOM.items():
    groups.setdefault((val, fpn, lc), []).append(ref)
with open(os.path.join(HERE, 'jlcpcb', 'bom.csv'), 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
    for (val, fpn, lc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
        w.writerow([val, ','.join(sorted(refs)), fpn, lc])
print('ok', out, len(nets), 'nets')
