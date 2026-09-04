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
| **289 Quellen des MWOS-SDK**, mit den Aufrufen aus dessen eigenen Makefiles | **byteidentisch** |
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
`not`/`tst`/`tas`/`swap`/`ext`/`extb`, `addx`/`subx`/`abcd`/`sbcd`/`nbcd`,
`chk`, alle acht Schiebe- und Rotierbefehle
(Sofortwert, Register, Speicherform), `Scc` (14 Bedingungen), `bra`/`bsr`/
`Bcc` (kurz und Wort), `dbra`/`dbcc`, `jsr`/`jmp`, `link`/`unlk`,
`rts`/`rte`/`rtr`/`nop`/`trap`/`trapv`/`reset`/`stop`/`illegal`,
`cmpm`, `movep`, die 68020-Langformen von `mulu`/`muls`/`divu`/`divs`
(auch als `dr:dq`) und `divul`/`divsl`, die acht **Bitfeldbefehle**
`bftst`/`bfextu`/`bfchg`/`bfexts`/`bfclr`/`bfffo`/`bfset`/`bfins`,
`chk2`/`cmp2`, `pack`/`unpk`, `cas`, `bkpt`, `rtd`, `callm`/`rtm`,
`link.l` und `trapcc` (alle 16 Bedingungen, ohne Operand und mit `.w`/`.l`),
die 68040-Cachebefehle `cinva`/`cpusha`/`cinvl`/`cpushl`/`cinvp`/`cpushp`
und `move16` (alle fünf Formen), vom PMMU `pmove`/`pmovefd` (`tc`, `srp`,
`crp`, `tt0`, `tt1`, `mmusr`/`psr`), `pflush`/`pflusha`/`pflushs`,
`ptestr`/`ptestw`, `psave`/`prestore`, dazu `lpstop`, sowie
`movem` mit Registerlisten (`d0-d7/a0-a6`, bei `-(An)` mit umgekehrter
Maske), die Bitbefehle `btst`/`bset`/`bclr`/`bchg` (statisch und dynamisch),
`ori`/`andi`/`eori` nach `ccr`/`sr`, `move` von und nach `sr`/`ccr`/`usp`,
`exg`, und vom 68010 `movec` und `moves` — `movec` mit den
Kontrollregistern des 68000er-Kerns *und* denen des 68040/68060
(`tc`, `itt0`, `itt1`, `dtt0`, `dtt1`, `buscr`, `mmusr`, `urp`, `srp`,
`pcr`).

**Der Integerbestand ist damit vollständig** — nicht mehr nur für den
Korpus: es gibt keinen Integerbefehl mehr, den `r68 V2.9.1` assembliert und
`qr68` nicht. Nachgemessen, indem jeder Kandidat des 68000/68010/68020/
68030/68040/68060 einzeln durch **beide** Assembler geschickt wurde. FPU
kommt in den 207.000 Zeilen nirgends vor und bleibt draußen.

**Schlüsselwörter als Operand sind schreibungsUNabhängig** — Register-,
Kontrollregister-, MMU- und Cachenamen. `movec d0,DFC` ist dasselbe wie
`movec d0,dfc` (an r68 gemessen), und im SDK steht beides. **Symbolnamen
sind es nicht**, die bleiben schreibungsabhängig.

**Adressierungsarten:** alle zwölf des 68000 —  `Dn`, `An`, `(An)`, `(An)+`,
`-(An)`, `d16(An)`, `d8(An,Xn)`, `abs.w`, `abs.l`, `d16(PC)`, `d8(PC,Xn)`,
`#imm`.

Dazu die Direktiven `use` (Include, mit `-u=`-Suchliste), `org`/`do` (der
Strukturbeschreiber des SDK) und `.` als org-Zähler, die bedingte
Assemblierung (`ifeq`/`ifne`/`ifgt`/`ifge`/`iflt`/`ifle`/`ifdef`/`ifndef`/
`else`/`endc`), **Makros** (`\1`…`\9`, `\#`, `\@`) mit `rept`/`endr`, und
der Systemaufruf `os9`.

