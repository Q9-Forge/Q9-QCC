# Review und überarbeiteter Implementierungsplan für `QCC-DEFMODUL`

Stand: 2026-09-22  
Branch: `QCC-DEFMODUL`  
Projekt: Q9-QCC

Dieses Dokument ist ein Vorab-Review der folgenden Spezifikationen:

- `OS9_SYSTEM_INTERFACE.md`
- `IR_OPCODES_de.md`
- `STATUS_DEFMODUL.md`

Es beschreibt außerdem, wie ich die Umsetzung strukturieren würde, damit zuerst
ein kleiner, überprüfbarer Kern entsteht und spätere Erweiterungen die
Compiler-Pipeline nicht erneut grundlegend verändern müssen.

## 1. Gesamtbewertung

Die grundsätzliche Richtung ist sinnvoll: Modulmetadaten, OS-9-Calling-
Conventions, Syscalls und Inline-Assembler sollen über eine Zwischenrepräsentation
in die Backends gelangen. Die vorhandene Aufteilung in Systemspezifikation,
IR-Referenz und Status-/Arbeitsplan ist ebenfalls geeignet.

Der bisherige Plan ist als Gesamtpaket jedoch noch zu breit und an einigen
Stellen nicht implementierungsreif. Vor dem eigentlichen Codegen müssen drei
Schnittstellen verbindlich festgelegt werden:

1. die genaue OS-9-/Microware-ABI für Treiber und Handler,
2. das vollständige IR-Metadatenmodell,
3. die Übergabe dieser Metadaten durch `qcc`, `qir68k`, `qr68k` und `ql68k`.

Danach sollte zunächst nur ein kleiner `PROG`-/`DRIVER`-Pfad umgesetzt werden.
`__syscall`, Inline-Assembler, `NOOS`, Manager und Trap-Handler sollten erst
auf diesem stabilen Fundament folgen.

## 2. Wichtigste Änderungen am bisherigen Plan

### 2.1 Erst ABI und Modulformat klären

Die Treiberregister und die Rückgabesemantik müssen vor dem Backend-Codegen
eindeutig sein. In der Spezifikation wird der Static-Storage derzeit über `a1`
beschrieben, während der bestehende Linker-Code `a2` nennt. Das ist ein
Blocker, kein Detail.

Festzulegen sind mindestens:

- Register für Static-Storage, Path-Descriptor, Prozesskontext und System-Globals
- Parameterübergabe an C-Funktionen
- Rückgaberegister und Rückgabebreite
- Bedeutung von Carry bei Erfolg und Fehler
- zu erhaltende und zerstörte Register
- genaue Prolog-/Epilog-Sequenz je Calling-Convention

Für jede Convention sollte anschließend ein kleines Assembler-Golden-File als
Referenz angelegt werden.

### 2.2 IR als formale Schnittstelle definieren

Die Quelldirektiven enthalten derzeit mehr Informationen als die geplanten
IR-Opcodes transportieren. Insbesondere fehlen beziehungsweise sind nicht
präzise zugeordnet:

- Entry-Symbol für `PROG` und `NOOS`
- `ALIGN`
- `ORG`
- Modul-/Subsystem-spezifische Zusatzdaten
- die genaue Bedeutung von `TYPE`, `subtype` und Attributen

Vor der Implementierung sollte eine kleine IR-Spezifikation mit folgenden
Regeln erstellt werden:

- exakte Argumentanzahl und Datentyp jedes Metadatums
- erlaubte Werte und Wertebereiche
- Reihenfolge der Metadaten im Stream
- Verhalten bei doppelten oder widersprüchlichen Angaben
- Versionierung oder wenigstens eine Kompatibilitätsregel
- Diagnose bei unbekannten Metadaten

Eine mögliche, besser erweiterbare Darstellung wäre:

```text
MODHEADER name=cfide type=driver subtype=rbf attr=0x8000 edition=1 stack=0
ENTRY drv_init
DISPATCHTAB drv_init drv_read drv_write drv_getstat drv_putstat drv_term
```

Ob diese Schlüsselwortform oder die bestehende Positionssyntax verwendet wird,
ist weniger wichtig als die eindeutige und dokumentierte Semantik.

### 2.3 Frontend und Backend sauber trennen

`q9-qcpp` kann Direktiven erkennen beziehungsweise weiterreichen, erzeugt aber
nicht automatisch die vollständige semantische IR. Die eigentliche Zuordnung
von Moduldefinition, Funktionen und Symbolen muss im Frontend erfolgen, das
die Stack-IR erzeugt.

Daher sollte der Plan explizit zwischen folgenden Aufgaben unterscheiden:

1. Präprozessor-/Scanner-Unterstützung in `q9-qcpp`
2. semantische Verarbeitung im C-Frontend beziehungsweise `q9-qcir`
3. IR-Validierung
4. Backend-Codegen
5. Linker-/Driver-Integration

Ein bloßes Durchreichen von `#DEFMODUL`, `#ORG` oder `#SECTION` reicht nicht,
wenn die nachfolgenden Stufen diese Direktiven nicht auswerten.

