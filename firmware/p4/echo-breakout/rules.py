"""Design rules shared by gen_board.py and finish_board.py."""
import pcbnew

mm = pcbnew.FromMM


def apply_rules(board, power_clearance=0.2):
    """JLCPCB 2-layer limits with margin. Stored in the .kicad_pro, so re-applied before every save."""
    ds = board.GetDesignSettings()
    nc = ds.m_NetSettings.GetDefaultNetclass()
    nc.SetClearance(mm(0.15))
    nc.SetTrackWidth(mm(0.15))
    nc.SetViaDiameter(mm(0.6))
    nc.SetViaDrill(mm(0.3))
    pw = pcbnew.NETCLASS('Power')
    pw.SetClearance(mm(power_clearance))
    pw.SetTrackWidth(mm(0.4))
    pw.SetViaDiameter(mm(0.7))
    pw.SetViaDrill(mm(0.35))
    ds.m_NetSettings.SetNetclass('Power', pw)
    for pat in ('GND', '+3V3', '+5V', 'T_VDD', 'SPK_1', 'SPK_2'):
        ds.m_NetSettings.SetNetclassPatternAssignment(pat, 'Power')
    ds.m_TrackMinWidth = mm(0.127)
    ds.m_MinClearance = mm(0.127)
    ds.m_ViasMinSize = mm(0.5)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.3)


def write_project(pcb_path, power_clearance=0.2):
    """KiCad keeps net classes and board limits in the .kicad_pro; write one beside the board."""
    import json
    import os
    pro = os.path.splitext(pcb_path)[0] + '.kicad_pro'
    cls = lambda name, clr, tw, vd, vdr, pri: {
        'name': name, 'clearance': clr, 'track_width': tw, 'via_diameter': vd, 'via_drill': vdr,
        'microvia_diameter': 0.3, 'microvia_drill': 0.1, 'diff_pair_width': 0.2, 'diff_pair_gap': 0.25,
        'diff_pair_via_gap': 0.25, 'wire_width': 6, 'bus_width': 12, 'line_style': 0,
        'pcb_color': 'rgba(0, 0, 0, 0.000)', 'schematic_color': 'rgba(0, 0, 0, 0.000)', 'priority': pri}
    data = {
        'board': {'design_settings': {'rules': {
            'min_clearance': 0.127, 'min_track_width': 0.127, 'min_via_diameter': 0.5,
            'min_via_annular_width': 0.1, 'min_through_hole_diameter': 0.3, 'min_hole_to_hole': 0.25,
            'min_copper_edge_clearance': 0.3, 'min_silk_clearance': 0.0, 'min_text_height': 0.8,
            'min_text_thickness': 0.08, 'solder_mask_to_copper_clearance': 0.0,
            'min_hole_clearance': 0.25, 'use_height_for_length_calcs': True},
            # GP1 (the touch ground pad) is built in gen_board.py and has no library to compare against.
            'rule_severities': {'lib_footprint_issues': 'ignore'}}},
        'net_settings': {
            'classes': [cls('Default', 0.15, 0.15, 0.6, 0.3, 2147483647), cls('Power', power_clearance, 0.4, 0.7, 0.35, 0)],
            'meta': {'version': 4},
            'netclass_patterns': [{'netclass': 'Power', 'pattern': p}
                                  for p in ('GND', '+3V3', '+5V', 'T_VDD', 'SPK_1', 'SPK_2')]},
        'meta': {'filename': os.path.basename(pro), 'version': 3},
    }
    with open(pro, 'w') as f:
        json.dump(data, f, indent=2)
