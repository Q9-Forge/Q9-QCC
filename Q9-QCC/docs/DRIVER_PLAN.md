# Q9-QCC – Treiberprojekt

Der Name `Q9-QCC` bezeichnet hier das universelle Treiberprojekt innerhalb des
Sammelrepositories `Q9-QCC`. Das erzeugte Programm heißt `qcc`.

`qcc` ist kein C-Compiler. Das C-spezifische Frontend ist `qcir` im Projekt
`Q9-FRONTEND-C/q9-qcir`. Der Treiber verbindet Frontends, Stack-IR-Interpreter,
Backends, Optimierer, Assembler und Linker.

Die ausführliche Ketten- und Optionsplanung liegt in dieser Datei. Die
Dokumentation wird während der Migration schrittweise an den implementierten
Treiberstand angepasst.
