# Q9-ql68 — der Binder der Q9-Werkzeugkette

Ausführbares Programm: **`ql68`**

```
ql68 [Optionen] <eingabe.r> -O=<modul>
```

Ziel: Microwares `l68` ersetzen. Nach `qcpp` (Präprozessor), QCC (Compiler)
und `qr68` (Assembler) ist der Binder das letzte große Fremdteil der Kette
— danach bleibt nur noch Microwares `clib`.

```
QCC / qcpp  →  .a  →  qr68  →  .r (ROF)  →  ql68  →  OS-9-Modul
                                            ^^^^
```

## Methode

Dieselbe wie bei [`Q9-qr68`](../Q9-qr68/): **`l68` ist das Orakel.** Jede
Formatfrage wird an einer kleinen Probe gemessen und byteweise verglichen,
nicht aus Dokumentation abgeleitet. Was Microware selbst beschreibt, steht
in `../Q9-qr68/docs/ROF_UND_LINKER_QUELLEN.md` — es gibt die Struktur vor,
die Messung die Bytes.

**`l68` ist byteweise reproduzierbar** — und zwar ohne jede Ausnahme.
Anders als `r68`, das sechs Zeitstempelbytes einträgt, enthält ein
OS-9-Modul gar kein Datum. Zweimal binden ergibt zweimal dieselbe Datei.
Der Vergleich braucht deshalb kein Gegenstück zu `qr68 -fdate=`.

> **Falle beim ersten Messen:** der **Modulname kommt aus dem Ausgabenamen**
> (`-O=`), nicht aus dem psect. Zwei Läufe mit verschiedenen Ausgabenamen
> unterscheiden sich deshalb in genau vier Bytes — dem Namen und dem CRC
> darüber. Das sah nach fehlender Reproduzierbarkeit aus und war keine.

## Was gemessen ist

### Der Modulkopf

Aufbau nach `MWOS/OS9/SRC/DEFS/module.h` (`struct modhcom` + `mod_exec`),
Byte für Byte an einem gebundenen Modul nachgeprüft:

| Offset | Feld | im Beispiel |
|---|---|---|
| `$00` | `M$ID` Sync | `4afc` |
| `$02` | `M$SysRev` | `0001` |
| `$04` | `M$Size` | Gesamtgröße **einschließlich** Kopf und CRC |
| `$08` | `M$Owner` | `00010000` ohne `-gu=` |
| `$0c` | `M$Name` | Offset auf den Namensstring |
| `$10` | `M$Accs` | `0555` ohne `-p=` |
| `$12` | `M$TyLan` | aus dem psect |
| `$14` | `M$AttRev` | aus dem psect |
| `$16` | `M$Edit` | aus dem psect |
| `$18` | `M$Usage` | 0 |
| `$1c` | `M$Symbol` | 0 |
| `$20` | `M$Ident` | 0 |
| `$22` | 6 Reservebytes | 0 |
| `$28` | `M$HdExt` | 0 |
| `$2c` | `M$HdExtSz` | 0 |
| `$2e` | `M$Parity` | s. u. |
| `$30` | `M$Exec` | Einsprung, Modulabstand |
| `$34` | `M$Excpt` | Ausnahmeeinsprung |
| `$38` | `M$Data` | Gesamtgröße des Datenbereichs |
| `$3c` | `M$Stack` | aus dem psect |
| `$40` | `M$IData` | Abstand auf die initialisierten Daten |
| `$44` | `M$IRefs` | Abstand auf die Zeigerlisten |

Danach: Name (nullterminiert, auf gerade aufgefüllt), Code, IData, IRefs,
CRC.

### Kopfparität

**Das Einerkomplement des XOR aller Kopfworte von `$00` bis `$2d`.**
Gemessen und nachgerechnet — stimmt beim ersten Versuch.

### CRC

24 Bit, **Polynom `$800063`**, Startwert `$FFFFFF`, über das Modul **ohne**
die letzten drei Bytes, das Ergebnis **komplementiert**.

Die Konstante `CRCCON = $800FE3` aus `module.h` ist **nicht** das Polynom,
sondern der **Sollrest**: rechnet man den CRC über das *ganze* Modul
einschließlich seiner drei CRC-Bytes, kommt genau `$800FE3` heraus. Genau
das prüft der Kernel. An `l68`s Ausgabe nachgerechnet, beides stimmt.

### Der Datenbereich

**Erst die uninitialisierten, dann die initialisierten Daten.** Bei
`wert dc.l / zeiger dc.l / puffer ds.b 8` liegt `puffer` auf 0..7 und
`wert`/`zeiger` auf 8..15 — deshalb nennt der IData-Abschnitt den Offset 8.

### IData-Abschnitt

```
<Offset in den Datenbereich: 4 Byte>
<Anzahl Bytes: 4 Byte>
<die Bytes>
```

### IRefs-Abschnitt

**Zwei Listen**: zuerst die Zeiger, auf die der **Code**basis aufaddiert
wird, dann die auf die **Daten**basis. Jede Liste ist eine Folge von
Gruppen

```
<msw: 2 Byte>  <Anzahl: 2 Byte>  <lsw: 2 Byte × Anzahl>
```

und endet mit einer Gruppe der Anzahl 0. Die vollständige Adresse eines
Eintrags ist `msw<<16 | lsw` — der Abstand im Datenbereich, an dem der zu
relozierende Zeiger liegt.

