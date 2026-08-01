# OS-9/Q9-Bootstrap

Stand: **2026-07-29**

Der Tiny-C-Weg bis zu einem ausführbaren OS-9/68k-Modul ist auf dem Q9-
Emulator verifiziert:

```text
C-Quelle → tinyc_p → IR → tinyc_backend → .s68 → r68 → .r → l68 → OS-9-Modul
```

## Verifizierter Ablauf

Auf OS-9 kann der Parser eine Datei einlesen und IR erzeugen:

```text
tinyc_p @/dd/HOME/ROOT/test_one.tc >/dd/HOME/ROOT/test_one.ir
```

Das Backend erzeugt daraus Microware-Assembler:

```text
tinyc_backend test_one.ir test_one.s68 -os9
r68 -o=test_one.r test_one.s68
```

Der abschließende `l68`-Link benötigt `cstart.r` als erstes Modul sowie die
Microware-Bibliotheken `clib.l`, `os_lib.l` und `sys.l`. Das resultierende
Programm wurde auf Q9 ausgeführt und gab `42` aus.

## Erster Mehrdatei-Workflow

Ein kleines reproduzierbares Projekt liegt unter
`examples/tinyc-project/`. Es enthält `main.tc` und `math.tc` und wird mit
dem Bash-Script gebaut:

```sh
examples/tinyc-project/build.sh
```

Das Script führt den vollständigen Ablauf aus:

```text
mehrere .tc → tinyc_p → IR pro Datei → tinyc_merge.py → gemeinsames IR
           → tinyc_backend → r68 → l68 → project.out
```

Das erzeugte Modul bekommt standardmäßig den Namen der ersten Quelldatei;
im Beispiel liegt es unter `examples/tinyc-project/build/main.out`. Existiert
das Ziel bereits, werden `.1`, `.2` usw. angehängt. Mit `-o NAME` kann der
Name gezielt vorgegeben und das Ziel überschrieben werden. Mit `--no-link`
lässt sich zunächst nur bis zur `.s68`-Datei testen.

## Native Parser-Prüfung

Der selbstübersetzte EBNF-Parser ist ebenfalls als OS-9-Modul lauffähig.
Ein dateibasierter Treiber liest eine `.tc`-Datei und meldet `OK`/`FAIL`.
Die Kommandozeilenvariante akzeptiert:

```text
./tinyc_file_driver /dd/HOME/ROOT/test_one.tc
```

Die Ausgabe `OK` bestätigt das erfolgreiche Parsen. Die IR-Aktionen sind in
diesem kleinen Parser-Demotreiber noch nicht integriert; für den praktischen
C→IR-Weg wird derzeit `tinyc_p` verwendet.

## Bekannte Grenzen

- Der Mehrdatei-Ablauf ist als Beispielscript verpackt; ein allgemeines
  `cc`-Frontend mit Projektkonfiguration gibt es noch nicht.
- Der OS-9-EBNF-Treiber verwendet für den Basisnamen derzeit noch den festen
  Namen `tinyc`; der Tiny-C-Dateitreiber kann bereits `argv[1]` verwenden.
- Die große Parser-Tabelle ist auf einem 80-Spalten-Terminal stark umgebrochen;
  das ist nur ein Anzeigeproblem.
