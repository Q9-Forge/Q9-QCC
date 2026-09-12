#!/usr/bin/env python3
"""Erzeugt Proben, die l68 zur Sprungtabelle zwingen.

    genjt.py <verzeichnis> <anzahl_ferner_aufrufe> [<fuellworte>]

Der erste psect ruft N Ziele im ZWEITEN psect, dazwischen liegt so viel
Fuellcode, dass die 16-Bit-Reichweite von bsr nicht reicht. Damit laesst
sich messen, ob die Tabelle nur fuer Bibliothekssymbole entsteht (hier
sind es ganz normale ROF-Eingaben) und wonach l68 ihre Groesse schaetzt.
"""
import sys
import os

d = sys.argv[1]
n = int(sys.argv[2])
fill = int(sys.argv[3]) if len(sys.argv) > 3 else 20000
os.makedirs(d, exist_ok=True)

with open(os.path.join(d, "ja.a"), "w") as f:
    f.write("* %d ferne Aufrufe, %d Fuellworte dahinter\n" % (n, fill))
    f.write("         psect   ja,$0101,$8000,1,100,start\n")
    f.write("start    moveq   #1,d0\n")
    for i in range(n):
        f.write("         bsr     far%d\n" % i)
    f.write("         rts\n")
    f.write("* Fuellcode -- bringt die Ziele ausser Reichweite\n")
    for _ in range(fill):
        f.write("         nop\n")
    f.write("         ends\n")

with open(os.path.join(d, "jb.a"), "w") as f:
    f.write("         psect   jb,0,0,0,0,0\n")
    for i in range(n):
        f.write("far%d:    moveq   #%d,d0\n" % (i, i % 100))
        f.write("         rts\n")
    f.write("         ends\n")

print("%s: ja.a (%d Aufrufe, %d Fuellworte) + jb.a" % (d, n, fill))
