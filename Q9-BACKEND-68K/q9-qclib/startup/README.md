# startup/ -- eigenes C-Startup-Modul (`q9_start.a`)

Der einzige Startcode der Kette.

## Entstehung

`q9_start.a` wurde aus der Herstellerdokumentation (Registerzustand beim
Programmstart, Speicherbild, Programmende) und der Schnittstelle der eigenen
`qclib` selbst geschrieben. Ergaenzt wurden nur Symbole, deren Fehlen gemessen
wurde (Linkerfehler beim Binden gegen die Referenz-C-Bibliothek, siehe
Kopfkommentar der Datei).

## Namensschema der eigenen Definitionen

| Praefix | Bedeutung |
|---|---|
| `Q$xxx`  | Systemaufrufe (Funktionscodes) |
| `IQ$xxx` | I/O-Aufrufe |
| `EQ$xxx` | Fehlercodes |
| `QM$xxx` | Felder des Modulkopfs |

## Verhalten

* nullt den nicht initialisierten Datenbereich selbst,
* zerlegt die Kommandozeile in `argc`/`argv` (Leerraum trennt, `'..'`/`".."`
  gruppieren, kein Escape; `argv[0]` = Modulname; `argv[argc]` = NULL),
* liefert einen leeren `envp`,
* ruft `_iobinit`, `_utinit`, `_initarg`, dann `main(argc, argv, envp)`, danach
  `exit(status)`; bei zu wenig Speicher endet der Prozess mit `EQ$MemFul`,
* stellt die Prozessvariablen bereit, die die Referenz-Bibliothek erwartet
  (`errno`, `_environ`, `_modhead`, `_modname`, `_sttop`, `_stbot`, `_mtop`,
  `_stklimit`, `_maxstack`, `_sysglob`).

## Bauen

    make build/q9_start.r        # im Verzeichnis q9-qclib

Binden wie bisher, mit `q9_start.r` (frueher `q9_cstart.r`):

    q9_ql68 -a build/q9_start.r prog.r -l=build/qclib.l -M=8K -O=prog

## Getestet

* Testprogramm mit `argc`/`argv`, BSS-Feld, initialisierter Variable, Exit:
  im Emulator auf dem 68030.
* Alle 19 vorhandenen `tests/*.sh` liefern mit `q9_start.r` dieselben
  Rueckgabewerte wie mit dem alten Startcode.
* `qcpp` und `ql68` (grosse Programme) laufen mit dem neuen Startcode und
  liefern dieselbe Ausgabe wie auf dem Host.

## Offen

* Exit-Status und der Fehlerpfad `EQ$MemFul` sind nicht im Emulator geprueft.
* `MINSTK` (1024) ist geschaetzt.
* Nur ein Init-Block (wie `ql68` ihn erzeugt) wird unterstuetzt.
* Keine Stackpruefung, keine Trap-/Signalbehandlung.
