# Q9DEFS -- eigene Systemdefinitionen (`q9sys.d`)

Zahlen, Offsets und Codes des Systems (Systemaufrufe, I/O-Aufrufe,
Ereigniscodes, Fehlercodes, Modulkopf, Deskriptorfelder, Signale) unter
eigenen Namen. Im installierten SDK gehoert die Datei nach `DEFS/`
(Assembler: `use q9sys.d`).

## Entstehung

* Eine Tabelle aus der Herstellerdokumentation (Namen, Werte, Bedeutung) und
  eine Tabelle der Aufrufnummern (aus der oeffentlichen Namenstabelle eines
  Open-Source-Disassemblers, GPLv3 -- nur die Zuordnung Name -> Nummer, keine
  Codeuebernahme) wurden von der Datei getrennt erstellt.
* `q9sys.d` wurde aus diesen Tabellen erzeugt (`gen/gen.py`). Erschlossene
  Werte sind im Kommentar mit `(inferred)`/`(erschlossen)` markiert, Namen ohne
  bekannten Wert sind NICHT definiert, sondern nur als Kommentar am
  Abschnittsende aufgefuehrt.

## Namensschema

| Handbuchname | unser Name | Bedeutung |
|---|---|---|
| `F$Xxx` | `Q$Xxx` | Kernel-Aufrufe |
| `I$Xxx` | `IQ$Xxx` | I/O-Aufrufe |
| `E$Xxx` | `EQ$Xxx` | Fehlercodes (16 Bit) |
| `M$Xxx` | `QM$Xxx` | Modulkopf-Felder |
| `Ev$`, `A$`, `S$`, `SS_`, `P$`, `D_` | `QEv$`, `QA$`, `QS$`, `QSS_`, `QP$`, `QD_` | Ereignisse, Alarm, Signale, Status, Prozess, Systemglobale |
| alle anderen | vorangestelltes `Q` | Modultypen, Sprachen, Modusbits, PD_/DD_/V_/FD_/DT_-Felder |

## Nicht definiert (kein Wert bekannt)

Neun Aufrufe (`Q$Attach`, `Q$FIRQ`, `Q$Mbuf`, `Q$OSCall`, `Q$Sema`,
`Q$Service`, `Q$SigReset`, `Q$Sigmask`, `Q$SysTrap`), alle `QSS_`-Statuscodes,
die Prozess-/Systemglobalfelder, die Alarmcodes und einige Zeichen-/Trapnamen.
Die Liste steht am Ende der jeweiligen Abschnitte in `q9sys.d`.

## Pruefen

    python3 gen/check.py q9sys.d     # keine doppelten Namen, keine Wertekollisionen

## Benutzen

    use q9sys.d      * relativ zum Arbeitsverzeichnis; im Makefile nach build/ kopieren

## include/ -- eigene C-Systemheader

Header fuer die Tool-Quellen (Q9-Tools: System, Network), 32 Dateien. Der Bedarf
wurde aus den Verbraucherprogrammen ermittelt (`NEEDS.md`: was wirklich gebraucht
wird), die Werte und Layouts stammen aus der Herstellerdokumentation bzw.
oeffentlichen Standards (BSD/POSIX). Definitionen ohne belegten Wert sind NICHT
definiert, sondern am Dateiende jedes Headers als "not defined" aufgelistet;
abgeleitete Definitionen tragen den Kommentar `derived:`.

Stand und Restfehler: `include/STATUS.md`. Pruefen:

    gen/chk_headers.sh <Verzeichnis mit den Tool-Quellen>

Bei der letzten Pruefung uebersetzen 60 von 81 Quelldateien im Nur-Syntax-Modus;
die Restfehler gehen auf offene Bestandteile zurueck (z. B. Prozessdeskriptor,
`S_I*`-Modusbits, `struct ifreq`, `enum clnt_stat`); drei Dateien
(`kr2iso`, `iso2kr`, `mbr`) waren nicht Teil der Bedarfsermittlung.
Die Dateinamen mit `os9` (z. B. `UNIX/os9def.h`) sind Namen, die die Verbraucher
per `#include` erwarten.
