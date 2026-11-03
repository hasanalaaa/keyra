"""Writes keyra-stick.kicad_sch (and lib/keyra.kicad_sym) from design.py.

The sheet is drawn block by block below: wires inside a block, net labels between blocks, power
symbols for VBUS, +3V3 and GND. Parts and their nets come from design.py; check() refuses to write
a sheet whose connections differ from it, so the drawing can only move, never rewire the board.
Run: python3 gen/make_sch.py (KiCad's symbol libraries are read from KICAD_SYMBOL_DIR, or from the
macOS app bundle).
"""
import math
import os
import uuid
from collections import Counter
from datetime import date
from pathlib import Path

import symbols
from design import PARTS
from sexpr import Sym, dump, find, find_all, parse

HERE = Path(__file__).resolve().parent.parent
KICAD_SYMBOLS = Path(os.environ.get('KICAD_SYMBOL_DIR',
                                    '/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols'))
NS = uuid.UUID('6b2f0f9e-3c1d-4f4e-9b1a-6b65797261aa')   # stable ids: a re-run changes nothing
G = 2.54


def uid(*key):
    return str(uuid.uuid5(NS, '/'.join(map(str, key))))


def load_library(name):
    """Every top-level symbol in one KiCad library (file or KiCad 10 folder)."""
    if name == 'keyra':
        files = [HERE / 'lib' / 'keyra.kicad_sym']
    else:
        f, d = KICAD_SYMBOLS / f'{name}.kicad_sym', KICAD_SYMBOLS / f'{name}.kicad_symdir'
        files = [f] if f.exists() else sorted(d.glob('*.kicad_sym'))
    out = {}
    for f in files:
        for s in find_all(parse(f.read_text()), 'symbol'):
            out[s[1]] = s
    return out


_libs = {}


def lib_symbol(lib_id):
    """The symbol as a schematic embeds it: named lib:name, with any `extends` flattened."""
    lib, name = lib_id.split(':')
    if lib not in _libs:
        _libs[lib] = load_library(lib)
    s = list(_libs[lib][name])
    ext = find(s, 'extends')
    if ext:
        parent = lib_symbol(f'{lib}:{ext[1]}')
        s.remove(ext)
        props = {p[1] for p in find_all(s, 'property')}
        s += [p for p in parent if isinstance(p, list) and p[0] == 'property' and p[1] not in props]
        for key in ('pin_names', 'pin_numbers', 'exclude_from_sim', 'in_bom', 'on_board'):
            if not find(s, key) and find(parent, key):
                s.append(find(parent, key))
        for unit in find_all(parent, 'symbol'):
            u = list(unit)
            u[1] = name + u[1][len(ext[1]):]
            s.append(u)
    s[1] = lib_id
    return s


def pins(sym):
    """[(number, x, y)] of a flattened symbol, lib coordinates (y up)."""
    out = []
    for unit in find_all(sym, 'symbol'):
        for p in find_all(unit, 'pin'):
            at = find(p, 'at')
            out.append((find(p, 'number')[1], float(at[1]), float(at[2])))
    return out


def font(size=1.27, bold=False, justify=None, hide=False):
    f = [Sym('font'), [Sym('size'), size, size]]
    if bold:
        f.append([Sym('bold'), Sym('yes')])
    e = [Sym('effects'), f]
    if justify:
        e.append([Sym('justify')] + [Sym(j) for j in justify.split()])
    if hide:
        e.append([Sym('hide'), Sym('yes')])
    return e


def r2(v):
    return round(v + 0.0, 2)


