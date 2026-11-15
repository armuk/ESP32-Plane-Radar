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
rules.apply_rules(board)
if not pcbnew.ImportSpecctraSES(board, os.path.join(HERE, 'echo-breakout.ses')):
    raise SystemExit('SES import failed')


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


if not os.environ.get('NO_CLEAN'):
    print('removed dangling segments:', remove_dangling(board))

gnd = board.FindNet('GND')
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
rules.write_project(out)
print('saved', out, 'tracks', len(board.GetTracks()))
