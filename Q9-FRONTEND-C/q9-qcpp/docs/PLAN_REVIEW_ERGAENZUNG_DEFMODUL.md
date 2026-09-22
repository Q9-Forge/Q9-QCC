# Ergänzung zum Plan-Review: `QCC-DEFMODUL` (Claud Ada)

Stand: 2026-09-22
Branch: `QCC-DEFMODUL`
Bezug: `PLAN_REVIEW_DEFMODUL.md` (dieselbe Datei, selber Ordner)

Dieses Dokument dupliziert `PLAN_REVIEW_DEFMODUL.md` nicht, sondern ergänzt
es. Dessen Phasenplan (ABI/IR erst einfrieren, dann ein minimaler
`PROG`-/`DRIVER`-Pfad, danach erst Syscalls/Adressplatzierung/Inline-Assembler)
halte ich für den richtigen Umbau und würde ihn nicht ändern. Was fehlt: die
konkrete Antwort auf die Fragen, die dieser Plan bewusst offen lässt, plus
vier Punkte, die er nicht anspricht.

## 1. Die offengelassene `a1`/`a2`-Frage ist entscheidbar

`PLAN_REVIEW_DEFMODUL.md` Abschnitt 2.1 benennt den Widerspruch
("Static-Storage derzeit über `a1`... bestehender Linker-Code nennt `a2`"),
aber nicht die Auflösung. Sie lässt sich mit drei unabhängigen Quellen klären,
die alle übereinstimmen:

| Quelle | Aussage |
|---|---|
| `MWOS/DOC/PDF/68k_techio.pdf`, S. 143 (GETSTAT/SETSTAT), S. 149 (INIT), S. 154 (READ) | `(a1) = address of the path descriptor` / `(a2) = address of the device static storage area` |
| `Q9-Flux/Q9-Flux-68kQEMU/docs/HOSTFS_MANAGER.md`, Z. 69f. | „a1 points to the Path Descriptor... a6 to the system global area" |
| `Q9-Flux-68k/docs/ARBEITSPLAN_de.md` (realer `ss_gdp.a`-Fund vom historischen Disk-Image) | „A1=Pfad-Deskriptor (aus D0)" |

**Ergebnis: `a1` = Path-Descriptor, `a2` = Device-Static-Storage — die
aktuelle `OS9_SYSTEM_INTERFACE.md` (§4.1) hat es genau andersherum.** Das
ist kein „Detail, das noch geklärt werden muss", sondern schon jetzt mit dem
offiziellen Microware-Handbuch widerlegbar. Sollte direkt in Phase 1 als
erledigt eingetragen werden, ohne eigene Recherche zu wiederholen.

Zusätzlich, aus derselben Handbuchstelle: `a4` = Process-Descriptor, `a5` =
Caller-Register-Stack-Pointer, `a6` = System-Global-Data-Storage-Pointer —
das deckt sich mit der bestehenden Spec und mit Punkt 3 unten.

## 2. Die Standard-Dispatch-Tabelle hat nur 6 statt 7 Einträge

Nicht in `PLAN_REVIEW_DEFMODUL.md` erwähnt. Laut Handbuch (`68k_techio.pdf`,
Abschnitt "Random Block File Manager"): *"The execution offset address in
the module header points to a branch table with **seven** entries"* —
`INIT, READ, WRITE, GETSTAT, SETSTAT, TERM, TRAP`. Sowohl
`OS9_SYSTEM_INTERFACE.md` (§2.1) als auch das `DISPATCHTAB`-Beispiel in
§7 listen nur sechs Namen (`drv_init drv_read drv_write drv_getstat
drv_putstat drv_term`) — der siebte Slot `TRAP` fehlt komplett. Laut
Handbuch darf er auf 0 zeigen, muss als Tabelleneintrag aber vorhanden sein
("to ensure future compatibility"); ohne ihn verschiebt sich die reale
Tabellenlänge gegen das, was IOMan erwartet. Sollte in Phase 1 (ABI-Tabelle)
und Phase 4 (Referenztreiber-Test) als Pflichtfeld ergänzt werden — ein
guter Kandidat für genau die "positiven/negativen Beispiele" aus
`PLAN_REVIEW_DEFMODUL.md` Abschnitt 3, Phase 1.

## 3. `-remotedata`/`vsect remote` ist mit dem Treiber-`a6` nicht abgeglichen

Weder die ursprünglichen drei Spezifikationsdokus noch
`PLAN_REVIEW_DEFMODUL.md` erwähnen `-remotedata`. Das ist seit 08.09.2026
projektweiter Standard: vollständig genullte UND (seit 15.09., initialisierte
Zeiger) auch initialisierte Pointer-Globals wandern in `vsect remote` und
werden **`a6`-relativ** adressiert (`movea.l #sym,reg` / `adda.l a6,reg`).
Im Treiberkontext ist `a6` aber der System-Global-Data-Storage-Pointer des
Kernels (siehe Punkt 1) — ein völlig anderer Zeiger als die private
`vsect remote`-Basis eines normalen `PROG`-Moduls.

