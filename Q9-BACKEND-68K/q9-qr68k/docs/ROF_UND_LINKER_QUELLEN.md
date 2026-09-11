# ROF und Linker — was die Originaldokumentation hergibt

Aufgenommen am 2026-09-04, nachdem `qr68` fertig war. Zweck: bevor die
Arbeit an einem `l68`-Ersatz beginnt, soll feststehen, **was schon
beschrieben ist** und **wo es steht** — damit nicht ein zweites Mal
gemessen wird, was Microware selbst dokumentiert hat.

Der Befund vorweg: **die Dokumentation ist vollständiger als gedacht, und
sie deckt sich an jeder Stelle mit dem, was an `r68` gemessen wurde.** Sie
ersetzt das Messen nicht (sie schweigt zu Reihenfolgen, Auffüllregeln und
allen Defekten), aber sie liefert die Feldtabellen und — für `l68` — das
Modulformat und den Linkeralgorithmus.

## Wo die Quellen liegen

| Quelle | Inhalt |
|---|---|
| `MWOS/DOC/PDF/ultrac_use.pdf` | **Die Hauptquelle.** Kap. 6 „Assembler and Object Code Linker Overview" mit dem **ROF-Format** (S. 236–260), Kap. 9 „Object Code Linker" (S. 341–370) mit Algorithmus, **Bibliotheksformat** und Modulkopf-Überschreibungen, Kap. 10 die Werkzeuge |
| `MWOS/DOC/PDF/68k_tech.pdf` | **Modulformat**: „Module Header Definitions", CRC, Kopfparität |
| `MWOS/DOC/PDF/utils.pdf` | Werkzeugreferenz; zu `l68` nur der Debug-Bezug (`-g`, STB-Modul) |
| `MWOS/OS9/SRC/DEFS/module.h`, `module.a` | Modulkopf als Quelltext: `MODSYNC 0x4afc`, `CRCCON 0x800fe3` |
| `MWOS/DOC/{Books,Microware,RadiSys}` | weitere Handbücher, nicht durchgesehen |

Text herausziehen mit `/opt/homebrew/bin/pdftotext -layout` (liegt nicht im
nicht-interaktiven PATH).

## ROF: welche Edition r68 V2.9.1 schreibt

Die Dokumentation nennt **zwei** ROF-Formate, Edition 9 und Edition 15, und
innerhalb von 9 die Untervarianten 9.1 und 9.2. Unterschieden werden sie an
Feldgrößen:

- **9.0**: Zähler (externe Definitionen, Referenzen, lokale Referenzen)
  sind **2 Byte**
- **9.1**: dieselben Zähler sind **4 Byte**
- **9.2**: zusätzlich „Code Information" (2 Byte) und „Header Expansion"
  (2 Byte) im Kopf, Kopf also 60 statt 56 Byte

Unser gemessener Kopf ist **56 Byte** und die Zähler sind **32 Bit** →
`r68 V2.9.1` schreibt **ROF Edition 9.1**.

**Das ist nicht nur erschlossen, sondern bestätigt.** Microwares eigenes
`rdump` sagt es über eine von `qr68` erzeugte Datei:

```
Module name:  init
Asm valid:    Yes
CPU/ROF type: 680x0/9.1
  Code:      00000140
Excpt entry:  ffffffff
```

Zwei Dinge nebenbei: `rdump` liest `qr68`-Ausgaben anstandslos — es ist
damit ein **zweites Orakel** neben `r68`, und zwar eines, das die Felder
benennt statt Bytes zu zeigen. Und der fehlende siebte psect-Parameter
erscheint dort als `Excpt entry: ffffffff`, also genau das gemessene
`utrap = -1`.

**Damit ist die alte Unstimmigkeit erklärt.** `MWOS/APPS/src/osk-disasm/rof.c`
liest die Zähler mit `fread_w`, also 16-bittig — dieser Leser implementiert
**Edition 9.0**, nicht 9.1. Es war kein Fehler in rof.c, sondern eine
andere Formatversion. (Gemessen hatten wir das ohnehin richtig; jetzt ist
auch der Grund bekannt.)

Für die Bibliotheken gilt: „Processors using ROF edition number 9 use
**library format number 1**." Das ist das Format von `sys.l`, das `l68`
über `-l=` liest.

## Wo Dokumentation und Messung sich decken

### Referenz-Typwort — vollständige Bitbedeutung

Die Messung hatte die Bits aus 17 Proben zusammengesetzt; die
Dokumentation benennt sie:

