#!/usr/bin/env python3
"""Builds firmware/components/keyra_hid/layouts/layouts.txt, the one source
table of Keyra's keyboard layouts, from what the operating systems themselves
ship:

  Windows: the .klc files kbdlayout.info generates from Microsoft's own
           keyboard DLLs (https://kbdlayout.info/<name>/download/klc).
  macOS:   the output of tools/layouts/mac_dump.swift (UCKeyTranslate on the
           layout data of the running macOS, ANSI keyboard type).

Usage:
  for k in kbdus kbduk kbdgr kbdfr kbdsp kbdit kbddv kbda1; do
    curl -sL -o klc/$k.klc https://kbdlayout.info/$k/download/klc; done
  swift tools/layouts/mac_dump.swift $(python3 tools/layouts/extract.py --mac-ids) > mac.txt
  python3 tools/layouts/extract.py klc/ mac.txt > firmware/components/keyra_hid/layouts/layouts.txt

The downloaded files are not committed (Microsoft's copyright notice); only
the key-to-character facts end up in layouts.txt.
"""
import pathlib
import sys
import unicodedata

# id, platform, display name, source, layers ("all" or "base" = base+shift only)
LAYOUTS = [
    ("us", "any", "English (US)", "klc:kbdus", "base"),
    ("uk", "win", "English (UK)", "klc:kbduk", "all"),
    ("uk-mac", "mac", "British", "mac:com.apple.keylayout.British", "all"),
    ("de", "win", "German", "klc:kbdgr", "all"),
    ("de-mac", "mac", "German", "mac:com.apple.keylayout.German", "all"),
    ("fr", "win", "French (AZERTY)", "klc:kbdfr", "all"),
    ("fr-mac", "mac", "French (AZERTY)", "mac:com.apple.keylayout.French", "all"),
    ("es", "win", "Spanish", "klc:kbdsp", "all"),
    ("es-mac", "mac", "Spanish", "mac:com.apple.keylayout.Spanish-ISO", "all"),
    ("it", "win", "Italian", "klc:kbdit", "all"),
    ("it-mac", "mac", "Italian", "mac:com.apple.keylayout.Italian-Pro", "all"),
    ("dvorak", "any", "Dvorak", "klc:kbddv", "base"),
    ("colemak", "any", "Colemak", "mac:com.apple.keylayout.Colemak", "base"),
    ("ar", "win", "Arabic (101)", "klc:kbda1", "all"),
    ("ar-mac", "mac", "Arabic", "mac:com.apple.keylayout.Arabic", "all"),
    ("ar-pc-mac", "mac", "Arabic – PC", "mac:com.apple.keylayout.ArabicPC", "all"),
]

# Windows scan code -> HID usage (Keyboard page). 0x2B is both the ANSI
# backslash and the ISO '#' key; Windows maps HID 0x31 and 0x32 to it.
SC_TO_HID = {0x29: 0x35, 0x0C: 0x2D, 0x0D: 0x2E, 0x1A: 0x2F, 0x1B: 0x30, 0x2B: 0x31, 0x27: 0x33,
             0x28: 0x34, 0x33: 0x36, 0x34: 0x37, 0x35: 0x38, 0x39: 0x2C, 0x56: 0x64}
SC_TO_HID.update({0x02 + i: 0x1E + i for i in range(10)})
for sc, letter in zip(range(0x10, 0x1A), "qwertyuiop"):
    SC_TO_HID[sc] = 0x04 + ord(letter) - ord("a")
for sc, letter in zip(range(0x1E, 0x27), "asdfghjkl"):
    SC_TO_HID[sc] = 0x04 + ord(letter) - ord("a")
for sc, letter in zip(range(0x2C, 0x33), "zxcvbnm"):
    SC_TO_HID[sc] = 0x04 + ord(letter) - ord("a")

KLC_LAYER = {"0": 0, "1": 1, "6": 2, "7": 3}  # base, Shift, AltGr (Ctrl+Alt), Shift+AltGr


def keep(cp):
    """Characters Keyra may type: one visible code point (or the space bar)."""
    if cp is None or cp > 0xFFFF:
        return False
    if cp == 0x20:
        return True
    cat = unicodedata.category(chr(cp))
    return cat[0] != "C" and cat[0] != "Z"


