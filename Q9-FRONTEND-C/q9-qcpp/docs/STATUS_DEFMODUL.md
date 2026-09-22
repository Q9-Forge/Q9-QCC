# Q9 MODUL Erweiterung

Status-Übersicht und Arbeitsplan für die Implementierung nativer OS-9 Modulartefakte (`#DEFMODUL`, Dispatch-Tabellen, Calling-Conventions, `#ASM`) in Q9-QCC.

Stand: 2026-09-22. Diese Fassung ersetzt die ursprüngliche flache Paketliste durch den Phasenplan aus den Reviews von Codex und Claude (`PLAN_REVIEW_DEFMODUL.md`/`PLAN_REVIEW_ERGAENZUNG_DEFMODUL.md`, beide nach vollständiger Einarbeitung gelöscht, s. Abschnitt "Quellen") und trägt deren Ergebnisse konkret ein.

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
| **0. Baseline** | Reproduzierbare Ausgangsbasis | 🟢 | Alle fünf Komponenten gebaut+gehasht, `smoke.c`-Golden-Test durch die Kette, Header-/CRC-Tests gesammelt, `gdp.a` als Referenztreiber gefunden |
| **1. ABI & IR-Entscheidungen** | Keine Backend-Arbeit auf Annahmen | 🟢 | Register-ABI, Dispatch-Tabelle, IR-Syntaxform, Keyword-Namensraum, `#DEFOS` und Backend-Abdeckung entschieden (s. u.) |
| **2. Minimaler IR-Metadatenpfad** | Metadaten sicher bis zum 68k-Backend | 🟡 | `MODHEADER`/`ENTRY` werden jetzt emittiert (mit Defaults, an tatsächliches `#DEFOS`/`#DEFMODUL`-Vorkommen gekoppelt); IR-Validierung und Backend-Aufnahme noch offen |
| **3. Einfaches `PROG`-Modul** | Einfachster Modultyp ohne Treiber-ABI | 🔴 | Referenz-MVP, danach erst Treiber |
| **4. Minimaler `DRIVER`-Pfad** | Ein Treibertyp, explizite Dispatch-Tabelle | 🔴 | Keine automatische Default-Magie im ersten Schritt |
| **5. Weitere Calling-Conventions** | `interrupt`, `trap`, `naked` | 🔴 | In dieser Reihenfolge, je eigener Prolog-/Epilog-Test |
| **6. Adressplatzierung** | `ALIGN`, `SECTION`, `ORG`, `NOOS` | 🟡 | Scanner-Passthrough für `#ORG`/`#SECTION` existiert; Semantik/Backend offen |
| **7. `modul syscall`** | Wenige konkrete Syscalls zuerst | 🔴 | Pro Syscall eigene Registrierung statt Archetyp allein; Syntax entschieden 22.09. (s. `OS9_SYSTEM_INTERFACE.md` Abschnitt 5) |
| **8. Inline-Assembler** | `#ASM`/`#ENDASM` → `INLINEASM` | 🟡 | Präprozessor-Marker existiert bereits 🟢; `qcir`-Parser & IR-Emission offen 🔴 |

---

## Phase 0 — Baseline einfrieren (abgeschlossen 2026-09-22) 🟢

- [x] **Build dokumentiert** — alle fünf Komponenten clean gebaut gegen Branch-Commit `fc78f7f`, Apple clang 21.0.0 (arm64-apple-darwin25.6.0), nur Warnungen, keine Fehler:

  | Komponente | Binärgröße | SHA-256 |
  |---|---:|---|
  | `q9-qcpp/build/qcpp` | 76.088 B | `025827...58d9` |
  | `q9-qcir/build/qcir` | 422.136 B | `16e23c...80d3d` |
  | `q9-qir68k/build/qir68k` | 105.160 B | `27cc1f...128b5` |
  | `q9-qr68k/build/qr68k` | 155.544 B | `f09b40...d9920` |
  | `q9-ql68k/build/ql68k` | 72.472 B | `4c0806...93d25` |

