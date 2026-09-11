//═════════════════════════════════════════════════════════════════════════════════════════════════
// File:   parsec.cpp                                                                      Ver. 2.30
// Owner:  AF
// Desc.:  EBNF translator (Wirth dialect): parses a .ebnf grammar, generates a
//         call/return jump table (stack machine), executes it against test input
//         aus und verwaltet eine strukturierte Arbeitsdatei (<basis>.lextab) mit den Bloecken
//         EBNF-QUELLTEXT, TS-/NTS-SYMBOLTABELLE, PARSER-TABELLE sowie den nutzer-editierbaren
//         TESTS and USER-CODE blocks.
//         Project goal: recursive-descent code generation for the grammar,
//         initially as 68k assembly.
//
// Call:   parsec <basis> [<teststring>]
//         Case A: <base>.ebnf exists -> retranslate the grammar (the .ebnf is authoritative),
//                 rewrite <base>.lextab while preserving TESTS, and write <base>.lexlst.
//         Case B: no .ebnf -> load the grammar directly from <base>.lextab.
//         Then execute TESTS; optionally test <teststring> against the start rule.
//
// Edition History
//─────────┬──────┬─────────────────────────────────────────────────────────────────────────┬──────
// Date    │ Ver. │ Description                                                             │ By
//─────────┼──────┼─────────────────────────────────────────────────────────────────────────┼──────
// 20-04-28│ 1.00 │ Urspruengliche Version (Visual Studio, Windows): EBNF-Parser +          │ AF
//         │      │ Tabellen-Erzeugung als CSV (.lextab) und Listing (.lexlst)              │
// 26-07-19│ 1.10 │ Adressaufloesung fuer NTS-Referenzen (resolveCallAddresses) +           │ CF
//         │      │ Linksrekursions-Erkennung ueber firstPos-Graph (checkLeftRecursion)     │
// 26-07-19│ 1.20 │ Bereichsoperator "~" ("a"~"z", mode RNG) + optionale <name>-NTS-Syntax; │ CF
//         │      │ ISO-14977-Abweichungen dokumentiert (Dialekt-Kommentar unten)           │
// 26-07-19│ 1.30 │ Stack-Maschine execFrom(): Tabelle laeuft wirklich gegen Eingabetext;   │ CF
//         │      │ dabei 4 alte Tabellen-Bugs behoben (Sequenz-Kurzschluss, mittlere       │
//         │      │ Alternativen, {}/[]/() -Nachbearbeitung, komplexer erster Faktor)       │
// 26-07-19│ 2.00 │ Arbeitsdatei-Format: .lextab ist jetzt strukturiert (EBNF-QUELLTEXT,    │ CF
//         │      │ TS-/NTS-SYMBOLTABELLE, PARSER-TABELLE, TESTS, NUTZER-CODE). Die          │
//         │      │ editierbaren Bloecke bleiben beim Neu-Erzeugen erhalten.                 │
//         │      │ Neu-Erzeugen erhalten und laufen automatisch (runTests); ohne .ebnf     │
//         │      │ laedt loadWorkfileAsGrammar() die Tabelle direkt (Fall B). Portierung   │
//         │      │ auf macOS/clang (msvc_compat.h), OS-Kennung erweitert                   │
// 26-07-19│ 2.10 │ Tabellen-Bugs 5+6 behoben: expression() patcht Alternativen jetzt ueber │ CF
//         │      │ den ganzen Zeilenbereich (mehrfaktorige Alternativen "a" "b" | "c" und  │
//         │      │ komplexe erste Faktoren [x] y | z waren falsch verdrahtet); term()      │
//         │      │ unterscheidet Fehlschlag-ohne-Konsum (F) von committed (E) nach         │
//         │      │ ueberspringbaren Gruppen; runtime-mehrdeutige Stellen -> WARNUNG        │
// 26-07-19│ 2.20 │ [LEXER]-Block in der Arbeitsdatei (roh erhalten wie NUTZER-CODE, an     │ CF
//         │      │ lexParseConfig() der Codegenerierung uebergeben); Kommentarfilter-Fix:  │
//         │      │ '#' zaehlt nur noch am ZEILENANFANG als Kommentar (vorher verschwand    │
//         │      │ z.B. die ganze Regelzeile expression = ... ("#") ... mitsamt Regel)     │
// 26-07-19│ 2.30 │ [CODEGEN]-Block (roh erhalten): M68K OS9 erzeugt zusaetzlich            │ CF
//         │      │ <basis>_os9.a im Microware-r68-Format (nam/psect/ends) -- mit echtem    │
//         │      │ r68 via Wine/MWOS in der Suite verifiziert                              │
// 26-07-19│ 2.11 │ Arbeitsdatei bewahrt jetzt auch [NUTZER-CODE] roh als Vorbereitung fuer │ CF
//         │      │ semantische Aktionen; fehlerhafte Steuerzeichenpruefung korrigiert      │
//─────────┴──────┴─────────────────────────────────────────────────────────────────────────┴──────

//------------------------------------------------------------------------------------------------
// Supported EBNF dialect vs. ISO/IEC 14977:1996
//------------------------------------------------------------------------------------------------
// This tool deliberately follows the "Wirth dialect" used in Pascal, Modula-2
// and Oberon language reports, not ISO/IEC 14977 itself. Main differences:
//
//  - Rule terminator:   "."                instead of ISO's ";"
//  - Comments:          "#", "//", "/* */" instead of ISO's only "(* ... *)"
//  - NTS brackets:       optional "<name>", originating in classic BNF rather
//                       than ISO 14977, which uses bare meta-identifiers
//  - Range operator:     "~" for character ranges such as "a"~"z". This is a
//                       deliberate extension; "-" is already ISO's set-
//                       difference operator, for example letter - "e".
//
// All other core operators (=, ,, |, [ ], { }, ( ), quoted terminals) are
// compatible with ISO semantics. Grammars intended to be strictly ISO-14977
// compliant should avoid the extensions (<name>, ~).
//------------------------------------------------------------------------------------------------
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "msvc_compat.h"
#include "codegen.h"

#ifdef _WIN64
#define OS "Windows 64Bit"
#elif defined _WIN32
#define OS "Windows 32Bit"
#elif defined __APPLE__
#define OS "macOS"
#elif defined __linux__
#define OS "Linux"
#else
#define OS "unknown"
#endif


#define EOS					'\0'
#define EOL					'\n'

#define IDENT_LEN			32
#define FILENAME_LEN		64
#define IDENTLIST_MAX		1024
#define LEXTAB_LEN			1024

#define TOKEN_START			0x80
#define TOKEN_EQUAL			0x81
#define TOKEN_SEQ			0x82
#define TOKEN_OR			0x83	
#define TOKEN_BLOCKON		0x84
#define TOKEN_BLOCKOFF		0x85
#define TOKEN_OPTIONON		0x86
#define TOKEN_OPTIONOFF		0x87
#define TOKEN_REPEATON		0x88
#define TOKEN_REPEATOFF		0x89
#define TOKEN_IDENT			0x8A
#define TOKEN_LITERAL		0x8B
#define TOKEN_QUOT			0x8C
#define TOKEN_END			0x8F
#define TOKEN_EXIT			0x90
#define TOKEN_ERROR			0x91
#define TOKEN_RANGE			0x92

//------------------------------------------------------------------------------------------------
// globals
//------------------------------------------------------------------------------------------------	
char aktChar = EOL;
char lastChar = EOS;
char nextChar = EOS;
int aktToken = TOKEN_START;
int lastToken = TOKEN_START;
int restartToken[16];
char aktName[IDENT_LEN + 1];
char lastName[IDENT_LEN + 1];
char aktRule[IDENT_LEN + 1];
char aktString[IDENT_LEN+1];
char lastString[IDENT_LEN+1];

FILE* fpIn, * fpOut, * fpLst;
int aktTabIndex = 0;
unsigned int errorCnt = 0;
int tableReady = 0;		// 1 once a complete table exists (successful parse or loaded workfile);
						// only then may writeWorkfile() run

//void test(int checkTS, char* msg);
void put();
char getAktChar();
char* getAktLine();
void ebnfSyntax();
void semantischeAnylyse();
void lexikalischeAnalyse();
void exitProgram(int exitCode);
int execFrom(int startRow, int* pos);
void loadPreservedTests(const char* path);
void writeWorkfile(FILE* fp);
int loadWorkfileAsGrammar(const char* path);
void rebuildFirstEdgesFromTable();
int runTests();
void resolveCallAddresses();
void checkLeftRecursion();
void printLexTab();
extern int leftRecursionFound;
extern const char* inputBuf;
extern int inputLen;
extern char quelltextBuf[];
extern char lexerCfgBuf[];
extern char cgenCfgBuf[];
extern char userCodeBuf[];
extern int tableReady;

