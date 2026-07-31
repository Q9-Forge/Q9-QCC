# QCC Mehrdatei-Projekt

Dieses kleine Beispiel prüft den ersten praktischen Workflow:

```text
main.tc + math.tc
        ↓ qcc_p
        IR pro Datei
        ↓ qcc_merge.py
        project.ir
        ↓ qcc_backend -os9
        project.s68
        ↓ r68 + l68
        project.out
```

Auf dem Mac genügt:

```sh
./build.sh
```

Das erzeugte OS‑9-Modul bekommt standardmäßig den Namen der ersten Quelldatei,
also hier `build/main.out`. Existiert die Datei bereits, wird automatisch
`main.1.out`, `main.2.out` usw. verwendet. Mit `-o` kann der Name vorgegeben
werden; ein vorhandenes Ziel wird dann überschrieben:

```sh
./build.sh -o demo
```

Wenn nur bis zum Assembler getestet werden soll:

```sh
./build.sh --no-link
```

## Direkter OS-9-Build

Der OS-9-Bashtreiber `q9build` führt denselben Ablauf direkt im Q9 aus:

```text
q9build main.tc math.tc
```

Er verwendet `merge`, `qcc_backend`, `r68` und `l68` aus `/dd/CMDS` sowie
die Libraries aus `/dd/LIB`.

Das Programm gibt beim Ausführen `42` mit CR/LF aus.
