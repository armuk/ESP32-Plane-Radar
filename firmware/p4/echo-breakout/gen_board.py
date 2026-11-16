"""Generate the Echo Show 5 (cronos) breakout board (v2: drop-in, mainboard outline).

Run with KiCad's bundled Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen_board.py
Writes echo-breakout-unrouted.kicad_pcb and echo-breakout.dsn (for Freerouting).
"""
import os
import math
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
FPDIR = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/'
W, H = 108.0, 60.0           # board size, mm: the Echo mainboard outline (KC19-TI MLB)
mm = pcbnew.FromMM
V = pcbnew.VECTOR2I_MM

board = pcbnew.BOARD()
board.SetCopperLayerCount(4)    # 2 layers can't fan out the 0.3 mm sockets in the mainboard footprint
board.GetDesignSettings().SetBoardThickness(pcbnew.FromMM(1.0))   # the mainboard is 1.0 mm; the ribbons wrap its edge
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


def body_face(fp, side, v):
    """Move a socket so its body edge (F.Fab outline) on the ribbon side ('left' or 'top') lies at v."""
    xs, ys = [], []
    for g in fp.GraphicalItems():
        if g.GetLayer() == pcbnew.F_Fab:
            r = g.GetBoundingBox()
            xs += [r.GetLeft(), r.GetRight()]
            ys += [r.GetTop(), r.GetBottom()]
    p = fp.GetPosition()
    if side == 'left':
        fp.SetPosition(pcbnew.VECTOR2I(p.x + mm(v) - min(xs), p.y))
    else:
        fp.SetPosition(pcbnew.VECTOR2I(p.x, p.y + mm(v) - min(ys)))


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
# Which way round the FH26W goes. The ribbon enters on the LOCK side: the side with the flip lock, the
# two metal end tabs and the larger leg row (16 of 31, 17 of 33). On the mainboard that side faces the
# board edge on J44, J46 and J100 (microscope photos, 2026-11-16), and the smaller leg row is inboard
# with its legs standing 0.5 mm clear of the body. In KiCad's footprint the lock side is -x:
# rot 0 -> ribbon from the left, rot 270 -> from the top. (Earlier versions had these reversed.)
# 'face' is the socket body's edge from the board edge, measured by caliper on the mainboard.
SOCKETS = [
    # ref, pins, rot, ribbon side, face coordinate, centre, Echo pin 1 end, header, header pads, header centre, title
    ('J44', 33, 0, 'left', 4.2, (None, 39.0), 'top', 'P1', 34, (36.0, 28.5), 'J44 DISPLAY 33P'),
    ('J100', 23, 270, 'top', 4.5, (22.4, None), 'right', 'P3', 24, (22.0, 17.5), 'J100 TOP BOARD 23P'),
    ('J46', 31, 270, 'top', 4.4, (42.0, None), 'right', 'P2', 32, (60.0, 17.5), 'J46 IO BOARD 31P'),
]
for ref, n, rot, side, face, (cx, cy), p1end, href, hn, (hx, hy), title in SOCKETS:
    sk = add(ref, 'Connector_FFC-FPC', FH26.format(n=n), rot, value='FH26W-%dS-0.3SHW(60)' % n,
             lcsc=FH26W_LCSC[n])
    centre_on(sk, cx, cy)
    body_face(sk, side, face)
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

