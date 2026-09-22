# Q9 MODUL Erweiterung

Status-Übersicht und Arbeitsplan für die Implementierung nativer OS-9 Modulartefakte (`#DEFMODUL`, Dispatch-Tabellen, Calling-Conventions, `#ASM`) in Q9-QCC.

Stand: 2026-09-22. Diese Fassung ersetzt die ursprüngliche flache Paketliste durch den Phasenplan aus `PLAN_REVIEW_DEFMODUL.md` (Codex) und trägt die dort offen gelassenen bzw. dort fehlenden Entscheidungen aus `PLAN_REVIEW_ERGAENZUNG_DEFMODUL.md` (Claude) als konkrete Ergebnisse ein. Beide Dokumente bleiben als Begründungstext stehen — hier steht nur, was daraus folgt.

**Leitsatz aus beiden Reviews, unverändert übernommen:** erst ABI und IR verbindlich machen, dann ein kleines `PROG`-/`DRIVER`-MVP durch die komplette Pipeline bringen, danach erst `modul syscall`, Adressplatzierung und Inline-Assembler ergänzen.

**Legende:**
- 🔴 Offen / Noch nicht begonnen
- 🟡 In Arbeit / In Prüfung / teilweise entschieden
- 🟢 Abgeschlossen / Verifiziert / entschieden

---

## Ausgangslage

`q9-qcpp` erkennt `#DEFMODUL`, `#ORG`, `#SECTION` sowie `#ASM`/`#ENDASM` in Groß- und Kleinschreibung bereits auf Scanner-Ebene und reicht sie unverändert als Text durch (Commit `edcd65a`) — dasselbe Marker-Muster wie das seit längerem bestehende `#asm`/`#endasm`. Das ist reines Passthrough ohne Semantik und blockiert Phase 1 nicht; alles Weitere (Frontend-Semantik, IR-Emission, Backend-Codegen, Linker) ist noch nicht begonnen.

---

## Phasenplan (Dashboard)

| Phase | Ziel | Status | Kurzbeschreibung |
|---|---|:---:|---|
| **0. Baseline** | Reproduzierbare Ausgangsbasis | 🔴 | Aktuellen Build aller fünf Komponenten + Golden-Test für normale Programme sichern |
| **1. ABI & IR-Entscheidungen** | Keine Backend-Arbeit auf Annahmen | 🟢 | Register-ABI, Dispatch-Tabelle, IR-Syntaxform, Keyword-Namensraum, `#DEFOS` und Backend-Abdeckung entschieden (s. u.) |
| **2. Minimaler IR-Metadatenpfad** | Metadaten sicher bis zum 68k-Backend | 🔴 | Nur `MODHEADER`, Entry-Zuordnung, `DISPATCHTAB`, `FUNC ... [conv]` — kein `ORG`/`SECTION`/`ALIGN`/`INLINEASM`/`modul syscall` |
| **3. Einfaches `PROG`-Modul** | Einfachster Modultyp ohne Treiber-ABI | 🔴 | Referenz-MVP, danach erst Treiber |
| **4. Minimaler `DRIVER`-Pfad** | Ein Treibertyp, explizite Dispatch-Tabelle | 🔴 | Keine automatische Default-Magie im ersten Schritt |
| **5. Weitere Calling-Conventions** | `interrupt`, `trap`, `naked` | 🔴 | In dieser Reihenfolge, je eigener Prolog-/Epilog-Test |
| **6. Adressplatzierung** | `ALIGN`, `SECTION`, `ORG`, `NOOS` | 🟡 | Scanner-Passthrough für `#ORG`/`#SECTION` existiert; Semantik/Backend offen |
| **7. `modul syscall`** | Wenige konkrete Syscalls zuerst | 🔴 | Pro Syscall eigene Registrierung statt Archetyp allein; Syntax entschieden 22.09. (s. `OS9_SYSTEM_INTERFACE.md` Abschnitt 5) |
| **8. Inline-Assembler** | `#ASM`/`#ENDASM` → `INLINEASM` | 🟡 | Präprozessor-Marker existiert bereits 🟢; `qcir`-Parser & IR-Emission offen 🔴 |

---

## Phase 0 — Baseline einfrieren

