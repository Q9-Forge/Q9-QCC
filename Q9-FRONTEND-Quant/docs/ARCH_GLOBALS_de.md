# Architektur-Globals in der IR — Vorschlag für `__ptrsize`

**Status: Entwurf, noch NICHT implementiert** — weder im QCC-Frontend, noch in
`qccvm.py`, noch in Q9-Run. Entstanden aus einer Diskussion zum ungelösten
`sizeof(Zeiger)`-Problem in QCC (siehe `Q9-QCC/docs/ISO_C_GAP_LIST_de.md`).
Dieses Dokument beschreibt die geplante Lösung, damit sie beim Erreichen von
Phase 5 (Pointer & Arrays) hier direkt nachlesbar ist, statt neu erfunden
werden zu muessen.

## Das Problem

`qcc_p` (Frontend) kennt keine Zielarchitektur — dieselbe erzeugte IR-Datei
läuft wahlweise durch den 68k- oder den ARM64-Backend (oder eben Q9-Run/
QCCVM). Ein Zeiger ist aber real 4 Byte auf 68k und 8 Byte auf ARM64.
`sizeof(char*)` kann das Frontend deshalb nicht als feste Zahl in die IR
schreiben, ohne die Eins-IR-für-jedes-Ziel-Eigenschaft zu zerstören. Aktuell
lehnt QCC `sizeof()` auf Zeigertypen deshalb mit einer eigenen Meldung ab
("sizeof of pointer types not supported in this version").

## Die Lösung: architekturdefinierte globale Symbole

Kein neuer Opcode — Wiederverwendung des bestehenden Mehrdatei-Mechanismus
(`GLOBALDECL`/`LOADG`, siehe `docs/IR_OPCODES_de.md`).

**Deklaration** (vom Frontend einmal pro Übersetzungseinheit emittiert, die
`sizeof(Zeiger)` benutzt):
```
GLOBALDECL __ptrsize i
```

**Benutzung** (an der Stelle von `sizeof(T*)`):
```
LOADG __ptrsize
```

Das ist exakt dieselbe IR-Form wie ein `extern int x;` aus einer anderen
Datei — nur dass die "andere Datei" hier keine QCC-Quelle ist, sondern eine
Architekturdefinition außerhalb des Programms.

## Reservierter Namensraum

Namen mit führendem doppeltem Unterstrich (`__ptrsize`, später ggf.
`__intsize`, `__endian`, …) sind **architekturdefiniert**. Wer `GLOBALDECL`
prüft — heute `Q9-QCC/tools/qcc_merge.py`, in Q9-Run analog jede eigene
Mehrdatei-/Link-Logik — muss diese Namen **ausnehmen**: sie brauchen KEINE
passende `GLOBAL`-Definition im Programm, sie kommen von außerhalb. Jeder
andere `GLOBALDECL`-Name ohne Definition bleibt ein echter Fehler wie bisher.

Bisher einziger Name: **`__ptrsize`** (Byte-Größe eines beliebigen
Zeigertyps, Typ `i`).

## Wer löst auf — je nach Ziel unterschiedlich

| Ziel | Wer | Wie |
|---|---|---|
| 68k/ARM64 (echter Compiler) | **Linker** | winzige, immer mitgelinkte Architekturdatei pro Ziel (z. B. `runtime/arch_68k.a`: `xdef __ptrsize` / `__ptrsize dc.l 4`; `arch_arm64.s` analog mit 8) |
| **Q9-Run / QCCVM** (kein separater Link-Schritt) | **der Interpreter selbst, vor dem Start von `main`** | neue Option, z. B. `--arch=arch/68k.json`, die die passenden Globals vorbelegt, BEVOR die IR ausgeführt wird |

Der Interpreter übernimmt hier die Rolle, die sonst der Linker hätte —
es gibt bei ihm ja keinen eigenen Link-Schritt.

Vorschlag für das Architektur-Definitionsfile (bewusst simpel gehalten):
```json
// arch/68k.json
{ "__ptrsize": 4 }
```
```json
// arch/arm64.json
{ "__ptrsize": 8 }
```

**Für Q9-Run konkret:** beim Laden der IR (`qrun_ir.c`) nach `GLOBALDECL`
mit reserviertem Namen suchen und den Wert aus der `--arch=`-Datei in die
Globals-Tabelle eintragen, bevor die VM-Schleife (`qrun_vm.c`) losläuft.
Fehlt `--arch=` und referenziert das Programm ein solches Symbol, sollte das
wie ein nicht aufgelöstes `GLOBALDECL` ohne Definition behandelt werden —
also ein Fehler, kein stiller Rateversuch.

## Nicht Teil dieses Vorschlags

- Konstantenfaltung: ob ein Backend `LOADG __ptrsize` zur Übersetzungszeit
  in ein Immediate umwandelt (es kennt ja seine eigene Architektur), ist eine
  optionale Backend-Optimierung, keine Voraussetzung für Korrektheit. Q9-Run
  braucht das nicht — ein normaler globaler Lesezugriff reicht.
- Weitere Symbole (`__intsize`, `__endian`, …) — folgen bei Bedarf demselben
  Muster, sind aber aktuell nicht konkret motiviert.
