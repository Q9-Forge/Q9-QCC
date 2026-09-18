Design-Spezifikation: Die Programmiersprache "Quant" (Compiler: Quant9)Dieses Dokument definiert die Architektur, die Syntax und das Compiler-Design der Systemprogrammiersprache Quant. Sie dient als offizielle Hochsprache für das Q9-OS (angetrieben vom QBit-Kernel) und wird vom Compiler Quant9 in die positionsunabhängige QCC Stack-IR übersetzt.1. Das Q9-Ökosystem & NamensfamilieDie System-Architektur ist aus einem Guss gebaut und teilt sich wie folgt auf:Das Ökosystem: Q9-Forge (Die Entwicklungs-Plattform / GitHub-Repository)Die Hardware-Emulation: Q9-Flux (Die virtuelle Testumgebung)Der OS-Kernel: QBit / QBit-Kernel (Der hardwarenahe Kern, der die Bits kontrolliert)Das Betriebssystem: Q9-OS (Das Gesamtsystem)Die Programmiersprache: Quant (Die typisierte TypeScript-Wirth-Hybridsprache)Der Compiler: Quant9 (Das Frontend, das Quant-Code verarbeitet; komplementär zu QCC für C)Die Runtime & VM: Q9-Run (Der Interpreter für den IR-Code)Der Parser-Generator: Parsec (Erzeugt das C-Grundgerüst für den rekursiven Abstieg)2. Die Sprachgrammatik (EBNF)Die Syntax nutzt das moderne Schreibgefühl von TypeScript (geschweifte Klammern {}, Zuweisung über =), verzichtet aber auf dynamischen JavaScript-Ballast. Für maximale System-Identität wird das Branding (quant und qbit) als festes Schlüsselwort zur Modularisierung in der Grammatik verankert.ebnf; =============================================================================
; GRAMMATIK FÜR DIE PROGRAMMIERSPRACHE "QUANT" (COMPILER: QUANT9)
; Zielplattform: Q9-OS (QBit Kernel) | Syntax: TypeScript-Hybrid mit '=' und '{}'
; =============================================================================

; --- Programmebene & Imports ---
Program             ::= { ImportNamespaceDecl } { SystemBlock | ClassDecl | InterfaceDecl | FunctionDecl }

ImportNamespaceDecl ::= "import" "namespace" NamespacePath ";"
NamespacePath       ::= Identifier { "." Identifier }

; --- System-Namensräume (Branding-Integration) ---
SystemBlock         ::= ("quant" | "qbit" | "namespace") NamespacePath "{" { ClassDecl | InterfaceDecl | FunctionDecl } "}"

; --- Interfaces (VTables werden vollautomatisch im C-Backend berechnet) ---
InterfaceDecl       ::= "interface" Identifier "{" { MethodSignature } "}"
MethodSignature     ::= Identifier "(" [ ParameterList ] ")" ":" Type ";"

; --- Klassen (Statisches, flaches Speicherlayout ohne V8-Bloat) ---
ClassDecl           ::= "class" Identifier [ "implements" Identifier ] "{" { ClassMember } "}"
ClassMember         ::= FieldDecl | ConstructorDecl | MethodDecl

FieldDecl           ::= [ "public" | "private" ] Identifier ":" Type ";"
ConstructorDecl     ::= [ "public" | "private" ] "constructor" "(" [ ParameterList ] ")" Block
MethodDecl          ::= [ "public" | "private" ] Identifier "(" [ ParameterList ] ")" ":" Type Block

; --- Funktionen & Variablen ---
FunctionDecl        ::= "function" Identifier "(" [ ParameterList ] ")" ":" Type Block
ParameterList       ::= Parameter { "," Parameter }
Parameter           ::= Identifier ":" Type

; --- Variable mit optionalem Decorator für Memory-Mapping ---
VariableDecl        ::= [ "[" Decorator "]" ] "let" Identifier ":" Type [ "=" Expression ] ";"
Decorator           ::= "&" HexNumber
Assignment          ::= Identifier "=" Expression ";"

Block               ::= "{" { Statement } "}"
Statement           ::= VariableDecl | Assignment | FunctionCall ";" | Block | "return" [ Expression ] ";"

; --- Ausdrücke & Basis-Typen ---
FunctionCall        ::= Identifier "(" [ ArgList ] ")"
ArgList             ::= Expression { "," Expression }
Expression          ::= Term { ("+" | "-") Term }
Term                ::= Factor { ("*" | "/") Factor }
Factor              ::= Identifier | Number | HexNumber | "(" Expression ")" | FunctionCall | AddressOfExpr | DereferenceExpr | CastExpr
AddressOfExpr       ::= "&" Identifier
DereferenceExpr     ::= "*" Identifier
CastExpr            ::= "(" Type ")" Factor

Type                ::= Identifier | PointerType | ArrayType
PointerType         ::= "*" Type
ArrayType           ::= Identifier "[" Number "]"

