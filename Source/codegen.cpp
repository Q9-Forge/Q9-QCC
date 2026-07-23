//═════════════════════════════════════════════════════════════════════════════════════════════════
// File:   codegen.cpp                                                                    Ver. 1.50
// Owner:  AF
// Desc.:  AST-Aufbau + Codegenerierung fuer den EBNF-Uebersetzer (docs/ARCHITEKTUR.md).
//         Der AST wird waehrend des normalen Parsens in ebnf.cpp mit aufgebaut (additiv,
//         die Tabellen-Erzeugung bleibt unangetastet). Aus dem AST entstehen zwei
//         strukturgleiche Backtracking-Parser (rekursiver Abstieg, geordnete Auswahl):
//           - <basis>_p.c   C-Zwilling, auf dem Host kompilier- und testbar (Validierung)
//           - <basis>.s68   68k-Assembler (Motorola-Syntax), das eigentliche Ziel
//         Semantik: Alternative/Option/Wiederholung sichern die Eingabeposition und
//         setzen sie bei Misserfolg zurueck -- damit hat der erzeugte Code die
//         "committed"-Grenze der flachen Sprungtabelle NICHT (siehe ARCHITEKTUR.md §3).
//
// Edition History
//─────────┬──────┬─────────────────────────────────────────────────────────────────────────┬──────
// Date    │ Ver. │ Description                                                             │ By
//─────────┼──────┼─────────────────────────────────────────────────────────────────────────┼──────
// 26-07-19│ 1.00 │ Initiale Version: AST-Stack-API, C-Backend, 68k-Backend                 │ CF
// 26-07-19│ 1.10 │ AST-Validierung: nullable Wiederholungen und Namenskollisionen sichern │ CF
// 26-07-19│ 1.20 │ LEXER-Modus (scannerless): ws()/idch()-Helfer in C- und 68k-Backend,   │ CF
//         │      │ TOKEN-Abschluss = lexikalische Regeln ohne Whitespace-Skipping,        │
//         │      │ Wortgrenzen-Check fuer wortartige Literale ("MODULEX" != "MODULE X")   │
// 26-07-19│ 1.30 │ COMMENT BLOCK = "(*" "*)" (Blockkommentare der Objektsprache, nicht    │ CF
//         │      │ geschachtelt; unterminiert laeuft bis Eingabeende) in C und 68k        │
// 26-07-19│ 1.40 │ OS-9/r68-Ausgabeformat: [CODEGEN]-Block (M68K OS9, M68K PSECT),        │ CF
//         │      │ genParser68kOS9 = gleicher Body mit nam/psect/ends + '*'-Kommentaren   │
// 26-07-19│ 1.50 │ COMMENT BLOCK NESTED: geschachtelte Blockkommentare (Oberon/Modula-2), │ CF
//         │      │ Tiefenzaehler in C (lokal) und 68k (d2; nur bei NESTED zerstoert)      │
//─────────┴──────┴─────────────────────────────────────────────────────────────────────────┴──────
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "msvc_compat.h"
#include "codegen.h"

#define AST_MAX_NODES	8192
#define AST_MAX_RULES	256
#define AST_TEXT_LEN	40
#define AST_STACK_MAX	256
#define GEN_NAME_LEN	64

enum AstKind { AST_SEQ, AST_ALT, AST_OPT, AST_REP, AST_TS, AST_RNG, AST_NTS };

typedef struct {
	int kind;
	char text[AST_TEXT_LEN + 1];	// TS: Literaltext (roh, ohne Anfuehrungszeichen); NTS: Regelname
	char lo, hi;					// nur AST_RNG
	int firstChild;					// -1 = keins; Kinder als first-child/next-sibling-Kette
	int nextSib;
} AstNode;

typedef struct {
	char name[AST_TEXT_LEN + 1];
	int root;
} AstRule;

static AstNode nodes[AST_MAX_NODES];
static int nodeCnt = 0;
static AstRule rules[AST_MAX_RULES];
static int ruleCnt = 0;
static int astStack[AST_STACK_MAX];
static int astDepth = 0;
static int astOverflow = 0;

//------------------------------------------------------------------------------------------------
// AST-Aufbau
//------------------------------------------------------------------------------------------------
void astReset() {
	nodeCnt = 0;
	ruleCnt = 0;
	astDepth = 0;
	astOverflow = 0;
}

int astMark() {
	return astDepth;
}

static int newNode(int kind) {
	AstNode* n;
	if (nodeCnt >= AST_MAX_NODES) {
		astOverflow = 1;
		return AST_MAX_NODES - 1;		// letzter Knoten wird Muellhalde; Codegen bricht ab
	}
	n = &nodes[nodeCnt];
	n->kind = kind;
	n->text[0] = '\0';
	n->lo = n->hi = 0;
	n->firstChild = -1;
	n->nextSib = -1;
	return nodeCnt++;
}

static void pushNode(int id) {
	if (astDepth < AST_STACK_MAX) {
		astStack[astDepth++] = id;
	}
	else {
		astOverflow = 1;
	}
}

void astPushTS(const char* text) {
	int id = newNode(AST_TS);
	strncpy_s(nodes[id].text, sizeof(nodes[id].text), text, AST_TEXT_LEN);
	pushNode(id);
}

void astPushRNG(char lo, char hi) {
	int id = newNode(AST_RNG);
	nodes[id].lo = lo;
	nodes[id].hi = hi;
	pushNode(id);
}

void astPushNTS(const char* name) {
	int id = newNode(AST_NTS);
	strncpy_s(nodes[id].text, sizeof(nodes[id].text), name, AST_TEXT_LEN);
	pushNode(id);
}

// Stack[mark..] zu einem Knoten der Art 'kind' zusammenfassen. Genau ein Knoten auf dem
// Bereich: unveraendert lassen (keine unnoetigen Einer-Gruppen). Null Knoten: leere
// Sequenz (Epsilon) erzeugen -- so wird z.B. "empty = ." korrekt abgebildet.
static void groupAs(int kind, int mark) {
	int id, i, prev;

	if (mark < 0 || mark > astDepth) return;		// Fehler-Recovery: nichts kaputt machen
	if (astDepth - mark == 1) return;
	if (astDepth - mark == 0) kind = AST_SEQ;		// leerer Bereich = Epsilon, immer als SEQ
	id = newNode(kind);
	prev = -1;
	for (i = mark; i < astDepth; i++) {
		if (prev < 0) {
			nodes[id].firstChild = astStack[i];
		}
		else {
			nodes[prev].nextSib = astStack[i];
		}
		prev = astStack[i];
	}
	astDepth = mark;
	pushNode(id);
}

void astGroupSeq(int mark) {
	groupAs(AST_SEQ, mark);
}

void astGroupAlt(int mark) {
	groupAs(AST_ALT, mark);
}

static void wrapAs(int kind) {
	int id;
	if (astDepth < 1) return;						// Fehler-Recovery
	id = newNode(kind);
	nodes[id].firstChild = astStack[astDepth - 1];
	astStack[astDepth - 1] = id;
}

void astWrapOpt() {
	wrapAs(AST_OPT);
}

void astWrapRep() {
	wrapAs(AST_REP);
}

void astFinishRule(const char* name) {
	if (ruleCnt >= AST_MAX_RULES) {
		astOverflow = 1;
		return;
	}
	strncpy_s(rules[ruleCnt].name, sizeof(rules[ruleCnt].name), name, AST_TEXT_LEN);
	if (astDepth >= 1) {
		rules[ruleCnt].root = astStack[--astDepth];
	}
	else {
		rules[ruleCnt].root = newNode(AST_SEQ);		// leere Regel (Epsilon)
	}
	ruleCnt++;
	astDepth = 0;		// Regelgrenze: Reste einer Fehler-Recovery verwerfen
}

//------------------------------------------------------------------------------------------------
// Gemeinsame Helfer beider Backends
//------------------------------------------------------------------------------------------------
// Regelnamen duerfen '$' enthalten (EBNF-Ident), C-Bezeichner und manche Assembler nicht:
// '$' wird zu '_'. Kollisionen prueft validateAstForCodegen() vor der Ausgabe.
static void sanitizeName(const char* in, char* out) {
	int i = 0;
	while (in[i] != '\0' && i < GEN_NAME_LEN - 1) {
		out[i] = (in[i] == '$') ? '_' : in[i];
		i++;
	}
	out[i] = '\0';
}

static int labelCnt = 0;

static int newLabel() {
	return labelCnt++;
}

//------------------------------------------------------------------------------------------------
// LEXER-Konfiguration (scannerless, siehe ARCHITEKTUR.md §8)
//------------------------------------------------------------------------------------------------
// Design: kein separater Token-Puffer, sondern drei kleine Regeln direkt im erzeugten
// Parser (nur wenn ein [LEXER]-Block konfiguriert ist):
//  1. Vor jedem Terminal (TS/RNG) und jedem Aufruf einer LEXIKALISCHEN Regel aus
//     syntaktischem Kontext wird ws() gerufen: ueberliest WHITESPACE-Zeichen und
//     Zeilenkommentare der Objektsprache.
//  2. Innerhalb lexikalischer Regeln (transitiver Abschluss der TOKEN-Wurzeln) wird
//     NICHTS uebersprungen -- Token-Zeichen muessen adjazent sein.
//  3. Wortartige Literale ("MODULE", "IF", ...) bekommen im syntaktischen Kontext einen
//     Wortgrenzen-Check: nach dem Literal darf KEIN Identifikator-Zeichen folgen
//     ("MODULEX" ist nicht "MODULE" + "X"). Eine KEYWORDS-Liste ist damit unnoetig;
//     Schluesselwort-vs-ident loest das Backtracking der geordneten Auswahl.
#define LEX_MAX_ROOTS	32
#define LEX_WS_MAX		32
#define LEX_LC_MAX		8
#define LEX_MARKERS_MAX	4		// mehrere gleichzeitige Kommentar-Marker, z.B. "#" UND "//"

