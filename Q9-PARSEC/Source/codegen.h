//═════════════════════════════════════════════════════════════════════════════════════════════════
// File:   codegen.h                                                                      Ver. 1.00
// Owner:  AF
// Desc.:  Schnittstelle AST-Aufbau + Codegenerierung (siehe docs/ARCHITEKTUR.md).
//         Der Parser in parsec.cpp baut ueber die astPush*/astGroup*-Aufrufe waehrend des
//         normalen Parsens einen AST auf; daraus erzeugen genParserC()/genParser68k()
//         einen backtracking-rekursiven Abstiegsparser (C-Zwilling zur Validierung,
//         68k-Assembler als eigentliches Ziel).
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
// AST-Aufbau -- wird vom Parser (parsec.cpp) waehrend des Parsens aufgerufen.
// Konvention: factor() hinterlaesst genau EINEN Knoten auf dem AST-Stack;
// term()/expression() fassen mit astGroupSeq()/astGroupAlt() alles oberhalb ihrer
// gemerkten Marke (astMark()) zusammen; rule() schliesst mit astFinishRule() ab.
// Bei Grammatik-Fehlern (errorCnt>0) wird der AST einfach nie benutzt -- die
// Funktionen sind gegen unbalancierte Aufrufe (Fehler-Recovery) robust.
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
// LEXER-Konfiguration (aus dem [LEXER]-Block der Arbeitsdatei, siehe ARCHITEKTUR.md §8).
// buf = kompletter Roh-Blockinhalt (Zeilen mit \n getrennt), NULL/leer = kein Lexer
// (Codegen bleibt dann zeichenbasiert wie bisher). Liefert 1 ok / 0 Konfigurationsfehler.
// Unterstuetzte Zeilen:
//   WHITESPACE = " \t\r\n"        Zeichen, die zwischen Symbolen ueberlesen werden
//   TOKEN <regel>                 Regel ist lexikalisch (Token-Wurzel); ihr transitiver
//                                 Abschluss wird OHNE Whitespace-Skipping generiert
//   COMMENT LINE = "//"           Zeilenkommentar der OBJEKTsprache (bis Zeilenende)
//------------------------------------------------------------------------------------------------
int lexParseConfig(const char* buf);

//------------------------------------------------------------------------------------------------
// CODEGEN-Konfiguration (aus dem [CODEGEN]-Block der Arbeitsdatei).
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
// ACTIONS-Konfiguration (aus dem [NUTZER-CODE]-Block der Arbeitsdatei, siehe ARCHITEKTUR.md §9).
// buf = kompletter Roh-Blockinhalt, NULL/leer = keine Aktionen. Unterstuetzte Zeilen:
//   ACTION AFTER <regel> CALL <name>    Aufruf direkt nach Erfolg von <regel>
//   ROUTINE C <name> ... END            rohe C-Funktion void <name>(const char*,const char*)
//   ROUTINE M68K <name> ... END         rohe 68k-Subroutine (Label <name>:, a0=Ende, rts)
// Muss VOR genParserC()/genParser68k() aufgerufen werden. Liefert immer 1 (Fehler = Warnung
// + betroffene Aktion faellt weg, kein Abbruch der Codegenerierung).
//------------------------------------------------------------------------------------------------
int actionsParseConfig(const char* buf);

//------------------------------------------------------------------------------------------------
// Codegenerierung -- nur bei fehlerfreier, nicht-linksrekursiver Grammatik aufrufen.
// Liefert 1 bei Erfolg, 0 bei Fehler (Meldung bereits ausgegeben).
// genParser68kOS9: wie genParser68k, aber im Microware-r68-Format (nam/psect/ends,
// '*'-Kommentarzeilen); baseName = Basisname ohne Pfad fuer den Default-psect-Namen.
//------------------------------------------------------------------------------------------------
int genParserC(const char* path);
int genParser68k(const char* path);
int genParser68kOS9(const char* path, const char* baseName);

#endif // CODEGEN_H
