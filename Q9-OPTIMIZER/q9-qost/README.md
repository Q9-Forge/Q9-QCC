# qost — Q9 Stack-IR Optimierer

`qost` ist ein eigenständiger, backend-neutraler Optimierungsschritt für Q9
Stack-IR (`.ir`). Aufruf:

```sh
make -C Q9-OPTIMIZER/q9-qost
Q9-OPTIMIZER/q9-qost/build/qost input.ir output.opt.ir
```

Der erste Pass faltet konstante 32-Bit-Integer-Ausdrücke, wenn die Operanden
unmittelbar als `PUSH` vorliegen. Unterstützt werden Addition, Subtraktion,
Multiplikation (nur ohne signed Überlauf), Bitoperationen, Vergleiche,
`NEG`, `NOT`, `NOTBIT` und `NARROWC`. Da `PUSH` kein Vorzeichen-/Typattribut
trägt, lässt der Optimierer positive Konstanten oberhalb `INT32_MAX` stehen,
statt signed oder unsigned zu erraten. Arithmetische Überläufe bleiben
unverändert; dadurch wird keine Annahme über C-Overflow-Verhalten in die IR
eingeführt. Pointer-Opcodes, Speicherzugriffe, Aufrufe, Division durch null,
Labels und Kontrollfluss werden nicht umgeschrieben. Unbekannte oder
fehlerhafte Zeilen bleiben unverändert.

Die Eingabe bleibt zeilenweise strukturell erhalten: Kommentare und
Zeilenenden dienen als Barrieren und werden nicht in Optimierungen
übersprungen. Die Ausgabedatei ist wieder gültige Stack-IR. `make test`
vergleicht die erwartete Faltung und führt das QCCVM vor und nach der
Optimierung aus.

Noch nicht enthalten sind Kontrollfluss-/Datenflussanalysen, Variablen-
Konstantenpropagation, tote Codeabschnitte, mehrere Optimierungsstufen und
die Einbindung in den `qcc`-Treiber. `qost` wird zunächst explizit aufgerufen,
damit der Optimierer isoliert geprüft werden kann.