static int lexActive = 0;
static char lexWs[LEX_WS_MAX + 1];
static int lexWsLen = 0;
static char lexLineComment[LEX_MARKERS_MAX][LEX_LC_MAX + 1];
static int lexLineCommentLen[LEX_MARKERS_MAX];
static int lexLineCommentCnt = 0;
static char lexBlockOn[LEX_MARKERS_MAX][LEX_LC_MAX + 1];		// Blockkommentar-Anfang, z.B. "(*"
static int lexBlockOnLen[LEX_MARKERS_MAX];
static char lexBlockOff[LEX_MARKERS_MAX][LEX_LC_MAX + 1];		// Blockkommentar-Ende, z.B. "*)"
static int lexBlockOffLen[LEX_MARKERS_MAX];
static int lexBlockNested[LEX_MARKERS_MAX];	// 1 = dieser Marker schachtelt (Oberon/Modula-2)
static int lexBlockCnt = 0;
static char lexRoots[LEX_MAX_ROOTS][AST_TEXT_LEN + 1];
static int lexRootCnt = 0;
static int ruleIsLexical[AST_MAX_RULES];

static int lexAnyBlockNested(void) {
	int j;
	for (j = 0; j < lexBlockCnt; j++) {
		if (lexBlockNested[j]) return 1;
	}
	return 0;
}

static int ruleIndexByName(const char* name) {
	int r;
	for (r = 0; r < ruleCnt; r++) {
		if (strcmp(rules[r].name, name) == 0) return r;
	}
	return -1;
}

// Einen "..."-String ab Position start entnehmen (Escapes \t \r \n \\ \" aufloesen).
// Liefert Laenge oder -1; *nextOut zeigt hinter das schliessende '"'.
static int lexUnquoteAt(const char* start, char* out, int outMax, const char** nextOut) {
	const char* q1 = strchr(start, '"');
	const char* q2;
	int n = 0;

	if (q1 == NULL) return -1;
	q2 = q1 + 1;
	while (*q2 && !(*q2 == '"' && q2[-1] != '\\')) q2++;
	if (*q2 != '"') return -1;
	if (nextOut) *nextOut = q2 + 1;
	q1++;
	while (q1 < q2 && n < outMax - 1) {
		if (*q1 == '\\' && q1 + 1 < q2) {
			q1++;
			switch (*q1) {
			case 't': out[n++] = '\t'; break;
			case 'r': out[n++] = '\r'; break;
			case 'n': out[n++] = '\n'; break;
			default:  out[n++] = *q1;  break;		// \\ und \" und alles andere: wie notiert
			}
		}
		else {
			out[n++] = *q1;
		}
		q1++;
	}
	out[n] = '\0';
	return n;
}

// Inhalt zwischen erstem und letztem '"' einer Konfigurationszeile entnehmen und
// die ueblichen Escapes aufloesen (\t \r \n \\ \").
static int lexUnquote(const char* line, char* out, int outMax) {
	const char* q1 = strchr(line, '"');
	const char* q2 = strrchr(line, '"');
	int n = 0;

	if (q1 == NULL || q2 == NULL || q2 <= q1) return -1;
	q1++;
	while (q1 < q2 && n < outMax - 1) {
		if (*q1 == '\\' && q1 + 1 < q2) {
			q1++;
			switch (*q1) {
			case 't': out[n++] = '\t'; break;
			case 'r': out[n++] = '\r'; break;
			case 'n': out[n++] = '\n'; break;
			default:  out[n++] = *q1;  break;		// \\ und \" und alles andere: wie notiert
			}
		}
		else {
			out[n++] = *q1;
		}
		q1++;
	}
	out[n] = '\0';
	return n;
}

int lexParseConfig(const char* buf) {
	char line[256];
	int li, ok = 1;

	lexActive = 0;
	lexWsLen = 0;
	lexWs[0] = '\0';
	lexLineCommentCnt = 0;
	lexBlockCnt = 0;
	lexRootCnt = 0;

	if (buf == NULL || buf[0] == '\0') {
		return 1;			// kein Lexer konfiguriert -- zeichenbasierter Codegen wie bisher
	}
	while (*buf) {
		li = 0;
		while (*buf && *buf != '\n' && li < (int)sizeof(line) - 1) {
			line[li++] = *buf++;
		}
		line[li] = '\0';
		if (*buf == '\n') buf++;

		if (line[0] == '#' || line[0] == '\0') continue;
		if (strncmp(line, "WHITESPACE", 10) == 0) {
			lexWsLen = lexUnquote(line, lexWs, LEX_WS_MAX + 1);
			if (lexWsLen < 0) {
				printf("LEXER: WHITESPACE-Zeile ohne \"...\" -- ignoriert.\n");
				lexWsLen = 0;
			}
			lexActive = 1;
		}
		else if (strncmp(line, "COMMENT LINE", 12) == 0) {
			// mehrere COMMENT LINE-Zeilen sind erlaubt (z.B. "#" UND "//" gleichzeitig)
			if (lexLineCommentCnt >= LEX_MARKERS_MAX) {
				printf("LEXER: zu viele COMMENT LINE-Marker (max %d) -- ignoriert: %s\n",
					LEX_MARKERS_MAX, line);
			}
			else {
				int n = lexUnquote(line, lexLineComment[lexLineCommentCnt], LEX_LC_MAX + 1);
				if (n < 0) {
					printf("LEXER: COMMENT LINE-Zeile ohne \"...\" -- ignoriert.\n");
				}
				else {
					lexLineCommentLen[lexLineCommentCnt] = n;
					lexLineCommentCnt++;
					lexActive = 1;
				}
			}
		}
		else if (strncmp(line, "COMMENT BLOCK", 13) == 0) {
			// zwei Strings: Anfang und Ende, z.B. COMMENT BLOCK = "(*" "*)"
			// optional: COMMENT BLOCK NESTED = ... -> Kommentare schachteln
			// mehrere COMMENT BLOCK-Zeilen sind erlaubt (z.B. "/* */" UND "(* *)" gleichzeitig)
			if (lexBlockCnt >= LEX_MARKERS_MAX) {
				printf("LEXER: zu viele COMMENT BLOCK-Marker (max %d) -- ignoriert: %s\n",
					LEX_MARKERS_MAX, line);
			}
			else {
				const char* rest = NULL;
				const char* nst = strstr(line, "NESTED");
				const char* q0 = strchr(line, '"');
				int onLen = lexUnquoteAt(line, lexBlockOn[lexBlockCnt], LEX_LC_MAX + 1, &rest);
				int offLen = (onLen > 0 && rest != NULL)
					? lexUnquoteAt(rest, lexBlockOff[lexBlockCnt], LEX_LC_MAX + 1, NULL) : -1;
				if (onLen <= 0 || offLen <= 0) {
					printf("LEXER: COMMENT BLOCK braucht ZWEI \"...\"-Strings (Anfang Ende) -- ignoriert.\n");
				}
				else {
					lexBlockOnLen[lexBlockCnt] = onLen;
					lexBlockOffLen[lexBlockCnt] = offLen;
					lexBlockNested[lexBlockCnt] = (nst != NULL && q0 != NULL && nst < q0);
					lexBlockCnt++;
					lexActive = 1;
				}
			}
		}
		else if (strncmp(line, "TOKEN", 5) == 0) {
			// akzeptiert "TOKEN <regel>" und "TOKEN <TYP> = <regel>" -- massgeblich
			// ist das LETZTE Wort der Zeile (der Regelname)
			const char* pWord = line + strlen(line);
			while (pWord > line && (pWord[-1] == ' ' || pWord[-1] == '\t')) pWord--;
			{
				const char* end = pWord;
				while (pWord > line && pWord[-1] != ' ' && pWord[-1] != '\t' && pWord[-1] != '=') pWord--;
				if (end > pWord && lexRootCnt < LEX_MAX_ROOTS) {
					int n = (int)(end - pWord);
					if (n > AST_TEXT_LEN) n = AST_TEXT_LEN;
					memcpy(lexRoots[lexRootCnt], pWord, n);
					lexRoots[lexRootCnt][n] = '\0';
					lexRootCnt++;
					lexActive = 1;
				}
			}
		}
		else {
			printf("LEXER: unbekannte Konfigurationszeile ignoriert: %s\n", line);
		}
	}

	// Standard-Whitespace, falls TOKEN/COMMENT konfiguriert wurden, aber WHITESPACE fehlt
	if (lexActive && lexWsLen == 0) {
		strcpy_s(lexWs, sizeof(lexWs), " \t\r\n");
		lexWsLen = 4;
	}
	return ok;
}

// Lexikalische Regeln = transitiver Abschluss der TOKEN-Wurzeln ueber NTS-Referenzen im AST
static void markLexicalNode(int id) {
	AstNode* n = &nodes[id];
	int child, idx;

	if (n->kind == AST_NTS) {
		idx = ruleIndexByName(n->text);
		if (idx >= 0 && !ruleIsLexical[idx]) {
			ruleIsLexical[idx] = 1;
			markLexicalNode(rules[idx].root);
		}
	}
	for (child = n->firstChild; child >= 0; child = nodes[child].nextSib) {
		markLexicalNode(child);
	}
}

