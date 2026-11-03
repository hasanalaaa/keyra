"""Places keyra-stick.kicad_pcb from design.py (run with KiCad's own Python, see build.sh).

  place NET    outline, parts (nets from the schematic's netlist NET), planes, the antenna keep-out, silkscreen
  dsn FILE     export the Specctra design for Freerouting
  finish FILE  import Freerouting's session and fill the zones
"""
import math
import sys
import uuid
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
import design as D  # noqa: E402

HERE = Path(__file__).resolve().parent.parent
PCB = HERE / 'keyra-stick.kicad_pcb'
KICAD_FP = Path('/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints')
NS = uuid.UUID('6b2f0f9e-3c1d-4f4e-9b1a-6b65797261aa')   # the same ids as make_sch.py
MM = pcbnew.FromMM
ANTENNA_DEPTH = 5.08   # ESP32-S3-MINI-1 antenna area along the module (footprint silk and courtyard)


def uid(*key):
    return str(uuid.uuid5(NS, '/'.join(map(str, key))))


def pt(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


def segment(board, a, b, layer, width=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pt(*a))
    s.SetEnd(pt(*b))
    s.SetLayer(layer)
    s.SetWidth(MM(width))
    board.Add(s)


def outline_points():
    """The board edge, clockwise from the tip: the plug's tongue (DWG001-A401), a flare, the stick."""
    nw, nl = D.TONGUE_NECK
    hw, tw, W = nw / 2, D.TONGUE_W / 2, D.BOARD_W / 2
    c = 0.20                                    # chamfer on the tip's corners
    return [(0, -hw + c), (c, -hw), (nl, -hw), (nl, -tw), (D.TONGUE_L, -tw), (D.FLARE_END, -W),
            (D.BOARD_L, -W), (D.BOARD_L, W), (D.FLARE_END, W), (D.TONGUE_L, tw), (nl, tw), (nl, hw),
            (c, hw), (0, hw - c)]


def load_fp(fpid):
    lib, name = fpid.split(':')
    path = HERE / 'lib' / 'keyra.pretty' if lib == 'keyra' else KICAD_FP / f'{lib}.pretty'
    fp = pcbnew.FootprintLoad(str(path), name)
    if fp is None:
        raise SystemExit(f'footprint not found: {fpid}')
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def set_field(fp, name, value):
    fp.SetField(name, value)
    f = fp.GetField(name)
    f.SetVisible(False)
    f.SetLayer(pcbnew.F_Fab)


def netlist_pins(path):
    """{(ref, pad): net} from the schematic's netlist, so the board cannot drift from it."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from sexpr import find, find_all, parse
    out = {}
    for net in find_all(find(parse(Path(path).read_text()), 'nets'), 'net'):
        name = find(net, 'name')[1]
        for node in find_all(net, 'node'):
            out[(find(node, 'ref')[1], find(node, 'pin')[1])] = name
    return out


def add_parts(board, nets, pins):
    for ref, _sym, value, fpid, lcsc, _padnets, (x, y, rot, side) in D.PARTS:
        fp = load_fp(fpid)
        fp.SetReference(ref)
        fp.SetValue(value)
        if lcsc:
            set_field(fp, 'LCSC', lcsc)
        else:
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        fp.SetPath(pcbnew.KIID_PATH('/' + uid('sym', ref)))
        board.Add(fp)
        fp.SetPosition(pt(x, y))
        if side == 'B':
            fp.Flip(pt(x, y), pcbnew.FLIP_DIRECTION_TOP_BOTTOM)
        fp.SetOrientationDegrees(rot)
        fp.Reference().SetVisible(False)       # too dense for designators; the fab layer keeps them
        if ref in ('U1', 'J1'):                # big ground pads: solid to the pour, not thermal spokes
            for pad in fp.Pads():
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        for pad in fp.Pads():
            net = pins.get((ref, pad.GetNumber()))
            if net:
                pad.SetNet(nets[net])


def add_zone(board, net, layer, pts):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetLocalClearance(MM(0.2))
    z.SetMinThickness(MM(0.2))
    z.SetThermalReliefGap(MM(0.25))
    z.SetThermalReliefSpokeWidth(MM(0.3))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)   # no floating copper scraps
    ol = z.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(MM(x), MM(y))
    board.Add(z)


def add_keepout(board, x0, x1):
    """No copper of any kind under the module's antenna, on every layer (Espressif's layout guide)."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowZoneFills(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
        ls.AddLayer(layer)
    z.SetLayerSet(ls)
    z.SetZoneName('antenna')
    W = D.BOARD_W / 2 + 0.5
    ol = z.Outline()
    ol.NewOutline()
    for x, y in ((x0, -W), (x1 + 0.5, -W), (x1 + 0.5, W), (x0, W)):
        ol.Append(MM(x), MM(y))
    board.Add(z)