# J30 touch: Hirose FH34SRJ-8S-0.5SH(50). The mainboard's own socket matches it on every count: 6.0 mm
# wide, 3.8 mm deep, back-flip lock on the leg side, 0.3 mm ribbon, and the ribbon's 1.5 mm of bare
# contact with its insertion line at 1.5 mm (Hirose: "uncovered area 1.5 +-0.3"). The FH12 used
# before needs the ribbon 4.4 mm deep, and the Echo's has only a 3 mm tongue.
def make_fh34srj_8s():
    """Footprint from Hirose's recommended PCB mounting pattern (FH34 catalogue, p.8). Leads at -y,
    ribbon enters from +y, contact No.1 at +x. Origin at the body centre (3.8 mm deep)."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID('echo-breakout', 'Hirose_FH34SRJ-8S-0.5SH_1x08-1MP_P0.50mm_Horizontal'))
    fp.SetAttributes(pcbnew.FP_SMD)
    def smd(num, x, y, w, h):
        p = pcbnew.PAD(fp)
        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetShape(pcbnew.PAD_SHAPE_RECT)
        p.SetSize(V(w, h))
        p.SetLayerSet(pcbnew.PAD.SMDMask())
        p.SetNumber(num)
        fp.Add(p)
        p.SetPosition(V(x, y))
    for k in range(1, 9):
        smd(str(k), 1.75 - 0.5 * (k - 1), -1.15, 0.3, 0.8)     # signal lands: 0.3 x 0.8, 0.5 pitch
    for x in (-2.75, 2.75):
        smd('MP', x, 1.35, 0.4, 0.8)                           # metal fittings: E 5.1 / F 5.9
    def box(layer, x0, y0, x1, y1, w):
        g = pcbnew.PCB_SHAPE(fp)
        g.SetShape(pcbnew.SHAPE_T_RECT)
        g.SetStart(V(x0, y0))
        g.SetEnd(V(x1, y1))
        g.SetLayer(layer)
        g.SetWidth(mm(w))
        fp.Add(g)
    box(pcbnew.F_Fab, -3.0, -1.9, 3.0, 1.9, 0.1)               # body: 6.0 x 3.8 with leads
    box(pcbnew.F_CrtYd, -3.25, -2.15, 3.25, 2.15, 0.05)
    # Silkscreen: a bracket round the leg side and both ends, open where the ribbon enters. KiCad has
    # no 3D model for this part, so this is also what shows in the renders.
    for x0, y0, x1, y1 in ((-3.15, -2.05, 3.15, -2.05), (-3.15, -2.05, -3.15, 0.7), (3.15, -2.05, 3.15, 0.7)):
        g = pcbnew.PCB_SHAPE(fp)
        g.SetShape(pcbnew.SHAPE_T_SEGMENT)
        g.SetStart(V(x0, y0))
        g.SetEnd(V(x1, y1))
        g.SetLayer(pcbnew.F_SilkS)
        g.SetWidth(mm(0.15))
        fp.Add(g)
    return fp


j30 = make_fh34srj_8s()
j30.SetReference('J30')
j30.SetValue('FH34SRJ-8S-0.5SH(50)')
board.Add(j30)
j30.SetOrientationDegrees(-90)          # ribbon from +y -> from the left
j30.Reference().SetVisible(False)
j30.Value().SetVisible(False)
BOM['J30'] = ('FH34SRJ-8S-0.5SH(50)', 'Hirose_FH34SRJ-8S-0.5SH_1x08-1MP_P0.50mm_Horizontal', 'C88372')
centre_on(j30, None, 53.8)
# Front face at 15.1 mm from the display edge, where the mainboard's is: its legs measure 18.2-18.7 mm
# in the ruler photos and this puts the lands at 17.75-18.55. The courtyard edge is 0.25 mm further out.
entry_face(j30, 'left', 14.85)
text('1', 20.1, 52.05, 0.8)      # Echo pin numbers at the ends of J30's leg row
text('8', 20.1, 55.55, 0.8)
# The mainboard silkscreen marks pin 1 at the top end (nearest J44) and 8 at the bottom (nearest screw
# S4), so the header carries Echo pin numbers like the others.
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
label_header(p5, {'1': 'V1', '2': 'V2', '3': 'G', '4': 'SDA', '5': 'SCL', '6': 'INT', '7': 'RST'}, 'above', 0.8)
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
# Molex Pico-SPOX 87438-0243: 1.5 mm pitch, right-angle SMT, no metal side tabs. The mainboard's header
# matches it: leads 1.50-1.53 mm apart, housing about 4.5 mm wide and 5.4 mm deep (Molex: 4.3 x 5.75).
# The plug is the 87439-0200 housing. (Earlier versions had a 1.25 mm PicoBlade here, which is wrong.)
def make_picospox_2():
    """Footprint from Molex's recommended PCB layout (874380231-SD sheet 2). Each terminal solders
    in two places with a no-copper gap between: under the body (3.55 x 0.85) and at the tail
    (2.65 x 0.85). The plug enters at +y; origin at the body centre (4.3 x 5.75)."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID('echo-breakout', 'Molex_Pico-SPOX_87438-0243_1x02_P1.50mm_Horizontal'))
    fp.SetAttributes(pcbnew.FP_SMD)
    def smd(num, x, y, w, h):
        p = pcbnew.PAD(fp)
        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetShape(pcbnew.PAD_SHAPE_RECT)
        p.SetSize(V(w, h))
        p.SetLayerSet(pcbnew.PAD.SMDMask())
        p.SetNumber(num)
        fp.Add(p)
        p.SetPosition(V(x, y))
    for num, x in (('1', 0.75), ('2', -0.75)):
        smd(num, x, -4.05, 0.85, 2.65)      # tail land: carries the net
        smd('', x, -0.15, 0.85, 3.55)       # land under the body: same terminal, left without a net
    def box(layer, x0, y0, x1, y1, w):
        g = pcbnew.PCB_SHAPE(fp)
        g.SetShape(pcbnew.SHAPE_T_RECT)
        g.SetStart(V(x0, y0))
        g.SetEnd(V(x1, y1))
        g.SetLayer(layer)
        g.SetWidth(mm(w))
        fp.Add(g)
    box(pcbnew.F_Fab, -2.15, -2.875, 2.15, 2.875, 0.1)
    box(pcbnew.F_CrtYd, -2.4, -5.6, 2.4, 3.125, 0.05)
    for x0, y0, x1, y1 in ((-2.3, -3.0, -2.3, 3.0), (2.3, -3.0, 2.3, 3.0), (-2.3, 3.0, 2.3, 3.0)):
        g = pcbnew.PCB_SHAPE(fp)
        g.SetShape(pcbnew.SHAPE_T_SEGMENT)
        g.SetStart(V(x0, y0))
        g.SetEnd(V(x1, y1))
        g.SetLayer(pcbnew.F_SilkS)
        g.SetWidth(mm(0.15))
        fp.Add(g)
    return fp