static int computeLexicalSet() {
	int r, idx, ok = 1;

	for (r = 0; r < ruleCnt; r++) ruleIsLexical[r] = 0;
	if (!lexActive) return 1;
	for (r = 0; r < lexRootCnt; r++) {
		idx = ruleIndexByName(lexRoots[r]);
		if (idx < 0) {
			printf("LEXER: TOKEN-Regel '%s' existiert nicht in der Grammatik.\n", lexRoots[r]);
			ok = 0;
			continue;
		}
		if (!ruleIsLexical[idx]) {
			ruleIsLexical[idx] = 1;
			markLexicalNode(rules[idx].root);
		}
	}
	return ok;
}

//------------------------------------------------------------------------------------------------
// CODEGEN-Konfiguration ([CODEGEN]-Block der Arbeitsdatei)
//------------------------------------------------------------------------------------------------
static int cgenOS9 = 0;
static char cgenPsect[GEN_NAME_LEN];
static char cgenStart[GEN_NAME_LEN];

int cgenWantOS9() {
	return cgenOS9;
}

const char* cgenStartRule() {
	return cgenStart;
}

int cgenParseConfig(const char* buf) {
	char line[256];
	int li;

	cgenOS9 = 0;
	cgenPsect[0] = '\0';
	cgenStart[0] = '\0';

	if (buf == NULL || buf[0] == '\0') {
		return 1;
	}
	while (*buf) {
		li = 0;
		while (*buf && *buf != '\n' && li < (int)sizeof(line) - 1) {
			line[li++] = *buf++;
		}
		line[li] = '\0';
		if (*buf == '\n') buf++;

		if (line[0] == '#' || line[0] == '\0') continue;
		if (strncmp(line, "M68K PSECT", 10) == 0) {
			// letztes Wort der Zeile = psect-Name
			const char* pEnd = line + strlen(line);
			while (pEnd > line && (pEnd[-1] == ' ' || pEnd[-1] == '\t')) pEnd--;
			{
				const char* pStart = pEnd;
				while (pStart > line && pStart[-1] != ' ' && pStart[-1] != '\t' && pStart[-1] != '=') pStart--;
				if (pEnd > pStart) {
					int n = (int)(pEnd - pStart);
					if (n > GEN_NAME_LEN - 1) n = GEN_NAME_LEN - 1;
					memcpy(cgenPsect, pStart, n);
					cgenPsect[n] = '\0';
				}
			}
		}
		else if (strncmp(line, "M68K OS9", 8) == 0) {
			cgenOS9 = 1;
		}
		else if (strncmp(line, "START", 5) == 0) {
			// letztes Wort der Zeile = Name der Startregel
			const char* pEnd = line + strlen(line);
			while (pEnd > line && (pEnd[-1] == ' ' || pEnd[-1] == '\t')) pEnd--;
			{
				const char* pStart = pEnd;
				while (pStart > line && pStart[-1] != ' ' && pStart[-1] != '\t' && pStart[-1] != '=') pStart--;
				if (pEnd > pStart) {
					int n = (int)(pEnd - pStart);
					if (n > GEN_NAME_LEN - 1) n = GEN_NAME_LEN - 1;
					memcpy(cgenStart, pStart, n);
					cgenStart[n] = '\0';
				}
			}
		}
		else {
			printf("CODEGEN: unbekannte Konfigurationszeile ignoriert: %s\n", line);
		}
	}
	return 1;
}

//------------------------------------------------------------------------------------------------
// ACTIONS-Konfiguration (im [NUTZER-CODE]-Block der Arbeitsdatei, siehe ARCHITEKTUR.md §9).
//------------------------------------------------------------------------------------------------
// Zeilenformate:
//   ACTION AFTER <regel> CALL <name>     Aufruf von <name> direkt nach Erfolg von <regel>,
//                                        in BEIDEN Backends (falls die jeweilige ROUTINE
//                                        existiert -- fehlt sie, wird der Aufruf mit einer
//                                        Warnung weggelassen, kein harter Fehler).
//   ROUTINE C <name> ... END            Roh-C-Funktion, wortwoertlich vor die erzeugten
//                                        p_<regel>-Funktionen kopiert. Signatur MUSS
//                                        "void <name>(const char* start, const char* end)"
//                                        sein (start/end = Anfang/Ende des erkannten Textes).
//   ROUTINE M68K <name> ... END         Rohe 68k-Subroutine, wortwoertlich ans Ende des
//                                        erzeugten .s68 angehaengt (Label <name>: + rts).
//                                        Aufruf ist "bsr <name>" mit a0 = Ende des erkannten
//                                        Textes (wie beim C-Backend "end"); a0 MUSS
//                                        unveraendert zurueckgegeben werden. d0/d1/d2 frei.
//                                        (Der Regelanfang steht -- anders als bei C -- NICHT
//                                        als eigenes Register bereit, siehe ARCHITEKTUR.md §9.)
// Aktionen sind eine Grenzflaeche zu Nutzer-Code, keine Grammatik -- Fehler hier (unbekannte
// Regel, fehlende ROUTINE) brechen die Codegenerierung nicht ab, sie werden nur gewarnt und
// die betroffene Aktion faellt weg.
#define ACTION_ROUTINE_MAX  256
#define ACTION_ROUTINE_LEN 65536

static char ruleActionCall[AST_MAX_RULES][GEN_NAME_LEN];

typedef struct {
	char name[GEN_NAME_LEN];
	char text[ACTION_ROUTINE_LEN];
} ActionRoutine;

static ActionRoutine routinesC[ACTION_ROUTINE_MAX];
static int routinesCCnt = 0;
static ActionRoutine routines68k[ACTION_ROUTINE_MAX];
static int routines68kCnt = 0;

static const char* routineTextC(const char* name) {
	int i;
	for (i = 0; i < routinesCCnt; i++) {
		if (strcmp(routinesC[i].name, name) == 0) return routinesC[i].text;
	}
	return NULL;
}

static const char* routineText68k(const char* name) {
	int i;
	for (i = 0; i < routines68kCnt; i++) {
		if (strcmp(routines68k[i].name, name) == 0) return routines68k[i].text;
	}
	return NULL;
}

// letztes Wort einer Zeile (durch Leerraum getrennt) in out kopieren, max outMax-1 Zeichen
static void lastWord(const char* line, char* out, int outMax) {
	const char* pEnd = line + strlen(line);
	const char* pStart;
	int n;
	while (pEnd > line && (pEnd[-1] == ' ' || pEnd[-1] == '\t')) pEnd--;
	pStart = pEnd;
	while (pStart > line && pStart[-1] != ' ' && pStart[-1] != '\t') pStart--;
	n = (int)(pEnd - pStart);
	if (n > outMax - 1) n = outMax - 1;
	memcpy(out, pStart, n);
	out[n] = '\0';
}

int actionsParseConfig(const char* buf) {
	char line[ACTION_ROUTINE_LEN];
	int li, r;
	int collecting = 0;			// 0=nichts, 1=ROUTINE C, 2=ROUTINE M68K
	char collectName[GEN_NAME_LEN];
	char collectText[ACTION_ROUTINE_LEN];
	int collectLen;

	for (r = 0; r < AST_MAX_RULES; r++) ruleActionCall[r][0] = '\0';
	routinesCCnt = 0;
	routines68kCnt = 0;

	if (buf == NULL || buf[0] == '\0') {
		return 1;
	}
	while (*buf) {
		li = 0;
		while (*buf && *buf != '\n' && li < (int)sizeof(line) - 1) {
			line[li++] = *buf++;
		}
		line[li] = '\0';
		if (*buf == '\n') buf++;

		if (collecting) {
			if (strcmp(line, "END") == 0) {
				collectText[collectLen] = '\0';
				if (collecting == 1 && routinesCCnt < ACTION_ROUTINE_MAX) {
					strcpy_s(routinesC[routinesCCnt].name, sizeof(routinesC[routinesCCnt].name), collectName);
					strcpy_s(routinesC[routinesCCnt].text, sizeof(routinesC[routinesCCnt].text), collectText);
					routinesCCnt++;
				}
				else if (collecting == 2 && routines68kCnt < ACTION_ROUTINE_MAX) {
					strcpy_s(routines68k[routines68kCnt].name, sizeof(routines68k[routines68kCnt].name), collectName);
					strcpy_s(routines68k[routines68kCnt].text, sizeof(routines68k[routines68kCnt].text), collectText);
					routines68kCnt++;
				}
				collecting = 0;
				continue;
			}
			{
				int n = (int)strlen(line);
				if (collectLen + n + 2 < ACTION_ROUTINE_LEN) {
					memcpy(collectText + collectLen, line, n);
					collectLen += n;
					collectText[collectLen++] = '\n';
				}
			}
			continue;
		}

		if (line[0] == '#' || line[0] == '\0') continue;
		if (strncmp(line, "ACTION AFTER", 12) == 0) {
			char ruleName[AST_TEXT_LEN + 1];
			char callName[GEN_NAME_LEN];
			const char* callPos = strstr(line, "CALL");
			int idx;
			if (callPos == NULL) {
				printf("ACTIONS: 'ACTION AFTER ... CALL <name>' erwartet, ignoriert: %s\n", line);
				continue;
			}
			{
				const char* p = line + 12;
				while (*p == ' ' || *p == '\t') p++;
				int n = 0;
				while (p < callPos && p[n] != ' ' && p[n] != '\t' && n < AST_TEXT_LEN) n++;
				memcpy(ruleName, p, n);
				ruleName[n] = '\0';
			}
			lastWord(line, callName, GEN_NAME_LEN);
			idx = ruleIndexByName(ruleName);
			if (idx < 0) {
				printf("ACTIONS: unbekannte Regel '%s' -- ACTION ignoriert.\n", ruleName);
				continue;
			}
			strcpy_s(ruleActionCall[idx], sizeof(ruleActionCall[idx]), callName);
		}
		else if (strncmp(line, "ROUTINE C", 9) == 0) {
			lastWord(line, collectName, GEN_NAME_LEN);
			collecting = 1;
			collectLen = 0;
		}
		else if (strncmp(line, "ROUTINE M68K", 12) == 0) {
			lastWord(line, collectName, GEN_NAME_LEN);
			collecting = 2;
			collectLen = 0;
		}
		else {
			printf("ACTIONS: unbekannte Konfigurationszeile ignoriert: %s\n", line);
		}
	}
	// jede ACTION braucht mindestens EINE der beiden ROUTINEn, sonst verpufft sie
	// vollstaendig -- das ist erlaubt (z.B. waehrend nur ein Backend entwickelt wird),
	// aber eine klare Warnung ist besser als stilles Weglassen.
	for (r = 0; r < AST_MAX_RULES; r++) {
		if (ruleActionCall[r][0] == '\0') continue;
		if (routineTextC(ruleActionCall[r]) == NULL && routineText68k(ruleActionCall[r]) == NULL) {
			printf("ACTIONS: Aktion '%s' hat weder ROUTINE C noch ROUTINE M68K -- wird nirgends aufgerufen.\n",
				ruleActionCall[r]);
		}
	}
	return 1;
}