- [ ] Build von `q9-qcpp`, `q9-qcir`, `q9-qir68k`, `q9-qr68k`, `q9-ql68k` dokumentieren
- [ ] Normalen Programm-Build als Golden-Test sichern (ohne jede neue Direktive)
- [ ] Bestehende Modul-Header-/CRC-Tests in `ql68k` sammeln
- [ ] Prüfen, ob ein vorhandener realer Treiber als Referenzmodul dienen kann
- [ ] Uncommitteten Ausgangszustand vor der Erweiterung separat sichern

**Abnahmekriterium:** ein unveränderter normaler Programm-Build bleibt bitweise bzw. semantisch reproduzierbar.

---

## Phase 1 — ABI & IR-Entscheidungen abschließen

### 1.a Register-ABI (jetzt entschieden 🟢)

Ursprüngliche Spec (`OS9_SYSTEM_INTERFACE.md` §4.1) hatte `a1`/`a2` vertauscht. Aufgelöst mit drei unabhängigen Quellen (`PLAN_REVIEW_ERGAENZUNG_DEFMODUL.md` Abschnitt 1): offizielles Microware-Handbuch `MWOS/DOC/PDF/68k_techio.pdf` (S. 143/149/154, für READ/GETSTAT/SETSTAT/INIT übereinstimmend), `HOSTFS_MANAGER.md`, realer `ss_gdp.a`-Fund.

| Register | Bedeutung | Quelle |
|---|---|---|
| `a1` | Path-Descriptor | Handbuch + 2 Projektquellen |
| `a2` | Device-Static-Storage | Handbuch + 2 Projektquellen |
| `a4` | Process-Descriptor | Handbuch, deckt sich mit ursprünglicher Spec |
| `a5` | Caller-Register-Stack-Pointer | Handbuch, in ursprünglicher Spec gefehlt — ergänzen |
| `a6` | System-Global-Data-Storage-Pointer | Handbuch, deckt sich mit ursprünglicher Spec |

**To-do:** `OS9_SYSTEM_INTERFACE.md` §4.1 entsprechend korrigieren (`a1`/`a2` tauschen, `a5` ergänzen).

### 1.b Dispatch-Tabelle für `DRIVER` (jetzt entschieden 🟢)

Handbuch: *„branch table with **seven** entries"* — `INIT, READ, WRITE, GETSTAT, SETSTAT, TERM, TRAP`. Ursprüngliche Spec und Codex' Phase-4-Beispiel listen beide nur 6 Namen, `TRAP` fehlt in beiden. `TRAP` darf laut Handbuch auf 0 zeigen, muss als Tabellenslot aber vorhanden sein.

