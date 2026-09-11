# q9-qir68k – 68k-Backend

`qir68k` übersetzt Q9 Stack-IR (`.ir`) in Motorola-68000-Assemblertext
(`.s68k`).

Die Optimierung, Assemblierung und das Linken sind separate Werkzeuge:

```text
.ir -> qir68k -> .s68k -> qo68k -> qr68k -> .r -> ql68k -> Modul
```

Die produktiven Quellen liegen unter `src/`.