- [x] **Golden-Test gesichert** — `Q9-QCC/tests/smoke.c` (`int main(){ putint(42); }`, bereits bestehende Testquelle, keine neue Direktive) durch die volle Kette `qcpp → qcir → qir68k -os9 → qr68k` geführt, jede Stufe erfolgreich (rc=0), Hashes der Zwischenstufen festgehalten (nicht als Binärartefakte committet — bei Bedarf mit denselben Befehlen und demselben Toolchain-Commit exakt reproduzierbar):
  - `smoke.i` (qcpp) byteidentisch zu `smoke.c` (keine Makros/Includes in diesem Minimalfall)
  - `smoke.ir` (qcir): `FUNC main 0 0 / PUSH 42 / PRINT / PUSH 0 / RET / ENDFUNC`, Schlusswort `OK`
  - `smoke.s68` (qir68k -os9): 228 Zeilen Assembler
  - `smoke.r` (qr68k): 1.261 Byte ROF
  - Volles Linken zu einem lauffähigen Modul (ql68k gegen `clib.l`/`sys.l`) bewusst **nicht** Teil dieser Baseline — das deckt die bestehende Testinfrastruktur (`tools/test_short_68k.sh` u. ä., läuft gegen die reale Microware-Toolchain im DOS-Emulator) bereits ab; für die reine "bleibt reproduzierbar"-Frage reicht die Kette bis zum ROF.
- [x] **Bestehende Modul-Header-/CRC-Tests in `ql68k` gesammelt** — drei einschlägige Skripte in `Q9-BACKEND-68K/q9-ql68k/tests/`:
  - `difftest.sh` — dieselbe ROF-Datei durch `ql68k` und echtes `l68`, byteweiser Vergleich
  - `optstest.sh` — Differenztest für alle modulverändernden `l68`-Schalter
  - `sdkdiff.sh` — Differenztest über den ganzen SDK-Korpus mit dessen eigenen realen Aufrufen
- [x] **Realer Referenztreiber gefunden** — `gdp.a` (`/Volumes/SSD1TB/projects/MWOS/OS9/68030/PORTS/Q9/SCF/gdp.a`, 15.576 Byte, eigenes Q9-Forge-Copyright): SCF-Framebuffer-Treiber, GetStt/SetStt-basiert, laut Kopfkommentar gegen das reale historische Microware/VCS-GDP-Protokoll verifiziert (derselbe Disk-Image-Fund, der auch die `a1`=Path-Descriptor-Korrektur in 1.a belegt) und bereits erfolgreich gegen echte `sys.l`/`scfstat.l` assembliert+gelinkt. Eignet sich als Strukturvergleich für Phase 4 (wie muss ein QCC-erzeugtes Treibermodul aussehen) — ist selbst Assembler, kein C, also kein direkter Kompilierungs-Testfall, aber eine belastbare Zielform.
- [x] **Uncommitteter Ausgangszustand gesichert** — Arbeitsbaum war zu Beginn von Phase 0 bereits sauber (`git status --short` leer), nichts zu sichern.

**Abnahmekriterium erfüllt:** der unveränderte normale Programm-Build (`smoke.c`) bleibt über alle vier lokal reproduzierbaren Kettenstufen hinweg erfolgreich und mit dokumentierten Hashes nachprüfbar.

---

## Phase 1 — ABI & IR-Entscheidungen abschließen

### 1.a Register-ABI (jetzt entschieden 🟢)

Ursprüngliche Spec (`OS9_SYSTEM_INTERFACE.md` §4.1) hatte `a1`/`a2` vertauscht. Aufgelöst mit drei unabhängigen Quellen (ursprünglich zusammengetragen im inzwischen gelöschten Review, s. "Quellen"): offizielles Microware-Handbuch `MWOS/DOC/PDF/68k_techio.pdf` (S. 143/149/154, für READ/GETSTAT/SETSTAT/INIT übereinstimmend), `HOSTFS_MANAGER.md`, realer `ss_gdp.a`-Fund.