def fiducial_keepouts(board):
    """No track within a fiducial's 1.5 mm mask opening: the camera needs bare copper on bare board."""
    for f in board.GetFootprints():
        if not f.GetReference().startswith('FID'):
            continue
        z = pcbnew.ZONE(board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowZoneFills(False)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET()
        ls.AddLayer(pcbnew.F_Cu)
        z.SetLayerSet(ls)
        c = f.GetPosition()
        cx, cy = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
        ol = z.Outline()
        ol.NewOutline()
        for k in range(16):
            a = k * math.pi / 8
            ol.Append(MM(cx + 1.0 * math.cos(a)), MM(cy + 1.0 * math.sin(a)))
        board.Add(z)


def mark_keepout(board):
    """The gold mark on the back is the ground pour itself: no track or via may cross it."""
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowZoneFills(False)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    ls.AddLayer(pcbnew.B_Cu)
    z.SetLayerSet(ls)
    z.SetZoneName('mark')
    mx, my, ms = D.MARK
    h = ms / 2 + 0.2
    ol = z.Outline()
    ol.NewOutline()
    for x, y in ((mx - h, my - h), (mx + h, my - h), (mx + h, my + h), (mx - h, my + h)):
        ol.Append(MM(x), MM(y))
    board.Add(z)


def text(board, s, x, y, layer, size=1.0, thick=0.15, angle=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pt(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(thick))
    t.SetBold(bold)
    t.SetTextAngleDegrees(angle)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)


def contour(board, cx, cy, size, layer, width, mirror=False):
    """Keyra's Contour (docs/brand/BRAND.md §2) on its 100-unit grid, scaled to `size` mm; mirrored
    for the back, so it reads right when the board is turned over."""
    k = size / 100.0
    kx = -k if mirror else k

    def bez(p0, p1, p2, p3, n=12):
        return [tuple((1 - t) ** 3 * a + 3 * (1 - t) ** 2 * t * b + 3 * (1 - t) * t * t * c + t ** 3 * d
                      for a, b, c, d in zip(p0, p1, p2, p3)) for t in (i / n for i in range(n + 1))]

    path = [(72.3, 31.37), (75.5, 33), (75.5, 44)]
    path += bez((75.5, 44), (75.5, 60), (64, 73), (50, 80))[1:]
    path += bez((50, 80), (36, 73), (24.5, 60), (24.5, 44))[1:]
    path += [(24.5, 33), (50, 20), (63.8, 27.04)]
    pts = [(cx + (x - 50) * kx, cy + (y - 50) * k) for x, y in path]
    for a, b in zip(pts, pts[1:]):
        segment(board, a, b, layer, width)


def silkscreen(board):
    """The top stays clean (parts only: an enclosure covers it). The back carries the mark in bare
    gold (copper opened in the mask), the name, the test points' names and the fab's order spot."""
    B = pcbnew.B_SilkS
    mx, my, ms = D.MARK
    contour(board, mx, my, ms, pcbnew.B_Mask, 0.7, mirror=True)   # bare gold over the ground pour
    text(board, 'KEYRA', D.RING[0], -3.6, B, 1.8, 0.3, bold=True)
    text(board, 'Keyra Stick v1.0 · MIT', D.RING[0], 3.4, B, 0.8, 0.13)
    text(board, 'JLCJLCJLCJLC', D.RING[0], 5.4, B, 0.8, 0.13)    # where JLCPCB prints its order number
    for (x, y), name in zip(D.TP_AT, ('5V', '3V3', 'GND', 'EN', 'IO0', 'TX', 'RX')):
        text(board, name, x, y - 1.4 if y < 0 else y + 1.4 if y > 0 else y - 1.4, B, 0.8, 0.13)
    text(board, 'BOOT', 42.2, 0.0, B, 0.8, 0.13, angle=90)


def _seg_dist(p, a, b):
    (px, py), (ax, ay), (bx, by) = p, a, b
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy or 1e-12)))
    return ((px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2) ** 0.5


HAND_ROUTED = {('C17', '1'), ('C4', '1')}   # reach the plane through the module's pin-3 via


def pad_at(board, ref, num):
    f = next(f for f in board.GetFootprints() if f.GetReference() == ref)
    p = next(p for p in f.Pads() if p.GetNumber() == num)
    return pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y), p.GetNet()


