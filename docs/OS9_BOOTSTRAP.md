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

- Der vollständige Ablauf ist verifiziert, aber noch nicht als ein einziges
  `cc`-Kommando verpackt.
- Der OS-9-EBNF-Treiber verwendet für den Basisnamen derzeit noch den festen
  Namen `tinyc`; der Tiny-C-Dateitreiber kann bereits `argv[1]` verwenden.
- Die große Parser-Tabelle ist auf einem 80-Spalten-Terminal stark umgebrochen;
  das ist nur ein Anzeigeproblem.