| Bit(s) | Dokumentation | gemessen als |
|---|---|---|
| 0–2 | Zielabschnitt (Bit 2 = Code; Bits 0–1 = 00 uninit., 01 init., 10 uninit. remote, 11 init. remote) | „untere Bits Zielabschnitt (Code 4, idata 1, udata 0)" |
| 3–4 | Umfang: 01 = 1 Byte, 10 = 2 Byte, 11 = 4 Byte | `$08` / `$10` / `$18` |
| 5 | Ort der Referenz: 0 = Daten, 1 = Code | `$20` „liegt im Code" |
| 6 | negativ — der Wert geht abgezogen ein | `$40` „abziehen" |
| 7 | relativ — bezogen auf den Ort der Referenz | `$80` „PC-relativ" |
| 8 | Common-Block | nie erzeugt |
| 9 | remote (0 = nicht remote) | nie erzeugt |

Das Handbuch gibt als Beispiel `0x00b0` = „2 bytes, relative reference flag
set, code" — genau der Wert, der für `move.l fremd(pc),d0` gemessen wurde.

### Typwort einer externen Definition

Die Dokumentation zählt die gültigen Werte abschließend auf:

```
0x0000 uninitialisierte Daten     0x0004 Code
0x0001 initialisierte Daten       0x0005 set
0x0002 uninit. remote             0x0006 equ
0x0003 init. remote               0x0100 Common
                                  0x0102 Remote Common
```

Deckt sich mit den gemessenen `$0004` (Code), `$0001` (idata), `$0000`
(udata) und `$0006` (globales `equ`).

