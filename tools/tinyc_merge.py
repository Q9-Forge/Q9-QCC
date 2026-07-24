#!/usr/bin/env python3
# Tiny-C Mehrdatei-"Linker" fuer TinyVM (siehe docs/ARCHITEKTUR.md Kapitel 10,
# Mehrdatei-Uebersetzung). TinyVM selbst hat kein Objektdatei-/Linker-Modell --
# dieses Werkzeug fuehrt mehrere IR-Dateien (je ein separat mit tinyc_p
# uebersetztes Tiny-C-Quelldokument) zu EINEM Programm zusammen und prueft dabei
# explizit, was ein echter Linker (l68 fuers 68k/OS-9-Ziel, ld/clang fuers
# ARM64-Ziel) ebenfalls durchsetzen wuerde -- ohne diese Pruefungen wuerden die
# einfachen Python-Dicts in tinyvm.run() Namenskollisionen stillschweigend
# ueberschreiben und echte Mehrdatei-Bugs maskieren statt sie aufzudecken.
#
# Nutzung: python3 tools/tinyc_merge.py datei1.ir datei2.ir [...] > merged.ir
#          python3 tools/tinyvm.py merged.ir
import sys

from tinyvm import parse_ir


def load(paths):
    files = []
    for path in paths:
        with open(path) as f:
            files.append((path, parse_ir(f.read())))
    return files


def defs_and_decls(files):
    """Liefert (defs, decls): defs[name] = (kind, static, path, extra...),
    decls[name] = liste von (kind, path, extra...) -- kind ist 'FUNC'/'GLOBAL'."""
    defs = {}
    decls = []
    mains = []
    for path, prog in files:
        for op, args in prog:
            if op == "FUNC":
                name = args[0]
                nargs = int(args[1])
                static = int(args[2]) if len(args) >= 3 else 0
                if name in defs:
                    okind, ostatic, opath = defs[name][0], defs[name][1], defs[name][2]
                    sys.stderr.write(
                        "tinyc_merge: doppelte Definition von '%s' (%s UND %s) -- "
                        "simuliert 'duplicate symbol' eines echten Linkers\n"
                        % (name, opath, path))
                    return None, None, None
                defs[name] = ("FUNC", static, path, nargs)
                if name == "main":
                    mains.append(path)
            elif op == "GLOBAL" or op == "GARRAY":
                name = args[0]
                static = 0
                if op == "GLOBAL" and len(args) >= 4:
                    static = int(args[3])
                elif op == "GARRAY" and len(args) >= 4:
                    static = int(args[3])
                if name in defs:
                    opath = defs[name][2]
                    sys.stderr.write(
                        "tinyc_merge: doppelte Definition von '%s' (%s UND %s) -- "
                        "simuliert 'duplicate symbol' eines echten Linkers\n"
                        % (name, opath, path))
                    return None, None, None
                defs[name] = (op, static, path, None)
            elif op == "FUNCDECL":
                decls.append(("FUNC", args[0], path, int(args[1])))
            elif op == "GLOBALDECL":
                decls.append(("GLOBAL", args[0], path, args[1]))
    return defs, decls, mains


def check(defs, decls, mains):
    ok = True
    if len(mains) == 0:
        sys.stderr.write("tinyc_merge: keine Datei definiert 'main'\n")
        ok = False
    elif len(mains) > 1:
        sys.stderr.write("tinyc_merge: 'main' in mehreren Dateien definiert: %s\n" % ", ".join(mains))
        ok = False
    for kind, name, path, extra in decls:
        if name not in defs:
            sys.stderr.write(
                "tinyc_merge: '%s' (deklariert in %s) ist in KEINER Datei definiert\n"
                % (name, path))
            ok = False
            continue
        dkind, dstatic, dpath, dextra = defs[name]
        if dkind != kind:
            sys.stderr.write(
                "tinyc_merge: '%s' ist in %s als %s deklariert, aber in %s als %s definiert\n"
                % (name, path, kind, dpath, dkind))
            ok = False
            continue
        if dstatic:
            sys.stderr.write(
                "tinyc_merge: '%s' ist in %s als static definiert -- fuer %s nicht sichtbar\n"
                % (name, dpath, path))
            ok = False
            continue
        # Bonus-Konsistenzpruefung (Signatur), die ein echter Linker NICHT leisten
        # koennte (der kennt nur Namen, keine Typen/Argumentzahlen) -- faengt den
        # klassischen "veralteter Handschrift-Prototyp"-Bug.
        if kind == "FUNC" and dextra is not None and dextra != extra:
            sys.stderr.write(
                "tinyc_merge: '%s' hat in %s %d Parameter deklariert, in %s aber %d definiert\n"
                % (name, path, extra, dpath, dextra))
            ok = False
    return ok


def main():
    paths = sys.argv[1:]
    if not paths:
        sys.stderr.write("usage: tinyc_merge.py datei1.ir datei2.ir [...]\n")
        return 2
    files = load(paths)
    defs, decls, mains = defs_and_decls(files)
    if defs is None:
        return 1
    if not check(defs, decls, mains):
        return 1
    for path, prog in files:
        for op, args in prog:
            if op in ("FUNCDECL", "GLOBALDECL"):
                continue  # bereits gegen die echte Definition geprueft, im gemergten IR ueberfluessig
            print(" ".join([op] + list(args)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