//------------------------------------------------------------------------------------------------
// Main program
//------------------------------------------------------------------------------------------------	
int main(int argc, char* argv[]) {

	char inputFileName[FILENAME_LEN + 6];
	char outputFileName[FILENAME_LEN + 8];
	char listFileName[FILENAME_LEN + 8];
	char cgenCName[FILENAME_LEN + 8];
	char cgen68kName[FILENAME_LEN + 8];
	char cgenOS9Name[FILENAME_LEN + 8];

	int fileRet;
	int fallB = 0;

	if (argc < 2) {
		printf("usage: %s <inputfile> [<teststring>]\n", argv[0]);
		printf("Translate EBNF grammar to syntax table\n");
		exitProgram(-1);
	}

	size_t len = strlen(argv[1]);
	if (len > FILENAME_LEN) len = FILENAME_LEN;
	strncpy_s(inputFileName, sizeof(inputFileName), argv[1], len);
	strcat_s(inputFileName, sizeof(inputFileName), ".ebnf");
	strncpy_s(outputFileName, sizeof(outputFileName), argv[1], len);
	strcat_s(outputFileName, sizeof(outputFileName), ".lextab");
	strncpy_s(listFileName, sizeof(listFileName), argv[1], len);
	strcat_s(listFileName, sizeof(listFileName), ".lexlst");
	strncpy_s(cgenCName, sizeof(cgenCName), argv[1], len);
	strcat_s(cgenCName, sizeof(cgenCName), "_p.c");
	strncpy_s(cgen68kName, sizeof(cgen68kName), argv[1], len);
	strcat_s(cgen68kName, sizeof(cgen68kName), ".s68");

	printf("Parsec EBNF Translator on %s\n", OS);
	astReset();

	// IMPORTANT (ordering): preserve the TESTS block from the old workfile
	// BEFORE opening it with "w", which would clear it.
	loadPreservedTests(outputFileName);

	fileRet = fopen_s(&fpIn, inputFileName, "r");
	if (fileRet != 0) {
		// Case B: no .ebnf source; the workfile itself is the grammar source.
		fallB = 1;
		printf("Hinweis: '%s' nicht gefunden -- lade Grammatik aus Arbeitsdatei '%s'\n",
			inputFileName, outputFileName);
		if (!loadWorkfileAsGrammar(outputFileName)) {
			printf("usage: %s <inputfile> [<teststring>]\n", argv[0]);
			printf("    error: weder '%s' noch eine Arbeitsdatei '%s' lesbar\n",
				inputFileName, outputFileName);
			exitProgram(-2);
		}
	}

	fileRet = fopen_s(&fpLst, listFileName, "w");
	if (fileRet != 0) {
		printf("usage: %s <inputfile> [<teststring>]\n", argv[0]);
		printf("    error: can't open list file: %s", listFileName);
		exitProgram(-2);
	}

	if (fallB) {
		// Case A performs all of this inside ebnfSyntax()/semantischeAnylyse().
		resolveCallAddresses();
		rebuildFirstEdgesFromTable();
		checkLeftRecursion();
		printf("%s", quelltextBuf);
		fprintf(fpLst, "%s", quelltextBuf);
		printLexTab();
		tableReady = 1;
	}
	else {
		ebnfSyntax();
	}

	// Rewrite the workfile only now (after loadPreservedTests), and only when a
	// complete table was produced. On parse errors, preserve the old workfile
	// including its TESTS block instead of clearing it as before.
	if (tableReady) {
		fileRet = fopen_s(&fpOut, outputFileName, "w");
		if (fileRet != 0) {
			printf("usage: %s <inputfile> [<teststring>]\n", argv[0]);
			printf("    error: can't open output file: %s", outputFileName);
			exitProgram(-2);
		}
		writeWorkfile(fpOut);
	}
	else {
		printf("Hinweis: Grammatik fehlerhaft -- Arbeitsdatei '%s' bleibt unveraendert.\n",
			outputFileName);
	}

	// Code generation (see docs/ARCHITEKTUR.md): only in case A, where the real
	// .ebnf was parsed and an AST exists, and only for an error-free grammar
	// without left recursion. Generate the host-testable C counterpart
	// (<base>_p.c) and the 68k parser (<base>.s68).
	if (!fallB && tableReady && errorCnt == 0 && !leftRecursionFound) {
		actionsParseConfig(userCodeBuf);
		if (!lexParseConfig(lexerCfgBuf) || !cgenParseConfig(cgenCfgBuf)) {
			printf("CODEGEN: Konfiguration fehlerhaft -- Codegenerierung uebersprungen.\n");
		}
		else if (genParserC(cgenCName) && genParser68k(cgen68kName)) {
			printf("CODEGEN: '%s' und '%s' erzeugt.\n", cgenCName, cgen68kName);
			if (cgenWantOS9()) {
				// Use the basename without its directory as the default psect name.
				const char* base = argv[1];
				const char* slash = strrchr(base, '/');
				if (slash == NULL) slash = strrchr(base, '\\');
				if (slash != NULL) base = slash + 1;
				strncpy_s(cgenOS9Name, sizeof(cgenOS9Name), argv[1], len);
				strcat_s(cgenOS9Name, sizeof(cgenOS9Name), "_os9.a");
				if (genParser68kOS9(cgenOS9Name, base)) {
					printf("CODEGEN: '%s' (OS-9/r68-Format) erzeugt.\n", cgenOS9Name);
				}
			}
		}
	}

	runTests();

	// Optional test run: ebnf <basename> <teststring>. Execute the generated
	// table as a stack machine against <teststring>, starting at row 0 (the
	// first rule defined in the .ebnf and therefore the start rule).
	if (argc >= 3) {
		if (leftRecursionFound) {
			printf("\nHinweis: Grammatik ist linksrekursiv -- Testlauf uebersprungen (wuerde nie enden).\n");
		}
		else if (errorCnt > 0) {
			printf("\nHinweis: Grammatik enthaelt Fehler -- Testlauf uebersprungen.\n");
		}
		else {
			inputBuf = argv[2];
			inputLen = (int)strlen(inputBuf);
			int pos = 0;
			int ok = execFrom(0, &pos);

			printf("\n=== Testlauf gegen Eingabe \"%s\" ===\n", inputBuf);
			if (ok && pos == inputLen) {
				printf("ERGEBNIS: vollstaendig erkannt (%d Zeichen)\n", pos);
			}
			else if (ok) {
				printf("ERGEBNIS: teilweise erkannt, %d von %d Zeichen (Rest: \"%s\")\n", pos, inputLen, inputBuf + pos);
			}
			else {
				printf("ERGEBNIS: NICHT erkannt\n");
			}
		}
	}

	exitProgram(0);
}

//------------------------------------------------------------------------------------------------
// end program
void exitProgram(int exitCode){
	fflush(stdout);
	if (fpIn != NULL) {
		fclose(fpIn);
	}
	if (fpOut != NULL) {
		fclose(fpOut);
	}
	if (fpLst != NULL) {
		fclose(fpLst);
	}
	exit(exitCode);
}



//------------------------------------------------------------------------------------------------
// Source-text collection buffer for the workfile ("EBNF-QUELLTEXT" block)
//------------------------------------------------------------------------------------------------
// Collects the same formatted text already printed per rule to the console and
// .lexlst file (see rule()), so the workfile shows the source as in the listing
// without reconstructing it from the table a second time.
#define QUELLTEXT_BUF_SIZE 65536
char quelltextBuf[QUELLTEXT_BUF_SIZE];
int quelltextLen = 0;

void appendQuelltext(const char* text) {
	size_t len = strlen(text);
	if (quelltextLen + (int)len < QUELLTEXT_BUF_SIZE - 1) {
		memcpy(quelltextBuf + quelltextLen, text, len);
		quelltextLen += (int)len;
		quelltextBuf[quelltextLen] = EOS;
	}
}

//------------------------------------------------------------------------------------------------
// List Buffer
//------------------------------------------------------------------------------------------------
char lst[8192];

void clearLst() {
	lst[0] = EOS;
}

void addLst(const char* str, int num) {
	int i;

	strcat_s(lst, sizeof(lst), " ");
	strcat_s(lst, sizeof(lst), str);
	if (num > 0) {
		num = num - (int)strlen(str);
		for(i=0; i<num; i++){
			strcat_s(lst, sizeof(lst), " ");
		}
	}
	//printf("#%s# ",str);
}


//------------------------------------------------------------------------------------------
// Semantic table
//------------------------------------------------------------------------------------------------	
#define STAT_TRUE			-1
#define STAT_FALSE			-2
#define STAT_ERROR			-3
#define STAT_NEXT			-4  
#define STAT_NONE			-5

char statString[] =  " TFEN-";		// Short sign for TURE, FALSE, ERROR, NEXT
const char* head0 =  "+-----+---------------------------------+-----+---------------------------------+------+----+----+\n";
const char* head1 =  "| Nr  | Rule                            | Mode| NTS/TS                          | Addr | T  | F  |\n";
const char* value0 = "| %-4d| %-32s| %-4s| %-32s| %-5s| %-3s| %-3s|\n";

typedef struct {
	char ident[IDENT_LEN + 1];
	char* mode;
	char TS[IDENT_LEN + 1];
	int trueAction;
	int falseAction;
	int callAddr;		// aufgeloeste Zieladresse fuer NTS-Referenzen (-1 = unaufgeloest/undefiniert)
	char rangeLo;		// nur bei mode=="RNG": untere Grenze des Zeichenbereichs
	char rangeHi;		// nur bei mode=="RNG": obere Grenze des Zeichenbereichs
	char ambigF;		// nur Parse-Zeit: STAT_FALSE dieser Zeile ist "nach ueberspringbarer
						// group" -- a jump to a real row is runtime-ambiguous because the
						// input position may not be reset; issue a warning.
} TabEntry;

TabEntry lexTab[1024];

void printLexTab() {
	char trueStr[8];
	char falseStr[8];
	char addrStr[8];
	char identDisp[IDENT_LEN + 3];
	char tsDisp[IDENT_LEN + 3];
	int i, stat;

	printf(head0);
	printf(head1);
	fprintf(fpLst, head0);
	fprintf(fpLst, head1);
	for (i = 0; i < aktTabIndex; i++) {
		if (strlen(lexTab[i].ident)) {
			printf(head0);
			fprintf(fpLst, head0);
		}
		stat = lexTab[i].trueAction;
		if (stat < 0) {
			trueStr[0] = statString[-stat];
			trueStr[1] = EOS;
		}
		else {
			_itoa_s(stat, trueStr, sizeof(trueStr), 10);
		}
		stat = lexTab[i].falseAction;
		if (stat < 0) {
			falseStr[0] = statString[-stat];
			falseStr[1] = EOS;
		}
		else {
			_itoa_s(stat, falseStr, sizeof(falseStr), 10);
		}
		if (strcmp(lexTab[i].mode, "NTS") == 0) {
			if (lexTab[i].callAddr >= 0) {
				_itoa_s(lexTab[i].callAddr, addrStr, sizeof(addrStr), 10);
			}
			else {
				strcpy_s(addrStr, sizeof(addrStr), "???");
			}
		}
		else {
			strcpy_s(addrStr, sizeof(addrStr), "-");
		}
		// Display variants with <NTS> brackets are only for screen/listing output;
		// .lextab (fpOut) remains raw for later machine processing.
		if (strlen(lexTab[i].ident)) {
			sprintf_s(identDisp, sizeof(identDisp), "<%s>", lexTab[i].ident);
		}
		else {
			identDisp[0] = EOS;
		}
		if (strcmp(lexTab[i].mode, "NTS") == 0) {
			sprintf_s(tsDisp, sizeof(tsDisp), "<%s>", lexTab[i].TS);
		}
		else {
			strncpy_s(tsDisp, sizeof(tsDisp), lexTab[i].TS, IDENT_LEN);
		}
		printf(value0, i, identDisp, lexTab[i].mode, tsDisp, addrStr, trueStr, falseStr);
		fprintf(fpLst, value0, i, identDisp, lexTab[i].mode, tsDisp, addrStr, trueStr, falseStr);
		// fpOut (.lextab) is no longer written here: the old raw CSV format was
		// replaced by the structured workfile; see writeWorkfile().
	}
	printf(head0);
	fprintf(fpLst, head0);
}

//------------------------------------------------------------------------------------------------
// Rule address resolution (rule name -> start row)
//------------------------------------------------------------------------------------------------
#define MAX_RULES 256
typedef struct {
	char name[IDENT_LEN + 1];
	int addr;
} RuleSym;

RuleSym ruleSymbols[MAX_RULES];
int ruleSymbolCnt = 0;

void resolveCallAddresses() {
	int i, j, found;

	ruleSymbolCnt = 0;
	for (i = 0; i < aktTabIndex; i++) {
		if (strlen(lexTab[i].ident) && ruleSymbolCnt < MAX_RULES) {
			strncpy_s(ruleSymbols[ruleSymbolCnt].name, sizeof(ruleSymbols[ruleSymbolCnt].name), lexTab[i].ident, IDENT_LEN);
			ruleSymbols[ruleSymbolCnt].addr = i;
			ruleSymbolCnt++;
		}
	}

	for (i = 0; i < aktTabIndex; i++) {
		if (strcmp(lexTab[i].mode, "NTS") == 0) {
			found = -1;
			for (j = 0; j < ruleSymbolCnt; j++) {
				if (strcmp(ruleSymbols[j].name, lexTab[i].TS) == 0) {
					found = ruleSymbols[j].addr;
					break;
				}
			}
			lexTab[i].callAddr = found;
			if (found < 0) {
				printf("FEHLER: Zeile %d referenziert undefinierte Regel '%s'\n", i, lexTab[i].TS);
				errorCnt++;
			}
		}
		else {
			lexTab[i].callAddr = -1;
		}
	}
}

//------------------------------------------------------------------------------------------------
// Left-recursion detection
//------------------------------------------------------------------------------------------------
// A call from rule A to rule B is at the "first position" when it is reachable
// without consuming a terminal first (directly after '=' or '|', or after a
// skippable [option]/{repeat} group). Cycles in this graph are left recursion;
// recursive descent and a call/return stack machine would never terminate.
char currentDefRule[IDENT_LEN + 1] = "";
int firstPos = 1;

