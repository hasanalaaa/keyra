"""Keyra Stick: the one description of the board.

make_sch.py draws the schematic from it and make_pcb.py places the board from it, so the two
cannot disagree. Coordinates on the board are millimetres, x along the stick from the tip of the
USB-C plug's tongue (x = 0) to the antenna end, y across it (0 = centre line, KiCad's y points
down). Every part with an LCSC number is assembled by JLCPCB; test points and the BOOT jumper are
bare copper on the bottom.
"""
import math as _m

BOARD_W = 18.5          # stick width: room beside the module for its EN and 3V3 parts
TONGUE_NECK = (6.10, 0.90)   # plug datasheet DWG001-A401: width, length of the chamfered tip
TONGUE_W = 7.90         # tongue width under the plug housing
TONGUE_L = 6.30         # tongue length from the tip
FLARE_END = 9.30        # where the board reaches full width
RING = (24.5, 0.0)      # centre of the button and the LED ring
RING_R = 6.5            # LED ring radius
CAP_R = 5.0             # the LEDs' 100 nF, inside the ring
MARK = (35.9, 0.0, 5.0)  # the gold Contour on the back, under the module: centre x, y and size
MODULE_X = 40.30        # module footprint origin (its antenna points to +x)
BOARD_L = 53.12         # the module's antenna edge is the board's end

# (ref, symbol, value, footprint, lcsc, {pad: net}, (x, y, rotation, side))
# Symbols "keyra:*" come from lib/keyra.kicad_sym (LCSC/EasyEDA data, the same pin numbers as the
# footprints), the rest from KiCad's own libraries. Pads not listed stay unconnected.
PLUG = {p: 'GND' for p in ('A1', 'A12', 'B1', 'B12', '25', '26')}
PLUG.update({p: 'VBUS' for p in ('A4', 'A9', 'B4', 'B9')})
PLUG.update({'A5': 'CC', 'A6': 'USB_DP_C', 'A7': 'USB_DM_C'})

