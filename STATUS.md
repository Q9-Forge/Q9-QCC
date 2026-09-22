# Q9 MODUL Erweiterung

Status-Übersicht und Arbeitsplan für die Implementierung nativer OS-9 Modulartefakte (`#DEFMODUL`, Dispatch-Tabellen, Calling-Conventions, `#ASM`) in Q9-QCC.

**Legende:**
- 🔴 Offen / Noch nicht begonnen
- 🟡 In Arbeit / In Prüfung
- 🟢 Abgeschlossen / Verifiziert

---

## Schnellübersicht (Dashboard)

| Paket | Aufgabe | Status | Beschreibung |
|---|---|:---:|---|
| **1. Spezifikation & IR** | 1.1 IR-Opcodes formal festlegen | 🟢 | `MODHEADER`, `DISPATCHTAB`, `ORG`, `SECTION`, `INLINEASM`, `FUNC ... [conv]` |
| | 1.2 Doku `IR_OPCODES_de.md` aktualisieren | 🟢 | Opcodes in Referenzdokumentation aufgenommen |
| | 1.3 `OS9_SYSTEM_INTERFACE.md` abgleichen | 🟢 | Architektur, `#DEFMODUL`-Syntax, Calling-Conventions (`driver`, `interrupt`, `trap`, `naked`) & Multi-Regionen spezifiziert |
| **2. Frontend (`q9-qcpp` / `qcir`)** | 2.1 `#DEFMODUL` Parser im Präprozessor | 🔴 | Syntax `#DEFMODUL <KEYWORD> ...` (NAME, EDITION, STACK, ATTR, ORG, ALIGN, TYPE) einlesen |
| | 2.2 Calling-Convention Keywords (`driver`, `interrupt`, `trap`, `naked`) | 🔴 | ABI-Modifikatoren auf `static`-Ebene parsen |
| | 2.3 Syscall-Intrinsics (`__syscall`) | 🔴 | 3 Archetypen (`CALL_D`, `CALL_DA`, `CALL_FORK`) unterstützen |
| | 2.4 IR-Emission von Modul- und Funktions-Metadaten | 🔴 | `MODHEADER`, `DISPATCHTAB`, `FUNC ... [conv]` im `.qir`-Stream |
| | 2.5 `#ASM ... #ENDASM` Parser & IR-Emission | 🔴 | Inline-Assembler erfassen und als `INLINEASM` weiterleiten |
| **3. Backend 68k (`q9-qir68k`)** | 3.1 Handler für `MODHEADER` | 🔴 | Generierung von `psect`, Typ/Sprache, Attr/Rev |
| | 3.2 Handler für `DISPATCHTAB` | 🔴 | Relative Offset-Tabelle zu den C-Funktionen emittieren |
| | 3.3 Codegen für `driver` Calling-Convention | 🔴 | Register-Prolog (`a1`/`a2`) & Carry-Flag Epilog + `rts` |
| | 3.4 Codegen für `interrupt` Calling-Convention | 🔴 | `movem.l` Registerrettung & Beendigung mit `rte` |
| | 3.5 Handler für `INLINEASM` | 🔴 | 1:1 Ausgabe in die `.s68`-Assemblerdatei |
| **4. Linker & Prüfsumme (`q9-ql68k`)** | 4.1 Modul-Header Offset-Berechnung | 🔴 | Verifikation Name- und Execution-Offsets (`isDrvr`) |
| | 4.2 24-Bit OS-9 CRC-Prüfsumme | 🔴 | Sicherstellen der korrekten CRC-Generierung ($800063) |
| **5. Test & Verifikation** | 5.1 Test-Suite für Programm (`test_prog.c`) | 🔴 | End-to-End: C -> IR -> ASM -> ROF -> Binär |
| | 5.2 Test-Suite für Treiber (`test_driver.c`) | 🔴 | RBF-Treiber mit 6 Funktionen verifizieren |
| | 5.3 Test-Suite für Inline-ASM (`test_asm.c`) | 🔴 | Hardwarenahe Registerzugriffe testen |
| | 5.4 Test auf Q9-Emulator / Zielsystem | 🔴 | OS-9 `load` / Ausführungstest |

---

## Detaillierte Arbeitsschritte

