# Keyra Stick: a purpose-built PCB (design proposal)

> **Status: proposal only.** Nothing here has been laid out, built or tested. No schematic or
> PCB files exist yet. Part numbers, prices and fees were checked against distributor pages in
> October 2026 and will drift; re-check every line before ordering. Items still marked
> **open** in the text must be resolved before layout.

A USB-C stick that plugs straight into a computer: the ESP32-S3 module at the far end, one
large button in the middle, a ring of eight addressable LEDs around the button, and a USB-C
plug at the near end. It runs the same Keyra firmware as the DevKitC-1, with a board profile
of its own (see [Firmware mapping](#firmware-mapping)).

## 1. Block diagram

```
 USB-C plug (direct-attach device, USB 2.0 full speed)
   VBUS ──┬───────────────────────────────┬──────────────► LED ring VDD (5 V)
          │                               │                 8 × WS2812B-2020, 100 nF each
          │                        74AHCT1G125 (5 V)
   ESD: USBLC6-2SC6 on VBUS, D+, D-       │  A ◄── GPIO6 (3.3 V data)
          │                               └─ Y ──330 Ω──► LED1 DIN → LED2 … → LED8
          ▼
   AP2112K-3.3 LDO (600 mA) ── 3V3 ──► ESP32-S3-MINI-1-N8 (8 MB flash, PCB antenna)
                                        ├─ GPIO19/20 ◄─22 Ω─► D-/D+ (plug A7/A6)
                                        ├─ GPIO5  ◄── Button (5.1 × 5.1 tactile to GND)
                                        ├─ GPIO0  ◄── BOOT pinhole switch + 10 kΩ pull-up
                                        ├─ EN     ◄── 10 kΩ / 1 µF RC + RST test pad
                                        └─ GPIO43/44 (U0TXD/U0RXD) → test pads
   CC (plug A5) ── 5.1 kΩ Rd to GND        Antenna end: board edge, keep-out on every layer
```

## 2. Module: ESP32-S3-MINI-1-N8 (recommended)

| | |
|---|---|
| Module | **ESP32-S3-MINI-1-N8** (ESP32-S3FN8 inside: 8 MB quad flash in package, no PSRAM), LCSC **C2913206** |
| Size | 15.4 × 20.5 × 2.4 mm (datasheet Table 1-1; §10.1: 20.5 ± 0.2 × 15.4 ± 0.2 × 2.4 ± 0.15 mm) |
| Exposed GPIOs | IO0 to IO21, IO26, IO33 to IO42, IO45 to IO48, TXD0/RXD0 (datasheet Table 3-1) |
| Supply | 3.0 to 3.6 V; the external supply must deliver **at least 0.5 A** (Table 6-2); Wi-Fi TX peaks at **355 mA** (Table 6-4, 802.11b 1 Mbps, 20.5 dBm) |

Why the module and not a chip-down ESP32-S3FN8:

- Keyra's partition table is an 8 MB layout and the firmware boots without PSRAM
  ([docs/HARDWARE.md](../../docs/HARDWARE.md)), so the N8 fits as is.
- The module carries the crystal, the RF matching network and a characterised PCB antenna, and
  it holds the radio certifications. A chip-down QFN56 design means a 40 MHz crystal, a tuned
  CLC matching network, an own antenna design, an RF test setup and its own certification.
  That is a later step, worth it only at volume; this document does not cover it.
- The -N4R2 variant is not an option: 4 MB flash is below Keyra's minimum.

## 3. USB-C plug and protection

**Plug.** The stick needs a **plug** (male), not a receptacle. Candidates are board-edge,
straddle-mount USB 2.0 plugs:

| Part | Type | Price (DigiKey, Oct 2026) | Note |
|---|---|---|---|
| Adam Tech **USB-C20-PLUG-R-BK-HT-PP** | USB 2.0 plug, 24 pos., board edge, straddle mount | US$0.67 (1), 0.572 (10), 0.486 (100) | First choice: USB 2.0 is all Keyra needs. |
| Molex **1054440011** (series 105444) | USB 3.2 plug, board edge, straddle mount | €2.72 (1), 2.315 (10), 1.968 (100) | Alternative; the USB 3 contacts stay unconnected. |

**Open:** no LCSC part for a USB-C *plug* was found (LCSC's 16-pin parts are receptacles), so
JLCPCB would have to source it through global sourcing or take it as a consigned part, or it
is hand-soldered. **Open:** a straddle-mount plug fixes the PCB thickness. The 1.0 mm stack
below is a placeholder until the chosen plug's drawing confirms it (some need 0.8 mm).

**Wiring** (direct-attach device, plug side):

- A5 **CC** → **5.1 kΩ to GND** (Rd: tells the host this is a sink so it switches VBUS on).
  The plug has no second CC; B5 (VCONN) stays unconnected.
- A6 **D+** → GPIO20, A7 **D-** → GPIO19, each through **22 Ω** close to the module.
  Espressif: GPIO19/20 are USB D-/D+, reserve series resistors (22/33 Ω) and unpopulated
  capacitors to ground near the chip
  ([schematic checklist, USB](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)).
- VBUS, GND to all plug VBUS/GND contacts; shell to GND.

**ESD:** **USBLC6-2SC6** (ST, SOT-23-6, LCSC **C7519**), clamping D+, D- and VBUS, placed
right behind the plug, before the series resistors. Espressif asks for an ESD diode at the
main power entrance and ESD parts close to the power input
([PCB layout guide](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html)).

**Routing:** D+/D- as a parallel, equal-length pair over a continuous ground reference, ground
return vias at any via pair (layout guide). The guide's 90 Ω ± 10 % differential target is
hard to reach on a 1.0 mm two-layer board; the pair is short (under 15 mm) and full-speed USB
(12 Mbit/s) tolerates it. Use coplanar ground on both sides of the pair and the fab's
impedance calculator to get as close as practical.

## 4. Power: 3.3 V LDO

| | |
|---|---|
| Regulator | **AP2112K-3.3TRG1** (Diodes Inc., SOT-23-5, 600 mA, 250 mV dropout at 600 mA, enable pin), LCSC **C51118** |
| Input | VBUS after the ESD clamp; **10 µF + 100 nF** at the input |
| Output | **10 µF + 100 nF** at the module's 3V3 pin, plus 100 nF at each remaining supply pin |
| EN of the LDO | tied to VBUS |

600 mA covers the module's 355 mA TX peak with margin and meets the 0.5 A minimum. The LED ring
is **not** on this rail (section 6). Espressif: 3.3 V supply, output current no less than
500 mA, at least 10 µF at the main power entrance; main power traces at least 25 mil wide,
other power traces 10 mil.

**Chip enable:** EN through **10 kΩ to 3V3 and 1 µF to GND** (Espressif's suggested
starting values for power-up timing), and a test pad on EN for a manual reset.

## 5. The button, and why it is not on GPIO0

The DevKitC-1 uses its BOOT button (GPIO0) as Keyra's button. GPIO0 is a strapping pin: held
low at reset, the chip enters ROM download mode, which is why the firmware refuses to restart
while GPIO0 is held. On the stick the main button moves to an ordinary GPIO:

- **Main button: GPIO5**, tactile switch to GND, active low, internal pull-up (optionally a
  10 kΩ external pull-up footprint, unpopulated). Pressing it during a reset or while plugging
  in does nothing to the boot mode.
- Avoided pins: the strapping pins **GPIO0, GPIO3, GPIO45, GPIO46** (GPIO0 weak pull-up,
  GPIO45/46 weak pull-down, GPIO3 floating; GPIO0 and GPIO46 select the boot mode, GPIO45 the
  flash voltage); GPIO19/20 (USB); GPIO39 to 42 (JTAG); GPIO43/44 (UART0, kept for the console).
  Strapping values need 3 ms hold time after EN rises (MINI-1 datasheet).
- Part: **TS-1187A-B-A-B** (XKB, 5.1 × 5.1 mm SMD, 1.5 mm high, 1.6 N, 100 000 cycles), LCSC
  **C318884** (JLCPCB Basic part). Its round actuator sits in the centre of the LED ring; the
  enclosure adds a larger key cap over it.

### Separate BOOT path for recovery

With the main button off GPIO0, ROM download needs its own path:

- **BOOT switch** on GPIO0 to GND: a second TS-1187A on the **bottom side** behind a pinhole,
  or just two pads to short with tweezers. **10 kΩ pull-up** on GPIO0 and **no capacitor**
  (Espressif: a large capacitor on GPIO0 can force download mode).
- Recovery: hold BOOT, plug the stick in, release; the ROM's USB serial port appears and
  `idf.py -p <PORT> flash` works as described in [HARDWARE.md](../../docs/HARDWARE.md).
- Day-to-day updates stay over the air (**Device → Firmware update**); dev builds keep the
  1200-baud reboot hook. Do not burn eFuses that disable USB download on this board: the
  pinhole is the only way back in.

## 6. LED ring: 8 × WS2812B-2020 around the button

| | |
|---|---|
| LED | **WS2812B-2020** (Worldsemi, 2.2 × 2.0 × 0.84 mm), LCSC **C965555**, JLCPCB lists it at 5 V. Alternative: **SK6805-EC20** (OPSCO, 2 × 2 mm), LCSC **C2890036**, JLCPCB lists 3.5 to 5.5 V. Both are Extended parts. |
| Geometry | 8 LEDs, 45° apart, on a circle of about 13 mm diameter centred on the button; chained DOUT → DIN around the ring |
| Supply | **VBUS (5 V)**, not the 3.3 V LDO; 100 nF at each LED, 10 µF bulk at the ring |
| Data | GPIO6 → **SN74AHCT1G125DBVR** (TI, SOT-23-5, 4.5 to 5.5 V supply, TTL input thresholds), LCSC **C7484**, powered from VBUS, OE tied to GND → **330 Ω** series → LED1 DIN |

Why the buffer: on 5 V the LEDs expect a data high level near 0.7 × VDD, which 3.3 V logic does
not reliably reach. The AHCT buffer accepts 3.3 V input and drives a full 5 V swing. Running
the ring from 3.3 V instead would load the LDO (and the SK6805's 3.5 V minimum is above 3.3 V).

**Current budget (open):** neither LED's per-channel current was confirmed from its datasheet.
Measure one ring at full white before release. The firmware already scales every frame by a
brightness setting (`keyra_io` `brightness()`), so a board profile can cap it; with eight LEDs
the cap should keep the ring well under 200 mA so the whole stick stays inside a USB 2.0
port's 500 mA.

**Gating (optional, not in this BOM):** idle WS2812-class LEDs draw a small quiescent current
each. If that matters, add an active-high load switch on the ring's VDD and map its enable to
`CONFIG_KEYRA_LED_POWER_GPIO`, which the firmware drives high at start-up.

## 7. Board outline and layout

Proposed outline: **about 20 × 54 mm**, corners r = 2 mm, components on the **top side only**
(single-sided assembly is cheaper), except the optional BOOT switch.

```
  y (mm)   0 ───────── 8 ────── 14 ─────────── 30 ── 33.5 ──────────────── 54
          │ USB-C plug │ESD,LDO,│ button + LED ring│     │ ESP32-S3-MINI-1-N8  │
          │ (straddle, │ buffer │ (centre y ≈ 22)  │     │ antenna end at the  │
          │ overhangs  │        │                  │     │ board edge ─────────┤
          │ the edge)  │        │                  │     │ (keep-out)          │
```

- **Antenna** at the end opposite the plug, so the computer's chassis is as far from it as the
  stick allows.
- Espressif's rules for a module's PCB antenna
  ([PCB layout guide, module positioning](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html)):
  place the antenna **outside the base board**; if that is not possible, put the feed point as
  close to the board edge as possible and **cut the board away on both sides of and below the
  antenna**; never centre the module with clearance hollowed out on all four sides; inside the
  housing keep **at least 15 mm clearance in all directions**. The MINI-1 datasheet marks the
  module's antenna area (Figures 3-1 and 11-1) and refers to these guidelines for the
  dimensions.
- For this stick: end the board at the module's antenna-area line (antenna overhanging), or
  keep the board under the antenna area free of copper, parts, traces and vias on **both**
  layers. **Open:** take the exact antenna-area length from the MINI-1 land-pattern drawing.
  No metal (screws, inserts, foil labels) over the antenna end in the enclosure.
- Continuous ground on the bottom layer under the module, the USB pair and the LDO; stitch the
  top-layer ground pours to it.
- Mounting holes are left to the enclosure design; if used, two NPTH M2 holes in the button
  section, not near the antenna.

### Layer stack

| | |
|---|---|
| Layers | 2 |
| Thickness | **1.0 mm FR-4** (placeholder until the plug drawing confirms it, see section 3) |
| Copper | 1 oz outer |
| Finish | ENIG (flat pads for the 2020 LEDs and the module's castellated/LGA pads) |
| Mask / silk | black mask, white silk (Graphite & Ivory) |
| Min. track / space | 0.15 / 0.15 mm (standard JLCPCB two-layer capability) |

## 8. Test points

Bottom-side pads, 1.0 mm, on a 2.54 mm grid where possible so a pogo-pin jig can reach them:

| TP | Net | Why |
|---|---|---|
| TP1 | VBUS | supply check |
| TP2 | 3V3 | LDO output |
| TP3 | GND | reference (two pads) |
| TP4 | EN | manual reset |
| TP5 | GPIO0 | download mode without the pinhole switch |
| TP6 / TP7 | GPIO43 U0TXD / GPIO44 U0RXD | UART console (the stick has no USB-UART bridge) |
| TP8 / TP9 | D+ / D- | USB debugging |
| TP10 | LED_DIN (after the buffer) | ring signal check |
| TP11 | BTN (GPIO5) | button check |

## 9. Bill of materials

Per board. LCSC numbers were checked on LCSC/JLCPCB pages; prices are from the same pages or
DigiKey and are approximate. Passive part numbers are not pinned here: use JLCPCB **Basic**
parts of the listed value and size at order time.

| Ref | Qty | Part | Package | LCSC | Approx. unit price |
|---|---|---|---|---|---|
| U1 | 1 | ESP32-S3-MINI-1-N8 | module | C2913206 | US$4.51 (1); about 3.2 at 1300+ |
| J1 | 1 | Adam Tech USB-C20-PLUG-R-BK-HT-PP (or Molex 1054440011) | straddle-mount plug | none found (DigiKey) | US$0.67 (1), 0.57 (10), 0.49 (100) |
| U2 | 1 | USBLC6-2SC6 | SOT-23-6 | C7519 | about US$0.11 (5+) |
| U3 | 1 | AP2112K-3.3TRG1 | SOT-23-5 | C51118 | about US$0.13 (5+), 0.07 (6000+) |
| U4 | 1 | SN74AHCT1G125DBVR | SOT-23-5 | C7484 | about US$0.05 at reel quantity; more at 10 |
| D1–D8 | 8 | WS2812B-2020 (or SK6805-EC20) | 2020 | C965555 (C2890036) | **open**: price not shown on JLCPCB's page |
| SW1 | 1 | TS-1187A-B-A-B (main button) | 5.1 × 5.1 SMD | C318884 | about US$0.01–0.02 |
| SW2 | 0–1 | TS-1187A-B-A-B (BOOT, optional, bottom) | 5.1 × 5.1 SMD | C318884 | about US$0.01–0.02 |
| R1 | 1 | 5.1 kΩ 1 % (CC Rd) | 0402 | Basic | <US$0.01 |
| R2, R3 | 2 | 22 Ω (D+/D- series) | 0402 | Basic | <US$0.01 |
| R4, R5 | 2 | 10 kΩ (EN, GPIO0 pull-ups) | 0402 | Basic | <US$0.01 |
| R6 | 1 | 330 Ω (LED data series) | 0402 | Basic | <US$0.01 |
| R7 | 0 | 10 kΩ (button pull-up, DNP) | 0402 | Basic | — |
| C1–C3 | 3 | 10 µF (LDO in, LDO out, ring bulk) | 0603 | Basic | <US$0.01 |
| C4 | 1 | 1 µF (EN delay) | 0402 | Basic | <US$0.01 |
| C5–C16 | 12 | 100 nF (LDO, module pins, buffer, 8 LEDs) | 0402 | Basic | <US$0.01 |
| C17, C18 | 0 | D+/D- caps to GND (DNP, per Espressif) | 0402 | — | — |

**Approximate cost per stick** (parts + bare PCB, assembly fees excluded):

| | 10 pcs | 100 pcs |
|---|---|---|
| Module | 4.5 | ~3.8 (estimate) |
| Plug | 0.57 | 0.49 |
| ICs (ESD, LDO, buffer) | ~0.35 | ~0.25 |
| 8 LEDs | ~0.8 (estimate) | ~0.5 (estimate) |
| Switches + passives | ~0.15 | ~0.1 |
| 2-layer 1.0 mm PCB, ENIG | ~1.5–3 | ~0.5–1 |
| **Total** | **≈ US$8–9.5** | **≈ US$5.5–6** |

These are estimates for planning only (the LED and PCB lines are not quotes). JLCPCB assembly
adds a per-order setup and stencil fee and a loading fee for each distinct **Extended** part
(the module, ESD, LDO, buffer and LEDs are Extended; the switch and the passives are Basic), plus
the plug's sourcing or consignment cost. Get the real number from a JLCPCB quote with the
BOM and CPL.

## 10. Assembly via JLCPCB

1. Export Gerbers, the BOM (JLCPCB columns: Comment, Designator, Footprint, LCSC Part #) and the
   CPL/pick-and-place file from the EDA tool.
2. PCB: 2 layers, 1.0 mm (or what the plug requires), ENIG, black mask.
3. PCBA: top side. Check every part's rotation in the CPL preview, especially the eight
   LEDs (each turned 45° further around the ring) and the module's pin-1 marker.
4. The plug: global sourcing or consignment; if neither works, order the boards without it
   and hand-solder it (straddle mount: both rows of contacts, then the shell tabs).
5. The bottom BOOT switch, if fitted, is a second (bottom-side) assembly pass or hand-soldered.

Bring-up order: VBUS and 3V3 on the test points with no load surprises → the host enumerates
the ROM USB port with BOOT held → flash the stick's image → button and ring test.

## Firmware mapping

A board profile `firmware/boards/keyra-stick.defaults` (not created yet; the board matrix in
[HARDWARE.md](../../docs/HARDWARE.md) explains profiles) would carry:

```
CONFIG_KEYRA_BOARD_ID="keyra-stick"
CONFIG_KEYRA_BUTTON_GPIO=5
CONFIG_KEYRA_BUTTON_ACTIVE_LOW=y
CONFIG_KEYRA_LED_WS2812=y
CONFIG_KEYRA_LED_GPIO=6
CONFIG_KEYRA_LED_POWER_GPIO=-1
```

**LED count: firmware change needed.** There is no `KEYRA_LED_COUNT` option today. The driver
creates the strip with `max_leds = 1` and only sets pixel 0
(`firmware/components/keyra_io/src/io.cpp`), so on this board only the first LED of the ring
would light. Supporting the ring needs a new Kconfig option (for example `KEYRA_LED_COUNT`,
default 1) that sets `max_leds` and writes the frame colour to every pixel, plus a brightness
cap in the profile once the ring's current is measured. That change is not part of this
proposal.

No PSRAM option is set: the N8 has none, and the firmware boots without it.

## Sources

- ESP32-S3 Hardware Design Guidelines, PCB layout (module positioning, antenna placement and
  15 mm clearance, USB routing, power traces, ESD placement):
  https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html
- ESP32-S3 Hardware Design Guidelines, schematic checklist (500 mA supply, 10 µF at the power
  entrance, EN RC 10 kΩ / 1 µF, strapping pins, USB series resistors and capacitors):
  https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html
- ESP32-S3-MINI-1/MINI-1U datasheet (dimensions, pin table, 0.5 A supply, 355 mA TX peak,
  strapping timing, antenna area figures):
  https://documentation.espressif.com/esp32-s3-mini-1_mini-1u_datasheet_en.html
- LCSC/JLCPCB part pages: C2913206, C7519, C51118, C7484, C318884, C965555, C2890036
  (https://jlcpcb.com/partdetail/C… and https://lcsc.com/product-detail/…).
- DigiKey: Adam Tech USB-C20-PLUG-R-BK-HT-PP
  (https://www.digikey.com/en/products/detail/adam-tech/USB-C20-PLUG-R-BK-HT-PP/25701105),
  Molex 1054440011 (https://www.digikey.com/en/products/detail/molex/1054440011/6173183).
- Keyra firmware: `firmware/components/keyra_io/Kconfig`, `firmware/components/keyra_io/src/io.cpp`,
  `firmware/boards/`.

## Not verified

- No schematic capture, layout, DRC, prototype or measurement has been done. No KiCad files:
  `kicad-cli` is not available here to check them, so none were written.
- Straddle-mount plug PCB thickness and exact footprint (datasheet drawing not read).
- MINI-1 antenna-area length for the board cut-out.
- Per-channel LED current and JLCPCB prices for the LEDs; WS2812B-2020 supply range (JLCPCB
  lists only "5 V").
- Mouser part numbers: not listed; search by manufacturer part number.
- Passive LCSC numbers: not pinned; pick Basic parts at order time.
