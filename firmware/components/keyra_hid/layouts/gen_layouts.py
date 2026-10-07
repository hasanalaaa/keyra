#!/usr/bin/env python3
"""Turns layouts.txt (the one source table) into the C++ tables keyra_hid
types with. Run by the build (ESP-IDF and the host tests):
    python3 gen_layouts.py layouts.txt out.cpp
"""
import sys

# Keys an ANSI/ISO host may swap (macOS swaps these two on "ISO" keyboards).
MAC_SWAP_KEYS = {0x35, 0x64}


def parse(path):
    layouts = []
    for raw in open(path, encoding="utf-8"):
        line = raw.split("  #", 1)[0].strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "layout":
            if parts[2] not in ("any", "win", "mac"):
                sys.exit(f"bad platform: {line}")
            layouts.append({"id": parts[1], "platform": parts[2], "name": " ".join(parts[3:]), "keys": []})
            continue
        usage = int(parts[0], 16)
        cells = []
        for tok in parts[1:5]:
            if tok == "-":
                cells.append(None)
            else:
                dead = tok.endswith("*")
                cp = int(tok.rstrip("*")[2:], 16)
                cells.append((cp, dead))
        if len(cells) != 4:
            sys.exit(f"need 4 cells: {line}")
        layouts[-1]["keys"].append((usage, cells))
    return layouts


def glyphs(layout):
    best = {}
    for usage, cells in layout["keys"]:
        for layer, cell in enumerate(cells):
            if cell is None:
                continue
            cp, dead = cell
            swap = layout["platform"] == "mac" and usage in MAC_SWAP_KEYS
            rank = (dead, swap, layer, usage)
            if cp not in best or rank < best[cp][0]:
                best[cp] = (rank, usage, layer, dead)
    out = []
    for cp in sorted(best):
        _, usage, layer, dead = best[cp]
        flags = (1 if layer & 1 else 0) | (2 if layer & 2 else 0) | (4 if dead else 0)
        out.append((cp, usage, flags))
    return out


def ident(s):
    return s.replace("-", "_")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    layouts = parse(src)
    if layouts[0]["id"] != "us":
        sys.exit("the first layout must be us (the default)")
    ids = [l["id"] for l in layouts]
    if len(set(ids)) != len(ids):
        sys.exit("duplicate layout id")
    o = ["// Generated from layouts/layouts.txt by layouts/gen_layouts.py. Do not edit.",
         '#include "layout_data.hpp"', "", "namespace keyra::hid::data {", "namespace {", ""]
    for l in layouts:
        g = glyphs(l)
        o.append(f"constexpr Glyph kGlyphs_{ident(l['id'])}[] = {{")
        for cp, usage, flags in g:
            o.append(f"    {{0x{cp:04X}, 0x{usage:02X}, {flags}}},")
        o.append("};")
        o.append(f"constexpr KeyCell kKeys_{ident(l['id'])}[] = {{")
        for usage, cells in l["keys"]:
            cps = ", ".join(f"0x{c[0]:04X}" if c else "0" for c in cells)
            dead = sum(1 << i for i, c in enumerate(cells) if c and c[1])
            o.append(f"    {{0x{usage:02X}, {{{cps}}}, {dead}}},")
        o.append("};")
    o += ["", "}  // namespace", "", "const LayoutDef kLayouts[] = {"]
    plat = {"any": "Platform::Any", "win": "Platform::Windows", "mac": "Platform::Mac"}
    for l in layouts:
        i = ident(l["id"])
        o.append(f'    {{"{l["id"]}", "{l["name"]}", {plat[l["platform"]]}, kGlyphs_{i}, sizeof kGlyphs_{i} / sizeof kGlyphs_{i}[0], '
                 f"kKeys_{i}, sizeof kKeys_{i} / sizeof kKeys_{i}[0]}},")
    o += ["};", f"const size_t kLayoutCount = {len(layouts)};", "", "}  // namespace keyra::hid::data", ""]
    text = "\n".join(o)
    try:
        if open(dst, encoding="utf-8").read() == text:
            return
    except OSError:
        pass
    open(dst, "w", encoding="utf-8").write(text)


if __name__ == "__main__":
    main()
