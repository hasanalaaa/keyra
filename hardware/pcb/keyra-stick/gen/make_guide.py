"""Writes the Keyra Stick guide, docs/keyra-stick-guide-{en,ar}.pdf.

  python3 gen/make_guide.py WORK_DIR
WORK_DIR holds what build.sh rendered for it: top.png, bottom.png, hero.png, side.png (transparent
KiCad renders) and keyra-stick.svg (the schematic). Every number, part and position comes from
design.py; the callouts land on the renders by measuring where the board sits in each picture.
Google Chrome prints the pages (CHROME overrides its path).
"""
import html
import re
import os
import struct
import subprocess
import sys
import zlib
from collections import defaultdict
from pathlib import Path

import design as D

HERE = Path(__file__).resolve().parent.parent
CHROME = os.environ.get('CHROME', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
PLUG_OVERHANG = 9.9   # mm the plug's shell reaches past the tongue's tip, measured on the 3D render


def alpha_box(path, threshold=250):
    """(width, height, x0, y0, x1, y1): the opaque part of an RGBA PNG; the renders' soft shadows stay out."""
    data, pos, idat = path.read_bytes(), 8, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, colour = struct.unpack('>IIBB', body[:10])
            if (depth, colour) != (8, 6):
                raise SystemExit(f'{path}: expected an 8-bit RGBA PNG')
        elif kind == b'IDAT':
            idat += body
    raw, stride = zlib.decompress(idat), w * 4
    prev, i, xs, ys = bytearray(stride), 0, [], []
    for y in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):        # undo PNG's row filters
            a = line[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        hit = [x for x, v in enumerate(line[3::4]) if v >= threshold]
        if hit:
            ys.append(y)
            xs += [hit[0], hit[-1]]
        prev = line
    return w, h, min(xs), min(ys), max(xs), max(ys)


class View:
    """A top or bottom render with board millimetres mapped to its pixels. The renders are orthographic and
    the board's full width and its antenna end are the edges of the opaque area."""

    def __init__(self, path, mirrored):
        self.path = path
        self.w, self.h, self.x0, self.y0, self.x1, self.y1 = alpha_box(path)
        self.scale = (self.y1 - self.y0) / D.BOARD_W
        self.mirrored = mirrored          # seen from below, the antenna end is on the left

    def px(self, x, y):
        dx = (D.BOARD_L - x) * self.scale
        return (self.x0 + dx if self.mirrored else self.x1 - dx), self.y0 + (y + D.BOARD_W / 2) * self.scale


def at(ref):
    x, y, _rot, _side = next(p for p in D.PARTS if p[0] == ref)[6]
    return x, y


# Callouts: board point (mm), badge above (-1) or below (+1) the board, badge nudge along the board (mm).
TOP_POINTS = {1: (-4.5, 0.0, -1, 0), 2: (*at('U2'), 1, -3), 3: (*at('U3'), -1, -1.5), 4: (12.8, 6.0, 1, 2),
              5: (*at('U4'), 1, 5), 6: (*D.RING[:2], -1, 0), 7: (*at('D3'), -1, 0), 8: (39.4, 0.0, 1, 0),
              9: (50.6, 0.0, -1, 1.5), 10: (44.6, -8.5, -1, -1.5)}
BOTTOM_POINTS = {'A': (44.04, -4.6, -1, 0), 'B': (*at('JP1'), 1, 0), 'C': (D.MARK[0], D.MARK[1], 1, 0),
                 'D': (24.5, 5.4, 1, 0)}


ARABIC = re.compile('[\u0600-\u06ff]')
LATIN_RUN = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.\-/:+ ]*[A-Za-z0-9]|[A-Za-z]')


def esc(s):
    """Escaped text; inside Arabic, each run of Latin words (file names, menu items) keeps its own order."""
    s = str(s)
    if not ARABIC.search(s):
        return html.escape(s)
    out, last = [], 0
    for m in LATIN_RUN.finditer(s):
        if re.search('[A-Za-z]', m.group()):
            out += [html.escape(s[last:m.start()]), f'<bdi dir="ltr" class="lt">{html.escape(m.group())}</bdi>']
            last = m.end()
    return ''.join(out) + html.escape(s[last:])


def ltr(s):
    return f'<bdi dir="ltr">{esc(s)}</bdi>'