PARTS = [
    # USB-C plug (straddles the 0.8 mm board's tongue), protection and the 3.3 V regulator
    ('J1', 'keyra:TYPE-C24P-GTJB040', 'USB-C plug', 'keyra:USB-C-SMD_TYPE-C-24P-GTJB-040', 'C3151751',
     PLUG, (1.98, 0.0, -90, 'F')),
    ('R1', 'Device:R', '5.1k', 'Resistor_SMD:R_0402_1005Metric', 'C25905',
     {'1': 'CC', '2': 'GND'}, (6.9, 2.7, 90, 'F')),
    ('U2', 'Power_Protection:USBLC6-2SC6', 'USBLC6-2SC6', 'keyra:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL', 'C7519',
     {'1': 'USB_DP_C', '6': 'USB_DP_C', '3': 'USB_DM_C', '4': 'USB_DM_C', '2': 'GND', '5': 'VBUS'},
     (9.4, 0.0, 90, 'F')),
    ('R2', 'Device:R', '22', 'Resistor_SMD:R_0402_1005Metric', 'C25092',
     {'1': 'USB_DP_C', '2': 'USB_DP'}, (12.6, 0.55, 0, 'F')),
    ('R3', 'Device:R', '22', 'Resistor_SMD:R_0402_1005Metric', 'C25092',
     {'1': 'USB_DM_C', '2': 'USB_DM'}, (12.6, -0.55, 0, 'F')),
    ('U3', 'Regulator_Linear:AP2112K-3.3', 'AP2112K-3.3', 'keyra:SOT-25-5_L2.9-W1.6-P0.95-LS2.8-BL', 'C51118',
     {'1': 'VBUS', '2': 'GND', '3': 'VBUS', '5': '+3V3'}, (12.0, -5.2, 0, 'F')),
    ('C1', 'Device:C', '4.7u', 'Capacitor_SMD:C_0603_1608Metric', 'C19666',
     {'1': 'VBUS', '2': 'GND'}, (9.0, -5.4, 90, 'F')),
    ('C2', 'Device:C', '100n', 'Capacitor_SMD:C_0402_1005Metric', 'C1525',
     {'1': 'VBUS', '2': 'GND'}, (11.4, -7.7, 0, 'F')),
    ('C3', 'Device:C', '10u', 'Capacitor_SMD:C_0603_1608Metric', 'C19702',
     {'1': '+3V3', '2': 'GND'}, (14.8, -5.2, 90, 'F')),
    # LED data: 3.3 V GPIO6 to a 5 V swing for the ring (TTL-level input, rated down to 1.6 V, so a
    # weak port's 4.4 V VBUS is still in range)
    ('U4', 'keyra:SN74LV1T125DBVR', 'SN74LV1T125', 'keyra:SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL', 'C473338',
     {'1': 'GND', '2': 'LED_3V3', '3': 'GND', '4': 'LED_5V', '5': 'LED_VDD'}, (15.4, 3.4, 0, 'F')),
    ('C6', 'Device:C', '100n', 'Capacitor_SMD:C_0402_1005Metric', 'C1525',
     {'1': 'LED_VDD', '2': 'GND'}, (15.4, 1.1, 0, 'F')),
    # The ring's supply, switched (GPIO7): no LED current while Keyra sleeps or before the firmware
    # runs, and the ring's capacitance stays off VBUS at plug-in. C16 holds Q1 off while VBUS rises;
    # (R8 || R9) x C16 makes its turn-on take about 1 ms. On: Vgs = -4.5 V (R8 100k / R9 10k); off:
    # even 1 uA of Q2 leakage moves Q1's gate only 0.1 V. All basic parts.
    ('Q1', 'Transistor_FET:AO3401A', 'AO3401A', 'keyra:SOT-23_L2.9-W1.3-P1.90-LS2.4-BR', 'C15127',
     {'1': 'LED_PG', '2': 'VBUS', '3': 'LED_VDD'}, (11.0, 5.3, 0, 'F')),
    ('R8', 'Device:R', '100k', 'Resistor_SMD:R_0402_1005Metric', 'C25741',
     {'1': 'VBUS', '2': 'LED_PG'}, (8.5, 2.9, 90, 'F')),
    ('C16', 'Device:C', '100n', 'Capacitor_SMD:C_0402_1005Metric', 'C1525',
     {'1': 'VBUS', '2': 'LED_PG'}, (9.6, 2.9, 90, 'F')),
    ('R9', 'Device:R', '10k', 'Resistor_SMD:R_0402_1005Metric', 'C25744',
     {'1': 'LED_PG', '2': 'LED_PGN'}, (10.4, 7.6, 0, 'F')),
    ('Q2', 'Transistor_FET:2N7002', '2N7002', 'keyra:SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR', 'C8545',
     {'1': 'LED_PWR_EN', '2': 'GND', '3': 'LED_PGN'}, (14.6, 6.8, 0, 'F')),
    ('R10', 'Device:R', '100k', 'Resistor_SMD:R_0402_1005Metric', 'C25741',
     {'1': 'LED_PWR_EN', '2': 'GND'}, (17.4, 7.4, 0, 'F')),
    ('R6', 'Device:R', '330', 'Resistor_SMD:R_0402_1005Metric', 'C25104',
     {'1': 'LED_5V', '2': 'LED_D1'}, (17.3, 5.9, 0, 'F')),
    # The button: Keyra's confirmation, on GPIO5 (never a strapping pin)
    ('SW1', 'keyra:TS-1187A-B-A-B', 'TS-1187A', 'keyra:SW-SMD_4P-L5.1-W5.1-P3.70-LS6.5-TL_H1.5', 'C318884',
     # diagonal pads: always the two sides of the switch, whichever pairs are joined inside;
     # the pull-up is the chip's own (the firmware enables it for an active-low button)
     {'1': 'BTN', '4': 'GND'}, (RING[0], RING[1], 0, 'F')),
    # The module (antenna at the far end, flush with the board edge)
    ('U1', 'keyra:ESP32-S3-MINI-1-N8', 'ESP32-S3-MINI-1-N8', 'keyra:BULETM-SMD_ESP32-S3-MINI-1-N8', 'C2913206',
     dict({'GND': 'GND', '1': 'GND', '2': 'GND', '42': 'GND', '43': 'GND',
           '3': '+3V3', '4': 'BOOT', '9': 'BTN', '10': 'LED_3V3', '11': 'LED_PWR_EN',
           '23': 'USB_DM', '24': 'USB_DP',
           '39': 'U0TXD', '40': 'U0RXD', '45': 'EN'},
          **{str(n): 'GND' for n in range(46, 61)}),
     (MODULE_X, 0.0, -90, 'F')),
    # Beside the module's 3V3 (pin 3) and EN (pin 45), in the strip between module and board edge
    # (Espressif: 10 uF + 100 nF at the module's supply; the CHIP_PU RC close to its pin).
    ('C17', 'Device:C', '4.7u', 'Capacitor_SMD:C_0603_1608Metric', 'C19666',
     {'1': '+3V3', '2': 'GND'}, (43.3, -8.5, 180, 'F')),
    ('C4', 'Device:C', '100n', 'Capacitor_SMD:C_0402_1005Metric', 'C1525',
     {'1': '+3V3', '2': 'GND'}, (45.9, -8.5, 0, 'F')),
    ('C5', 'Device:C', '1u', 'Capacitor_SMD:C_0402_1005Metric', 'C52923',
     {'1': 'EN', '2': 'GND'}, (46.0, 8.5, 0, 'F')),
    ('R4', 'Device:R', '10k', 'Resistor_SMD:R_0402_1005Metric', 'C25744',
     {'1': '+3V3', '2': 'EN'}, (44.0, 8.5, 0, 'F')),
    ('C15', 'Device:C', '4.7u', 'Capacitor_SMD:C_0603_1608Metric', 'C19666',
     {'1': 'LED_VDD', '2': 'GND'}, (18.6, -7.5, 0, 'F')),
    # Fiducials for the assembly machine's camera
    ('FID1', 'Mechanical:Fiducial', 'Fiducial', 'Fiducial:Fiducial_0.75mm_Mask1.5mm', None, {}, (15.4, -7.8, 0, 'F')),
    ('FID2', 'Mechanical:Fiducial', 'Fiducial', 'Fiducial:Fiducial_0.75mm_Mask1.5mm', None, {}, (20.7, 7.75, 0, 'F')),
    # GPIO0 keeps the chip's own pull-up (ESP32-S3 datasheet, strapping pins: weak pull-up, so
    # SPI boot by default); shorting JP1 at plug-in selects download mode.
    # Bottom: recovery and bring-up, bare copper (not assembled)
    ('JP1', 'Jumper:SolderJumper_2_Open', 'BOOT', 'Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm', None,
     {'1': 'BOOT', '2': 'GND'}, (44.04, 0.0, 90, 'B')),
]

