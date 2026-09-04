# Q9-qr68 — der 68k-Assembler der Q9-Werkzeugkette

Ausführbares Programm: **`qr68`**

```
qr68 [Optionen] <eingabe.a> <ausgabe.r>
```

Ziel: Microwares `r68` ersetzen. Präprozessor (`qcpp`) und Compiler-Frontend
(QCC) laufen bereits auf echtem 68030 — Assembler und Binder sind die letzten
Fremdteile der Kette.

## Stand (2026-09-04)

**Die vom QCC-Backend erzeugten Quellen assembliert `qr68` byteidentisch zu
`r68`** — ganze Module, nicht Einzelfälle:

| Prüfung | Ergebnis |
|---|---|
| 10 Proben (Kopf, Globale, Externe, vsect, Padding) | **byteidentisch** |
| `test/insn.a`, `test/dir.a`, `test/mac.a`, `test/bopt.a` — jede kodierbare Form | **byteidentisch**, Zeile für Zeile |
| 10 Module aus QCCs Backend, bis 146.848 Zeilen / 1,07 MB ROF | **byteidentisch** |
| Die Assemblerquellen des **Q9-OS-Kernels** (handgeschrieben, 4.000 Zeilen) | **byteidentisch** |
| **78 Quellen des MWOS-SDK** — Treiber, Descriptoren, Systemmodule, Boot- und ROM-Code | **byteidentisch** |
| **qr68 auf echtem 68030**, gebaut mit der eigenen Kette | **byteidentisch zum Hostlauf** |

Der Zeitstempel ist dabei nicht ausgenommen, sondern nachgebildet (`-fdate=`).

Abgedeckt: `psect`/`vsect`/`ends`/`endsect`, `equ`/`set`, `dc.b/.w/.l` (auch
Zeichenketten), `ds.b/.w/.l`, `align`, `end`, die beschreibenden Direktiven
(`nam`/`ttl`/`page`/`opt`/`spc`), Ausdrücke mit `$`/`%`/`@`/Zeichen,
`+ - * / & ! << >>` und unär `- + ^`, Klammern, `*` als aktueller Ort und
`.` als org-Zähler, Symbole mit Vorwärtsreferenzen, globale Labels
(`name:`), externe und lokale Referenzen.

**Achtung bei Ausdrücken, an r68 gemessen:** `^` ist das **unäre Nicht**
(`^$0f` = `$f0`), *kein* XOR — `$ff^$0f` lehnt r68 ab; und `~` kennt r68
gar nicht.

**Befehle:** `move`/`movea`/`moveq`, `lea`/`pea`,
`add`/`sub`/`and`/`or`/`eor`/`cmp` mit allen Formen, die `r68` daraus macht
(Grundform, `…a`, `…i`, `…q`), `adda`/`suba`/`cmpa`, `addi`…`cmpi`,
`addq`/`subq`, `muls`/`mulu`/`divs`/`divu` (Wortform), `clr`/`neg`/`negx`/
`not`/`tst`/`tas`/`swap`/`ext`/`extb`, alle acht Schiebe- und Rotierbefehle
(Sofortwert, Register, Speicherform), `Scc` (14 Bedingungen), `bra`/`bsr`/
`Bcc` (kurz und Wort), `dbra`/`dbcc`, `jsr`/`jmp`, `link`/`unlk`,
`rts`/`rte`/`rtr`/`nop`/`trap`/`trapv`/`reset`/`stop`/`illegal`,
`cmpm`, `movep`, die 68020-Langformen von `mulu`/`muls`/`divu`/`divs`
(auch als `dr:dq`) und `divul`/`divsl`, die acht **Bitfeldbefehle**
`bftst`/`bfextu`/`bfchg`/`bfexts`/`bfclr`/`bfffo`/`bfset`/`bfins`, die
68040-Cachebefehle `cinva`/`cpusha`/`cinvl`/`cpushl`/`cinvp`/`cpushp` und
`move16` (alle fünf Formen), das PMMU-`pmove`/`pmovefd` (`tc`, `srp`,
`crp`, `tt0`, `tt1`, `mmusr`/`psr`), dazu
`movem` mit Registerlisten (`d0-d7/a0-a6`, bei `-(An)` mit umgekehrter
Maske), die Bitbefehle `btst`/`bset`/`bclr`/`bchg` (statisch und dynamisch),
`ori`/`andi`/`eori` nach `ccr`/`sr`, `move` von und nach `sr`/`ccr`/`usp`,
`exg`, und vom 68010 `movec` und `moves` — `movec` mit den
Kontrollregistern des 68000er-Kerns *und* denen des 68040/68060
(`tc`, `itt0`, `itt1`, `dtt0`, `dtt1`, `buscr`, `mmusr`, `urp`, `srp`,
`pcr`).

**Der Integerbestand ist damit vollständig für den Korpus** — die
Restliste vom 2026-09-04 ist abgearbeitet. FPU kommt in den 207.000 Zeilen
nirgends vor und bleibt draußen.

**Adressierungsarten:** alle zwölf des 68000 —  `Dn`, `An`, `(An)`, `(An)+`,
`-(An)`, `d16(An)`, `d8(An,Xn)`, `abs.w`, `abs.l`, `d16(PC)`, `d8(PC,Xn)`,
`#imm`.

Dazu die Direktiven `use` (Include, mit `-u=`-Suchliste), `org`/`do` (der
Strukturbeschreiber des SDK) und `.` als org-Zähler, die bedingte
Assemblierung (`ifeq`/`ifne`/`ifgt`/`ifge`/`iflt`/`ifle`/`ifdef`/`ifndef`/
`else`/`endc`), **Makros** (`\1`…`\9`, `\#`, `\@`) mit `rept`/`endr`, und
der Systemaufruf `os9`.