// wortartiges Literal: beginnt wie ein Bezeichner und besteht nur aus Bezeichner-Zeichen
// -> bekommt im syntaktischen Kontext einen Wortgrenzen-Check
static int isWordLiteral(const char* s) {
	int i;
	if (!(isalpha((unsigned char)s[0]) || s[0] == '_' || s[0] == '$')) return 0;
	for (i = 0; s[i]; i++) {
		if (!(isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '$')) return 0;
	}
	return 1;
}

//------------------------------------------------------------------------------------------------
// AST-Validierung vor der Ausgabe
//------------------------------------------------------------------------------------------------
// Ein rekursiver Abstieg darf eine Wiederholung nur dann als einfache greedy-Schleife
// erzeugen, wenn ihr Rumpf mindestens ein Zeichen konsumiert. Andernfalls waere die
// erfolgreiche Iteration ohne Fortschritt eine Endlosschleife -- sowohl im C- als auch
// im 68k-Backend. "nullable" bedeutet hier: kann der Knoten erfolgreich sein, ohne
// Eingabe zu verbrauchen? Die Berechnung ist rein strukturell; NTS gelten konservativ
// als nicht-nullable (eine nullable NTS in einer Wiederholung wird unten separat durch
// die Regel-Fixpunktanalyse erkannt).
static int nodeNullable(int id, const int* ruleNullable) {
	AstNode* n = &nodes[id];
	int child;

	switch (n->kind) {
	case AST_SEQ:
		for (child = n->firstChild; child >= 0; child = nodes[child].nextSib) {
			if (!nodeNullable(child, ruleNullable)) return 0;
		}
		return 1;
	case AST_ALT:
		for (child = n->firstChild; child >= 0; child = nodes[child].nextSib) {
			if (nodeNullable(child, ruleNullable)) return 1;
		}
		return 0;
	case AST_OPT:
	case AST_REP:
		return 1;
	case AST_TS:
		return n->text[0] == '\0';
	case AST_RNG:
		return 0;
	case AST_NTS:
		for (child = 0; child < ruleCnt; child++) {
			if (strcmp(rules[child].name, n->text) == 0) return ruleNullable[child];
		}
		return 0; // undefinierte Regeln werden bereits von ebnf.cpp gemeldet
	}
	return 0;
}

static int validateRepeatProgress(int id, const int* ruleNullable) {
	AstNode* n = &nodes[id];
	int child;
	if (n->kind == AST_REP && nodeNullable(n->firstChild, ruleNullable)) {
		printf("CODEGEN: Wiederholung hat einen leeren Rumpf -- Endlosschleife verhindert.\n");
		return 0;
	}
	for (child = n->firstChild; child >= 0; child = nodes[child].nextSib) {
		if (!validateRepeatProgress(child, ruleNullable)) return 0;
	}
	return 1;
}

static int validateAstForCodegen() {
	int ruleNullable[AST_MAX_RULES] = { 0 };
	int changed, r, s;
	char a[GEN_NAME_LEN], b[GEN_NAME_LEN];

	if (ruleCnt == 0 || astOverflow) {
		printf("CODEGEN: kein AST vorhanden (leer oder Ueberlauf).\n");
		return 0;
	}

	// Die Nullbarkeit gegenseitig rekursiver Regeln braucht einen kleinen Fixpunkt.
	do {
		changed = 0;
		for (r = 0; r < ruleCnt; r++) {
			if (!ruleNullable[r] && nodeNullable(rules[r].root, ruleNullable)) {
				ruleNullable[r] = 1;
				changed = 1;
			}
		}
	} while (changed);

	for (r = 0; r < ruleCnt; r++) {
		if (!validateRepeatProgress(rules[r].root, ruleNullable)) {
			printf("         betroffen: Regel '%s'.\n", rules[r].name);
			return 0;
		}
	}

	// '$' wird fuer beide Backends zu '_' normalisiert. Eine Kollision wuerde sonst
	// doppelte C-Funktionen bzw. 68k-Labels erzeugen und ist daher ein klarer Fehler.
	for (r = 0; r < ruleCnt; r++) {
		sanitizeName(rules[r].name, a);
		for (s = r + 1; s < ruleCnt; s++) {
			sanitizeName(rules[s].name, b);
			if (strcmp(a, b) == 0) {
				printf("CODEGEN: Regeln '%s' und '%s' kollidieren als '%s'.\n",
					rules[r].name, rules[s].name, a);
				return 0;
			}
		}
	}
	return 1;
}

// Druckbare Darstellung eines Zeichens fuer Kommentare
static void charComment(char c, char* out, int outMax) {
	if (isprint((unsigned char)c)) {
		snprintf(out, outMax, "'%c'", c);
	}
	else {
		snprintf(out, outMax, "0x%02X", (unsigned char)c);
	}
}

//------------------------------------------------------------------------------------------------
// C-Backend: semantischer Zwilling (auf dem Host testbar)
//------------------------------------------------------------------------------------------------
// Erzeugte Struktur: static const char* p; pro Regel eine Funktion p_<name>() -> 1/0,
// bei Misserfolg ist p unveraendert (Ruecksetzung an Regel-/Auswahl-/Options-/
// Wiederholungs-Grenzen ueber lokalen save-Stack sv[]). main() nimmt die Eingabe als
// argv[1], druckt OK/FAIL und liefert exit 0/1 -- damit kann runtests.sh die
// TESTS-Bloecke der Arbeitsdatei direkt gegen den ERZEUGTEN Parser laufen lassen.
static void emitCString(FILE* fp, const char* s) {
	fputc('"', fp);
	while (*s) {
		if (*s == '\\' || *s == '"') {
			fputc('\\', fp);
			fputc(*s, fp);
		}
		else if (isprint((unsigned char)*s)) {
			fputc(*s, fp);
		}
		else {
			fprintf(fp, "\\x%02x", (unsigned char)*s);
		}
		s++;
	}
	fputc('"', fp);
}

// Syntaktische Operator-Literale folgen bei aktivem Lexer der ueblichen
// Longest-Match-Regel. Ohne diese Regel waere z.B. "a && &b" scannerlos auch
// als "a & &b" lesbar, sobald die Sprache den Adressoperator unterstuetzt.
static void emitLongerLiteralRejectC(FILE* fp, const char* text, int failLabel) {
	int i, j; size_t len = strlen(text);
	if (!lexActive || isWordLiteral(text)) return;
	for (i = 0; i < nodeCnt; i++) {
		const char* longer;
		if (nodes[i].kind != AST_TS) continue;
		longer = nodes[i].text;
		if (strlen(longer) <= len || strncmp(longer, text, len) != 0) continue;
		for (j = 0; j < i; j++) if (nodes[j].kind == AST_TS && strcmp(nodes[j].text, longer) == 0) break;
		if (j < i) continue;
		fprintf(fp, "\tif (strncmp(p, "); emitCString(fp, longer);
		fprintf(fp, ", %d) == 0) goto L%d;\t/* Longest-Match */\n", (int)strlen(longer), failLabel);
	}
}