**Eine Feinheit, die auffällt:** `0x0005` (`set`) steht als gültiger Wert im
Handbuch — aber `r68 V2.9.1` **lehnt** ein neues globales `set`-Symbol ab
(„illegal global symbol", gemessen). Der Assembler ist hier also enger als
das Format. Ein Linker muss `0x0005` trotzdem lesen können.

### Kopffelder

Die Reihenfolge und Breite der 56 Kopfbytes ist im Handbuch Feld für Feld
beschrieben und stimmt mit der gemessenen Tabelle im README überein, bis
hin zu den Bedeutungen: „Assembly Valid" ist ungleich null, **wenn beim
Assemblieren Fehler auftraten** — deshalb ist dieses Wort in unseren
Ausgaben immer 0.

## Was die Dokumentation NICHT sagt

Alles, was wir messen mussten, steht dort nicht:

- die **alphabetische Sortierung** von Globalen und externen Namen,
- die **Reihenfolge** der lokalen Referenzen (erst Code, dann Daten, je
  Gruppe absteigend nach Offset),
- die **Auffüllregeln** (Nullbyte auf gerade, dann `nop`/`trapf`/gar
  nicht — je nach `-m<n>`),
- dass ein `:`-Label nur **innerhalb** des psect global wird,
- sämtliche **Defekte** von `r68 V2.9.1`.

Das Messen bleibt also der Prüfstein. Die Dokumentation gibt die Struktur
vor, die Messung die Bytes.

---

# Für `l68`: was schon feststeht

## Das Modulformat (`68k_tech.pdf`, `module.h`)

Ein Modul besteht aus **Kopf**, **Rumpf** und **CRC**:

- **Sync** `$4AFC` (`MODSYNC`) — daran erkennt der Kernel Module im ROM.
- **Kopfparität** `M$Parity`: das **Einerkomplement des XOR der
  vorangehenden Kopfworte**. Schnelltest vor dem CRC.
- **CRC**: 24 Bit über das **ganze** Modul, vom ersten Syncbyte bis
  unmittelbar vor den CRC-Wert. Polynomkonstante `CRCCON = 0x800fe3`
  (`module.h`). Der Kernel lehnt Module mit falschem CRC ab; `fixmod`
  rechnet ihn nach.

Kopffelder: `M$ID`, `M$SysRev`, `M$Size`, `M$Owner`, `M$Name` (Offset auf
den Namensstring), `M$Accs`, `M$Type`, `M$Lang`, `M$Attr`, `M$Revs`,
`M$Edit`, `M$Usage`, `M$Symbol`, `M$Ident`, `M$HdExt`, `M$HdExtSz`,
`M$Parity`. Typcodes: 1 Prgm, 2 Sbrtn, 4 Data, 11 TrapLib, 12 Systm,
13 Flmgr, 14 Drivr, 15 Devic. Sprachcode 1 = 68000-Maschinencode.

## Der Linkeralgorithmus (`ultrac_use.pdf`, Kap. 9)

```
linker [options] <mainline> [<rof2> ... <rofN>]
```

`mainline` ist die Datei mit dem **Wurzel-psect** — erkennbar an einem
Typ/Sprach-Wert ungleich null in der `psect`-Zeile. Nur daraus entsteht der
Modulkopf; die übrigen ROFs dürfen keinen Wurzel-psect enthalten. Wurzel
und Unterprogrammdateien landen **immer** im Modul, auch unreferenziert.

**Erste Phase.** Alle Eingabedateien in Kommandozeilenreihenfolge lesen,
psects prüfen, globale Definitionen in die Symboltabelle (Doppeldefinition
= Fehler), unaufgelöste Referenzen sammeln. Danach die Bibliotheken **eine
nach der anderen**, bis nichts mehr offen ist. Was dann noch offen ist, ist
ein Fehler. Ein **`symbol`-psect** (nur Konstanten, kein Code, keine Daten)
ist ein Sonderfall: daraus wird nur übernommen, was als unaufgelöst
vermerkt ist — deshalb gehört er **ans Ende** der Dateiliste. Am Ende der
ersten Phase stehen die Größen von Code, Daten, initialisierten Daten und
Remote-Bereich sowie alle Code-Offsets fest.

**Zweite Phase.** Modulkopf schreiben, jeden psect erneut lesen, die
Referenzen auf ihre Zielposition im Modul umrechnen, Code und
initialisierte Daten schreiben — **den CRC dabei mitrechnen** und zuletzt
anhängen.

Das Layout: Kopf, dann die Codeabschnitte der psects in Reihenfolge, dann
die initialisierten Daten, dann der CRC. Der **Datenbereich** des Prozesses
wird getrennt aufgebaut: erst alle uninitialisierten, dann alle
initialisierten Variablen, dann dasselbe für remote.

## Modulkopf-Überschreibungen

Der Linker erkennt bestimmte **globale Labels** und trägt ihren Wert in den
Modulkopf ein — typischerweise per `equ` gesetzt:

| Label | Feld |
|---|---|
| `_m_tylan` | Typ/Sprache |
| `_m_attrev`, `_sysattr` | Attribute/Revision |
| `_m_edit`, `_sysedit` | Edition |
| `_m_access`, `_sysperm` | Zugriffsrechte |
| `_m_grpusr` | Eigentümer |
| `_m_exec` | Ausführungseinsprung |
| `_m_excpt` | Ausnahmeeinsprung |
| `_m_stack` | Stackbedarf |
| `_m_usage`, `_syscmnt` | Kommentarstring (nur OS-9/68K) |

## Symbole, die der Linker selbst definiert

`btext`/`_btext` (Modulanfang), `etext`/`_etext` (Modulende),
`bname`/`_bname` (Offset auf den Modulnamen), `end`/`_enddata` (letzter
Datenoffset), `_jmptbl` (Sprungtabelle). Mit `-r` verschiebt sich die
Bedeutung von `etext` auf den Anfang der initialisierten Daten, und
`edata`/`_birefs` kommen hinzu.

## Werkzeuge, die es schon gibt

- **`rdump`** — zerlegt eine ROF-Datei. Das ist ein zweites Orakel neben
  `r68` selbst und sollte beim Aufbau von `l68` benutzt werden.
- **`libgen`** — erzeugt Bibliotheken (`.l`), Format 1 für Edition 9. Der
  Aufbau (Header, Hashtabelle der globalen Definitionen, String-Tabelle,
  psect-Abschnitt, interne und externe Referenzabschnitte) ist in Kap. 9
  ab S. 355 vollständig tabelliert.
- **`Q9-Tools/System/qid`** — eigener Leser für Module und ROF.
- **`Q9-qr68/tools/rofcmp.py`, `rofhunks.py`** — der byteweise Vergleich,
  auf Module übertragbar.

## Der Prüfstand steht bereits

`Q9-qr68/test/sdkdiff.sh` holt die `r68`-Kommandozeilen per
`MWMAKEOPTS=-u os9make -nn -u` aus den SDK-Makefiles. **Derselbe
Trockenlauf druckt die `l68`-Aufrufe mit:**

```
l68 -l=..\..\..\..\68000\LIB\sys.l -gu=0.0 -p=577 RELS\term.r -O=..\CMDS\BOOTOBJS\term
```

Allein in 20 von 195 Verzeichnissen sind das 148 Aufrufe. Für einen
`l68`-Ersatz muss an dem Skript nur die Filterzeile und der Vergleich
getauscht werden — die Mechanik (Trockenlauf, Ausgabe umbiegen, byteweiser
Vergleich, ehrliche Zählung) ist fertig.

## Empfohlene Reihenfolge

1. `rdump` und `qid` gegen eigene Proben laufen lassen, um das Lesen von
   ROF und Modul abzusichern.
2. Modulkopf und CRC an einem **vorhandenen** Modul nachrechnen, bevor
   eines erzeugt wird.
3. Den einfachsten Fall zuerst: ein einziger ROF ohne Bibliothek, ohne
   Remote-Daten, ohne Sprungtabelle.
4. `sdkdiff.sh` auf `l68` umstellen und ab da byteweise vergleichen.