Von r68s Schaltern: `-b` (Sprungweiten selbst wählen), `-a<sym>[=<wert>]`,
`-u=<verz>`. Angenommen und übergangen werden die Listing- und
Meldungsschalter (`-q -l -g -e -s -n -x -c -f -r -m<n> -d<n>`), damit die
Aufrufe der SDK-Makefiles unverändert laufen; `-y`, `-bt`, `-j` und `-p<n>`
ändern die Ausgabe und werden **abgelehnt**, statt sie stillschweigend zu
übergehen.

**Alles andere bricht mit Meldung ab.** Das ist Absicht: eine still falsche
Kodierung wäre schlimmer als eine fehlende.

## qr68 läuft auf dem 68030

```
make os9        # Modul bauen
make test68k    # Modul auf echtem 68030 fahren und vergleichen
```

Die Kette dorthin kommt ohne Fremdcompiler aus, und Schritt 4 ist der Punkt:

```
1  qcpp        src/qr68.c   -> qr68.i      eigener Präprozessor
2  qcc_p       @qr68.i      -> qr68.ir     eigener Compiler
3  qcc_backend qr68.ir      -> qr68.s68    eigene Codeerzeugung
4  qr68        qr68.s68     -> qr68.r      SICH SELBST
5  r68         q9_cstart.a  -> q9_cstart.r Laufzeiteinstieg
6  l68         + clib       -> q9_qr68     Modul (1,18 MB)
```

Auf dem 68030 assembliert das Modul dann eine Quelle, und das Ergebnis wird
byteweise mit dem Hostlauf verglichen — geprüft mit `test/insn.a` (jede
kodierbare Form) und mit `q9kernel_entry.a` des Q9-OS-Kernels (153 KB,
249 Symbole). Beide **byteidentisch**.

**Warum das Modul 1,18 MB groß ist:** QCCs Backend legt genullte Felder in
den *initialisierten* Datenbereich, und der wandert vollständig ins Modul.
Der Grund liegt tiefer — ein nicht-remoter `vsect` wird über `d16(a6)`
angesprochen und passt damit nur in 64 KB; `r68` meldet für alles darüber
„value out of range". Solange QCCs Datenmodell so ist, hält `-D_Q9OS` die
Felder auf Zielmaß (64 KB Namen, 256 KB Quelle, 4096 Symbole — der
Q9-OS-Kernel braucht 249, der größte SDK-Treiber 1428).

## Der Prüfstein

`r68` ist byteweise reproduzierbar — bis auf **sechs Zeitstempelbytes** im
ROF-Kopf. Also:

> qr68 ist richtig, wenn seine Ausgabe zu der von `r68` byteidentisch ist,
> diese sechs Bytes ausgenommen.

```
make test                          # Proben, Befehlstabelle, use, -b
make backend                       # QCC-Backend-Quellen + Q9-OS-Kernel
make check                         # beides
./test/mwos.sh                     # die SCF-Treiber des SDK

./test/difftest.sh                 # die eingebauten Proben
./test/difftest.sh datei.a         # eine echte Quelle, ganze ROF-Datei
./test/insndiff.sh datei.a         # Quellzeile für Quellzeile
RFLAGS=-b ./test/insndiff.sh d.a   # dieselben Schalter auf beiden Seiten
```

`difftest.sh` vergleicht die ganze Datei und bleibt bei der ersten Abweichung
stehen. `insndiff.sh` nutzt zusätzlich `r68 -l`: dessen Listing nennt zu jeder
Quellzeile den Offset im Code, damit steht in der Meldung die **Zeile**, die
falsch kodiert wurde. Eine Befehlstabelle lässt sich damit in einem Durchgang
abarbeiten statt Fehler für Fehler.

Das ist ein echtes Orakel: kein selbstgeschriebener Sollwert, der erst selbst
richtig sein müsste. Der Zielkorpus sind **644 handgeschriebene `.a`-Dateien
mit 207.847 Zeilen** (MWOS-SDK + Q9-OS-Kernel).

## Das ROF-Format, am Original gemessen

Es gibt genau **eine** Beschreibung des Formats: `MWOS/APPS/src/osk-disasm`
(`rof.c`/`rof.h`), ein Disassembler. Sie stimmt in einem wesentlichen Punkt
**nicht** mit dem überein, was `r68` schreibt — und in einem weiteren ist sie
gar nicht ausgeführt. Deshalb hier das gemessene Layout.

### Kopf (56 Byte, big-endian)

| Offset | Größe | Feld | Herkunft |
|---|---|---|---|
| 0 | 4 | `sync` = `$DEADFACE` | |
| 4 | 2 | `ty_lan` | psect-Parameter 2 |
| 6 | 2 | `att_rev` | psect-Parameter 3 |
| 8 | 2 | `valid` = 0 | |
| 10 | 2 | `series` = 249 | Assemblerkennung von r68 V2.9.1 |
| 12 | 6 | `rdate` | Jahr−1900, Monat, Tag, Stunde, Minute, Sekunde |
| 18 | 2 | `edition` | psect-Parameter 4 |
| 20 | 4 | `statstorage` | reservierte Daten (`ds` im vsect) |
| 24 | 4 | `idatsz` | initialisierte Daten (`dc` im vsect) |
| 28 | 4 | `codsz` | Codegröße |
| 32 | 4 | `stksz` | psect-Parameter 5 |
| 36 | 4 | `code_begin` | psect-Parameter 6 |
| 40 | 4 | `utrap` | psect-Parameter 7, **sonst `-1`** |
| 44 | 4 | `remotestatsiz` | |
| 48 | 4 | `remoteidatsiz` | |
| 52 | 4 | `debugsiz` | |

### Danach

```
Psect-Name (NUL-terminiert)
Anzahl Globale (4)                 ← 32 Bit, NICHT 16
  je Global: Name (NUL), Typ (2), Adresse (4)
Code (codsz Byte)
Initialisierte Daten (idatsz Byte)
Anzahl externer Namen (4)
  je Name: Name (NUL), Anzahl Referenzen (4)
    je Referenz: Typ (2), Offset (4)
Anzahl lokaler Referenzen (4)
  je Referenz: Typ (2), Offset (4)
vier Langwörter (in allen gemessenen Fällen 0)
```

