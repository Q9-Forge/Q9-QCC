#!/usr/bin/env python3
"""Show ALL differences between two ROF code sections, word-wise and aligned.

rofcmp.py reports the first difference; when an instruction has a different
length, everything afterward shifts and every subsequent difference is noise.
Here difflib realigns both sequences so only the few REAL differences remain.

    rofhunks.py <r68.r> <qr68.q> [max]
"""
import difflib
import struct
import sys


def parse(path):
    d = open(path, "rb").read()
    (stat, idat, cods) = struct.unpack(">iii", d[20:32])
    p = 56
    p = d.index(b"\0", p) + 1
    (ng,) = struct.unpack(">I", d[p:p + 4])
    p += 4
    globs = []
    for _ in range(ng):
        e = d.index(b"\0", p)
        name = d[p:e].decode("latin-1")
        p = e + 1
        (t,) = struct.unpack(">H", d[p:p + 2])
        p += 2
        (a,) = struct.unpack(">I", d[p:p + 4])
        p += 4
        globs.append((name, t, a))
    code = d[p:p + cods]
    p += cods
    idata = d[p:p + idat]
    p += idat
    (nx,) = struct.unpack(">I", d[p:p + 4])
    p += 4
    ext = []
    for _ in range(nx):
        e = d.index(b"\0", p)
        name = d[p:e].decode("latin-1")
        p = e + 1
        (nr,) = struct.unpack(">I", d[p:p + 4])
        p += 4
        for _ in range(nr):
            (t, o) = struct.unpack(">HI", d[p:p + 6])
            p += 6
            ext.append((name, t, o))
    (nl,) = struct.unpack(">I", d[p:p + 4])
    p += 4
    loc = []
    for _ in range(nl):
        (t, o) = struct.unpack(">HI", d[p:p + 6])
        p += 6
        loc.append((t, o))
    return dict(stat=stat, idat=idat, cods=cods, globs=globs, code=code,
                idata=idata, ext=ext, loc=loc)


def main():
    a = parse(sys.argv[1])
    b = parse(sys.argv[2])
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 12

    for k in ("cods", "idat", "stat"):
        if a[k] != b[k]:
            print("%-6s r68 %d, qr68 %d" % (k, a[k], b[k]))

    wa = [a["code"][i:i + 2].hex() for i in range(0, len(a["code"]), 2)]
    wb = [b["code"][i:i + 2].hex() for i in range(0, len(b["code"]), 2)]
    sm = difflib.SequenceMatcher(None, wa, wb, autojunk=False)
    n = 0
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        n += 1
        if n > limit:
            print("  ... weitere unterdrueckt")
            break
        print("  %-7s r68 @0x%04x: %-28s qr68 @0x%04x: %s" %
              (tag, i1 * 2, " ".join(wa[i1:i2])[:28], j1 * 2,
               " ".join(wb[j1:j2])[:28]))
    if n == 0:
        print("  Code gleich (%d Byte)" % len(a["code"]))

    for name, key in (("Globale", "globs"), ("externe Referenzen", "ext"),
                      ("lokale Referenzen", "loc")):
        if len(a[key]) != len(b[key]):
            print("  %s: r68 %d, qr68 %d" % (name, len(a[key]), len(b[key])))
        sa, sb = set(a[key]), set(b[key])
        if sa != sb:
            only_a = sorted(sa - sb)[:4]
            only_b = sorted(sb - sa)[:4]
            print("  %s nur r68 : %s" % (name, only_a))
            print("  %s nur qr68: %s" % (name, only_b))


main()
