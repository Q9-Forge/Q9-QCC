# Benutzerhandbuch

## Was das Projekt macht

`parsec` liest eine EBNF-Grammatik und erzeugt daraus Parserquelltext. Die
Arbeitsdatei `<name>.lextab` enthält neben den generierten Tabellen auch die
bewusst editierbaren Blöcke für Lexer, Tests und Nutzer-Code.

## Bauen und Gesamttest

Auf macOS/Linux:

```sh
./runtests.sh
```

Der Lauf baut die benötigten Programme und prüft TinyVM, 68000-Simulator und
ARM64/Darwin. Erfolgreich ist der Lauf nur bei `=== ALLE TESTS OK ===`.

## Tiny-C verwenden

Die Referenzgrammatik liegt in `Data/tinyc.ebnf`. Nach ihrer Generierung wird der
Tiny-C-Parser gebaut. Ein kleines Programm kann anschließend als IR erzeugt und
mit der VM ausgeführt werden:

```sh
build/parsec Data/tinyc
cc -w -o build/tinyc_p Data/tinyc_p.c
build/tinyc_p 'int main(){ putint(2 + 3 * 4); }' > build/example.ir
python3 tools/tinyvm.py build/example.ir
```

## Unterstützte Tiny-C-Funktionen

Verfügbar sind Variablen, eindimensionale Arrays, Funktionen mit Parametern und
Rekursion, `if/else`, `while`, `return`, `putint`, `putuint`, `putchar`,
arithmetische und logische Ausdrücke sowie Pointer. Pointer werden typisiert;
`char*` skaliert byteweise, `int*` elementweise in 4-Byte-Schritten.

Beispiel:

```c
int main() {
  int a[2] = {10, 20};
  int *p = a;
  *(p + 1) += 2;
  putint(*p);
  putint(p[1]);
}
```

## Wichtige Dateien

- `Data/tinyc.ebnf`: Sprachgrammatik
- `Data/tinyc.lextab`: generierte Tabellen plus Nutzer-Aktionen
- `tools/tinyvm.py`: IR-Interpreter
- `runtests.sh`: verbindliche Regressionstests
- `docs/STATUS.md`: aktueller Stand

## OS-9/Q9-Workflow

Der Tiny-C-Compiler kann auf dem Q9/OS-9-Emulator bereits die einzelnen
Übersetzungsschritte ausführen. Eine vollständige Übersicht mit den nötigen
Microware-Komponenten steht in [`OS9_BOOTSTRAP.md`](OS9_BOOTSTRAP.md).

Kurzform:

```text
tinyc_p @quelle.tc >quelle.ir
tinyc_backend quelle.ir quelle.s68 -os9
r68 -o=quelle.r quelle.s68
```

Danach wird `quelle.r` mit `cstart.r`, `clib.l`, `os_lib.l` und `sys.l` zu
einem ausführbaren OS-9-Modul gelinkt.