def callout_svg(view, points):
    """The render cut to the board, with numbered badges and leader lines, in the picture's own pixels."""
    k = view.scale
    pad = k * 6.5
    x0, y0, w, h = view.x0 - k, view.y0 - pad, view.x1 - view.x0 + 2 * k, view.y1 - view.y0 + 2 * pad
    r = k * 1.5
    out = [f'<svg class="callouts" viewBox="{x0:.0f} {y0:.0f} {w:.0f} {h:.0f}" xmlns="http://www.w3.org/2000/svg">',
           f'<image href="{view.path.name}" width="{view.w}" height="{view.h}"/>']
    for key, (x, y, side, nudge) in points.items():
        px, py = view.px(x, y)
        bx, _ = view.px(x + nudge, y)
        by = view.y0 - pad * 0.55 if side < 0 else view.y1 + pad * 0.55
        out += [f'<line x1="{px:.0f}" y1="{py:.0f}" x2="{bx:.0f}" y2="{by:.0f}" stroke="#e8b931" stroke-width="{k * 0.2:.1f}"/>',
                f'<circle cx="{px:.0f}" cy="{py:.0f}" r="{k * 0.5:.1f}" fill="#e8b931" stroke="#141414" stroke-width="{k * 0.12:.1f}"/>',
                f'<circle cx="{bx:.0f}" cy="{by:.0f}" r="{r:.0f}" fill="#e8b931"/>',
                f'<text x="{bx:.0f}" y="{by + r * 0.4:.0f}" text-anchor="middle" font-size="{r * 1.15:.0f}" '
                f'font-weight="800" font-family="-apple-system, Helvetica, Arial" fill="#141414">{key}</text>']
    out.append('</svg>')
    return ''.join(out)


def legend(items):
    rows = []
    for key, (name, refs, text) in items.items():
        ref = f' <span class="refs">{ltr(refs)}</span>' if refs else ''
        rows.append(f'<li><span class="badge">{key}</span><div><b>{esc(name)}</b>{ref}<p>{esc(text)}</p></div></li>')
    return '<ol class="legend">' + ''.join(rows) + '</ol>'


def dimension_svg(t):
    """The outline from design.py with its main dimensions, in mm."""
    W, L, P = D.BOARD_W, D.BOARD_L, PLUG_OVERHANG
    poly = ' '.join(f'{x:.2f},{y:.2f}' for x, y in D.outline_points())
    d = t['dims']

    def hdim(xa, xb, y, label, above=True):
        return (f'<line x1="{xa}" y1="{y}" x2="{xb}" y2="{y}" class="dl" marker-start="url(#a)" marker-end="url(#a)"/>'
                f'<text x="{(xa + xb) / 2:.2f}" y="{y - 1.0 if above else y + 2.3:.2f}" class="dt">{label}</text>')

    return ''.join([
        f'<svg class="dims" viewBox="{-P - 2} -16 {L + P + 9} 35" xmlns="http://www.w3.org/2000/svg">',
        '<defs><marker id="a" viewBox="0 0 6 6" refX="3" refY="3" markerWidth="2.4" markerHeight="2.4" '
        'orient="auto-start-reverse"><path d="M0,0 L6,3 L0,6 z" fill="#c9a227"/></marker></defs>',
        f'<rect x="{-P}" y="-4.2" width="{P}" height="8.4" rx="1.6" fill="#d9dde3" stroke="#9aa3ad" stroke-width="0.15"/>',
        f'<polygon points="{poly}" fill="#2a2724" stroke="#0e0d0c" stroke-width="0.2"/>',
        # the module's shield and its antenna area, as the render shows them
        f'<rect x="32.3" y="-7.6" width="14.2" height="15.2" rx="0.4" fill="#e9ecef"/>',
        f'<rect x="47.0" y="-{W / 2 - 0.4}" width="{L - 47.4:.2f}" height="{W - 0.8}" fill="#5a5a5a"/>',
        f'<circle cx="{D.RING[0]}" cy="{D.RING[1]}" r="{D.RING_R + 1.3}" fill="none" stroke="#e8b931" '
        f'stroke-width="0.45" stroke-dasharray="1 0.9"/>',
        f'<circle cx="{D.RING[0]}" cy="{D.RING[1]}" r="2.6" fill="#f2f2f2"/>',
        f'<line x1="0" y1="-12.6" x2="0" y2="-3.5" class="ext"/><line x1="{L}" y1="-12.6" x2="{L}" y2="-{W / 2}" class="ext"/>',
        hdim(0, L, -12, f'{L:.1f} mm · {esc(d["length"])}'),
        f'<line x1="{L}" y1="-{W / 2}" x2="{L + 2.4}" y2="-{W / 2}" class="ext"/>'
        f'<line x1="{L}" y1="{W / 2}" x2="{L + 2.4}" y2="{W / 2}" class="ext"/>',
        f'<line x1="{L + 1.6}" y1="-{W / 2}" x2="{L + 1.6}" y2="{W / 2}" class="dl" marker-start="url(#a)" marker-end="url(#a)"/>',
        f'<text x="{L + 2.4}" y="0.7" class="dt" text-anchor="start">{W:.1f}</text>',
        f'<line x1="{-P}" y1="13" x2="{-P}" y2="4.2" class="ext"/><line x1="0" y1="16.2" x2="0" y2="3.5" class="ext"/>',
        hdim(-P, 0, 12.4, esc(d['plug']), above=False),
        f'<line x1="{D.TONGUE_L}" y1="{D.TONGUE_W / 2}" x2="{D.TONGUE_L}" y2="16.2" class="ext"/>',
        hdim(0, D.TONGUE_L, 15.6, f'{D.TONGUE_L:.1f} · {esc(d["tongue"])}', above=False),
        f'<line x1="{D.FLARE_END}" y1="{W / 2}" x2="{D.FLARE_END}" y2="13" class="ext"/>',
        hdim(D.TONGUE_L, D.FLARE_END, 12.4, f'{D.FLARE_END - D.TONGUE_L:.1f}', above=False),
        '</svg>'])


