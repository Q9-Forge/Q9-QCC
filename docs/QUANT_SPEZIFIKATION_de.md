# Quant9 – Sprachspezifikation (Entwurf)

## 1. Übersicht
Quant9 ist eine systemnahe Programmiersprache, die auf Einfachheit, explizite Speicherkontrolle und performante Codeerzeugung abzielt. Sie verfolgt ein minimalistisches Design, inspiriert von Wirths Sprachen (Oberon), kombiniert mit modernen Konzepten der Komposition.

## 2. Typen & Deklarationen
Quant9 unterscheidet strikt zwischen Werttypen (`class`) und Referenztypen (`*class`).

*   **Basistypen:** `bool`, `int8` bis `int64`, `uint8` bis `uint64`, `float`, `double`.
*   **Klassen:** Das zentrale Datenkonstrukt für Daten und Verhalten.
    *   `type Name = class { ... };`
*   **Pointers:** `*Name` definiert einen Zeiger auf einen Typ.
*   **Aliase:** `type Name = Typ;`

## 3. Klassen-Konzepte
*   **Mixins:** `mixin Name;` kopiert Felder und Methoden einer anderen Klasse flach ein (Komposition statt Vererbung).
*   **Static:** `static` kann auf Felder und Funktionen angewendet werden (globale/typweite Gültigkeit).
*   **Methoden:** Instanz-Methoden haben impliziten Zugriff auf das Objekt (`self` unter der Haube). Statische Methoden werden mit `static function` definiert.

## 4. Speichermanagement
*   **Stack/Statisch:** `let p : Point;` reserviert Speicher direkt (Inline/Stack).
*   **Heap:** `let r : *Point = new Point();` reserviert dynamisch Speicher auf dem Heap.

## 5. Memory-Mapping
Quant9 unterstützt hardwarenahe Programmierung direkt durch Decorators:
*   `[ &0x4000 ] let HardwareReg : *int32;`

## 6. Syntax-Referenz (EBNF-Ausschnitt)
```ebnf
ClassDecl = "type" Identifier "=" ( "class" ) "{" { ClassMember } "}" ";" .
ClassMember = FieldDecl | MethodDecl | MixinDecl .
FieldDecl = [ "public" | "private" ] [ "static" ] "let" Identifier ":" Type [ "=" Expression ] ";" .
MethodDecl = [ "public" | "private" ] [ "static" ] "function" Identifier "(" [ ParameterList ] ")" ":" Type Block .
MixinDecl = "mixin" Identifier ";" .
```