### Vier Befunde, die nur durch Messen kamen

1. **Die Zähler sind 32 Bit, nicht 16.** `rof.c` liest sie mit `fread_w` als
   Wort. Nachgemessen mit Proben von 0, 1, 2 und 3 Symbolen: `r68` schreibt
   Langwörter. Wer der Dokumentation folgt, verrutscht ab dem ersten Symbol.
2. **`r68` sortiert die Globalen alphabetisch**, nicht in Quellreihenfolge.
   Belegt an einer Quelle mit der Reihenfolge `wert`, `puffer`, `start` —
   ausgegeben wurde `puffer`, `start`, `wert`. Ohne dieselbe Reihenfolge gibt
   es keine Byteidentität.
3. **Der Code wird mit `NOP` (`$4E71`) auf ein Vielfaches von vier
   aufgefüllt.** Ein einzelnes `rts` ergibt `codsz` = 4.
4. **Im `vsect` haben initialisierte und reservierte Daten je einen eigenen
   Adressraum, beide ab 0.** Bei `d1 dc.l / d2 dc.l / u1 ds.b 4 / u2 ds.b 4`
   kommen die Adressen 0, 4 und 0, 4 heraus, `idatsz` = 8 **und**
   `statstorage` = 8.

### Das Typwort einer Referenz

Aus siebzehn gemessenen Fällen setzt es sich aus vier Teilen zusammen:

| Bit(s) | Bedeutung |
|---|---|
| `$80` | PC-relativ (`bsr fremd`, `lea fremd(pc),a0`) |
| `$40` | **abziehen** — der Anteil geht negativ ein (`dc.l fremd-basis`) |
| `$20` | die Referenz *liegt* im Code (ohne das Bit: in den init. Daten) |
| `$18`/`$10`/`$08` | Umfang: Langwort / Wort / Byte |
| untere Bits | **Ziel**abschnitt, wie bei den Globalen: Code 4, init. Daten 1, reservierte Daten 0, extern 0 |

Also `$3c` = Langwort im Code auf ein Codelabel, `$39` = auf initialisierte
Daten, `$38` = auf reservierte Daten oder einen externen Namen, `$19`/`$1c` =
dasselbe innerhalb des vsect, `$b0` = PC-relativ auf einen externen Namen,
`$7c` = ein abgezogenes Codelabel.

Globale: Code `$0004`, initialisierte Daten `$0001`, reservierte Daten
`$0000`. Weitere Fälle werden beim Ausbau **einzeln gemessen**, nicht
abgeleitet — bei jeder ungemessenen Kombination bricht `qr68` ab.

### Mehrere verschiebbare Anteile in einem Ausdruck

`r68` löst sie nicht auf, sondern legt **je Anteil eine Referenz auf denselben
Offset** ab. `move.b PD_PAR-PD_OPT+M$DTyp(a1),d0` (so steht es in den
SCF-Treibern, alle drei Namen extern) ergibt `$0030`, `$0070` und `$0030` auf
dem Displacementwort; `dc.l EA+EB` ergibt zwei Referenzen, `dc.l basis+basis`
ebenfalls. Nur die **Differenz zweier moduleigener Größen** rechnet r68 aus
und gibt gar keine Referenz aus — auch über Abschnittsgrenzen hinweg
(`dc.l dat-basis` mit `dat` im vsect: Wert 0, keine Referenz). Genau davon
leben die Indirektionstabellen von QCCs `-largedata`.

Multipliziert, geteilt, geschoben oder verundet werden darf nur ein Anteil
ohne Bezug — aber es zählt der **Teilausdruck**, nicht der ganze:
`(*-BaudTabl)/2` ist erlaubt (die Differenz ist eine Konstante), und
`\1+\1+\1+\1+256*4` aus `MACROS/os9svc.m` ebenfalls.

### Makroargumente werden an JEDEM Komma getrennt

Klammern zählen dabei **nicht** mit: `REGMOVE2 d0,(a0,d2.w)` sind für r68
drei Argumente (`d0`, `(a0`, `d2.w)`), und genau darauf baut
`MACROS/longio.m` — das Makro prüft `\#-3` und setzt sie mit
`move.b \1,\2,\3` wieder zusammen. Anführungszeichen zählen dagegen sehr
wohl: `#',',b` sind zwei Argumente.

### `*` ist der Ort der ZEILE

Nicht der laufende Ort: `dc.w *,*,*` auf Offset 2 ergibt dreimal `$0002`.
Wer den laufenden Ort nimmt, liegt ab dem zweiten Wert daneben — in
`SYSMODS/SYSCACHE/syscache.a:383` steht `dc.w F$CCtl,UsrCCtl-*-4`.

### PC-relativ: nur ein Codebezug wird ausgerechnet

`lea ziel(pc),a1` ergibt den **Abstand** (`$ffec`), `jmp 3(pc)` dagegen
`$0003` — der feste Wert steht unverändert als Displacement drin, ebenso
`lea WERT(pc),a2` mit `WERT equ 6`. Den Unterschied macht der **Ausdruck**,
nicht die Schreibweise: `pc` und `pcr` verhalten sich in beiden Fällen
gleich. (`SYSMODS/GCLOCK/tickgeneric.a:188`: `jmp 3(pc)`.)

Und bei einem **externen** Namen steht im Displacementwort nicht 0, sondern
der **konstante Anteil des Ausdrucks** — der Binder addiert ihn nicht
selbst dazu:

| Quelle | Displacement | Referenztyp |
|---|---|---|
| `move.l fremd(pc),d0` | `$0000` | `$00b0` |
| `move.l fremd+4(pc),d1` | `$0004` | `$00b0` |
| `move.l fremd-8(pc),d2` | `$fff8` | `$00b0` |
| `lea fremd+2(pc),a0` | `$0002` | `$00b0` |

