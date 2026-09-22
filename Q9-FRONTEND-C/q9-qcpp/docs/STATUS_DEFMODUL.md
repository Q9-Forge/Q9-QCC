# Q9 MODUL Erweiterung

Status-Übersicht und Arbeitsplan für die Implementierung nativer OS-9 Modulartefakte (`#DEFMODUL`, Dispatch-Tabellen, Calling-Conventions, `#ASM`) in Q9-QCC.

Stand: 2026-09-22. Diese Fassung ersetzt die ursprüngliche flache Paketliste durch den Phasenplan aus `PLAN_REVIEW_DEFMODUL.md` (Codex) und trägt die dort offen gelassenen bzw. dort fehlenden Entscheidungen aus `PLAN_REVIEW_ERGAENZUNG_DEFMODUL.md` (Claude) als konkrete Ergebnisse ein. Beide Dokumente bleiben als Begründungstext stehen — hier steht nur, was daraus folgt.

**Leitsatz aus beiden Reviews, unverändert übernommen:** erst ABI und IR verbindlich machen, dann ein kleines `PROG`-/`DRIVER`-MVP durch die komplette Pipeline bringen, danach erst `__syscall`, Adressplatzierung und Inline-Assembler ergänzen.

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
| **1. ABI & IR-Entscheidungen** | Keine Backend-Arbeit auf Annahmen | 🟡 | Register-ABI und Dispatch-Tabelle jetzt entschieden (s. u.); IR-Syntaxform, Namensraum und Backend-Abdeckung noch offen |
| **2. Minimaler IR-Metadatenpfad** | Metadaten sicher bis zum 68k-Backend | 🔴 | Nur `MODHEADER`, Entry-Zuordnung, `DISPATCHTAB`, `FUNC ... [conv]` — kein `ORG`/`SECTION`/`ALIGN`/`INLINEASM`/`__syscall` |
| **3. Einfaches `PROG`-Modul** | Einfachster Modultyp ohne Treiber-ABI | 🔴 | Referenz-MVP, danach erst Treiber |
| **4. Minimaler `DRIVER`-Pfad** | Ein Treibertyp, explizite Dispatch-Tabelle | 🔴 | Keine automatische Default-Magie im ersten Schritt |
| **5. Weitere Calling-Conventions** | `interrupt`, `trap`, `naked` | 🔴 | In dieser Reihenfolge, je eigener Prolog-/Epilog-Test |
| **6. Adressplatzierung** | `ALIGN`, `SECTION`, `ORG`, `NOOS` | 🟡 | Scanner-Passthrough für `#ORG`/`#SECTION` existiert; Semantik/Backend offen |
| **7. `__syscall`** | Wenige konkrete Syscalls zuerst | 🔴 | Pro Syscall eigene Registrierung statt Archetyp allein |
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

### 1.d Noch offene Entscheidungen (Bestätigung durch Andreas nötig)

Diese drei Punkte wurden bewusst **nicht** selbstständig entschieden, weil sie Geschmacks-/Architekturfragen sind, keine Tatsachenkorrekturen:

- **IR-Syntaxform:** bestehende Positionssyntax (`MODHEADER cfide driver rbf 8000 1 0`) beibehalten, oder auf Codex' Schlüsselwortform (`MODHEADER name=cfide type=driver ...` plus eigener `ENTRY`-Opcode) wechseln? Codex selbst: „weniger wichtig als die eindeutige und dokumentierte Semantik" — muss aber vor Phase 2 feststehen, egal wie entschieden wird, inklusive der aus 1.a/1.b korrigierten Werte.
- **Namensraum der Calling-Convention-Keywords:** `driver`/`interrupt`/`trap`/`naked` als bare Keywords riskieren Kollision mit bestehenden Identifiern im MWOS-C89-Korpus. Empfehlung (nicht verbindlich): `__driver`/`__interrupt`/`__trap`/`__naked`, konsistent zu `__syscall`. Alternative: kontextgebundene Erkennung nur unmittelbar vor einem Funktionsrückgabetyp am Deklarationsanfang.
- **Backend-Abdeckung nicht-68k-Ziele:** QCCVM, C-Backend und ARM64 müssen `MODHEADER`/`DISPATCHTAB`/`INLINEASM`/`ORG`/`SECTION` je bewusst ablehnen (klare Fehlermeldung) oder gezielt unterstützen — dürfen sie nicht stillschweigend verwerfen. Festlegung pro Backend fehlt noch.