def link(board, net, pts, width=0.25):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pt(*a))
        t.SetEnd(pt(*b))
        t.SetWidth(MM(width))
        t.SetLayer(pcbnew.F_Cu)
        t.SetNet(net)
        board.Add(t)


def module_strips(board):
    """The parts in the 0.75 mm strips beside the module, wired by hand: the 3V3 pin straight down
    to its two capacitors, the EN pin to its RC (Espressif: keep CHIP_PU short)."""
    x3, y3, n3 = pad_at(board, 'U1', '3')
    c17 = pad_at(board, 'C17', '1')
    c4 = pad_at(board, 'C4', '1')
    link(board, n3, [(x3, y3), (x3, c17[1]), (c17[0], c17[1])], 0.3)
    link(board, n3, [(x3, c4[1]), (c4[0], c4[1])], 0.3)
    xe, ye, ne = pad_at(board, 'U1', '45')
    c5 = pad_at(board, 'C5', '1')
    r4 = pad_at(board, 'R4', '2')
    link(board, ne, [(xe, ye), (c5[0], c5[1])])
    link(board, ne, [(r4[0], r4[1]), (c5[0], c5[1])])


def fanout(board):
    """A via beside every GND and +3V3 pad, into the inner planes (In1 ground, In2 3.3 V), so
    Freerouting only routes signals. Each via takes the first spot round its pad that keeps every
    other net's copper, the board edge, the antenna and the gold mark clear."""
    via_r, gap = 0.3, 0.2
    edge = outline_points()
    ant0 = D.BOARD_L - ANTENNA_DEPTH
    keep_out = [(D.MARK[0] - D.MARK[2] / 2 - 0.5, D.MARK[0] + D.MARK[2] / 2 + 0.5)]
    pads = [(p, p.GetEffectivePolygon(p.GetLayer() if p.IsOnLayer(pcbnew.F_Cu) is False else pcbnew.F_Cu,
                                      pcbnew.ERROR_INSIDE)) for f in board.GetFootprints() for p in f.Pads()]
    vias = []

    def inside(x, y):
        hit = False
        for (ax, ay), (bx, by) in zip(edge, edge[1:] + edge[:1]):
            if (ay > y) != (by > y) and x < ax + (y - ay) * (bx - ax) / (by - ay):
                hit = not hit
        return hit

    def clear(x, y, net, layer, track_from):
        if x > ant0 - via_r - 0.2 or any(a <= x <= b for a, b in keep_out) or not inside(x, y):
            return False
        if min(_seg_dist((x, y), a, b) for a, b in zip(edge, edge[1:] + edge[:1])) < via_r + 0.3:
            return False
        if net == '+3V3' and x < D.FLARE_END + 0.6:
            return False
        if any(((x - vx) ** 2 + (y - vy) ** 2) ** 0.5 < 0.9 for vx, vy in vias):
            return False
        for t in board.GetTracks():
            if t.GetNetname().lstrip('/') == net or t.Type() != pcbnew.PCB_TRACE_T:
                continue
            a, b = t.GetStart(), t.GetEnd()
            d = _seg_dist((x, y), (pcbnew.ToMM(a.x), pcbnew.ToMM(a.y)), (pcbnew.ToMM(b.x), pcbnew.ToMM(b.y)))
            if d < via_r + gap + pcbnew.ToMM(t.GetWidth()) / 2:
                return False
        v = pcbnew.VECTOR2I(MM(x), MM(y))
        for p, poly in pads:
            if p.GetNetname().lstrip('/') == net:
                continue
            d = pcbnew.ToMM(int(poly.SquaredDistance(v) ** 0.5))
            if d < via_r + (0.6 if not p.GetNetname() else gap):     # fiducials keep their mask ring
                return False
            if p.IsOnLayer(layer):     # the short track from the pad must clear it too
                for k in range(1, 5):
                    tx = track_from[0] + (x - track_from[0]) * k / 5
                    ty = track_from[1] + (y - track_from[1]) * k / 5
                    if pcbnew.ToMM(int(poly.SquaredDistance(pcbnew.VECTOR2I(MM(tx), MM(ty))) ** 0.5)) < 0.15 + gap:
                        return False
        return True

    nets = {n.GetNetname().lstrip('/'): n for n in board.GetNetInfo().NetsByNetcode().values()}
    for f in board.GetFootprints():
        ref = f.GetReference()
        for p in f.Pads():
            net = p.GetNetname().lstrip('/')
            if net not in ('GND', '+3V3'):
                continue
            if (ref, p.GetNumber()) in HAND_ROUTED:
                continue
            if ref == 'J1' and net == 'GND' or ref == 'U1' and net == 'GND' and p.GetNumber() not in ('42', '43'):
                continue    # the plug's ground: the tongue's pours; the module's: the centre vias below
                            # (42/43 sit in their own patch of top copper, so they get a via each)
            layer = pcbnew.F_Cu if p.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
            c = p.GetPosition()
            cx, cy = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
            bb = p.GetBoundingBox()
            half = max(pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())) / 2
            fc = f.GetPosition()
            away = (cx - pcbnew.ToMM(fc.x), cy - pcbnew.ToMM(fc.y))
            dirs = [(math.cos(a * math.pi / 8), math.sin(a * math.pi / 8)) for a in range(16)]
            dirs.sort(key=lambda d: -(d[0] * away[0] + d[1] * away[1]))   # outward from the part first
            placed = False
            for r in (half + 0.45, half + 0.7, half + 1.0, half + 1.4, half + 1.9, half + 2.5):
                for dx, dy in dirs:
                    x, y = round(cx + dx * r, 3), round(cy + dy * r, 3)
                    if clear(x, y, net, layer, (cx, cy)):
                        t = pcbnew.PCB_TRACK(board)
                        t.SetStart(c)
                        t.SetEnd(pt(x, y))
                        t.SetWidth(MM(0.3))
                        t.SetLayer(layer)
                        t.SetNet(nets[net])
                        board.Add(t)
                        add_via(board, x, y, nets[net])
                        vias.append((x, y))
                        placed = True
                        break
                if placed:
                    break
            if not placed:
                print(f'fanout: no room beside {ref}.{p.GetNumber()} ({net})')
    # The plug's VBUS, routed by hand: its pads sit 0.2 mm apart, too tight for the router at
    # power width. A4/A9 (top) and B9/B4 (bottom) meet at a via behind each pair.
    for y in (1.25, -1.25):
        vy = 1.7 if y > 0 else -1.7
        for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
            for a, b in (((1.95, y), (2.9, y)), ((2.9, y), (3.6, vy))):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pt(*a))
                t.SetEnd(pt(*b))
                t.SetWidth(MM(0.3))
                t.SetLayer(layer)
                t.SetNet(nets['VBUS'])
                board.Add(t)
        add_via(board, 3.6, vy, nets['VBUS'])
    t = pcbnew.PCB_TRACK(board)              # the two vias joined on the back, under the USB pair
    t.SetStart(pt(3.6, -1.7))
    t.SetEnd(pt(3.6, 1.7))
    t.SetWidth(MM(0.3))
    t.SetLayer(pcbnew.B_Cu)
    t.SetNet(nets['VBUS'])
    board.Add(t)
    # the tongue's ground pours, joined to the planes behind the plug's pads (under its housing)
    for x in (3.9, 5.6):
        for y in (-3.25, 3.25):
            add_via(board, x, y, nets['GND'])
    # the module's centre pads: four vias in the gaps of its 3 x 3 ground grid
    u1 = next(f for f in board.GetFootprints() if f.GetReference() == 'U1')
    ux, uy = pcbnew.ToMM(u1.GetPosition().x), pcbnew.ToMM(u1.GetPosition().y)
    for dx in (-0.825, 0.825):
        for dy in (-0.825, 0.825):
            add_via(board, ux + dx, uy + dy, nets['GND'])


