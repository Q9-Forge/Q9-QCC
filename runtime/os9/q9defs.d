        opt     -l

* Q9 OS-9/68000 Definitionsdatei
* -----------------------------------------------------------------------------
* Eigene Q9-Kopie der bisher verwendeten Microware-Definitionen.
*
* Originalquelle : Q9-Forge/Q9-Tools/System/echo/asm/q9defs.d
*                 sowie MWOS/OS9/SRC/DEFS/oskdefs.d, module.a, funcs.a
* Pflegehinweis  : Aendert sich die Originaldatei, muss diese Q9-Kopie
*                  geprueft und gegebenenfalls ebenfalls angepasst werden.
* Stand           : 2026-08-13
*
* Die Q9-Namen sind die bevorzugte Schreibweise. Die kompatiblen Microware-
* Namen bleiben zusaetzlich definiert, damit uebernommene OS-9-Quellen ohne
* sofortige Umbenennung assembliert und schrittweise portiert werden koennen.
* -----------------------------------------------------------------------------

* Modul-Typ und Sprachfeld ---------------------------------------------------

Q9Prgrm        equ     1                       * Programmmodul
Q9Sbrtn        equ     2                       * Subroutinenmodul
Q9Multi        equ     3                       * Multi-Modul
Q9Data         equ     4                       * Datenmodul
Q9TrapLib      equ     11                      * Trap-Handler-Bibliothek
Q9Systm        equ     12                      * Systemmodul
Q9FlMgr        equ     13                      * File-Manager
Q9Drivr        equ     14                      * Device-Driver
Q9Devic        equ     15                      * Device-Descriptor

Q9Objct        equ     1                       * Maschinencode
Q9ICode        equ     2                       * Basic-I-Code
Q9PCode        equ     3                       * Pascal-P-Code
Q9CCode        equ     4                       * C-I-Code

* Kompatibilitaetsnamen zum Microware-Original ------------------------------

Prgrm          equ     Q9Prgrm
Sbrtn          equ     Q9Sbrtn
Multi          equ     Q9Multi
Data           equ     Q9Data
TrapLib        equ     Q9TrapLib
Systm          equ     Q9Systm
FlMgr          equ     Q9FlMgr
Drivr          equ     Q9Drivr
Devic          equ     Q9Devic
Objct          equ     Q9Objct
ICode          equ     Q9ICode
PCode          equ     Q9PCode
CCode          equ     Q9CCode

* Modulattribute -------------------------------------------------------------

Q9ReEnt        equ     $80                     * Reentrant
Q9Ghost        equ     $40                     * Im Speicher behalten
Q9SupStat      equ     $20                     * Supervisor-State erforderlich
Q9ReEntBit     equ     7                       * Bitnummer Reentrant
Q9GhostBit     equ     6                       * Bitnummer Ghost
Q9SupStBit     equ     5                       * Bitnummer Supervisor-State

ReEnt          equ     Q9ReEnt
Ghost          equ     Q9Ghost
SupStat        equ     Q9SupStat
ReEntBit       equ     Q9ReEntBit
GhostBit       equ     Q9GhostBit
SupStBit       equ     Q9SupStBit

* Modulheader-Offsets --------------------------------------------------------
* Werte aus MWOS/OS9/SRC/DEFS/module.a, 68000-Format, ab Offset 0.

Q9_M_ID        equ     0                       * Modulkennung, Wort
Q9_M_SysRev    equ     2                       * Systemrevision, Wort
Q9_M_Size      equ     4                       * Modulgroesse, Langwort
Q9_M_Owner     equ     8                       * Besitzer-ID, Langwort
Q9_M_Name      equ     12                      * Name-Offset, Langwort
Q9_M_Accs      equ     16                      * Zugriffsrechte, Wort
Q9_M_Type      equ     18                      * Modultyp, Byte
Q9_M_Lang      equ     19                      * Modulsprache, Byte
Q9_M_Attr      equ     20                      * Attribute, Byte
Q9_M_Revs      equ     21                      * Revisionsnummer, Byte
Q9_M_Edit      equ     22                      * Edition, Wort
Q9_M_Usage     equ     24                      * Usage-Offset, Langwort
Q9_M_Symbol    equ     28                      * Symboltabellen-Offset, Langwort
Q9_M_Ident     equ     32                      * Identifikation, Wort
Q9_M_HdExt     equ     40                      * Header-Erweiterung, Langwort
Q9_M_HdExtSz   equ     44                      * Groesse der Erweiterung, Wort
Q9_M_Parity    equ     46                      * Header-Paritaet, Wort
Q9_M_IDSize    equ     48                      * Ende des universellen Headers
Q9_M_Exec      equ     48                      * Programmeinstieg, Langwort
Q9_M_Excpt     equ     52                      * Exception-Einstieg, Langwort
Q9_M_Mem       equ     56                      * Datenbereich, Langwort
Q9_M_Stack     equ     60                      * Stackgroesse, Langwort
Q9_M_IData     equ     64                      * Initialdatenzeiger, Langwort
Q9_M_IRefs     equ     68                      * Initialdaten-Referenzen, Langwort

* Kompatibilitaetsnamen fuer uebernommene Quellen ----------------------------

M$ID           equ     Q9_M_ID
M$SysRev       equ     Q9_M_SysRev
M$Size         equ     Q9_M_Size
M$Owner        equ     Q9_M_Owner
M$Name         equ     Q9_M_Name
M$Accs         equ     Q9_M_Accs
M$Type         equ     Q9_M_Type
M$Lang         equ     Q9_M_Lang
M$Attr         equ     Q9_M_Attr
M$Revs         equ     Q9_M_Revs
M$Edit         equ     Q9_M_Edit
M$Usage        equ     Q9_M_Usage
M$Symbol       equ     Q9_M_Symbol
M$Ident        equ     Q9_M_Ident
M$HdExt        equ     Q9_M_HdExt
M$HdExtSz      equ     Q9_M_HdExtSz
M$Parity       equ     Q9_M_Parity
M$IDSize       equ     Q9_M_IDSize
M$Exec         equ     Q9_M_Exec
M$Excpt        equ     Q9_M_Excpt
M$Mem          equ     Q9_M_Mem
M$Stack        equ     Q9_M_Stack
M$IData        equ     Q9_M_IData
M$IRefs        equ     Q9_M_IRefs

* OS-9-Systemaufrufe ---------------------------------------------------------

Q9_F_Link      equ     $00                     * Modul verknuepfen
Q9_F_Exit      equ     $06                     * Prozess beenden
Q9_F_TLink     equ     $21                     * Trap-Paket verknuepfen
Q9_I_WritLn    equ     $8c                     * ASCII-Zeile schreiben
Q9_T_Math      equ     15                      * Mathematik-Trap
Q9_E_IllFnc    equ     $40                     * Ungueltiger Funktionscode

F$Link         equ     Q9_F_Link
F$Exit         equ     Q9_F_Exit
F$TLink        equ     Q9_F_TLink
I$WritLn       equ     Q9_I_WritLn
T$Math         equ     Q9_T_Math
E$IllFnc       equ     Q9_E_IllFnc

* Q9-Trap-Makro --------------------------------------------------------------
* Das Servicewort folgt im OS-9/68000-ABI direkt auf TRAP #0.

Q9_OS9         macro
        trap    #$0
        dc.w    \1
        endm

OS9            macro
        Q9_OS9  \1
        endm