| Register | Bedeutung | Quelle |
|---|---|---|
| `a1` | Path-Descriptor | Handbuch + 2 Projektquellen |
| `a2` | Device-Static-Storage | Handbuch + 2 Projektquellen |
| `a4` | Process-Descriptor | Handbuch, deckt sich mit ursprünglicher Spec |
| `a5` | Caller-Register-Stack-Pointer | Handbuch, in ursprünglicher Spec gefehlt — ergänzen |
| `a6` | System-Global-Data-Storage-Pointer | Handbuch, deckt sich mit ursprünglicher Spec |

**Erledigt:** in `OS9_SYSTEM_INTERFACE.md` §4.1 korrigiert (`a1`/`a2` getauscht, `a5` ergänzt).

**Noch offen aus Codex' Review §2.1, nicht vergessen:** welche Register
jede Calling-Convention **erhalten** muss und welche sie **zerstören**
darf (callee-saved vs. clobbered) — für `driver`/`interrupt`/`trap`/
`naked` einzeln festzulegen, bisher nirgends dokumentiert. Gehört in
Phase 3/4/5 vor den jeweiligen Backend-Codegen.

### 1.b Dispatch-Tabelle für `DRIVER` (jetzt entschieden 🟢)

Handbuch: *„branch table with **seven** entries"* — `INIT, READ, WRITE, GETSTAT, SETSTAT, TERM, TRAP`. Ursprüngliche Spec und Codex' Phase-4-Beispiel listen beide nur 6 Namen, `TRAP` fehlt in beiden. `TRAP` darf laut Handbuch auf 0 zeigen, muss als Tabellenslot aber vorhanden sein.