TEST_POINTS = ['VBUS', '+3V3', 'GND', 'EN', 'BOOT', 'U0TXD', 'U0RXD']
TP_AT = [(41.5, -4.6), (44.04, -4.6), (46.58, -4.6), (41.5, 4.6), (44.04, 4.6), (46.58, 4.6), (46.58, 0.0)]
for _i, _net in enumerate(TEST_POINTS):
    PARTS.append((f'TP{_i + 1}', 'Connector:TestPoint', _net, 'TestPoint:TestPoint_Pad_D1.0mm', None,
                  # under the module's far half, on a 2.54 mm grid for a pogo-pin jig
                  {'1': _net}, TP_AT[_i] + (0, 'B')))

# The ring: eight LEDs 45° apart, DOUT of one into DIN of the next.
for _i in range(8):
    _a = 180 - 45 * _i                    # D1 nearest the plug, then round the ring
    _din = f'LED_D{_i + 1}'
    PARTS.append((f'D{_i + 1}', 'keyra:SK6805-EC20', 'SK6805-EC20',
                  'keyra:LED-SMD_4P-L2.0-W2.0-TL_J-ST2020RGB-IC-P4-5-SO1', 'C2890036',
                  # the last LED's DOUT stays open
                  dict({'1': 'GND', '2': _din, '3': 'LED_VDD'}, **({'4': f'LED_D{_i + 2}'} if _i < 7 else {})),
                  (round(RING[0] + RING_R * _m.cos(_m.radians(_a)), 3),
                   round(RING[1] - RING_R * _m.sin(_m.radians(_a)), 3), _a - 90, 'F')))
    # its 100 nF inside the ring, between the button and the LEDs; the data chain runs outside
    _c = _a - 22.5
    PARTS.append((f'C{7 + _i}', 'Device:C', '100n', 'Capacitor_SMD:C_0402_1005Metric', 'C1525',
                  {'1': 'LED_VDD', '2': 'GND'},
                  (round(RING[0] + CAP_R * _m.cos(_m.radians(_c)), 3),
                   round(RING[1] - CAP_R * _m.sin(_m.radians(_c)), 3), _c - 90, 'F')))


def outline_points():
    """The board edge, clockwise from the tip: the plug's tongue (DWG001-A401), a flare, the stick."""
    nw, nl = TONGUE_NECK
    hw, tw, W = nw / 2, TONGUE_W / 2, BOARD_W / 2
    c = 0.20                                    # chamfer on the tip's corners
    return [(0, -hw + c), (c, -hw), (nl, -hw), (nl, -tw), (TONGUE_L, -tw), (FLARE_END, -W),
            (BOARD_L, -W), (BOARD_L, W), (FLARE_END, W), (TONGUE_L, tw), (nl, tw), (nl, hw),
            (c, hw), (0, hw - c)]


NETS = sorted({n for p in PARTS for n in p[5].values()})