static void genNodeC(FILE* fp, int id, int failLabel, int lexical) {
	AstNode* n = &nodes[id];
	int child, l1, l2, lok;

	switch (n->kind) {
	case AST_TS: {
		size_t len = strlen(n->text);
		if (lexActive && !lexical) {
			fprintf(fp, "\tws();\n");
		}
		fprintf(fp, "\tif (strncmp(p, ");
		emitCString(fp, n->text);
		fprintf(fp, ", %d) != 0) goto L%d;\n", (int)len, failLabel);
		if (lexActive && !lexical) emitLongerLiteralRejectC(fp, n->text, failLabel);
		if (lexActive && !lexical && isWordLiteral(n->text)) {
			// Wortgrenze: "MODULEX" darf nicht als "MODULE" + Rest gelten
			fprintf(fp, "\tif (idch((unsigned char)p[%d])) goto L%d;\n", (int)len, failLabel);
		}
		fprintf(fp, "\tp += %d;\n", (int)len);
		break;
	}
	case AST_RNG:
		if (lexActive && !lexical) {
			fprintf(fp, "\tws();\n");
		}
		fprintf(fp, "\tif ((unsigned char)*p < 0x%02X || (unsigned char)*p > 0x%02X) goto L%d;\n",
			(unsigned char)n->lo, (unsigned char)n->hi, failLabel);
		fprintf(fp, "\tp++;\n");
		break;
	case AST_NTS: {
		char cName[GEN_NAME_LEN];
		int idx = ruleIndexByName(n->text);
		sanitizeName(n->text, cName);
		// vor dem Einstieg in ein TOKEN (lexikalische Regel) aus syntaktischem Kontext
		// Whitespace ueberlesen; syntaktische Unterregeln erledigen das selbst
		if (lexActive && !lexical && idx >= 0 && ruleIsLexical[idx]) {
			fprintf(fp, "\tws();\n");
		}
		fprintf(fp, "\tif (!p_%s()) goto L%d;\n", cName, failLabel);
		break;
	}
	case AST_SEQ:
		for (child = n->firstChild; child >= 0; child = nodes[child].nextSib) {
			genNodeC(fp, child, failLabel, lexical);
		}
		break;
	case AST_ALT: {
		lok = newLabel();
		fprintf(fp, "\tsv[sp] = p; svLog[sp] = actionLogLen; sp++;\n");
		child = n->firstChild;
		while (child >= 0) {
			l1 = newLabel();
			genNodeC(fp, child, l1, lexical);
			fprintf(fp, "\tgoto L%d;\n", lok);
			if (nodes[child].nextSib >= 0) {
				// naechste Alternative versuchen: Position UND Aktions-Log auf den
				// Stand vor dieser (verworfenen) Alternative zuruecksetzen -- sonst
				// bleiben Aktionen einer abgebrochenen Alternative im Log haengen
				// (siehe ARCHITEKTUR.md §9.4/Test/actionrollback).
				fprintf(fp, "L%d:\tp = sv[sp-1]; actionLogLen = svLog[sp-1];\n", l1);
			}
			else {
				// letzte Alternative gescheitert -> Auswahl gescheitert
				fprintf(fp, "L%d:\tsp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L%d;\n",
					l1, failLabel);
			}
			child = nodes[child].nextSib;
		}
		fprintf(fp, "L%d:\tsp--;\n", lok);
		break;
	}
	case AST_OPT:
		l1 = newLabel();
		l2 = newLabel();
		fprintf(fp, "\tsv[sp] = p; svLog[sp] = actionLogLen; sp++;\n");
		genNodeC(fp, n->firstChild, l1, lexical);
		fprintf(fp, "\tsp--; goto L%d;\n", l2);
		fprintf(fp, "L%d:\tsp--; p = sv[sp]; actionLogLen = svLog[sp];\n", l1);
		fprintf(fp, "L%d:\t;\n", l2);
		break;
	case AST_REP:
		l1 = newLabel();
		l2 = newLabel();
		fprintf(fp, "L%d:\tsv[sp] = p; svLog[sp] = actionLogLen; sp++;\n", l1);
		genNodeC(fp, n->firstChild, l2, lexical);
		fprintf(fp, "\tsp--; goto L%d;\n", l1);
		fprintf(fp, "L%d:\tsp--; p = sv[sp]; actionLogLen = svLog[sp];\n", l2);
		break;
	}
}

int genParserC(const char* path) {
	FILE* fp;
	int r, fail;
	char cName[GEN_NAME_LEN];

	if (!validateAstForCodegen()) {
		printf("         %s wird nicht erzeugt.\n", path);
		return 0;
	}
	if (!computeLexicalSet()) {
		printf("         %s wird nicht erzeugt.\n", path);
		return 0;
	}
	if (fopen_s(&fp, path, "w") != 0) {
		printf("CODEGEN: kann '%s' nicht schreiben\n", path);
		return 0;
	}
	labelCnt = 0;

	fprintf(fp, "/* Automatisch erzeugt von ebnf -- NICHT von Hand aendern.\n");
	fprintf(fp, " * Backtracking-Parser (rekursiver Abstieg, geordnete Auswahl).\n");
	fprintf(fp, " * Aufruf: %s \"<eingabe>\"  -> druckt OK/FAIL, exit 0/1.\n", "parser");
	if (lexActive) {
		fprintf(fp, " * LEXER aktiv: Whitespace/Kommentare werden zwischen Symbolen ueberlesen,\n");
		fprintf(fp, " * lexikalische Regeln (TOKEN-Abschluss) matchen adjazente Zeichen.\n");
	}
	fprintf(fp, " * Startregel: %s\n */\n", rules[0].name);
	fprintf(fp, "#include <stdio.h>\n#include <string.h>\n\n");
	fprintf(fp, "static const char* p;\n");
	fprintf(fp, "static int actionLogLen = 0;\t/* siehe ACTION-Routinen weiter unten */\n\n");
	if (lexActive) {
		fprintf(fp, "static int idch(int c) {\n");
		fprintf(fp, "\treturn (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')\n");
		fprintf(fp, "\t    || (c >= 'a' && c <= 'z') || c == '_' || c == '$';\n}\n\n");
		fprintf(fp, "static const char wsSet[] = ");
		emitCString(fp, lexWs);
		fprintf(fp, ";\n");
		fprintf(fp, "static void ws(void) {\n\tfor (;;) {\n");
		fprintf(fp, "\t\tif (*p && strchr(wsSet, *p)) { p++; continue; }\n");
		for (r = 0; r < lexLineCommentCnt; r++) {
			fprintf(fp, "\t\tif (strncmp(p, ");
			emitCString(fp, lexLineComment[r]);
			fprintf(fp, ", %d) == 0) { while (*p && *p != '\\n') p++; continue; }\n", lexLineCommentLen[r]);
		}
		for (r = 0; r < lexBlockCnt; r++) {
			if (lexBlockNested[r]) {
				// geschachtelt (Oberon/Modula-2): Tiefenzaehler; Ende-Sequenz VOR Start-Sequenz
				// pruefen (wichtig bei ueberlappenden Zeichen wie "(*" / "*)").
				// Ein unterminierter Kommentar laeuft bis zum Eingabeende.
				fprintf(fp, "\t\tif (strncmp(p, ");
				emitCString(fp, lexBlockOn[r]);
				fprintf(fp, ", %d) == 0) {\n", lexBlockOnLen[r]);
				fprintf(fp, "\t\t\tint tiefe = 1; p += %d;\n", lexBlockOnLen[r]);
				fprintf(fp, "\t\t\twhile (*p && tiefe > 0) {\n");
				fprintf(fp, "\t\t\t\tif (strncmp(p, ");
				emitCString(fp, lexBlockOff[r]);
				fprintf(fp, ", %d) == 0) { tiefe--; p += %d; }\n", lexBlockOffLen[r], lexBlockOffLen[r]);
				fprintf(fp, "\t\t\t\telse if (strncmp(p, ");
				emitCString(fp, lexBlockOn[r]);
				fprintf(fp, ", %d) == 0) { tiefe++; p += %d; }\n", lexBlockOnLen[r], lexBlockOnLen[r]);
				fprintf(fp, "\t\t\t\telse p++;\n");
				fprintf(fp, "\t\t\t}\n\t\t\tcontinue;\n\t\t}\n");
			}
			else {
				// nicht geschachtelt; ein unterminierter Kommentar laeuft bis zum Eingabeende
				fprintf(fp, "\t\tif (strncmp(p, ");
				emitCString(fp, lexBlockOn[r]);
				fprintf(fp, ", %d) == 0) { p += %d; while (*p && strncmp(p, ",
					lexBlockOnLen[r], lexBlockOnLen[r]);
				emitCString(fp, lexBlockOff[r]);
				fprintf(fp, ", %d) != 0) p++; if (*p) p += %d; continue; }\n",
					lexBlockOffLen[r], lexBlockOffLen[r]);
			}
		}
		fprintf(fp, "\t\treturn;\n\t}\n}\n\n");
	}
	if (routinesCCnt > 0) {
		// Aktionen feuern NICHT sofort beim Regelerfolg, sondern werden nur PROTOKOLLIERT
		// (actionLogPush) und erst am Ende bei ENDGUELTIGEM Gesamterfolg abgespielt
		// (actionLogReplay). Grund: ein Backtracking-Parser kann eine bereits erfolgreich
		// gematchte Regel spaeter DOCH verwerfen, wenn eine umschliessende Alternative
		// zurueckspringt -- Positions-Rollback existiert (sv[]), fuer Aktions-Nebeneffekte
		// bisher nicht. svLog[] laeuft parallel zu sv[] und rollt actionLogLen beim
		// Zuruecksetzen einer Alternative/Option/Wiederholung mit zurueck (siehe genNodeC
		// AST_ALT/OPT/REP) sowie beim Fehlschlag einer ganzen Regel (entryLog).
		// Siehe ARCHITEKTUR.md §9.4, Test/actionrollback fuer die Reproduktion ohne diesen
		// Mechanismus (ROUTINE C note_tag feuerte dort bei "T2" faelschlich 2x statt 1x).
		fprintf(fp, "#define ACTION_LOG_MAX 4096\n");
		fprintf(fp, "typedef void (*ActionFn)(const char*, const char*);\n");
		fprintf(fp, "typedef struct { ActionFn fn; const char* start; const char* end; } ActionLogEntry;\n");
		fprintf(fp, "static ActionLogEntry actionLog[ACTION_LOG_MAX];\n");
		fprintf(fp, "static void actionLogPush(ActionFn fn, const char* start, const char* end) {\n");
		fprintf(fp, "\tif (actionLogLen < ACTION_LOG_MAX) {\n");
		fprintf(fp, "\t\tactionLog[actionLogLen].fn = fn;\n");
		fprintf(fp, "\t\tactionLog[actionLogLen].start = start;\n");
		fprintf(fp, "\t\tactionLog[actionLogLen].end = end;\n");
		fprintf(fp, "\t\tactionLogLen++;\n\t}\n}\n");
		fprintf(fp, "static void actionLogReplay(void) {\n");
		fprintf(fp, "\tint i;\n\tfor (i = 0; i < actionLogLen; i++) actionLog[i].fn(actionLog[i].start, actionLog[i].end);\n}\n\n");
		fprintf(fp, "/* ACTION-Routinen aus [NUTZER-CODE] (roh uebernommen) */\n");
		for (r = 0; r < routinesCCnt; r++) {
			fprintf(fp, "%s\n", routinesC[r].text);
		}
	}
	for (r = 0; r < ruleCnt; r++) {
		sanitizeName(rules[r].name, cName);
		fprintf(fp, "static int p_%s(void);\n", cName);
	}
	fprintf(fp, "\n");
	for (r = 0; r < ruleCnt; r++) {
		sanitizeName(rules[r].name, cName);
		fail = newLabel();
		fprintf(fp, "/* %s%s */\n", rules[r].name, ruleIsLexical[r] ? " (lexikalisch)" : "");
		fprintf(fp, "static int p_%s(void) {\n", cName);
		fprintf(fp, "\tconst char* sv[64]; int svLog[64]; int sp;\n");
		if (lexActive && !ruleIsLexical[r]) {
			// entry MUSS erst NACH einem eventuellen fuehrenden ws() erfasst werden --
			// sonst landet Whitespace/Kommentar vor dem eigentlichen Regelinhalt im
			// start/end-Bereich, den eine ACTION dieser Regel bekommt (ws() ist
			// idempotent, ein zusaetzlicher Aufruf hier ist fuer die Parser-Semantik
			// ein No-Op, korrigiert aber start/end fuer alle ACTION-Aufrufe).
			fprintf(fp, "\tws();\n");
		}
		fprintf(fp, "\tsp = 0;\n\tconst char* entry = p;\n");
		fprintf(fp, "\tint entryLog = actionLogLen;\n");
		fprintf(fp, "\t(void)sv; (void)svLog; (void)sp; (void)entryLog;\n");
		genNodeC(fp, rules[r].root, fail, ruleIsLexical[r]);
		if (ruleActionCall[r][0] != '\0' && routineTextC(ruleActionCall[r]) != NULL) {
			fprintf(fp, "\tactionLogPush(%s, entry, p);\t/* ACTION AFTER %s */\n",
				ruleActionCall[r], rules[r].name);
		}
		fprintf(fp, "\treturn 1;\n");
		fprintf(fp, "L%d:\tp = entry; actionLogLen = entryLog;\n", fail);
		fprintf(fp, "\treturn 0;\n}\n\n");
	}
	sanitizeName(rules[0].name, cName);
	fprintf(fp, "int main(int argc, char* argv[]) {\n");
	fprintf(fp, "\tif (argc < 2) { fprintf(stderr, \"usage: %%s <eingabe>\\n\", argv[0]); return 2; }\n");
	fprintf(fp, "\tp = argv[1];\n");
	// actionLogReplay() erst NACH bestaetigtem Gesamterfolg (voller Input erkannt) --
	// nur dann steht fest, dass keine der protokollierten Aktionen zu einem inzwischen
	// verworfenen Backtracking-Pfad gehoert (siehe Kommentar bei actionLogPush oben).
	if (lexActive) {
		fprintf(fp, "\tif (p_%s()) { ws(); if (*p == '\\0') {%s printf(\"OK\\n\"); return 0; } }\n",
			cName, routinesCCnt > 0 ? " actionLogReplay();" : "");
	}
	else {
		fprintf(fp, "\tif (p_%s() && *p == '\\0') {%s printf(\"OK\\n\"); return 0; }\n",
			cName, routinesCCnt > 0 ? " actionLogReplay();" : "");
	}
	fprintf(fp, "\tprintf(\"FAIL\\n\");\n\treturn 1;\n}\n");
	fclose(fp);
	return 1;
}