Ohne das fehlt genau ein Wort: `ROM_CBOOT/sysinit.a` des Ports MVME147
legt mit `move.l VectTbl(pc),0(a0)` / `move.l VectTbl+4(pc),4(a0)` die
ersten beiden Vektoren an, und der zweite kam als `$0000` heraus statt als
`$0004`. Bei Summand 0 fällt der Fehler nicht auf — deshalb ist er so
lange durchgerutscht.

### Was sonst noch nur durchs Messen kam

- **Das Mnemonic endet auch an einer Klammer**, nicht nur am Leerzeichen: im
  SDK steht ` ifeq(CPUType-SYS360)` ohne Trennzeichen (auch
  `move.l(a0),d0` → `$2010`).
- **`|` ist ein zweites Zeichen für das bitweise ODER** neben `!`.
- **Auch die Grundform darf nach `ccr`/`sr`**: `and.w #$fe,ccr` wird
  `$023c`, `or.w #1,ccr` wird `$003c`, `and.w #$fe,sr` wird `$027c`.
- **Ein verschiebbares Displacement gibt es auch in der Indexform**:
  `move.b d0,dat(a2,d5.w)` ergibt eine Byte-Referenz auf das niederwertige
  Byte des Erweiterungswortes.

- **`use "datei"`** sucht im Verzeichnis der **einschließenden Datei** — das
  nackte `use datei` tut das nicht (dieselbe Datei nebenan wird dort nicht
  gefunden). Die Descriptor-Quellen des SDK leben davon
  (`use "scfdesc.a"` in `SRC/IO/SCF/DESC/p1.a`).
- **`\Ln`** ist die **Länge** des Arguments n, zweistellig (`a0` → `02`,
  leer → `00`). Die SDK-Makros prüfen damit die Art eines Arguments:
  `ifne \L1-2 / fail … must be a An register`.
- **`movea` ohne Größenbuchstaben ist ein LANGWORT**, `move` dagegen ein
  Wort (`movea PD_BUF(a1),a0` → `$2069`, `move d0,d1` → `$3200`).
- **`cc`** nimmt r68 neben `ccr` (aber nicht `c` oder `ccrx`) — in
  `rbvme10.a:1074` steht `ori #Carry,cc`, offenbar ein Tippfehler, den r68
  klaglos übersetzt.
- **`divu.l d1,d1`** trägt im Erweiterungswort unten **noch einmal `dq`**
  ein, nicht 0 (`$1001`). Bei `d0` fällt der Unterschied nicht auf — der
  Korpus hat ihn gefunden (`SYSMODS/GCLOCK/tk162.a`).

- **`equ` erbt einen externen Namen.** `IRQCtrl equ u_icr` (so in
  `sc68070.a`) bindet einen Namen an einen externen; jede Benutzung von
  `IRQCtrl` muss danach wieder eine Referenz auf `u_icr` erzeugen. Ohne das
  fehlen im ROF stillschweigend Referenzen — der Code ist byteidentisch, der
  Binder setzt die Adresse aber nie ein. Stehen mehrere unbekannte Namen
  rechts (`ILVLR4_default equ ILVLR4a+ILVLR4b+…`), legt auch r68 keine
  Referenz an.
- **`addi`/`subi` verkürzt r68 ebenfalls** zu `addq`/`subq` (1…8) — nicht
  nur `add`/`sub`.
- **`0x100`** ist neben `$100` ein gültiges Hexliteral (nur klein
  geschrieben; `0X10` lehnt r68 ab).
- **`pcr`** ist die zweite Schreibweise für `pc` und verhält sich genauso.
- **Ein globales `equ`** auf einen festen Wert bekommt das Typwort `$0006`,
  und in der Adresse steht der Wert selbst.

### Sofortwerte: drei gemessene Sonderfälle

- `move.b #fremd,d0` bekommt eine **Byte**referenz auf das *niederwertige*
  Byte des Erweiterungswortes (`$0028` auf Offset 3), die I-Formen dagegen
  eine Wortreferenz auf das ganze Wort: `cmpi.b #-1,d0` legt `$ffff` ab,
  `move.b #-1,d0` nur `$00ff`.
- `moveq #fremd,d1` bekommt eine Byte-Referenz auf das niederwertige Byte des
  **Befehlswortes** (`$0028` auf Offset 1). `addq`/Schiebeweiten lehnt r68
  mit einem externen Namen ab.
- `moveq` nimmt mehr als `-128..127`: `#$ff` wird `$70ff`, `#-129` wird
  `$707f`, erst ab 256 meldet r68 „value out of range".

### Reihenfolgen, ohne die es keine Byteidentität gibt

- **Globale**: alphabetisch nach Bytewerten.
- **Externe Namen**: ebenfalls alphabetisch (gemessen an einer Quelle, die
  erst `realloc` und dann `_os_write` braucht — ausgegeben wird `_os_write`
  zuerst). Die Referenzen unter einem Namen dagegen aufsteigend nach Offset.
- **Lokale Referenzen**: erst die im Code, dann die in den initialisierten
  Daten, je Gruppe mit **absteigendem** Offset.

### Auffüllen und Ausrichten

- Vor allem, was mindestens ein Wort breit ist, richtet `r68` selbst aus — und
  zwar **bevor** das Label der Zeile seinen Wert bekommt (`dc.b 1` /
  `lab: nop` ⇒ `lab` = 2).
- Ein einzelnes ungerades Byte wird mit `$00` gefüllt, danach mit `NOP`
  (`$4E71`) — im Code. Im `vsect` wird durchgehend mit `$00` gefüllt.
- Am Schluss wird der Code auf ein Vielfaches von vier gebracht, nach
  derselben Regel: erst das Nullbyte, dann NOPs.

