#!/usr/bin/env python3
"""Zeigt die Bytes einer Modulstelle und dekodiert die paar 68k-Befehle,
die in einem Systemaufruf-Rumpf vorkommen (moves, trap, rts, bcc).

    dis.py <modul> <adresse_hex> <laenge>

Die Adresse ist die aus l68s Symbolkarte (-s), also der Abstand im
GELADENEN Modul -- im Datei-Abbild liegt der Code an derselben Stelle,
weil der Modulkopf mitgezaehlt wird.
"""
import sys
import struct

d = open(sys.argv[1], "rb").read()
at = int(sys.argv[2], 16)
n = int(sys.argv[3]) if len(sys.argv) > 3 else 40

REG = ["d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
       "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"]


def ea(mode, reg, w, extra):
    """Nur die Formen, die hier vorkommen."""
    if mode == 0:
        return "d%d" % reg, 0
    if mode == 1:
        return "a%d" % reg, 0
    if mode == 2:
        return "(a%d)" % reg, 0
    if mode == 5:
        return "%d(a%d)" % (struct.unpack(">h", extra[:2])[0], reg), 2
    if mode == 7 and reg == 4:
        if w == 4:
            return "#$%x" % struct.unpack(">I", extra[:4])[0], 4
        return "#$%x" % struct.unpack(">H", extra[:2])[0], 2
    return "<%d,%d>" % (mode, reg), 0


i = at
end = at + n
while i < end:
    op = struct.unpack_from(">H", d, i)[0]
    raw = i
    txt = None
    if op == 0x4E75:
        txt, sz = "rts", 2
    elif op == 0x4E40:
        code = struct.unpack_from(">H", d, i + 2)[0]
        txt, sz = "trap #0 / dc.w $%02x   <== Systemaufruf" % code, 4
    elif (op & 0xF000) in (0x2000, 0x3000):      # move.l / move.w
        size = "l" if (op & 0xF000) == 0x2000 else "w"
        smode, sreg = (op >> 3) & 7, op & 7
        dmode, dreg = (op >> 6) & 7, (op >> 9) & 7
        src, k = ea(smode, sreg, 4 if size == "l" else 2, d[i + 2:i + 10])
        dst, k2 = ea(dmode, dreg, 4 if size == "l" else 2, d[i + 2 + k:i + 12])
        mnem = "movea" if dmode == 1 else "move"
        txt, sz = "%s.%s %s,%s" % (mnem, size, src, dst), 2 + k + k2
    elif (op & 0xFF00) == 0x6400:
        off = op & 0xFF
        txt, sz = "bcc +%d" % off, 2
    elif (op & 0xFF00) == 0x6500:
        off = op & 0xFF
        txt, sz = "bcs +%d" % off, 2
    elif (op & 0xF1FF) == 0x7000 or (op & 0xF100) == 0x7000:
        txt, sz = "moveq #%d,d%d" % (op & 0xFF, (op >> 9) & 7), 2
    if txt is None:
        txt, sz = "?", 2
    print("  $%04x: %-14s %s" % (raw, d[raw:raw + sz].hex(" "), txt))
    i += sz