### 2.4 Alle betroffenen Backends berücksichtigen

Die Erweiterung wird als neutrale Stack-IR beschrieben. Dann muss festgelegt
werden, welche Backends die neuen Opcodes jeweils unterstützen:

- 68k: native OS-9-Modulerzeugung
- VM: validieren, ignorieren oder simulieren?
- C-Backend: Metadaten ausgeben oder bewusst ablehnen?
- ARM64/andere Ziele: unterstützen, ablehnen oder auf Zieloption begrenzen?

Eine gute Regel wäre: Nicht unterstützte, zielabhängige Opcodes müssen mit
einer klaren Fehlermeldung abgelehnt werden. Sie dürfen nicht stillschweigend
verloren gehen.

## 3. Neuer phasenweiser Implementierungsplan

### Phase 0: Bestehende Pipeline einfrieren und Referenzen sammeln

Ziel: Eine reproduzierbare Ausgangsbasis schaffen.

Aufgaben:

- aktuellen Build von `q9-qcpp`, `q9-qcir`, `q9-qir68k`, `qr68k` und `ql68k`
  dokumentieren
- einen normalen Programm-Build als Baseline-Golden-Test sichern
- bestehende Modul-Header- und CRC-Tests sammeln
- festhalten, ob ein vorhandener Treiber als Referenzmodul verwendet werden kann
- den uncommitteten Ausgangszustand vor der Erweiterungsarbeit separat sichern

Abnahmekriterium: Ein unveränderter normaler Programm-Build bleibt bitweise
oder semantisch reproduzierbar.

### Phase 1: ABI- und IR-Entscheidungen abschließen

Ziel: Keine Backend-Implementierung auf Basis von Annahmen.

Verbindlich spezifizieren:

- `PROG`, `DRIVER`, `MANAGER`, `SYSTEM`, `TRAPHANDLER` und `NOOS`
- Modulname, Typ, Subtyp, Attribute, Edition und Stack
- Entry-Symbol und Dispatch-Symbole
- Static-Storage-Register und Treiberparameter
- Rückgabe-/Carry-Regeln
- `interrupt`, `trap` und `naked`
- Semantik von `ORG`, `SECTION` und `ALIGN`
- Verhalten nicht unterstützter Zielkombinationen

Abnahmekriterium: Für jede Entscheidung existiert mindestens ein positives
und ein negatives Beispiel mit erwarteter Diagnose oder erwarteter IR.

### Phase 2: Minimaler IR-Metadatenpfad

Ziel: Metadaten sicher vom Frontend zum 68k-Backend transportieren.

Zunächst nur implementieren:

- `MODHEADER`
- `ENTRY`
- `DISPATCHTAB`
- `FUNC ... [convention]`

Noch nicht implementieren:

- `ORG`
- `SECTION`
- `ALIGN`
- `INLINEASM`
- `__syscall`

Die IR-Parser aller betroffenen Komponenten müssen unbekannte oder ungültige
Metadaten melden. Das bestehende `FUNC`-Format mit zwei Argumenten darf dabei
nicht unbemerkt mit dem neuen Format kollidieren.

Abnahmekriterium: Eine minimale IR mit Modulheader und einer Funktion wird
akzeptiert; dieselbe IR ohne Pflichtargumente wird verständlich abgelehnt.

### Phase 3: Einfaches `PROG`-Modul

Ziel: Den einfachsten OS-9-Modultyp ohne Treiber-ABI zum Laufen bringen.

Beispiel:

```c
#DEFMODUL NAME testprog
#DEFMODUL TYPE PROG main

int main(void) {
    return 0;
}
```

Zu prüfen sind:

- Modulname
- Edition und Stack
- Entry-Offset
- Header-Parität
- CRC
- unveränderte Bibliotheks- und Startup-Regeln

Abnahmekriterium: Das erzeugte Modul wird von `ident` beziehungsweise dem
Zielsystem akzeptiert und kann geladen beziehungsweise ausgeführt werden.

### Phase 4: Minimaler `DRIVER`-Pfad

Ziel: Einen einzigen Treibertyp mit einer expliziten Dispatch-Tabelle zu
unterstützen.

Für den ersten Schritt keine automatische Magie für alle Subsysteme verwenden.
Die Funktionen sollten explizit angegeben werden:

```c
#DEFMODUL TYPE DRIVER rbf
#DEFMODUL ENTRY init drv_init
#DEFMODUL ENTRY read drv_read
#DEFMODUL ENTRY write drv_write
#DEFMODUL ENTRY getstat drv_getstat
#DEFMODUL ENTRY putstat drv_putstat
#DEFMODUL ENTRY term drv_term
```

Zu implementieren und zu testen sind:

- Dispatch-Reihenfolge
- relative Offsets
- Treiber-Prolog
- Rückgabe und Carry
- kein versehentliches `cstart.r`
- Linker-Layout für Treibermodule

Abnahmekriterium: Ein Referenztreiber besitzt die erwartete Dispatch-Tabelle,
korrekte Headerwerte und eine gültige OS-9-Prüfsumme.

