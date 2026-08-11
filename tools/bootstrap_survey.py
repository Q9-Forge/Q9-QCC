#!/usr/bin/env python3
"""Bootstrap-Erhebung: welche Teile einer C-Datei uebersetzt QCC schon?

Zweck: den Fortschritt zum Selfhosting nachpruefbar machen. Das Werkzeug
zerlegt die Eingabe in Top-Level-Einheiten (Funktionen, Deklarationen),
schiebt sie kumulativ durch das QCC-Frontend und listet auf, welche
scheitern -- statt beim ersten Fehler abzubrechen. Damit bekommt man die
VOLLSTAENDIGE Luecken-Liste in einem Lauf.

Aufruf:
    python3 tools/bootstrap_survey.py build/qcc_p <vorverarbeitete-datei.c>

Die Eingabe muss praeprozessorfrei sein (QCC hat keinen Praeprozessor):
    sed 's|^[[:space:]]*#[[:space:]]*include.*||' datei.c > tmp.c
    cc -E -P -x c tmp.c > vorverarbeitet.c

Hinweis zur Einordnung: gemessen wird, ob das FRONTEND die Einheit annimmt.
Semantische Fehler (Typpruefung) und die Uebersetzbarkeit durch das Backend
sind eigene, spaetere Stufen -- Parsen ist nicht Uebersetzen.

Gemessene Staende (2026-08-11):
    Data/qcc_p.c              742 Einheiten, 5 scheitern
    Source/qcc_backend_c.cpp   43 Einheiten, 2 scheitern
"""
import subprocess, sys

QCC, SRC = sys.argv[1], sys.argv[2]
text = open(SRC, encoding="utf-8", errors="replace").read()

def split_units(text):
    units, depth, buf, instr, inchr = [], 0, [], False, False
    i = 0
    while i < len(text):
        c = text[i]; buf.append(c)
        if instr:
            if c == '\\': buf.append(text[i+1]); i += 2; continue
            if c == '"': instr = False
        elif inchr:
            if c == '\\': buf.append(text[i+1]); i += 2; continue
            if c == "'": inchr = False
        else:
            if c == '"': instr = True
            elif c == "'": inchr = True
            elif c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    # typedef struct {...} Name;  -> erst beim ';' trennen
                    if "".join(buf).lstrip().startswith("typedef"):
                        pass
                    else:
                        units.append("".join(buf)); buf = []
            elif c == ';' and depth == 0:
                units.append("".join(buf)); buf = []
        i += 1
    if "".join(buf).strip(): units.append("".join(buf))
    return units

def parses(src):
    with open("/tmp/qcc_bootstrap_probe.c","w") as f: f.write(src)
    try:
        r = subprocess.run([QCC,"@/tmp/qcc_bootstrap_probe.c"],capture_output=True,text=True,timeout=180)
        return "FAIL" not in r.stdout
    except subprocess.TimeoutExpired:
        return False

units = split_units(text)
accepted, failed = [], []
for idx, u in enumerate(units):
    if not u.strip():
        accepted.append(u); continue
    if parses("".join(accepted) + u):
        accepted.append(u)
    else:
        failed.append((idx, u))

print(f"  Einheiten gesamt:     {len(units)}")
print(f"  davon uebersetzbar:   {len(units)-len(failed)}")
print(f"  SCHEITERN:            {len(failed)}")
print()
for idx, u in failed:
    head = u.strip().split("\n")[0][:88]
    print(f"    #{idx+1:<4} {head}")
with open("/tmp/qcc_bootstrap_failed.txt","w") as f:
    for idx,u in failed: f.write(f"===== Einheit {idx+1} =====\n{u}\n")