//------------------------------------------------------------------------------------------------
// 68k-Backend (Motorola-Syntax, vasm-kompatibel; keine Assembler-Direktiven noetig)
//------------------------------------------------------------------------------------------------
// Registerkonvention (siehe ARCHITEKTUR.md §4/§5):
//   a0   = Eingabezeiger (NUL-terminiert), laeuft bei Erfolg mit
//   d0.b = 1 Erfolg / 0 Misserfolg (bei Misserfolg: a0 unveraendert)
//   d1   = Scratch fuer Bereichsvergleiche; Ruecksetzpunkte liegen auf dem Stack -(a7)
// Jede Regel <name> wird Subroutine p_<name>; Einstieg "parse" ruft die Startregel.
// TS-Literale vergleichen erst alle Zeichen (mit Offsets) und konsumieren dann in einem
// Schritt -> ein TS konsumiert nie teilweise; NUL am Eingabeende laesst jeden Vergleich
// von selbst scheitern (kein separater Laengencheck noetig).
static void emitConsume68k(FILE* fp, int len) {
	if (len == 0) {
		return;			// leeres Literal "" matcht immer, konsumiert nichts
	}
	if (len <= 8) {
		fprintf(fp, "\taddq.l\t#%d,a0\n", len);
	}
	else {
		fprintf(fp, "\tlea\t%d(a0),a0\n", len);
	}
}

static void emitLongerLiteralReject68k(FILE* fp, const char* text, int failLabel) {
	int i, j, k, skip; size_t len = strlen(text);
	char cc[16];
	if (!lexActive || isWordLiteral(text)) return;
	for (i = 0; i < nodeCnt; i++) {
		const char* longer;
		if (nodes[i].kind != AST_TS) continue;
		longer = nodes[i].text;
		if (strlen(longer) <= len || strncmp(longer, text, len) != 0) continue;
		for (j = 0; j < i; j++) if (nodes[j].kind == AST_TS && strcmp(nodes[j].text, longer) == 0) break;
		if (j < i) continue;
		skip = newLabel();
		for (k = (int)len; longer[k]; k++) {
			charComment(longer[k], cc, sizeof(cc));
			fprintf(fp, "\tcmpi.b\t#$%02X,%d(a0)\t; Longest-Match %s\n", (unsigned char)longer[k], k, cc);
			fprintf(fp, "\tbne\tL%d\n", skip);
		}
		fprintf(fp, "\tbra\tL%d\t; kuerzeres Operator-Token ablehnen\nL%d:\n", failLabel, skip);
	}
}

