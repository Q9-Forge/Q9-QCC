# Benutzerhandbuch

## Was das Projekt macht

`ebnf` liest eine EBNF-Grammatik und erzeugt daraus Parserquelltext. Die
Arbeitsdatei `<name>.lextab` enthält neben den generierten Tabellen auch die
bewusst editierbaren Blöcke für Lexer, Tests und Nutzer-Code.

## Bauen und Gesamttest

Auf macOS/Linux:

```sh
./runtests.sh
```

Der Lauf baut die benötigten Programme und prüft QCCVM, 68000-Simulator und
ARM64/Darwin. Erfolgreich ist der Lauf nur bei `=== ALLE TESTS OK ===`.

## QCC verwenden

Die Referenzgrammatik liegt in `Data/qcc.ebnf`. Nach ihrer Generierung wird der
QCC-Parser gebaut. Ein kleines Programm kann anschließend als IR erzeugt und
mit der VM ausgeführt werden:

```sh
build/parsec Data/qcc
cc -w -o build/qcc_p Data/qcc_p.c
build/qcc_p 'int main(){ putint(2 + 3 * 4); }' > build/example.ir
python3 tools/qccvm.py build/example.ir
```

## Unterstützte QCC-Funktionen

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

- `Data/qcc.ebnf`: Sprachgrammatik
- `Data/qcc.lextab`: generierte Tabellen plus Nutzer-Aktionen
- `tools/qccvm.py`: IR-Interpreter
- `runtests.sh`: verbindliche Regressionstests
- `docs/STATUS.md`: aktueller Stand