Identifier          ::= Letter { Letter | Digit }
Number              ::= Digit { Digit }
HexNumber           ::= "0x" HexDigit { HexDigit }
Letter              ::= "a" | ... | "z" | "A" | ... | "Z"
Digit               ::= "0" | ... | "9"
HexDigit            ::= "0" | ... | "9" | "A" | ... | "F"
Verwende Code mit Vorsicht.3. Namespace- & Pfad-Management (Portabilität)Um absolute Portabilität über verschiedene Entwicklungs- und Zielplattformen (Amiga/68k, x86, RISC-V) zu garantieren, sind physische Dateipfade im Quellcode verboten.Kommandozeilen-Flags: Der Compiler Quant9 erhält Include-Pfade beim Aufruf via Parameter (z. B. quant9 main.ts -I ./libs/68k/). Der C-Parser verwaltet diese in einem einfachen, flachen String-Array (char* searchPaths[]).Namensauflösung via Hash-Maps: Das C-Frontend nutzt flache, eindeutige Schlüssel für die Symbol-Hash-Map (Name-Mangling). Ein Block quant Geometrie { class Kreis ... } wird intern unter dem Schlüssel "Geometrie.Kreis" abgelegt.Using-Suchliste: Bei einem Aufruf wie new Kreis() wandert der Parser die Liste der importierten Namespaces ab und prüft in der Hash-Map, ob die Kombination (z. B. "Geometrie.Kreis") existiert. Bei Namenskonflikten bricht der Compiler sofort mit einem Fehler ab.4. Speicherlayout & IR-Abbildung (100% Relocatable)Die Sprache erzeugt 100 % relocatiblen (positionsunabhängigen) Code. Objekte im Speicher besitzen ein vollkommen flaches, statisches Layout. Stringbasierte Eigenschaftssuchen zur Laufzeit sind ausgeschlossen. Dynamische Key-Value-Strukturen werden strikt in ein separates, integriertes Map-Objekt ausgelagert.Objekt-Layout im Heap/Stack:Offset 0: __vtable_ptr (Ein architekturabhängiger Pointer p auf die virtuelle Funktionstabelle. Wird vom Compiler nur generiert, wenn die Klasse ein Interface implementiert).Ab Offset 1 (auf Pointer-Ebene): Die deklarierten Variablen-Felder (z. B. tinte: int32).QCC Stack-IR Muster für Interface-Aufrufe (CALLIND):Da die Pointer-Breite (4 Byte auf 68k, 8 Byte auf modernen RISC-CPUs) erst im Backend oder beim Start des Interpreters (Q9-Run) aufgelöst wird, arbeitet das Frontend in parsec-C-Code ausschließlich mit logischen Indizes und den IR-Typtags (p für Pointer, i für Integer).Beim Aufruf einer Interface-Methode erzeugt der Parser das von der QCC-IR strikt vorgegebene Muster für CALLIND (Funktionszeiger ganz unten auf dem Stack, Argumente inklusive this darüber):text; --- Schritt 1: Funktionszeiger berechnen und zuunterst platzieren ---
LOADP 0              ; Lädt das Interface-Objekt 'd' aus lokalem Slot 0
LOADIND p            ; Holt den VTable-Zeiger (liegt bei Offset 0 des Objekts)
PUSH 0               ; Logischer Index 0 in der VTable (z.B. für Methode 'drucken')
PTRINDEX p           ; Berechnet die exakte, relozierte Position in der Tabelle
LOADIND p            ; Holt den echten Funktionszeiger -> Liegt nun zuunterst auf dem Stack!

; --- Schritt 2: Argumente darüber pushen (links nach rechts) ---
LOADP 0              ; Übergibt die Objektadresse selbst als 'this'-Parameter

; --- Schritt 3: Indirekter Aufruf ---
CALLIND 1            ; Ruft die Methode mit 1 Argument auf
DROP                 ; Da 'void', das unbenutzte Ergebnis vom Stack werfen
Verwende Code mit Vorsicht.5. Speichermanagement
Die Speicherallokation folgt einer einfachen, deterministischen Regel, um die Komplexität des Compilers gering zu halten:
- Lokale Instanzen (innerhalb von Funktionen/Methoden): Werden auf dem Stack allokiert.
- Globale Instanzen (außerhalb von Funktionen): Werden auf dem Heap allokiert.
Dies vereinfacht die Speicherverwaltung erheblich und vermeidet komplexe Garbage-Collection-Logik.

6. Definition der Symboltabelle in reinem CFür die Einbindung in Ihr bestehendes, C-basiertes Parser-Frontend (parsec.cpp / C89-Strukturen) wird das folgende Symbol-Struct im neuen Quant9-Verzeichnis hinterlegt:c/* quant_symtab.h - Symboltabelle für den Quant9-Compiler (Reines C) */
#ifndef QUANT_SYMTAB_H
#define QUANT_SYMTAB_H

typedef enum {
    SYM_NAMESPACE,
    SYM_INTERFACE,
    SYM_CLASS,
    SYM_FIELD,
    SYM_METHOD,
    SYM_VARIABLE
} SymbolType;

typedef struct Symbol {
    char* name;                 /* Der voll qualifizierte Name, z.B. "Geometrie.Kreis" */
    SymbolType type;            /* Art des Compiler-Symbols */
    char* dataType;             /* Datentyp als IR-Tag (z.B. "i", "f", "p") */
    
    /* OOP-Indizes für die QCC-Stack-IR */
    int field_offset;           /* Für SYM_FIELD: Index im Objekt (ab 1, da 0 = VTable) */
    int vtable_index;           /* Für SYM_METHOD: Index in der Interface-VTable (ab 0) */
    
    struct Symbol* next;        /* Chaining-Pointer für Kollisionen in der Hash-Map */
} Symbol;

#endif