class Sheet:
    def __init__(self):
        self.symbols = {i: lib_symbol(i) for i in sorted({p[1] for p in PARTS} | {
            'power:GND', 'power:VBUS', 'power:+3V3', 'power:PWR_FLAG'})}
        self.parts = {p[0]: p for p in PARTS}
        self.placed = {}          # ref -> {pin number: sheet point}
        self.items, self.segments, self.pin_points = [], [], []
        self.anchors = []         # (net, point): net labels and power symbols
        self.npwr = 0

    def _instance(self, lib_id, ref, value, x, y, rot, mirror, fields, footprint='', lcsc=None, in_bom=True):
        hidden = ref.startswith('#')
        (rx, ry, rj), vfield = fields
        (vx, vy, vj), vhide = (vfield or (0, 0, None)), vfield is None or lib_id == 'power:PWR_FLAG'
        fa = rot % 180            # a field's angle adds to the symbol's: this keeps the text level
        s = [Sym('symbol'), [Sym('lib_id'), lib_id], [Sym('at'), x, y, rot]]
        if mirror:
            s.append([Sym('mirror'), Sym(mirror)])
        s += [[Sym('unit'), 1], [Sym('exclude_from_sim'), Sym('no')], [Sym('in_bom'), Sym('yes' if in_bom else 'no')],
              [Sym('on_board'), Sym('yes')], [Sym('dnp'), Sym('no')], [Sym('uuid'), uid('sym', ref)],
              [Sym('property'), 'Reference', ref, [Sym('at'), r2(x + rx), r2(y + ry), fa], font(justify=rj, hide=hidden)],
              [Sym('property'), 'Value', value, [Sym('at'), r2(x + vx), r2(y + vy), fa],
               font(justify=vj, hide=vhide)],
              [Sym('property'), 'Footprint', footprint, [Sym('at'), x, y, 0], font(hide=True)],
              [Sym('property'), 'Datasheet', '', [Sym('at'), x, y, 0], font(hide=True)]]
        if lcsc:
            s.append([Sym('property'), 'LCSC', lcsc, [Sym('at'), x, y, 0], font(hide=True)])
        s.append([Sym('instances'), [Sym('project'), 'keyra-stick',
                                      [Sym('path'), '/' + uid('root'), [Sym('reference'), ref], [Sym('unit'), 1]]]])
        self.items.append(s)
        out = {}
        c, sn = round(math.cos(math.radians(rot))), round(math.sin(math.radians(rot)))
        for num, px, py in pins(self.symbols[lib_id]):
            if mirror == 'x':
                py = -py
            elif mirror == 'y':
                px = -px
            pt = (r2(x + px * c - py * sn), r2(y - (px * sn + py * c)))
            out[num] = pt
            self.pin_points.append(pt)
        return out

    def place(self, ref, x, y, rot=0, mirror=None, fields=None):
        """A design.py part at (x, y). fields: (dx, dy, justify) of the reference, then of the value."""
        _ref, lib_id, value, footprint, lcsc, _nets, _ = self.parts[ref]
        if fields is None:
            if lib_id in ('Device:R', 'Device:C') and rot % 180 == 0:
                fields = ((G, -1.27, 'left'), (G, 1.27, 'left'))
            elif lib_id in ('Device:R', 'Device:C'):
                fields = ((0, -G, None), (0, G, None))
            else:
                fields = ((0, -G * 4, None), (0, G * 4, None))
        self.placed[ref] = self._instance(lib_id, ref, value, x, y, rot, mirror, fields, footprint, lcsc,
                                          lcsc is not None)
        return self.placed[ref]

    def power(self, net, pt, flag=False):
        """A power symbol at pt (GND points down, the rest up), or a PWR_FLAG."""
        self.npwr += 1
        x, y = pt
        if flag:
            ref, lib_id, fields = f'#FLG{self.npwr:03d}', 'power:PWR_FLAG', ((0, -G * 2, None), (0, -G * 3, None))
        elif net == 'GND':
            ref, lib_id, fields = f'#PWR{self.npwr:03d}', 'power:GND', ((0, G * 2.5, None), (0, G * 1.5, None))
            self.anchors.append((net, (r2(x), r2(y))))
        else:
            ref, lib_id, fields = f'#PWR{self.npwr:03d}', f'power:{net}', ((0, -G * 2.5, None), (0, -G * 1.4, None))
            self.anchors.append((net, (r2(x), r2(y))))
        self._instance(lib_id, ref, 'PWR_FLAG' if flag else net, x, y, 0, None, fields)

    def wire(self, *pts):
        for a, b in zip(pts, pts[1:]):
            a, b = (r2(a[0]), r2(a[1])), (r2(b[0]), r2(b[1]))
            assert a[0] == b[0] or a[1] == b[1], f'wire {a} {b} is not straight'
            self.segments.append((a, b))
            self.items.append([Sym('wire'), [Sym('pts'), [Sym('xy'), *a], [Sym('xy'), *b]],
                               [Sym('stroke'), [Sym('width'), 0], [Sym('type'), Sym('default')]],
                               [Sym('uuid'), uid('wire', a, b)]])

    def label(self, net, pt, side='r'):
        x, y = r2(pt[0]), r2(pt[1])
        self.anchors.append((net, (x, y)))
        self.items.append([Sym('label'), net, [Sym('at'), x, y, 0 if side == 'r' else 180],
                           font(justify='left bottom' if side == 'r' else 'right bottom'),
                           [Sym('uuid'), uid('label', net, x, y)]])

    def nc(self, pt):
        self.items.append([Sym('no_connect'), [Sym('at'), *pt], [Sym('uuid'), uid('nc', *pt)]])

    def text(self, s, x, y, size=1.27, bold=False):
        self.items.append([Sym('text'), s, [Sym('exclude_from_sim'), Sym('no')], [Sym('at'), x, y, 0],
                           font(size, bold, 'left bottom'), [Sym('uuid'), uid('text', s, x, y)]])

    def frame(self, x0, y0, x1, y1, number, title, notes=()):
        self.items.append([Sym('rectangle'), [Sym('start'), x0, y0], [Sym('end'), x1, y1],
                           [Sym('stroke'), [Sym('width'), 0.15], [Sym('type'), Sym('dash')]],
                           [Sym('fill'), [Sym('type'), Sym('none')]], [Sym('uuid'), uid('frame', title)]])
        self.text(f'{number}  {title}', x0 + G, y0 + 5.08, 2.0, bold=True)
        for i, line in enumerate(notes):
            self.text(line, x0 + G, y1 - G - 3.3 * (len(notes) - 1 - i))

    def check(self):
        """The drawn connections must be design.py's, pin for pin, and nothing more."""
        parent = {}

        def root(p):
            parent.setdefault(p, p)
            while parent[p] != p:
                p = parent[p]
            return p

        for a, b in self.segments:
            parent[root(a)] = root(b)
        for net, pt in self.anchors:
            parent[root(('net', net))] = root(pt)
        found = {}
        for ref, part in self.parts.items():
            for num, net in part[5].items():
                pt = self.placed[ref][num]
                if root(pt) != root(('net', net)):
                    raise SystemExit(f'{ref} pin {num} is not drawn on {net}')
                found[net] = root(pt)
        clash = [r for r, n in Counter(found.values()).items() if n > 1]
        if clash:
            raise SystemExit('nets drawn together: ' + ', '.join(n for n, r in found.items() if r in clash))

    def junctions(self):
        ends = Counter(p for s in self.segments for p in s)
        pins_at = Counter(self.pin_points)
        for pt, n in ends.items():
            if n >= 3 or (n >= 2 and pins_at[pt]):
                self.items.append([Sym('junction'), [Sym('at'), *pt], [Sym('diameter'), 0],
                                   [Sym('color'), 0, 0, 0, 0], [Sym('uuid'), uid('junction', *pt)]])


