"""Writes keyra-stick.kicad_sch from design.py.

Every pin ends in a net label, a power symbol or a no-connect flag, so the drawing is the netlist
and nothing is wired by hand. Run: python3 gen/make_sch.py (KiCad's symbol libraries are read from
KICAD_SYMBOL_DIR, or from the macOS app bundle).
"""
import os
import uuid
from datetime import date
from pathlib import Path

from design import PARTS, POWER
from sexpr import Sym, dump, find, find_all, parse

HERE = Path(__file__).resolve().parent.parent
KICAD_SYMBOLS = Path(os.environ.get('KICAD_SYMBOL_DIR',
                                    '/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols'))
NS = uuid.UUID('6b2f0f9e-3c1d-4f4e-9b1a-6b65797261aa')   # stable ids: a re-run changes nothing


def uid(*key):
    return str(uuid.uuid5(NS, '/'.join(map(str, key))))


# Schematic position of each part (mm, A3 landscape, on the 1.27 mm grid) and the blocks' titles.
SCH = {
    'J1': (50.8, 81.28), 'R1': (88.9, 111.76), 'U2': (106.68, 76.2), 'R2': (134.62, 71.12), 'R3': (149.86, 71.12),
    'U3': (88.9, 170.18), 'C1': (54.61, 175.26), 'C2': (66.04, 175.26), 'C3': (118.11, 175.26),
    'C16': (38.1, 236.22), 'R8': (50.8, 236.22), 'Q1': (73.66, 231.14), 'R9': (96.52, 236.22),
    'Q2': (119.38, 241.3), 'R10': (137.16, 246.38),
    'U1': (233.68, 111.76), 'C17': (274.32, 49.53), 'C4': (284.48, 49.53), 'R4': (293.37, 49.53), 'C5': (303.53, 49.53),
    'JP1': (303.53, 81.28),
    'SW1': (355.6, 63.5),
    'FID1': (363.22, 190.5), 'FID2': (375.92, 190.5),
    'U4': (182.88, 226.06), 'C6': (200.66, 233.68), 'R6': (213.36, 226.06),
}
for _i in range(8):
    SCH[f'D{_i + 1}'] = (233.68 + 30.48 * (_i % 3), 222.25 + 20.32 * (_i // 3))
for _i in range(9):
    SCH[f'C{7 + _i}'] = (182.88 + 12.7 * _i, 278.13)
for _i in range(7):
    SCH[f'TP{_i + 1}'] = (334.01 + 10.16 * _i, 129.54)

BLOCKS = [
    ((25.4, 30.48), 'USB-C plug, CC and ESD',
     'Direct-attach device: Rd 5.1k on CC asks the host for 5 V. USBLC6 behind the plug, 22R near the module.'),
    ((25.4, 147.32), '3.3 V supply', 'AP2112K-3.3: 600 mA for a 355 mA Wi-Fi peak. 4.7u + 100n in, 10u out, 4.7u + 100n at the module.'),
    ((172.72, 30.48), 'ESP32-S3-MINI-1-N8', '8 MB flash, no PSRAM. Antenna at the board end: no copper under it.'),
    ((330.2, 30.48), 'Confirmation button', 'GPIO5, active low, the chip\'s own pull-up. GPIO0 (BOOT) is only the bottom jumper.'),
    ((172.72, 205.74), 'Status ring', '8 x SK6805-EC20 on LED_VDD; SN74LV1T125 lifts GPIO6 to a 5 V data swing; 100n at each LED.'),
    ((25.4, 213.36), 'Ring power switch', 'GPIO7 high: Q2 pulls Q1 on. C16 holds it off at plug-in, R9/C16 soften the turn-on; R10 keeps it off until the firmware runs.'),
    ((330.2, 109.22), 'Test points (bottom)', 'Bring-up and the UART console; JP1 shorted = download mode.'),
]


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
    """[(number, name, x, y, angle, type)] of a flattened symbol, lib coordinates (y up)."""
    out = []
    for unit in find_all(sym, 'symbol'):
        for p in find_all(unit, 'pin'):
            at = find(p, 'at')
            out.append((find(p, 'number')[1], find(p, 'name')[1], float(at[1]), float(at[2]),
                        int(float(at[3])) if len(at) > 3 else 0, p[1]))
    return out


def prop(name, value, x, y, hide=False, angle=0):
    effects = [Sym('effects'), [Sym('font'), [Sym('size'), 1.27, 1.27]]]
    if hide:
        effects.append([Sym('hide'), Sym('yes')])
    return [Sym('property'), name, value, [Sym('at'), x, y, angle], effects]


def instance(lib_id, ref, value, footprint, x, y, angle=0, lcsc=None, in_bom=True):
    hidden = ref.startswith('#')
    s = [Sym('symbol'), [Sym('lib_id'), lib_id], [Sym('at'), x, y, angle], [Sym('unit'), 1],
         [Sym('exclude_from_sim'), Sym('no')], [Sym('in_bom'), Sym('yes' if in_bom else 'no')],
         [Sym('on_board'), Sym('yes')], [Sym('dnp'), Sym('no')], [Sym('uuid'), uid('sym', ref)],
         prop('Reference', ref, x, y - 3.81, hide=hidden),
         prop('Value', value, x, y + 3.81, hide=hidden),
         prop('Footprint', footprint, x, y, hide=True),
         prop('Datasheet', '', x, y, hide=True)]
    if lcsc:
        s.append(prop('LCSC', lcsc, x, y, hide=True))
    s.append([Sym('instances'), [Sym('project'), 'keyra-stick',
                                  [Sym('path'), '/' + uid('root'), [Sym('reference'), ref], [Sym('unit'), 1]]]])
    return s


# Which way a pin's free end points on the sheet (screen coordinates, y down), from the lib angle.
OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}
LABEL_ANGLE = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}
# Rotation that turns a power symbol drawn pointing `home` to point `d`.
ROT = {(0, 1): {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270},
       (0, -1): {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270}}