Die vier Langwörter am Schluss sind offen: genau diesen Abschnitt hat
`osk-disasm` auskommentiert („common block variables… Do this after everything
else is done"). In allen Proben sind sie null — das ist das Messergebnis, kein
Platzhalter. Tritt ein Fall mit Inhalt auf, wird er gemessen.

## Was der Korpus verlangt

Aus 644 Dateien / 207.847 Zeilen gezählt:

**29 Direktiven:** `dc` `equ` `set` `ds` `align` `psect` `vsect` `ends` `org`
`use` `os9` `macro` `endm` `rept` `endr` `ifeq` `ifne` `ifgt` `ifge` `iflt`
`ifdef` `ifndef` `else` `endc` `fail` `opt` `nam` `ttl` `page`.
`use` (965×) ist Include, `os9` (742×) der Systemaufruf-Makro, `macro`/`endm`
(157×) heißt: **Makroverarbeitung ist Pflicht, nicht Beigabe.**

**Befehle:** 68000-Grundbestand plus 68010 (`movec` 220×, `moves` 9×),
68040-Cache/`move16` (61×), 68020-Bitfelder (`bfextu`/`bfins`/`bfffo`, 23×),
`divul` (8×) und PMMU (`pmove`, 4×). **Die FPU kommt in 207.000 Zeilen
nirgends vor** — sie bleibt draußen, mit klarem Abbruch statt stillem
Ignorieren.

## Was `r68` selbst umformt — und was nicht

Ohne diese Liste gibt es keine Byteidentität. Alles gemessen, nichts
hergeleitet:

**Umgeformt wird:**

- `add.l #4,d0` → **ADDQ**, `sub.l #4,a0` → SUBQ. Werte 1…8; außerhalb
  bleibt es ADDI/SUBI. Auch mit einem Symbol, dessen Wert erst später
  definiert wird — deshalb misst `qr68` in mehreren Durchläufen (s. u.).
- Ein Adressregister als Ziel wählt die A-Form: `add.l #9,a0` → ADDA,
  `cmp.l a1,a0` → CMPA, `move.l a5,a0` → MOVEA.
- `and.l #4,d0` → ANDI, ebenso `or`/`eor`/`cmp` — dort gibt es keine
  Kurzform.

**Nicht umgeformt wird:**

- `move.l #7,d0` bleibt MOVE (**kein** MOVEQ).
- `bra` ist **immer** die Wortform, auch wenn das Ziel in 8 Bit passt —
  `r68` warnt dann nur („destination in short branch range").
- `lea 0(a5),a0` bleibt die Displacementform; nur `lea (a5),a0` ist die
  indirekte.
- Ein nackter Ausdruck als Adresse ist **immer** absolut lang, auch
  `move.l $1000,d0`; `.w` am Operanden erzwingt die kurze Form.

**Was `r68` nicht kennt:** die Klammerform `(4,a5)` („parenthesis needed") —
nur `4(a5)`.

### Was `use`, `org`/`do` und die Makros wirklich tun

Alles an r68 gemessen, weil es sich anders liest, als man vermutet:

- **`use datei`** und **`use "datei"`** öffnen genau diesen Pfad, also
  relativ zum **Arbeitsverzeichnis** — *nicht* zum Verzeichnis der
  einschließenden Datei. **`use <datei>`** sucht in den `-u=`-Verzeichnissen
  (r68 nimmt dort zusätzlich ein festes `<MWOS>/OS9/SRC/DEFS`; qr68 liest
  keine Umgebungsvariablen, das Verzeichnis muss man ihm mit `-u=` nennen).
  Ein fehlendes `>` stört nicht: im SDK steht `use <memc040.d)` — Tippfehler
  in `systype.d` — und r68 übersetzt das anstandslos.
- **`org`** bewegt den Ort im Abschnitt **nicht**. Es setzt einen eigenen
  Zähler, auf den **`do.b/.w/.l`** Namen legt und den **`.`** liest — so
  beschreiben die SDK-Definitionsdateien ihre Strukturen (1593 `do` in 127
  Dateien). `do.w` und `do.l` richten dabei auf **gerade** aus, nicht auf
  ihre eigene Breite: `org 4 / A do.b 1 / B do.w 1` ergibt A=4, B=6.
- **Makros**: `\1`…`\9` sind die Argumente und werden **textuell** ersetzt,
  auch innerhalb von Anführungszeichen (`dc.b "\5",0` steht so im SDK);
  `\#` ist die Zahl der Argumente **zweistellig**, `\@` eine laufende
  Nummer **fünfstellig** (`lok00001`, beginnt bei 1). Ein fehlendes Argument
  wird zu nichts — es darf nicht abbrechen, denn die SDK-Makros prüfen `\#`
  und benutzen höhere Argumente nur in einem Zweig, den die bedingte
  Assemblierung dann überspringt.
- **Symbol- und Makronamen sind schreibungsabhängig** (`mactest` findet
  `MacTest` nicht), Befehlsnamen nicht.
- Hinter einem Befehl **ohne Operanden** ist das dritte Feld schon der
  Kommentar: `rte   * Kommentar` ist im Korpus üblich, und ein `*` dort ist
  kein Operand.

### Sprungweiten: `-b`

Ohne `-b` kodiert r68 **immer** die Wortform und warnt höchstens. Mit `-b`
wählt es selbst — und **übergeht den angegebenen Buchstaben ganz**: `bra.w`
auf ein nahes Ziel wird kurz, `beq.s` auf ein fernes wird zur Wortform. Ein
Ziel außerhalb des Moduls bleibt Wortform. Die SDK-Makefiles bauen alle
Treiber mit `-qb`, deshalb kann `qr68` das auch.

Bei **Abstand 0** — das Ziel ist die nächste Anweisung — lässt r68 den
Befehl ganz weg. Bei `bra`/`Bcc` ist das gleichbedeutend, bei `bsr` nicht
(die Rücksprungadresse fehlt dann). `qr68` bricht dort ab, statt eine
Bedeutungsänderung nachzubauen.

### Die Befehle des 68020/68030/68040, wie r68 sie kodiert

Alles gemessen; in mehr als einem Punkt hätte man es anders geraten.

**Bitfelder.** `$E8C0 | Kennung<<8 | ea`, danach ein Erweiterungswort — und
das steht **vor** den Erweiterungswörtern des Operanden:

```
bftst   8(a0){1:2}          -> e8e8 0042 0008
bfextu  (a1,d1.l){d2:1},d7  -> e9f1 7881 1800
```

Kennungen der Reihe nach: `bftst` 0, `bfextu` 1, `bfchg` 2, `bfexts` 3,
`bfclr` 4, `bfffo` 5, `bfset` 6, `bfins` 7. Im Erweiterungswort:

| Bit(s) | Bedeutung |
|---|---|
| 14..12 | Datenregister — `bfextu`/`bfexts`/`bfffo` das Ziel, `bfins` die Quelle; die vier ohne Register lassen es 0 |
| 11 | der Offset steht in einem Datenregister |
| 10..6 | Offset bzw. dessen Registernummer |
| 5 | die Breite steht in einem Datenregister |
| 4..0 | Breite bzw. deren Registernummer |

Weil das Erweiterungswort **vor** der Adresse liegt, zählt ein PC-Abstand ab
dem Wort dahinter: `bftst lab(pc){1:2}` auf Offset `$2a` ergibt `$ffd2`,
also den Abstand vom Adresswort auf `$2e`.

Beide Angaben dürfen **Ausdrücke** sein (`d0{WID:WID+1}` mit `WID equ 3` →
`$00c4`), und r68 **beschneidet sie auf 5 Bit** und warnt dabei nur
(„offset truncated to 5 bits" — auch wenn es die Breite meint). `{0:32}`
wird deshalb Breite 0, was in der Kodierung genau 32 bedeutet; genau so
steht es im Korpus (`bfffo d4{0:32},d4`). Einen **negativen** Wert lehnt
r68 ab („illegal addressing mode"), `qr68` ebenso.

**`move16`.** Nur die Form mit zwei Postinkrementen hat ein
Erweiterungswort — dort steckt `Ay` in Bit 14..12, Bit 15 ist gesetzt. Die
vier Formen mit absoluter Adresse haben keines; dort folgt die Adresse
direkt:

| Quelle | Bytes |
|---|---|
| `move16 (a0)+,(a2)+` | `f620 a000` |
| `move16 (a0)+,$12345678` | `f600` + Langwort |
| `move16 $12345678,(a1)+` | `f609` + Langwort |
| `move16 (a2),$12345678` | `f612` + Langwort |
| `move16 $12345678,(a3)` | `f61b` + Langwort |

**`pmove`.** `$F000 | ea`, dann ein Erweiterungswort — wieder **vor** der
Adresse (`pmove 8(a0),tc` → `f028 4000 0008`). Bit 9 gibt die Richtung an
(0 = in das MMU-Register, 1 = heraus), Bit 8 ist das `FD` von `pmovefd`:

| Register | Wort | mit `pmovefd` |
|---|---|---|
| `tc` | `$4000` | `$4100` |
| `srp` | `$4800` | `$4900` |
| `crp` | `$4c00` | |
| `tt0` | `$0800` | |
| `tt1` | `$0c00` | |
| `mmusr` (auch `psr`) | `$6000` | |

`pcsr` kennt r68 **nicht** („illegal register usage").

**`divul`/`divsl`.** Dasselbe Befehlswort wie `divu`/`divs`, aber mit
32-Bit-Dividend: Bit 10 bleibt **frei**, der Rest kommt trotzdem nach `dr`.

```
divul.l d1,d2:d0  -> 4c41 0002        divu.l d1,d2:d0  -> 4c41 0402
divsl.l d1,d2:d0  -> 4c41 0802        divs.l d1,d2:d0  -> 4c41 0c02
```

**`movec` auf 68040/68060.** `tc` 3, `itt0` 4, `itt1` 5, `dtt0` 6, `dtt1` 7,
`buscr` 8, `mmusr` `$805`, `urp` `$806`, `srp` `$807`, `pcr` `$808`.
Vorsicht: `tc`, `srp` und `mmusr` heißen bei `pmove` genauso und bezeichnen
dort etwas anderes — die beiden Tabellen sind in `qr68` absichtlich
getrennt.

### Ein Defekt in r68 V2.9.1

Drei Stellen, an denen `qr68` bewusst nicht folgt.

Die **lange Sprungform** (`bra.l`, `bsr.l`, `bcc.l`, 68020) ist kaputt: `r68`
gibt `6000 00000000` aus — ohne das nötige `$FF` im unteren Byte des
Befehlsworts und ohne den Abstand einzusetzen. Der erzeugte Sprung geht ins
Leere. Im ganzen handgeschriebenen Korpus kommt die Form zweimal vor
(`MWOS/OS9/SRC/IO/SCF/DRVR/sc68562.a:252`), beide Male trifft sie diesen
Defekt. `qr68` **bricht dafür ab** statt entweder den Defekt nachzubauen oder
still davon abzuweichen.

Und **`rept` spult falsch zurück**: für jede Wiederholung liest r68 die
Quellzeilen erneut, landet dabei aber mitten in einer vorangehenden Zeile.
Schon `delay35 / rept (35-5-9)/2 / nop / endr` — so steht es in
`MWOS/OS9/SRC/IO/SCF/DRVR/sc8x30.a` — ergibt neun `bad label`-Fehler. Die
erzeugten Bytes stimmen dabei zwar, als Orakel taugt es aber nicht; `qr68`
wiederholt genau den Rumpf zwischen `rept` und `endr`.

Und eine **Vorwärtsreferenz in einem Bitfeldzusatz schneidet den psect ab**.
Die Probe — `SPAET` steht erst hinter `ends`:

```
         psect   bfa,0,0,1,0,0
         bftst   d0{SPAET:2}
         nop
         nop
         nop
         nop
         ends
SPAET    equ     5
```

Das Erweiterungswort stimmt (`$0142`, also Offset 5 und Breite 2), aber r68
hört nach dieser Zeile auf zu listen und schreibt nur `codsz=8` statt 12 —
zwei der vier `nop` fehlen im Objekt. Ohne die Vorwärtsreferenz passiert
das nicht, und mit einem gewöhnlichen Befehl (`move.l #SPAET,d0`) auch
nicht; es hängt am Bitfeldpfad. Im Korpus kommt der Fall nicht vor — dort
sind Offset und Breite immer ein Register, `0` oder `32`. `qr68` übersetzt
solche Quellen **vollständig** und weicht damit an dieser Stelle bewusst
ab; als Orakel taugt r68 dafür nicht.

### `set` sieht immer den Stand nach dem ERSTEN Durchlauf

`r68` hat genau einen Messdurchlauf, und sein Ausgabelauf liest die Werte,
wie sie am **Ende dieses ersten Durchlaufs** standen. Die Probe:

```
         dc.w    A
         dc.w    B
A        set     B
B        set     5
```

`r68` legt `$0000 $0005` ab — beim ersten Lesen von `A set B` war `B` noch
unbekannt, und der Ausgabelauf sieht genau diesen Zwischenstand. Wer bis zur
Ruhe misst, bekäme zweimal `$0005`.

Genau daran hingen die Descriptor-Quellen: `pcfdesc.a` setzt
`WrtPrecomp set Cylnders` auf Dateiebene, **bevor** die Makroausdehnung
`Cylnders` überhaupt setzt. `qr68` hält deshalb den Stand der `set`-Symbole
nach dem ersten Durchlauf fest und stellt ihn vor dem Ausgeben wieder her —
die Längen dürfen weiter bis zur Ruhe gemessen werden, die Werte nicht.

### Mehrere Durchläufe statt zwei

Weil die Befehlslänge an Symbolwerten hängt (ADDQ), messen zwei feste
Durchläufe nicht sicher: ein Vorwärtsbezug kann alle Adressen dahinter
verschieben. `qr68` läuft deshalb so lange, bis sich **zwei Durchläufe
hintereinander** nichts mehr bewegt, und gibt erst dann aus (`-v` zeigt die
Durchläufe). `r68` selbst hat genau zwei und meldet in diesem Fall
„phasing error" — `qr68` ist hier also verträglicher, ohne bei den Quellen,
die `r68` annimmt, ein anderes Ergebnis zu liefern.

## Bewusste Entscheidungen

- **Zeitstempel ist fest** (`-fdate=J,M,T,S,Mi,Se` setzt ihn). Reproduzierbare
  Ausgabe ist wertvoller als eine Uhr im Objekt; für den Vergleich mit `r68`
  wird dessen Stempel damit nachgebildet.
- **Geschrieben im QCC-Subset** wie `qcpp` (keine Unions, kein `->`, kein
  `float`, Arraygrößen als Literale, feste Tabellen statt `malloc`), damit
  `qr68` sich später selbst übersetzen lässt.

  **Dazu gehört auch: keine aneinandergereihten Stringliterale.** Das
  ANSI-C-übliche

  ```c
  fatal("ein langer Text, der "
        "auf zwei Zeilen steht", x);
  ```

  lässt QCC mit `Schlusswort FAIL, 0 Meldungen` abbrechen — ohne
  Fehlermeldung und mit einer IR-Zeile, also praktisch ohne Hinweis auf die
  Ursache. Der Host-`cc` übersetzt es klaglos, `make check` bleibt grün;
  gefunden hat es erst `make os9`. **Lange Meldungstexte deshalb in eine
  Zeile schreiben**, auch wenn sie über 79 Zeichen gehen. (Zeiger-
  Ausgabeparameter wie `void f(int *p)` kann QCC dagegen sehr wohl —
  nachgemessen, nicht vermutet.)
- **Zeilenenden LF, CR+LF und CR** werden alle verstanden. OS-9-Textdateien
  enden mit CR — bei `qcpp` hat genau das im Emulator dafür gesorgt, dass die
  ganze Datei eine Zeile war.
- **An Modellgrenzen wird abgebrochen**, nicht geraten.

## Wo es beim Treiberkorpus noch klemmt

`./test/mwos.sh` fährt Quellen des SDK aus einem Port-Verzeichnis, so wie es
die SDK-Makefiles tun (`-qb -u=. -u=<DEFS> -u=<MACROS>`). Gezählt wird nur,
was `r68` selbst übersetzt — die meisten übersprungenen brauchen
Definitionen anderer Boards:

| Gruppe | Ergebnis |
|---|---|
| SCF-Treiber | 11 gleich, 3 abweichend |
| RBF-Treiber | **13 gleich, 0 abweichend** |
| SCF-/RBF-/PCF-Descriptoren | **15 gleich, 0 abweichend** |
| `SYSMODS/GCLOCK` (Uhren) | **12 gleich, 0 abweichend** |
| `SYSMODS/SYSGO`, `SYSCACHE`, `INIT` | **7 gleich, 0 abweichend** |
| `ROM/COMMON`, `ROM/SERIAL` (Bootcode) | **11 gleich, 0 abweichend** |
| `ROM_CBOOT/sysinit.a` der Ports Q9, CB030, MVME147, MVME167, MVME177 | **5 gleich, 0 abweichend** |
| SCF-Treiber des **Q9-Ports** | 4 gleich, 2 abweichend |

Dieselben Gruppen laufen auch aus den Ports **MVME147**, **CB030**,
**AtariST** und **Q9** heraus — dort greifen andere Bedingungen, und bis auf
dieselben bewussten Verweigerungen bleibt alles byteidentisch.

```
PORTDIR=…/PORTS/MVME172/RBF DRVDIR=…/SRC/IO/RBF/DRVR ./test/mwos.sh
```

`UEXTRA` nimmt weitere Suchverzeichnisse auf. Der ROM-Code braucht das:
`ROM_CBOOT/sysinit.a` holt `systype.d` aus dem **Wurzelverzeichnis** des
Ports (nicht aus dem eigenen) und `iniz050.a` aus `SRC/ROM/MVME050`. Ohne
den zusätzlichen `-u` fällt r68 auf sein eingebautes
`\mwos\OS9\SRC\DEFS` zurück und bricht ab — was wie ein fehlender
Korpusteil aussieht und keiner ist.

```
UEXTRA="…/PORTS/Q9 …/SRC/ROM/MVME050" PORTDIR=…/PORTS/Q9/ROM_CBOOT \
    DRVDIR=…/SRC/IO/SCF/DRVR ./test/mwos.sh …/PORTS/Q9/ROM_CBOOT/sysinit.a
```

Die verbliebenen Abweichungen sind **alle** bewusste Verweigerungen, und
zwar von genau drei Arten:

- **`sc68562`** benutzt `bsr.l`/`bcs.l` — die in r68 kaputte lange Sprungform
  (s. u.). `qr68` bricht dort ab.
- **`sc68990`**, **`oxc16954`** und **`gdp`** springen auf die unmittelbar
  folgende Anweisung; r68 lässt den Befehl mit `-b` weg, was sich nicht
  stabil nachbilden lässt (s. u.).
- **`sc8251a`** prüft `ifeq CPUType-FM16s` mit einem Namen, den es nicht
  gibt. r68 meldet dort „illegal external reference" und übersetzt **beide**
  Zweige — ein Ergebnis, das niemand haben will. `qr68` bricht ab.

## Was noch offen ist

### Der Befehlsvorrat ist zu (2026-09-04)

Die Restliste vom selben Tag — `bfextu`/`bfins`/`bfffo` (23×), `move16`
(20×) und `pmove` (4×) — ist abgearbeitet und in `test/insn.a` festgehalten.
Beim Durchmessen kamen **drei weitere Lücken** heraus, die die Restliste
nicht kannte, weil r68 die betroffenen Dateien ohne die zusätzlichen
Suchverzeichnisse selbst nicht übersetzte:

| Lücke | Wo sie sich zeigte |
|---|---|
| `divul.l` / `divsl.l` | `ROM_CBOOT/sysinit.a`, Ports MVME167 und MVME177 |
| `movec` mit den Kontrollregistern des 68040/68060 | dieselben zwei Dateien (`movec d0,tc`) |
| PC-relativ auf einen externen Namen **mit Summand** | `ROM_CBOOT/sysinit.a`, Port MVME147 |

Der dritte war ein stiller Fehler, kein Abbruch: `move.l VectTbl+4(pc),4(a0)`
kam als `$0000` statt `$0004` heraus. Bei Summand 0 fällt so etwas nicht auf
— das ist die Sorte Abweichung, die nur ein byteweiser Vergleich findet.

Damit ist der **Integerbestand für den Korpus vollständig**; die
**Direktiven** waren es schon. FPU kommt in 207.000 Zeilen nicht vor und
bleibt draußen.

Nur `ram.a` (RAMDISK, `move16`) ließ sich nicht gegenprüfen: r68 kommt in
keiner der hier vorhandenen RBF-Portkonfigurationen durch, weil die Makros
`ldbra` und `OS9svc` nicht hereingezogen werden. Die dort benutzte Form
`move16 (a0)+,(a2)+` ist über `test/insn.a` abgedeckt.

### Strukturelle Grenzen

- **Die Quelle wird ganz in eine Arena gelesen** (Host 4 MB, Ziel 256 KB).
  Die vom Backend erzeugten Dateien sind bis 18 MB — dafür bräuchte es einen
  strömenden Leser. Das ist auch der Grund, warum `qr68` seine *eigene*
  Modulquelle nur am Host assemblieren kann, nicht auf dem 68030.
  (Die Geschwindigkeit ist dagegen erledigt: mit Streutabellen für Namen und
  Symbole braucht eine 3,5-MB-Quelle **0,3 s** statt 8,3 s.)
- **Kein Listing.** `-l`, `-s`, `-g` werden angenommen und übergangen;
  `-z=<datei>` (Argumentdatei) fehlt ganz.
- **Das 1,18-MB-Modul** ist kein qr68-Problem, sondern QCCs Datenmodell
  (s. o.).

### Noch nicht geprüfte Korpusteile

Dateimanager, SCSI, der Rest von `ROM/CBOOT` (DISK, NETWORK, SYSBOOT) und
die Ports, für die hier die Board-Definitionen fehlen. `sysinit.a` aus
`ROM/CBOOT` ist jetzt aus fünf Ports geprüft und war der ergiebigste Teil:
drei der oben genannten Lücken kamen von dort. Die übrigen `ROM/CBOOT`-
Verzeichnisse sind der nächste Kandidat — „ungeprüft" ist nicht „geprüft",
und die Annahme, die Befundrate sei erschöpft, hat sich hier gerade als
falsch erwiesen.

## Nächste Schritte

1. **QCCs Datenmodell**: genullte Felder gehören in den reservierten
   Bereich, nicht in den initialisierten. Dafür müssen Globals über
   `a6 + 32-Bit-Offset` erreichbar werden statt über `d16(a6)`. Das würde
   qr68s Modul von 1,18 MB auf ~50 KB bringen, qcpps von 4,3 MB auf ~44 KB
   — und qr68 könnte dann seine *eigene* Modulquelle auch auf dem Ziel
   assemblieren.
2. Der Rest des Korpus — `ROM/CBOOT`/DISK, NETWORK, SYSBOOT zuerst.
3. Strömender Leser statt Arena.
4. `l68` — der Binder, das letzte große Fremdteil neben Microwares `clib`.