def block_svg(t):
    b = t['blocks']

    def box(x, y, w, h, label, cls='blk'):
        return (f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="2.5" class="{cls}"/>'
                f'<text x="{x + w / 2}" y="{y + h / 2 + 1.4}" class="bt{" light" if "mcu" in cls else ""}">{esc(label)}</text>')

    def arrow(pts, label='', lx=0, ly=0, cls='ar'):
        p = ' '.join(f'{x},{y}' for x, y in pts)
        lab = f'<text x="{lx}" y="{ly}" class="al">{esc(label)}</text>' if label else ''
        return f'<polyline points="{p}" class="{cls}" marker-end="url(#h)"/>{lab}'

    return ''.join([
        '<svg class="blocks" viewBox="0 0 200 92" xmlns="http://www.w3.org/2000/svg">',
        '<defs><marker id="h" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="4.5" markerHeight="4.5" orient="auto">'
        '<path d="M0,0 L8,4 L0,8 z" fill="context-stroke"/></marker></defs>',
        box(2, 40, 30, 13, b['usb'], 'blk dark'), box(46, 40, 32, 13, b['esd']), box(46, 66, 32, 13, b['ldo']),
        box(98, 33, 44, 32, b['mcu'], 'blk mcu'), box(46, 12, 32, 13, b['sw']), box(98, 4, 44, 13, b['buf']),
        box(158, 4, 40, 13, b['ring'], 'blk ring'), box(158, 33, 40, 11, b['btn']), box(158, 50, 40, 11, b['boot']),
        box(158, 67, 40, 11, b['tp']),
        arrow([(32, 46.5), (45, 46.5)]), arrow([(78, 46.5), (97, 46.5)], b['data'], 87.5, 44.2),
        arrow([(62, 53), (62, 65)], b['v5'], 67.5, 60.5, 'ar pw'),
        arrow([(78, 72.5), (120, 72.5), (120, 66)], b['v33'], 99, 77.2, 'ar pw'),
        arrow([(54, 40), (54, 26)], b['v5'], 49, 34, 'ar pw'),
        arrow([(78, 18.5), (152, 18.5), (152, 10.5), (157, 10.5)], 'LED_VDD', 115, 22.6, 'ar pw'),
        arrow([(106, 33), (106, 29), (70, 29), (70, 26)], 'IO7', 88, 27.5),
        arrow([(126, 33), (126, 18)], 'IO6', 130.5, 27),
        arrow([(142, 8.5), (157, 8.5)]),
        arrow([(158, 38.5), (143, 38.5)], 'IO5', 150.5, 36.6), arrow([(158, 55.5), (143, 55.5)], 'IO0', 150.5, 53.6),
        arrow([(143, 61), (150, 61), (150, 72.5), (157, 72.5)], b['console'], 146, 78.5),
        '</svg>'])


def stack_svg(t):
    colours = ['#c08040', '#7c6a4e', '#c9a227', '#c08040']
    rows = []
    for i, (name, what) in enumerate(t['stack']):
        y = 4 + i * 12
        rows.append(f'<rect x="2" y="{y}" width="70" height="3" rx="0.3" fill="{colours[i]}"/>')
        if i < 3:
            rows.append(f'<rect x="2" y="{y + 3}" width="70" height="9" fill="#3b3a36"/>')
        rows.append(f'<text x="78" y="{y + 1.6}" class="sl"><tspan class="sn">{esc(name)}</tspan>  {esc(what)}</text>')
    return f'<svg class="stack" viewBox="0 0 170 44" xmlns="http://www.w3.org/2000/svg">{"".join(rows)}</svg>'