def add_via(board, x, y, net):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y))
    v.SetDrill(MM(0.3))
    v.SetWidth(MM(0.6))
    v.SetNet(net)
    board.Add(v)


def place(netlist):
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(MM(0.8))
    ds.SetCopperLayerCount(4)
    pins = netlist_pins(netlist)
    nets = {}
    for name in sorted(set(pins.values())):
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    if {n.lstrip('/') for n in nets if not n.startswith('unconnected-')} != set(D.NETS):
        raise SystemExit('the netlist and design.py disagree: run gen/make_sch.py first')
    pts = outline_points()
    for a, b in zip(pts, pts[1:] + pts[:1]):
        segment(board, a, b, pcbnew.Edge_Cuts, 0.05)
    add_parts(board, nets, pins)
    W, ant0 = D.BOARD_W / 2, D.BOARD_L - ANTENNA_DEPTH
    plane = [(0, -D.TONGUE_NECK[0] / 2), (D.TONGUE_L, -D.TONGUE_W / 2), (D.FLARE_END, -W), (ant0, -W),
             (ant0, W), (D.FLARE_END, W), (D.TONGUE_L, D.TONGUE_W / 2), (0, D.TONGUE_NECK[0] / 2)]
    # Four layers: signals and ground fill outside, a solid ground plane under the top layer (the
    # USB pair's reference), and the 3.3 V plane under the button, the ring and the module.
    add_zone(board, nets['GND'], pcbnew.F_Cu, plane)
    add_zone(board, nets['GND'], pcbnew.In1_Cu, plane)
    add_zone(board, nets['GND'], pcbnew.B_Cu, plane)
    add_zone(board, nets['+3V3'], pcbnew.In2_Cu, [(D.FLARE_END, -W + 0.3), (ant0, -W + 0.3), (ant0, W - 0.3),
                                                  (D.FLARE_END, W - 0.3)])
    for layer in (pcbnew.In1_Cu, pcbnew.In2_Cu):
        board.SetLayerType(layer, pcbnew.LT_POWER)
    add_keepout(board, ant0, D.BOARD_L)
    mark_keepout(board)
    fiducial_keepouts(board)
    silkscreen(board)
    module_strips(board)
    fanout(board)
    board.Save(str(PCB))


