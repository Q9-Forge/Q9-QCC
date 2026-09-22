# OS-9 System-Schnittstellen, Treiber-Erweiterung & Calling-Conventions
=============================================================================

Status: Genehmigt / Spezifikation — Stand 2026-09-22
Projekt: Q9-QCC (Branch `QCC-DEFMODUL`)

---

## 1. Überblick & Motivation
Dieses Dokument beschreibt die Erweiterung des Compilers (Q9-QCC für C und Quant9) um:
1. **Native OS-9 Modul-Erzeugung:** Direktiven zur Erzeugung beliebiger OS-9 Modultypen (`#DEFMODUL`).
2. **Dedizierte Calling-Conventions:** Sprachschlüsselwörter für Treiber, Interrupts und Systemaufrufe (`driver`, `interrupt`, `trap`, `__syscall`).
3. **Universelle Syscall-Archetypen:** Reduktion von über 100 OS-9-Systemaufrufen auf 3 generische Register-Muster über Prefix-Sharing – vollständig ohne manuelle Assembler-Wrapper (`os9call.a`).
4. **Hardwarenaher Inline-Assembler:** `#ASM ... #ENDASM` für direkte Register- und Hardware-Manipulationen.
5. **Vollständige Plattformunabhängigkeit:** Alle Konzepte werden über neutrale Stack-IR-Opcodes transportiert. Das Frontend bleibt frei von 68k-Spezifika.
6. **Autarke Modulerzeugung:** Das Wurzel-`psect` wandert direkt in den Compiler/die IR; Treiber und Module benötigen kein starres externes `cstart.r` mehr.

---

## 2. Modul-Definition (`#DEFMODUL`)

Die Konfiguration des Zielmoduls erfolgt über die `#DEFMODUL`-Direktive am Anfang der Quelldatei. Das Format folgt dem `#define`-Muster mit Subkommandos:

```c
#DEFMODUL NAME cfide           // Optional: Interner OS-9 Modulname (Default: Dateiname)
#DEFMODUL EDITION 55          // Optional: Modul-Edition (Default: 1)
#DEFMODUL STACK 65536         // Optional: Stackgröße in Bytes (Default: PROG=4096, DRIVER=0)
#DEFMODUL ATTR 0x8000         // Optional: Modul-Attribute (Default: 0x8000 = Reentrant)
#DEFMODUL ORG 0xFFF80000      // Optional: Physische Basisadresse (für NOOS/BIOS Flat-Binaries)
#DEFMODUL ALIGN 0x1000        // Optional: Alignment-Grenze (z. B. 4KB Page)
#DEFMODUL TYPE DRIVER rbf     // Modultyp und Subsystem
```

### 2.1 Standardwerte (Defaults) & Konventionen

- **Modulname (`NAME`):**  
  Fehlt `#DEFMODUL NAME`, wird automatisch der Basisname der Quelldatei (z. B. `cfide` bei `cfide.c`) bzw. der `-O=` Parameter des Linkers verwendet.

- **Einsprungnamen bei `DRIVER`:**  
  Wird `#DEFMODUL TYPE DRIVER <subsystem>` ohne explizite Funktionsliste angegeben, setzt der Compiler automatisch die 6 OS-9 Standard-Einsprünge ein:
  1. `drv_init`
  2. `drv_read`
  3. `drv_write`
  4. `drv_getstat`
  5. `drv_putstat`
  6. `drv_term`

  Explizite abweichende Namen können optional angehängt werden:
  ```c
  #DEFMODUL TYPE DRIVER rbf my_init my_read my_write my_getstat my_putstat my_term
  ```

### 2.2 Unterstützte Modul-Typen (`TYPE`):

* **Normales Programm (Standard):**
  ```c
  #DEFMODUL TYPE PROG [entry_func]
  ```
  *(Default-Einsprung: `main`. Fehlt die Direktive komplett, gilt automatisch `PROG main`).*