def package(fp):
    name = fp.split(':')[1]
    for key, short in (('0402', '0402'), ('0603', '0603'), ('SOT-23-6', 'SOT-23-6'), ('SOT-23-5', 'SOT-23-5'),
                       ('SOT-25', 'SOT-23-5'), ('SOT-23', 'SOT-23'), ('ESP32', '15.4 × 20.5 mm'),
                       ('USB-C', 'USB-C'), ('LED', '2 × 2 mm'), ('SW-SMD', '5.1 × 5.1 mm')):
        if key in name:
            return short
    return name


def parts_table(t):
    generic = {'Device:R': ('Resistor', 'مقاومة'), 'Device:C': ('Capacitor', 'مكثف')}
    groups = defaultdict(list)
    for ref, sym, value, fp, lcsc, _nets, _pos in D.PARTS:
        if lcsc:
            groups[(lcsc, value, sym, fp)].append(ref)

    def number(r):
        return r.rstrip('0123456789'), int(r[len(r.rstrip('0123456789')):])

    def order(item):
        return 'JUQSDRC'.find(item[1][0][0]), -len(item[1]), number(min(item[1], key=number))

    rows = []
    for (lcsc, value, sym, fp), refs in sorted(groups.items(), key=order):
        refs.sort(key=number)
        if sym in generic:
            name, val = generic[sym][t['lang'] == 'ar'], value
        else:
            name, val = value, ''
        name_html = esc(name) if sym in generic and t['lang'] == 'ar' else ltr(name)
        rows.append(f'<tr><td>{ltr(", ".join(refs))}</td><td>{name_html}</td><td>{ltr(val)}</td>'
                    f'<td>{ltr(package(fp))}</td><td>{ltr(lcsc)}</td><td>{esc(t["roles"][lcsc])}</td></tr>')
    head = ''.join(f'<th>{esc(c)}</th>' for c in t['parts_cols'])
    count = sum(len(r) for r in groups.values())
    return f'<table class="parts"><thead><tr>{head}</tr></thead><tbody>{"".join(rows)}</tbody></table>', count


CSS = (HERE / 'gen' / 'guide.css')


def page(t, n, body, cls=''):
    foot = '' if cls else f'<div class="foot"><span>{esc(t["page"])}</span><span>{n}</span></div>'
    return f'<section class="page {cls}">{body}{foot}</section>'


def kicker(t, i):
    return f'<div class="kicker">{i + 1} · {esc(t["contents"][i])}</div>'


