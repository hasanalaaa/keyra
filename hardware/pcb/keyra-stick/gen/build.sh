#!/usr/bin/env bash
# Rebuilds the Keyra Stick from gen/design.py: schematic, placed and routed board, checks, and the
# files JLCPCB needs. macOS paths; needs KiCad 10, Java 21 and Freerouting 2.1 (FREEROUTING=jar).
#   gen/build.sh            everything
#   gen/build.sh fab        only the checks and the fab outputs, from the board as it is
set -euo pipefail
cd "$(dirname "$0")/.."
KICAD=${KICAD:-/Applications/KiCad/KiCad.app}
CLI="$KICAD/Contents/MacOS/kicad-cli"
KPY="$KICAD/Contents/Frameworks/Python.framework/Versions/Current/bin/python3"
JAVA=${JAVA:-/opt/homebrew/opt/openjdk@21/bin/java}
work=$(mktemp -d)

if [ "${1:-}" != fab ]; then
  : "${FREEROUTING:?set FREEROUTING to the freerouting-2.1.0.jar path}"
  python3 gen/make_sch.py
  "$CLI" sch export netlist -o "$work/keyra-stick.net" keyra-stick.kicad_sch >/dev/null
  "$KPY" gen/make_pcb.py place "$work/keyra-stick.net"
  "$KPY" gen/make_pcb.py dsn "$work/keyra-stick.dsn"
  "$JAVA" -jar "$FREEROUTING" -de "$work/keyra-stick.dsn" -do "$work/keyra-stick.ses" -mp 100 \
    --gui.enabled=false >"$work/freerouting.log" 2>&1 || { tail -20 "$work/freerouting.log"; exit 1; }
  "$KPY" gen/make_pcb.py finish "$work/keyra-stick.ses"
fi

"$CLI" sch erc --severity-error --exit-code-violations -o "$work/erc.rpt" keyra-stick.kicad_sch >/dev/null \
  || { cat "$work/erc.rpt"; exit 1; }
"$CLI" pcb drc --schematic-parity --severity-error --exit-code-violations -o "$work/drc.rpt" \
  keyra-stick.kicad_pcb >/dev/null || { cat "$work/drc.rpt"; exit 1; }
echo "ERC and DRC (with schematic parity): no errors"

mkdir -p fab renders
rm -f fab/gerbers/* fab/*.zip
mkdir -p fab/gerbers
"$CLI" sch export pdf -o fab/keyra-stick-schematic.pdf keyra-stick.kicad_sch >/dev/null
"$CLI" pcb export gerbers -l F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
  -o fab/gerbers/ keyra-stick.kicad_pcb >/dev/null
"$CLI" pcb export drill --excellon-separate-th -o fab/gerbers/ keyra-stick.kicad_pcb >/dev/null
"$CLI" pcb export pos --format csv --units mm --side front --exclude-dnp -o "$work/pos.csv" keyra-stick.kicad_pcb >/dev/null
python3 gen/jlc.py "$work/pos.csv" fab
(cd fab/gerbers && zip -q -r ../keyra-stick-gerbers.zip .)
for side in top bottom; do
  "$CLI" pcb render --side "$side" --quality high --use-board-stackup-colors --floor -w 1600 -h 900 --background opaque \
    -o "renders/$side.png" keyra-stick.kicad_pcb >/dev/null
done
"$CLI" pcb render --side top --quality high --use-board-stackup-colors --floor -w 1600 -h 1000 --rotate '-40,0,-25' --zoom 0.9 \
  --background opaque -o renders/angle.png keyra-stick.kicad_pcb >/dev/null
# The LEDs' model is VRML only, so the STEP export reports it and exits non-zero; the file is complete
# otherwise. Shipped gzip-compressed (.stpZ: KiCad and FreeCAD open it directly).
rm -f fab/keyra-stick.step fab/keyra-stick.stpZ
"$CLI" pcb export step --subst-models -f -o fab/keyra-stick.step keyra-stick.kicad_pcb >/dev/null 2>&1 || true
[ -s fab/keyra-stick.step ] || { echo "STEP export failed"; exit 1; }
gzip -9 -c fab/keyra-stick.step > fab/keyra-stick.stpZ && rm fab/keyra-stick.step

# The illustrated guide (docs/): transparent renders, the schematic as SVG, laid out and printed by Chrome
mkdir -p "$work/guide"
for side in top bottom; do
  "$CLI" pcb render --side "$side" --quality high --use-board-stackup-colors -w 2400 -h 1000 --background transparent \
    -o "$work/guide/$side.png" keyra-stick.kicad_pcb >/dev/null
done
"$CLI" pcb render --side top --quality high --use-board-stackup-colors -w 2400 -h 1500 --rotate '-50,0,-30' --zoom 0.85 \
  --perspective --background transparent -o "$work/guide/hero.png" keyra-stick.kicad_pcb >/dev/null
"$CLI" pcb render --side front --quality high --use-board-stackup-colors -w 2400 -h 600 --background transparent \
  -o "$work/guide/side.png" keyra-stick.kicad_pcb >/dev/null
"$CLI" sch export svg -o "$work/guide" keyra-stick.kicad_sch >/dev/null
python3 gen/make_guide.py "$work/guide"
echo "fab/: gerbers zip, bom.csv, cpl.csv, schematic PDF, STEP; renders/: top, bottom, angle; docs/: the guide (en, ar)"