* **Gerätetreiber (RBF, SCF, Pipe etc. — OS-9 Typ $0E):**
  ```c
  #DEFMODUL TYPE DRIVER rbf
  ```
  *Der Compiler erzeugt automatisch die 6-teilige Offset-Dispatch-Tabelle am Modulanfang.*

* **System-Manager / File-Manager (OS-9 Typ $0D):**
  ```c
  #DEFMODUL TYPE MANAGER fscs mgr_link mgr_create mgr_open
  ```

* **Systemmodul / Kernel (OS-9 Typ $0C):**
  ```c
  #DEFMODUL TYPE SYSTEM
  #DEFMODUL ATTR 0xA000        // Supervisor-State ($20) + Reentrant ($80)
  ```

* **Bare-Metal / BIOS / Boot-ROM (Kein OS-Header, Flat Binary):**
  ```c
  #DEFMODUL TYPE NOOS [entry_func]
  ```
  *(Alias: `BAREMETAL`. Erzeugt ein reines Flat Binary ohne OS-9 Modul-Sync ($4AFB), ohne Header und ohne CRC. Der Einstiegspunkt liegt direkt an Offset 0. In Kombination mit `#ASM` können Reset-Vektoren, Hardware-Init und Vektortabellen vollständig in C geschrieben und direkt ins Flash-ROM/EPROM gebrannt werden).*

* **Trap-Handler / Bibliothek (OS-9 Typ $0B):**
  ```c
  #DEFMODUL TYPE TRAPHANDLER math trap_func1 trap_func2
  ```

---

## 3. Speicher-Layout für Treiber und Kernel-Code

1. **Kein privates `a6`-Datensegment:**
   - Treiber und Kernelmodule laufen im Re-Entrant- bzw. Supervisor-Modus (`$8000` / `$A000`).
   - In Treibern zeigt `a6` auf die System-Globals des Kernels, **nicht** auf ein privates Programm-Datensegment.
   - **Regel:** Schreibbare globale C-Variablen (`static int count;`) sind in Treibern und Kernelmodulen verboten. Das Frontend meldet bei Deklaration schreibbarer globaler Variablen einen Compilerfehler.
2. **Device-Static-Storage (`a1`):**
   - Treiberdaten müssen im von OS-9 übergebenen Static-Storage liegen (erster Parameter der Treiberfunktionen). Der Zugriff erfolgt typsicher über Struct-Pointer.
3. **Konstanten (`const`):**
   - Read-Only-Daten (`const`, Strings, Lookup-Tabellen) liegen legal im `psect` und werden PC-relativ adressiert.
4. **Lokale Variablen:**
   - Liegen ganz normal auf dem CPU-Stack (`link a5` / `unlk a5`).

---

## 4. Sprach-Erweiterung: Calling-Conventions & ABI-Modifikatoren

Funktionen können mit ABI-Schlüsselwörtern auf der Ebene von `static`/`extern` deklariert werden:

### 4.1 `driver` (OS-9 Gerätetreiber)
Treiber-Routinen werden von OS-9 über feste CPU-Register angesprungen:
- **Eingang:**
  - `a1` = Zeiger auf Device-Static-Storage (Treiber-Instanzdaten)
  - `a2` = Zeiger auf Path-Descriptor
  - `a4` = Current Process Descriptor
  - `a6` = System-Globals
- **Rückgabe:**
  - `return 0;` $\rightarrow$ Erfolg: Carry-Bit gelöscht, `rts`.
  - `return err;` $\rightarrow$ Fehler: Carry-Bit gesetzt, Fehlercode in `d1.w`, `rts`.

```c
#DEFMODUL TYPE DRIVER rbf

driver int32_t drv_read(void *dev_storage, void *path_desc) {
    if (!dev_storage) return 216; // E$PNNP (Path Not Found / Error)
    return 0;                     // Erfolg
}
```

