# q9-qclib – OS-9000/x86

## Aktueller Umfang

Die CPU-unabhängige Logik wurde aus der vorhandenen 68k-CLib abgeleitet.
Assembleradapter und Systemaufrufe sind getrennte x86-Implementierungen.

- ASCII-Zeichenklassen, Zeichenketten-/Bytefunktionen, Ganzzahlkonvertierung.
- Prozesslokaler 64-KiB-Allocator mit Aufteilen und Zusammenführen freier
  Blöcke; `malloc`, `free`, `calloc`, `realloc`.
- Ungepufferte Streams mit 16 Plätzen, Standardpfaden 0/1/2, kurzen Transfers,
  Fehler-/EOF-Zustand, einem Pushback-Zeichen, Positionierung und `freopen`.
- `printf`, `fprintf`, `sprintf`: Ganzzahlen/Strings, C89-Flags, Breite und
  Präzision einschließlich `*`, sowie `h`/`l`. Fließkomma und `%n` fehlen und
  werden mit -1 zurückgewiesen.
- `qsort` und `bsearch` aus der 68k-C-Logik, mit cdecl-Callbacks.

Die öffentlichen Header liegen im gemeinsamen `Q9-FRONTEND-C/q9-qcpp/include`
und in `Q9DEFS/include`. `__Q9_X86__` wählt echte Standardstreams und eine
32-Bit-Pfadnummer. Strukturen mit Kernel- oder Modulformatbezug sind nicht
pauschal von 68k übertragbar.

## Bauen

`make` erzeugt `build/qclib.r`, einen zusammenhängenden Strom privater
QCC-ROF-PSECTs. Er ist keine indexierte Microware-Bibliothek und trägt deshalb
nicht die Endung `.l`. Der Diagnosepfad `qlx86 --emit-prototype` kann diese
PSECTs zusammen mit einem Tool zur Symbolauflösung verwenden.

`make runtime-candidate` erzeugt zusätzlich `qclib-os9000-candidate.r` mit
`path_os9000.c`, `syscalls_os9000.c`, `process_os9000.c` und `os9trap.sx86`. Der Adapter folgt dem beobachteten
Edition-1-Parameterblock aus dem benachbarten `Q9-OS-x86`-Quellbaum, darunter
Open-Ausgabe bei +18 und GetStat-Pufferzeiger bei +18. Die späteren, anders
gepolsterten SDK-Strukturen werden damit nicht gleichgesetzt. Lesen,
Schreiben, Schließen, Positionierung, Anlegen und Kürzen sind umgesetzt.
Append fehlt. Modul-Link/Unlink, externe Speicherkopien und Prozess-/Modul-
verzeichnisse haben eigene Edition-1-Blöcke. Dieser Adapter ist noch nicht in einem OS-9000-Gast validiert.

## Ausgeführte Prüfungen

```sh
python3 tests/run_backend_semantics.py
python3 tests/run_backend_semantics.py --clib
python3 tests/run_backend_semantics.py --syscalls
cc -std=c89 -fno-builtin -Wall -Wextra -Werror src/file.c tests/stream_adapter.c -o /tmp/q9-stream-check
/tmp/q9-stream-check
```

Die beiden Python-Läufe kompilieren mit QCC, assemblieren mit QRX86 und linken
mit QLX86. QEMU führt die tatsächlichen i386-Bytes aus. Geprüft werden u.a.
Argumentreihenfolge und Stackbereinigung, interne/externe/statische Callbacks,
Zeigerrückgaben, Feldadressen, lokale Arrays, Byte-/Wortzugriffe, Formatierung,
Speicherverwaltung und Sortierung. Der Streamlauf prüft kurze Transfers,
Pushback, EOF/Fehler und Positionierung gegen einen lokalen Adapter.

Diese Prüfungen verwenden einen kleinen Multiboot-Rahmen und eigene
Pfadadapter. Sie sind keine Prüfung des OS-9000-Loaders oder der Trap-ABI.

## Noch erforderlich

Ein vollständiger OS-9000-Startcode, prozessgetrennte Datenadressierung,
Loader-Relokationen, weitere Systemdienste und die Socket-/RPC-Laufzeit fehlen.
Die erzeugten `.qmod`-Dateien sind private Diagnosecontainer und keine
lauffähigen OS-9000-Programme. Auch die vollständige Tool-Portierung bleibt
deshalb offen.

Stand 2026-11-18: Alle 82 C-Übersetzungseinheiten der Q9-Tools erzeugen private
x86-Objekte; zuletzt bestanden 42 Diagnose-Links. IPv4-Konvertierungen sind
unter QEMU geprüft. Der zusätzliche --syscalls-Lauf prüft gepackte Blöcke,
Fehlerweitergabe und Architekturstrukturen gegen einen Mock-Trap. Daraus folgt
keine Bestätigung auf einem OS-9000-Gast. Die interne skalare cdecl-Konvention
ist außerdem noch nicht Microwares dokumentierte x86-Register-ABI.

Die experimentelle Prozessschicht bietet `exit`/`atexit` mit 32 Handlern und
Streamabschluss. Der Mock prüft umgekehrte Callback-Reihenfolge, F$Exit-Status
sowie Attach/Detach. Ein nicht zurückkehrender F$Exit auf dem Zielsystem ist
noch zu bestätigen; `_Exit` kehrt auch bei unerwarteter Trap-Rückkehr nicht
zum Aufrufer zurück.

Kalenderrechnung (`gmtime`, `localtime`, `mktime`) und F_TIME/F_STIME/F_SLEEP
sind ergänzt und unter QEMU geprüft. `localtime` verwendet derzeit UTC;
Zeitzonen/Sommerzeit fehlen. Die Tests decken Schaltjahr, Normalisierung,
negative Zeitwerte und die Grenzen des vorzeichenbehafteten 32-Bit-time_t ab.
Die OS-9000-fd_stats-Struktur (48 Byte, Epoch-Zeiten) und GetStat/SetStat für
Dateideskriptoren werden getrennt vom alten 68k-RBF-Sektor abgebildet.

Weitere Kandidaten: F$Load/F$GBlkMp, stat/fstat/chmod, makdir und chown. Die
Metadatenaufrufe lesen echte Deskriptorantworten; der Mock prüft Werte und
Parameterblöcke. chown erhält sonstige Felder beim Zurückschreiben. Die
Verzeichnisauflistung braucht noch getrennte RBF-/PCF-Formate. OS-9000-RBF
verwendet 64-Byte-Einträge und längere Namen als das Legacy-68k-Format.

XDR-Speicherstreams für die RPC-Werkzeuge sind ergänzt: Integer, gepolsterte
Bytes/Strings, Arrays und optionale Objekte mit Callback und Freigabe. Die
QEMU-Fixtures vergleichen feste Netzwerkbytes sowie Fehler bei kurzen oder
übergroßen Eingaben. Grundlage ist [RFC4506](https://www.rfc-editor.org/rfc/rfc4506.html).
Die Speicherkonvention gehört zu Q9; fremde Microware-x_ops werden abgelehnt.
NetDB-, Socket- und RPC-Transportanbindung fehlen weiterhin.
