"""Generate flex extension cables for the Echo Show 5's 0.3 mm ribbons (J44, J46, J100).

Each extender is a 2-layer flex PCB:
  - end A: a Hirose FH26W socket on the top face, on an FR4 stiffener. The Echo's own ribbon plugs in
    here exactly as it plugged into the mainboard.
  - end B: contacts to Hirose's recommended FPC dimensions (FH26 catalogue), on the bottom face, with a
    PI stiffener to bring the end to 0.2 mm. It plugs into the breakout (or the P4 board) in place of
    the Echo ribbon.
The contacts must be on the opposite face from the socket, or the pin order comes out mirrored, so every
line changes layer once (all vias sit on the stiffened end). Echo pin 1 is on the -y edge at both ends.

Run with KiCad's bundled Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3 gen_extenders.py
Writes <name>/<name>.kicad_pcb, .kicad_pro and jlcpcb/bom.csv for each extender.
"""
import json
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
FPDIR = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/'
LENGTH = 100.0          # mm, front edge of end A to the contact tip of end B
mm = pcbnew.FromMM
X0, Y0 = 20.0, 40.0     # page offset; everything below is relative to (X0, Y0)

# name, pins, LCSC part of the FH26W socket, what the ribbon goes to
EXTENDERS = [
    ('j44-ext', 'J44', 33, 'C424654', 'DISPLAY'),
    ('j46-ext', 'J46', 31, 'C2973806', 'IO BOARD'),
    ('j100-ext', 'J100', 23, 'C3012331', 'TOP BOARD'),
]

TRACE = 0.1             # signal width; 0.3 mm pitch leaves 0.2 mm gaps
# End B contacts, to Hirose's recommended FH26 FPC pattern (catalogue p.8): two staggered rows of pads,
# 0.6 mm pitch in each row. Odd pins (1, 3 ... n) are the row at the tip; even pins sit behind them,
# with the odd pins' 0.1 mm leads running between. Distances are from the tip.
PAD_W = 0.28            # Hirose: 0.3 +0.04/-0.03; 0.28 leaves 0.11 mm to the lead beside it
PAD_ODD = (0.3, 1.0)    # tip row: Hirose 0.3 +-0.1 to 1.0 +-0.1
PAD_EVEN = (1.15, 2.1)  # rear row: Hirose 1.1 +-0.1 to 2.1 +-0.1
EDGE_TRIM = 0.04        # taken off the outer side of the two edge pads
COVER_END = 2.7         # coverlay stops here (Hirose 2.5 +-0.3)
VIA_D, VIA_HOLE = 0.55, 0.3   # JLCPCB flex "regular" via

# x positions along the strip (mm from the front edge of end A)
XS = 4.2                # socket centre (between its two pad rows)
XVA, XVB = XS - 2.5, XS - 3.4   # two staggered via rows for the front pad row
XF0 = XS + 2.3          # fan-out starts (behind the rear pad row)
XF1 = XF0 + 5.0         # fan-out ends: pitch doubled (0.3 -> 0.6 per line)
XV = XF1 + 0.5          # one via row for the rear pad row (1.2 mm apart)
XB = XV + 0.8           # all lines now on B.Cu at 0.6 mm pitch
XC = XB + 6.0           # converged back to 0.3 mm pitch
XR = XV + 1.2           # end of the rigid (FR4-stiffened) end A


def V(x, y):
    return pcbnew.VECTOR2I_MM(X0 + x, Y0 + y)