// Indicates whether the last factor set its trueAction/falseAction itself
// (block()/repeat()/option()), unlike ident()/literal(), which rely on term()'s
// generic backpatching of the first row. term() may apply that patch only when
// the first factor was simple; otherwise it destroys the links fixed by the
// grouping constructs.
int lastFactorWasComplex = 0;

// Indicates whether the last factor is skippable ([option]/{repeat} may fail
// without failing the sequence or consuming input). Required factors
// (ident/literal/block) commit after success; a later failure may not select
// another alternative because input was consumed and the machine resets
// positions only at NTS-call boundaries.
int lastFactorSkippable = 0;

// Emit at most one warning per rule when runtime-ambiguous F wiring is created
// (a factor after a skippable group whose failure jumps to a real row).
int ambigFalseWarned = 0;

#define MAX_RULE_NAMES 256
char ruleNameList[MAX_RULE_NAMES][IDENT_LEN + 1];
int ruleNameListCnt = 0;

void addRuleNameIfNew(const char* name) {
	int i;
	for (i = 0; i < ruleNameListCnt; i++) {
		if (strcmp(ruleNameList[i], name) == 0) return;
	}
	if (ruleNameListCnt < MAX_RULE_NAMES) {
		strncpy_s(ruleNameList[ruleNameListCnt], sizeof(ruleNameList[ruleNameListCnt]), name, IDENT_LEN);
		ruleNameListCnt++;
	}
}

#define MAX_EDGES 1024
typedef struct {
	char from[IDENT_LEN + 1];
	char to[IDENT_LEN + 1];
} Edge;

Edge firstEdges[MAX_EDGES];
int firstEdgeCnt = 0;

void addFirstEdge(const char* from, const char* to) {
	if (firstEdgeCnt < MAX_EDGES) {
		strncpy_s(firstEdges[firstEdgeCnt].from, sizeof(firstEdges[firstEdgeCnt].from), from, IDENT_LEN);
		strncpy_s(firstEdges[firstEdgeCnt].to, sizeof(firstEdges[firstEdgeCnt].to), to, IDENT_LEN);
		firstEdgeCnt++;
	}
}

int findRuleNameIndex(const char* name) {
	int i;
	for (i = 0; i < ruleNameListCnt; i++) {
		if (strcmp(ruleNameList[i], name) == 0) return i;
	}
	return -1;
}

int dfsColor[MAX_RULE_NAMES];		// 0=weiss, 1=grau (auf dem Stack), 2=schwarz (fertig)
char dfsPath[MAX_RULE_NAMES][IDENT_LEN + 1];
int dfsPathLen = 0;

int leftRecursionFound = 0;

void printCyclePath(int targetIdx) {
	int i, startPos = 0;
	for (i = 0; i < dfsPathLen; i++) {
		if (strcmp(dfsPath[i], ruleNameList[targetIdx]) == 0) {
			startPos = i;
			break;
		}
	}
	printf("FEHLER: LINKSREKURSION erkannt: ");
	for (i = startPos; i < dfsPathLen; i++) {
		printf("%s -> ", dfsPath[i]);
	}
	printf("%s\n", ruleNameList[targetIdx]);
	leftRecursionFound = 1;
}

void dfsVisit(int idx) {
	int i, targetIdx;

	dfsColor[idx] = 1;
	strncpy_s(dfsPath[dfsPathLen], sizeof(dfsPath[dfsPathLen]), ruleNameList[idx], IDENT_LEN);
	dfsPathLen++;

	for (i = 0; i < firstEdgeCnt; i++) {
		if (strcmp(firstEdges[i].from, ruleNameList[idx]) == 0) {
			targetIdx = findRuleNameIndex(firstEdges[i].to);
			if (targetIdx < 0) {
				continue;	// undefinierte Regel, wird schon in resolveCallAddresses gemeldet
			}
			if (dfsColor[targetIdx] == 1) {
				printCyclePath(targetIdx);
				errorCnt++;
			}
			else if (dfsColor[targetIdx] == 0) {
				dfsVisit(targetIdx);
			}
		}
	}

	dfsPathLen--;
	dfsColor[idx] = 2;
}

void checkLeftRecursion() {
	int i;
	for (i = 0; i < ruleNameListCnt; i++) {
		dfsColor[i] = 0;
	}
	for (i = 0; i < ruleNameListCnt; i++) {
		if (dfsColor[i] == 0) {
			dfsPathLen = 0;
			dfsVisit(i);
		}
	}
}

//------------------------------------------------------------------------------------------------
// Stack-Maschine: fuehrt die fertige Tabelle gegen echten Eingabetext aus
//------------------------------------------------------------------------------------------------
// Kernidee (Call/Return statt reinem Goto):
//  - TS/RNG-Zeilen vergleichen direkt gegen das naechste Eingabezeichen (bzw. den naechsten
//    Eingabeabschnitt bei TS) und ruecken bei Erfolg die Position weiter.
//  - NTS-Zeilen sind ein "Unterprogrammaufruf": wir merken uns die Eingabeposition VOR dem
//    Aufruf, rufen die Zielregel (Addr-Spalte) rekursiv auf. Der native C++-Aufrufstack
//    uebernimmt hier exakt die Rolle des Call/Return-Stacks, an dem wir vorher gescheitert
//    waren -- ein Stack-Eintrag ist implizit (Rueckkehr-Zeile ueber trueAction/falseAction
//    der aufrufenden Zeile, Ruecksetzposition ueber die lokale Variable curPos).
//  - falseAction unterscheidet zwei Arten von Fehlschlag: STAT_FALSE (bzw. ein Sprung auf
//    eine andere Zeile = naechste Alternative) heisst "gescheitert, OHNE Eingabe konsumiert
//    zu haben" -- nur solche Fehlschlaege duerfen eine andere Alternative anspringen, denn
//    die Maschine setzt Positionen nur an NTS-Aufruf-Grenzen zurueck. STAT_ERROR heisst
//    "committed": ein Pflicht-Faktor hatte bereits konsumiert, der Fehlschlag ist endgueltig.
//    term() verdrahtet das beim Erzeugen (Faktoren nach ueberspringbaren Gruppen bleiben
//    STAT_FALSE, Faktoren nach sicher konsumierenden Faktoren werden STAT_ERROR).
//    Bekannte Grenze: hat eine ueberspringbare Gruppe zur Laufzeit DOCH konsumiert und
//    ein nachfolgender Faktor scheitert, springt die Maschine ohne Positions-Ruecksetzung
//    weiter -- solche Stellen werden beim Erzeugen der Tabelle klar angewarnt (ambigF).
//  - Linksrekursive Grammatiken wuerden hier (wie bei jedem rekursiven Abstieg) in einer
//    Endlosrekursion enden -- deshalb wird vor dem Start immer checkLeftRecursion() geprueft
//    und ein Testlauf bei gefundener Linksrekursion von main() gar nicht erst gestartet.
const char* inputBuf = NULL;
int inputLen = 0;

void extractLiteralText(const char* quoted, char* out, int outMax) {
	int len = (int)strlen(quoted);
	int n = len - 2;		// ohne die beiden umschliessenden Anfuehrungszeichen
	if (n < 0) n = 0;
	if (n > outMax - 1) n = outMax - 1;
	strncpy_s(out, outMax, quoted + 1, n);
	out[n] = EOS;
}

int execFrom(int startRow, int* pos) {
	int row = startRow;
	int curPos = *pos;

	while (1) {
		int matched = 0;

		if (strcmp(lexTab[row].mode, "TS") == 0) {
			char lit[IDENT_LEN + 1];
			int litLen;
			extractLiteralText(lexTab[row].TS, lit, IDENT_LEN + 1);
			litLen = (int)strlen(lit);
			if (curPos + litLen <= inputLen && strncmp(inputBuf + curPos, lit, litLen) == 0) {
				matched = 1;
				curPos += litLen;
			}
		}
		else if (strcmp(lexTab[row].mode, "RNG") == 0) {
			if (curPos < inputLen
				&& (unsigned char)inputBuf[curPos] >= (unsigned char)lexTab[row].rangeLo
				&& (unsigned char)inputBuf[curPos] <= (unsigned char)lexTab[row].rangeHi) {
				matched = 1;
				curPos += 1;
			}
		}
		else if (strcmp(lexTab[row].mode, "NTS") == 0) {
			int subPos;
			if (lexTab[row].callAddr < 0) {
				printf("LAUFZEITFEHLER: Aufruf einer undefinierten Regel in Zeile %d\n", row);
				return 0;
			}
			subPos = curPos;		// Rueckstzpunkt: was war die Position VOR dem Aufruf
			if (execFrom(lexTab[row].callAddr, &subPos)) {
				matched = 1;
				curPos = subPos;	// Erfolg: Position des Unteraufrufs uebernehmen
			}
			else {
				matched = 0;		// Misserfolg: curPos bleibt unveraendert (nichts verbraucht)
			}
		}
		else {
			printf("LAUFZEITFEHLER: unbekannter Modus '%s' in Zeile %d\n", lexTab[row].mode, row);
			return 0;
		}

		{
			int next = matched ? lexTab[row].trueAction : lexTab[row].falseAction;

			if (next == STAT_TRUE) {
				*pos = curPos;
				return 1;
			}
			if (next == STAT_FALSE || next == STAT_ERROR) {
				return 0;
			}
			if (next < 0) {
				printf("LAUFZEITFEHLER: unerwarteter Status %d in Zeile %d\n", next, row);
				return 0;
			}
			row = next;
		}
	}
}

//------------------------------------------------------------------------------------------------
// Arbeitsdatei (<basis>.lextab)
//------------------------------------------------------------------------------------------------
// Die .lextab ist keine rohe CSV-Tabelle mehr, sondern eine strukturierte "Arbeitsdatei"
// mit fuenf Bloecken (Blockanfang = Zeile "[NAME]", Blockende = "[ENDE]"):
//
//   [EBNF-QUELLTEXT]     huebsch formatierter, syntaxfehlerfreier Quelltext (wie im Listing)
//   [TS-SYMBOLTABELLE]   alle distinkten Terminale (TS-Literale und RNG-Bereiche)
//   [NTS-SYMBOLTABELLE]  alle Regelnamen + Startzeile in der Parser-Tabelle
//   [PARSER-TABELLE]     die eigentliche Tabelle, ein Eintrag pro Zeile, maschinell parsebar:
//                        zeile true false addr rngLo rngHi regel modus symbol
//                        (true/false/addr als Integer 1:1 wie intern, STAT_TRUE=-1 usw.;
//                        regel = "-" wenn leer; symbol = Rest der Zeile, verbatim)
//   [TESTS]              Nutzer-editierbare Testzeilen: TEST "<eingabe>" OK|FAIL
//   [NUTZER-CODE]        unveraenderter Platz fuer spaetere semantische Aktionen
//
// Die Arbeitsdatei ist AUCH Eingabe:
//   Fall A (Normalfall): <basis>.ebnf existiert -> IMMER die Wahrheit, alles wird neu
//     gebaut; die editierbaren Bloecke TESTS und NUTZER-CODE der alten Arbeitsdatei
//     werden vorher gerettet und unveraendert wieder mit hineingeschrieben
//     (loadPreservedTests()).
//   Fall B: <basis>.ebnf existiert NICHT, aber die Arbeitsdatei schon -> Tabelle wird
//     direkt aus dem PARSER-TABELLE-Block geladen (loadWorkfileAsGrammar()), danach
//     laufen resolveCallAddresses() und checkLeftRecursion() erneut darueber.
//
// Grenze (bewusst): Testeingaben duerfen selbst kein '"' enthalten (keine Escapes).
//------------------------------------------------------------------------------------------------
#define MAX_TESTS       256
#define TEST_INPUT_LEN  256
#define WORKFILE_LINE   512
#define USER_CODE_LEN   1048576