static void genNode68k(FILE* fp, int id, int failLabel, int lexical) {
	AstNode* n = &nodes[id];
	int child, l1, l2, lok, i;
	char cc[16], cc2[16];

	switch (n->kind) {
	case AST_TS: {
		int len = (int)strlen(n->text);
		if (lexActive && !lexical) {
			fprintf(fp, "\tbsr\tws\n");
		}
		for (i = 0; i < len; i++) {
			charComment(n->text[i], cc, sizeof(cc));
			if (i == 0) {
				fprintf(fp, "\tcmpi.b\t#$%02X,(a0)\t; %s\n", (unsigned char)n->text[i], cc);
			}
			else {
				fprintf(fp, "\tcmpi.b\t#$%02X,%d(a0)\t; %s\n", (unsigned char)n->text[i], i, cc);
			}
			fprintf(fp, "\tbne\tL%d\n", failLabel);
		}
		if (lexActive && !lexical) emitLongerLiteralReject68k(fp, n->text, failLabel);
		if (lexActive && !lexical && isWordLiteral(n->text)) {
			// Wortgrenze: Folgezeichen darf kein Identifikator-Zeichen sein
			fprintf(fp, "\tmove.b\t%d(a0),d1\n", len);
			fprintf(fp, "\tbsr\tidch\n");
			fprintf(fp, "\ttst.b\td0\n");
			fprintf(fp, "\tbne\tL%d\t; Wortgrenze verletzt\n", failLabel);
		}
		emitConsume68k(fp, len);
		break;
	}
	case AST_RNG:
		charComment(n->lo, cc, sizeof(cc));
		charComment(n->hi, cc2, sizeof(cc2));
		if (lexActive && !lexical) {
			fprintf(fp, "\tbsr\tws\n");
		}
		fprintf(fp, "\tmove.b\t(a0),d1\n");
		fprintf(fp, "\tcmpi.b\t#$%02X,d1\t; %s\n", (unsigned char)n->lo, cc);
		fprintf(fp, "\tblo\tL%d\n", failLabel);
		fprintf(fp, "\tcmpi.b\t#$%02X,d1\t; %s\n", (unsigned char)n->hi, cc2);
		fprintf(fp, "\tbhi\tL%d\n", failLabel);
		fprintf(fp, "\taddq.l\t#1,a0\n");
		break;
	case AST_NTS: {
		char aName[GEN_NAME_LEN];
		int idx = ruleIndexByName(n->text);
		sanitizeName(n->text, aName);
		if (lexActive && !lexical && idx >= 0 && ruleIsLexical[idx]) {
			fprintf(fp, "\tbsr\tws\n");
		}
		fprintf(fp, "\tbsr\tp_%s\n", aName);
		fprintf(fp, "\ttst.b\td0\n");
		fprintf(fp, "\tbeq\tL%d\n", failLabel);
		break;
	}
	case AST_SEQ:
		for (child = n->firstChild; child >= 0; child = nodes[child].nextSib) {
			genNode68k(fp, child, failLabel, lexical);
		}
		break;
	case AST_ALT: {
		lok = newLabel();
		fprintf(fp, "\tmove.l\ta0,-(a7)\t; Ruecksetzpunkt Auswahl\n");
		child = n->firstChild;
		while (child >= 0) {
			l1 = newLabel();
			genNode68k(fp, child, l1, lexical);
			fprintf(fp, "\tbra\tL%d\n", lok);
			if (nodes[child].nextSib >= 0) {
				fprintf(fp, "L%d:\tmove.l\t(a7),a0\t; naechste Alternative\n", l1);
			}
			else {
				fprintf(fp, "L%d:\tmove.l\t(a7)+,a0\t; Auswahl gescheitert\n", l1);
				fprintf(fp, "\tbra\tL%d\n", failLabel);
			}
			child = nodes[child].nextSib;
		}
		fprintf(fp, "L%d:\taddq.l\t#4,a7\n", lok);
		break;
	}
	case AST_OPT:
		l1 = newLabel();
		l2 = newLabel();
		fprintf(fp, "\tmove.l\ta0,-(a7)\t; Ruecksetzpunkt Option\n");
		genNode68k(fp, n->firstChild, l1, lexical);
		fprintf(fp, "\taddq.l\t#4,a7\n");
		fprintf(fp, "\tbra\tL%d\n", l2);
		fprintf(fp, "L%d:\tmove.l\t(a7)+,a0\t; Option uebersprungen\n", l1);
		fprintf(fp, "L%d:\n", l2);
		break;
	case AST_REP:
		l1 = newLabel();
		l2 = newLabel();
		fprintf(fp, "L%d:\tmove.l\ta0,-(a7)\t; Ruecksetzpunkt Wiederholung\n", l1);
		genNode68k(fp, n->firstChild, l2, lexical);
		fprintf(fp, "\taddq.l\t#4,a7\n");
		fprintf(fp, "\tbra\tL%d\n", l1);
		fprintf(fp, "L%d:\tmove.l\t(a7)+,a0\t; Wiederholung beendet\n", l2);
		break;
	}
}

// Laufzeit-Helfer fuer den LEXER-Modus:
//   ws   -- ueberliest WHITESPACE-Zeichen und (falls konfiguriert) Zeilenkommentare.
//           Zerstoert d1, laesst d0 unangetastet.
//   idch -- prueft d1: Identifikator-Zeichen? d0.b = 1 ja / 0 nein.
static void emitLexHelpers68k(FILE* fp, const char* cs) {
	int lTop = newLabel();		// ws: Schleifenkopf
	int lSkip = newLabel();		// ws: ein Zeichen ueberlesen
	int lRet = newLabel();		// ws: fertig
	int lCmtShared = newLabel();	// ws: gemeinsamer Rumpf "bis Zeilenende ueberlesen" fuer ALLE Line-Marker
	int lLCentry[LEX_MARKERS_MAX];		// ws: Eintritt je Zeilenkommentar-Marker
	int lBCentry[LEX_MARKERS_MAX];		// ws: Eintritt je Blockkommentar-Marker
	int lYes = newLabel();		// idch: ja
	int lNo1 = newLabel();
	int lNo2 = newLabel();
	int lNo3 = newLabel();
	int i, j;
	char cc[16];

	for (j = 0; j < lexLineCommentCnt; j++) lLCentry[j] = newLabel();
	for (j = 0; j < lexBlockCnt; j++) lBCentry[j] = newLabel();

	fprintf(fp, "%s---------------------------------------------------------------------------\n", cs);
	fprintf(fp, "%s ws -- Whitespace/Kommentare ueberlesen (zerstoert d1%s)\n", cs,
		lexAnyBlockNested() ? "/d2" : "");
	fprintf(fp, "ws:\n");
	fprintf(fp, "L%d:\tmove.b\t(a0),d1\n", lTop);
	fprintf(fp, "\tbeq\tL%d\t; Eingabeende\n", lRet);
	for (i = 0; i < lexWsLen; i++) {
		charComment(lexWs[i], cc, sizeof(cc));
		fprintf(fp, "\tcmpi.b\t#$%02X,d1\t; %s\n", (unsigned char)lexWs[i], cc);
		fprintf(fp, "\tbeq\tL%d\n", lSkip);
	}
	// Zeilenkommentar-Marker der Reihe nach probieren: Mismatch faellt zum naechsten
	// Marker durch (letzter Marker faellt zum ersten Blockkommentar-Marker bzw. lRet).
	// Voller Match springt IMMER explizit zu lCmtShared (sonst wuerde er in den
	// naechsten Marker-Check hineinlaufen).
	for (j = 0; j < lexLineCommentCnt; j++) {
		int failTo = (j + 1 < lexLineCommentCnt) ? lLCentry[j + 1]
			: (lexBlockCnt > 0 ? lBCentry[0] : lRet);
		fprintf(fp, "L%d:\n", lLCentry[j]);
		for (i = 0; i < lexLineCommentLen[j]; i++) {
			charComment(lexLineComment[j][i], cc, sizeof(cc));
			if (i == 0) {
				fprintf(fp, "\tcmpi.b\t#$%02X,(a0)\t; %s\n", (unsigned char)lexLineComment[j][i], cc);
			}
			else {
				fprintf(fp, "\tcmpi.b\t#$%02X,%d(a0)\t; %s\n", (unsigned char)lexLineComment[j][i], i, cc);
			}
			fprintf(fp, "\tbne\tL%d\n", failTo);
		}
		fprintf(fp, "\tbra\tL%d\n", lCmtShared);
	}
	if (lexLineCommentCnt > 0) {
		fprintf(fp, "L%d:\tmove.b\t(a0),d1\t; Kommentar bis Zeilenende\n", lCmtShared);
		fprintf(fp, "\tbeq\tL%d\n", lRet);
		fprintf(fp, "\tcmpi.b\t#$0A,d1\t; LF\n");
		fprintf(fp, "\tbeq\tL%d\n", lTop);
		fprintf(fp, "\taddq.l\t#1,a0\n");
		fprintf(fp, "\tbra\tL%d\n", lCmtShared);
	}
	// Blockkommentar-Marker der Reihe nach probieren (analog); jeder Marker hat seine
	// eigene Ende-Sequenz und damit seine eigene innere Schleife -- kann NICHT wie bei
	// den Zeilenkommentaren einen gemeinsamen Rumpf teilen.
	for (j = 0; j < lexBlockCnt; j++) {
		int failTo = (j + 1 < lexBlockCnt) ? lBCentry[j + 1] : lRet;
		int lBk = newLabel();
		int lBk1 = newLabel();
		int lBkOpen = lexBlockNested[j] ? newLabel() : -1;

		fprintf(fp, "L%d:\n", lBCentry[j]);
		for (i = 0; i < lexBlockOnLen[j]; i++) {
			charComment(lexBlockOn[j][i], cc, sizeof(cc));
			if (i == 0) {
				fprintf(fp, "\tcmpi.b\t#$%02X,(a0)\t; %s\n", (unsigned char)lexBlockOn[j][i], cc);
			}
			else {
				fprintf(fp, "\tcmpi.b\t#$%02X,%d(a0)\t; %s\n", (unsigned char)lexBlockOn[j][i], i, cc);
			}
			fprintf(fp, "\tbne\tL%d\n", failTo);
		}
		emitConsume68k(fp, lexBlockOnLen[j]);
		if (lexBlockNested[j]) {
			fprintf(fp, "\tmoveq\t#1,d2\t; Schachtelungs-Tiefe\n");
		}
		fprintf(fp, "L%d:\tmove.b\t(a0),d1\t; im Blockkommentar\n", lBk);
		fprintf(fp, "\tbeq\tL%d\t; unterminiert: Eingabeende\n", lRet);
		for (i = 0; i < lexBlockOffLen[j]; i++) {
			charComment(lexBlockOff[j][i], cc, sizeof(cc));
			if (i == 0) {
				fprintf(fp, "\tcmpi.b\t#$%02X,(a0)\t; %s\n", (unsigned char)lexBlockOff[j][i], cc);
			}
			else {
				fprintf(fp, "\tcmpi.b\t#$%02X,%d(a0)\t; %s\n", (unsigned char)lexBlockOff[j][i], i, cc);
			}
			fprintf(fp, "\tbne\tL%d\n", lexBlockNested[j] ? lBkOpen : lBk1);
		}
		emitConsume68k(fp, lexBlockOffLen[j]);
		if (lexBlockNested[j]) {
			fprintf(fp, "\tsubq.l\t#1,d2\n");
			fprintf(fp, "\tbeq\tL%d\t; Tiefe 0: Kommentar zu Ende, weiter ueberlesen\n", lTop);
			fprintf(fp, "\tbra\tL%d\n", lBk);
			fprintf(fp, "L%d:\n", lBkOpen);
			for (i = 0; i < lexBlockOnLen[j]; i++) {
				charComment(lexBlockOn[j][i], cc, sizeof(cc));
				if (i == 0) {
					fprintf(fp, "\tcmpi.b\t#$%02X,(a0)\t; %s\n", (unsigned char)lexBlockOn[j][i], cc);
				}
				else {
					fprintf(fp, "\tcmpi.b\t#$%02X,%d(a0)\t; %s\n", (unsigned char)lexBlockOn[j][i], i, cc);
				}
				fprintf(fp, "\tbne\tL%d\n", lBk1);
			}
			emitConsume68k(fp, lexBlockOnLen[j]);
			fprintf(fp, "\taddq.l\t#1,d2\t; tiefer geschachtelt\n");
			fprintf(fp, "\tbra\tL%d\n", lBk);
		}
		else {
			fprintf(fp, "\tbra\tL%d\t; Kommentar zu Ende, weiter ueberlesen\n", lTop);
		}
		fprintf(fp, "L%d:\taddq.l\t#1,a0\n", lBk1);
		fprintf(fp, "\tbra\tL%d\n", lBk);
	}
	if (lexLineCommentCnt == 0 && lexBlockCnt == 0) {
		fprintf(fp, "\tbra\tL%d\n", lRet);
	}
	fprintf(fp, "L%d:\taddq.l\t#1,a0\n", lSkip);
	fprintf(fp, "\tbra\tL%d\n", lTop);
	fprintf(fp, "L%d:\trts\n\n", lRet);

	fprintf(fp, "%s---------------------------------------------------------------------------\n", cs);
	fprintf(fp, "%s idch -- d1 Identifikator-Zeichen? d0.b = 1/0\n", cs);
	fprintf(fp, "idch:\n");
	fprintf(fp, "\tcmpi.b\t#$30,d1\t; '0'\n");
	fprintf(fp, "\tblo\tL%d\n", lNo1);
	fprintf(fp, "\tcmpi.b\t#$39,d1\t; '9'\n");
	fprintf(fp, "\tbls\tL%d\n", lYes);
	fprintf(fp, "L%d:\tcmpi.b\t#$41,d1\t; 'A'\n", lNo1);
	fprintf(fp, "\tblo\tL%d\n", lNo2);
	fprintf(fp, "\tcmpi.b\t#$5A,d1\t; 'Z'\n");
	fprintf(fp, "\tbls\tL%d\n", lYes);
	fprintf(fp, "L%d:\tcmpi.b\t#$61,d1\t; 'a'\n", lNo2);
	fprintf(fp, "\tblo\tL%d\n", lNo3);
	fprintf(fp, "\tcmpi.b\t#$7A,d1\t; 'z'\n");
	fprintf(fp, "\tbls\tL%d\n", lYes);
	fprintf(fp, "L%d:\tcmpi.b\t#$5F,d1\t; '_'\n", lNo3);
	fprintf(fp, "\tbeq\tL%d\n", lYes);
	fprintf(fp, "\tcmpi.b\t#$24,d1\t; '$'\n");
	fprintf(fp, "\tbeq\tL%d\n", lYes);
	fprintf(fp, "\tmoveq\t#0,d0\n");
	fprintf(fp, "\trts\n");
	fprintf(fp, "L%d:\tmoveq\t#1,d0\n", lYes);
	fprintf(fp, "\trts\n\n");
}

