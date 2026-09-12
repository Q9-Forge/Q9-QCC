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