typedef struct {
	char input[TEST_INPUT_LEN];
	int expectOk;			// 1 = OK erwartet, 0 = FAIL erwartet
} TestCase;

TestCase testCases[MAX_TESTS];
int testCaseCnt = 0;
char userCodeBuf[USER_CODE_LEN];
int userCodeLen = 0;
int userCodeTruncated = 0;

void appendUserCodeLine(const char* line) {
	int n = (int)strlen(line);
	if (userCodeLen + n + 2 >= USER_CODE_LEN) {
		userCodeTruncated = 1;
		return;
	}
	memcpy(userCodeBuf + userCodeLen, line, n);
	userCodeLen += n;
	userCodeBuf[userCodeLen++] = EOL;
	userCodeBuf[userCodeLen] = EOS;
}

// [LEXER]-Block: wie NUTZER-CODE roh erhalten; zusaetzlich wird er vor der
// Codegenerierung an lexParseConfig() (codegen.cpp) uebergeben.
#define LEXER_CFG_LEN 4096
char lexerCfgBuf[LEXER_CFG_LEN];
int lexerCfgLen = 0;

void appendLexerCfgLine(const char* line) {
	int n = (int)strlen(line);
	if (lexerCfgLen + n + 2 >= LEXER_CFG_LEN) {
		printf("WARNUNG: [LEXER]-Block groesser als %d Bytes -- Rest ignoriert.\n", LEXER_CFG_LEN - 1);
		return;
	}
	memcpy(lexerCfgBuf + lexerCfgLen, line, n);
	lexerCfgLen += n;
	lexerCfgBuf[lexerCfgLen++] = EOL;
	lexerCfgBuf[lexerCfgLen] = EOS;
}

// [CODEGEN]-Block: analog LEXER -- roh erhalten, vor der Codegenerierung an
// cgenParseConfig() (codegen.cpp) uebergeben (z.B. M68K OS9 fuer r68/psect-Ausgabe).
#define CGEN_CFG_LEN 4096
char cgenCfgBuf[CGEN_CFG_LEN];
int cgenCfgLen = 0;

void appendCgenCfgLine(const char* line) {
	int n = (int)strlen(line);
	if (cgenCfgLen + n + 2 >= CGEN_CFG_LEN) {
		printf("WARNUNG: [CODEGEN]-Block groesser als %d Bytes -- Rest ignoriert.\n", CGEN_CFG_LEN - 1);
		return;
	}
	memcpy(cgenCfgBuf + cgenCfgLen, line, n);
	cgenCfgLen += n;
	cgenCfgBuf[cgenCfgLen++] = EOL;
	cgenCfgBuf[cgenCfgLen] = EOS;
}

//------------------------------------------------------------------------------------------------
// Escape-Sequenzen in Literalen
//------------------------------------------------------------------------------------------------
// Der Lexer liest Literale roh ein (inkl. Backslash-Sequenzen); HIER wird dekodiert,
// bevor Tabelle und AST befuellt werden -- das Listing (EBNF-QUELLTEXT) zeigt weiterhin
// die Original-Schreibweise. Unterstuetzt: \ddd (1-3 Oktalziffern, z.B. \042 = '"'),
// \t \n \r, \\ und \" (alle anderen \x ergeben x). Das war schon die Absicht der
// 2020er-Grammatiken (ebnf.ebnf: literal = "\042" {character} "\042".) -- bis jetzt
// wurden die Sequenzen aber faelschlich als rohe Zeichen gematcht.
void decodeEscapes(char* s) {
	char* r = s;
	char* w = s;

	while (*r) {
		if (*r == '\\' && r[1] != EOS) {
			r++;
			if (*r >= '0' && *r <= '7') {
				int v = 0, n = 0;
				while (n < 3 && *r >= '0' && *r <= '7') {
					v = v * 8 + (*r - '0');
					r++;
					n++;
				}
				*w++ = (char)v;
				continue;
			}
			switch (*r) {
			case 't': *w++ = '\t'; break;
			case 'n': *w++ = '\n'; break;
			case 'r': *w++ = '\r'; break;
			default:  *w++ = *r;   break;	// \\ und \" und alles andere: das Zeichen selbst
			}
			r++;
		}
		else {
			*w++ = *r++;
		}
	}
	*w = EOS;
}

// Gegenstueck fuer die Arbeitsdatei: Steuerzeichen und Backslash als \ooo enkodieren,
// damit die zeilenbasierte PARSER-TABELLE robust bleibt und der Fall-B-Reload per
// decodeEscapes() den Originalinhalt zurueckerhaelt.
void encodeTSField(const char* in, char* out, int outMax) {
	int n = 0;

	while (*in && n < outMax - 5) {
		unsigned char c = (unsigned char)*in;
		if (c < 32 || c == 127 || c == '\\') {
			n += snprintf(out + n, outMax - n, "\\%03o", c);
		}
		else {
			out[n++] = *in;
		}
		in++;
	}
	out[n] = EOS;
}

// Zeilenende (\n und ggf. Windows-\r) entfernen -- Arbeitsdateien koennen von beiden
// Plattformen stammen.
void chompLine(char* line) {
	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
		line[--len] = EOS;
	}
}

// TESTS-Block aus einer (alten) Arbeitsdatei retten, BEVOR sie per "w" ueberschrieben wird.
// Eine alte CSV-.lextab (Vorgaenger-Format) hat keinen [TESTS]-Block -> es wird schlicht
// nichts gefunden, das ist kein Fehler.
void loadPreservedTests(const char* path) {
	FILE* fp;
	char line[WORKFILE_LINE];
	int inTests = 0;
	int inUserCode = 0;
	int inLexer = 0;
	int inCgen = 0;

	testCaseCnt = 0;
	userCodeLen = 0;
	userCodeTruncated = 0;
	userCodeBuf[0] = EOS;
	lexerCfgLen = 0;
	lexerCfgBuf[0] = EOS;
	cgenCfgLen = 0;
	cgenCfgBuf[0] = EOS;
	if (fopen_s(&fp, path, "r") != 0) {
		return;					// keine alte Arbeitsdatei vorhanden -- nichts zu retten
	}
	while (fgets(line, WORKFILE_LINE, fp) != NULL) {
		chompLine(line);
		if (inUserCode) {
			if (strcmp(line, "[ENDE]") == 0) {
				inUserCode = 0;
			}
			else {
				appendUserCodeLine(line);
			}
			continue;
		}
		if (inLexer) {
			if (strcmp(line, "[ENDE]") == 0) {
				inLexer = 0;
			}
			else {
				appendLexerCfgLine(line);
			}
			continue;
		}
		if (inCgen) {
			if (strcmp(line, "[ENDE]") == 0) {
				inCgen = 0;
			}
			else {
				appendCgenCfgLine(line);
			}
			continue;
		}
		if (line[0] == '[') {
			inTests = (strncmp(line, "[TESTS]", 7) == 0);
			inUserCode = (strncmp(line, "[NUTZER-CODE]", 13) == 0);
			inLexer = (strncmp(line, "[LEXER]", 7) == 0);
			inCgen = (strncmp(line, "[CODEGEN]", 9) == 0);
			continue;
		}
		if (inTests && strncmp(line, "TEST", 4) == 0) {
			char* firstQuote = strchr(line, '"');
			char* lastQuote = strrchr(line, '"');
			if (firstQuote != NULL && lastQuote != NULL && lastQuote > firstQuote
				&& testCaseCnt < MAX_TESTS) {
				size_t n = (size_t)(lastQuote - firstQuote - 1);
				if (n > TEST_INPUT_LEN - 1) n = TEST_INPUT_LEN - 1;
				memcpy(testCases[testCaseCnt].input, firstQuote + 1, n);
				testCases[testCaseCnt].input[n] = EOS;
				testCases[testCaseCnt].expectOk = (strstr(lastQuote + 1, "FAIL") == NULL);
				testCaseCnt++;
			}
		}
	}
	fclose(fp);
	if (userCodeTruncated) {
		printf("WARNUNG: [NUTZER-CODE] ist groesser als %d Bytes und wurde gekuerzt.\n", USER_CODE_LEN - 1);
	}
}

// Distinkte Terminale (TS + RNG) aus der fertigen Parser-Tabelle einsammeln -- Grundlage
// fuer die TS-SYMBOLTABELLE und spaeter fuer den tabellenbasierten Lexer (Pass 1).
int tsSymbolIndexOf(char tsList[][IDENT_LEN + 1], int tsCnt, const char* sym) {
	int i;
	for (i = 0; i < tsCnt; i++) {
		if (strcmp(tsList[i], sym) == 0) return i;
	}
	return -1;
}