### Phase 5: Weitere Calling-Conventions

In dieser Reihenfolge:

1. `interrupt`
2. `trap`
3. `naked`

Jede Convention bekommt eigene Prolog-/Epilog-Tests. Besonders bei `naked`
muss die Spezifikation festlegen, welche Rückkehr überhaupt erlaubt ist und
ob der Compiler bestimmte C-Konstrukte verbieten muss.

### Phase 6: Adressplatzierung

Erst nach dem einfachen Modulpfad implementieren:

- `ALIGN`
- `SECTION`
- `ORG`
- Raw-Binary-/NOOS-Ausgabe

Diese Funktionen betreffen nicht nur das IR, sondern auch Assembler, ROF-Layout,
Linker und die Bedeutung von Symboladressen. Sie sollten deshalb nicht als
kleine Präprozessor-Erweiterung behandelt werden.

### Phase 7: `__syscall`

Zunächst nur wenige konkret definierte Systemaufrufe unterstützen. Für jeden
Syscall eine Tabelle mit folgenden Angaben führen:

- Trap-Nummer
- Registerzuordnung
- Parameterbreite
- Rückgaberegister
- Fehlerkonvention
- zerstörte Register
- zulässige C-Signatur

Die Archetypen `CALL_D`, `CALL_DA` und `CALL_FORK` können als Codegen-Muster
dienen, dürfen aber nicht die fehlende Einzelbeschreibung der Syscalls ersetzen.

### Phase 8: Inline-Assembler

Inline-Assembler zuletzt und zunächst nur für 68k implementieren.

Vorher definieren:

- Syntax und Kommentarregeln
- Gültigkeitsbereiche
- Symbol- und Variablenreferenzen
- Register-Clobbers
- Labels
- Fehlerverhalten bei falscher Zielarchitektur
- Behandlung innerhalb von Funktionen

`INLINEASM` sollte als ausdrücklich zielabhängige IR-Erweiterung gekennzeichnet
werden, nicht als vollständig portable Stack-IR.

## 4. Empfohlene Dokumentänderungen

### `OS9_SYSTEM_INTERFACE.md`

- ABI-Register und Rückgaberegeln mit einer autoritativen Quelle abgleichen.
- `a1`/`a2`-Widerspruch auflösen.
- Für jeden Modultyp eine vollständige Minimaldefinition ergänzen.
- Automatische Standard-Dispatch-Einträge zunächst durch explizite Einträge
  ersetzen oder klar als Komfortsyntax kennzeichnen.
- `__syscall` um Register-, Fehler- und Clobber-Regeln erweitern.
- Inline-Assembler als 68k-spezifische Erweiterung markieren.

### `IR_OPCODES_de.md`

- `ENTRY`, `ALIGN` und die genaue Metadatenrepräsentation ergänzen.
- Für jeden Opcode Argumentanzahl, Wertebereiche und Position im Stream angeben.
- `FUNC`-Erweiterung mit allen Backends abstimmen.
- ausdrücklich dokumentieren, was VM und nicht-68k-Backends tun.
- Stack-Effekte für mehrwortige Werte einheitlich definieren.

### `STATUS_DEFMODUL.md`

- Phase 0 und Phase 1 vor die Implementierungsaufgaben setzen.
- Aufgaben nach Komponenten statt nur nach Themen schneiden.
- Linker-/Driver-Integration als eigenes Paket ergänzen.
- Für jede Aufgabe ein messbares Abnahmekriterium hinzufügen.
- `trap`, `naked`, `ORG`, `SECTION`, `ALIGN` und `NOOS` nicht implizit in
  Sammelaufgaben verstecken.

## 5. Minimaler Testsatz

Vor dem Ausbau sollte mindestens Folgendes automatisiert werden:

1. normale Programmübersetzung ohne neue Direktiven
2. `PROG` mit Standardwerten
3. `PROG` mit explizitem Entry und Stack
4. `DRIVER` mit vollständiger Dispatch-Tabelle
5. fehlendes Dispatch-Symbol
6. falsche Treiberfunktion-Signatur
7. falscher Modul- oder Subtyp
8. doppelte `#DEFMODUL`-Angabe
9. unbekanntes Subkommando
10. CRC-/Header-Prüfung des erzeugten Moduls
11. falscher Einsatz eines zielabhängigen Opcodes im VM-/C-Backend
12. Regressionstest für einen normalen Nicht-Modul-Build

## 6. Schlussfolgerung

Ich würde den bisherigen Plan nicht verwerfen, sondern deutlich stärker
sequenzieren. Der wichtigste Wechsel ist:

> Zuerst ABI und IR verbindlich machen, dann ein kleines `PROG`-/`DRIVER`-MVP
> durch die komplette Pipeline bringen, danach erst Syscalls, Adressplatzierung
> und Inline-Assembler ergänzen.

Damit werden die riskantesten Annahmen früh sichtbar, ohne gleichzeitig alle
OS-9-Modularten und alle Backendvarianten implementieren zu müssen.