# ---- the drawing, block by block -------------------------------------------------------------------

def stub(s, ref, num, dx, net):
    """A short wire out of a pin, ending in a net label."""
    p = s.placed[ref][num]
    end = (p[0] + dx, p[1])
    s.wire(p, end)
    s.label(net, end, 'r' if dx > 0 else 'l')


def usb(s):
    s.frame(15.24, 20.32, 142.24, 96.52, '1', 'USB-C plug and protection', [
        'J1 plugs straight into the computer. R1 (5.1 k on CC) asks the host for 5 V.',
        'U2 clamps static on D+, D- and VBUS right behind the plug; R2, R3 (22 R) damp the USB edges.'])
    j = s.place('J1', 35.56, 55.88, fields=((-G * 3, -G * 4.5, 'left'), (-G * 1.5, -G * 4.5, 'left')))
    y = j['A4'][1]
    s.wire(j['A4'], (53.34, y), (58.42, y))
    s.power('VBUS', (53.34, y))
    s.power('VBUS', (58.42, y), flag=True)
    s.wire(j['A1'], (29.21, j['A1'][1]))
    s.power('GND', j['A1'])
    s.power('GND', (29.21, j['A1'][1]), flag=True)
    u = s.place('U2', 73.66, 53.34, fields=((G * 1.5, -G * 3, 'left'), (-G * 2, G * 6, 'left')))
    s.power('VBUS', u['5'])
    s.power('GND', u['2'])
    s.wire(j['A6'], (55.88, j['A6'][1]), u['1'])
    s.label('USB_DP_C', (55.88, j['A6'][1]))
    s.wire(j['A7'], (55.88, j['A7'][1]), u['3'])
    s.label('USB_DM_C', (55.88, j['A7'][1]))
    # U2 passes each line through (pin 1 to 6, pin 3 to 4): both sides carry the plug's name
    r2_ = s.place('R2', 101.6, 53.34, rot=90)
    r3_ = s.place('R3', 101.6, 63.5, rot=90)
    s.wire(u['6'], (81.28, u['6'][1]), r2_['1'])
    s.label('USB_DP_C', (81.28, u['6'][1]))
    s.wire(u['4'], (81.28, u['4'][1]), (81.28, r3_['1'][1]), r3_['1'])
    s.label('USB_DM_C', (81.28, r3_['1'][1]))
    stub(s, 'R2', '2', 12.7, 'USB_DP')
    stub(s, 'R3', '2', 12.7, 'USB_DM')
    r1 = s.place('R1', 60.96, 64.77)
    s.wire(j['A5'], (50.8, j['A5'][1]), r1['1'])
    s.label('CC', (50.8, j['A5'][1]))
    s.power('GND', r1['2'])


