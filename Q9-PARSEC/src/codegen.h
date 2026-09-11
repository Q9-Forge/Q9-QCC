//═════════════════════════════════════════════════════════════════════════════════════════════════
// File:   codegen.h                                                                      Ver. 1.00
// Owner:  AF
// Desc.:  AST construction and code-generation interface (see docs/ARCHITEKTUR.md).
//         parsec.cpp builds an AST through astPush*/astGroup* calls during normal parsing;
//         genParserC()/genParser68k() then generate a backtracking recursive-descent parser
//         (C counterpart for validation, 68k assembly as the primary target).
//
// Edition History
//─────────┬──────┬─────────────────────────────────────────────────────────────────────────┬──────
// Date    │ Ver. │ Description                                                             │ By
//─────────┼──────┼─────────────────────────────────────────────────────────────────────────┼──────
// 26-07-19│ 1.00 │ Initiale Version: AST-Stack-API + C- und 68k-Backend                    │ CF
//─────────┴──────┴─────────────────────────────────────────────────────────────────────────┴──────
#ifndef CODEGEN_H
#define CODEGEN_H

//------------------------------------------------------------------------------------------------
// AST construction, called by parsec.cpp during parsing. Convention: factor()
// leaves exactly one node on the AST stack; term()/expression() group everything
// above astMark() with astGroupSeq()/astGroupAlt(); rule() closes with
// astFinishRule(). The AST is unused when errorCnt>0; these functions tolerate
// unbalanced calls during error recovery.
//------------------------------------------------------------------------------------------------
void astReset();
int  astMark();
void astPushTS(const char* text);
void astPushRNG(char lo, char hi);
void astPushNTS(const char* name);
void astGroupSeq(int mark);		// Stack[mark..] -> ein SEQ-Knoten (1 Knoten: unveraendert)
void astGroupAlt(int mark);		// Stack[mark..] -> ein ALT-Knoten (1 Knoten: unveraendert)
void astWrapOpt();				// oberster Knoten -> [..]
void astWrapRep();				// oberster Knoten -> {..}
void astFinishRule(const char* name);

//------------------------------------------------------------------------------------------------
// LEXER configuration from the workfile's [LEXER] block (see ARCHITEKTUR.md §8).
// buf is the complete raw block, with lines separated by \n; NULL/empty means
// no lexer configuration and preserves character-based generation. Return 1 on
// success and 0 on configuration error. Supported lines:
//   WHITESPACE = " \t\r\n"        characters skipped between symbols
//   TOKEN <rule>                  lexical rule root; its transitive closure is
//                                 generated without whitespace skipping
//   COMMENT LINE = "//"           object-language line comment through line end
//------------------------------------------------------------------------------------------------
int lexParseConfig(const char* buf);

//------------------------------------------------------------------------------------------------
// CODEGEN configuration from the workfile's [CODEGEN] block.
// buf = kompletter Roh-Blockinhalt, NULL/leer = Defaults. Unterstuetzte Zeilen:
//   M68K OS9                zusaetzlich <basis>_os9.a im r68/psect-Format erzeugen
//   M68K PSECT = <name>     psect-Name (Default: <basisname>_p)
//   START = <regel>         Startregel (Default: erste Regel der Grammatik) -- gilt
//                           fuer die erzeugten Parser UND den Tabellen-Testlauf
//------------------------------------------------------------------------------------------------
int cgenParseConfig(const char* buf);
int cgenWantOS9();
const char* cgenStartRule();		// "" wenn nicht konfiguriert

//------------------------------------------------------------------------------------------------
// ACTIONS configuration from the workfile's [USER-CODE] block (see ARCHITEKTUR.md §9).
// buf = kompletter Roh-Blockinhalt, NULL/leer = keine Aktionen. Unterstuetzte Zeilen:
//   ACTION AFTER <regel> CALL <name>    Aufruf direkt nach Erfolg von <regel>
//   ROUTINE C <name> ... END            rohe C-Funktion void <name>(const char*,const char*)
//   ROUTINE M68K <name> ... END         rohe 68k-Subroutine (Label <name>:, a0=Ende, rts)
// Muss VOR genParserC()/genParser68k() aufgerufen werden. Liefert immer 1 (Fehler = Warnung
// + betroffene Aktion faellt weg, kein Abbruch der Codegenerierung).
//------------------------------------------------------------------------------------------------
int actionsParseConfig(const char* buf);

//------------------------------------------------------------------------------------------------
// Call code generation only for an error-free, non-left-recursive grammar.
// Return 1 on success and 0 on error (a diagnostic has already been printed).
// genParser68kOS9 is like genParser68k but uses Microware r68 format
// (nam/psect/ends and '*' comment lines); baseName is the path-free basename
// used for the default psect name.
//------------------------------------------------------------------------------------------------
int genParserC(const char* path);
int genParser68k(const char* path);
int genParser68kOS9(const char* path, const char* baseName);

#endif // CODEGEN_H