### 4.2 `interrupt` (Hardware-Interrupt-Service-Routinen)
- **Prolog:** Sichert alle flüchtigen Register (`movem.l d0-d7/a0-a6,-(sp)`).
- **Epilog:** Stellt Register wieder her und beendet zwingend mit **`rte`** (Return from Exception) statt `rts`.

```c
interrupt void my_timer_isr(void) {
    // Hardware-Tick bearbeiten
}
```

### 4.3 `naked` (Bare-Metal / Reset-Vektor / Zero-Overhead)
- **Prolog:** Keiner (`link` und Register-Rettung entfallen vollständig; kein Stack-Zugriff).
- **Epilog:** Keiner (der Entwickler steuert Rücksprung oder Endlosschleife selbst via `#ASM`).
- **Einsatz:** Kaltstart/Reset-Routinen vor RAM-Initialisierung oder ultra-schnelle Hardware-Entrypoints.

```c
naked void reset_entry(void) {
    #ASM
        move.w  #0x2700, sr
        lea     0x0007FF00, sp
        jmp     main
    #ENDASM
}
```

### 4.4 `trap` (Exception- & Software-Trap-Handler)
- Behandelt den CPU-Trap-Frame und beendet mit `rte`.

---

## 5. Die 3 universellen OS-9 Syscall-Archetypen (Prefix-Sharing)

Bisher erforderte jeder OS-9 Systemaufruf einen handgeschriebenen Assembler-Wrapper in `os9call.a`. Durch das Prinzip des **Prefix-Sharings** (Teilmengen fester Registerbelegungen) lassen sich über 100 Syscalls auf **nur 3 universelle Grundkonventionen** abbilden:

### 1. `CALL_D` (Reine Werte/Handles: `d0` -> `d1` -> `d2`)
Werte sequentiell in Datenregistern:
- 1 Parameter (`d0`): `F$Sleep(ticks)`, `F$Exit(status)`, `F$Mem(size)`
- 2 Parameter (`d0, d1`): `F$Send(pid, sig)`, `I$Close(path)`
- 3 Parameter (`d0, d1, d2`): Erweiterte Prozess-/Signal-Calls

### 2. `CALL_DA` (Das OS-9 Arbeitspferd: `d0` [Int], `a0` [Ptr], `d1` [Int])
Standard für praktisch alle I/O- und Dateioperationen:
- 2 Parameter (`d0, a0`): `I$Open(mode, name)`, `I$Create(mode, name)`, `F$Link(tylan, name)`, `I$ChgDir(mode, path)`
- 3 Parameter (`d0, a0, d1`): `I$Read(path, buf, count)`, `I$Write(path, buf, count)`, `I$ReadLn(...)`, `I$WritLn(...)`

### 3. `CALL_FORK` (Kernel-Großaufrufe: `a0, a1, d0, d1, d2`)
Für die wenigen komplexen Prozessaufrufe:
- `F$Fork(name, params, tylan, mem, param_size)`
- `F$Chain(...)`

### Direkte Deklaration in C (Inline-Syscall-Intrinsics):
```c
__syscall(0x8a, CALL_DA) int32_t os_write(int32_t path, const void *buf, uint32_t count);
__syscall(0x84, CALL_DA) int32_t os_open(int32_t mode, const char *name);
__syscall(0x06, CALL_D)  void    os_exit(int32_t status);
```
Der Compiler erzeugt direkt an der Aufrufstelle den `TRAP #0` samt Parameterübergabe. Die gesamte Datei `os9call.a` wird mittelfristig überflüssig.

---

## 6. Inline-Assembler (`#ASM`) & Adressplatzierung (`#ORG`, `#SECTION`)

### 6.1 Inline-Assembler (`#ASM ... #ENDASM`)
`#ASM` ist sowohl **global** (auf Dateiebene) als auch **lokal** (innerhalb von Funktionen) gültig:
- **Global:** Für feste CPU-Vektortabellen, Konstanten (`equ`) und Hardware-Tabellen direkt an Offset 0.
- **Lokal:** Für hardwarenahe 68k-Instruktionen (`moves`, `andi.w`, Registermanipulationen) mit Zugriff auf C-Variablen (`%val`).