def guide(t, views):
    contents = ''.join(f'<span data-n="{i + 1}">{esc(c)}</span>' for i, c in enumerate(t['contents']))
    stats = ''.join(f'<div class="stat"><b>{ltr(v)} {esc(u)}</b><span>{esc(k)}</span></div>' for v, u, k in t['stats'])
    side = ''.join(f'<span><b>{ltr(v)}</b>{esc(k)}</span>' for v, k in t['side_notes'])
    spec = ''.join(f'<tr><td>{esc(k)}</td><td>{esc(v)}</td></tr>' for k, v in t['spec'])
    how = ''.join(f'<div><b>{esc(k)}</b><p>{esc(v)}</p></div>' for k, v in t['how'])
    pins = ''.join(f'<tr><td>{ltr(k)}</td><td>{esc(v)}</td></tr>' for k, v in t['pins'])
    table, count = parts_table(t)
    files = ''.join(f'<div><code>{esc(f)}</code><span>{esc(w)}</span></div>' for f, w in t['files'])
    steps = ''.join(f'<li>{esc(s)}</li>' for s in t['steps'])
    opts = ''.join(f'<tr><td>{ltr(a)}</td><td>{ltr(b)}</td><td>{esc(c)}</td></tr>' for a, b, c in t['options'])
    opt_head = ''.join(f'<th>{esc(c)}</th>' for c in t['opt_cols'])
    checks = ''.join(f'<li>{esc(c)}</li>' for c in t['checks'])
    power = ''.join(f'<li>{esc(s)}</li>' for s in t['power_steps'])
    trouble = ''.join(f'<tr><td>{esc(k)}</td><td>{esc(v)}</td></tr>' for k, v in t['trouble'])
    pages = [
        page(t, 1, f'<div class="brand">KEYRA · OPEN HARDWARE</div><div class="status">{esc(t["status"])}</div>'
                   f'<h1>{esc(t["title"])}</h1><div class="tag">{esc(t["tagline"])}</div>'
                   f'<div class="hero">{views["hero"]}</div><div class="stats">{stats}</div>'
                   f'<div class="contents"><b>{esc(t["contents_title"])}</b>{contents}</div>'
                   f'<div class="lic">{esc(t["license"])}</div>', 'cover'),
        page(t, 2, f'{kicker(t, 0)}<h1>{esc(t["top_title"])}</h1><p class="intro">{esc(t["top_intro"])}</p>'
                   f'{callout_svg(views["top"], TOP_POINTS)}{legend(t["top"])}'),
        page(t, 3, f'{kicker(t, 0)}<h1>{esc(t["bottom_title"])}</h1><p class="intro">{esc(t["bottom_intro"])}</p>'
                   f'{callout_svg(views["bottom"], BOTTOM_POINTS)}{legend(t["bottom"])}'
                   f'<h2 class="gap">{esc(t["side_title"])}</h2>{views["side"]}<div class="sidenotes">{side}</div>'),
        page(t, 4, f'{kicker(t, 1)}<h1>{esc(t["size_title"])}</h1><p class="intro">{esc(t["size_intro"])}</p>'
                   f'{dimension_svg(t)}<h2>{esc(t["stack_title"])}</h2>{stack_svg(t)}'
                   f'<h2>{esc(t["spec_title"])}</h2><table class="kv">{spec}</table>'),
        page(t, 5, f'{kicker(t, 2)}<h1>{esc(t["how_title"])}</h1><p class="intro">{esc(t["how_intro"])}</p>'
                   f'{block_svg(t)}<div class="how">{how}</div>'
                   f'<h2>{esc(t["pins_title"])}</h2><table class="pins kv">{pins}</table>'),
        f'<section class="page sheetpage"><img src="keyra-stick.svg"></section>',
        page(t, 7, f'{kicker(t, 4)}<h1>{esc(t["parts_title"])} · {count}</h1><p class="intro">{esc(t["parts_intro"])}</p>'
                   f'{table}<div class="note">{esc(t["cost"])}</div>'),
        page(t, 8, f'{kicker(t, 5)}<h1>{esc(t["order_title"])}</h1><p class="intro">{esc(t["order_intro"])}</p>'
                   f'<div class="files">{files}</div><ol class="steps">{steps}</ol>'
                   f'<table class="opts"><thead><tr>{opt_head}</tr></thead><tbody>{opts}</tbody></table>'
                   f'<h2>{esc(t["remark_title"])}</h2><div class="remark">{esc(t["remark"])}</div>'
                   f'<h2>{esc(t["check_title"])}</h2><ul class="checks">{checks}</ul>'),
        page(t, 9, f'{kicker(t, 6)}<h1>{esc(t["power_title"])}</h1><ol class="steps">{power}</ol>'
                   f'<h2>{esc(t["trouble_title"])}</h2><table class="kv">{trouble}</table>'
                   f'<h2>{esc(t["change_title"])}</h2><p class="intro">{esc(t["change"])}</p>'),
    ]
    return (f'<!doctype html><html lang="{t["lang"]}" dir="{t["dir"]}"><head><meta charset="utf-8">'
            f'<title>Keyra Stick</title><style>{CSS.read_text()}</style></head><body>{"".join(pages)}</body></html>')


def crop(work, name, threshold, pad):
    """The render name.png cut to its opaque part, as inline SVG (Chrome prints it sharp)."""
    w, h, x0, y0, x1, y1 = alpha_box(work / f'{name}.png', threshold)
    return (f'<svg class="r-{name}" xmlns="http://www.w3.org/2000/svg" '
            f'viewBox="{x0 - pad} {y0 - pad} {x1 - x0 + 2 * pad} {y1 - y0 + 2 * pad}">'
            f'<image href="{name}.png" width="{w}" height="{h}"/></svg>')


def main():
    from guide_text import T
    work = Path(sys.argv[1]).resolve()
    views = {'top': View(work / 'top.png', mirrored=False), 'bottom': View(work / 'bottom.png', mirrored=True)}
    views['hero'], views['side'] = crop(work, 'hero', 20, 30), crop(work, 'side', 20, 10)
    (HERE / 'docs').mkdir(exist_ok=True)
    for lang in ('en', 'ar'):
        src = work / f'guide-{lang}.html'
        src.write_text(guide(T[lang], views))
        pdf = HERE / 'docs' / f'keyra-stick-guide-{lang}.pdf'
        subprocess.run([CHROME, '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
                        '--allow-file-access-from-files', f'--print-to-pdf={pdf}', src.as_uri()],
                       check=True, capture_output=True)
        print(f'wrote {pdf.relative_to(HERE)}')


if __name__ == '__main__':
    main()
