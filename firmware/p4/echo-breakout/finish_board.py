"""Import the Freerouting session, add ground pours on both layers, fill, save.

  .../python3 finish_board.py   ->  echo-breakout.kicad_pcb
"""
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
mm = pcbnew.FromMM
board = pcbnew.LoadBoard(os.path.join(HERE, 'echo-breakout-unrouted.kicad_pcb'))
import importlib.util
_spec = importlib.util.spec_from_file_location('rules', os.path.join(HERE, 'rules.py'))
rules = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rules)
# The autorouter works to 0.2 mm around the power nets but rounds to the micron, and now and then
# lands a track 0.1994 mm from a pad. Check the finished board at 0.19 mm (the fab limit is 0.127).
POWER_CLEARANCE = 0.19
rules.apply_rules(board, POWER_CLEARANCE)
# The session file also carries each footprint's position, and the import applies it. Keep the
# generator's placement: a part moved in gen_board.py since the last autoroute must stay moved.
placed = {fp.GetReference(): (fp.GetPosition(), fp.GetOrientationDegrees()) for fp in board.GetFootprints()}
if not pcbnew.ImportSpecctraSES(board, os.path.join(HERE, 'echo-breakout.ses')):
    raise SystemExit('SES import failed')
for fp in board.GetFootprints():
    pos, rot = placed[fp.GetReference()]
    moved = max(abs(fp.GetPosition().x - pos.x), abs(fp.GetPosition().y - pos.y))
    if moved or abs(fp.GetOrientationDegrees() - rot) > 0.01:
        if moved > 1000:     # more than a micron: a real move, not rounding
            print('placement restored from the generator:', fp.GetReference())
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(pos)


def remove_dangling(board):
    """Delete router stubs: track segments with an end that touches nothing on its net."""
    removed = 0
    seen = set()
    dups = []
    for t in [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]:
        a, b = (t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)
        key = (t.GetLayer(), t.GetNetCode(), min(a, b), max(a, b))
        if key in seen:          # same segment drawn twice (once each way)
            dups.append(t)
        seen.add(key)
    for t in dups:
        board.Delete(t)
    removed += len(dups)
    while True:
        tracks = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
        vias = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]
        pads = list(board.GetPads())
        dead = []
        for t in tracks:
            for end in (t.GetStart(), t.GetEnd()):
                hit = any(o is not t and o.GetNetCode() == t.GetNetCode() and o.GetLayer() == t.GetLayer()
                          and o.HitTest(end) for o in tracks)          # end or T-junction
                hit = hit or any(v.GetNetCode() == t.GetNetCode() and v.HitTest(end) for v in vias)
                hit = hit or any(p.GetNetCode() == t.GetNetCode() and p.IsOnLayer(t.GetLayer())
                                 and p.HitTest(end) for p in pads)
                if not hit:
                    dead.append(t)
                    break
        if not dead:
            return removed
        for t in dead:
            board.Delete(t)
        removed += len(dead)


def remove_dangling_kicad(board):
    """Second pass with KiCad's own connectivity, for stubs the geometric pass misses."""
    removed = 0
    while True:
        board.BuildConnectivity()
        conn = board.GetConnectivity()
        dead = [t for t in board.GetTracks()
                if t.Type() == pcbnew.PCB_TRACE_T and conn.TestTrackEndpointDangling(t, False)]
        if not dead:
            return removed
        for t in dead:
            board.Delete(t)
        removed += len(dead)


def route_speaker(board):
    """Lay the two speaker tracks by hand, replacing whatever the autorouter drew for them: J2's tail
    lands to P8, in a corridor nothing else uses. Fixed, so J2 can change without a full re-route."""
    for t in list(board.GetTracks()):
        if t.GetNetname() in ('SPK_1', 'SPK_2'):
            board.Delete(t)
    j2 = board.FindFootprintByReference('J2')
    p8 = board.FindFootprintByReference('P8')
    for net, lane in (('SPK_1', 9.6), ('SPK_2', 10.5)):
        a = [p for p in j2.Pads() if p.GetNetname() == net][0].GetPosition()
        b = [p for p in p8.Pads() if p.GetNetname() == net][0].GetPosition()
        ax, ay, bx, by = (pcbnew.ToMM(v) for v in (a.x, a.y, b.x, b.y))
        d = 0.55
        pts = [(ax, ay), (ax, lane - d), (ax + d, lane), (bx - d, lane), (bx, lane - d), (bx, by)]
        for p, q in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(pcbnew.VECTOR2I_MM(*p))
            t.SetEnd(pcbnew.VECTOR2I_MM(*q))
            t.SetWidth(mm(0.4))
            t.SetLayer(pcbnew.F_Cu)
            t.SetNet(board.FindNet(net))
            board.Add(t)


route_speaker(board)
if not os.environ.get('NO_CLEAN'):
    print('removed dangling segments:', remove_dangling(board))
    print('removed dangling segments (KiCad check):', remove_dangling_kicad(board))

gnd = board.FindNet('GND')
# Screw pads and the bare ground pads (GP*, GB*): tie them to ground here (they are net-less obstacles for the autorouter). The pours
# connect them solidly on every layer.
for fp in board.GetFootprints():
    if fp.GetReference().startswith('H') or fp.GetReference().startswith('G'):
        for p in fp.Pads():
            p.SetNet(gnd)
            p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
            # The autorouter saw these pads without a net, so it kept the default 0.15 mm from
            # them, not the 0.2 mm of the ground net's class. Hold them to 0.15 mm here too.
            p.SetLocalClearance(mm(0.15))
for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(gnd)
    z.SetIsRuleArea(False)
    z.SetLocalClearance(mm(0.25))
    z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3))
    z.SetThermalReliefSpokeWidth(mm(0.3))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    o = z.Outline()
    o.NewOutline()
    bb = board.GetBoardEdgesBoundingBox()
    W, H = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
    for x, y in ((0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)):
        o.Append(mm(x), mm(y))
    board.Add(z)

pcbnew.ZONE_FILLER(board).Fill(board.Zones())
out = os.path.join(HERE, 'echo-breakout.kicad_pcb')
pcbnew.SaveBoard(out, board)
rules.write_project(out, POWER_CLEARANCE)
print('saved', out, 'tracks', len(board.GetTracks()))

# Placement file for JLCPCB: every part in the BOM, at its body centre (KiCad anchors a pin header on
# pin 1, JLCPCB on the middle of the part). Y is negative down the board, as KiCad exports it.
import csv
with open(os.path.join(HERE, 'jlcpcb', 'bom.csv')) as fh:
    in_bom = {r for row in csv.DictReader(fh) for r in row['Designator'].split(',')}
with open(os.path.join(HERE, 'jlcpcb', 'cpl.csv'), 'w') as fh:
    fh.write('Designator,Mid X,Mid Y,Layer,Rotation\n')
    for fp in sorted(board.GetFootprints(), key=lambda q: q.GetReference()):
        if fp.GetReference() not in in_bom:
            continue
        tht = any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in fp.Pads())
        if tht:
            xs = [p.GetPosition().x for p in fp.Pads()]
            ys = [p.GetPosition().y for p in fp.Pads()]
            x, y = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        else:
            x, y = fp.GetPosition().x, fp.GetPosition().y
        fh.write('%s,%.6fmm,%.6fmm,Top,%.6f\n' % (fp.GetReference(), pcbnew.ToMM(x), -pcbnew.ToMM(y),
                                                 fp.GetOrientationDegrees()))
print('placement file:', len(in_bom), 'parts')