Von r68s Schaltern: **`-o=<datei>`/`-O=<datei>`** (Ausgabedatei), `-b`
(Sprungweiten selbst wählen), `-a<sym>[=<wert>]`, `-u=<verz>`. Angenommen
und übergangen werden die Listing- und Meldungsschalter
(`-q -l -g -e -s -n -x -c -f -r -m<n> -d<n>`), damit die
Aufrufe der SDK-Makefiles unverändert laufen; `-y`, `-bt`, `-j` und `-p<n>`
ändern die Ausgabe und werden **abgelehnt**, statt sie stillschweigend zu
übergehen.

`-o=` ist nicht Kosmetik: **von den 300 Makefiles des SDK, die `r68`
aufrufen, benennt keines die Ausgabe über die Stellung** — alle schreiben
`-o=$(RDIR)/$@` oder `-O=$@`. Ohne den Schalter erzeugt `qr68` zwar
dieselben Bytes, lässt sich in den Makefiles aber nicht einsetzen. Beide
Schreibungen kommen vor, und die Stellung relativ zur Quelle ist egal — wie
bei r68. Ohne Ausgabeangabe schreibt r68 **gar nichts** (es gibt keinen
Vorgabenamen); `qr68` meldet das als Aufruffehler, statt still nichts zu
tun. Die eigene Form `qr68 <eingabe.a> <ausgabe.r>` bleibt daneben gültig.

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

### `-m<n>` ändert die Ausgabe — es ist kein Listing-Schalter

Lange als kosmetisch abgetan; gemessen ist es der **Ziel-CPU-Schalter**, und
er verändert das Objekt an zwei Stellen. Ein psect mit einem einzelnen
`rts`:

| `-m` | `codsz` | Füllwort |
|---|---|---|
| (ohne) | 4 | `$4E71` (`nop`) |
| `-m0`, `-m1` | **2** | **kein Auffüllen auf 4** |
| `-m2` … `-m6` | 4 | `$51FC` (`trapf`) |

Ein ungerades Byte wird in allen Fällen zuerst mit `$00` auf gerade
gebracht; nur der Schritt auf ein Vielfaches von vier hängt an `-m`.
Dasselbe Füllwort nimmt `align` mitten im Code. Und **ab `-m2` nimmt r68
den skalierten Index an**, darunter meldet es „illegal addressing mode".

Daran hingen `cache030`/`cache040`/`cache349` in `SYSMODS/SYSCACHE` — drei
Module aus **derselben** Quelle, gebaut mit `-m3` bzw. `-m4`.

### Skalierter Index

`(An,Xn*1|2|4|8)`, die Skala als Zweierlogarithmus in **Bit 10..9** des
Erweiterungswortes:

```
move.l d1,(a5,d0*4)     -> 2b81 0c00
move.l (a5,d0.l*8),d1   -> 2235 0e00
```

Die Skalierung steht **hinter** der Breite (`d0.l*4`). Ohne `-m2` lehnt r68
ab — deshalb tut `qr68` es auch.

### Zeichen: einfache Anführungszeichen sind eine ZAHL

Der Unterschied ist scharf gemessen:

| Quelle | r68 |
|---|---|
| `dc.b 'abc'` | **„value out of range"** — es ist der Wert `$616263` |
| `dc.b "a"+1` | **„bad operand"** — auf einen Text kann man nicht rechnen |
| `dc.b ')'+$80` | `$a9` |
| `dc.b 2*'a'` | `$c2` |
| `dc.b 'a'&$0f` | `$01` |
| `dc.w 'ab'` / `dc.l 'abcd'` | `$6162` / `$61626364` |

Ein Komma **innerhalb** der Anführungszeichen trennt dabei nicht:
`dc.b ','+1` ist ein Operand (`$2d`). Genau davon lebt
`RBF/DRVR/RAMDISK/ram.a:145` — `dc.b "Ram Disk (Caution: Volatile",')'+$80`.
Als Text gelesen kamen dort zwei Bytes zu viel heraus, und alles dahinter
verschob sich.

### Makroargumente: die Anführungszeichen fallen weg

