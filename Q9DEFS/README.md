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
* `q9sys.d` wurde aus diesen Tabellen erzeugt (`gen/gen.py`), ohne eine
  Vorlage-Definitionsdatei zu lesen. Erschlossene Werte sind im Kommentar mit
  `(inferred)`/`(erschlossen)` markiert, Namen ohne bekannten Wert sind NICHT
  definiert, sondern nur als Kommentar am Abschnittsende aufgefuehrt.
* Das ist kein formaler Clean-Room-Prozess (siehe `../Q9-BACKEND-68K/q9-qclib/startup/README.md`).

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