def supply(s):
    s.frame(15.24, 101.6, 142.24, 172.72, '2', '3.3 V supply', [
        'U3: 600 mA LDO for the module (Wi-Fi peaks at about 355 mA). EN tied to VBUS: always on.',
        'C1, C2 at its input, C3 at its output; C17 and C4 sit at the module itself.'])
    u = s.place('U3', 91.44, 132.08, fields=((-G * 3, -G * 3, 'left'), (-G * 0.8, G * 3.6, 'right')))
    y = u['1'][1]
    c1 = s.place('C1', 43.18, y + 3.81)
    c2 = s.place('C2', 55.88, y + 3.81)
    s.wire((30.48, y), c1['1'], c2['1'], (73.66, y), u['1'])
    s.wire((73.66, y), (73.66, u['3'][1]), u['3'])
    s.power('VBUS', (30.48, y))
    for p in (c1['2'], c2['2'], u['2']):
        s.power('GND', p)
    s.nc(u['4'])
    c3 = s.place('C3', 109.22, y + 3.81)
    s.wire(u['5'], c3['1'], (121.92, y))
    s.power('+3V3', (121.92, y))
    s.power('GND', c3['2'])


def module(s):
    s.frame(147.32, 20.32, 276.86, 172.72, '3', 'ESP32-S3 module', [
        '8 MB flash, no PSRAM. The antenna end is the board end: no copper under it.',
        'R4/C5 delay EN at power-up. IO0 keeps the chip\'s pull-up: JP1 to GND = download mode.',
        'IO19/IO20 are the chip\'s own USB: keyboard, security key and console.'])
    u = s.place('U1', 215.9, 95.25, fields=((-symbols.MODULE_W, -symbols.MODULE_TOP - 1.27, 'left'),
                                            (G, -symbols.MODULE_TOP - 1.27, 'left')))
    top = u['3']
    s.wire(top, (top[0], 45.72), (205.74, 45.72), (195.58, 45.72), (185.42, 45.72))
    s.power('+3V3', (185.42, 45.72))
    for ref, x in (('C4', 205.74), ('C17', 195.58)):
        s.power('GND', s.place(ref, x, 45.72 + 3.81)['2'])
    en = u['45']
    r4 = s.place('R4', 180.34, en[1] - 3.81)
    c5 = s.place('C5', 172.72, en[1] + 3.81, fields=((-G, -1.27, 'right'), (-G, 1.27, 'right')))
    s.wire(en, r4['2'], c5['1'], (165.1, en[1]))
    s.label('EN', (165.1, en[1]), 'l')
    s.power('+3V3', r4['1'])
    s.power('GND', c5['2'])
    for num, net in (('4', 'BOOT'), ('39', 'U0TXD'), ('40', 'U0RXD')):
        stub(s, 'U1', num, -G * 2, net)
    for num, net in (('9', 'BTN'), ('10', 'LED_3V3'), ('11', 'LED_PWR_EN'), ('23', 'USB_DM'), ('24', 'USB_DP')):
        stub(s, 'U1', num, G * 2, net)
    s.power('GND', u['GND'])
    used = s.parts['U1'][5]
    for row in symbols.MODULE_LEFT + symbols.MODULE_RIGHT:
        if row and row[0] not in used:
            s.nc(u[row[0]])