**To-do:** überall, wo die Standard-Dispatch-Liste auftaucht (`OS9_SYSTEM_INTERFACE.md` §2.1/§7, `IR_OPCODES_de.md`-Beispiel, Codex' Phase-4-Beispiel), einen siebten Eintrag `trap`/`drv_trap` (Default-Ziel: 0) ergänzen.

### 1.c Globals-Adressierung in Calling-Convention-Funktionen (Grundsatz entschieden 🟢, Umsetzung offen 🔴)

`-remotedata`/`vsect remote` ist seit 08.09.2026 projektweiter Standard und adressiert `a6`-relativ. Im Treiberkontext ist `a6` aber der System-Global-Pointer des Kernels (s. 1.a), nicht die private `vsect remote`-Basis eines `PROG`-Moduls. Grundsatzentscheidung: innerhalb von `driver`/`interrupt`/`trap`/`naked`-Funktionen wird für **alle** Globals (auch `const`/Pointer-Globals mit `GINITADDR`) PC-relative Adressierung erzwungen, der `-remotedata`-Pfad dort deaktiviert. Die technische Umsetzung im Backend ist Teil von Phase 3/4, nicht von Phase 1.

**To-do:** eigener Arbeitsschritt in Phase 3 (Backend-Erkennung „Funktion hat Calling-Convention ≠ Standard → PC-relativ statt `-remotedata`") plus negativer Testfall (`const char*` in einer `driver`-Funktion referenziert).

### 1.d Entscheidungen

**IR-Syntaxform — entschieden 2026-09-22 (Andreas):** Codex' Schlüsselwortform
(`MODHEADER name=cfide type=driver subtype=rbf attr=0x8000 edition=1 stack=0`
plus eigener `ENTRY`-Opcode) statt der ursprünglichen Positionssyntax —
Begründung: "kann sich ja keiner merken in welcher Reihenfolge das muss".
Reihenfolge der Schlüssel-Wert-Paare ist damit beliebig. `DISPATCHTAB`
bleibt bewusst positional (physische OS-9-Sprungtabellen-Offsets, kein
IR-Notationsdetail) — **nicht** vertauschbar, unabhängig von dieser
Entscheidung. Bereits umgesetzt in `OS9_SYSTEM_INTERFACE.md` und
`IR_OPCODES_de.md` (inklusive der Korrekturen aus 1.a/1.b).

**Namensraum der Calling-Convention-Keywords — entschieden 2026-09-22
(Andreas):** weder bare Keywords noch reines `__`-Präfix, sondern eine
dritte, von Andreas vorgeschlagene Variante — genau **ein** neues
Schlüsselwort `modul`, dem `driver`/`interrupt`/`trap`/`naked`
unmittelbar folgen müssen, um als Calling-Convention erkannt zu werden
(`modul driver int32_t drv_read(...) { ... }`). Löst die Kollisionslücke
auf, die reines kontextgebundenes B noch gehabt hätte (eine Funktion
namens `driver` ohne vorangestelltes `modul` bleibt unberührt), ohne vier
neue reservierte Wörter einzuführen wie bei Variante A. Bereits
eingetragen in `OS9_SYSTEM_INTERFACE.md` Abschnitt 4. Offen für die
Grammatikarbeit in Phase 1: genaue Position von `modul` relativ zu
`static`/`extern`. **Nachtrag 22.09.2026:** Syscalls wandern doch unter
`modul` (`modul syscall(trapnr, archetyp) ...`, s. `OS9_SYSTEM_INTERFACE.md`
Abschnitt 5) statt bei `__syscall` zu bleiben -- einheitlicher Mechanismus
für die ganze Spracherweiterung. C-Parameter werden der Reihe nach den
Registerslots des Archetyps zugeordnet, Compiler prueft Anzahl und Typ je
Slot. Registernamen selbst (`d0`/`a0`/...) sind keine reservierten
Woerter, nur Parameterpositionen zaehlen.

**Backend-Abdeckung nicht-68k-Ziele — entschieden 2026-09-22:** Wichtige
Erkenntnis aus der Diskussion: Ziel-Betriebssystem (Q9/OS-9) und
CPU-Backend (68k/ARM64/...) sind zwei unabhängige Achsen, keine an die
andere gekoppelt. Ein künftiges bare-metal-Q9-auf-ARM64 bräuchte dieselbe
Modul-Maschinerie wie heute 68k. Deshalb: ARM64- und C-Backend lehnen
`MODHEADER`/`ENTRY`/`DISPATCHTAB`/`INLINEASM`/`ORG`/`SECTION` mit klarer
Fehlermeldung als **„noch nicht implementiert"** ab (nicht als
grundsätzlich ausgeschlossen). QCCVM ignoriert nur die Modul-/Dispatch-
Metadaten, interpretiert den reinen Funktionsrumpf aber normal weiter,
damit die C-Logik von Treiberfunktionen vorab am Orakel testbar bleibt —
Begründung: kurzfristig ist fast alle Arbeit Q9-Code, ohne diesen
Mittelweg verlöre genau der aktuell wichtigste Code die
Orakel-Absicherung. Bereits eingetragen in `IR_OPCODES_de.md`.

### 1.e `#DEFOS` — Ziel-Betriebssystem als eigene Direktive (entschieden 2026-09-22)

Ursprünglich als Kommandozeilenschalter (Erweiterung des bestehenden
68k-Backend-Schalters `-os9`) angedacht, dann als `#DEFMODUL`-Subkommando
— am Ende als **eigene, `#DEFMODUL` vorgeschaltete Direktive** entschieden,
weil eine Modul-Eigenschaft unter vielen die falsche Ebene gewesen wäre:
`#DEFOS` steckt den Rahmen ab, in dem `#DEFMODUL` und die
Calling-Convention-Keywords überhaupt gelten.

```c
#DEFOS Q9               // oder: OS9 — Default Q9, wenn weggelassen

#DEFMODUL NAME cfide
#DEFMODUL TYPE DRIVER rbf
```

Muss vor dem ersten `#DEFMODUL` einer Datei stehen. Bei `TYPE NOOS`
wirkungslos (informativ). `Q9` und `OS9` sind heute vermutlich
byteidentisch (Q9 ist bewusst ABI-kompatibel zu echtem Microware-OS-9) —
die Unterscheidung ist ein Zukunfts-Haken. Bereits eingetragen in
`OS9_SYSTEM_INTERFACE.md` Abschnitt 2.

**Abnahmekriterium (gesamte Phase 1):** für jede Entscheidung — inklusive 1.a/1.b/1.c — existiert mindestens ein positives und ein negatives Beispiel mit erwarteter IR bzw. erwarteter Diagnose. Für `#DEFOS`/Backend-Abdeckung zusätzlich: ein Testfall pro Backend (68k/ARM64/C-Backend/QCCVM), der das jeweils erwartete Verhalten zeigt.

---

## Phase 2 — Minimaler IR-Metadatenpfad

Nur implementieren: `MODHEADER`, Entry-Zuordnung (Form gemäß 1.d), `DISPATCHTAB`, `FUNC ... [convention]`. Noch **nicht**: `ORG`, `SECTION`, `ALIGN`, `INLINEASM`, `modul syscall`.

| Teilschritt | Status | Anmerkung |
|---|:---:|---|
| Scanner-Erkennung `#DEFMODUL` in `q9-qcpp` | 🟢 | Bereits vorhanden (`edcd65a`), reines Passthrough |
| Scanner-Erkennung `#DEFOS` in `q9-qcpp` | 🔴 | Neu seit 1.e, noch nicht implementiert — muss vor `#DEFMODUL` geprüft werden können |
| Semantische Verarbeitung im C-Frontend/`q9-qcir` | 🔴 | Muss Moduldefinition, Funktionen und Symbole tatsächlich zuordnen, nicht nur durchreichen |
| IR-Validierung (Pflichtfelder, unbekannte Metadaten, Kollision mit bestehendem `FUNC`-Format) | 🔴 | |
| Backend-Aufnahme der neuen Opcodes (68k: implementieren; ARM64/C-Backend: klare „noch nicht implementiert"-Ablehnung; QCCVM: Metadaten ignorieren, Funktionsrumpf interpretieren) | 🔴 | gemäß 1.d |

**Abnahmekriterium:** eine minimale IR mit Modulheader und einer Funktion wird akzeptiert; dieselbe IR ohne Pflichtargumente wird verständlich abgelehnt.

---

## Phase 3 — Einfaches `PROG`-Modul

```c
#DEFMODUL NAME testprog
#DEFMODUL TYPE PROG main

int main(void) {
    return 0;
}
```

| Teilschritt | Status |
|---|:---:|
| `MODHEADER`-Handler im Backend (`psect`, Typ/Sprache, Attr/Rev, Stack) | 🔴 |
| Entry-Offset korrekt | 🔴 |
| Header-Parität, CRC (`ql68k`, Mechanismus existiert bereits — nur neue Modultypen verifizieren) | 🔴 |
| PC-relative Zwangsadressierung greift NICHT bei `PROG` (normales `-remotedata`-Verhalten bleibt Standard) | 🔴 |
| Bestehende Bibliotheks-/Startup-Regeln unverändert | 🔴 |

**Abnahmekriterium:** das erzeugte Modul wird von `ident` bzw. dem Zielsystem akzeptiert und ist lauffähig.

---

## Phase 4 — Minimaler `DRIVER`-Pfad

Erster Schritt bewusst ohne automatische Default-Einsprünge (Codex' Empfehlung, übernommen) — explizite Zuordnung, korrigiert um den fehlenden `trap`-Slot aus 1.b:

```c
#DEFMODUL TYPE DRIVER rbf
#DEFMODUL ENTRY init    drv_init
#DEFMODUL ENTRY read    drv_read
#DEFMODUL ENTRY write   drv_write
#DEFMODUL ENTRY getstat drv_getstat
#DEFMODUL ENTRY setstat drv_setstat
#DEFMODUL ENTRY term    drv_term
#DEFMODUL ENTRY trap    0
```

(Bezeichnung `setstat` statt `putstat` an die offizielle Microware-Terminologie angeglichen — reine Namenskonsistenz, keine funktionale Änderung.)

| Teilschritt | Status |
|---|:---:|
| Dispatch-Reihenfolge, 7 relative Offsets inkl. `trap` | 🔴 |
| Treiber-Prolog mit korrigierter Register-ABI (`a1`=Path-Desc, `a2`=Static-Storage) | 🔴 |
| Rückgabe/Carry-Konvention | 🔴 |
| PC-relative Zwangsadressierung für Globals in Treiberfunktionen (aus 1.c) | 🔴 |
| Kein versehentliches `cstart.r` | 🔴 |
| Linker-Layout für Treibermodule (`isDrvr`-Zweig in `ql68k`) | 🔴 |
| Automatische Default-Einsprünge (Komfortsyntax aus ursprünglicher Spec §2.1) | 🔴 — bewusst erst NACH Phase 4 nachziehen |

**Abnahmekriterium:** ein Referenztreiber hat die erwartete 7-Wort-Dispatch-Tabelle, korrekte Headerwerte und eine gültige OS-9-Prüfsumme.

---

## Phase 5 — Weitere Calling-Conventions

Reihenfolge: `interrupt` → `trap` → `naked`. Jede Convention bekommt einen eigenen Prolog-/Epilog-Test. Bei `naked` muss vorher feststehen, welche Rückkehr erlaubt ist und ob bestimmte C-Konstrukte im Funktionsrumpf verboten werden müssen.

| Teilschritt | Status |
|---|:---:|
| `interrupt`: `movem.l`-Prolog/-Epilog, Abschluss mit `rte` | 🔴 |
| `trap`: Trap-Frame-Behandlung, Abschluss mit `rte` | 🔴 |
| `naked`: kein Prolog/Epilog, Entwickler steuert Rücksprung selbst | 🔴 |
| `modul`-Präfix-Grammatik umgesetzt (gemäß Entscheidung aus 1.d) | 🔴 |

---

## Phase 6 — Adressplatzierung

| Teilschritt | Status |
|---|:---:|
| Scanner-Passthrough `#ORG`/`#SECTION` in `q9-qcpp` | 🟢 |
| Semantische Verarbeitung (`ALIGN`, Ziel-Offsets, Multi-Regionen) | 🔴 |
| Assembler-/ROF-Layout-Auswirkung | 🔴 |
| `NOOS`/Bare-Metal-Ausgabe (kein Modulkopf, keine CRC) | 🔴 |

Betrifft nicht nur die IR, sondern auch Assembler, ROF-Layout, Linker und Symboladressen — bewusst nicht als kleine Präprozessor-Erweiterung behandeln (Codex' Einwand, übernommen).

---

## Phase 7 — `modul syscall`

Zunächst nur wenige konkret definierte Syscalls, pro Syscall eine eigene Registrierung statt nur die drei Archetypen:

| Angabe je Syscall | Status |
|---|:---:|
| Trap-Nummer | 🔴 |
| Registerzuordnung | 🔴 |
| Parameterbreite | 🔴 |
| Rückgaberegister | 🔴 |
| Fehlerkonvention | 🔴 |
| zerstörte Register | 🔴 |
| zulässige C-Signatur | 🔴 |

`CALL_D`/`CALL_DA`/`CALL_FORK` bleiben Codegen-Muster, ersetzen aber nicht die Einzelbeschreibung pro Syscall.

---

## Phase 8 — Inline-Assembler

| Teilschritt | Status |
|---|:---:|
| Präprozessor-Marker `#asm`/`#ASM`/`#endasm`/`#ENDASM` (Groß-/Kleinschreibung) | 🟢 bereits vorhanden |
| Umstellung von Text-Passthrough auf zeilenweise `INLINEASM`-Emission im `qcir`-Parser | 🔴 |
| Syntax-/Kommentarregeln, Gültigkeitsbereiche, Symbol-/Variablenreferenzen (`%val`), Register-Clobbers, Labels festlegen | 🔴 |
| Fehlerverhalten bei falscher Zielarchitektur | 🔴 |
| Backend-Ausgabe 1:1 in `.s68` | 🔴 |

Aufwand geringer als ursprünglich veranschlagt, da der Marker-Mechanismus wiederverwendet werden kann statt neu gebaut zu werden — Reihenfolge (zuletzt) bleibt trotzdem wie in Codex' Plan begründet.

---

## Minimaler Testsatz

Aus Codex' Vorschlag übernommen, um die drei neuen Punkte ergänzt (markiert):

1. normale Programmübersetzung ohne neue Direktiven
2. `PROG` mit Standardwerten
3. `PROG` mit explizitem Entry und Stack
4. `DRIVER` mit vollständiger **7-Wort**-Dispatch-Tabelle inkl. `trap` *(ergänzt)*
5. fehlendes Dispatch-Symbol
6. falsche Treiberfunktion-Signatur
7. falscher Modul- oder Subtyp
8. doppelte `#DEFMODUL`-Angabe
9. unbekanntes Subkommando
10. CRC-/Header-Prüfung des erzeugten Moduls
11. falscher Einsatz eines zielabhängigen Opcodes im VM-/C-Backend (muss abgelehnt werden, nicht verworfen)
12. Regressionstest für einen normalen Nicht-Modul-Build
13. `const`/Pointer-Global in einer `driver`-Funktion referenziert — PC-relative statt `-remotedata`-Adressierung erwartet *(ergänzt)*
14. Calling-Convention-Keyword als gewöhnlicher Identifier in portiertem C89-Code — darf nicht brechen *(ergänzt)*

---

## Ausstehende Dokumentänderungen

**Erledigt (2026-09-22):** `a1`/`a2`-Tausch + `a5` (1.a), 7-Wort-Dispatch-Tabelle inkl. `trap` (1.b), Schlüsselwortform inkl. `ENTRY`-Opcode (1.d), `modul`-Präfix für Calling-Convention-Keywords (1.d), `#DEFOS`-Direktive (1.e) und QCCVM-/ARM64-/C-Backend-Verhalten (1.d) sind in `OS9_SYSTEM_INTERFACE.md` und `IR_OPCODES_de.md` eingetragen. Damit ist Phase 1 inhaltlich vollständig.

**`OS9_SYSTEM_INTERFACE.md`, noch offen:**
- Neuer Absatz zur PC-relativen Zwangsadressierung in Calling-Convention-Funktionen (1.c) — bisher nur als Grundsatz in `STATUS_DEFMODUL.md`, noch nicht in der Spec selbst ausformuliert
- genaue Grammatik-Position von `modul` relativ zu `static`/`extern` (offen für Phase-1-Grammatikarbeit, `Data/qcc.ebnf`)
- toten Verweis auf `docs/ARCHITEKTUR.md` entfernen oder Datei für Q9-QCC nachziehen

**`IR_OPCODES_de.md`, noch offen:**
- Argumentanzahl, Wertebereiche und Position im Stream je neuem Opcode ergänzen (Codex §2.2)
- toten Verweis auf `docs/ARCHITEKTUR.md` entfernen oder Datei für Q9-QCC nachziehen

---

## Quellen

- `PLAN_REVIEW_DEFMODUL.md` (Codex) — Phasenplan, IR-Spezifikationsdisziplin, Backend-Abdeckung, Testkatalog
- `PLAN_REVIEW_ERGAENZUNG_DEFMODUL.md` (Claude) — Register-ABI-Auflösung, Dispatch-Tabellen-Korrektur, `-remotedata`/`a6`-Lücke, Keyword-Kollisionsrisiko
- `MWOS/DOC/PDF/68k_techio.pdf` (offizielles Microware-Handbuch, „OS-9 for 68K Processors Technical I/O Manual")
- `Q9-Flux/Q9-Flux-68kQEMU/docs/HOSTFS_MANAGER.md`, `Q9-Flux-68k/docs/ARBEITSPLAN_de.md` (realer `ss_gdp.a`-Fund)
