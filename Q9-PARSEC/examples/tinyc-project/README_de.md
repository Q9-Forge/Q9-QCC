# Tiny-C-Mehrdatei-Beispiel

[English](README.md)

Dieses historische Beispiel beschreibt eine Mehrdatei-Kette: `main.tc` und
`math.tc` werden getrennt übersetzt, ihre IR wird zusammengeführt und durch
Backend, Assembler und Linker zu einem Modul verarbeitet. Das Beispielprogramm
soll `42` ausgeben.

Die Skripte verwenden weiterhin die früheren Werkzeugnamen `tinyc_p`,
`tinyc_backend` und `tinyc_merge.py`. Diese Werkzeuge liegen in diesem
Arbeitsstand nicht an den vorgegebenen Pfaden; das Beispiel ist daher derzeit
kein geprüftes Buildrezept. Für das Linken erwartet das Host-Skript außerdem
eine konfigurierte Cross-Toolchain.

```sh
./build.sh --no-link
./build.sh -o demo
```

`--no-link` endet nach der Assemblererzeugung durch das Backend. Mit `-o` kann
die benannte Ausgabedatei überschrieben werden. Das Host-Skript entfernt bei
jedem Lauf sein Verzeichnis `build/.work` und legt es neu an. Ohne expliziten
Ausgabenamen wählt es einen freien nummerierten Namen.

Das separate Skript `q9build` beschreibt einen nativen Q9-Ablauf mit Befehlen
aus `/dd/CMDS` und Bibliotheken aus `/dd/LIB`. Auch seine Voraussetzungen und
der Lauf auf dem Zielsystem müssen noch geprüft werden. Die Kette verwendet
`tinyc_p`, `merge`, `tinyc_backend`, `r68` und `l68`:

```sh
q9build main.tc math.tc
```