// Die komplette Arbeitsdatei schreiben. fp ist bereits per "w" geoeffnet (WICHTIG:
// erst NACH loadPreservedTests() oeffnen, sonst ist der alte TESTS-Block schon weg).
void writeWorkfile(FILE* fp) {
	static char tsList[LEXTAB_LEN][IDENT_LEN + 1];
	static char tsMode[LEXTAB_LEN][4];
	int tsCnt = 0;
	int i;

	fprintf(fp, "#================================================================================\n");
	fprintf(fp, "# EBNF-ARBEITSDATEI -- automatisch erzeugt von parsec\n");
	fprintf(fp, "# Die Bloecke [TESTS] und [NUTZER-CODE] sind zum Editieren gedacht und bleiben\n");
	fprintf(fp, "# beim Neu-Erzeugen erhalten. NUTZER-CODE ist vorbereitet, wird aber noch nicht\n");
	fprintf(fp, "# in C/68k eingebunden (Format/Architektur: docs/ARCHITEKTUR.md).\n");
	fprintf(fp, "# Existiert <basis>.ebnf, ist IMMER die .ebnf die Wahrheit; ohne .ebnf\n");
	fprintf(fp, "# kann diese Datei direkt als Grammatik geladen werden (PARSER-TABELLE-Block).\n");
	fprintf(fp, "#================================================================================\n\n");

	// Block 1: EBNF-QUELLTEXT (waehrend des Parsens von rule() mitgeschrieben)
	fprintf(fp, "[EBNF-QUELLTEXT]\n");
	fprintf(fp, "%s", quelltextBuf);
	if (quelltextLen > 0 && quelltextBuf[quelltextLen - 1] != EOL) {
		fprintf(fp, "\n");
	}
	fprintf(fp, "[ENDE]\n\n");

	// Block 2: TS-SYMBOLTABELLE (distinkte Terminale, Reihenfolge = erstes Vorkommen)
	for (i = 0; i < aktTabIndex; i++) {
		if ((strcmp(lexTab[i].mode, "TS") == 0 || strcmp(lexTab[i].mode, "RNG") == 0)
			&& tsSymbolIndexOf(tsList, tsCnt, lexTab[i].TS) < 0 && tsCnt < LEXTAB_LEN) {
			strncpy_s(tsList[tsCnt], sizeof(tsList[tsCnt]), lexTab[i].TS, IDENT_LEN);
			strcpy_s(tsMode[tsCnt], sizeof(tsMode[tsCnt]), lexTab[i].mode);
			tsCnt++;
		}
	}
	fprintf(fp, "[TS-SYMBOLTABELLE]\n");
	fprintf(fp, "# nr  typ  symbol\n");
	for (i = 0; i < tsCnt; i++) {
		char enc[2 * IDENT_LEN + 8];
		encodeTSField(tsList[i], enc, (int)sizeof(enc));
		fprintf(fp, "%-4d %-4s %s\n", i, tsMode[i], enc);
	}
	fprintf(fp, "[ENDE]\n\n");

	// Block 3: NTS-SYMBOLTABELLE (= ruleSymbols: Regelname + Startzeile)
	fprintf(fp, "[NTS-SYMBOLTABELLE]\n");
	fprintf(fp, "# nr  startzeile  name\n");
	for (i = 0; i < ruleSymbolCnt; i++) {
		fprintf(fp, "%-4d %-11d %s\n", i, ruleSymbols[i].addr, ruleSymbols[i].name);
	}
	fprintf(fp, "[ENDE]\n\n");

	// Block 4: PARSER-TABELLE (Integer-Werte 1:1 wie intern, symbol = Rest der Zeile)
	fprintf(fp, "[PARSER-TABELLE]\n");
	fprintf(fp, "# zeile true false addr rngLo rngHi regel modus symbol\n");
	fprintf(fp, "# (symbol: Steuerzeichen/Backslash als \\ooo enkodiert -- Zeilenformat!)\n");
	for (i = 0; i < aktTabIndex; i++) {
		char enc[2 * IDENT_LEN + 8];
		encodeTSField(lexTab[i].TS, enc, (int)sizeof(enc));
		fprintf(fp, "%-5d %-5d %-5d %-5d %-5d %-5d %-16s %-4s %s\n",
			i,
			lexTab[i].trueAction,
			lexTab[i].falseAction,
			lexTab[i].callAddr,
			(strcmp(lexTab[i].mode, "RNG") == 0) ? (int)(unsigned char)lexTab[i].rangeLo : 0,
			(strcmp(lexTab[i].mode, "RNG") == 0) ? (int)(unsigned char)lexTab[i].rangeHi : 0,
			strlen(lexTab[i].ident) ? lexTab[i].ident : "-",
			lexTab[i].mode,
			enc);
	}
	fprintf(fp, "[ENDE]\n\n");

	// Block 5: TESTS (aus der alten Arbeitsdatei gerettet, unveraendert zurueckschreiben)
	fprintf(fp, "[TESTS]\n");
	fprintf(fp, "# TEST \"<eingabe>\" OK|FAIL   -- wird bei jedem Lauf automatisch ausgefuehrt\n");
	for (i = 0; i < testCaseCnt; i++) {
		fprintf(fp, "TEST \"%s\" %s\n", testCases[i].input, testCases[i].expectOk ? "OK" : "FAIL");
	}
	fprintf(fp, "[ENDE]\n");

	// Block 6: LEXER-Konfiguration (roh erhalten; wird vor der Codegenerierung an
	// lexParseConfig() uebergeben, siehe codegen.h/docs/ARCHITEKTUR.md §8)
	fprintf(fp, "\n[LEXER]\n");
	if (lexerCfgLen == 0) {
		fprintf(fp, "# Optional: aktiviert Whitespace-/Kommentar-Behandlung im ERZEUGTEN Parser.\n");
		fprintf(fp, "# WHITESPACE = \" \\t\\r\\n\"\n");
		fprintf(fp, "# TOKEN ident            (Regel wird lexikalisch: Zeichen muessen adjazent sein)\n");
		fprintf(fp, "# COMMENT LINE = \"//\"\n");
		fprintf(fp, "# COMMENT BLOCK = \"(*\" \"*)\"          (NESTED vor '=' erlaubt Schachtelung)\n");
	}
	else {
		fprintf(fp, "%s", lexerCfgBuf);
	}
	fprintf(fp, "[ENDE]\n");

	// Block 6b: CODEGEN-Optionen (roh erhalten, geparst von cgenParseConfig())
	fprintf(fp, "\n[CODEGEN]\n");
	if (cgenCfgLen == 0) {
		fprintf(fp, "# Optional: Ausgabe-Optionen der Codegenerierung.\n");
		fprintf(fp, "# M68K OS9               (zusaetzlich <basis>_os9.a im r68/psect-Format)\n");
		fprintf(fp, "# M68K PSECT = name      (psect-Name, Default: <basisname>_p)\n");
	}
	else {
		fprintf(fp, "%s", cgenCfgBuf);
	}
	fprintf(fp, "[ENDE]\n");

	// Block 7: NUTZER-CODE. Der Inhalt bleibt absichtlich roh: C, 68k und spaetere
	// Aktions-Metadaten duerfen hier stehen, ohne dass der EBNF-Parser sie kennen muss.
	fprintf(fp, "\n[NUTZER-CODE]\n");
	if (userCodeLen == 0) {
		fprintf(fp, "# Semantische Aktionen (siehe docs/ARCHITEKTUR.md, Abschnitt 9). Beispiel:\n");
		fprintf(fp, "# ACTION AFTER <regel> CALL <name>\n");
		fprintf(fp, "# ROUTINE C <name>\n");
		fprintf(fp, "# void <name>(const char* start, const char* end) { /* ... */ }\n");
		fprintf(fp, "# END\n");
		fprintf(fp, "# ROUTINE M68K <name>\n");
		fprintf(fp, "# <name>:\t; a0 = Ende des erkannten Textes, MUSS erhalten bleiben\n");
		fprintf(fp, "#\trts\n");
		fprintf(fp, "# END\n");
	}
	else {
		fprintf(fp, "%s", userCodeBuf);
		if (userCodeTruncated) fprintf(fp, "# WARNUNG: Eingabe war groesser als %d Bytes.\n", USER_CODE_LEN - 1);
	}
	fprintf(fp, "[ENDE]\n");
}

// Fall B: Kanten fuer die Linksrekursions-Pruefung aus der geladenen Tabelle rekonstruieren.
// Idee: ein Fehlschlag konsumiert nie Eingabe -- alles, was vom Regelstart aus NUR ueber
// falseAction-Kanten erreichbar ist, liegt an "erster Position" der Regel. Jede NTS-Zeile
// auf diesem Weg ergibt eine Kante Regel->Zielregel (dieselbe Semantik, die beim normalen
// Parsen ueber das firstPos-Flag entsteht; die dokumentierte Grenze "leere Regeln werden
// nicht erkannt" gilt hier genauso).
void rebuildFirstEdgesFromTable() {
	static int visited[LEXTAB_LEN];
	int r, row, i;

	firstEdgeCnt = 0;
	ruleNameListCnt = 0;
	for (r = 0; r < aktTabIndex; r++) {
		if (strlen(lexTab[r].ident) == 0) continue;
		addRuleNameIfNew(lexTab[r].ident);
		for (i = 0; i < aktTabIndex; i++) visited[i] = 0;
		row = r;
		while (row >= 0 && row < aktTabIndex && !visited[row]) {
			visited[row] = 1;
			if (strcmp(lexTab[row].mode, "NTS") == 0) {
				addFirstEdge(lexTab[r].ident, lexTab[row].TS);
			}
			row = lexTab[row].falseAction;		// nur Misserfolgs-Kanten: konsumieren nichts
		}
	}
}

// Fall B: PARSER-TABELLE- und EBNF-QUELLTEXT-Block direkt aus der Arbeitsdatei laden
// (keine .ebnf vorhanden). Liefert 1 bei Erfolg, 0 wenn die Datei keine Arbeitsdatei
// im neuen Format ist (z.B. alte CSV-.lextab) oder nicht lesbar war.
int loadWorkfileAsGrammar(const char* path) {
	FILE* fp;
	char line[WORKFILE_LINE];
	enum { BLK_NONE, BLK_QUELLTEXT, BLK_PARSERTAB, BLK_SONST } blk = BLK_NONE;
	int rowsLoaded = 0;

	if (fopen_s(&fp, path, "r") != 0) {
		return 0;
	}
	aktTabIndex = 0;
	quelltextLen = 0;
	quelltextBuf[0] = EOS;

	while (fgets(line, WORKFILE_LINE, fp) != NULL) {
		chompLine(line);
		if (line[0] == '[') {
			if (strncmp(line, "[EBNF-QUELLTEXT]", 16) == 0)      blk = BLK_QUELLTEXT;
			else if (strncmp(line, "[PARSER-TABELLE]", 16) == 0) blk = BLK_PARSERTAB;
			else                                                 blk = BLK_SONST;
			continue;
		}
		if (blk == BLK_QUELLTEXT) {
			appendQuelltext(line);
			appendQuelltext("\n");
		}
		else if (blk == BLK_PARSERTAB) {
			int nr, tA, fA, addr, lo, hi, consumed = -1;
			char identBuf[IDENT_LEN + 1];
			char modeBuf[8];
			if (line[0] == '#' || line[0] == EOS) continue;
			if (sscanf(line, "%d %d %d %d %d %d %32s %7s %n",
				&nr, &tA, &fA, &addr, &lo, &hi, identBuf, modeBuf, &consumed) >= 8
				&& consumed >= 0 && aktTabIndex < LEXTAB_LEN) {
				TabEntry* e = &lexTab[aktTabIndex];
				if (strcmp(identBuf, "-") == 0) {
					e->ident[0] = EOS;
				}
				else {
					strncpy_s(e->ident, sizeof(e->ident), identBuf, IDENT_LEN);
				}
				// mode als persistente statische Strings (KEINE Pointer auf Puffer!)
				if (strcmp(modeBuf, "TS") == 0)       e->mode = (char*)"TS";
				else if (strcmp(modeBuf, "NTS") == 0) e->mode = (char*)"NTS";
				else if (strcmp(modeBuf, "RNG") == 0) e->mode = (char*)"RNG";
				else {
					printf("FEHLER: Arbeitsdatei Zeile %d: unbekannter Modus '%s'\n", nr, modeBuf);
					errorCnt++;
					continue;
				}
				e->trueAction = tA;
				e->falseAction = fA;
				e->callAddr = addr;
				e->rangeLo = (char)lo;
				e->rangeHi = (char)hi;
				e->ambigF = 0;		// reines Parse-Zeit-Flag, wird nicht serialisiert
				strncpy_s(e->TS, sizeof(e->TS), line + consumed, IDENT_LEN);
				decodeEscapes(e->TS);	// Gegenstueck zu encodeTSField() beim Schreiben
				aktTabIndex++;
				rowsLoaded++;
			}
		}
	}
	fclose(fp);

	if (rowsLoaded == 0) {
		printf("FEHLER: '%s' enthaelt keinen [PARSER-TABELLE]-Block (alte CSV-Datei?)\n", path);
		return 0;
	}
	return 1;
}