**To-do:** überall, wo die Standard-Dispatch-Liste auftaucht (`OS9_SYSTEM_INTERFACE.md` §2.1/§7, `IR_OPCODES_de.md`-Beispiel, Codex' Phase-4-Beispiel), einen siebten Eintrag `trap`/`drv_trap` (Default-Ziel: 0) ergänzen.

### 1.c Globals-Adressierung in Calling-Convention-Funktionen (Grundsatz + Spec-Text 🟢, Backend-Umsetzung offen 🔴)

`-remotedata`/`vsect remote` ist seit 08.09.2026 projektweiter Standard und adressiert `a6`-relativ. Im Treiberkontext ist `a6` aber der System-Global-Pointer des Kernels (s. 1.a), nicht die private `vsect remote`-Basis eines `PROG`-Moduls. Grundsatzentscheidung: innerhalb von `modul driver`/`interrupt`/`trap`/`naked`-Funktionen wird für **alle** Globals (auch `const`/Pointer-Globals mit `GINITADDR`) PC-relative Adressierung erzwungen, der `-remotedata`-Pfad dort deaktiviert. Seit 22.09.2026 als eigener Absatz in `OS9_SYSTEM_INTERFACE.md` Abschnitt 3, Punkt 3 ausformuliert (dabei nebenbei eine veraltete `a1`-Referenz in Punkt 2 auf das korrekte `a2` korrigiert). Die technische Umsetzung im Backend ist weiterhin Teil von Phase 3/4, nicht von Phase 1.

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
| Scanner-Erkennung `#DEFOS` in `q9-qcpp` | 🟢 | Erledigt 22.09.2026 (`62d2544`), reines Passthrough wie `#DEFMODUL` |
| `#DEFOS` im C-Frontend/`q9-qcir` geparst und registriert | 🟢 | Erledigt 22.09.2026 (`7e76535`), Name in `tcDefOsName` abgelegt, noch keine IR-Emission |
| `#DEFMODUL` im C-Frontend/`q9-qcir` geparst und registriert | 🟡 | `NAME`/`EDITION`/`STACK`/`ATTR`/`TYPE PROG`\|`SYSTEM`\|`NOOS` erledigt (Werte in `tcDefModul*`-Variablen, noch keine IR-Emission); `TYPE DRIVER`/`MANAGER`/`TRAPHANDLER` und `ORG`/`ALIGN` bewusst noch offen (Phase 4/6) — s. Architekturfund unten |
| IR-Validierung (Pflichtfelder, unbekannte Metadaten, Kollision mit bestehendem `FUNC`-Format) | 🔴 | |
| `MODHEADER`/`ENTRY`-IR-Emission im Frontend (Defaults gemäß Spec, gekoppelt an tatsächliches Vorkommen) | 🟢 | Erledigt 22.09.2026 (`73d0c66`) |
| Backend-Aufnahme der neuen Opcodes (68k: implementieren; ARM64/C-Backend: klare „noch nicht implementiert"-Ablehnung; QCCVM: Metadaten ignorieren, Funktionsrumpf interpretieren) | 🔴 | gemäß 1.d |

**Abnahmekriterium:** eine minimale IR mit Modulheader und einer Funktion wird akzeptiert; dieselbe IR ohne Pflichtargumente wird verständlich abgelehnt.

### Architekturfund (22.09.2026): die Grammatik kennt keine Zeilengrenzen

Bei der `#DEFMODUL`-Umsetzung gemessen, nicht nur vermutet: `q9-qcir`s Grammatik ist ein reiner Zeichenstrom-Parser, `ws()` behandelt Zeilenumbrüche exakt wie gewöhnlichen Leerraum. Ein **optionaler** abschließender Bezeichner in einer Regel (z. B. `"PROG" [ entry ]`, wörtlich wie in der ursprünglichen Spec) ist deshalb gefährlich: Bei `#DEFMODUL TYPE PROG` **ohne** Namen, gefolgt von `int main(void)...`, verschlingt die Regel gierig das `int` der nächsten Zeile als vermeintlichen Einsprungnamen — der Rest der Datei scheitert danach mit einem nichtssagenden `FAIL`. Reproduziert, verifiziert, behoben.

**Übernommene Regel für diesen Schritt:** der Einsprungname bei `TYPE PROG`/`TYPE NOOS` ist jetzt **verpflichtend**, nicht optional wie in der Spec (`PROG [entry]`, Default `main`). Die „Default anwenden, wenn weggelassen"-Logik aus der Spec gehört auf die Ebene der späteren `MODHEADER`-Emission (wo ein leerer `tcDefModulEntry`-Puffer einfach `main` einsetzt), nicht auf die Grammatikebene selbst.

**Für künftige Arbeit relevant, nicht nur für diesen Schritt:** jede optionale oder wiederholte (`{ ident }`) Bezeichnerliste am Ende einer `#DEFMODUL`-Regel trägt dasselbe Risiko, sobald ihr echter C-Code oder eine weitere bezeichnerartige Direktive folgen kann. Phase 4s aktueller Entwurf (ein `#DEFMODUL ENTRY <slot> <funcname>` pro Zeile, je genau zwei Pflicht-Bezeichner) umgeht die akute Form davon bereits von sich aus — sollte aber bei der Umsetzung bewusst so bleiben (keine Rückkehr zur ursprünglichen `TYPE DRIVER rbf my_init my_read ...`-Auto-Default-Form auf einer Zeile, ohne das Zeilengrenzen-Problem vorher grundsätzlich zu lösen, z. B. durch einen expliziten Endemarker, den `qcpp` beim Durchreichen anhängt).

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

Erster Schritt bewusst ohne automatische Default-Einsprünge (Codex' Empfehlung, übernommen) — explizite Zuordnung, korrigiert um den fehlenden `trap`-Slot aus 1.b. Die Ein-Zeile-pro-`ENTRY`-Form ist seit dem Architekturfund in Phase 2 (s. o.) nicht mehr nur eine Stilentscheidung, sondern vermeidet aktiv das dort gemessene Zeilengrenzen-Problem (je Zeile genau zwei Pflicht-Bezeichner, keine offene/optionale Liste) — bei der Umsetzung so beibehalten:

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

**Testmethode aus Codex' Review §2.1, gilt auch rückwirkend für `driver`
in Phase 4:** zu jeder Calling-Convention ein kleines, von Hand
geschriebenes Assembler-Golden-File als Referenz anlegen (erwarteter
Prolog/Epilog byteweise), gegen das der generierte Code verglichen wird —
nicht nur "läuft/läuft nicht", sondern exakter Soll-Ist-Vergleich.

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

**Erledigt (2026-09-22):** `a1`/`a2`-Tausch + `a5` (1.a, inkl. einer nachtraeglich gefundenen veralteten `a1`-Referenz in §3), 7-Wort-Dispatch-Tabelle inkl. `trap` (1.b), Schlüsselwortform inkl. `ENTRY`-Opcode (1.d), `modul`-Präfix für Calling-Convention-Keywords und Syscalls (1.d), `#DEFOS`-Direktive (1.e), QCCVM-/ARM64-/C-Backend-Verhalten (1.d), PC-relative Zwangsadressierung als Spec-Absatz (1.c), Grammatik-Position von `modul` relativ zu `static`/`extern` und die toten `ARCHITEKTUR.md`-Verweise sind in `OS9_SYSTEM_INTERFACE.md` und `IR_OPCODES_de.md` eingetragen bzw. entfernt. Damit ist Phase 1 vollständig — sowohl inhaltlich als auch in den Spec-Dokumenten selbst.

**`OS9_SYSTEM_INTERFACE.md`, noch offen (aus Codex' Review §4, bisher nicht nachgezogen):**
- Für jeden Modultyp eine vollständige Minimaldefinition ergänzen — `DRIVER` ist inzwischen detailliert (Register, Dispatch, Prolog/Epilog), `MANAGER`/`SYSTEM`/`TRAPHANDLER` haben bisher nur ein Kurzbeispiel in §2.2, keine eigene Tiefe wie `DRIVER` in §4.1

**`IR_OPCODES_de.md`, noch offen (reine Ausführungsarbeit, keine Entscheidung mehr):**
- Argumentanzahl, Wertebereiche und Position im Stream je neuem Opcode ergänzen (Codex §2.2)
- `ALIGN` als eigener IR-Opcode fehlt noch in der Opcode-Tabelle — `#DEFMODUL ALIGN`/`#ORG`/`#SECTION` sind als Quelldirektiven beschrieben, aber `ALIGN` hat (anders als inzwischen `ORG`/`SECTION`) noch keine eigene IR-Opcode-Zeile (Codex §4)

---

## Quellen

- `PLAN_REVIEW_DEFMODUL.md` (Codex) und `PLAN_REVIEW_ERGAENZUNG_DEFMODUL.md`
  (Claude) — beide Review-Dokumente vom 22.09.2026, nach vollständiger
  Einarbeitung ihres Inhalts in diese Datei sowie in `OS9_SYSTEM_INTERFACE.md`
  und `IR_OPCODES_de.md` am selben Tag gelöscht (Andreas' Entscheidung,
  keine Duplikate lebender Inhalte). Phasenplan, IR-Spezifikationsdisziplin,
  Backend-Abdeckung, Testkatalog, Register-ABI-Auflösung,
  Dispatch-Tabellen-Korrektur, `-remotedata`/`a6`-Lücke,
  Keyword-Kollisionsrisiko — alles oben eingearbeitet, nichts verloren.
- `MWOS/DOC/PDF/68k_techio.pdf` (offizielles Microware-Handbuch, „OS-9 for 68K Processors Technical I/O Manual")
- `Q9-Flux/Q9-Flux-68kQEMU/docs/HOSTFS_MANAGER.md`, `Q9-Flux-68k/docs/ARBEITSPLAN_de.md` (realer `ss_gdp.a`-Fund)
