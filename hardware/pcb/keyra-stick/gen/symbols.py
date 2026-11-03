"""Writes lib/keyra.kicad_sym: the schematic symbols of the parts KiCad's libraries lack.

Pin numbers are the footprints' pad numbers (lib/keyra.pretty). Where KiCad has a part with the
same drawing (the SK6805 LED, the push button, the '125 buffer) its graphics are reused and only
the pin numbers change; the plug and the module are drawn here, laid out for a readable sheet:
supply on top, ground at the bottom, used pins named for what they do, unused pins kept so every
pad still has a pin. Stacked pins (the plug's four VBUS contacts, the module's ground pads) share
one point; all but one are hidden.
"""
import copy

from sexpr import Sym, dump

G = 2.54


def _font(size=1.27):
    return [Sym('effects'), [Sym('font'), [Sym('size'), size, size]]]


def _pin(kind, num, name, x, y, angle, length=G * 2, hide=False):
    p = [Sym('pin'), Sym(kind), Sym('line'), [Sym('at'), x, y, angle], [Sym('length'), length]]
    if hide:
        p.append([Sym('hide'), Sym('yes')])
    return p + [[Sym('name'), name, _font()], [Sym('number'), num, _font()]]


def _prop(name, value, x=0.0, y=0.0, hide=False):
    e = _font()
    if hide:
        e.append([Sym('hide'), Sym('yes')])
    return [Sym('property'), name, value, [Sym('at'), x, y, 0], e]


def _rect(x0, y0, x1, y1):
    return [Sym('rectangle'), [Sym('start'), x0, y0], [Sym('end'), x1, y1],
            [Sym('stroke'), [Sym('width'), 0.254], [Sym('type'), Sym('default')]],
            [Sym('fill'), [Sym('type'), Sym('background')]]]


def _symbol(name, ref, value, footprint, description, graphics, pins):
    return [Sym('symbol'), name, [Sym('pin_names'), [Sym('offset'), 1.016]],
            [Sym('exclude_from_sim'), Sym('no')], [Sym('in_bom'), Sym('yes')], [Sym('on_board'), Sym('yes')],
            _prop('Reference', ref), _prop('Value', value),
            _prop('Footprint', footprint, hide=True), _prop('Datasheet', '', hide=True),
            _prop('Description', description, hide=True),
            [Sym('symbol'), f'{name}_0_1'] + graphics, [Sym('symbol'), f'{name}_1_1'] + pins]


def plug():
    """USB-C plug as a USB 2.0 device uses it: VBUS, D+, D-, CC on the right, ground below."""
    x = G * 5
    pins = [_pin('passive', 'A4', 'VBUS', x, G * 3, 180)]
    pins += [_pin('passive', n, 'VBUS', x, G * 3, 180, hide=True) for n in ('A9', 'B4', 'B9')]
    pins += [_pin('passive', 'A6', 'D+', x, G, 180), _pin('passive', 'A7', 'D-', x, 0, 180),
             _pin('passive', 'A5', 'CC', x, -G * 2, 180),
             _pin('passive', 'A1', 'GND', 0, -G * 6, 90)]
    pins += [_pin('passive', n, 'GND', 0, -G * 6, 90, hide=True) for n in ('A12', 'B1', 'B12', '25', '26')]
    # a plug has CC on A5 and D+/D- on the A row only; SuperSpeed, SBU and VCONN stay open
    unused = {'A2': 'SSTXP1', 'A3': 'SSTXn1', 'A8': 'SBU1', 'A10': 'SSRXn2', 'A11': 'SSRXp2', 'B2': 'SSTXp2',
              'B3': 'SSTXn2', 'B5': 'VCONN', 'B6': 'NC', 'B7': 'NC', 'B8': 'SBU2', 'B10': 'SSRXn1', 'B11': 'SSRXp1'}
    pins += [_pin('no_connect', n, name, 0, 0, 0, length=0, hide=True) for n, name in unused.items()]
    return _symbol('TYPE-C24P-GTJB040', 'J', 'USB-C plug', 'keyra:USB-C-SMD_TYPE-C-24P-GTJB-040',
                   'USB-C plug, 24 pins, straddles a 0.8 mm board (GTJB-040)',
                   [_rect(-G * 3, G * 4, G * 3, -G * 4)], pins)


# The module's pins, top to bottom on each side; None leaves a row empty.
MODULE_LEFT = [None, None, None, None, ('45', 'EN'), None, ('4', 'IO0 (BOOT)'), None,
               ('39', 'U0TXD'), ('40', 'U0RXD'), None,
               ('5', 'IO1'), ('6', 'IO2'), ('7', 'IO3'), ('8', 'IO4'), ('12', 'IO8'), ('13', 'IO9'),
               ('14', 'IO10'), ('15', 'IO11'), ('16', 'IO12'), ('17', 'IO13'), ('18', 'IO14'),
               ('19', 'IO15'), ('20', 'IO16'), ('21', 'IO17'), ('22', 'IO18')]
