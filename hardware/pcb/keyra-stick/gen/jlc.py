"""JLCPCB's assembly files: bom.csv from design.py, cpl.csv from KiCad's position export.

  python3 gen/jlc.py POS_CSV OUT_DIR
Footprints from lib/keyra.pretty come from LCSC's own data, so JLCPCB's placement preview needs
no rotation corrections for them; KiCad's 0402/0603 passives have no polarity.
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from design import PARTS  # noqa: E402

pos_csv, out = Path(sys.argv[1]), Path(sys.argv[2])

groups = defaultdict(list)
for ref, _sym, value, fp, lcsc, _nets, _place in PARTS:
    if lcsc:
        groups[(value, fp.split(':')[1], lcsc)].append(ref)
with open(out / 'bom.csv', 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
    for (value, fp, lcsc), refs in sorted(groups.items(), key=lambda g: g[1][0]):
        w.writerow([value, ','.join(sorted(refs, key=lambda r: (r.rstrip('0123456789'), int(r.lstrip('ABCDEFGHIJKLMNOPQRSTUVWXYZ'))))), fp, lcsc])

assembled = {p[0] for p in PARTS if p[4]}
with open(pos_csv) as f, open(out / 'cpl.csv', 'w', newline='') as g:
    w = csv.writer(g)
    w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
    rows = 0
    for r in csv.DictReader(f):
        if r['Ref'] in assembled:
            w.writerow([r['Ref'], f"{float(r['PosX']):.4f}mm", f"{float(r['PosY']):.4f}mm",
                        'Top' if r['Side'] == 'top' else 'Bottom', f"{float(r['Rot']) % 360:.0f}"])
            rows += 1
if rows != len(assembled):
    raise SystemExit(f'cpl.csv has {rows} parts, the BOM {len(assembled)}')