j2 = make_picospox_2()
j2.SetReference('J2')
j2.SetValue('87438-0243')
BOM['J2'] = ('87438-0243', 'Molex_Pico-SPOX_87438-0243_1x02_P1.50mm_Horizontal', 'C240853')
board.Add(j2)
j2.SetOrientationDegrees(180)             # plug enters from the top board edge
j2.Reference().SetVisible(False)
j2.Value().SetVisible(False)
# On the mainboard the two leads are centred 82.0 mm from the display edge and their tips are 8.4 mm
# from the cable edge (ruler photos), which puts the mating face 0.6 mm in and the body centre at 3.475.
j2.SetPosition(V(82.0, 3.475))
setnet(j2, 1, 'SPK_2')     # which lead is which doesn't matter for a speaker; this order keeps
setnet(j2, 2, 'SPK_1')     # the two tracks to P8 from crossing
p8 = add('P8', 'Connector_PinHeader_2.54mm', 'PinHeader_1x02_P2.54mm_Vertical', 90)
centre_pads_at(p8, 103.0, 4.0)
setnet(p8, 1, 'SPK_1')
setnet(p8, 2, 'SPK_2')
text('SPEAKER', 97.0, 4.0, 0.8, bold=True)

# ---------------------------------------------------------------- mounting (the original screw positions) and ground
HOLE = 2.6      # finished hole; an M2 screw has 0.3 mm of slack in it
RING = 4.8      # grounded pad around each screw, as on the mainboard (5 mm there)
RING_AT = {4: 3.6}   # S4: the board edge is 2.1 mm away
KEEPOUT = 2.6   # radius of the track/via keepout around each screw
KEEPOUT_AT = {}
# Hole centres measured from two perspective-corrected ruler photos of the mainboard (2026-11-16);
# the two agree within 0.2 mm on every hole. See "Positions from the photos" in ECHO-SHOW-5.md.
SCREWS = [(30.35, 6.45), (65.8, 2.85), (2.8, 29.9), (23.25, 57.85), (70.85, 57.05), (105.1, 31.35)]
for i, (x, y) in enumerate(SCREWS, 1):
    # Plated hole in an exposed copper pad on both faces, like the mainboard's screw pads. The pad
    # has no net here, so the autorouter treats it as an obstacle; finish_board.py ties it to GND
    # and the ground pours on all four layers connect it.
    mh = add('H%d' % i, 'MountingHole', 'MountingHole_2.2mm_M2', 0)
    ring = RING_AT.get(i, RING)
    for hp in mh.Pads():
        hp.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        hp.SetLayerSet(pcbnew.PAD.PTHMask())
        hp.SetNumber('1')
        hp.SetDrillSize(V(HOLE, HOLE))
        hp.SetSize(V(ring, ring))
    mh.SetPosition(V(x, y))
    # Keep tracks and vias out from under the screw head on both outer layers: an M2 pan head is
    # 3.8 mm across and the 2.6 mm hole lets it sit 0.3 mm off-centre, so it reaches 2.2 mm out.
    ko = pcbnew.ZONE(board)
    ko.SetIsRuleArea(True)
    ko.SetDoNotAllowTracks(True)
    ko.SetDoNotAllowVias(True)
    ko.SetDoNotAllowPads(False)
    ko.SetDoNotAllowZoneFills(False)
    ko.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    ko.SetLayerSet(ls)
    ko.SetZoneName('SCREW_H%d' % i)
    o = ko.Outline()
    o.NewOutline()
    for k in range(32):
        a = 2 * math.pi * k / 32
        rk = KEEPOUT_AT.get(i, KEEPOUT)
        o.Append(mm(x + rk * math.cos(a)), mm(y + rk * math.sin(a)))
    board.Add(ko)
