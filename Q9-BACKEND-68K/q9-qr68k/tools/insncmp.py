#!/usr/bin/env python3
"""Compare code sections from two ROF files source line by source line.

The anchor is r68's listing (option -l), which gives the code-section offset
for each source line. Code is split into source-line pieces and compared with
the corresponding qr68 output, so the report names the incorrectly encoded
line instead of only saying "first difference at offset 1234".

    insncmp.py <listing> <r68.r> <qr68.r> [quelle.a]
"""
import re
import struct
import sys

LST = re.compile(r"^(\d{5}) ([0-9a-fA-F]{4})[ =]")


def code_of(path):
    d = open(path, "rb").read()
    if d[0:4] != b"\xde\xad\xfa\xce":
        raise SystemExit("%s: kein ROF" % path)
    (idat, cods) = struct.unpack(">ii", d[24:32])
    p = 56
    p = d.index(b"\0", p) + 1
    (ng,) = struct.unpack(">I", d[p:p + 4])
    p += 4
    for _ in range(ng):
        p = d.index(b"\0", p) + 1 + 6
    return d[p:p + cods]


def main():
    listing, ref, mine = sys.argv[1], sys.argv[2], sys.argv[3]
    src = open(sys.argv[4], encoding="latin-1").read().splitlines() if len(sys.argv) > 4 else []

    spots = []          # (zeilennummer, offset)
    for line in open(listing, encoding="latin-1"):
        m = LST.match(line)
        if m:
            spots.append((int(m.group(1)), int(m.group(2), 16)))

    a = code_of(ref)
    b = code_of(mine)
    if not spots:
        raise SystemExit("Listing enthaelt keine Offsets -- lief r68 mit -l?")

    bad = 0
    for i, (lineno, off) in enumerate(spots):
        end = spots[i + 1][1] if i + 1 < len(spots) else len(a)
        if end < off:
            end = off
        ra = a[off:end]
        rb = b[off:end] if off <= len(b) else b""
        if ra != rb:
            bad += 1
            text = src[lineno - 1].rstrip() if lineno - 1 < len(src) else ""
            print("  Zeile %-5d Offset %-6d %s" % (lineno, off, text))
            print("      r68 : %s" % ra.hex(" "))
            print("      qr68: %s" % rb.hex(" "))
            if bad >= 25:
                print("  ... weitere unterdrueckt")
                break

    if len(a) != len(b):
        print("  Codelaenge: r68 %d, qr68 %d" % (len(a), len(b)))
        bad += 1
    if bad == 0:
        print("  Code Zeile fuer Zeile gleich (%d Byte, %d Zeilen)" %
              (len(a), len(spots)))
    return 1 if bad else 0


sys.exit(main())