def klc_cell(tok):
    """-> (codepoint, dead) or None."""
    if tok in ("-1", "%%"):
        return None
    dead = tok.endswith("@")
    tok = tok.rstrip("@")
    cp = ord(tok) if len(tok) == 1 else int(tok, 16)
    return cp, dead


def read_klc(path):
    text = path.read_bytes().decode("utf-16")
    lines = text.splitlines()
    states, grid, deadspace = [], {}, {}
    section, deadkey = None, None
    for raw in lines:
        line = raw.split("//")[0].strip()
        if not line:
            continue
        head = line.split()[0]
        if head in ("SHIFTSTATE", "LAYOUT", "LIGATURE", "KEYNAME", "KEYNAME_EXT", "KEYNAME_DEAD",
                    "DESCRIPTIONS", "LANGUAGENAMES", "ENDKBD"):
            section = head
            continue
        if head == "DEADKEY":
            section, deadkey = "DEADKEY", int(line.split()[1], 16)
            continue
        if section == "SHIFTSTATE":
            states.append(head)
        elif section == "LAYOUT":
            parts = line.split()
            sc = int(parts[0], 16)
            if sc not in SC_TO_HID:
                continue
            usage = SC_TO_HID[sc]
            for state, tok in zip(states, parts[3:]):
                if state in KLC_LAYER:
                    cell = klc_cell(tok)
                    if cell:
                        grid[(usage, KLC_LAYER[state])] = cell
        elif section == "DEADKEY":
            a, b = line.split()[:2]
            if int(a, 16) == 0x20:
                deadspace[deadkey] = int(b, 16)
    out = {}
    for key, (cp, dead) in grid.items():
        if dead:
            spacing = deadspace.get(cp)
            out[key] = (spacing, True) if spacing is not None else None
        else:
            out[key] = (cp, False)
    return {k: v for k, v in out.items() if v is not None}


def read_mac(path, ident):
    out, on = {}, False
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            on = line.split()[1] == ident and "not found" not in line
            continue
        if not on:
            continue
        usage, layer, val = line.split()
        dead = val.startswith("dead:")
        val = val[5:] if dead else val
        cps = [int(u[2:], 16) for u in val.split(",") if u]
        if len(cps) == 1:
            out[(int(usage, 16), int(layer))] = (cps[0], dead)
    return out


def main():
    if sys.argv[1:] == ["--mac-ids"]:
        print(" ".join(src[4:] for *_, src, _ in LAYOUTS if src.startswith("mac:")))
        return
    klc_dir, mac_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    print("# Keyra keyboard layouts: the one source table (generated by tools/layouts/extract.py;")
    print("# see that script for where the data comes from). Edit by regenerating, not by hand.")
    print("#")
    print("# layout <id> <platform any|win|mac> <display name>")
    print("# <HID usage> <base> <Shift> <AltGr/Option> <Shift+AltGr/Option>")
    print("#   cell: U+XXXX = the key types that character; U+XXXX* = dead key, which types")
    print("#   that character when followed by Space; - = nothing Keyra may use.")
    for ident, platform, name, src, layers in LAYOUTS:
        kind, ref = src.split(":", 1)
        grid = read_klc(klc_dir / f"{ref}.klc") if kind == "klc" else read_mac(mac_path, ref)
        if not grid:
            sys.exit(f"no data for {ident} ({src})")
        print(f"\nlayout {ident} {platform} {name}")
        usages = sorted({u for u, _ in grid})
        for usage in usages:
            cells, glyphs = [], []
            for layer in range(4):
                v = grid.get((usage, layer))
                ok = v is not None and keep(v[0]) and (layers == "all" or layer < 2)
                if ok and v[0] == 0x20 and not (usage == 0x2C and layer == 0):
                    ok = False  # only the space bar's plain press types a space
                if ok and usage == 0x2C and layer > 0:
                    ok = False
                if ok:
                    cells.append(f"U+{v[0]:04X}" + ("*" if v[1] else ""))
                    glyphs.append(chr(v[0]) if v[0] != 0x20 else "␠")
                else:
                    cells.append("-")
                    glyphs.append(" ")
            if any(c != "-" for c in cells):
                print(f"{usage:02X} " + " ".join(f"{c:<8}" for c in cells).rstrip() + "  # " + " ".join(glyphs))


if __name__ == "__main__":
    main()