**Abnahmekriterium (gesamte Phase 1):** für jede Entscheidung — inklusive 1.a/1.b/1.c — existiert mindestens ein positives und ein negatives Beispiel mit erwarteter IR bzw. erwarteter Diagnose.

---

## Phase 2 — Minimaler IR-Metadatenpfad

Nur implementieren: `MODHEADER`, Entry-Zuordnung (Form gemäß 1.d), `DISPATCHTAB`, `FUNC ... [convention]`. Noch **nicht**: `ORG`, `SECTION`, `ALIGN`, `INLINEASM`, `__syscall`.

| Teilschritt | Status | Anmerkung |
|---|:---:|---|
| Scanner-Erkennung `#DEFMODUL` in `q9-qcpp` | 🟢 | Bereits vorhanden (`edcd65a`), reines Passthrough |
| Semantische Verarbeitung im C-Frontend/`q9-qcir` | 🔴 | Muss Moduldefinition, Funktionen und Symbole tatsächlich zuordnen, nicht nur durchreichen |
| IR-Validierung (Pflichtfelder, unbekannte Metadaten, Kollision mit bestehendem `FUNC`-Format) | 🔴 | |
| Backend-Aufnahme der neuen Opcodes | 🔴 | |

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
| Keyword-Namensraum gemäß Entscheidung aus 1.d umgesetzt | 🔴 |

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

## Phase 7 — `__syscall`

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

**`OS9_SYSTEM_INTERFACE.md`:**
- §4.1 `a1`/`a2` tauschen, `a5` ergänzen (1.a)
- §2.1/§7 Dispatch-Beispiele auf 7 Einträge inkl. `trap` erweitern (1.b)
- Neuer Absatz zur PC-relativen Zwangsadressierung in Calling-Convention-Funktionen (1.c)
- Entscheidung aus 1.d zum Keyword-Namensraum eintragen, sobald getroffen
- toten Verweis auf `docs/ARCHITEKTUR.md` entfernen oder Datei für Q9-QCC nachziehen

**`IR_OPCODES_de.md`:**
- IR-Syntaxform gemäß 1.d final dokumentieren (inkl. `ENTRY`, falls so entschieden)
- Argumentanzahl, Wertebereiche und Position im Stream je neuem Opcode ergänzen (Codex §2.2)
- VM-/C-Backend-/ARM64-Verhalten für die neuen, backend-only markierten Opcodes ausdrücklich dokumentieren
- toten Verweis auf `docs/ARCHITEKTUR.md` entfernen oder Datei für Q9-QCC nachziehen

---

## Quellen

- `PLAN_REVIEW_DEFMODUL.md` (Codex) — Phasenplan, IR-Spezifikationsdisziplin, Backend-Abdeckung, Testkatalog
- `PLAN_REVIEW_ERGAENZUNG_DEFMODUL.md` (Claude) — Register-ABI-Auflösung, Dispatch-Tabellen-Korrektur, `-remotedata`/`a6`-Lücke, Keyword-Kollisionsrisiko
- `MWOS/DOC/PDF/68k_techio.pdf` (offizielles Microware-Handbuch, „OS-9 for 68K Processors Technical I/O Manual")
- `Q9-Flux/Q9-Flux-68kQEMU/docs/HOSTFS_MANAGER.md`, `Q9-Flux-68k/docs/ARBEITSPLAN_de.md` (realer `ss_gdp.a`-Fund)