### 1. Spezifikation & Zwischenschicht (Stack-IR)
*Ziel: Vollständige Plattformunabhängigkeit zwischen C-Frontend und Code-Backends.*
- **1.1 & 1.2 IR-Opcodes:**
  - `MODHEADER <name> <type> <subtype> <attr> <edition> <stack>`: Legt Metadaten des Zielmoduls fest.
  - `DISPATCHTAB <sym1> <sym2> ...`: Gibt die geordnete Liste aller Einsprung-Symbole an.
  - `FUNC <name> <nargs> [convention]`: Erweitert `FUNC` um ABI-Informationen (`driver`, `interrupt`, `trap`).
  - `INLINEASM "<assembler-zeile>"`: Reicht maschinenspezifischen Assemblercode transparent durch.
- **1.3 Dokumentation:**
  - Spezifikation in `docs/OS9_SYSTEM_INTERFACE.md` und `docs/IR_OPCODES_de.md` ist vollständig ausgearbeitet und synchronisiert.

### 2. Frontend & Präprozessor (`Q9-FRONTEND-C`)
*Ziel: C-Quelldateien einlesen und reinen Stack-IR-Code erzeugen.*
- **2.1 `#DEFMODUL` Erkennung:**
  - Der Präprozessor/Scanner (`qcpp`) erkennt Direktiven am Kopf der Datei im Subkommando-Format:
    - `#DEFMODUL NAME <name>` (Default: Basisdateiname)
    - `#DEFMODUL EDITION <num>` (Default: 1)
    - `#DEFMODUL STACK <bytes>` (Default: 4096 bei PROG, 0 bei DRIVER)
    - `#DEFMODUL ATTR <hex>` (Default: 0x8000 = Reentrant)
    - `#DEFMODUL TYPE PROG [entry]` (Default: main)
    - `#DEFMODUL TYPE DRIVER <subsystem> [entries...]` (Default-Einsprünge: `drv_init`, `drv_read`, `drv_write`, `drv_getstat`, `drv_putstat`, `drv_term`)
    - `#DEFMODUL TYPE MANAGER <subsystem> [entries...]`
    - `#DEFMODUL TYPE SYSTEM`
    - `#DEFMODUL TYPE NOOS [entry]` (Flat Binary / Bare-Metal BIOS)
    - `#DEFMODUL TYPE TRAPHANDLER <subsystem> [entries...]`
- **2.2 Calling-Convention Keywords:**
  - Parsing von `driver`, `interrupt`, `trap` als Speicherklassen/Funktionsmodifikatoren.
- **2.3 Inline-Syscalls:**
  - Parsing von `__syscall(id, archetype)` zur direkten Generierung von TRAP-Calls ohne Wrapper.
- **2.4 & 2.5 IR-Emission & Inline-ASM:**
  - Ausgabe der Metadaten in `.qir` und Kapselung von `#ASM ... #ENDASM` in `INLINEASM`.

### 3. Backend 68k (`Q9-BACKEND-68K/q9-qir68k`)
*Ziel: Generierung von OS-9-kompatiblem Assembler.*
- **3.1 `MODHEADER` Umsetzung:**
  - Erzeugt das Wurzel-`psect` mit den korrekten OS-9 Parametern (Name, Typ/Sprache wie `$0E01` für RBF, Attribute `$8000`, Stackgröße).
- **3.2 Dispatch-Tabelle:**
  - Erzeugt am Modulanfang die relative Sprungtabelle (z. B. 6 Worte `dc.w drv_init-dispatch_entry` für RBF-Treiber).
- **3.3 & 3.4 Calling-Convention Codegen:**
  - `driver`: Parameter-Slots aus `a1` (Storage) und `a2` (Path) belegen; Epilog prüft `d0`, setzt/löscht Carry-Flag und meldet `d1`.
  - `interrupt`: Generiert `movem.l` Prolog/Epilog und schließt mit **`rte`** ab.
- **3.5 `INLINEASM` Ausgabe:**
  - Direkte Übergabe der Assemblerzeilen in die `.s68`-Ausgabe.

### 4. Linker & OS-9 CRC (`Q9-BACKEND-68K/q9-ql68k`)
*Ziel: Valide Binärmodule erzeugen.*
- **4.1 Header-Offsets:**
  - Prüfung, ob `ql68k` alle Offsets (Name, Exception, Execution) konform zum OS-9 Standard setzt (Zweig `isDrvr`).
- **4.2 24-Bit CRC:**
  - Verifikation, dass der OS-9 CRC-Algorithmus (Polynom `$800063`) am Dateiende korrekt berechnet wird.

### 5. Test & Verifikation
*Ziel: Qualitätsnachweis der gesamten Kette.*
- Aufbau automatisierter Tests in `tests/`:
  - Übersetzungskette `.c` -> `.qir` -> `.s68` -> `.r68` -> Binärmodul.
  - Inspektion der Header-Bytes (`$4AFB`) und Tabellenstruktur.
