# Q9-Compilerstruktur und Migrationsplan

Stand: 2026-09-11

Dieses Dokument beschreibt die verbindliche Zielstruktur der Q9-Compiler-
Werkzeugkette und die Reihenfolge, in der die bestehenden Repositories und
Buildpfade umgestellt werden. Die laufende Arbeit an `Q9-OS` und
`Q9-Flux-68k` bleibt ausserhalb dieses Vorhabens und wird nicht veraendert.

## Zielnamen

| Projekt/Repository | Binary | Aufgabe | Eingabe -> Ausgabe |
|---|---|---|---|
| `Q9-PARSEC` | `qparsec` | Parsergenerator | `.ebnf` -> `.c` |
| `Q9-QCC` / `q9-qcc` | `qcc` | uebergeordneter Treiber | Quellen -> Zielmodul |
| `Q9-RUN` | `qrun` | Interpreter fuer Q9 Stack-IR | `.ir` -> Ausfuehrung; P-Modul spaeter |
| `Q9-FRONTEND-C/q9-qcpp` | `qcpp` | C-Praeprozessor | `.c` -> `.i` |
| `Q9-FRONTEND-C/q9-qcir` | `qcir` | C-Frontend / IR-Erzeuger | `.i` -> `.ir` |
| `Q9-BACKEND-68K/q9-qir68k` | `qir68k` | 68k-Backend | `.ir` -> `.s68k` |
| `Q9-BACKEND-68K/q9-qo68k` | `qo68k` | 68k-Peephole-Optimierer | `.s68k` -> `.opt.s68k` |
| `Q9-BACKEND-68K/q9-qr68k` | `qr68k` | 68k-Macroassembler | `.s68k` -> `.r` |
| `Q9-BACKEND-68K/q9-ql68k` | `ql68k` | 68k-ROF-Linker | `.r` + `.l` -> Modul |
| `Q9-BACKEND-x86/q9-qirx86` | `qirx86` | x86-Backend | `.ir` -> `.sx86` |
| `Q9-BACKEND-x86/q9-qox86` | `qox86` | x86-Peephole-Optimierer | `.sx86` -> `.opt.sx86` |
| `Q9-BACKEND-x86/q9-qrx86` | `qrx86` | x86-Assembler/Objektstufe | `.sx86` -> `.o` |
| `Q9-BACKEND-x86/q9-qlx86` | `qlx86` | x86-ROF/ELF-Linker | `.o` + Bibliotheken -> Modul |

Architekturbezeichnungen stehen ohne Unterstrich im Binarynamen. Das `q` am
Anfang kennzeichnet Q9-Werkzeuge; ein `q9`-Praefix gehoert nur zum Projekt-
oder Verzeichnisnamen.

## Gemeinsames Zwischenformat

Der verbindliche Name ist **Q9 Stack-IR**. Es handelt sich um textuelle,
architektur- und betriebssystemunabhaengige IR mit der Dateiendung `.ir`.
`QIR` wird nicht als eigener Formatname eingefuehrt.

Der Interpreter `qrun` liest zunaechst direkt diese Text-IR. Eine kompakte
P-Modul-Ausgabe ist eine spaetere Erweiterung und wird nicht in die erste
Umstrukturierung eingemischt.

## Dateiendungen

- `.c`: C-Quelltext
- `.i`: vorverarbeitetes C
- `.ir`: Q9 Stack-IR
- `.s68k`: erzeugter 68k-Assemblertext
- `.sx86`: erzeugter x86-Assemblertext
- `.opt.s68k` / `.opt.sx86`: optional optimierter Assemblertext
- `.a`: handgeschriebener bzw. klassischer OS-9-Assemblerquelltext
- `.r`: 68k-ROF-Objekt
- `.o`: x86-ELF-Objekt
- `.l`: Bibliothek
- keine feste Endung: fertiges OS-9/OS-9000-Modul

Der Optimierer behaelt die Architekturendung. Der mit `-o` angegebene
Benutzername bleibt unveraendert; nur temporäre Zwischenstufen erhalten
automatisch Namen wie `hello.opt.s68k`.

## Aktueller Bestand -> Ziel

| Aktuell | Ziel | Bemerkung |
|---|---|---|
| `Q9-Parsec` | `Q9-PARSEC` | separater Parsergenerator; Rename/Remote erst spaeter |
| `Q9-QCC/Data/qcc_p.c` | `Q9-FRONTEND-C/q9-qcir/data` | generierter C-Frontend-Quelltext; Binary heute `qcc_p` |
| `Q9-QCC/q9-cpp` | `Q9-FRONTEND-C/q9-qcpp` | Präprozessor, innerhalb des Sammelrepositories verschoben |
| `Q9-QCC/qo68/qo68.c` | `Q9-BACKEND-68K/q9-qo68k` | bereits aus dem Backend ausgelagert |
| `Q9-qr68` | `Q9-BACKEND-68K/q9-qr68k` | derzeit eigenes Repository, Binary heute `qr68` |
| `Q9-ql68` | `Q9-BACKEND-68K/q9-ql68k` | derzeit eigenes Repository, Binary heute `ql68` |
| `Q9-x86/backend/qcc_i386_backend.cpp` | `Q9-BACKEND-x86/q9-qirx86` | Rename erst nach Anpassung aller x86-Skripte |
| `Q9-x86/linker/*` | `Q9-BACKEND-x86/q9-qlx86` | heutiger Packer ist noch kein vollständiger `qlx86` |
| `Q9-Run` | `Q9-RUN` | aktueller Text-IR-Interpreter |

## Migrationsreihenfolge

1. Arbeitsstände und aktive Repositories sichern; `Q9-OS` und Flux nicht
   anfassen.
2. Gemeinsame Namens-/Endungsdokumentation aktualisieren.
3. `qcc_p` technisch um eine saubere Ausgabedatei erweitern; danach als
   `qcir` bauen und testen.
4. Den Treiber `q9-qcc` als Binary `qcc` implementieren, zunächst für einen
   68k-Ein-Datei-Pfad.
5. Den 68k-Pfad mit temporären `.i`, `.ir`, `.s68k`, `.opt.s68k` und `.r`
   Dateien umstellen und gegen Host, qr68/ql68 und Emulator prüfen.
6. Die bestehenden 68k-Repositories und ihre Aufrufer in einem kontrollierten
   Rename-Durchlauf auf `qir68k`, `qo68k`, `qr68k` und `ql68k` umstellen.
7. Den x86-Pfad separat auf `qirx86` und die tatsächlichen Objekt-/Modul-
   formate abbilden; `qox86`, `qrx86` und `qlx86` erst benennen, wenn die
   jeweiligen Stufen existieren.
8. Erst danach Remote-Repository-Namen und GitHub-Verweise ändern.

Jede Stufe bekommt vor dem nächsten Rename einen eigenen Build-/Regression-
Test. Keine Umbenennung soll die aktiven Kernel- oder Emulator-Branches
verändern.