// Gemeinsamer Kern beider 68k-Ausgabeformate. os9=0: "nacktes" Motorola-Format
// (vasm-kompatibel, nur Labels). os9=1: Microware-r68-Format -- gleicher Code-Body
// (r68 akzeptiert Label-Doppelpunkte und ";"-Endkommentare, empirisch verifiziert),
// aber '*' fuer VOLLE Kommentarzeilen plus nam/psect/ends-Rahmen.
static int genParser68kTo(const char* path, int os9, const char* baseName) {
	FILE* fp;
	int r, fail;
	char aName[GEN_NAME_LEN];
	char psectName[GEN_NAME_LEN + 4];
	const char* cs = os9 ? "*" : ";";		// Praefix fuer volle Kommentarzeilen

	if (!validateAstForCodegen()) {
		printf("         %s wird nicht erzeugt.\n", path);
		return 0;
	}
	if (!computeLexicalSet()) {
		printf("         %s wird nicht erzeugt.\n", path);
		return 0;
	}
	if (fopen_s(&fp, path, "w") != 0) {
		printf("CODEGEN: kann '%s' nicht schreiben\n", path);
		return 0;
	}
	labelCnt = 0;

	if (os9) {
		if (cgenPsect[0] != '\0') {
			strcpy_s(psectName, sizeof(psectName), cgenPsect);
		}
		else {
			sanitizeName((baseName != NULL) ? baseName : "parser", psectName);
			strcat_s(psectName, sizeof(psectName), "_p");
		}
	}

	fprintf(fp, "%s---------------------------------------------------------------------------\n", cs);
	fprintf(fp, "%s Automatisch erzeugt von ebnf -- NICHT von Hand aendern.\n", cs);
	fprintf(fp, "%s Backtracking-Parser (rekursiver Abstieg, geordnete Auswahl), 68k/Motorola.\n", cs);
	fprintf(fp, "%s\n", cs);
	fprintf(fp, "%s Aufruf:  a0 = ^Eingabe (NUL-terminiert)\n", cs);
	fprintf(fp, "%s          bsr parse\n", cs);
	fprintf(fp, "%s Rueckgabe: d0.b = 1 Erfolg (a0 hinter dem Erkannten), 0 Misserfolg\n", cs);
	fprintf(fp, "%s            (bei Misserfolg ist a0 unveraendert). d1%s wird zerstoert.\n", cs,
		lexAnyBlockNested() ? "/d2" : "");
	fprintf(fp, "%s Vollstaendige Erkennung: nach Erfolg pruefen, ob (a0) = 0 (Eingabeende).\n", cs);
	if (lexActive) {
		fprintf(fp, "%s LEXER aktiv: parse ueberliest nach Erfolg auch Whitespace am Ende;\n", cs);
		fprintf(fp, "%s lexikalische Regeln (TOKEN-Abschluss) matchen adjazente Zeichen.\n", cs);
	}
	fprintf(fp, "%s Startregel: %s\n", cs, rules[0].name);
	fprintf(fp, "%s---------------------------------------------------------------------------\n\n", cs);

	if (os9) {
		fprintf(fp, "\tnam\t%s\n", psectName);
		fprintf(fp, "\tpsect\t%s,0,0,1,0,0\n\n", psectName);
	}

	sanitizeName(rules[0].name, aName);
	if (lexActive) {
		int lDone = newLabel();
		fprintf(fp, "parse:\tbsr\tp_%s\n", aName);
		fprintf(fp, "\ttst.b\td0\n");
		fprintf(fp, "\tbeq\tL%d\n", lDone);
		fprintf(fp, "\tbsr\tws\t; Whitespace am Eingabeende gehoert mit dazu\n");
		fprintf(fp, "L%d:\trts\n\n", lDone);
		emitLexHelpers68k(fp, cs);
	}
	else {
		fprintf(fp, "parse:\tbsr\tp_%s\n\trts\n\n", aName);
	}

	for (r = 0; r < ruleCnt; r++) {
		sanitizeName(rules[r].name, aName);
		fail = newLabel();
		fprintf(fp, "%s---------------------------------------------------------------------------\n", cs);
		fprintf(fp, "%s Regel: %s%s\n", cs, rules[r].name, ruleIsLexical[r] ? " (lexikalisch)" : "");
		fprintf(fp, "p_%s:\n", aName);
		fprintf(fp, "\tmove.l\ta0,-(a7)\t; Ruecksetzpunkt Regel\n");
		genNode68k(fp, rules[r].root, fail, ruleIsLexical[r]);
		if (ruleActionCall[r][0] != '\0' && routineText68k(ruleActionCall[r]) != NULL) {
			fprintf(fp, "\tbsr\t%s\t%s ACTION AFTER %s (a0=Ende, muss erhalten bleiben)\n",
				ruleActionCall[r], cs, rules[r].name);
		}
		fprintf(fp, "\taddq.l\t#4,a7\n");
		fprintf(fp, "\tmoveq\t#1,d0\n");
		fprintf(fp, "\trts\n");
		fprintf(fp, "L%d:\tmove.l\t(a7)+,a0\t; Regel gescheitert, Position zurueck\n", fail);
		fprintf(fp, "\tmoveq\t#0,d0\n");
		fprintf(fp, "\trts\n\n");
	}
	if (routines68kCnt > 0) {
		fprintf(fp, "%s---------------------------------------------------------------------------\n", cs);
		fprintf(fp, "%s ACTION-Routinen aus [NUTZER-CODE] (roh uebernommen)\n", cs);
		for (r = 0; r < routines68kCnt; r++) {
			fprintf(fp, "%s\n", routines68k[r].text);
		}
	}
	if (os9) {
		fprintf(fp, "\tends\n");
	}
	fclose(fp);
	return 1;
}

int genParser68k(const char* path) {
	return genParser68kTo(path, 0, NULL);
}

int genParser68kOS9(const char* path, const char* baseName) {
	return genParser68kTo(path, 1, baseName);
}
