"""A minimal reader and writer for KiCad's s-expression files."""
import re

_TOKEN = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)


class Sym(str):
    """An unquoted atom (keyword or number), as opposed to a quoted string."""


def parse(text):
    stack, cur = [], []
    for m in _TOKEN.finditer(text):
        if m.group(1):
            stack.append(cur)
            cur = []
        elif m.group(2):
            done, cur = cur, stack.pop()
            cur.append(done)
        elif m.group(3) is not None:
            cur.append(m.group(3).replace('\\"', '"').replace('\\\\', '\\'))
        elif m.group(4):
            cur.append(Sym(m.group(4)))
    return cur[0]


def dump(x, indent=0):
    if isinstance(x, list):
        if not x:
            return '()'
        if all(not isinstance(e, list) for e in x):
            return '(' + ' '.join(dump(e) for e in x) + ')'
        pad = '  ' * (indent + 1)
        out = '('
        for i, e in enumerate(x):
            if isinstance(e, list):
                out += '\n' + pad + dump(e, indent + 1)
            else:
                out += ('' if i == 0 else ' ') + dump(e)
        return out + ')'
    if isinstance(x, Sym):
        return str(x)
    if isinstance(x, bool):
        return 'yes' if x else 'no'
    if isinstance(x, float):
        s = f'{x:.4f}'.rstrip('0').rstrip('.')
        return '0' if s == '-0' else s
    if isinstance(x, int):
        return str(x)
    return '"' + str(x).replace('\\', '\\\\').replace('"', '\\"') + '"'


def find(node, key):
    for e in node:
        if isinstance(e, list) and e and e[0] == key:
            return e
    return None


def find_all(node, key):
    return [e for e in node if isinstance(e, list) and e and e[0] == key]
