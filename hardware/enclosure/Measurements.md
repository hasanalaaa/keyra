# Measurement evidence / القياسات ومصادرها

**Target correction:** the owner's photographs show a dual USB-C YD-style board, with BOOT/RST near the middle. The owner explicitly rejected the official Espressif board whose buttons sit beside its Micro-USB connectors. This package therefore uses the matching YD family and preserves the tested case footprint. It does not label YD dimensions as Espressif DevKitC-1 dimensions.

Coordinates: mm; origin at PCB front edge centre; +Y toward antenna; +X to the right in the front-component view. PCB bottom Z=4.0; assumed PCB thickness=1.6, so top Z=5.6. Component XY values below use this frame.

## Evidence

**A. OEM VCC-GND board outline and connector/header origins:** [manufacturer Altium file](https://github.com/vcc-gnd/YD-ESP32-S3/blob/main/5-public-YD-ESP32-S3-Hardware%20info/ESP32-S3-size-pcb-ad.PcbDoc), [metric drawing](https://github.com/vcc-gnd/YD-ESP32-S3/blob/main/5-public-YD-ESP32-S3-Hardware%20info/ESP32-S3-Metric.pdf). Copies in References. The Altium file contains the outline, headers, module, and USB connectors; it does **not** contain BOOT, RST or RGB components. Their circle centres were instead extracted from the manufacturer's metric PDF vector paths, calibrated in X/Y independently to its 27.94 ×57.15 mm outline. This is a derivation from its drawing, not a printed numeric coordinate callout. See OEM_Vector_Positions.json.

**B. Detailed reference model, not OEM manufacturing authority:** [shkuznetsov YD-ESP32-S3 assets](https://github.com/shkuznetsov/YD-ESP32-S3), model credited there to [David Scambell](https://grabcad.com/library/yd-esp32-s3-1). Top-side component height/envelope estimates were extracted from the STEP. BOOT/RST/RGB XY and the two USB X positions in the assembled simulation were aligned to the OEM drawing/origins; the original community STEP remains unmodified in References. Transformation: `x=-original_z`, `y=28.575-original_x`, `z=5.6+original_y`. Pin headers were removed to match the photographs. This model's PCB thickness is about 1.0 mm, so it is not treated as proof of the owner's PCB thickness. The simulation uses a separate 1.6 mm PCB envelope.

**C. Current project / owner evidence:** the owner reported that the earlier 74.2 × 32.64 mm direct-port case and PCB fit were excellent. New photographs show no fitted headers, BOOT on the right below RST, RGB on the left toward USB, and the connected port labelled USB on the back. These confirm layout and previous fit; they do not establish precise switch height or travel.

**D. Electrical functions:** [OEM README](https://github.com/vcc-gnd/YD-ESP32-S3). BOOT=GPIO0; native USB uses GPIO19/20; RGB=WS2812 on GPIO48 for the documented YD board. Firmware must match the actual revision. The official Espressif v1.1 instead uses GPIO38 and different mechanics.

**E. Rejected board comparison only:** [Espressif v1.1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html) and [layout PDF](https://dl.espressif.com/dl/schematics/PCB_ESP32-S3-DevKitC-1_V1.1_20220429.pdf). Its dimensions and switch datasheet were **not substituted for the photographed board**. NTC013's 2.5 mm height is not used here.

## Board-related numbers actually used

| Parameter / feature | Number, mm unless noted | Source / confidence |
|---|---:|---|
| PCB width | 27.94 | A: 1100 mil × 0.0254 |
| PCB body length | 57.15 | A: 2250 mil × 0.0254 |
| Overall including antenna extension | 63.39 | A drawing / previous fit, approximately 6.24 beyond body |
| Dedicated mounting holes | none identified | A; header holes are electrical holes, not screw mounts |
| Header hole pitch / rows | 2.54 / 25.40 | A; 22 positions each row |
| Header centre X | ±12.70 | A: ±500 mil |
| First pin Y from front | 1.905 | A: 75 mil; reference alignment |
| Hole nominal envelope diameter | 1.0 | reference-model / legacy estimate, not used for fastening |
| USB1 / USB2 X origins | +5.969 / -5.969 | A Components6: (5080.5−4845.5) and (4610.5−4845.5) mil |
| Both USB footprint Y origins | 5.080 | A: (2104.2−1904.2) mil; footprint origins, not mouth centres |
| Native USB side | X negative / left | C back USB label and connected port; configurable |
| Connector design envelope | 9.0 wide × 3.2 high × 7.6 deep | B / conservative previous fit; legs not counted in shell height |
| Connector envelope centre Y / Z | 3.0 / 7.2 | legacy fitting envelope; community shells occupy approximately Y -0.634..6.166, Z 4.587..8.810 |
| BOOT actuator centre XY | (5.157, 28.222) | A PDF vector path985 centre; B supplies only its Z envelope |
| RST centre XY | (5.157, 33.475) | A PDF vector path987 centre; covered and no actuator supplied |
| BOOT to RST centre distance | 5.253 | derived from A vectors; not measured on owner's board |
| BOOT/RST actuator circle diameter | approximately2.0 | A vector circle diameter calibrated to PCB width |
| Visual switch body partition in SCAD | 1.55 high | display-only assumption; never used for printed-part fit |
| Switch envelope | about 4.2 × 3.2; terminal envelope about 5 × 3 | B/photo/engineering envelope |
| Switch top above PCB | 1.905 | B; **estimate, not an OEM switch specification** |
| Switch top Z in nominal assembly | 7.505 | derived from assumed 5.6 PCB top plus B height |
| Assumed switch stroke | 0.200 | engineering assumption; actual manufacturer/travel unidentified |
| RGB centre XY | (-5.151, 18.377) | A PDF vector path986 centre; B supplies height |
| RGB envelope / top above PCB | 5.0 × 5.0 / 1.7 | B; model height about 1.7 |
| Module envelope | 18.0 × 25.5 × 3.2 | A footprint length/width; B conservative overall top height |
| Shielded module portion | 18.0 × 19.5; centre Y 48.9 | previous A/B envelope; source assembly uses conservative box |
| Antenna extension | 18.0 × 6.24 × 1.0; centre Y 60.27 | A/B envelope, conservative |
| Tallest top-side envelope without headers | 3.21 above PCB / Z 8.81 | B connector shell; design module envelope 3.2 |
| PCB thickness | 1.600 | C previous fit assumption; **not physically measured** |
| PCB bottom / top Z | 4.000 / 5.600 | design seats plus thickness assumption |
| Optional header envelope | 3.0 above / 8.0 below PCB, row block 2.54 × 55.89 | B-informed conservative assumption; no OEM exact header specified; default OFF |

The PcbDoc's default layer stack is not a fabrication specification. The community STEP and OEM outline are not sufficient to certify the exact tactile switch or height on an unmarked third-party PCB. These limitations are explicit rather than inventing an OEM number.

## Refined design values - chosen dimensions, not OEM measurements

The board table above is unchanged. Owner evidence: the preceding corrected print fits and actuates BOOT successfully; empty shells still retain a visible gap. No caliper measurement or exact tactile-switch identification was supplied. The following are engineering choices for this revision.

| Feature | Value, mm unless noted |
|---|---:|
| Cable exterior L x W x H | 74.2 x32.64 x18.2 |
| Dongle exterior, default parameters | 87.634 x32.64 x18.2 |
| Wall / floor / roof | 1.6 each |
| Corners / softened edge radius | 6.0 /2.0 |
| Board XY clearance / vertical clearance | 0.20 /0.10 |
| PCB seat Z / guide bottom Z | 4.0 /9.6 |
| Retainer-tip receiver depth relief | 0.35 |
| USB receiver extra depth / seam gap | 0.30 /0.00 |
| Snap retaining-face lift before contact | about0.0867, actual solid sweep in Closure_Checks.json |
| Frame ear vertical / Y clearance | 0.15 /0.20 |
| Four stationary frame locators, thickness / height / run | 0.8 /3.0 /4.0 |
| Locator root thickness / nominal radial preload | 1.2 /0.04 |
| Two lid Y stops: width xdepth / lower Z / gap | 10.0 x1.6 /10.8 /0.20 |
| Button OD / flange OD / flange thickness | 16.0 /18.0 /1.7 |
| D shank diameter / flat X | 10.0 /-4.0 |
| Captive socket bore / outer diameter | 10.8 /13.0 |
| Fixed-frame free-motion bore | 13.8 |
| Four cap feet, thickness / width / free span | 0.8 /2.4 /5.45 |
| Nominal foot radial compression / insertion compression | 0.10 /0.27 |
| Socket bead projection / nominal axial lift | 0.15 /0.05 |
| BOOT rest gap / after maximal PCB lift | 0.70 /0.60, conditional on assumed switch height |
| Total press / assumed switch compression / stop Z | 0.90 /0.20 /14.0 |
| Return-arm thickness / width / run length | 0.8 /2.2 /20.0 |
| Return preload / full deflection | 0.15 /1.05 |
| Button-to-optics gap, radial | 0.60 |
| Ring ID / OD / flange OD | 17.2 /20.4 /22.8 |
| Ring flange thickness / radial seat clearance | 1.2 /0.20 |
| Ring axial seat gap / bearing Z | 0.00 /15.4 |
| Light collector diameter / nominal LED gap | 6.4 /1.0 |
| Native cable throat / counterbore | 9.8 x4.4 /11.8 x6.4, unchanged |
| Male dongle exit | 9.0 x3.4 |
| Logo centre / height / stroke / depth | (0,53) /14 /1.0 /0.50 |

### Dongle envelope and sources

[Delock60035 official product](https://www.delock.com/produkt/60035/merkmale.html) publishes overall27.6 x10.7 x6.0 mm. These three dimensions are the default example, not a measured owner adapter. Metal tip length6.5, insertion5.0, outside projection5.8 and socket-mouth Y=-0.634 are explicitly assumed/model-derived. Housing extends nominally Y=-16.734..-2.134; tip span Y=-23.234..4.366. The body bay is14.4 x8.4; its pedestal supports the default6.0 mm-high body atZ4.2. Different body height requires changing the source, even if its width fits. No claim of universal adapter compatibility or CC/data electrical qualification is made.

The ring is unchanged from the successful print. Only its receiving surfaces change. The logo remains the exact open path in References/Keyra_Contour.svg, debossed0.50 mm, 1.0 mm nominal stroke (polygon minimum about0.999 mm), not mirrored or closed. The three stops still transfer the final press into the rigid frame and case seats. RST is fully covered.

## Manufacturing and analysis references

- [Prusa modelling guide](https://help.prusa3d.com/article/modeling-with-3d-printing-in-mind_164135): moving clearances need printer/material qualification; there is no universal exact fit.
- [Elephant foot compensation](https://help.prusa3d.com/article/elephant-foot-compensation_114487): first-layer widening can interfere with assembly.
- [Prusament PETG data sheet](https://prusament.com/wp-content/uploads/2022/10/PETG_Prusament_TDS_2021_10_EN.pdf): surrogate elastic modulus1.5GPa. This is not the owner's unidentified PETG. Poisson ratio0.38 is an engineering assumption; sensitivity uses1.0–2.5GPa. No S–N fatigue or creep model was available for the actual print.
