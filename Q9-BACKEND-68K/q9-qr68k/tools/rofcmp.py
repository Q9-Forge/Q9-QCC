#!/usr/bin/env python3
"""Vergleicht zwei ROF-Dateien byteweise -- die sechs Zeitstempelbytes im
Kopf (Offset 12..17) ausgenommen, denn nur die sind bei r68 nicht
reproduzierbar. Meldet die erste Abweichung mit Kontext.
"""
import sys

STAMP = slice(12, 18)


def load(p):
    d = bytearray(open(p, "rb").read())
    d[STAMP] = b"\0" * 6
    return bytes(d)


ok = 0
bad = 0
for pair in sys.argv[1:]:
    name, ref, mine = pair.split(":")
    try:
        a = load(ref)
        b = load(mine)
    except OSError as e:
        print("  %-8s FEHLT: %s" % (name, e))
        bad += 1
        continue
    if a == b:
        print("  %-8s gleich (%d Byte)" % (name, len(a)))
        ok += 1
        continue
    bad += 1
    print("  %-8s ABWEICHUNG (r68 %d Byte, qr68 %d Byte)" % (name, len(a), len(b)))
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            lo = max(0, i - 6)
            print("           erste bei Offset %d: r68=%02x qr68=%02x" % (i, a[i], b[i]))
            print("           r68 : %s" % a[lo:i + 10].hex(" "))
            print("           qr68: %s" % b[lo:i + 10].hex(" "))
            break
    else:
        print("           nur die Laenge unterscheidet sich")

print()
print("  %d gleich, %d abweichend" % (ok, bad))
sys.exit(1 if bad else 0)
