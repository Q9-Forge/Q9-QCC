# Entwicklerhandbuch

## Datenfluss

```text
EBNF-Quelle
  → Source/ebnf.cpp (Generator)
  → Data/tinyc_p.c (Parser mit Nutzer-Aktionen)
  → Stack-IR
  → tools/tinyvm.py oder Target-Backend
```

Die Stack-IR ist die zentrale Grenze. Frontend-Aktionen dürfen keine
68000-/ARM64-Details enthalten. CPU-spezifische Register, Frames und
Adressbreiten gehören ausschließlich in die Backends.

## Typmodell

`TCType` besteht aus Basistyp und Pointertiefe. Dadurch sind `int`, `int *` und
`int **` dieselbe Modellfamilie. Arrays bleiben Objekte mit fester Bytegröße und
zerfallen in Ausdrücken zu einem Pointer auf das erste Element.

Adressen und Werte sind getrennt: `ADDRL`/`ADDRG` bilden Adressen, `LOADIND` und
`STOREIND` dereferenzieren sie. Pointeroperationen tragen ihren Elementtyp (`c`,
`i` oder `p`) in der IR, damit jedes Backend korrekt skaliert.

## Backend-Verträge

- TinyVM nutzt abstrakte Block-/Byteadressen und ist das schnellste Semantik-Orakel.
- Das 68000-Backend erzeugt 32-Bit-Pointercode.
- Das ARM64-Backend erzeugt 64-Bit-Pointercode und linkt gegen
  `runtime/arm64_darwin`.
- `tc_putint` und `tc_exit` sind Runtime-Symbole; sie gehören nicht in die IR.

## Änderungsregeln

Bei jedem neuen Sprachfeature müssen Grammatik, semantische Aktionen, IR,
TinyVM, beide Backends und mindestens ein Regressionstest gemeinsam betrachtet
werden. Zuerst wird die Semantik in der VM abgesichert, danach werden die
Maschinen-Backends angepasst. Überlappende Operatoren benötigen
Longest-Match-Behandlung im Generator.

**Falle: `type` darf auf keinen Fall auf einen BLOSSEN Bezeichner matchen,
ohne `statement`s Reihenfolge zu pruefen.** Seit `typedef` gibt es in `type`
eine Alternative `typedefRef = ident` (jeder Bezeichner ist strukturell ein
moeglicher Typname). Das Werkzeug hat KEINE semantischen Praedikate -- eine
Regel matcht rein strukturell, ohne zu wissen, ob der Bezeichner wirklich ein
registriertes Typedef ist. Solange `varDecl` in `statement`s Alternativenliste
VOR `returnStmt`/`breakStmt`/`continueStmt` steht, wird z. B. `return p;`
faelschlich als Vardeklaration `<Bezeichner-als-Typ> p;` geparst (beide haben
die Form "Bezeichner Bezeichner ;", ein Backtracking-Fehlschlag tritt NICHT
ein, weil die Deklaration strukturell tatsaechlich passt). Fix (2026-07-23):
`varDecl` steht in `statement` bewusst AN LETZTER STELLE, nach allen Alter-
nativen mit festem Schluesselwort. Bei jeder kuenftigen Erweiterung, die
`ident` als neue `type`- oder statement-einleitende Alternative zulaesst,
diese Reihenfolge erneut pruefen.

## Diagnose und Wiederaufnahme

Für eine kurze neue Sitzung zuerst `docs/STATUS.md`, danach nur die thematisch
passende Datei lesen. `context.txt` ist ein historisches Arbeitsprotokoll und
nicht die maßgebliche Spezifikation. Nach Änderungen immer `./runtests.sh`
ausführen und das Ergebnis in `STATUS.md` festhalten.

Die ausführliche Begründung einzelner Entscheidungen steht in
`docs/ARCHITEKTUR.md`; dieses Dokument beschreibt dagegen die dauerhaft gültigen
Schnittstellen und Arbeitsregeln.