// Alle TEST-Zeilen durch die Stack-Maschine jagen und mit dem erwarteten Ergebnis
// vergleichen. Liefert die Anzahl der MISMATCHes (0 = alles gruen).
int runTests() {
	int i, mismatches = 0;

	if (testCaseCnt == 0) {
		return 0;
	}
	printf("\n=== TESTS (%d Testfaelle) ===\n", testCaseCnt);
	if (leftRecursionFound || errorCnt > 0) {
		printf("uebersprungen: Grammatik ist %s\n",
			leftRecursionFound ? "linksrekursiv" : "fehlerhaft");
		return 0;
	}
	for (i = 0; i < testCaseCnt; i++) {
		int pos = 0;
		int ok, fullOk;

		inputBuf = testCases[i].input;
		inputLen = (int)strlen(inputBuf);
		ok = execFrom(0, &pos);
		fullOk = (ok && pos == inputLen);
		if (fullOk == testCases[i].expectOk) {
			printf("PASS      TEST \"%s\" %s\n", testCases[i].input,
				testCases[i].expectOk ? "OK" : "FAIL");
		}
		else {
			printf("MISMATCH  TEST \"%s\" erwartet %s, erhalten %s", testCases[i].input,
				testCases[i].expectOk ? "OK" : "FAIL", fullOk ? "OK" : "FAIL");
			if (ok && !fullOk) {
				printf(" (nur %d von %d Zeichen erkannt)", pos, inputLen);
			}
			printf("\n");
			mismatches++;
		}
	}
	printf("=== %d/%d PASS%s ===\n", testCaseCnt - mismatches, testCaseCnt,
		mismatches ? ", MISMATCHES!" : "");
	return mismatches;
}

//------------------------------------------------------------------------------------------------
// NTS Collection
//------------------------------------------------------------------------------------------------
char identList[IDENT_LEN + 1][IDENTLIST_MAX + 2];
int identListCnt = 0;
void addIdentList() {
	// printf("IDENT_LIST ADD %s \n", aktName);
	if (identListCnt < IDENTLIST_MAX) {
	//	strncpy_s(identList[identListCnt++], sizeof(identList[identListCnt++]), aktName, IDENT_LEN);
	}
}

//------------------------------------------------------------------------------------------------
// lineStack
//------------------------------------------------------------------------------------------------

#define LINESTACK_MAX 1024
int lineStack[LINESTACK_MAX];
int lineStackIndex = 0;

void push(int line) {
	if (lineStackIndex < LINESTACK_MAX) {
		lineStack[lineStackIndex++] = line;
	}
	else {
		printf("LEX_ERROR >>> Stack Overflow");
	}
}

int pop() {
	if (lineStackIndex > 0) {
		return lineStack[--lineStackIndex];
	}
	else {
		printf("LEX_ERROR >>> Stack underflow");
		return -1;
	}
}


//------------------------------------------------------------------------------------------------
// Pharser
//------------------------------------------------------------------------------------------------

void literal();
void ident();
void block();
void repeat();
void option();
void factor();
void term();
void expression();
void rule();
void patchLocalTrue(int fromRow, int toRowExclusive, int target);
void patchLocalFalse(int fromRow, int toRowExclusive, int target);

int restart() {
	int i = 0;
	while (restartToken[i]) {
		if (aktToken == restartToken[i++]) {
			return 1;
		}
	}
	return 0;
}

void errorMsg(const char* msg) {
	printf("<< %s erwartet. >>", msg);
	errorCnt++;
	while (!restart()) {
		lexikalischeAnalyse();
	}
}

void test(int checkTS, const char* msg) {
	if (aktToken != checkTS) {
		errorMsg(msg);
	}
	else {
		lexikalischeAnalyse();
	}
}

void ebnfSyntax() {

	restartToken[0] = TOKEN_END;
	restartToken[1] = TOKEN_EXIT;
	restartToken[2] = EOS;

	getAktChar();
	lexikalischeAnalyse();
	while (aktToken != TOKEN_EXIT) {
		rule();
	}
	put();
}

void rule() {
	char bracketedName[IDENT_LEN + 3];
	int ruleStart = aktTabIndex;

	clearLst();
	test(TOKEN_IDENT, (char *)"IDENT ident ");
	addIdentList();
	sprintf_s(bracketedName, sizeof(bracketedName), "<%s>", aktName);		// Listing zeigt NTS immer als <name>
	addLst(bracketedName, 32);
	strncpy_s(aktRule, sizeof(aktRule), aktName, IDENT_LEN);
	strncpy_s(currentDefRule, sizeof(currentDefRule), aktName, IDENT_LEN);
	addRuleNameIfNew(currentDefRule);
	firstPos = 1;
	ambigFalseWarned = 0;
	test(TOKEN_EQUAL, (char *)"EQUAL Symbol =");
	addLst((const char*)"=", 0);
	expression();
	if (aktToken == TOKEN_END) {
		put();
		test(TOKEN_END, (char *)"END Symbol .");
		
		// UEBER DIE GANZE REGEL scannen, nicht nur die letzte Zeile: ein {..}/[..]/(..)
		// als LETZTES Konstrukt der Regel patcht seine eigene falseAction/trueAction
		// (Schleifen-/Gruppen-Ende) auf "aktTabIndex zum Zeitpunkt des Patches" -- steht
		// danach nichts mehr in DIESER Regel, zeigt das faelschlich auf die naechste Regel
		// (Zeilennummern sind fortlaufend ueber alle Regeln hinweg). Betrifft nicht
		// zwingend die LETZTE Zeile der Regel (z.B. bei "X = A {B A}.": die dangling
		// Referenz sitzt auf der B-Zeile, nicht auf der letzten A-Zeile).
		{
			int i;
			for (i = ruleStart; i < aktTabIndex; i++) {
				if (lexTab[i].trueAction >= aktTabIndex) {
					lexTab[i].trueAction = STAT_TRUE;
				}
				if (lexTab[i].falseAction >= aktTabIndex) {
					lexTab[i].falseAction = STAT_TRUE;
				}
			}
		}
		
		// strncpy_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), ".", IDENT_LEN);
		// aktTabIndex++;

		addLst((const char*)".", 0);
		printf("%s\n\n", lst);
		fprintf(fpLst, "%s\n\n", lst);
		appendQuelltext(lst);
		appendQuelltext("\n\n");
	}
	astFinishRule(currentDefRule);

}

int expressionCount = 0;
void expression() {
	int entryFirstPos = firstPos;		// jede Alternative beginnt an derselben "ersten Position"
	int altStart = aktTabIndex;		// Startzeile DER AKTUELLEN Alternative
	int astM = astMark();			// AST: alle Alternativen dieser Auswahl sammeln

	push(aktTabIndex);
	term();

	while (aktToken == TOKEN_OR) {
		// Diese Alternative ist fertig geparst (aktToken zeigt jetzt auf '|').
		// Ueber den GANZEN Zeilenbereich der Alternative (nicht nur die Startzeile --
		// das war Bug Nr. 6: bei mehrfaktorigen Alternativen wie "a" "b" | "c" erklaerte
		// schon der Erfolg des ERSTEN Faktors die ganze Regel fuer fertig, und bei
		// komplexen ersten Faktoren wie [ "-" ] ... | ... wurde deren eigene
		// Verdrahtung zerstoert) gilt:
		//  - Erfolg der Alternative (STAT_TRUE oder "dangling" Vorwaertsreferenz
		//    hinter das Alternativen-Ende) -> ganze Auswahl fertig (kurzschliessen)
		//  - Misserfolg ohne Konsum (STAT_FALSE) -> Start der NAECHSTEN Alternative
		//    (= aktueller aktTabIndex, dort schreibt der naechste term() gleich hin).
		// Committed-Fehlschlaege (STAT_ERROR) bleiben unveraendert Fehler.
		patchLocalTrue(altStart, aktTabIndex, STAT_TRUE);
		patchLocalFalse(altStart, aktTabIndex, aktTabIndex);

		if (expressionCount == 0) {
			addLst("\n", 34);
		}
		addLst("|", 0);
		lexikalischeAnalyse();
		expressionCount++;
		firstPos = entryFirstPos;
		altStart = aktTabIndex;		// Start der naechsten Alternative merken
		term();
		expressionCount--;
	}
	// Letzte (oder einzige) Alternative: term() hat ihre Startzeile bereits korrekt
	// gesetzt (Sequenz-Fortsetzung intern bzw. STAT_FALSE nach aussen); rule() biegt
	// ein "dangling" trueAction am Regelende automatisch auf STAT_TRUE um. Hier also
	// nichts mehr anfassen -- nur den Stack balancieren (Wert wird nicht mehr gebraucht).
	if (lineStackIndex > 0) {
		pop();
	}
	astGroupAlt(astM);
}

void term(void) {

	int last = 0;
	int firstFactorComplex;
	int committed = 0;		// hat ein frueherer Faktor dieser Sequenz sicher Eingabe konsumiert?
	int hadSkippable = 0;	// kam in dieser Sequenz schon eine ueberspringbare Gruppe vor?
	int factorStart;
	int wasAmbig;
	int i;
	int astM = astMark();	// AST: alle Faktoren dieser Sequenz sammeln

	if (aktToken == TOKEN_END) {
		return;
	}

	push(aktTabIndex);
	factor();
	firstFactorComplex = lastFactorWasComplex;	// NUR direkt nach dem ERSTEN Faktor auswerten,
												// bevor er von weiteren factor()-Aufrufen ueberschrieben wird
	if (lastFactorSkippable) hadSkippable = 1;
	else committed = 1;

	while (aktToken == TOKEN_IDENT || aktToken == TOKEN_LITERAL || aktToken == TOKEN_BLOCKON || aktToken == TOKEN_REPEATON || aktToken == TOKEN_OPTIONON || aktToken == TOKEN_SEQ) {
		// Vorverdrahtung des naechsten Faktors: Erfolg -> naechste Zeile. Misserfolg:
		// solange NUR ueberspringbare Gruppen vor diesem Faktor lagen, ist noch nichts
		// konsumiert -> STAT_FALSE (Fehlschlag ohne Konsum, darf noch eine andere
		// Alternative anspringen). Nach einem sicher konsumierenden Faktor dagegen
		// STAT_ERROR: committed, kein Zuruecksetzen mehr. (War Bug Nr. 5: pauschal
		// STAT_ERROR -- dadurch scheiterte z.B. [ "-" ] "a" "x" | "y" bei "ax".)
		wasAmbig = (!committed && hadSkippable);
		lexTab[aktTabIndex].trueAction = aktTabIndex+1;
		lexTab[aktTabIndex].falseAction = committed ? STAT_ERROR : STAT_FALSE;

		if (aktToken == TOKEN_SEQ) {
			lexikalischeAnalyse();
		}
		if (aktToken == TOKEN_IDENT || aktToken == TOKEN_LITERAL || aktToken == TOKEN_BLOCKON || aktToken == TOKEN_REPEATON || aktToken == TOKEN_OPTIONON) {
			factorStart = aktTabIndex;
			factor();
			if (committed) {
				// Block-interne "Fehlschlag ohne Konsum"-Zeilen (F) liegen nach einem
				// sicher konsumierenden Faktor in Wahrheit im committed-Bereich -> E,
				// sonst wuerde ein spaeterer Alternativen-Dispatch faelschlich mit schon
				// konsumierter Eingabe weitermachen.
				patchLocalFalse(factorStart, aktTabIndex, STAT_ERROR);
			}
			else if (wasAmbig) {
				// Faktor an "nur Ueberspringbares davor"-Position: seine F-Zeilen sind
				// runtime-mehrdeutig (Gruppe koennte doch konsumiert haben) -- markieren,
				// gewarnt wird erst, wenn daraus wirklich ein Zeilen-Sprung wird.
				for (i = factorStart; i < aktTabIndex; i++) {
					if (lexTab[i].falseAction == STAT_FALSE) {
						lexTab[i].ambigF = 1;
					}
				}
			}
			if (lastFactorSkippable) hadSkippable = 1;
			else committed = 1;
		}
	}
	if (lineStackIndex > 0) {
		last = pop();
		// Nur anwenden, wenn der ERSTE Faktor "einfach" war (ident/literal) und daher
		// noch KEIN eigenes trueAction/falseAction gesetzt hat. War er komplex (block/
		// repeat/option), hat der sich schon selbst korrekt verdrahtet -- das hier wuerde
		// es sonst wieder zerstoeren (der Bug hinter dem gescheiterten "num"-Test).
		if (!firstFactorComplex) {
			lexTab[last].trueAction = last+1;
			lexTab[last].falseAction = STAT_FALSE;
		}
	}
	astGroupSeq(astM);

}