STACKUP = """		(stackup
			(layer "F.SilkS" (type "Top Silk Screen") (color "White"))
			(layer "F.Paste" (type "Top Solder Paste"))
			(layer "F.Mask" (type "Top Solder Mask") (color "Black") (thickness 0.01))
			(layer "F.Cu" (type "copper") (thickness 0.035))
			(layer "dielectric 1" (type "prepreg") (thickness 0.1) (material "FR4") (epsilon_r 4.1) (loss_tangent 0.02))
			(layer "In1.Cu" (type "copper") (thickness 0.0152))
			(layer "dielectric 2" (type "core") (thickness 0.465) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
			(layer "In2.Cu" (type "copper") (thickness 0.0152))
			(layer "dielectric 3" (type "prepreg") (thickness 0.1) (material "FR4") (epsilon_r 4.1) (loss_tangent 0.02))
			(layer "B.Cu" (type "copper") (thickness 0.035))
			(layer "B.Mask" (type "Bottom Solder Mask") (color "Black") (thickness 0.01))
			(layer "B.Paste" (type "Bottom Solder Paste"))
			(layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))
			(copper_finish "ENIG")
			(dielectric_constraints no)
		)
"""


def add_stackup(path):
    """Black mask, white silkscreen, ENIG and a 0.8 mm four-layer stack close to JLCPCB's standard
    one (the fab uses its own exact stack; these values serve the renders and the 3D model)."""
    text = Path(path).read_text()
    if '(stackup' not in text:
        Path(path).write_text(text.replace('\t(setup\n', '\t(setup\n' + STACKUP, 1))


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'place':
        place(sys.argv[2])
    else:
        board = pcbnew.LoadBoard(str(PCB))
        if cmd == 'dsn' and not pcbnew.ExportSpecctraDSN(board, sys.argv[2]):
            raise SystemExit('Specctra export failed')
        if cmd == 'finish':
            if not pcbnew.ImportSpecctraSES(board, sys.argv[2]):
                raise SystemExit('Specctra session import failed')
            # Freerouting can leave a stub with a free end; remove them until none is left.
            while True:
                conn = board.GetConnectivity()
                conn.RecalculateRatsnest()
                stubs = [t for t in board.GetTracks()
                         if t.Type() == pcbnew.PCB_TRACE_T and conn.TestTrackEndpointDangling(t, False)]
                if not stubs:
                    break
                for t in stubs:
                    board.Remove(t)
            pcbnew.ZONE_FILLER(board).Fill(board.Zones())
            board.Save(str(PCB))
            add_stackup(PCB)