def build(name, ref, n, lcsc, what):
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(mm(0.11))
    nc = ds.m_NetSettings.GetDefaultNetclass()
    nc.SetClearance(mm(0.1))
    nc.SetTrackWidth(mm(TRACE))
    nc.SetViaDiameter(mm(VIA_D))
    nc.SetViaDrill(mm(VIA_HOLE))
    ds.m_TrackMinWidth = mm(0.08)
    ds.m_MinClearance = mm(0.08)
    ds.m_CopperEdgeClearance = mm(0.2)

    nets = {}
    for k in range(1, n + 1):
        ni = pcbnew.NETINFO_ITEM(board, 'E%02d' % k)   # Echo pin k
        board.Add(ni)
        nets[k] = ni

    F = 0.3 * n + 0.3          # FPC width at the contacts (Hirose "F")
    ym = 0.15 * (n - 1)        # outermost line, |y|
    HA = 2 * ym + 0.9          # half-width of the rigid end A
    WB2 = ym + 0.5             # half-width of the body
    L = LENGTH
    ys = [0.3 * (k - (n + 1) / 2) for k in range(1, n + 1)]   # Echo pin k at ys[k-1] (pin 1 at -y)

    def lead_end(k):
        """x where pin k's line stops: 0.1 mm inside the back of its contact pad."""
        return L - (PAD_ODD if k % 2 else PAD_EVEN)[1] + 0.1

    def seg(a, b, k, layer, w=TRACE):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(V(*a))
        t.SetEnd(V(*b))
        t.SetWidth(mm(w))
        t.SetLayer(layer)
        t.SetNet(nets[k])
        board.Add(t)

    def path(pts, k, layer):
        for a, b in zip(pts, pts[1:]):
            if a != b:
                seg(a, b, k, layer)

    def via(x, y, k):
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(V(x, y))
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        v.SetWidth(mm(VIA_D))
        v.SetDrill(mm(VIA_HOLE))
        v.SetNet(nets[k])
        board.Add(v)

    def shape(layer, pts, filled=False, width=0.1):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_POLY)
        s.SetPolyPoints([V(*p) for p in pts])
        s.SetLayer(layer)
        s.SetWidth(mm(width))
        s.SetFilled(filled)
        board.Add(s)

    def rect(layer, x0, y0, x1, y1, filled=True):
        shape(layer, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], filled, 0.05 if filled else 0.1)

    def text(layer, s, x, y, size=1.0, rot=0, mirror=False):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s)
        t.SetPosition(V(x, y))
        t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I_MM(size, size))
        t.SetTextThickness(mm(max(0.15, size * 0.15)))   # JLC: >= 0.15
        t.SetTextAngleDegrees(rot)
        t.SetMirrored(mirror)
        board.Add(t)

    # ------------------------------------------------------------ outline
    outline = [(0, -HA), (XR, -HA), (XC + 0.4, -WB2), (L - 4.5, -WB2), (L - 4.5, -F / 2), (L, -F / 2),
               (L, F / 2), (L - 4.5, F / 2), (L - 4.5, WB2), (XC + 0.4, WB2), (XR, HA), (0, HA), (0, -HA)]
    for a, b in zip(outline, outline[1:]):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(V(*a))
        s.SetEnd(V(*b))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)

    # ------------------------------------------------------------ end A: the socket
    fp = pcbnew.FootprintLoad(FPDIR + 'Connector_FFC-FPC.pretty',
                              'Hirose_FH26-{n}S-0.3SHW_2Rows-{n}Pins-1MP_P0.60mm_Horizontal'.format(n=n))
    fp.SetReference('J1')
    # J100: Hirose's 23-pin part was out of stock on 2026-11-16; HCTL's copy has the same lands.
    part = 'HC-FPC-03-10-23RLTAG' if n == 23 else 'FH26W-%dS-0.3SHW(60)' % n
    fp.SetValue(part)
    board.Add(fp)
    # The FH26W takes the ribbon on its LOCK side: the flip lock, the two metal end tabs and the
    # larger pad row. In KiCad's footprint that side is -x, so no rotation: the ribbon comes in over
    # the front edge of end A. (Earlier versions had the socket the other way round.)
    fp.SetOrientationDegrees(0)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    sig = [p for p in fp.Pads() if p.GetNumber() != 'MP']
    xs_ = [p.GetPosition().x for p in sig]
    ys_ = [p.GetPosition().y for p in sig]
    pos = fp.GetPosition()
    fp.SetPosition(pcbnew.VECTOR2I(pos.x + mm(X0 + XS) - (min(xs_) + max(xs_)) // 2,
                                   pos.y + mm(Y0) - (min(ys_) + max(ys_)) // 2))
    xc = mm(X0 + XS)
    sig.sort(key=lambda p: p.GetPosition().y)
    front, rear = [], []
    for k, p in enumerate(sig, 1):
        px = pcbnew.ToMM(p.GetPosition().x) - X0
        py = pcbnew.ToMM(p.GetPosition().y) - Y0
        assert abs(py - ys[k - 1]) < 0.01, (name, k, py, ys[k - 1])
        p.SetNet(nets[k])
        (front if p.GetPosition().x < xc else rear).append((k, px))

    # front row: F.Cu stub forward to a via (two staggered rows), then B.Cu back under the socket
    for i, (k, px) in enumerate(front):
        y = ys[k - 1]
        vx = XVA if i % 2 == 0 else XVB
        path([(px, y), (vx, y)], k, pcbnew.F_Cu)
        via(vx, y, k)
        path([(vx, y), (XF0, y), (XF1, 2 * y), (XB, 2 * y), (XC, y), (lead_end(k), y)], k, pcbnew.B_Cu)
    # rear row: F.Cu out the back, fan out to double pitch, via, then B.Cu
    for k, px in rear:
        y = ys[k - 1]
        path([(px, y), (XF0, y), (XF1, 2 * y), (XV, 2 * y)], k, pcbnew.F_Cu)
        via(XV, 2 * y, k)
        path([(XV, 2 * y), (XB, 2 * y), (XC, y), (lead_end(k), y)], k, pcbnew.B_Cu)

    # coverlay openings over each pad row (one window per row: coverlay can't make 0.1 mm webs)
    for row in (front, rear):
        x = sum(px for _, px in row) / len(row)
        yy = [ys[k - 1] for k, _ in row]
        half = 0.5 if row is front else 0.45       # front lands are 0.8 mm long, rear 0.65 mm
        rect(pcbnew.F_Mask, x - half, min(yy) - 0.3, x + half, max(yy) + 0.3)

    # ------------------------------------------------------------ end B: contacts (bottom face)
    fing = pcbnew.FOOTPRINT(board)
    fing.SetReference('P1')
    fing.SetValue('FH26 FPC end')
    fing.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    board.Add(fing)
    FX = L - 1.4            # footprint origin; the flip below mirrors x about it
    fing.SetPosition(V(FX, 0))
    fing.Reference().SetVisible(False)
    fing.Value().SetVisible(False)
    for k in range(1, n + 1):
        d0, d1 = PAD_ODD if k % 2 else PAD_EVEN
        p = pcbnew.PAD(fing)
        p.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        p.SetShape(pcbnew.PAD_SHAPE_RECT)
        # The two edge pads lose 0.04 mm on their outer side to keep 0.2 mm of copper-to-edge
        # clearance (Hirose draws 0.15 mm there; JLCPCB's flex outline needs 0.2).
        trim = EDGE_TRIM if k in (1, n) else 0.0
        p.SetSize(pcbnew.VECTOR2I_MM(d1 - d0, PAD_W - trim))
        ls = pcbnew.LSET()
        ls.AddLayer(pcbnew.F_Cu)
        ls.AddLayer(pcbnew.F_Mask)
        p.SetLayerSet(ls)
        p.SetNumber(str(k))
        fing.Add(p)
        x = L - (d0 + d1) / 2
        yk = ys[k - 1] + (trim / 2 if k == 1 else -trim / 2)
        p.SetPosition(V(2 * FX - x, yk))                  # pre-mirrored: the flip puts it at x
    fing.Flip(fing.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)   # x-mirror keeps the y order
    for p in fing.Pads():
        k = 1 + round((pcbnew.ToMM(p.GetPosition().y) - Y0 - ys[0]) / 0.3)
        d0, d1 = PAD_ODD if k % 2 else PAD_EVEN
        assert abs(pcbnew.ToMM(p.GetPosition().x) - X0 - (L - (d0 + d1) / 2)) < 0.001, (name, k)
        p.SetNumber(str(k))
        p.SetNet(nets[k])
    rect(pcbnew.B_Mask, L - COVER_END, -F / 2 - 0.5, L + 0.5, F / 2 + 0.5)

    # ------------------------------------------------------------ stiffeners (drawn for the order)
    rect(pcbnew.User_1, 0, -HA, XR, HA)                   # FR4 0.8 mm, bottom face, under the socket
    rect(pcbnew.User_2, L - 4.0, -F / 2, L, F / 2)        # PI 0.1 mm, top face, behind the contacts
    text(pcbnew.User_1, 'User.1: FR4 stiffener 0.8 mm, BOTTOM face (under J1)', XR / 2 + 8, -HA - 3, 1.0)
    text(pcbnew.User_2, 'User.2: PI stiffener 0.1 mm, TOP face, total 0.2 mm at contacts',
         L - 20, F / 2 + 3, 1.0)

    # ------------------------------------------------------------ silkscreen (top face)
    S = pcbnew.F_SilkS
    mid = (XC + L - 4.5) / 2
    text(S, '%s %s EXTENDER' % (ref, what), mid, -0.8, 1.0)
    text(S, 'ECHO RIBBON INTO J1   TIP INTO BREAKOUT %s' % ref, mid, 0.9, 0.8)
    for x in (XC + 6, L - 12):
        text(S, 'PIN 1', x, -ym + 0.15, 0.8)
    text(S, 'THIS SIDE UP', L - 7.5, 0, 0.8)
    text(S, '1', 0.9, -ym - 0.9, 0.8)

    # ------------------------------------------------------------ save
    out = os.path.join(HERE, name)
    os.makedirs(os.path.join(out, 'jlcpcb'), exist_ok=True)
    pcb = os.path.join(out, name + '.kicad_pcb')
    pcbnew.SaveBoard(pcb, board)
    write_project(pcb)
    with open(os.path.join(out, 'jlcpcb', 'bom.csv'), 'w') as f:
        f.write('Comment,Designator,Footprint,LCSC Part #\n')
        f.write('"%s",J1,"%s",%s\n' % (part, fp.GetFPID().GetLibItemName(), lcsc))
    print('saved', pcb, 'width at socket %.1f, body %.1f, contacts %.1f mm' % (2 * HA, 2 * WB2, F))


def write_project(pcb_path):
    """Net class and limits for KiCad's DRC (JLCPCB flex, 12 um copper: 3/3 mil, regular vias)."""
    pro = os.path.splitext(pcb_path)[0] + '.kicad_pro'
    data = {
        'board': {'design_settings': {
            'rules': {
                'min_clearance': 0.08, 'min_track_width': 0.08, 'min_via_diameter': 0.5,
                'min_via_annular_width': 0.1, 'min_through_hole_diameter': 0.3, 'min_hole_to_hole': 0.25,
                'min_copper_edge_clearance': 0.2, 'min_silk_clearance': 0.0, 'min_text_height': 0.8,
                'min_text_thickness': 0.08, 'solder_mask_to_copper_clearance': 0.0,
                'min_hole_clearance': 0.2, 'use_height_for_length_calcs': True},
            'rule_severities': {'silk_over_copper': 'ignore', 'silk_overlap': 'ignore',
                                'silk_edge_clearance': 'ignore', 'lib_footprint_issues': 'ignore',
                                'lib_footprint_mismatch': 'ignore',
                                'solder_mask_bridge': 'ignore'}}},
        'net_settings': {
            'classes': [{
                'name': 'Default', 'clearance': 0.1, 'track_width': TRACE, 'via_diameter': VIA_D,
                'via_drill': VIA_HOLE, 'microvia_diameter': 0.3, 'microvia_drill': 0.1,
                'diff_pair_width': 0.1, 'diff_pair_gap': 0.2, 'diff_pair_via_gap': 0.25, 'wire_width': 6,
                'bus_width': 12, 'line_style': 0, 'pcb_color': 'rgba(0, 0, 0, 0.000)',
                'schematic_color': 'rgba(0, 0, 0, 0.000)', 'priority': 2147483647}],
            'meta': {'version': 4}},
        'meta': {'filename': os.path.basename(pro), 'version': 3},
    }
    with open(pro, 'w') as f:
        json.dump(data, f, indent=2)


for e in EXTENDERS:
    build(*e)