void factor() {
	switch (aktToken) {
	case TOKEN_IDENT:
		ident();
		aktTabIndex++;
		break;
	case TOKEN_BLOCKON:
		block();
		break;
	case TOKEN_REPEATON:
		repeat();
		break;
	case TOKEN_OPTIONON:
		option();
		break;
	case TOKEN_LITERAL:
		literal();
		aktTabIndex++;
		break;
	default:
		errorMsg((char *)"IDENT or BLOCK or REPEAT or OPTION or LITERAL");
	}

}

// Nachbearbeitung fuer block()/repeat()/option(): expression() setzt fuer die letzte
// (oder einzige) Alternative ihrer eigenen Zeile trueAction=STAT_TRUE (bzw. laesst
// falseAction=STAT_FALSE), weil sie nicht wissen kann, dass sie gerade INNERHALB eines
// (..)/{..}/[..]-Konstrukts steht statt am Ende der GANZEN Regel. STAT_TRUE wuerde zur
// Laufzeit sofort die komplette Regel fuer beendet erklaeren -- korrekt nur, wenn dieses
// Konstrukt tatsaechlich die aeusserste Regel selbst ist. Diese beiden Funktionen biegen
// alle noch offenen STAT_TRUE/STAT_FALSE-Vorkommen im gerade abgeschlossenen Zeilenbereich
// auf das um, was in DIESEM Kontext eigentlich gemeint ist (siehe Aufrufer).
void patchLocalTrue(int fromRow, int toRowExclusive, int target) {
	int i;
	for (i = fromRow; i < toRowExclusive; i++) {
		// STAT_TRUE: die letzte Alternative eines nested "a|b" wurde per Kurzschluss
		// dorthin gesetzt. "Dangling" (>= toRowExclusive): die letzte (oder einzige)
		// Alternative eines nested Terms wurde von term()'s eigenem Backpatching noch
		// auf "naechste Zeile" gesetzt, die es zu diesem Zeitpunkt noch gar nicht gibt --
		// genau der Fall, den sonst nur rule() am Ende der GANZEN Regel aufloest (dort zu
		// STAT_TRUE). Innerhalb von (..)/{..}/[..] muss das HIER, auf unseren eigenen
		// Kontext bezogen, aufgeloest werden statt dem aeusseren rule()-Cleanup ueberlassen
		// zu werden -- sonst "gewinnt" hinterher das falsche (zu globale) Cleanup.
		if (lexTab[i].trueAction == STAT_TRUE || lexTab[i].trueAction >= toRowExclusive) {
			lexTab[i].trueAction = target;
		}
	}
}

void patchLocalFalse(int fromRow, int toRowExclusive, int target) {
	int i;
	for (i = fromRow; i < toRowExclusive; i++) {
		if (lexTab[i].falseAction == STAT_FALSE) {
			// Wird ein "nach ueberspringbarer Gruppe"-Fehlschlag auf eine ECHTE Zeile
			// umgebogen (naechste Alternative bzw. Fortsetzung nach [..]/{..}), kann die
			// Maschine dort mit bereits konsumierter Eingabe weitermachen, falls die
			// Gruppe zur Laufzeit doch etwas konsumiert hatte -- die flache Tabelle hat
			// keinen Ruecksetzpunkt dafuer. Bekannte Grenze, wird klar gewarnt.
			if (target >= 0 && lexTab[i].ambigF && !ambigFalseWarned) {
				printf("WARNUNG: Regel '%s': Pflicht-Faktor nach ueberspringbarer Gruppe ([..]/{..})\n"
					"         vor einer Alternative/umschliessenden Gruppe: konsumiert die Gruppe\n"
					"         Eingabe und der Faktor schlaegt fehl, wird die Position NICHT\n"
					"         zurueckgesetzt (moeglicher Fehlakzept). Grammatik ggf. umformen.\n",
					currentDefRule);
				ambigFalseWarned = 1;
			}
			lexTab[i].falseAction = target;
		}
	}
}

void block(void) {
	int blockStart = aktTabIndex;

	addLst("(", 0);

	// firstPos wird UNVERAENDERT vom umgebenden Kontext geerbt: war vorher schon ein
	// Terminal konsumiert (firstPos==0), ist auch der Inhalt von "(...)" nicht mehr an
	// "erster Position" der aeusseren Regel erreichbar.
	lexikalischeAnalyse();
	expression();
	test(TOKEN_BLOCKOFF, (char *)"BLOCKOFF Symbol )");
	addLst(")", 0);
	firstPos = 0;		// Block ist verpflichtend -> bricht jede Linksrekursions-Kette

	// Erfolg -> weiter beim naechsten Faktor nach dem Block (aktTabIndex = genau das,
	// da dort noch nichts geschrieben wurde). Misserfolg bleibt STAT_FALSE/STAT_ERROR:
	// ein Pflicht-Block, der nicht matcht, soll die ganze umschliessende Regel scheitern
	// lassen -- das ist bei "(...)" (im Gegensatz zu "[...]"/"{...}") so gewollt.
	patchLocalTrue(blockStart, aktTabIndex, aktTabIndex);
	lastFactorWasComplex = 1;
	lastFactorSkippable = 0;	// (...) ist verpflichtend: Erfolg konsumiert (praktisch immer)
}

void repeat() {
	int repStart = aktTabIndex;
	int savedFirstPos = firstPos;

	addLst("{", 0);

	// firstPos bleibt wie vom Aufrufer geerbt (siehe Kommentar in block())
	lexikalischeAnalyse();
	expression();
	test(TOKEN_REPEATOFF, (char *)"REPEATOFF Symbol }");
	addLst("}", 0);
	firstPos = savedFirstPos;	// {..} ist ueberspringbar -> "erste Position" bleibt wie zuvor

	// Erfolg -> zurueck an den Anfang des Wiederholungs-Inhalts (nochmal versuchen).
	// Misserfolg -> das ist bei einer Wiederholung kein Fehler, sondern das normale Ende
	// der Schleife (0 Treffer sind erlaubt): weiter beim naechsten Faktor danach.
	patchLocalTrue(repStart, aktTabIndex, repStart);
	patchLocalFalse(repStart, aktTabIndex, aktTabIndex);
	lastFactorWasComplex = 1;
	lastFactorSkippable = 1;	// {..} darf 0-mal matchen
	astWrapRep();
}

void option() {
	int optStart = aktTabIndex;
	int savedFirstPos = firstPos;

	addLst("[", 0);

	// firstPos bleibt wie vom Aufrufer geerbt (siehe Kommentar in block())
	lexikalischeAnalyse();
	expression();
	test(TOKEN_OPTIONOFF, (char *)"OPTIONOFF Symbol ]");
	addLst("]", 0);
	firstPos = savedFirstPos;	// [..] ist ueberspringbar -> "erste Position" bleibt wie zuvor

	// Sowohl Erfolg als auch Misserfolg fuehren zum naechsten Faktor danach -- eine
	// Option matcht 0 oder 1 mal, ein Fehlschlag ist also kein Fehler.
	patchLocalTrue(optStart, aktTabIndex, aktTabIndex);
	patchLocalFalse(optStart, aktTabIndex, aktTabIndex);
	lastFactorWasComplex = 1;
	lastFactorSkippable = 1;	// [..] darf 0-mal matchen
	astWrapOpt();
}

void ident() {
	char bracketedName[IDENT_LEN + 3];

	if (firstPos) {
		addFirstEdge(currentDefRule, aktName);
	}
	firstPos = 0;
	lastFactorWasComplex = 0;
	lastFactorSkippable = 0;
	astPushNTS(aktName);
	sprintf_s(bracketedName, sizeof(bracketedName), "<%s>", aktName);		// Listing zeigt NTS immer als <name>
	addLst(bracketedName, 0);
	put();
	lexikalischeAnalyse();
}

void literal() {
	char loChar[IDENT_LEN + 1];
	char loRaw[IDENT_LEN + 1];

	firstPos = 0;
	lastFactorWasComplex = 0;
	lastFactorSkippable = 0;
	strncpy_s(loRaw, sizeof(loRaw), aktString, IDENT_LEN);		// Original-Schreibweise fuers Listing
	strncpy_s(loChar, sizeof(loChar), aktString, IDENT_LEN);
	decodeEscapes(loChar);						// dekodiert fuer Tabelle + AST
	lexikalischeAnalyse();		// ein Token vorausschauen: folgt ein '~' Bereichs-Operator?

	if (aktToken == TOKEN_RANGE) {
		lexikalischeAnalyse();	// '~' konsumieren
		if (aktToken != TOKEN_LITERAL) {
			errorMsg((char *)"LITERAL nach '~' (Zeichenbereich) erwartet");
			return;
		}
		else {
			char hiChar[IDENT_LEN + 1];
			char hiRaw[IDENT_LEN + 1];
			strncpy_s(hiRaw, sizeof(hiRaw), aktString, IDENT_LEN);
			strncpy_s(hiChar, sizeof(hiChar), aktString, IDENT_LEN);
			decodeEscapes(hiChar);

			astPushRNG(loChar[0], hiChar[0]);
			strcat_s(lst, sizeof(lst), " \"");
			strcat_s(lst, sizeof(lst), loRaw);
			strcat_s(lst, sizeof(lst), "\"~\"");
			strcat_s(lst, sizeof(lst), hiRaw);
			strcat_s(lst, sizeof(lst), "\"");

			if (errorCnt == 0) {
				if (strlen(aktRule)) {
					strncpy_s(lexTab[aktTabIndex].ident, sizeof(lexTab[aktTabIndex].ident), aktRule, IDENT_LEN);
					aktRule[0] = EOS;
				}
				else {
					lexTab[aktTabIndex].ident[0] = EOS;
				}
				lexTab[aktTabIndex].mode = (char *)"RNG";
				lexTab[aktTabIndex].rangeLo = loChar[0];
				lexTab[aktTabIndex].rangeHi = hiChar[0];
				strcpy_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), "\"");
				strncat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), loChar, IDENT_LEN);
				strcat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), "\"~\"");
				strncat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), hiChar, IDENT_LEN);
				strcat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), "\"");
			}
			lexikalischeAnalyse();	// weiter zum Token nach dem zweiten Literal
		}
	}
	else {
		astPushTS(loChar);
		strcat_s(lst, sizeof(lst), " \"");
		strcat_s(lst, sizeof(lst), loRaw);
		strcat_s(lst, sizeof(lst), "\"");

		if (errorCnt == 0) {
			if (strlen(aktRule)) {
				strncpy_s(lexTab[aktTabIndex].ident, sizeof(lexTab[aktTabIndex].ident), aktRule, IDENT_LEN);
				aktRule[0] = EOS;
			}
			else {
				lexTab[aktTabIndex].ident[0] = EOS;
			}
			lexTab[aktTabIndex].mode = (char *)"TS";
			strcpy_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), "\"");
			strncat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), loChar, IDENT_LEN);
			strcat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), "\"");
		}
		// aktToken zeigt bereits auf das naechste Token (s.o. vorausgeschaut) --
		// hier KEIN weiteres lexikalischeAnalyse() mehr aufrufen!
	}
}