Gemessen an vier Zeigern (`p1 dc.l start`, `w1 dc.l $aabbccdd`,
`p2 dc.l start`, `p3 dc.l p1`): Liste 1 nennt `$0000` und `$0008` (die
beiden Codezeiger), Liste 2 nennt `$000c` (den Datenzeiger), `w1` kommt in
keiner Liste vor. Beim leeren Modul sind beide Listen leer — daher die acht
Nullbytes plus ein Füllbyte auf gerade Länge.

### Der A6-Bias von `$8000`

Der Zugriff auf die eigenen Daten läuft über `a6` — und `a6` zeigt **nicht**
auf den Anfang des Datenbereichs, sondern `$8000` dahinter. So reicht ein
16-Bit-Displacement ±32K weit statt nur 0…32K.

Gemessen: aus `move.l zeiger(a6),d1` mit `zeiger` auf Datenoffset `$000c`
macht `l68` das Displacement **`$800c`**. Der Bias gilt nur für dieses
Displacement im Code — ein 32-Bit-Zeiger *in* den Daten bleibt
unvorgespannt (`p3 dc.l p1` mit `p1` auf Offset 0 ergibt 0).

### Zwei Kleinigkeiten, die man sonst sucht

- **`M$Excpt` wird 0, nicht `$ffffffff`.** Fehlt der siebte
  psect-Parameter, trägt `r68` `utrap = -1` in den ROF ein; `l68` macht
  daraus im Modul die 0.
- **Die Zeigerlisten sind aufsteigend sortiert.** Der ROF liefert seine
  lokalen Referenzen absteigend nach Offset — `l68` dreht sie um.

### Bibliotheken sind kein Sonderformat

`-l=sys.l` liest **eine Folge von ROF-Dateien**, hintereinander in einer
Datei — kein libgen-Format. Nachgemessen an
`MWOS/OS9/68000/LIB/sys.l`: sieben ROFs, 1747 Globale, **alle vom Typ
`$0006` (equ)**, und die Längenrechnung landet exakt auf dem Dateiende.
Genau das meint die Dokumentation mit „the `sys.l` library module …
contains only `equ` symbol definitions".

Die Länge eines ROF ist dabei: 56 + Name + Globale + Code + init. Daten +
externe Referenzen + lokale Referenzen + **vier abschließende
Langwörter**.

### Externe Referenzen

Der Wert des Symbols wird an der Referenzstelle **aufaddiert** — der
Assembler hat dort schon den konstanten Anteil des Ausdrucks abgelegt.
Umfang und Ort stehen im selben Typwort wie bei den lokalen Referenzen.

Dazu kommen die Symbole, die **erst der Binder kennt** (Tabelle 9-8/9-9
des Handbuchs): `bname`/`_bname` (Offset auf den Modulnamen),
`btext`/`_btext`, `etext`/`_etext`, `end`/`_enddata`. Sie werden **nach**
den Bibliotheken eingetragen und verdecken damit eine gleichnamige
Definition von dort.

### Gerätedescriptoren haben einen anderen Aufbau

Ein Modul vom **Typ 15 (Devic)** benutzt nicht `mod_exec`, sondern
`mod_dev`: sein ROF-**Code ist die Kopferweiterung** — Portadresse,
Vektor, IRQ-Ebene, Modus, die Namensoffsets für Dateimanager und Treiber —
und liegt unmittelbar hinter dem gemeinsamen Kopf auf `$30`. Danach folgt
nur der Name und der CRC; **es gibt weder IData- noch IRefs-Abschnitt**.

Gemessen an `SRC/IO/SCF/DESC/term.a`: die Namensoffsets im Code springen
von `$0034`/`$0038`/`$003e` auf `$0064`/`$0068`/`$006e` — genau um die
Codebasis `$30`.

## Stand

**Ein einzelner ROF wird byteidentisch zu `l68` gebunden — auch echte
SDK-Module.**

| Probe | Inhalt | Ergebnis |
|---|---|---|
| `test/tiny.a` | nur Code | **byteidentisch** (102 Byte) |
| `test/dat.a` | Code, `dc`- und `ds`-Daten, ein `a6`-Displacement | **byteidentisch** (118 Byte) |
| `test/ref.a` | vier Zeiger: zweimal Code, einmal Daten, einer ohne Bezug | **byteidentisch** (130 Byte) |

| **7 SCF-Descriptoren des SDK** (`term`, `t1`–`t3`, `p1`–`p3`) | mit `sys.l`, `-gu=0.0`, `-p=577`, je ~24 externe Referenzen | **byteidentisch** |

`./test/difftest.sh` fährt die eigenen Proben, `./test/descs.sh` die
Descriptoren des SDK mit den Aufrufen aus dessen Makefile. Beide
vergleichen byteweise;
`tools/modcmp.py` benennt bei einer Abweichung das betroffene Kopffeld,
statt nur einen Offset zu zeigen.

### Was als Nächstes ansteht

1. **Mehrere ROFs binden** — psects aneinanderreihen, Globale auflösen.
   Das ist der Schritt zu Treibern und Programmen.
2. Symbole, die **nicht** absolut sind (Code- und Datenbezüge aus einer
   Bibliothek), und damit die Zeigerlisten für externe Referenzen.
3. Die restlichen Schalter der SDK-Makefiles: `-M=`, `-a`/`-j`
   (Sprungtabelle), `-r` (rohe Ausgabe), `-g` (STB-Modul).
4. **Den Prüfstand aus den SDK-Makefiles speisen.** `os9make -nn -u`
   druckt die `l68`-Aufrufe genauso mit wie die von `r68` — allein in 20
   von 195 Verzeichnissen sind es 148. Bei `qr68` hat genau dieser
   differentielle Prüfstand elf Fehler gefunden, vier davon still.