def ring_power(s):
    s.frame(281.94, 20.32, 404.86, 96.52, '4', 'Ring power switch', [
        'IO7 high: Q2 pulls Q1\'s gate down and the ring gets 5 V. R10 keeps it off until the firmware runs.',
        'C16 holds Q1 off while VBUS rises at plug-in; R8 || R9 with C16 make the turn-on about 1 ms.'])
    x, rail, pg, g2 = 297.18, 35.56, 43.18, 60.96
    r8 = s.place('R8', x + 7.62, rail + 3.81, fields=((-G, -1.27, 'right'), (-G, 1.27, 'right')))
    c16 = s.place('C16', x + 15.24, rail + 3.81)
    r9 = s.place('R9', x + 25.4, pg + 3.81, fields=((-G, -1.27, 'right'), (-G, 1.27, 'right')))
    q1 = s.place('Q1', x + 50.8, pg, mirror='x', fields=((G * 2, -G * 0.5, 'left'), (G * 2, G * 0.5, 'left')))
    s.wire((x, rail), r8['1'], c16['1'], (q1['2'][0], rail), q1['2'])
    s.power('VBUS', (x, rail))
    s.wire(r8['2'], c16['2'], r9['1'], (x + 30.48, pg), q1['1'])
    s.label('LED_PG', (x + 30.48, pg))
    q2 = s.place('Q2', r9['2'][0] - G, g2, fields=((G * 2, -G * 0.5, 'left'), (G * 2, G * 0.5, 'left')))
    s.wire(r9['2'], (r9['2'][0], r9['2'][1] + G), q2['3'])
    s.label('LED_PGN', (r9['2'][0], r9['2'][1] + G))
    s.power('GND', q2['2'])
    r10 = s.place('R10', x + 7.62, g2 + 3.81, fields=((-G, -1.27, 'right'), (-G, 1.27, 'right')))
    s.wire((x, g2), r10['1'], q2['1'])
    s.label('LED_PWR_EN', (x, g2), 'l')
    s.power('GND', r10['2'])
    d = q1['3']
    out = (d[0], d[1] + 5.08)
    s.wire(d, out, (out[0] + 10.16, out[1]), (out[0] + 20.32, out[1]))
    s.power('LED_VDD', (out[0] + 10.16, out[1]), flag=True)
    s.label('LED_VDD', (out[0] + 20.32, out[1]))


def controls(s):
    s.frame(281.94, 101.6, 404.86, 172.72, '5', 'Button, recovery and test points')
    for ref, y, net, lines in (
            ('SW1', 114.3, 'BTN', ['Keyra\'s confirm button, in the middle of the ring.']),
            ('JP1', 127.0, 'BOOT', ['Bottom solder jumper: short it while plugging in', 'to reach download mode (recovery only).'])):
        p = s.place(ref, 304.8, y, fields=((0, -G * 1.5, None), (0, G * 1.5, None)))
        a, b = p['1'], p['4' if ref == 'SW1' else '2']
        s.wire((292.1, y), a)
        s.label(net, (292.1, y), 'l')
        s.wire(b, (317.5, y))
        s.power('GND', (317.5, y))
        for i, line in enumerate(lines):
            s.text(line, 325.12, y + 0.64 + 3.55 * i)
    s.text('Bottom pads on a 2.54 mm grid, for a pogo-pin jig:', 289.56, 139.7)
    for i in range(7):
        ref = f'TP{i + 1}'
        net = s.parts[ref][5]['1']
        x, y = (299.72 if i < 4 else 360.68), 147.32 + 7.62 * (i % 4)
        tp = s.place(ref, x, y, rot=90, fields=((-G * 1.5, -G * 0.8, None), None))
        end = (x + 7.62, y)
        s.wire(tp['1'], end)
        if net in ('VBUS', '+3V3', 'GND'):
            s.power(net, end)
        else:
            s.label(net, end)
    for ref, x in (('FID1', 386.08), ('FID2', 396.24)):
        s.place(ref, x, 110.49, fields=((0, -G * 1.5, None), (0, G * 1.5, None)))


