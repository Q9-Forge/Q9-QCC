#!/usr/bin/env python3
"""Zerlegt eine OS-9/68k-Bibliothek (.l) in ihre ROFs und listet die
globalen Symbole.

Eine Bibliothek ist KEIN Sonderformat, sondern eine Folge von ROFs --
an `sys.l` gemessen (siehe Q9-ql68/README). Der Kopfaufbau steht in
Q9-qr68/README, "Das ROF-Format, am Original gemessen".

    libdump.py <datei.l> [--namen] [--module] [--extern]

Ohne Schalter: die Zusammenfassung. --namen druckt alle Globalen,
--module eine Zeile je ROF.
"""
import sys
import struct

SYNC = 0xDEADFACE


def cstr(d, off):
    end = d.index(b"\0", off)
    return d[off:end].decode("latin-1"), end + 1


def parse_rof(d, off):
    """Ein ROF ab off. Gibt (naechster_offset, dict) zurueck."""
    (sync,) = struct.unpack_from(">I", d, off)
    if sync != SYNC:
        raise ValueError("kein $DEADFACE auf Offset %d (0x%x)" % (off, sync))
    ty_lan, att_rev = struct.unpack_from(">HH", d, off + 4)
    statsz, idatsz, codsz = struct.unpack_from(">III", d, off + 20)
    p = off + 56
    psect, p = cstr(d, p)

    (nglob,) = struct.unpack_from(">I", d, p)
    p += 4
    globs = []
    for _ in range(nglob):
        name, p = cstr(d, p)
        typ, addr = struct.unpack_from(">HI", d, p)
        p += 6
        globs.append((name, typ, addr))

    p += codsz + idatsz

    (next_, ) = struct.unpack_from(">I", d, p)
    p += 4
    exts = []
    for _ in range(next_):
        name, p = cstr(d, p)
        (nref,) = struct.unpack_from(">I", d, p)
        p += 4 + nref * 6
        exts.append(name)

    (nloc,) = struct.unpack_from(">I", d, p)
    p += 4 + nloc * 6
    p += 16                      # vier Langwoerter, in allen Proben 0

    return p, {
        "psect": psect, "ty_lan": ty_lan, "att_rev": att_rev,
        "codsz": codsz, "idatsz": idatsz, "statsz": statsz,
        "globals": globs, "externals": exts,
    }


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = set(a for a in sys.argv[1:] if a.startswith("--"))
    if not args:
        sys.exit(__doc__)
    d = open(args[0], "rb").read()

    mods = []
    off = 0
    while off < len(d):
        if len(d) - off < 56:
            print("  ! %d Restbytes ohne ROF-Kopf" % (len(d) - off))
            break
        try:
            off, m = parse_rof(d, off)
        except (ValueError, IndexError, struct.error) as e:
            # Nicht stillschweigend weiterlaufen -- an der Modellgrenze
            # abbrechen und sagen, wo.
            print("  ! Abbruch bei Offset %d: %s" % (off, e))
            break
        mods.append(m)

    allg = [g for m in mods for g in m["globals"]]
    allx = sorted(set(x for m in mods for x in m["externals"]))
    if "--extern" in flags:
        # Nur die externen Namen, einer je Zeile -- fuer tools/korpus.sh.
        for x in allx:
            print(x)
        return
    print("%s: %d Byte, %d ROFs, %d Globale, %d verschiedene externe Namen"
          % (args[0], len(d), len(mods), len(allg), len(allx)))
    print("   Code %d Byte, init. Daten %d, reservierte Daten %d"
          % (sum(m["codsz"] for m in mods),
             sum(m["idatsz"] for m in mods),
             sum(m["statsz"] for m in mods)))

    if "--module" in flags:
        print()
        for m in mods:
            print("  %-16s code=%-7d idat=%-6d stat=%-6d glob=%-4d ext=%d"
                  % (m["psect"], m["codsz"], m["idatsz"], m["statsz"],
                     len(m["globals"]), len(m["externals"])))
    if "--namen" in flags:
        print()
        for name, typ, addr in sorted(allg):
            print("  %-28s typ=%04x adr=%d" % (name, typ, addr))
        print()
        print("  --- externe Namen (was clib SELBST braucht) ---")
        for x in allx:
            print("  %s" % x)


main()
