#!/usr/bin/env python3
"""Bootstrap-Erhebung: welche Teile einer C-Datei uebersetzt QCC schon?

Zweck: den Fortschritt zum Selfhosting nachpruefbar machen. Das Werkzeug
zerlegt die Eingabe in Top-Level-Einheiten (Funktionen, Deklarationen),
schiebt sie kumulativ durch das QCC-Frontend und listet auf, welche
scheitern -- statt beim ersten Fehler abzubrechen. Damit bekommt man die
VOLLSTAENDIGE Luecken-Liste in einem Lauf.

Aufruf:
    python3 tools/bootstrap_survey.py build/qcir <vorverarbeitete-datei.c>

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
    """Nimmt die GRAMMATIK die Einheit an?

    Bewusst NUR das Parse-Ergebnis (stdout "FAIL"), NICHT der Rueckgabewert.
    Grund: die Einheiten werden kumulativ geprueft, ein Praefix enthaelt also
    voellig regulaer noch unaufgeloeste Vorwaertsbezuege. Semantische Fehler
    ("qcc: unknown function") sind in einem Praefix daher zu erwarten und kein
    Befund -- wer sie hier mitzaehlt, bekommt eine Kaskade von Falschmeldungen
    ab der ersten Vorwaertsreferenz. Die semantische Lage wird stattdessen am
    Ende EINMAL an der vollstaendigen Datei gemessen, siehe unten.

    Verglichen wird das Schlusswort ZEILENWEISE und exakt, nicht per
    Teilzeichenkette: Marker koennen einander enthalten (der Semantik-Marker
    hiess zunaechst SEMFAIL und wurde von einer "FAIL"-in-stdout-Suche
    faelschlich als Parse-Fehler gelesen -- 346 statt 5 Meldungen).
    """
    with open("/tmp/qcc_bootstrap_probe.c","w") as f: f.write(src)
    try:
        r = subprocess.run([QCC,"@/tmp/qcc_bootstrap_probe.c"],capture_output=True,text=True,timeout=180)
        return "FAIL" not in r.stdout.split()
    except subprocess.TimeoutExpired:
        return False


def semantics(path):
    """Zweite, unabhaengige Stufe: uebersetzt die GANZE Datei beanstandungsfrei?

    Erst hier sind alle Definitionen sichtbar, erst hier ist der Rueckgabewert
    aussagekraeftig. Seit 2026-08-11 liefert der erzeugte Parser bei einem
    semantischen Fehler 1 statt 0 (vorher: Meldung auf stderr, aber "OK" und
    Rueckgabewert 0 -- solche Faelle galten stillschweigend als in Ordnung).
    """
    try:
        r = subprocess.run([QCC, "@" + path], capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return None, "<Zeitueberschreitung>", []
    # Schlusswort exakt bestimmen (Marker koennen einander als Teilzeichenkette
    # enthalten, deshalb zeilenweise vergleichen -- siehe parses()).
    marker = "<keines>"
    for line in r.stdout.splitlines():
        if line in ("OK", "SEMERR", "FAIL"):
            marker = line
    msgs, seen = [], set()
    for line in r.stderr.splitlines():
        if line.startswith("qcc:") and line not in seen:
            seen.add(line); msgs.append(line)
    return r.returncode, marker, msgs

units = split_units(text)
accepted, failed = [], []
for idx, u in enumerate(units):
    if not u.strip():
        accepted.append(u); continue
    if parses("".join(accepted) + u):
        accepted.append(u)
    else:
        failed.append((idx, u))

print("  STUFE 1 -- Grammatik (kumulativ, Einheit fuer Einheit)")
print(f"    Einheiten gesamt:   {len(units)}")
print(f"    davon angenommen:   {len(units)-len(failed)}")
print(f"    SCHEITERN:          {len(failed)}")
if failed:
    print()
    for idx, u in failed:
        head = u.strip().split("\n")[0][:88]
        print(f"      #{idx+1:<4} {head}")
with open("/tmp/qcc_bootstrap_failed.txt","w") as f:
    for idx,u in failed: f.write(f"===== Einheit {idx+1} =====\n{u}\n")

print()
print("  STUFE 2 -- Semantik (die GANZE Datei in einem Lauf)")
rc, marker, msgs = semantics(SRC)
if marker == "OK":
    print("    beanstandungsfrei uebersetzt (OK, Rueckgabewert 0)")
elif marker == "FAIL":
    print("    PARSE-Fehler an der ganzen Datei (FAIL) -- die Semantik ist damit")
    print("    noch nicht messbar. Erst die Luecken aus Stufe 1 schliessen.")
elif marker == "SEMERR":
    print(f"    Grammatik ok, aber {len(msgs)} verschiedene Beanstandung(en) (SEMERR):")
    for m in msgs[:25]:
        print(f"      {m}")
    if len(msgs) > 25:
        print(f"      ... und {len(msgs)-25} weitere")
else:
    print(f"    unerwartet: kein Schlusswort, Rueckgabewert {rc}")
print()
print("  Stufe 1 misst, ob die Grammatik den Text annimmt; Stufe 2, ob er auch")
print("  bedeutungsvoll ist. Nur Stufe 2 = 0 heisst uebersetzbar.")