//------------------------------------------------------------------------------------------------
// Lexikalische Analyse
//------------------------------------------------------------------------------------------------
int flagEOF = 0;


int isIdentChar(char ch) {
	if (isalnum(ch) || ch == '_' || ch == '$') {
		return 1;
	}
	else {
		return 0;
	}
}

int isFirstIdentChar(char ch) {
	if (isalpha(ch) || ch == '_' || ch == '$') {
		return 1;
	}
	else {
		return 0;
	}
}

int isLiteralDelimiter(char ch) {
	if (ch == '"') {
		return 1;
	}
	else {
		return 0;
	}
}

void lexikalischeAnalyse() {
	int index = 0;

	// ignore spaces 
	while (isspace(aktChar) && aktChar != EOF) {
		getAktChar();
	};
	if (aktChar == EOF) {
		lastToken = aktToken;
		aktToken = TOKEN_EXIT;
		return;
	}
	// Check for idends
	if (isFirstIdentChar(aktChar)) {
		aktName[index++] = aktChar;
		getAktChar();
		strcpy_s(lastName, sizeof(lastName), aktName);
		while (isIdentChar(aktChar)) {
			if (index < IDENT_LEN) {
				aktName[index++] = aktChar;
			}
			getAktChar();
		}
		aktName[index] = EOS;
		lastToken = aktToken;
		aktToken = TOKEN_IDENT;
	} else if (aktChar == '<') {
		// Alternative NTS-Schreibweise <name> -- wird wie ein normaler Bezeichner behandelt,
		// Parser/Semantik sehen keinen Unterschied zu einem "nackten" ident.
		getAktChar();
		while (isIdentChar(aktChar)) {
			if (index < IDENT_LEN) {
				aktName[index++] = aktChar;
			}
			getAktChar();
		}
		aktName[index] = EOS;
		if (aktChar == '>') {
			getAktChar();
		}
		else {
			printf("LEX_ERROR: '>' nach <%s erwartet\n", aktName);
		}
		lastToken = aktToken;
		aktToken = TOKEN_IDENT;
	} else if (isLiteralDelimiter(aktChar)) {
		// Literal
		getAktChar();
		strcpy_s(lastString, sizeof(lastString), aktString);
		while (!(aktChar == '"' && lastChar != '\\')) {
			if (index < IDENT_LEN) {
				aktString[index++] = aktChar;
			}
			getAktChar();
		}	
		aktString[index] = EOS;
		getAktChar();
		lastToken = aktToken;
		aktToken = TOKEN_LITERAL;
	} else {
		if (flagEOF) {
			lastToken = aktToken;
			aktToken = TOKEN_EXIT;
		}
		else {
			// Spezial Token
			lastToken = aktToken;
			switch (aktChar) {
				case '=': aktToken = TOKEN_EQUAL; 		break;
				case ',': aktToken = TOKEN_SEQ; 		break;
				case '|': aktToken = TOKEN_OR;			break;
				case '"': aktToken = TOKEN_QUOT;		break;
				case '(': aktToken = TOKEN_BLOCKON; 	break;
				case ')': aktToken = TOKEN_BLOCKOFF; 	break;
				case '[': aktToken = TOKEN_OPTIONON; 	break;
				case ']': aktToken = TOKEN_OPTIONOFF; 	break;
				case '{': aktToken = TOKEN_REPEATON; 	break;
				case '}': aktToken = TOKEN_REPEATOFF; 	break;
				case ';': aktToken = TOKEN_END; 		break;
				case '.': aktToken = TOKEN_END; 		break;
				case '~': aktToken = TOKEN_RANGE; 		break;	// Bereichsoperator, bewusst nicht "-"
														// (das ist in ISO 14977 bereits der
														// Except-/Mengendifferenz-Operator)
				case EOF: aktToken = TOKEN_EXIT; 		break;
				default:  aktToken = TOKEN_ERROR;
					printf("LEX_ERROR: flasches Zeichen <%c>\n", aktChar);
			}
		}
		getAktChar();
	}
}


//------------------------------------------------------------------------------------------------
// Semantische analyser
//------------------------------------------------------------------------------------------------
int tableIdentFlag = 0;


void put() {
	if (errorCnt == 0) {
		semantischeAnylyse();
	}
}

void semantischeAnylyse() {

	switch (aktToken) {
	case TOKEN_IDENT:
		if (strlen(aktRule)) {
			strncpy_s(lexTab[aktTabIndex].ident, sizeof(lexTab[aktTabIndex].ident), aktRule, IDENT_LEN);
			aktRule[0] = '\0';
		}
		else {
			lexTab[aktTabIndex].ident[0] = '\0';
		}
		lexTab[aktTabIndex].mode = (char *)"NTS";
		strncpy_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), aktName, IDENT_LEN);
		//aktTabIndex++;
		break;

	case TOKEN_LITERAL:
		if (strlen(aktRule)) {
			strncpy_s(lexTab[aktTabIndex].ident, sizeof(lexTab[aktTabIndex].ident), aktRule, IDENT_LEN);
			aktRule[0] = EOS;
		}
		else {
			lexTab[aktTabIndex].ident[0] = EOS;
		}
		lexTab[aktTabIndex].mode = (char *)"TS";
		strcpy_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), "\"");
		strncat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), aktString, IDENT_LEN);
		strcat_s(lexTab[aktTabIndex].TS, sizeof(lexTab[aktTabIndex].TS), "\"");
		//aktTabIndex++;
		break;

	case TOKEN_START:
	case TOKEN_EQUAL:
	case TOKEN_SEQ:
	case TOKEN_OR:
	case TOKEN_BLOCKON:
	case TOKEN_BLOCKOFF:
	case TOKEN_OPTIONON:
	case TOKEN_OPTIONOFF:
	case TOKEN_REPEATON:
	case TOKEN_REPEATOFF:
	case TOKEN_QUOT:
	case TOKEN_END:
		break;
	case TOKEN_EXIT:
		resolveCallAddresses();
		checkLeftRecursion();
		printLexTab();
		tableReady = 1;
		break;
	case TOKEN_ERROR:
		break;
	default:
		break;
	}
}

//------------------------------------------------------------------------------------------------
// lexicalic scanner
//------------------------------------------------------------------------------------------------
#define CHARBUFFER_LEN   512
char sourceBuffer[CHARBUFFER_LEN];
char* sourcePtr;


int flagStartLineComment = 1;
const char* startLineCommentString = "#";
int flagRestLineComment = 1;
const char* restLineCommendString = "//";
int flagBlockComment = 1;
int flagBlockCommentActive = 0;
const char* startBlockCommandString = "/*";
const char* endBlockCommandString = "*/";
int lineCnt = 0;
int charLen = 0;


char getNext() {
	if (charLen > 0) {
		lastChar = aktChar;
		aktChar = *sourcePtr++;
		nextChar = *sourcePtr;
		charLen--;
		return aktChar;
	} else {
		if (flagEOF) {
			lastChar = aktChar;
			aktChar = EOF;
			nextChar = EOF;
			return aktChar;
		} else {
			sourcePtr = getAktLine();
			lastChar = aktChar;
			aktChar = *sourcePtr++;
			nextChar = *sourcePtr;
			charLen--;
			return aktChar;
		}
	}
}


char getAktChar() {

	if (flagEOF || aktChar == EOF) {
		return EOF;
	}

	aktChar = getNext();

	// overread white chars 
	if (isspace(aktChar)) {
		while (isspace(nextChar)) {
			aktChar = getNext();
		}
		aktChar = ' ';
	}
	if (aktChar < 32 || aktChar > 127) {
		// Error, invalid character
		aktChar = '?';
	}
	return aktChar;
}

char * comment() {
	char* index;
	char* indexEnd;
	int len;

	// filter block Comment
	if (flagBlockCommentActive) {
		if (flagBlockComment) {
			// search for end block comment
			index = strstr(sourceBuffer, endBlockCommandString);
			if (index != NULL) {
				// end Block found, delete chars before end command string
				strcpy_s(sourceBuffer, sizeof(sourceBuffer), index + strlen(endBlockCommandString));
				charLen = (int)strlen(sourceBuffer);
				flagBlockCommentActive = 0;
			} else {
				// end Block not found, delete whole line					
				sourceBuffer[0] = EOS;
				charLen = 0;
			}
		}
	} else {
		if (flagBlockComment && strlen(startBlockCommandString) > 0 && strlen(endBlockCommandString) > 0) {
			// search for start block comment
			index = strstr(sourceBuffer, startBlockCommandString);
			if (index != NULL) {
				// search for end block comment in same line
				indexEnd = strstr(sourceBuffer, endBlockCommandString);
				if (indexEnd != NULL) {
					// copy rest of line after start block
					len = (int)strlen(endBlockCommandString);
					indexEnd += len;
					strcpy_s(index, len, indexEnd);
					charLen = (int)strlen(sourceBuffer);
				} else {
					// comment block aktive over more than one line						
					*index = EOS;
					charLen = (int)strlen(sourceBuffer);
					flagBlockCommentActive = 1;
				}
			}
		}
	}

	// filter start line comments -- NUR wenn das Kommentarzeichen das ERSTE
	// Nicht-Leerzeichen der Zeile ist. (Vorher wurde ein '#' IRGENDWO in der Zeile
	// als Kommentar gewertet -- damit verschwand z.B. die komplette Regelzeile
	// expression = ... ("=" | "#" | ...) ... wegen des gequoteten "#"-Literals!)
	if (flagStartLineComment && strlen(startLineCommentString) > 0) {
		index = sourceBuffer;
		while (*index == ' ' || *index == '\t') {
			index++;
		}
		if (strncmp(index, startLineCommentString, strlen(startLineCommentString)) == 0) {
			// start line comment found, delete whole line
			sourceBuffer[0] = EOS;
			charLen = 0;
		}
	}

	// filter rest line comments
	if (flagRestLineComment && strlen(restLineCommendString) > 0) {
		// search for start line comment
		index = strstr(sourceBuffer, restLineCommendString);
		if (index != NULL) {
			// rest line comment found, delete rest of line
			*index = EOS;
			charLen = (int)strlen(sourceBuffer);
		}
	}
	return sourceBuffer;
}


char* readPtr = NULL;
char* getAktLine() {

	while (charLen == 0 && !feof(fpIn)) {
		// Einlesen bis etwas im buffer oder EOF
		lineCnt++;
		readPtr = fgets(sourceBuffer, CHARBUFFER_LEN, fpIn);
		if (readPtr != NULL) {
			charLen = (int)strlen(sourceBuffer);
			comment();
		} else {
			// Scan Error
			charLen = 0;
			break;
		}
	}
	if (charLen == 0 && feof(fpIn)) {
		// EOF
		printf("EOF\n");
		flagEOF = true;
		sourceBuffer[0] = EOS;
		readPtr = sourceBuffer;
	}
	else if (charLen == 0 && !feof(fpIn)) {
		// Fehler Buffer leer aber kein EOF
		printf("LINE %-3d %s Scanner read Error, \n", lineCnt, sourceBuffer);
		sourceBuffer[0] = EOL;
		readPtr = sourceBuffer;
	} else if (charLen > 0) {
		// Return Buffer
		//printf("LINE %-3d %s\n", lineCnt, sourceBuffer);
		readPtr = sourceBuffer;
	}
	return readPtr;
}
