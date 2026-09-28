# q9-qo68k – 68k-Peephole-Optimierer

`qo68k` optimiert erzeugten 68k-Assemblertext. Die Architekturendung bleibt
erhalten: `.s68k` wird zu `.opt.s68k`.

Der Optimierer arbeitet unabhängig vom C-Frontend auf der Assemblerausgabe des
68k-Backends.

Er wird als eigenes Programm gebaut und aufgerufen:

```sh
make -C Q9-BACKEND-68K/q9-qo68k
qo68k input.s68k output.opt.s68k
```

## Große Eingaben und Zeilenenden

Eingabe, Zeilentabelle und Synthesetext werden dynamisch angelegt; die frühere
feste 2-MiB-Grenze ist damit beseitigt. Der Leser verarbeitet OS-9-Text mit
CR-Zeilenenden ebenso wie LF und CRLF. Das Ausgabeformat übernimmt den
Zeilenendstil der Eingabe. Synthesezeilen liegen in stabilen Speicherblöcken,
damit ein späteres Wachstum keine bereits gespeicherten Zeiger ungültig macht.

Verifiziert mit der im Emulator erzeugten `qcp.s68` (6.247.987 Byte,
323.050 Zeilen): Host- und 68030-Optimierer erzeugten byteidentische Ausgaben
(5.129.673 Byte; 52.831 Optimierungen in zwei Durchläufen). Die optimierte
Hostausgabe wurde anschließend mit `qr68k` erfolgreich zu ROF assembliert.
Auch `qom.s68` wurde auf Host und Ziel byteidentisch optimiert.

Der Optimierer hält weiterhin die gesamte Eingabe und seine Zeilentabelle im
Speicher. Die große Probe lief erfolgreich, ersetzt aber keinen systematischen
Speichergrenz- und Fehlerpfadtest auf jedem Zielsystem.