# Alignment holes: the case has locating pegs here (by J45, and by the part-number print). Plain
# holes, 2.5 mm as on the mainboard (the pegs are 1.8-1.9 mm), with tracks kept 2 mm clear on the outer
# layers.
ALIGN_HOLE = 2.5
ALIGN = [(3.1, 16.8), (93.4, 17.05)]
for i, (x, y) in enumerate(ALIGN, 1):
    ah = add('A%d' % i, 'MountingHole', 'MountingHole_2.2mm_M2', 0)
    for hp in ah.Pads():
        hp.SetDrillSize(V(ALIGN_HOLE, ALIGN_HOLE))
        hp.SetSize(V(ALIGN_HOLE, ALIGN_HOLE))
    ah.SetPosition(V(x, y))
    ko = pcbnew.ZONE(board)
    ko.SetIsRuleArea(True)
    ko.SetDoNotAllowTracks(True)
    ko.SetDoNotAllowVias(True)
    ko.SetDoNotAllowPads(False)
    ko.SetDoNotAllowZoneFills(False)
    ko.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.F_Cu)
    ls.AddLayer(pcbnew.B_Cu)
    ko.SetLayerSet(ls)
    ko.SetZoneName('ALIGN_A%d' % i)
    o = ko.Outline()
    o.NewOutline()
    for k in range(32):
        a = 2 * math.pi * k / 32
        o.Append(mm(x + 2.0 * math.cos(a)), mm(y + 2.0 * math.sin(a)))
    board.Add(ko)