`r68` entfernt die **umgebenden doppelten Anführungszeichen** eines
Arguments. Probe — Makrorumpf `dc.b "\1",0` und `dc.b \L1`, Aufruf
`M "abc"`:

```
61 62 63 00     \1 ist abc, OHNE Anführungszeichen
03              \L1 zählt sie ebenfalls nicht mit
```

Ohne das entstünde `dc.b ""abc"",0`, was r68 selbst als „bad operand"
ablehnt. Daran hingen **alle 25 SBF-Descriptoren**: ihr Makro übergibt den
Treibernamen schon in Anführungszeichen
(`SBFDesc …,IRQPrior,"sbviper"`) und der Rumpf setzt ihn in `dc.b "\5",0`
ein.

### Wo das Mnemonic endet

Am Leerzeichen — oder direkt am Operanden, wenn der mit einem Zeichen
beginnt, das in keinem Mnemonic vorkommt:

| Quelle | |
|---|---|
| `ifeq(CPUType-SYS360)` | ja (so steht es im SDK) |
| `move.l(a0),d0` | ja → `$2010` |
| `andi.l#^$ff,d7` | ja (`ROM_CBOOT/sysinit.a:726`, MVME172) |
| `bra.s*+2` | ja |
| `move.l-(a0),d4` | ja |
| `move.l$1234.w,d2` | **nein** — `$` gehört noch zum Mnemonic, r68 meldet „bad mnemonic" |

### Das Labelfeld endet am Doppelpunkt

Auch ohne Trennzeichen davor: `DC_GetCluts:do.b 1` (`SRC/DEFS/funcs.a:725`)
ist Label + Direktive, nicht ein Label namens `DC_GetCluts:do.b`.

### Wann ein `:`-Label global wird

Zwei Regeln, beide gemessen:

- **Nur innerhalb des psect.** Labels davor und nach `ends` stehen nicht in
  der Globalenliste. Daran hingen sechs `*stat`-Dateien in `SRC/DEFS`, die
  ihre Feldabstände per `use` noch **vor** der psect-Zeile holen: r68 legt
  für `scfstat.a` null Globale an, `qr68` legte 21 an.
- **Ein `set`-Symbol wird nie global** — und ein **neues** mit Doppelpunkt
  lehnt r68 sogar ganz ab:

  ```
  Z:  set 2              -> "illegal global symbol", keine Ausgabe
  X   set 0 / X: set 1   -> angenommen, X ist NICHT global
  ```

  Der zweite Fall steht im Korpus: `SYSMODS/INIT/init.a:165` setzt
  `Compat set 0`, und `PORTS/RUSSBOX/systype.d:184` überschreibt es mit
  `Compat: set $00`.

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

**Die Paarform** (`addx`, `subx`, `abcd`, `sbcd`, `pack`, `unpk`) trägt das
**Ziel** in Bit 11..9 und die Quelle unten; Bit 3 wählt `-(aN),-(aM)` statt
`dN,dM`. Grundworte `addx` `$d100`, `subx` `$9100`, `abcd` `$c100`,
`sbcd` `$8100`, `pack` `$8140`, `unpk` `$8180`. Ohne Größenbuchstaben ist
`addx`/`subx` das **Wort** (`addx d0,d1` → `$d340`); `abcd`/`sbcd` tragen
gar kein Größenfeld.

**`chk`** hat die Breite in Bit 8..7, und zwar **Wort 3, Langwort 2**
(`chk.w (a0),d0` → `$4190`, `chk.l (a0),d2` → `$4510`); ohne Buchstaben das
Wort. **`chk2`/`cmp2`** legen ihr Erweiterungswort wieder **vor** die des
Operanden (`chk2.w 8(a1),d3` → `$02e9 $3800 $0008`): Bit 15 = Adress-
register, Bit 14..12 dessen Nummer, Bit 11 unterscheidet `chk2` von `cmp2`.

**`cas`** hat ein Größenfeld, das **eins größer** ist als das übliche —
Byte 1, Wort 2, Langwort 3 (`cas.w d0,d1,(a2)` → `$0cd2 $0040`).