`OS9_SYSTEM_INTERFACE.md` §3.3 sagt zwar richtig, dass `const`-Daten
"PC-relativ adressiert" werden sollen, behandelt das aber als Selbstverständ-
lichkeit statt als Implementierungsauftrag. Ohne expliziten Backend-Schalter
würde der bestehende `-remotedata`-Codepfad (Standardverhalten der ganzen
Kette) genau diese `const`/Pointer-Globals fälschlich `a6`-relativ gegen den
Kernel-Zeiger adressieren, sobald sie in einer `driver`/`interrupt`/`trap`/
`naked`-Funktion referenziert werden.

**Konkreter Vorschlag für Phase 1/2 des überarbeiteten Plans:** eine eigene
Zeile "Globals-Adressierung in Modul-Funktionen mit Calling-Convention ≠
Standard: erzwungen PC-relativ statt `-remotedata`, inklusive `GINITADDR`-
Pointer-Globals" — mit einem eigenen negativen Testfall (ein `const char*`
in einer `driver`-Funktion referenziert, erwartete Adressierungsart im
erzeugten Assembler geprüft).

## 4. Bare Keywords `driver`/`interrupt`/`trap`/`naked` kollidieren mit realem C89-Korpus

Ebenfalls nicht in `PLAN_REVIEW_DEFMODUL.md`. Die Spezifikation führt
`__syscall(...)` bewusst in einem kollisionssicheren Namensraum ein, aber
`driver`, `interrupt`, `trap` und `naked` als einfache, uneingeschränkte
Bezeichner. Im MWOS-Korpus (historischer OS-9-C89-Code, den dieses Projekt
selbst als Testkorpus nutzt) ist es plausibel, dass einer dieser Namen schon
als gewöhnlicher Identifier auftaucht (Struct-Feld, Parametername,
Funktionsname). Das würde bestehenden, bisher kompilierbaren Code brechen.

**Vorschlag:** entweder dieselbe Namenskonvention wie `__syscall` verwenden
(`__driver`/`__interrupt`/`__trap`/`__naked`) oder in der ABI-Spezifikation
(Phase 1) explizit festlegen, dass diese vier Wörter nur unmittelbar vor
einem Funktionsrückgabetyp am Anfang einer Top-Level-Deklaration als
Schlüsselwort erkannt werden (kontextabhängige Erkennung, kein globales
Schlüsselwort-Reservat). Passt als zusätzliches Kriterium in den Abschnitt
"IR als formale Schnittstelle" bzw. in die dortige Grammatik-Entscheidung.

## 5. Kleinkorrektur: toter Querverweis

`IR_OPCODES_de.md` und `OS9_SYSTEM_INTERFACE.md` verweisen beide auf
`docs/ARCHITEKTUR.md` Abschnitt 10. Diese Datei existiert in `Q9-QCC/docs/`
nicht (nur in `Q9-PARSEC`, `Q9-RUN`, `Q9-FRONTEND-Quant` — andere Projekte
im selben Forge-Baum). Entweder Verweis entfernen oder die Datei für
Q9-QCC nachziehen.

## 6. Eine Ergänzung zu Phase 8 des bestehenden Plans

`PLAN_REVIEW_DEFMODUL.md` Phase 8 (Inline-Assembler) plant `INLINEASM`
komplett neu inklusive Syntaxdefinition. Wert ist: `#asm`/`#ASM` und
`#endasm`/`#ENDASM` (case-insensitiv) sind in `q9-qcpp/src/qcpp.c` bereits
seit längerem vollständig implementiert (Marker-basiertes Passthrough,
`emitAsmMarker`, Schalter `-asm-strip`). Der aktuelle
`#DEFMODUL`/`#ORG`/`#SECTION`-Commit (`edcd65a`) hat diesen Mechanismus für
die neue Großschreibung bereits mitverwendet. Phase 8 sollte deshalb eher
als "bestehenden Marker-Mechanismus von Text-Passthrough auf zeilenweise
`INLINEASM`-Emission umstellen" formuliert werden statt als Neuentwicklung
— reduziert den in Phase 8 veranschlagten Aufwand spürbar, ändert aber
nichts an der Reihenfolge (weiterhin zuletzt, wie im bestehenden Plan
begründet).

## Zusammenfassung: was ich am Plan ändern würde

Die Struktur von `PLAN_REVIEW_DEFMODUL.md` bleibt. Konkret in Phase 1
einzutragen, bevor Phase 2 beginnt:

1. `a1` = Path-Descriptor, `a2` = Device-Static-Storage (belegt, nicht mehr
   offen).
2. Dispatch-Tabelle für `DRIVER` hat 7 Einträge, nicht 6 — `TRAP` ergänzen.
3. Globals-Adressierung innerhalb von Calling-Convention-Funktionen: PC-
   relativ erzwingen, `-remotedata`-Standardpfad dort explizit abschalten.
4. `driver`/`interrupt`/`trap`/`naked` entweder umbenennen (`__`-Präfix)
   oder Erkennung auf Deklarationsanfang beschränken.

Punkt 5 und 6 sind Aufräumarbeiten ohne Risiko, können jederzeit nebenbei
erledigt werden.