def text(s, x, y, size, bold=False):
    font = [Sym('font'), [Sym('size'), size, size]]
    if bold:
        font.append([Sym('bold'), Sym('yes')])
    return [Sym('text'), s, [Sym('exclude_from_sim'), Sym('no')], [Sym('at'), x, y, 0],
            [Sym('effects'), font, [Sym('justify'), Sym('left'), Sym('bottom')]], [Sym('uuid'), uid('text', s)]]


def label(net, x, y, d, key):
    return [Sym('label'), net, [Sym('at'), x, y, LABEL_ANGLE[d]],
            [Sym('effects'), [Sym('font'), [Sym('size'), 1.27, 1.27]],
             [Sym('justify'), Sym('right' if d in ((-1, 0), (0, 1)) else 'left')]],
            [Sym('uuid'), uid('label', *key)]]


def build():
    lib_ids = sorted({p[1] for p in PARTS} | {'power:GND', 'power:VBUS', 'power:+3V3', 'power:PWR_FLAG'})
    symbols = {i: lib_symbol(i) for i in lib_ids}
    items, npwr = [], [0]

    def power(net, x, y, d):
        npwr[0] += 1
        home = (0, 1) if net == 'GND' else (0, -1)
        items.append(instance(f'power:{net}', f'#PWR{npwr[0]:03d}', net, '', x, y, ROT[home][d]))

    for ref, lib_id, value, footprint, lcsc, nets, _ in PARTS:
        x, y = SCH[ref]
        items.append(instance(lib_id, ref, value, footprint, x, y, lcsc=lcsc, in_bom=lcsc is not None))
        for num, _name, px, py, ang, _type in pins(symbols[lib_id]):
            ex, ey = round(x + px, 2), round(y - py, 2)
            d = OUTWARD[ang]
            net = nets.get(num)
            if net is None:
                items.append([Sym('no_connect'), [Sym('at'), ex, ey], [Sym('uuid'), uid('nc', ref, num)]])
            elif net in POWER:
                power(net, ex, ey, d)
            else:
                items.append(label(net, ex, ey, d, (ref, num)))

    # A PWR_FLAG on each net no output pin drives: the plug brings VBUS and GND from outside, and
    # LED_VDD comes through the Q1 switch.
    items.append(text('Power flags', 25.4, 264.16, 2.0, bold=True))
    for i, net in enumerate(('VBUS', 'GND', 'LED_VDD')):
        x, y = 38.1 + 20.32 * i, 276.86
        if net == 'LED_VDD':
            items.append(label(net, x, y, (0, 1), ('flag', net)))
        else:
            power(net, x, y, (0, 1) if net == 'GND' else (0, -1))
        items.append(instance('power:PWR_FLAG', f'#FLG0{i + 1}', 'PWR_FLAG', '', x, y, 180 if net != 'VBUS' else 0))

    for (bx, by), title, note in BLOCKS:
        items.append(text(title, bx, by, 2.0, bold=True))
        items.append(text(note, bx, by + 3.81, 1.27))

    sch = [Sym('kicad_sch'), [Sym('version'), 20231120], [Sym('generator'), 'keyra_gen'],
           [Sym('generator_version'), '1.0'], [Sym('uuid'), uid('root')], [Sym('paper'), 'A3'],
           [Sym('title_block'), [Sym('title'), 'Keyra Stick'], [Sym('date'), date.today().isoformat()],
            [Sym('rev'), '1.0'], [Sym('company'), 'Keyra open hardware (MIT)'],
            [Sym('comment'), 1, 'USB-C stick: ESP32-S3-MINI-1-N8, one button, 8 LED ring'],
            [Sym('comment'), 2, 'Generated by gen/make_sch.py from gen/design.py: edit those, not this file']],
           [Sym('lib_symbols')] + [symbols[i] for i in lib_ids]]
    sch += items
    sch.append([Sym('sheet_instances'), [Sym('path'), '/', [Sym('page'), '1']]])
    (HERE / 'keyra-stick.kicad_sch').write_text(dump(sch) + '\n')


if __name__ == '__main__':
    build()