def ring(s):
    s.frame(15.24, 177.8, 404.86, 248.92, '6', 'Status ring', [
        'U4 lifts IO6 (3.3 V) to a 5 V data swing; R6 damps the line. Eight SK6805-EC20 in one chain, all showing '
        'the same colour; D8\'s DOUT stays open.',
        'One 100 nF at each LED (C7 to C14) and 4.7 uF bulk (C15), all on the switched LED_VDD.'])
    y = 210.82
    u = s.place('U4', 50.8, y, fields=((G * 1.5, -G * 2.5, 'left'), (G * 0.5, G * 4.5, 'left')))
    oe = u['1']
    s.wire(oe, (oe[0], oe[1] - 5.08), (oe[0] + 5.08, oe[1] - 5.08))
    s.power('GND', (oe[0] + 5.08, oe[1] - 5.08))
    vcc = u['5']
    c6 = s.place('C6', vcc[0] - 10.16, vcc[1] - 6.35)
    s.wire(vcc, (vcc[0], vcc[1] - 10.16), c6['1'], (vcc[0] - 17.78, vcc[1] - 10.16))
    s.label('LED_VDD', (vcc[0] - 17.78, vcc[1] - 10.16), 'l')
    s.power('GND', c6['2'])
    s.power('GND', u['3'])
    stub(s, 'U4', '2', -G * 3, 'LED_3V3')
    r6 = s.place('R6', 83.82, y, rot=90)
    s.wire(u['4'], (69.85, y), r6['1'])
    s.label('LED_5V', (69.85, y))
    vdd, gnd = 195.58, 226.06
    x0, step = 111.76, 30.48
    s.wire(r6['2'], (91.44, y), (x0 - 7.62, y))
    s.label('LED_D1', (91.44, y))
    rail_v, rail_g = [(97.79, vdd)], [(97.79, gnd)]
    for i in range(8):
        x = x0 + step * i
        d = s.place(f'D{i + 1}', x, y, fields=((G * 1.5, -G * 2.5, 'left'), (G * 1.5, G * 2.5, 'left')))
        s.wire(d['3'], (x, vdd))
        s.wire(d['1'], (x, gnd))
        rail_v.append((x, vdd))
        rail_g.append((x, gnd))
        if i < 7:
            s.wire(d['4'], (x + 12.7, y), (x + step - 7.62, y))
            s.label(f'LED_D{i + 2}', (x + 12.7, y))
        else:
            s.nc(d['4'])
    caps = ['C15'] + [f'C{7 + i}' for i in range(8)]
    cx = [340.36 + 7.62 * i for i in range(len(caps))]
    s.wire(*(rail_v + [(c, vdd) for c in cx]))
    s.label('LED_VDD', (97.79, vdd), 'l')
    s.wire(*rail_g)
    s.power('GND', (97.79, gnd))
    for ref, c in zip(caps, cx):
        s.place(ref, c, vdd + 3.81, fields=((0, G * 3.2, None), (0, G * 4.2, None)))
    s.wire((cx[0] - 5.08, vdd + 7.62), *[(c, vdd + 7.62) for c in cx])
    s.power('GND', (cx[0] - 5.08, vdd + 7.62))


def notes(s):
    s.text('Notes', 15.24, 256.54, 2.0, bold=True)
    for i, line in enumerate([
            'Keyra Stick rev 1.0, Keyra\'s own board: 4 layers, 0.8 mm, 53.1 x 18.5 mm, ENIG. JLCPCB assembles it from fab/ (BOM, CPL).',
            'Every value, net and position comes from gen/design.py; gen/build.sh redraws this sheet and the board and checks both.',
            'Firmware: the keyra-stick board profile (button IO5, eight LEDs on IO6, their supply on IO7).']):
        s.text(line, 15.24, 262.89 + 4.0 * i)


def build():
    symbols.write(HERE / 'lib' / 'keyra.kicad_sym', lib_symbol)
    _libs.pop('keyra', None)
    s = Sheet()
    for block in (usb, supply, module, ring_power, controls, ring, notes):
        block(s)
    missing = set(s.parts) - set(s.placed)
    if missing:
        raise SystemExit('not drawn: ' + ', '.join(sorted(missing)))
    s.check()
    s.junctions()
    sch = [Sym('kicad_sch'), [Sym('version'), 20231120], [Sym('generator'), 'keyra_gen'],
           [Sym('generator_version'), '1.0'], [Sym('uuid'), uid('root')], [Sym('paper'), 'A3'],
           [Sym('title_block'), [Sym('title'), 'Keyra Stick'], [Sym('date'), date.today().isoformat()],
            [Sym('rev'), '1.0'], [Sym('company'), 'Keyra open hardware (MIT)'],
            [Sym('comment'), 1, 'USB-C stick: ESP32-S3-MINI-1-N8, one button, a ring of eight RGB LEDs'],
            [Sym('comment'), 2, 'Generated by gen/make_sch.py from gen/design.py: edit those, not this file']],
           [Sym('lib_symbols')] + list(s.symbols.values())]
    sch += s.items
    sch.append([Sym('sheet_instances'), [Sym('path'), '/', [Sym('page'), '1']]])
    (HERE / 'keyra-stick.kicad_sch').write_text(dump(sch) + '\n')


if __name__ == '__main__':
    build()
