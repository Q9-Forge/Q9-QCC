# q9-qrx86 – x86-Assembler

`qrx86` ist der eigenständige x86-32-Assembler. Er wird hier neu aufgebaut;
`qox86` bleibt außerhalb dieses Arbeitsschritts.

## Aktueller Stand

`src/qrx86.c` enthält den ersten Assemblerkern. Er liest zeilenweise
Assemblertext, verarbeitet `nam`, `psect`, `vsect`, `ends`, `end`, `dc.b/w/l`,
`ds.b/w/l`, `align`, `even` sowie Code- und Datenlabels. Lokale Branch-Ziele
werden nach dem Einlesen aufgelöst und bei rel8 auf den Wertebereich geprüft.
`global`/`globl`/`export` schreibt definierte Code- und Datensymbole in die
private ROF-Globalliste. Nicht definierte Ziele von `call` und Branches werden
als 32-Bit-PC-relative Einträge in die private ROF-Externliste geschrieben.
Symbolische `dc.l`-Werte erzeugen externe oder lokale 32-Bit-Referenzeinträge;
bei lokalen Symbolen wird der section-relative Wert in das Datenfeld gesetzt.
QIRX86 kennzeichnet exportierte Funktionen mit `global tc_<name>` und legt
für jede definierte globale Variable die Labels `tc_g_<name>` und
`__offset_<name>` an.
Absolute symbolische Speicheroperanden wie `lea eax, [tc_func]` erhalten
ebenfalls einen Code- oder Daten-Referenzeintrag. Symbolische Displacements mit
Registerbasis werden nur für die QIRX86-PIC-Form `edx + __offset_name`
abgebildet. Dafür gibt es einen privaten PIC-Referenztyp; lokale Felder
enthalten zunächst den section-relativen Wert plus 7. Der Linker leitet die
EDX-Basis aus der Call/Pop/LEA-Folge ab und berechnet daraus den endgültigen
Displacement. Diese Konvention ist aus der QIRX86-Ausgabe abgeleitet und noch
nicht gegen Microware `l386` oder einen OS-9000-Loader validiert.

Der Encoder deckt einen Teil der vom aktuellen QIRX86-Backend ausgegebenen
386-Befehle ab: `mov`, `movzx`, `lea` mit Registerbasis, `push`, `pop`, `add`,
`sub`, `and`, `or`, `xor`, `cmp`, `test`, `imul`, `mul`, `div`, `idiv`, `neg`, `not`,
`cdq`, `shl`, `shr`, `sar`, `call`, `jmp`, gängige signed/unsigned bedingte
Sprünge, `setcc`, `nop` und `ret`. ModRM/SIB-Adressen mit Registerbasis,
Index und numerischem Displacement werden unterstützt.

Das ist noch kein vollständiger Assembler und kein Nachweis der
Microware-ROF-Kompatibilität. Die Referenzlisten sind nur für das private
QCC-Profil definiert und nicht gegen Microware-Werkzeuge geprüft. Der Linker
verarbeitet nur einen Teil dieser Einträge, keine allgemeinen
Ausdrucksrelokationen. Vollständige Ausdrücke, Makros, mehrere PSECTs und der
restliche x86-Opcode-Umfang fehlen. Nicht unterstützte Eingabe wird mit
Zeilennummer abgewiesen.

Host-Build:

```sh
make -C Q9-BACKEND-x86/q9-qrx86
```