**`trapcc`** ist `$50F8 | Bedingung<<8 | Form`, Form 4 ohne Operand
(`trapeq` → `$57fc`), 2 mit Wort, 3 mit Langwort. `trapt`/`trapf` sind die
Bedingungen 0 und 1.

**`pflush`.** `$F000 | ea`, Erweiterungswort `001` in Bit 15..13,
Betriebsart in Bit 12..10, **Maske in Bit 8..5 — vier Bit**, Funktionscode
in Bit 4..0. Die Maske ist die Falle: von `#0` bis `#8` wächst das Wort in
Schritten von `$20`, nicht `$40`. Betriebsart `1` für `pflusha`, sonst
`4 + (pflushs ? 1 : 0) + (Adresse genannt ? 2 : 0)` — gemessen `$3010`,
`$3410`, `$3810`, `$3c10`. Funktionscode: `#n` → `1nnnn`, `dN` → `01nnn`,
`sfc` → `00000`, `dfc` → `00001`.

### Ein Defekt in r68 V2.9.1

Vier Stellen, an denen `qr68` bewusst nicht folgt.

**`ptest` verliert die Adresse.** Braucht die Adressierungsart Erweiterungs-
wörter, legt r68 an deren Stelle die **Ebene** ab:

| Quelle | r68 | richtig wäre |
|---|---|---|
| `ptestr #1,16(a0),#3` | `f028 8e11 0003` | `… 0010` |
| `ptestr #1,32(a0),#5` | `f028 9611 0005` | `… 0020` |
| `ptestr #1,$1234.w,#3` | `f039 8e11 0000 0003` | `… 1234` |

Das Erweiterungswort selbst stimmt (Ebene, Funktionscode und Richtung sitzen
richtig) — nur die Adresse geht verloren, und der Befehl prüft danach eine
ganz andere Stelle. Heil ist allein `(aN)`, und nur das steht in echtem
Code. `qr68` **bricht für alles andere ab**. `pflush` mit Adresse und
`psave`/`prestore` sind davon **nicht** betroffen — nachgemessen
(`pflush #0,#0,16(a0)` → `f028 3810 0010`, korrekt).

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

  **Und kein zweistufiger Index auf ein Zeigerfeld.** `macArgP[i][k]` bei
  `char *macArgP[9]` meldet QCC als „array is not two-dimensional" (plus
  „pointer comparison expects char*, got int"). Anders als die
  Stringverkettung nennt es dafür immerhin Zeile und Grund. Abhilfe ist ein
  Zwischenzeiger: `ap = macArgP[i]; ap[k] = …`.
- **Zeilenenden LF, CR+LF und CR** werden alle verstanden. OS-9-Textdateien
  enden mit CR — bei `qcpp` hat genau das im Emulator dafür gesorgt, dass die
  ganze Datei eine Zeile war.
- **An Modellgrenzen wird abgebrochen**, nicht geraten.

## Der Korpus, mit den Aufrufen des SDK selbst

**`./test/sdkdiff.sh` ist der Prüfstand, auf den es ankommt:
289 Quellen byteidentisch, eine einzige Abweichung — und die ist eine
bewusste Verweigerung** (`sc68990`, s. u.).

Er kommt ohne jede Handkonfiguration aus (s. u.), und genau das war der
Punkt: die vorherige Zahl von 78 stammte aus geratenen Portverzeichnissen,
und in den 211 Quellen, die dadurch nie geprüft wurden, steckten **elf
echte Fehler** — vier davon still, also ohne Abbruch und mit falschen Bytes.
Wer nur zählt, was durchläuft, misst seine eigene Konfiguration.

Der ältere `./test/mwos.sh` bleibt daneben nützlich, wenn man eine bestimmte
Gruppe aus einem bestimmten Port fahren will; er bekommt
`PORTDIR`/`DRVDIR`/`UEXTRA` von Hand:

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

### Die Aufrufe aus den Makefiles holen, statt sie zu raten

`./test/sdkdiff.sh` fährt denselben Vergleich, aber **ohne dass die
Portkonfiguration von Hand gesetzt werden muss**. Der Hebel ist ein
Trockenlauf des SDK-eigenen Make:

```
MWMAKEOPTS=-u  os9make -nn -u
```

`-nn` **druckt** die Kommandos, statt sie auszuführen, und steigt dabei in
die Untermakes ab; `MWMAKEOPTS=-u` sorgt dafür, dass auch die Untermakes
alles bauen wollen — sonst schweigen sie, weil die `.r`-Dateien im Baum
schon liegen. Heraus kommt jede `r68`-Kommandozeile mit ihren echten
Schaltern, Suchverzeichnissen und `-a`-Definitionen:

```
r68 -q  -u=. -u=..\..\..\..\SRC\DEFS ..\..\..\..\SRC\IO\SCF\DESC\term.a -o=RELS\term.r
r68 -qb -u=. -u=..\..\..\..\SRC\DEFS -u=..\..\..\..\SRC\MACROS -aNODATAPORT \
        ..\..\..\..\SRC\IO\SCF\DRVR\sc8x30.a -o=RELS\sc172.r
```

In den SDK-Baum wird dabei **nichts** geschrieben: `os9make` führt nichts
aus, und die `-o=`-Angabe biegt das Skript auf ein Temporärverzeichnis um.
Damit fällt das Raten weg, das `test/mwos.sh` nötig macht — und mit ihm der
Verdacht, ein „übersprungen" sei eine Portfrage und kein Befund.

Der Lauf über alle 195 Verzeichnisse mit einem `makefile`:

```
651 Aufrufe: 289 gleich, 1 abweichend, 130 uebersprungen, 231 doppelt
```

- **289 gleich** — byteidentisch, Kopf, Code, Globale und Referenzen.
- **1 abweichend** — `sc68990`, eine der bewussten Verweigerungen.
- **231 doppelt** — `-nn` steigt in die Untermakes ab und druckt deren
  Kommandos mit dem Arbeitsverzeichnis **des Kindes**. Da jedes Verzeichnis
  mit einem `makefile` ohnehin einzeln angefahren wird, sind das
  Wiederholungen; das Skript weist sie getrennt aus, statt sie als Lücke
  erscheinen zu lassen.
- **130 übersprungen** — dieselbe Klasse, nur liegt das Verzeichnis des
  Untermakes tiefer, als die Suche reicht. Stichprobe: `tickgeneric`,
  `scsiglue` und `syscalls` stehen sämtlich unter den 289 geprüften.

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
zwar von genau vier Arten (die vierte, `ptest`, kommt im Korpus nicht vor):

- **`sc68562`** benutzt `bsr.l`/`bcs.l` — die in r68 kaputte lange Sprungform
  (s. u.). `qr68` bricht dort ab.
- **`sc68990`**, **`oxc16954`** und **`gdp`** springen auf die unmittelbar
  folgende Anweisung; r68 lässt den Befehl mit `-b` weg, was sich nicht
  stabil nachbilden lässt (s. u.).
- **`sc8251a`** prüft `ifeq CPUType-FM16s` mit einem Namen, den es nicht
  gibt. r68 meldet dort „illegal external reference" und übersetzt **beide**
  Zweige — ein Ergebnis, das niemand haben will. `qr68` bricht ab.

## Was noch offen ist

### Was `r68` selbst nicht kann — und `qr68` deshalb auch nicht

Nachgemessen, damit „`qr68` bricht ab" nicht mit „hier fehlt etwas"
verwechselt wird. In allen folgenden Fällen lehnt **r68 genauso ab**:

| Konstrukt | r68-Meldung |
|---|---|
| **Skalierter Index** `(a0,d0*4)`, `8(a0,d0.l*8)`, `pea (a0,d0*2)` | „illegal addressing mode" |
| Klammerform `(8,a0,d0.w)`, `(bd,a0,d0.l)` | „parenthesis needed" |
| Symbol doppelt definiert (`X equ 1` / `X equ 2`) | „redefined label" |
| Schieben, unäres Nicht, Mal, Geteilt auf einem Abschnittsbezug (`dc.w lab>>2`, `^lab`, `lab*2`, `lab/2`) | „illegal external reference" |
| Speicherform `asl.b (a0)` | „illegal size" |
| `pload` | „bad mnemonic" |

