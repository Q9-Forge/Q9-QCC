"""Zwei OS-9/68k-Module vergleichen und die erste Abweichung benennen.

Anders als ein blosses "cmp" sagt es, IN WELCHEM FELD sie liegt -- der
Kopf ist durchweg festgelegt, und ein verrutschter Offset ist sonst kaum
zuzuordnen.
"""
import sys
import struct

FELDER = [
    (0x00, 2, "M$ID"), (0x02, 2, "M$SysRev"), (0x04, 4, "M$Size"),
    (0x08, 4, "M$Owner"), (0x0C, 4, "M$Name"), (0x10, 2, "M$Accs"),
    (0x12, 2, "M$TyLan"), (0x14, 2, "M$AttRev"), (0x16, 2, "M$Edit"),
    (0x18, 4, "M$Usage"), (0x1C, 4, "M$Symbol"), (0x20, 2, "M$Ident"),
    (0x22, 6, "Reserve"), (0x28, 4, "M$HdExt"), (0x2C, 2, "M$HdExtSz"),
    (0x2E, 2, "M$Parity"), (0x30, 4, "M$Exec"), (0x34, 4, "M$Excpt"),
    (0x38, 4, "M$Data"), (0x3C, 4, "M$Stack"), (0x40, 4, "M$IData"),
    (0x44, 4, "M$IRefs"),
]


def feld(off):
    for at, n, name in FELDER:
        if at <= off < at + n:
            return name
    return None


def wert(d, at, n):
    v = 0
    for i in range(n):
        v = (v << 8) | d[at + i]
    return v


def main():
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    print("l68 %d Byte, ql68 %d Byte" % (len(a), len(b)))

    for at, n, name in FELDER:
        if at + n <= min(len(a), len(b)) and a[at:at + n] != b[at:at + n]:
            print("  %-10s @%04x: l68=%0*x  ql68=%0*x"
                  % (name, at, n * 2, wert(a, at, n), n * 2, wert(b, at, n)))

    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            f = feld(i)
            wo = " (%s)" % f if f else ""
            print("  erste Abweichung bei %#06x%s: l68=%02x ql68=%02x"
                  % (i, wo, a[i], b[i]))
            lo = max(0, i - 8)
            print("    l68 : %s" % a[lo:i + 12].hex(" "))
            print("    ql68: %s" % b[lo:i + 12].hex(" "))
            break
    else:
        if len(a) != len(b):
            print("  gleicher Anfang, verschiedene Laenge")


main()