# Touch ground pad: the mainboard has a bare gold area here, left of J30, that the ground pad on the
# back of the touch ribbon's GT911 rests on (measured from the ruler photos: x 2.3-11.6, y 46.0-58.7,
# plus the via-stitched strips out to the board edges). One exposed pad with four plated holes that
# tie it to the ground pours on every layer. No net here (an obstacle for the autorouter);
# finish_board.py ties it to GND.
GPAD = (1.0, 46.0, 11.5, 59.0)   # x0, y0, x1, y1
gp = pcbnew.FOOTPRINT(board)
gp.SetReference('GP1')
gp.SetValue('Touch ground pad')
gp.SetFPID(pcbnew.LIB_ID('echo-breakout', 'TouchGroundPad'))   # the Specctra files need a footprint name
gp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_BOARD_ONLY)
board.Add(gp)
_gx, _gy = (GPAD[0] + GPAD[2]) / 2, (GPAD[1] + GPAD[3]) / 2
gp.SetPosition(V(_gx, _gy))
gp.Reference().SetVisible(False)
gp.Value().SetVisible(False)
_p = pcbnew.PAD(gp)
_p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
_p.SetShape(pcbnew.PAD_SHAPE_RECT)
_p.SetSize(V(GPAD[2] - GPAD[0], GPAD[3] - GPAD[1]))
_ls = pcbnew.LSET()
_ls.AddLayer(pcbnew.F_Cu)
_ls.AddLayer(pcbnew.F_Mask)          # exposed, no paste
_p.SetLayerSet(_ls)
_p.SetNumber('1')
gp.Add(_p)
_p.SetPosition(V(_gx, _gy))
for _dx in (-3.0, 3.0):
    for _dy in (-4.0, 4.0):
        _v = pcbnew.PAD(gp)
        _v.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        _v.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
        _v.SetSize(V(0.7, 0.7))
        _v.SetDrillSize(V(0.35, 0.35))
        _v.SetLayerSet(pcbnew.PAD.PTHMask())
        _v.SetNumber('1')
        gp.Add(_v)
        _v.SetPosition(V(_gx + _dx, _gy + _dy))