Das ist mehr als eine Fußnote: **r68 V2.9.1 kennt das skalierte Indizieren
gar nicht.** Quellen wie `SRC/IO/SCF/DRVR/sc68360.a` oder
`CPU32/PORTS/QUADS/ROM_CBOOT/sysinit.a`, die es benutzen, übersetzt r68
selbst nicht — sie sind kein offener Posten von `qr68`, sondern gar nicht
erst prüfbar. Auch die 68020-Formen mit Speicherindirektion
(`([bd,An],Xn,od)`) fehlen r68 vollständig.

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

Danach wurde nicht mehr nur der Korpus gezählt, sondern **jeder Integer-
befehl des 68000/68010/68020/68030/68040/68060 einzeln durch beide
Assembler geschickt**. Das brachte 23 weitere, darunter sechs aus dem
Grundbestand des 68000:

| | Befehle |
|---|---|
| 68000 | `addx`, `subx`, `abcd`, `sbcd`, `nbcd`, `chk` |
| 68020 | `chk2`, `cmp2`, `pack`, `unpk`, `bkpt`, `rtd`, `cas`, `callm`, `rtm`, `link.l`, `trapcc` |
| PMMU/68060 | `pflusha`, `pflush`, `ptestr`/`ptestw`, `psave`, `prestore`, `lpstop` |

Dazu die **Schreibungsunabhängigkeit** der Schlüsselwörter: `regNum` und
`ccr`/`sr`/`usp` waren es schon, die Tabellen für `movec`, `pmove` und die
Cachekennungen nicht — `movec d0,DFC` brach ab. Betrifft real
`CPU32/PORTS/QUADS` und `68000/PORTS/RUSSBOX`.

**Damit ist der Integerbestand vollständig**: es gibt keinen Integerbefehl
mehr, den r68 assembliert und `qr68` nicht. Die **Direktiven** waren es
schon. FPU kommt in 207.000 Zeilen nicht vor und bleibt draußen.

Der Rundumlauf über alle 355 handgeschriebenen Quellen (`qr68` allein, ohne
r68-Vergleich, mit einer generischen Konfiguration) meldet danach keinen
unbekannten **Befehl** mehr — die verbliebenen „nicht kodierbar" sind
durchweg Makro- und Descriptornamen, deren Makrodatei in dieser
Konfiguration nicht eingebunden ist (`ldbra`, `tpad`, `diskh1pfmt`,
`t0`…`t33`).

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

### Was der Makefile-Prüfstand gekostet hat — und warum er sich lohnte

Elf Fehler in 211 Quellen, die vorher nie geprüft wurden. **Vier davon
liefen ohne Abbruch durch und lieferten still falsche Bytes** — die
Zeichenkonstante in `ram.a`, die überzähligen Globalen der `*stat`-Dateien,
das Füllwort unter `-m3`/`-m4` und der PC-relative Summand. Genau diese
Klasse findet ein Testlauf, der nur „bricht ab / bricht nicht ab" prüft,
grundsätzlich nicht.

Und zweimal war die eigene frühere Messung schuld: „r68 kennt den
skalierten Index nicht" und „`-m<n>` ist kosmetisch" stimmten beide nur,
weil die Probe den Schalter nicht gesetzt hatte. **Wer eine Fähigkeit
ausschließt, muss sie mit den Schaltern prüfen, unter denen sie benutzt
wird.**

### Noch nicht geprüfte Korpusteile

Alles, was **kein Makefile-Ziel** ist, sieht `sdkdiff.sh` nicht: Quellen,
die nur per `use` eingebunden werden, und Verzeichnisse ohne `makefile`.
Ebenso die Ports, für die hier die Board-Definitionen fehlen — dort kommt
`r68` selbst nicht durch, und ohne Orakel gibt es nichts zu vergleichen.

Der nächste Schritt wäre, den Prüfstand um die **Kommandozeilen der
`*.make`-Dateien** zu erweitern, die kein `makefile` daneben haben.

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
