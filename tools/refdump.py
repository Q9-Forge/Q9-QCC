#!/usr/bin/env python3
"""Zeigt zu einem externen Namen alle Referenzen eines ROF -- Typwort,
Offset und die Bytes, die dort im Code stehen.

    refdump.py <datei.r> <name> [<name> ...]

Das Typwort ist bei Q9-qr68 dokumentiert:
  Bit 5     die Referenz LIEGT im Code (sonst in den Daten)
  Bit 3..4  Umfang: 01 = 1, 10 = 2, 11 = 4 Byte
  Bit 2     das ZIEL ist Code
  Bit 6/7   abziehen / relativ
"""
import sys
import struct


def cstr(d, off):
    e = d.index(b"\0", off)
    return d[off:e].decode("latin-1"), e + 1


def umfang(typ):
    return {1: 1, 2: 2, 3: 4}.get((typ >> 3) & 3, 0)


def erklaere(typ):
    t = []
    t.append("im Code" if typ & 0x20 else "in den Daten")
    t.append("%d Byte" % umfang(typ))
    if typ & 4:
        t.append("Ziel=Code")
    if typ & 0x40:
        t.append("abziehen")
    if typ & 0x80:
        t.append("relativ")
    return ", ".join(t)


def main():
    path = sys.argv[1]
    wanted = set(sys.argv[2:])
    d = open(path, "rb").read()
    statsz, idatsz, codsz = struct.unpack_from(">III", d, 20)
    p = 56
    psect, p = cstr(d, p)
    (ng,) = struct.unpack_from(">I", d, p)
    p += 4
    for _ in range(ng):
        _n, p = cstr(d, p)
        p += 6
    code_at = p
    p += codsz
    idata_at = p
    p += idatsz
    (nx,) = struct.unpack_from(">I", d, p)
    p += 4
    print("%s  psect=%s  Code %d Byte ab Dateioffset %d"
          % (path, psect, codsz, code_at))
    for _ in range(nx):
        name, p = cstr(d, p)
        (nref,) = struct.unpack_from(">I", d, p)
        p += 4
        refs = []
        for _ in range(nref):
            typ, off = struct.unpack_from(">HI", d, p)
            p += 6
            refs.append((typ, off))
        if wanted and name not in wanted:
            continue
        print("\n  %s -- %d Referenz(en)" % (name, nref))
        for typ, off in refs:
            wo = code_at + off if (typ & 0x20) else idata_at + off
            n = umfang(typ) or 4
            roh = d[wo - 4:wo + n]
            print("    typ=%04x offset=$%x  (%s)" % (typ, off, erklaere(typ)))
            print("       Bytes ab Offset-4: %s" % roh.hex(" "))


main()