def ground_pads(ref, rects, layer, mask):
    """Bare ground pads: one footprint holding a rectangle of exposed copper per (x0, y0, x1, y1).
    No net here (obstacles for the autorouter); finish_board.py ties every G* footprint to GND."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference(ref)
    fp.SetValue('Ground pad')
    fp.SetFPID(pcbnew.LIB_ID('echo-breakout', 'GroundPad'))
    fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_BOARD_ONLY)
    board.Add(fp)
    x0, y0, x1, y1 = rects[0]
    fp.SetPosition(V((x0 + x1) / 2, (y0 + y1) / 2))
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    for x0, y0, x1, y1 in rects:
        p = pcbnew.PAD(fp)
        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetShape(pcbnew.PAD_SHAPE_RECT)
        p.SetSize(V(x1 - x0, y1 - y0))
        ls = pcbnew.LSET()
        ls.AddLayer(layer)
        ls.AddLayer(mask)
        p.SetLayerSet(ls)
        p.SetNumber('1')
        fp.Add(p)
        p.SetPosition(V((x0 + x1) / 2, (y0 + y1) / 2))
    return fp


# Tape pad: the button/mic board's ground reaches the mainboard through a strip of metal-mesh tape stuck
# to the bare area above J45, which runs down the display edge as a sliver to a second bare area by
# the alignment hole. Same shape here (measured from the ruler photos).
ground_pads('GP2', [(0.6, 0.6, 9.0, 4.8), (0.6, 4.8, 2.0, 12.4), (0.6, 12.4, 5.6, 14.0)],
            pcbnew.F_Cu, pcbnew.F_Mask)

# Back of the board: the display frame carries conductive fabric gaskets that press on bare ground
# pads on the back of the mainboard, and the GT911's ground pad rests on the one under the J30 corner.
# Nine pads, measured from a perspective-corrected ruler photo of the back (2026-11-16), given here
# in the board's own (top view) coordinates. No through-hole part may sit inside one: the gasket
# would short its pins. B6 is cut short at x = 6.0 to stay clear of P3; nothing presses on it.
BACK_PADS = [
    (1.2, 51.8, 10.0, 58.9),      # B1: under the J30 corner (GT911 ground pad; no gasket)
    (27.8, 51.7, 37.9, 59.4),     # B2: bottom edge
    (99.8, 50.1, 107.1, 59.4),    # B3: bottom-right corner
    (99.8, 34.9, 107.1, 44.7),    # B4: right edge
    (66.7, 24.5, 74.8, 34.5),     # B5: centre
    (1.2, 19.0, 6.0, 26.6),       # B6: display edge (no gasket)
    (6.5, 1.8, 16.5, 9.7),        # B7: top-left
    (74.0, 1.6, 84.2, 9.7),       # B8: top edge
    (90.4, 2.0, 98.6, 12.0),      # B9: top edge, right
]
for i, r in enumerate(BACK_PADS, 1):
    ground_pads('GB%d' % i, [r], pcbnew.B_Cu, pcbnew.B_Mask)

for i, (x, y) in enumerate([(101.0, 12.0), (40.0, 57.5)], 1):
    tp = add('TP%d' % i, 'TestPoint', 'TestPoint_THTPad_D1.5mm_Drill0.7mm', 0)
    tp.SetPosition(V(x, y))
    setnet(tp, 1, 'GND')
    text('GND', x + (2.6 if x < 100 else -2.6), y, 0.8)

# ---------------------------------------------------------------- notes (back: it faces the screen when fitted)
text('ECHO SHOW 5 BREAKOUT v2', 106.2, 17.5, 0.9, bold=True, rot=90)
NOTES = [
    'ECHO SHOW 5 BREAKOUT v2 (KC19 mainboard size)',
    'Header pin n = Echo pin n.',
    'Check pin 1 by continuity first.',
    'Unpowered only: never on a powered Echo.',
    'Tape over header pins, not the bare pads.',
    'Touch: jumper J30 > TOUCH BUS, IO4 low,',
    'then scan I2C 0x5D and 0x14. Touch off:',
    'hold SDA/SCL/INT/RST low (no back-power).',
]
for i, line in enumerate(NOTES):
    text(line, 44.0, 31.5 + i * 1.25, 0.8, layer=pcbnew.B_SilkS)   # clear of back pad B5



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

# BOM for JLCPCB. The through-hole headers and sockets are fitted by JLCPCB too (Kinghelm 2.54 mm
# straight headers, LCSC numbers looked up 2026-11-16). The two ground test loops stay as bare holes.
THT_PARTS = {
    'PinHeader_2x17_P2.54mm_Vertical': ('KH-2.54PH180-2X17P-L8.9', 'C5157640'),
    'PinHeader_2x16_P2.54mm_Vertical': ('KH-2.54PH180-2X16P-L8.9', 'C5157639'),
    'PinHeader_2x12_P2.54mm_Vertical': ('KH-2.54PH180-2X12P-L8.9', 'C5157635'),
    'PinHeader_1x08_P2.54mm_Vertical': ('KH-2.54PH180-1X8P-L11.5', 'C2905487'),
    'PinHeader_1x07_P2.54mm_Vertical': ('KH-2.54PH180-1X7P-L11.5', 'C2932700'),
    'PinHeader_1x02_P2.54mm_Vertical': ('KH-2.54PH180-1X2P-L11.5', 'C2905434'),
    'PinSocket_1x08_P2.54mm_Vertical': ('KH-2.54FH-1X8P-H8.5', 'C2905417'),
}
for _fp in board.GetFootprints():
    _name = str(_fp.GetFPID().GetLibItemName())
    if _name in THT_PARTS:
        BOM[_fp.GetReference()] = (THT_PARTS[_name][0], _name, THT_PARTS[_name][1])
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