```c
#ASM
    * MC68000 Vektortabelle direkt am Dateianfang:
    dc.l  0x00080000       * Initial SSP
    dc.l  reset_entry      * Reset PC
#ENDASM

int32_t read_hardware_port(void) {
    int32_t val = 0;
    
    #ASM
        move.w  0x4000, d0
        ext.l   d0
        move.l  d0, %val
    #ENDASM
    
    return val;
}
```

### 6.2 Adressplatzierung & Multi-Regionen (`#ORG`, `#SECTION`)
Für Bare-Metal/BIOS-Entwicklung können innerhalb einer C-Datei feste Zieladressen definiert werden:
1. **`#ORG <adresse>` (Padding / Auffüllen):**
   Füllt das Binärimage bis zur Zieladresse auf (z. B. mit `0xFF` für Flash-ROMs). Ideal für feste Sprungtabellen oder ROM-Endsignaturen.
   ```c
   #ORG 0xFFF81000
   void bios_jump_table(void) { ... }
   ```
2. **`#SECTION <name> [adresse]` (Getrennte Speicherbereiche):**
   Erzeugt ein eigenes `psect` für eine andere Speicherregion (z. B. Fast-RAM `0x00001000` für Routinen, die aus dem ROM ins RAM kopiert werden).
   ```c
   #SECTION fast_ram 0x00001000
   void time_critical_code(void) { ... }
   ```

---

## 7. Die neutrale Zwischenschicht (Stack-IR `.qir`)

Das Frontend bleibt 100 % plattformunabhängig. Es emittiert ausschließlich neutrale IR-Opcodes:

```text
; --- Header & Dispatch-Tabelle am Dateianfang ---
MODHEADER cfide driver rbf 8000 1 0
DISPATCHTAB drv_init drv_read drv_write drv_getstat drv_putstat drv_term

; --- Funktionsdefinition mit Calling-Convention-Attribut ---
FUNC drv_read 2 driver
    LOADL 0             ; dev_storage (aus a1 geladen)
    LOADL 1             ; path_desc (aus a2 geladen)
    PUSH 0
    RET                 ; Backend emittiert Carry-Clear + RTS
ENDFUNC

FUNC my_timer_isr 0 interrupt
    ; Rumpf
    RET                 ; Backend emittiert RTE
ENDFUNC
```

---

## 8. Codegen im 68k-Backend (`q9-qir68k`) & Linker (`ql68k`)

Das Backend übersetzt die IR in OS-9-konformen 68k-Assembler:

1. **`MODHEADER`:** Erzeugt `psect <name>,<tylan>,<attrev>,<edit>,<stack>,<entry>`.
2. **`DISPATCHTAB`:** Emittiert an Offset 0 die Wort-Offsets zu den Funktionen (`dc.w drv_init-dispatch_entry` etc.).
3. **`FUNC ... driver`:**
   - Prolog: `move.l a1,-4(a6)` und `move.l a2,-8(a6)`.
   - Epilog: Test von `d0`, `andi #$FE,ccr` bei 0, sonst `move.w d0,d1` + `ori #$01,ccr`, danach `unlk` und `rts`.
4. **`FUNC ... interrupt`:**
   - Prolog: `movem.l d0-d7/a0-a6,-(sp)`.
   - Epilog: `movem.l (sp)+,d0-d7/a0-a6` + `rte`.
5. **Linker `ql68k`:**
   - Berechnet automatisch Name-Offset, Header-Parität und 24-Bit OS-9 CRC (`$800063`).
   - Bei Treibern (`isDrvr`): Kein `cstart.r` erforderlich, Einstiegspunkt liegt direkt auf `dispatch_entry`.