MODULE_RIGHT = [None, None, ('9', 'IO5'), ('10', 'IO6'), ('11', 'IO7'), None,
                ('23', 'IO19 (USB D-)'), ('24', 'IO20 (USB D+)'), None,
                ('25', 'IO21'), ('26', 'IO26'), ('28', 'IO33'), ('29', 'IO34'), ('31', 'IO35'), ('32', 'IO36'),
                ('33', 'IO37'), ('34', 'IO38'), ('35', 'IO39'), ('36', 'IO40'), ('37', 'IO41'), ('38', 'IO42'),
                ('41', 'IO45'), ('44', 'IO46'), ('27', 'IO47'), ('30', 'IO48')]
MODULE_GND = ('1', '2', '42', '43', 'GND') + tuple(str(n) for n in range(46, 61))
MODULE_W = G * 6          # half the body's width
MODULE_TOP = G * 14


def module():
    rows = max(len(MODULE_LEFT), len(MODULE_RIGHT))
    bottom = MODULE_TOP - G * (rows + 1)
    pins = [_pin('power_in', '3', '3V3', 0, MODULE_TOP + G * 2, 270)]
    for side, column in ((-1, MODULE_LEFT), (1, MODULE_RIGHT)):
        for i, row in enumerate(column):
            if row:
                kind = {'45': 'input', '39': 'output', '40': 'input'}.get(row[0], 'bidirectional')
                pins.append(_pin(kind, row[0], row[1], side * (MODULE_W + G * 2), MODULE_TOP - G * (i + 1),
                                 90 + side * 90))
    for i, n in enumerate(MODULE_GND):
        pins.append(_pin('power_in', n, 'GND', 0, bottom - G * 2, 90, hide=i > 0))
    return _symbol('ESP32-S3-MINI-1-N8', 'U', 'ESP32-S3-MINI-1-N8', 'keyra:BULETM-SMD_ESP32-S3-MINI-1-N8',
                   'ESP32-S3 module, 8 MB flash, no PSRAM, PCB antenna',
                   [_rect(-MODULE_W, MODULE_TOP, MODULE_W, bottom)], pins)


def _derived(kicad_symbol, name, value, footprint, description, numbers, extra_pins=()):
    """A KiCad symbol's drawing under a new name, its pins renumbered by `numbers`."""
    s = copy.deepcopy(kicad_symbol)
    old = s[1].split(':')[1]
    s[1] = name
    fields = {'Value': value, 'Footprint': footprint, 'Description': description, 'Datasheet': ''}
    extra = list(extra_pins)
    for e in s:
        if not (isinstance(e, list) and e):
            continue
        if e[0] == 'property' and e[1] in fields:
            e[2] = fields[e[1]]
        if e[0] == 'symbol':
            e[1] = name + e[1][len(old):]
            unit_pins = [p for p in e if isinstance(p, list) and p and p[0] == 'pin']
            for p in unit_pins:
                num = next(q for q in p if isinstance(q, list) and q and q[0] == 'number')
                num[1] = numbers.get(num[1], num[1])
            if unit_pins and extra:      # the extra pins join the unit that holds the others
                e.extend(extra)
                extra = []
    return s


def build(lib_symbol):
    """Every keyra symbol; lib_symbol(lib_id) reads one from KiCad's own libraries."""
    return [
        plug(),
        module(),
        # KiCad's SK6805 is numbered for the PLCC-4; the EC20's pads are 1 GND, 2 DIN, 3 VDD, 4 DOUT
        _derived(lib_symbol('LED:SK6805'), 'SK6805-EC20', 'SK6805-EC20',
                 'keyra:LED-SMD_4P-L2.0-W2.0-TL_J-ST2020RGB-IC-P4-5-SO1',
                 'Addressable RGB LED, 2020 package, WS2812 protocol', {'1': '4', '2': '1', '3': '2', '4': '3'}),
        # pads 1 and 4 sit diagonally, always across the switch; 2 and 3 stay open
        _derived(lib_symbol('Switch:SW_Push'), 'TS-1187A-B-A-B', 'TS-1187A',
                 'keyra:SW-SMD_4P-L5.1-W5.1-P3.70-LS6.5-TL_H1.5', 'Tactile switch, 5.1 mm, 4 pads', {'2': '4'},
                 [_pin('no_connect', n, name, 0, 0, 0, length=0, hide=True) for n, name in (('2', 'B'), ('3', 'C'))]),
        _derived(lib_symbol('74xGxx:74LVC1G125'), 'SN74LV1T125DBVR', 'SN74LV1T125',
                 'keyra:SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL',
                 'Single buffer, tri-state, TTL-level input: 3.3 V logic in, VCC swing out', {}),
    ]


def write(path, lib_symbol):
    lib = [Sym('kicad_symbol_lib'), [Sym('version'), 20241209], [Sym('generator'), 'keyra_gen'],
           [Sym('generator_version'), '1.0']] + build(lib_symbol)
    path.write_text(dump(lib) + '\n')
