/* Automatisch erzeugt von parsec -- NICHT von Hand aendern.
 * Backtracking-Parser (rekursiver Abstieg, geordnete Auswahl).
 * Aufruf: parser "<eingabe>"  -> druckt OK/FAIL, exit 0/1.
 * LEXER aktiv: Whitespace/Kommentare werden zwischen Symbolen ueberlesen,
 * lexikalische Regeln (TOKEN-Abschluss) matchen adjazente Zeichen.
 * Startregel: program
 */
#include <stdio.h>
#include <string.h>

static const char* p;
static int actionLogLen = 0;	/* siehe ACTION-Routinen weiter unten */

static int idch(int c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
	    || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

static const char wsSet[] = " \x09\x0d\x0a";
static void ws(void) {
	for (;;) {
		if (*p && strchr(wsSet, *p)) { p++; continue; }
		if (strncmp(p, "//", 2) == 0) { while (*p && *p != '\n') p++; continue; }
		if (strncmp(p, "/*", 2) == 0) { p += 2; while (*p && strncmp(p, "*/", 2) != 0) p++; if (*p) p += 2; continue; }
		return;
	}
}

#define ACTION_LOG_MAX 1048576
extern void exit(int);
typedef void (*ActionFn)(const char*, const char*);
typedef struct { ActionFn fn; const char* start; const char* end; } ActionLogEntry;
static ActionLogEntry actionLog[ACTION_LOG_MAX];
static void actionLogPush(ActionFn fn, const char* start, const char* end) {
	if (actionLogLen >= ACTION_LOG_MAX) {
		fprintf(stderr, "qcc: Aktions-Log-Grenze (%d) ueberschritten -- Eingabe zu gross/komplex fuer diese Version.\n", ACTION_LOG_MAX);
		exit(1);
	}
	actionLog[actionLogLen].fn = fn;
	actionLog[actionLogLen].start = start;
	actionLog[actionLogLen].end = end;
	actionLogLen++;
}
static void actionLogReplay(void) {
	int i;
	for (i = 0; i < actionLogLen; i++) actionLog[i].fn(actionLog[i].start, actionLog[i].end);
}

/* ACTION-Routinen aus [NUTZER-CODE] (roh uebernommen) */
/* ---- QCC Frontend: globaler Zustand + Helfer (ARCHITEKTUR.md Kap.10) ---- */
typedef struct TCType {
	char base;                    /* i=int32, u=uint32, c=char, b=bool, z=Nullkonstante, s=struct */
	unsigned char pointers;       /* 0=Skalar, 1=T*, 2=T** usw. */
	unsigned char structId;       /* 1-basiert, gueltig nur wenn base=='s'; 0=keiner */
	unsigned char pointeeConst;   /* nur bei pointers>0 relevant: 1 = "const T*" -- Schreiben
	                                  DURCH den Pointer (*p = .., p[i] = ..) verboten, der
	                                  Pointer selbst bleibt frei zuweisbar (echtes C-Modell,
	                                  im Unterschied zu Bindungs-Immutabilitaet bei Skalaren).
	                                  Nur EIN Bit fuer die gesamte Kette -- bei T** wird nur
	                                  die unmittelbare Dereferenzierung geschuetzt, kein
	                                  vollstaendiges Mehrebenen-Constmodell. */
} TCType;
#define TC_SCALAR(ch) tcMakeType((ch), 0)
/* Structs mit GEMISCHTEN skalaren Feldtypen (2026-07-24, siehe SELFHOSTING_LUECKENLISTE.md):
   ein struct-Wert ist ein Byte-Blob fester Groesse (tcStructByteSize[id]), jedes Feld hat
   einen eigenen Typ (tcStructFieldTypes) und einen eigenen Byte-Offset (tcStructFieldOffset)
   nach natuerlichem Alignment (4-Byte-Typen auf 4er-Grenze, char/bool ohne Padding).
   .feld-Zugriffe emittieren PUSHADDR/PUSH <offset>/IPADD c/LOADIND|STOREIND <feldTag> --
   bereits vorhandene, architekturneutrale Pointer-Arithmetik-Opcodes (siehe tc_varref/
   tc_target), kein neuer Opcode und keine Backend-Aenderung noetig. Bewusst NICHT
   abgedeckt (siehe SELFHOSTING_LUECKENLISTE.md, je eigener Folgeschritt): Array-Felder,
   Pointer-Felder (unterschiedliche Groesse 68k/ARM64 wuerde das frontend-berechnete Layout
   architekturabhaengig machen) und verschachtelte structs. */
#define MAX_STRUCTS 16
#define MAX_STRUCT_FIELDS 16
static char tcStructNames[MAX_STRUCTS][32];
static int  tcStructFieldCount[MAX_STRUCTS];
static char tcStructFieldNames[MAX_STRUCTS][MAX_STRUCT_FIELDS][32];
static TCType tcStructFieldTypes[MAX_STRUCTS][MAX_STRUCT_FIELDS];
static int  tcStructFieldOffset[MAX_STRUCTS][MAX_STRUCT_FIELDS];
static int  tcStructFieldArrayLen[MAX_STRUCTS][MAX_STRUCT_FIELDS]; /* 0=Skalar, sonst Elementzahl */
static int  tcStructByteSize[MAX_STRUCTS];
static int  tcStructCount = 0;
static char tcStructBuildName[32];
static int  tcStructBuildFieldCount = 0;
static char tcStructBuildFieldNames[MAX_STRUCT_FIELDS][32];
static TCType tcStructBuildFieldTypes[MAX_STRUCT_FIELDS];
static int  tcStructBuildFieldArrayLen[MAX_STRUCT_FIELDS];
/* anonymes struct inline im typedef (2026-07-24, siehe tc_anonstructbegin/tc_typedefend):
   der Zielname ("Name" in typedef struct {...} Name;) kommt in der Grammatik erst NACH
   dem Feld-Body -- die Felder werden wie gewohnt gesammelt, die Registrierung (mit dem
   typedef-Namen selbst als internem struct-Tag) passiert verzoegert in tc_typedefend. */
static int  tcAnonStructPending = 0;
#define MAX_TYPEDEFS 32
static char tcTypedefNames[MAX_TYPEDEFS][32];
static TCType tcTypedefTypes[MAX_TYPEDEFS];
static int  tcTypedefCount = 0;
/* ---- Funktionszeiger ---------------------------------------------------
   Ein Funktionszeigertyp ist base='F', pointers=1 (er IST ein Zeiger, also
   liefert tcIsPointer korrekt 1 -- wichtig, weil Backend/IR danach zwischen
   Zeiger- und Zahlwerten unterscheiden). structId traegt 1-basiert die
   Signatur-Id, genau wie bei base='s' der Struct-Index; tcSameType
   vergleicht sie deshalb fuer 'F' mit. */
#define MAX_FNSIGS 16
#define MAX_FNSIG_PARAMS 8
static TCType tcFnSigRet[MAX_FNSIGS];
static int    tcFnSigNargs[MAX_FNSIGS];
static TCType tcFnSigParams[MAX_FNSIGS][MAX_FNSIG_PARAMS];
static int    tcFnSigCount = 0;
static TCType tcFnPtrRetPending;     /* Rueckgabetyp, gemerkt beim "(" der Zeigerklammer */
/* enum: Konstanten UND (seit 2026-07-23) der Enum-NAME selbst sind bekannt, damit
   "enum Name var;" als Deklaration moeglich ist -- im Speicher/Typsystem bleibt
   ein enum-Wert einfach 'i' (int), keine eigene Typidentitaet/-pruefung noetig
   (entspricht C, wo enum frei mit int austauschbar ist). */
#define MAX_ENUM_CONSTANTS 128
static char tcEnumConstNames[MAX_ENUM_CONSTANTS][32];
static long tcEnumConstValues[MAX_ENUM_CONSTANTS];
static int  tcEnumConstCount = 0;
#define MAX_ENUM_TYPES 16
static char tcEnumTypeNames[MAX_ENUM_TYPES][32];
static int  tcEnumTypeCount = 0;
/* 2026-07-25: MAX_LOCALS/MAX_GLOBALS/MAX_FUNCTIONS waren vorher als nackte
   "64"-Literale verstreut -- tcLocalCount hatte dabei ueberhaupt KEINE
   Grenzpruefung vor dem Schreiben in tcNames[]/tcLocalTypes[]/... (echter
   Pufferueberlauf bei >64 Parametern+Lokalen einer einzigen Funktion, nicht
   nur ein zu kleines Limit). Gefunden beim Skalierungstest fuer den
   -largedata-Funktionsaufruf-Schalter (Source/qcc_backend_c.cpp) mit 150
   generierten Funktionen, die prompt tcFunctionCount>=64 rissen. Grenzen
   grosszuegig erhoeht UND tcLocalCount erstmals ueberhaupt geprueft (siehe
   tc_param/tc_local), analog zum bereits vorhandenen Muster bei MAX_STRUCTS/
   MAX_TYPEDEFS/MAX_ENUM_CONSTANTS weiter oben. */
#define MAX_LOCALS 256
#define MAX_GLOBALS 512
#define MAX_FUNCTIONS 512
static char tcNames[MAX_LOCALS][32];        /* lokale Variablen der aktuellen Funktion */
static int  tcLocalCount = 0;
static TCType tcLocalTypes[MAX_LOCALS];
static int  tcLocalArrayLen[MAX_LOCALS];     /* 0 = Skalar, sonst GESAMTE (flache) Elementzahl */
#define TC_MAXDIMS 6                 /* grosszuegige Obergrenze fuer Array-Dimensionen, siehe tcEmitNDCombine */
static int  tcLocalArrayNDims[MAX_LOCALS];   /* 1 = kein Mehrdim-Array (Skalar/1D zaehlt auch als 1) */
static int  tcLocalArrayDims[MAX_LOCALS][TC_MAXDIMS - 1]; /* [0]=dim2, [1]=dim3, ... -- nur die ersten NDims-1 Eintraege gueltig */
static int  tcLocalConst[MAX_LOCALS];        /* 1 = mit "const" deklariert (auch Parameter) */
static char tcGlobalNames[MAX_GLOBALS][32];   /* globale Variablen des Programms */
static int  tcGlobalCount = 0;
static TCType tcGlobalTypes[MAX_GLOBALS];
static int  tcGlobalArrayLen[MAX_GLOBALS];
static int  tcGlobalArrayNDims[MAX_GLOBALS];  /* siehe tcLocalArrayNDims */
static int  tcGlobalArrayDims[MAX_GLOBALS][TC_MAXDIMS - 1]; /* siehe tcLocalArrayDims */
static int  tcGlobalConst[MAX_GLOBALS];       /* 1 = mit "const" deklariert */
/* Mehrdatei-Uebersetzung (2026-07-25): tcFunctionIsDeclOnly/tcGlobalIsDeclOnly
   markieren eine bare-Prototyp- bzw. "extern <typ> <name>;"-Deklaration --
   existiert in dieser Datei nur als Signatur/Typ, OHNE Speicher/Rumpf (wird in
   einer ANDEREN QCC-Datei mit normaler interner ABI definiert, siehe
   FUNCDECL/GLOBALDECL-IR). Bewusst getrennt von tcFunctionIsExternal (das
   bleibt die Microware-ABI/clib.l-Bedeutung von "extern" bei Funktionen).
   tcFunctionIsStatic/tcGlobalIsStatic geben "static" zum ersten Mal echte
   Bedeutung (Sichtbarkeitssteuerung fuer den Linker in den Backends, siehe
   docs/STATUS.md). */
static int  tcFunctionIsDeclOnly[MAX_FUNCTIONS];
static int  tcFunctionIsStatic[MAX_FUNCTIONS];
static int  tcGlobalIsDeclOnly[MAX_GLOBALS];
static int  tcGlobalIsStatic[MAX_GLOBALS];
static int  tcIdxNDScratchDeclared[TC_MAXDIMS + 1]; /* Index 2..TC_MAXDIMS: GLOBAL __idxNd_<lvl> einmalig deklariert */
static int  tcStringCounter = 0;    /* naechster freier __strN-Name fuer String-Literale */
static int  tcPendingConst = 0;      /* gesetzt durch constKw, konsumiert von tc_local/tc_param/tc_globalend */
static int  tcPendingStatic = 0;     /* gesetzt durch staticKw; konsumiert von tc_local/tc_param/tc_staticlocal/
                                        tc_globalend/tc_defname (fuer Funktionen: siehe tcFuncNameIsStatic) */
static int  tcCurrentFuncIndex = -1; /* Tabellenindex der aktuell geparsten Funktion (zwischen tc_funcbegin und
                                        tc_funcend/tc_funcdeclend), -1 wenn keine gueltig (Fehlerpfad) */
static int  tcFuncNameIsStatic = 0;  /* Schnappschuss von tcPendingStatic, von tc_defname VOR dem Reset gerettet,
                                        von tc_funcbegin gelesen (tc_defname feuert vor tc_funcbegin) */
static char tcStaticLocalName[32];   /* Name-Zwischenspeicher zwischen staticLocalName und staticVarDecl */
static int  tcStaticRuntimeInitPending = 0; /* gesetzt von tc_staticruntimeinit, konsumiert von tc_staticlocal */
static TCType tcCurrentType;
static TCType tcFuncType;
static char tcFuncName[32];
static char tcFunctionNames[MAX_FUNCTIONS][32];
static TCType tcFunctionReturnTypes[MAX_FUNCTIONS], tcFunctionParamTypes[MAX_FUNCTIONS][64];
static int tcFunctionNargs[MAX_FUNCTIONS], tcFunctionCount = 0;
static int tcFunctionIsExternal[MAX_FUNCTIONS];  /* 1 = extern-Deklaration, kein QCC-Rumpf/FUNC-Block */
static int tcFunctionIsVariadic[MAX_FUNCTIONS];  /* 1 = "..." am Ende der Parameterliste (wie printf) */
/* Sammelzustand waehrend eine einzelne externDecl geparst wird (analog zu
   tcStructBuildFieldTypes fuer struct-Felder). */
static char tcExternName[32];
static TCType tcExternReturnType;
static TCType tcExternBuildParamTypes[64];
static int tcExternBuildParamCount = 0;
static int tcExternIsVariadic = 0;
static char tcCallName[64][32];     /* verschachtelbare Aufruf-Frames */
static int  tcCallArgCount[64];
static int  tcCallFnSig[64];        /* >=0: indirekter Aufruf ueber diese Signatur; -1: normaler Aufruf */
static char tcCallSavedAdd[64], tcCallSavedMul[64];
static char tcCallSavedRel0[64], tcCallSavedRel1[64];
static int  tcCallDepth = 0;
static char tcPendingAdd = 0;       /* '+' oder '-' */
static char tcPendingMul = 0;       /* '*' oder '/' */
static char tcIndexSavedAdd[64], tcIndexSavedMul[64];
static char tcIndexSavedRel0[64], tcIndexSavedRel1[64];
static int  tcIndexDepth = 0;
static char tcRel0 = 0, tcRel1 = 0; /* Relop-Zeichen; tcRel1==0 wenn einstellig */
static int  tcTargetSlot = -1;
static char tcTargetGlobal[32];
static int  tcTargetIsGlobal = 0;
static TCType tcTargetType;
static int  tcTargetIsArray = 0;
static int  tcTargetIndirect = 0;
static char tcAssignOp[3];
static int  tcSemanticErrors = 0;
static TCType tcValueTypes[256];
static int  tcValueDepth = 0;
static TCType tcRetType;
static int  tcArgCount = 0;
static int  tcRetHasVal = 0;
static int  tcLastWasPrint = 0;
static int  tcNextLabel = 0;
/* ---- goto/Sprungmarken -------------------------------------------------
   Quellsprachliche Sprungmarken sind NAMEN, die IR kennt aber nur
   nummerierte Label ("LABEL L<n>"/"JMP L<n>", siehe docs/IR_OPCODES.md).
   Diese Tabelle bildet Name -> Nummer ab, PRO FUNKTION (Reset in
   tc_defname, wie tcLocalCount). Vorwaertsspruenge ("goto weiter;" vor
   "weiter:") sind dadurch problemlos: die Nummer wird beim ERSTEN
   Auftreten vergeben, egal ob das der Sprung oder die Marke war -- die
   Aufloesung zur echten Adresse macht ohnehin erst der Assembler. */
#define MAX_GOTO_LABELS 512
static char tcGotoNames[MAX_GOTO_LABELS][32];
static int  tcGotoLabel[MAX_GOTO_LABELS];    /* zugeordnete IR-Labelnummer */
static char tcGotoDefined[MAX_GOTO_LABELS];  /* Marke "name:" gesehen */
static char tcGotoUsed[MAX_GOTO_LABELS];     /* "goto name;" gesehen */
static int  tcGotoCount = 0;
static int  tcCtrlTop[64], tcCtrlEnd[64], tcCtrlCont[64], tcCtrlExtra[64];
static char tcCtrlKind[64];               /* 'i'=if, 'w'=while, 'f'=for, 'd'=do-while, 's'=switch */
static int  tcCtrlDepth = 0;
/* switch/case: gestapelte Case-Label (case A: case B: body) unterstuetzt, aber KEIN
   Fallthrough MIT Code zwischen verschiedenen Bodies (jeder Body endet implizit wie mit
   break) -- deckt genau das reale Nutzungsmuster in parsec.cpp/codegen.cpp ab. */
#define MAX_SWITCH 16
static int  tcSwitchBodyLabel[MAX_SWITCH], tcSwitchNextLabel[MAX_SWITCH], tcSwitchEndLabel[MAX_SWITCH];
static int  tcSwitchGroupOpen[MAX_SWITCH], tcSwitchHadDefault[MAX_SWITCH];
static int  tcSwitchDepth = 0;
/* Cast-Zieltyp separat zwischenspeichern (nicht ueber tcCurrentType), weil das
   Operanden-factor (castOperand) selbst z.B. ein verschachteltes sizeof/cast
   enthalten und tcCurrentType dabei ueberschreiben kann, bevor castExpr's
   eigene Aktion sie liest. */
static TCType tcCastStack[16];
static int  tcCastDepth = 0;
static char tcLogicKind[64];
static int  tcLogicBranch[64], tcLogicDone[64], tcLogicDepth = 0;
static char tcBitKind[64];
static int  tcBitDepth = 0;
static char tcPendingShift0 = 0, tcPendingShift1 = 0;
static int tcTernaryFalse[64], tcTernaryDone[64], tcTernaryDepth = 0;
static TCType tcTernaryTrueType[64];
static void tcPushCtrl(char kind, int top, int cont, int end, int extra) {
	if (tcCtrlDepth >= 64) { fprintf(stderr, "qcc: control nesting too deep\\n"); return; }
	tcCtrlKind[tcCtrlDepth] = kind;
	tcCtrlTop[tcCtrlDepth] = top;
	tcCtrlCont[tcCtrlDepth] = cont;
	tcCtrlEnd[tcCtrlDepth] = end;
	tcCtrlExtra[tcCtrlDepth] = extra;
	tcCtrlDepth++;
}
static int tcNeedCtrl(char kind) {
	if (tcCtrlDepth > 0 && tcCtrlKind[tcCtrlDepth - 1] == kind) return 1;
	fprintf(stderr, "qcc: internal control-frame mismatch\\n");
	return 0;
}
/* Naechste umschliessende Schleife suchen (if-Rahmen ueberspringen) -- NUR fuer continue,
   das an einem umschliessenden switch vorbei zur echten Schleife muss. */
static int tcFindLoop(void) {
	int i;
	for (i = tcCtrlDepth - 1; i >= 0; i--) {
		if (tcCtrlKind[i] == 'w' || tcCtrlKind[i] == 'f' || tcCtrlKind[i] == 'd') return i;
	}
	return -1;
}
/* Wie tcFindLoop, aber switch zaehlt hier zusaetzlich als gueltiges break-Ziel. */
static int tcFindBreakTarget(void) {
	int i;
	for (i = tcCtrlDepth - 1; i >= 0; i--) {
		if (tcCtrlKind[i] == 'w' || tcCtrlKind[i] == 'f' || tcCtrlKind[i] == 'd' || tcCtrlKind[i] == 's') return i;
	}
	return -1;
}
static void tcCopy(char* dst, const char* s, const char* e) {
	int n = 0; const char* q;
	for (q = s; q < e && n < 31; q++) dst[n++] = *q;
	dst[n] = 0;
}
static int tcEq(const char* a, const char* b) {
	while (*a && *b) { if (*a != *b) return 0; a++; b++; }
	return *a == *b;
}
/* Holt den Bezeichner aus dem erkannten Textbereich einer VOLLSTAENDIGEN
   Regel -- noetig, weil die goto/label-Aktionen bewusst an der Gesamtregel
   haengen und nicht an einer Namens-Unterregel (siehe Kommentar in
   Data/qcc.ebnf bei gotoStmt). skipKw ueberspringt ein fuehrendes
   Schluesselwort ("goto"), danach Leerraum, dann wird bis zum ersten
   Nicht-Bezeichnerzeichen kopiert (":" bzw. ";" beenden also sauber). */
static void tcIdentFromSpan(char* dst, const char* s, const char* e, const char* skipKw) {
	const char* p = s;
	int n = 0;
	while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	if (skipKw) {
		const char* k = skipKw;
		while (p < e && *k && *p == *k) { p++; k++; }
		while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	}
	while (p < e && n < 31 &&
	       ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
	        (*p >= '0' && *p <= '9') || *p == '_')) {
		dst[n++] = *p++;
	}
	dst[n] = 0;
}
/* Sucht die Sprungmarke, legt sie beim ersten Auftreten an. Rueckgabe:
   Index in die tcGoto*-Tabellen, oder -1 bei Ueberlauf. */
static int tcGotoFind(const char* name) {
	int i;
	for (i = 0; i < tcGotoCount; i++)
		if (tcEq(tcGotoNames[i], name)) return i;
	if (tcGotoCount >= MAX_GOTO_LABELS) {
		fprintf(stderr, "qcc: too many goto labels\n"); tcSemanticErrors++; return -1;
	}
	tcCopy(tcGotoNames[tcGotoCount], name, name + strlen(name));
	tcGotoLabel[tcGotoCount] = tcNextLabel++;
	tcGotoDefined[tcGotoCount] = 0;
	tcGotoUsed[tcGotoCount] = 0;
	return tcGotoCount++;
}
static int tcLookupLocal(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcLocalCount; i++) if (tcEq(tcNames[i], name)) return i;
	return -1;
}
static int tcLookupGlobal(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcGlobalCount; i++) if (tcEq(tcGlobalNames[i], name)) return i;
	return -1;
}
/* Wortende ab s (Buchstaben/Ziffern/_) -- fuer Schluesselwort-/Bezeichner-Vergleich in tc_type,
   anders als tcNameEnd (das gezielt bei '[' bzw. '.' fuer Ziel-/Referenzausdruecke stoppt). */
static const char* tcWordEnd(const char* s, const char* e) {
	const char* p = s;
	while (p < e && (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) p++;
	return p;
}
static int tcEqSpan(const char* s, const char* e, const char* lit) {
	while (s < e && *lit && *s == *lit) { s++; lit++; }
	return s == e && *lit == 0;
}
static int tcLookupStruct(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcStructCount; i++) if (tcEq(tcStructNames[i], name)) return i;
	return -1;
}
static int tcLookupStructField(int sid, const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	if (sid < 0 || sid >= tcStructCount) return -1;
	for (i = 0; i < tcStructFieldCount[sid]; i++) if (tcEq(tcStructFieldNames[sid][i], name)) return i;
	return -1;
}
static int tcLookupTypedef(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcTypedefCount; i++) if (tcEq(tcTypedefNames[i], name)) return i;
	return -1;
}
static int tcLookupEnumConst(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcEnumConstCount; i++) if (tcEq(tcEnumConstNames[i], name)) return i;
	return -1;
}
static int tcLookupEnumType(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcEnumTypeCount; i++) if (tcEq(tcEnumTypeNames[i], name)) return i;
	return -1;
}
static TCType tcMakeType(char base, int pointers) { TCType t; t.base = base; t.pointers = (unsigned char)pointers; t.structId = 0; t.pointeeConst = 0; return t; }
/* Vorwaertsdeklaration: Definition steht bei tc_string (ROUTINE C tc_string),
   wird aber auch von tc_globalend (frueher in dieser Datei) fuer String-
   Literale als Array-Initialisierer gebraucht. */
static int tcDecodeStringLit(const char* start, const char* end, unsigned char* bytes, int cap);
static TCType tcBadType(void) { return tcMakeType('?', 0); }
/* base=='F' (Funktionszeiger) traegt wie 's' eine Id in structId -- zwei
   Funktionszeiger sind nur bei GLEICHER Signatur derselbe Typ. */
static int tcSameType(TCType a, TCType b) { return a.base == b.base && a.pointers == b.pointers && ((a.base != 's' && a.base != 'F') || a.structId == b.structId); }
static int tcIsPointer(TCType t) { return t.pointers != 0; }
static int tcIsFnPtr(TCType t) { return t.base == 'F' && t.pointers == 1; }
static TCType tcMakeFnPtr(int sigId) {
	TCType t = tcMakeType('F', 1);
	t.structId = (unsigned char)(sigId + 1);
	return t;
}
static int tcIsInteger(TCType t) { return !t.pointers && (t.base == 'i' || t.base == 'u' || t.base == 'c' || t.base == 'z'); }
static int tcIsBool(TCType t) { return !t.pointers && t.base == 'b'; }
/* ISO C kennt keinen eigenen Wahrheitswert-Typ in Bedingungen: "if (x)" ist
   fuer JEDEN skalaren Typ definiert (Ganzzahl, Zeichen, Zeiger) und bedeutet
   "ungleich 0". QCC war hier bisher strenger als C und verlangte ueberall
   einen echten bool -- das erzwang in jedem Port Umschreibungen ("!= 0",
   aufgeloeste if/else statt "x = (a == b);", siehe Quirk 3/10 in
   docs/FORTSCHRITT.md) und machte maschinell erzeugten, normalen C-Code wie
   Data/qcc_p.c uneuebersetzbar. Da docs/ISO_C_LUECKENLISTE.md ISO-C-Konformitaet
   als Ziel fuehrt, wird die Regel hier auf die C-Bedeutung GELOCKERT.
   Rein additiv: was vorher zulaessig war, bleibt es. Codegen-seitig aendert
   sich nichts -- ein bool liegt ohnehin als 0/1 im selben 32-Bit-Slot wie ein
   int, und JZ/JNZ testen den Wert unabhaengig vom Typ. */
static int tcIsTruthy(TCType t) { return t.pointers != 0 || t.base == 'b' || t.base == 'i' || t.base == 'u' || t.base == 'c' || t.base == 'z'; }
static TCType tcPointerTo(TCType t) { if (t.pointers < 255) t.pointers++; else tcSemanticErrors++; return t; }
static TCType tcPointee(TCType t) { if (t.pointers) t.pointers--; else tcSemanticErrors++; return t; }
static char tcTypeTag(TCType t) { return t.pointers ? 'p' : (t.base == 'c' || t.base == 'b') ? t.base : 'i'; }
static TCType tcPromoteInteger(TCType a, TCType b) { return tcMakeType(a.base == 'u' || b.base == 'u' ? 'u' : 'i', 0); }
static TCType tcLocalType(int slot) { return slot >= 0 && slot < tcLocalCount ? tcLocalTypes[slot] : tcMakeType('i', 0); }
static TCType tcGlobalType(int slot) { return slot >= 0 && slot < tcGlobalCount ? tcGlobalTypes[slot] : tcMakeType('i', 0); }
static int tcLookupFunction(const char* name) { int i; for (i = 0; i < tcFunctionCount; i++) if (tcEq(tcFunctionNames[i], name)) return i; return -1; }
/* Wie tcLookupFunction, aber auf einem Textbereich statt einem C-String --
   tc_varref hat nur start/end. */
static int tcLookupFunction2(const char* s, const char* e) {
	char name[32];
	tcCopy(name, s, e);
	return tcLookupFunction(name);
}
/* Liefert die Signatur-Id zur Signatur einer bereits bekannten Funktion und
   legt sie an, falls es sie noch nicht gibt. Dadurch ist "zeiger = funktion;"
   auch ohne passendes typedef moeglich -- und zwei Funktionen mit gleicher
   Signatur bekommen DIESELBE Id, sind also zuweisungskompatibel. */
static int tcFnSigForFunction(int fnIdx) {
	int i, k;
	if (tcFunctionNargs[fnIdx] > MAX_FNSIG_PARAMS) {
		fprintf(stderr, "qcc: function has too many parameters for a function pointer\n");
		tcSemanticErrors++; return -1;
	}
	for (i = 0; i < tcFnSigCount; i++) {
		if (tcFnSigNargs[i] != tcFunctionNargs[fnIdx]) continue;
		if (!tcSameType(tcFnSigRet[i], tcFunctionReturnTypes[fnIdx])) continue;
		for (k = 0; k < tcFnSigNargs[i]; k++)
			if (!tcSameType(tcFnSigParams[i][k], tcFunctionParamTypes[fnIdx][k])) break;
		if (k == tcFnSigNargs[i]) return i;
	}
	if (tcFnSigCount >= MAX_FNSIGS) {
		fprintf(stderr, "qcc: too many function pointer signatures\n");
		tcSemanticErrors++; return -1;
	}
	tcFnSigRet[tcFnSigCount] = tcFunctionReturnTypes[fnIdx];
	tcFnSigNargs[tcFnSigCount] = tcFunctionNargs[fnIdx];
	for (k = 0; k < tcFunctionNargs[fnIdx]; k++)
		tcFnSigParams[tcFnSigCount][k] = tcFunctionParamTypes[fnIdx][k];
	return tcFnSigCount++;
}
static void tcTypePush(TCType type) { if (tcValueDepth < 256) tcValueTypes[tcValueDepth++] = type; else tcSemanticErrors++; }
static TCType tcTypePop(void) { return tcValueDepth > 0 ? tcValueTypes[--tcValueDepth] : tcBadType(); }
/* Prae-/Postinkrement/-dekrement, nur einfache int/unsigned/char-Skalare (lokal/global) --
   Praefix: LOAD;PUSH 1;ADD-oder-SUB;DUP;STORE (laesst NEUEN Wert); Postfix: LOAD;DUP;PUSH 1;
   ADD-oder-SUB;STORE (laesst ALTEN Wert). STOREC/STOREGC uebernehmen die Byte-Kuerzung wie
   bei normalen Zuweisungen -- kein extra NARROWC noetig. */
static void tcIncDecEmit(int slot, int global, TCType t, int isDec, int isPre) {
	char tag = tcTypeTag(t);
	if (tcIsPointer(t)) {
		/* Zeiger-Inkrement rechnet in ELEMENTEN, nicht in Bytes -- deshalb IPADD
		   mit dem Tag des ZIELtyps statt eines schlichten ADD. IPADD erwartet den
		   Zaehler UNTEN und den Zeiger OBEN (siehe docs/IR_OPCODES.md), weshalb
		   der Zeiger beim Postfix zweimal geladen statt per DUP vervielfaeltigt
		   wird: ein DUP laege an der falschen Stelle relativ zum Zaehler. */
		const char* ld = slot >= 0 ? "LOADP" : "LOADGP";
		const char* st = slot >= 0 ? "STOREP" : "STOREGP";
		char ptag = tcTypeTag(tcPointee(t));
		if (isPre) {
			printf("PUSH %d\n", isDec ? -1 : 1);
			if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
			printf("IPADD %c\nDUP\n", ptag);
		} else {
			if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
			printf("PUSH %d\n", isDec ? -1 : 1);
			if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
			printf("IPADD %c\n", ptag);
		}
		if (slot >= 0) printf("%s %d\n", st, slot); else printf("%s %s\n", st, tcGlobalNames[global]);
		tcTypePush(t);
		return;
	}
	const char* loadOp = slot >= 0 ? (tag == 'i' ? "LOADL" : "LOADC") : (tag == 'i' ? "LOADG" : "LOADGC");
	const char* storeOp = slot >= 0 ? (tag == 'i' ? "STOREL" : "STOREC") : (tag == 'i' ? "STOREG" : "STOREGC");
	if (slot >= 0) printf("%s %d\n", loadOp, slot); else printf("%s %s\n", loadOp, tcGlobalNames[global]);
	if (isPre) printf("PUSH 1\n%s\nDUP\n", isDec ? "SUB" : "ADD");
	else printf("DUP\nPUSH 1\n%s\n", isDec ? "SUB" : "ADD");
	if (slot >= 0) printf("%s %d\n", storeOp, slot); else printf("%s %s\n", storeOp, tcGlobalNames[global]);
	tcTypePush(t);
}
static int tcIncDecCheck(int slot, int global, TCType t) {
	/* Zeiger sind seit 2026-08-10 erlaubt ("p++" ist ein C-Kernidiom und kommt
	   in Data/qcc_p.c ueberall vor) -- tcIncDecEmit rechnet dafuer ueber IPADD
	   mit der Groesse des Zieltyps statt +-1. Arrays bleiben ausgeschlossen
	   (ein Arrayname ist kein veraenderbarer Lvalue), ebenso struct und bool. */
	if (t.base == 's' || t.base == 'b') return 0;
	if (slot >= 0 && tcLocalArrayLen[slot]) return 0;
	if (slot < 0 && global >= 0 && tcGlobalArrayLen[global]) return 0;
	return 1;
}
static int tcCompatible(TCType wanted, TCType got) {
	if (tcSameType(wanted, got)) return 1;
	/* void*: generischer Pointer, in echtem C implizit mit jedem ANDEREN Pointer
	   derselben Tiefe kompatibel (beide Richtungen). Tiefenvergleich verhindert, dass
	   z.B. void** faelschlich mit T* (unterschiedliche Zeigertiefe) kompatibel wird. */
	if (tcIsPointer(wanted) && tcIsPointer(got) && wanted.pointers == got.pointers && (wanted.base == 'v' || got.base == 'v')) return 1;
	if (tcIsPointer(wanted)) return !tcIsPointer(got) && got.base == 'z';
	/* bool ist in C ein Ganzzahltyp (Wert 0/1) -- "int x = (a == b);" und
	   "return (a && b);" aus einer int-Funktion sind legal. */
	if (tcIsInteger(wanted) && tcIsBool(got)) return 1;
	return tcIsInteger(wanted) && tcIsInteger(got);
}
static void tcPrintType(FILE* out, TCType t) {
	int i; const char* name = t.base == 'u' ? "unsigned int" : t.base == 'c' ? "char" : t.base == 'b' ? "bool" : t.base == 'z' ? "null" : t.base == 'i' ? "int" : t.base == 'v' ? "void" : "?";
	fputs(name, out); for (i = 0; i < t.pointers; i++) fputc('*', out);
}
static void tcTypeError(const char* what, TCType wanted, TCType got) {
	fprintf(stderr, "qcc: %s expects ", what); tcPrintType(stderr, wanted);
	fputs(", got ", stderr); tcPrintType(stderr, got); fputc('\n', stderr); tcSemanticErrors++;
}
static void tcLogicBegin(char kind) {
	TCType left = tcTypePop(); int branch, end;
	if (!tcIsTruthy(left)) tcTypeError("logical operator", tcMakeType('b', 0), left);
	if (tcLogicDepth >= 64) { fprintf(stderr, "qcc: logical nesting too deep\n"); tcSemanticErrors++; return; }
	branch = tcNextLabel++; end = tcNextLabel++;
	tcLogicKind[tcLogicDepth] = kind; tcLogicBranch[tcLogicDepth] = branch; tcLogicDone[tcLogicDepth] = end; tcLogicDepth++;
	printf("%s L%d\n", kind == '&' ? "JZ" : "JNZ", branch);
}
static int tcHasToken(const char* s, const char* e, char a, char b) {
	const char* p; for (p = s; p + 1 < e; p++) if (p[0] == a && p[1] == b) return 1;
	return 0;
}
static int tcHasChar(const char* s, const char* e, char c) {
	const char* p; for (p = s; p < e; p++) if (*p == c) return 1;
	return 0;
}
static int tcHasTopToken(const char* s, const char* e, char a, char b) {
	const char* p; int round = 0, square = 0;
	for (p = s; p < e; p++) {
		if (*p == '(') round++;
		else if (*p == ')') round--;
		else if (*p == '[') square++;
		else if (*p == ']') square--;
		else if (!round && !square && p + 1 < e && p[0] == a && p[1] == b) return 1;
	}
	return 0;
}
static int tcHasTopChar(const char* s, const char* e, char c) {
	const char* p; int round = 0, square = 0;
	for (p = s; p < e; p++) {
		if (*p == '(') round++;
		else if (*p == ')') round--;
		else if (*p == '[') square++;
		else if (*p == ']') square--;
		else if (!round && !square && *p == c) return 1;
	}
	return 0;
}
static void tcLogicEnd(char kind) {
	TCType right; int frame;
	if (tcLogicDepth <= 0 || tcLogicKind[tcLogicDepth - 1] != kind) { fprintf(stderr, "qcc: logical-frame mismatch\n"); tcSemanticErrors++; return; }
	right = tcTypePop(); if (!tcIsTruthy(right)) tcTypeError("logical operator", tcMakeType('b', 0), right);
	frame = --tcLogicDepth;
	printf("%s L%d\nPUSH %d\nJMP L%d\nLABEL L%d\nPUSH %d\nLABEL L%d\n",
		kind == '&' ? "JZ" : "JNZ", tcLogicBranch[frame], kind == '&' ? 1 : 0,
		tcLogicDone[frame], tcLogicBranch[frame], kind == '&' ? 0 : 1, tcLogicDone[frame]);
	tcTypePush(tcMakeType('b', 0));
}
static void tcBitBegin(char kind) {
	if (tcBitDepth >= 64) { fprintf(stderr, "qcc: bitwise nesting too deep\n"); tcSemanticErrors++; return; }
	tcBitKind[tcBitDepth++] = kind;
}
static void tcBitEnd(char kind) {
	TCType right, left;
	if (tcBitDepth <= 0 || tcBitKind[tcBitDepth - 1] != kind) { fprintf(stderr, "qcc: bitwise-frame mismatch\n"); tcSemanticErrors++; return; }
	tcBitDepth--;
	right = tcTypePop(); left = tcTypePop();
	if (!tcIsInteger(left)) tcTypeError("bitwise operator", tcMakeType('i', 0), left);
	if (!tcIsInteger(right)) tcTypeError("bitwise operator", tcMakeType('i', 0), right);
	tcTypePush(tcPromoteInteger(left, right));
	printf("%s\n", kind == '&' ? "BAND" : kind == '^' ? "BXOR" : "BOR");
}
static void tcShiftEnd(void) {
	TCType right = tcTypePop(), left = tcTypePop();
	if (!tcIsInteger(left)) tcTypeError("shift", tcMakeType('i', 0), left);
	if (!tcIsInteger(right)) tcTypeError("shift", tcMakeType('i', 0), right);
	tcTypePush(left.base == 'u' ? left : tcMakeType('i', 0));
	printf("%s\n", tcPendingShift0 == '<' ? "SHL" : left.base == 'u' ? "USHR" : "SHR");
	tcPendingShift0 = tcPendingShift1 = 0;
}
static void tcTernaryBegin(void) {
	TCType condition = tcTypePop(); int falseLabel, endLabel;
	if (!tcIsTruthy(condition)) tcTypeError("conditional condition", tcMakeType('b', 0), condition);
	if (tcTernaryDepth >= 64) { fprintf(stderr, "qcc: conditional nesting too deep\n"); tcSemanticErrors++; return; }
	falseLabel = tcNextLabel++; endLabel = tcNextLabel++;
	tcTernaryFalse[tcTernaryDepth] = falseLabel;
	tcTernaryDone[tcTernaryDepth] = endLabel;
	tcTernaryDepth++;
	printf("JZ L%d\n", falseLabel);
}
static void tcTernaryMiddle(void) {
	if (tcTernaryDepth <= 0) { fprintf(stderr, "qcc: conditional-frame mismatch\n"); tcSemanticErrors++; return; }
	tcTernaryTrueType[tcTernaryDepth - 1] = tcTypePop();
	printf("JMP L%d\nLABEL L%d\n", tcTernaryDone[tcTernaryDepth - 1], tcTernaryFalse[tcTernaryDepth - 1]);
}
static void tcTernaryEnd(void) {
	TCType falseType, trueType, result;
	if (tcTernaryDepth <= 0) { fprintf(stderr, "qcc: conditional-frame mismatch\n"); tcSemanticErrors++; return; }
	falseType = tcTypePop();
	trueType = tcTernaryTrueType[tcTernaryDepth - 1];
	if (tcSameType(trueType, falseType)) result = trueType;
	else if (tcIsPointer(trueType) && falseType.base == 'z' && !falseType.pointers) result = trueType;
	else if (tcIsPointer(falseType) && trueType.base == 'z' && !trueType.pointers) result = falseType;
	else if (tcIsInteger(trueType) && tcIsInteger(falseType)) result = tcPromoteInteger(trueType, falseType);
	else { tcTypeError("conditional branches", trueType, falseType); result = trueType; }
	tcTypePush(result);
	printf("LABEL L%d\n", tcTernaryDone[tcTernaryDepth - 1]);
	tcTernaryDepth--;
}
static void tcLoadTarget(void) {
	char tag = tcTypeTag(tcTargetType);
	if (tcTargetIndirect) printf("DUPP\nLOADIND %c\n", tag);
	else if (tcTargetIsGlobal && tcTargetIsArray) printf("DUP\nLOADIDX G %s %c\n", tcTargetGlobal, tag);
	else if (tcTargetIsGlobal) printf("LOADG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "", tcTargetGlobal);
	else if (tcTargetSlot >= 0 && tcTargetIsArray) printf("DUP\nLOADIDX L %d %c\n", tcTargetSlot, tag);
	else if (tcTargetSlot >= 0) printf("LOAD%s %d\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "L", tcTargetSlot);
	else { fprintf(stderr, "qcc: unknown assignment target\n"); tcSemanticErrors++; return; }
	tcTypePush(tcTargetType);
}
static void tcCompoundAssign(void) {
	TCType right = tcTypePop(), left = tcTypePop();
	if (tcIsPointer(left)) {
		if ((tcAssignOp[0] != '+' && tcAssignOp[0] != '-') || !tcIsInteger(right)) tcTypeError("pointer compound assignment", tcMakeType('i', 0), right);
		else printf("P%s %c\n", tcAssignOp[0] == '+' ? "ADD" : "SUB", tcTypeTag(tcPointee(left)));
		tcTypePush(left); return;
	}
	if (!tcIsInteger(left) || !tcIsInteger(right)) tcTypeError("compound assignment", tcMakeType('i', 0), !tcIsInteger(left) ? left : right);
	if (tcAssignOp[0] == '+') printf("ADD\n");
	else if (tcAssignOp[0] == '-') printf("SUB\n");
	else if (tcAssignOp[0] == '*') printf("MUL\n");
	else if (tcAssignOp[0] == '/') printf("%s", left.base == 'u' || right.base == 'u' ? "UDIV\n" : "DIV\n");
	else if (tcAssignOp[0] == '%') printf("%s", left.base == 'u' || right.base == 'u' ? "UMOD\n" : "MOD\n");
	else if (tcAssignOp[0] == '&') printf("BAND\n");
	else if (tcAssignOp[0] == '^') printf("BXOR\n");
	else if (tcAssignOp[0] == '|') printf("BOR\n");
	else if (tcAssignOp[0] == '<') printf("SHL\n");
	else if (tcAssignOp[0] == '>') printf("%s", left.base == 'u' ? "USHR\n" : "SHR\n");
	tcTypePush(tcTargetType);
}
static long tcNum(const char* s, const char* e) {
	long v = 0; const char* q;
	/* Hexform "0x.."/"0X.." (siehe hexNumber in der Grammatik). */
	if (e - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		for (q = s + 2; q < e; q++) {
			int d;
			if (*q >= '0' && *q <= '9') d = *q - '0';
			else if (*q >= 'a' && *q <= 'f') d = *q - 'a' + 10;
			else if (*q >= 'A' && *q <= 'F') d = *q - 'A' + 10;
			else break;
			v = v * 16 + d;
		}
		return v;
	}
	for (q = s; q < e; q++) v = v * 10 + (*q - '0');
	return v;
}
static const char* tcNameEnd(const char* s, const char* e) {
	const char* p = s;
	/* auch am "-" anhalten: "p->f" (das "-" kann hier nur der Pfeil sein --
	   ein Minus innerhalb eines Index steht hinter "[", wo schon abgebrochen wird). */
	while (p < e && *p != '[' && *p != '.' && *p != '-') p++;
	return p;
}
static int tcConstIndex(const char* s, const char* e, int* value) {
	const char* p = s;
	int v = 0, digits = 0;
	while (p < e && *p != '[') p++;
	if (p == e) return 0;
	p++;
	while (p < e && *p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); digits = 1; }
	if (!digits || p >= e || *p != ']') return 0;
	p++;
	return p == e ? (*value = v, 1) : 0;
}
static void tcCheckConstIndex(const char* text, const char* end, int length) {
	int index;
	if (tcConstIndex(text, end, &index) && (index < 0 || index >= length)) {
		fprintf(stderr, "qcc: constant array index %d out of range (length %d)\n", index, length);
		tcSemanticErrors++;
	}
}
/* zaehlt TOP-LEVEL "["-Vorkommen zwischen p und end (verschachtelte Klammern in
   einem Index-Ausdruck wie "arr[b[k]]" zaehlen NICHT als zweite Dimension von arr). */
static int tcCountTopIndexes(const char* p, const char* end) {
	int depth = 0, count = 0;
	while (p < end) {
		if (*p == '[') { if (depth == 0) count++; depth++; }
		else if (*p == ']') { if (depth > 0) depth--; }
		p++;
	}
	return count;
}
/* p zeigt auf "[" -- liefert einen Zeiger genau HINTER die zugehoerige,
   klammertiefen-bewusste schliessende "]" (verschachtelte Klammern wie
   "arr[b[k]]" werden korrekt uebersprungen, nicht bei der ERSTEN "]" abgebrochen).
   Wird von arr[i].feld (2026-07-25) gebraucht, um nach dem Index-Ausdruck zu
   pruefen, ob direkt danach ein "." (Feldzugriff) folgt. */
static const char* tcSkipOneIndex(const char* p, const char* end) {
	int depth = 0;
	while (p < end) {
		if (*p == '[') depth++;
		else if (*p == ']') { depth--; p++; if (depth == 0) return p; continue; }
		p++;
	}
	return p;
}
/* arr[i1][i2]...[iN] -- der Laufzeit-Operandenstack traegt an dieser Stelle
   bereits [i1, i2, ..., iN] (i1 unten, iN oben, durch die N bereits geparsten
   Index-Ausdruecke). Direktes MUL/ADD wuerde die FALSCHEN Operanden erwischen
   (immer der oberste Wert, nicht i1) -- deshalb werden i2..iN der Reihe nach
   (von oben, also erst iN, dann i(N-1), ..., zuletzt i2) in je ein eigenes
   verstecktes globales Scratch-Feld ausgelagert, bis nur noch i1 auf dem
   Stack uebrig ist. Danach liefert das Horner-Schema result = (((i1*dim2+i2)
   *dim3+i3)*dim4+i4)...*dimN+iN den flachen row-major-Index -- fuer N=2
   identisch zur urspruenglichen Zweidimensional-Loesung (nur EIN Scratch-Feld
   noetig). Kein neuer IR-Opcode, kein Backend-Change: nur vorhandene
   STOREG/PUSH/MUL/LOADG/ADD-Opcodes, jetzt in einer Schleife statt fest
   ausgeschrieben fuer genau zwei Ebenen. */
static void tcEmitNDCombine(int ndims, const int* trailingDims) {
	char name[24]; int lvl;
	for (lvl = ndims; lvl >= 2; lvl--) {
		sprintf(name, "__idxNd_%d", lvl);
		if (!tcIdxNDScratchDeclared[lvl]) { printf("GLOBAL %s 0 i 1\n", name); tcIdxNDScratchDeclared[lvl] = 1; }
		printf("STOREG %s\n", name);
	}
	for (lvl = 2; lvl <= ndims; lvl++) {
		sprintf(name, "__idxNd_%d", lvl);
		printf("PUSH %d\nMUL\nLOADG %s\nADD\n", trailingDims[lvl - 2], name);
	}
}
/* ndims>1 heisst "diese Variable ist ein Mehrdim-Array" (siehe tcLocalArrayNDims/
   tcGlobalArrayNDims), trailingDims = tcLocalArrayDims[slot]/tcGlobalArrayDims[global]
   (dim2..dimN), idxCount = tcCountTopIndexes(nameEnd, end) am Aufrufort. Nur der
   Fall idxCount==ndims emittiert die Kombinationslogik (ab ndims>1); alle anderen
   Nicht-Uebereinstimmungen werden nur diagnostiziert (kein Absturz, der
   nachfolgende Code laeuft mit dem unveraendert vorhandenen Operandenstack weiter
   -- ein Kompilierfehler ist ohnehin schon gemeldet). Die beiden urspruenglichen
   2D-spezifischen Meldungen ("array is not two-dimensional", "partial indexing of
   a 2D array is not supported") bleiben fuer ndims==1/2 wortgleich bestehen (siehe
   runtests.sh), fuer ndims>2 gibt es generalisierte Entsprechungen. */
static void tcCheckNDIndex(int ndims, const int* trailingDims, int idxCount) {
	if (idxCount == ndims) { if (ndims > 1) tcEmitNDCombine(ndims, trailingDims); return; }
	if (ndims == 1 && idxCount == 2) { fprintf(stderr, "qcc: array is not two-dimensional\n"); tcSemanticErrors++; return; }
	if (ndims > 1 && idxCount == ndims - 1) {
		fprintf(stderr, "qcc: partial indexing of a %dD array is not supported (use arr[i1]...[i%d])\n", ndims, ndims);
		tcSemanticErrors++; return;
	}
	fprintf(stderr, "qcc: array has %d dimension(s), but %d index(es) were given\n", ndims, idxCount);
	tcSemanticErrors++;
}
static int tcInitList(const char* start, const char* end, long* values, int cap) {
	const char* p = start; int count = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p == end || *p != '{') return -1;
	p++;
	for (;;) {
		int neg = 0; long value = 0; int digits = 0;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p < end && *p == '}') return count;
		if (p < end && *p == '-') { neg = 1; p++; }
		while (p < end && *p >= '0' && *p <= '9') { value = value * 10 + (*p++ - '0'); digits = 1; }
		if (!digits || count >= cap) return -2;
		values[count++] = neg ? -value : value;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p < end && *p == ',') { p++; continue; }
		if (p < end && *p == '}') return count;
		return -2;
	}
}
void tc_defname(const char* start, const char* end) {
	tcLocalCount = 0;
	tcGotoCount = 0;          /* Sprungmarken sind funktionslokal (wie in echtem C) */
	tcFuncType = tcCurrentType;
	tcCopy(tcFuncName, start, end);
	/* "static" vor einer Funktion: Schnappschuss VOR dem Reset retten (tc_funcbegin
	   feuert erst am ENDE von funcHead und liest tcFuncNameIsStatic, um
	   tcFunctionIsStatic[] zu setzen -- echte Bedeutung seit Mehrdatei-Uebersetzung,
	   siehe docs/STATUS.md). Reset weiterhin noetig, damit es nicht faelschlich der
	   ERSTEN lokalen Variablen im Funktionskoerper zugerechnet wird. */
	tcFuncNameIsStatic = tcPendingStatic;
	tcPendingStatic = 0;
}

void tc_const(const char* start, const char* end) {
	(void)start; (void)end;
	tcPendingConst = 1;
}

void tc_static(const char* start, const char* end) {
	(void)start; (void)end;
	tcPendingStatic = 1;
}

/* "extern type pointerDecl NAME (...)" -- type/pointerDecl haben tcCurrentType
   bereits VOR diesem Aufruf gesetzt (gleiche Reihenfolge wie bei tc_defname). */
void tc_externname(const char* start, const char* end) {
	tcCopy(tcExternName, start, end);
	tcExternReturnType = tcCurrentType;
	tcExternBuildParamCount = 0;
	tcExternIsVariadic = 0;
}

void tc_externparam(const char* start, const char* end) {
	(void)start; (void)end;
	tcPendingConst = 0; /* rein dokumentarisch, siehe Grammatik-Kommentar -- nur konsumieren */
	if (tcExternBuildParamCount < 64) tcExternBuildParamTypes[tcExternBuildParamCount++] = tcCurrentType;
	else { fprintf(stderr, "qcc: too many extern parameters\n"); tcSemanticErrors++; }
}

void tc_externvariadic(const char* start, const char* end) {
	(void)start; (void)end;
	tcExternIsVariadic = 1;
}

/* Registriert dieselbe Signatur-Tabelle (tcFunctionNames/tcFunctionReturnTypes/
   tcFunctionParamTypes/tcFunctionNargs) wie eine echte QCC-Funktion (siehe
   tc_funcbegin) -- dadurch funktionieren Aufrufpruefung (tc_arg/tc_call:
   tcLookupFunction, Argumentanzahl/-typen) unveraendert fuer externe wie interne
   Funktionen. tcFunctionIsExternal/tcFunctionIsVariadic (neue Parallel-Arrays)
   steuern in tc_call, ob CALL/CALLP (interner QCC-Aufruf, "bsr tc_<name>")
   oder CALLEXT/CALLEXTP (Microware-ABI, siehe Backend) emittiert wird. KEIN
   FUNC/ENDFUNC-IR-Block -- eine extern-Deklaration hat keinen QCC-Rumpf. */
void tc_externdeclend(const char* start, const char* end) {
	int i; (void)start; (void)end;
	if (tcLookupFunction(tcExternName) >= 0) { fprintf(stderr, "qcc: duplicate function\n"); tcSemanticErrors++; return; }
	if (tcFunctionCount >= MAX_FUNCTIONS) { fprintf(stderr, "qcc: too many functions\n"); tcSemanticErrors++; return; }
	i = tcFunctionCount++;
	tcCopy(tcFunctionNames[i], tcExternName, tcExternName + strlen(tcExternName));
	tcFunctionReturnTypes[i] = tcExternReturnType;
	tcFunctionNargs[i] = tcExternBuildParamCount;
	{ int p; for (p = 0; p < tcExternBuildParamCount; p++) tcFunctionParamTypes[i][p] = tcExternBuildParamTypes[p]; }
	tcFunctionIsExternal[i] = 1;
	tcFunctionIsVariadic[i] = tcExternIsVariadic;
}

void tc_type(const char* start, const char* end) {
	const char* we = tcWordEnd(start, end);
	if (tcEqSpan(start, we, "unsigned")) {
		/* "unsigned char" -> QCCs char (der 68k-Codegen laedt char nullerweitert,
		   verhaelt sich also schon vorzeichenlos); "unsigned int"/"unsigned long"
		   -> 'u'. Dafuer muss das ZWEITE Wort geprueft werden, tcWordEnd liefert
		   nur das erste. */
		const char* q = we; const char* qe;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) tcCurrentType = tcMakeType('c', 0);
		else tcCurrentType = tcMakeType('u', 0);
	}
	else if (tcEqSpan(start, we, "int")) tcCurrentType = tcMakeType('i', 0);
	/* long ist auf dem 68k-Ziel wortgleich mit int (32 Bit) -- siehe Grammatik. */
	else if (tcEqSpan(start, we, "long")) tcCurrentType = tcMakeType('i', 0);
	else if (tcEqSpan(start, we, "char")) tcCurrentType = tcMakeType('c', 0);
	else if (tcEqSpan(start, we, "bool")) tcCurrentType = tcMakeType('b', 0);
	else if (tcEqSpan(start, we, "void")) tcCurrentType = tcMakeType('v', 0);
	else if (tcEqSpan(start, we, "struct")) {
		const char* p = we; const char* ne; int sid;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		sid = tcLookupStruct(p, ne);
		if (sid < 0) { fprintf(stderr, "qcc: unknown struct\n"); tcSemanticErrors++; tcCurrentType = tcBadType(); return; }
		tcCurrentType = tcMakeType('s', 0); tcCurrentType.structId = (unsigned char)(sid + 1);
	} else if (tcEqSpan(start, we, "enum")) {
		const char* p = we; const char* ne;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		if (tcLookupEnumType(p, ne) < 0) { fprintf(stderr, "qcc: unknown enum\n"); tcSemanticErrors++; tcCurrentType = tcBadType(); return; }
		tcCurrentType = tcMakeType('i', 0);
	} else {
		int td = tcLookupTypedef(start, we);
		if (td < 0) { fprintf(stderr, "qcc: unknown type name '%.*s'\n", (int)(we - start), start); tcSemanticErrors++; tcCurrentType = tcBadType(); return; }
		tcCurrentType = tcTypedefTypes[td];
	}
}

void tc_pointerdecl(const char* start, const char* end) {
	const char* p; for (p = start; p < end; p++) if (*p == '*') tcCurrentType = tcPointerTo(tcCurrentType);
}

void tc_param(const char* start, const char* end) {
	const char* nameEnd = tcNameEnd(start, end);
	if (tcLocalCount >= MAX_LOCALS) { fprintf(stderr, "qcc: too many locals\n"); tcSemanticErrors++; return; }
	tcCopy(tcNames[tcLocalCount], start, nameEnd);
	tcLocalTypes[tcLocalCount] = nameEnd < end ? tcPointerTo(tcCurrentType) : tcCurrentType;
	if (!tcIsPointer(tcLocalTypes[tcLocalCount]) && tcLocalTypes[tcLocalCount].base == 'v') {
		fprintf(stderr, "qcc: void is not a valid parameter type\n"); tcSemanticErrors++;
		tcLocalTypes[tcLocalCount] = tcMakeType('i', 0);
	}
	tcLocalArrayLen[tcLocalCount] = 0;
	tcLocalArrayNDims[tcLocalCount] = 1;
	/* "const" vor einem Pointertyp ist Pointee-Constness (const char* p laesst p selbst
	   frei zuweisbar, aber *p/p[i] = .. wird verboten -- tcTargetType.pointeeConst greift
	   in tc_target/tc_indirecttarget), keine Bindungs-Immutabilitaet (das uebliche
	   "p++"-Idiom bleibt erlaubt). */
	tcLocalConst[tcLocalCount] = tcPendingConst && !tcIsPointer(tcLocalTypes[tcLocalCount]);
	if (tcPendingConst && tcIsPointer(tcLocalTypes[tcLocalCount])) tcLocalTypes[tcLocalCount].pointeeConst = 1;
	tcPendingConst = 0;
	tcLocalCount++;
}

void tc_funcbegin(const char* start, const char* end) {
	int i, f; (void)start; (void)end;
	tcCurrentFuncIndex = -1;
	f = tcLookupFunction(tcFuncName);
	if (f >= 0) {
		/* Eine bare Vorwaertsdeklaration darf spaeter in derselben Datei
		   durch den echten Funktionsrumpf aufgeloest werden. */
		if (!tcFunctionIsDeclOnly[f] || tcFunctionIsExternal[f] ||
			tcFunctionNargs[f] != tcLocalCount ||
			!tcSameType(tcFunctionReturnTypes[f], tcFuncType)) {
			fprintf(stderr, "qcc: duplicate function\n"); return;
		}
		for (i = 0; i < tcLocalCount; i++) {
			if (!tcSameType(tcFunctionParamTypes[f][i], tcLocalTypes[i])) {
				fprintf(stderr, "qcc: function prototype mismatch\n");
				tcSemanticErrors++; return;
			}
		}
		tcCurrentFuncIndex = f;
		return;
	}
	if (tcFunctionCount >= MAX_FUNCTIONS) { fprintf(stderr, "qcc: too many functions\n"); tcSemanticErrors++; return; }
	f = tcFunctionCount++;
	tcCopy(tcFunctionNames[f], tcFuncName, tcFuncName + strlen(tcFuncName));
	tcFunctionReturnTypes[f] = tcFuncType;
	tcFunctionNargs[f] = tcLocalCount;
	for (i = 0; i < tcLocalCount; i++) tcFunctionParamTypes[f][i] = tcLocalTypes[i];
	tcFunctionIsExternal[f] = 0;
	tcFunctionIsVariadic[f] = 0;
	tcFunctionIsStatic[f] = tcFuncNameIsStatic;
	tcFunctionIsDeclOnly[f] = 0; /* ggf. spaeter von tc_funcdeclend auf 1 gesetzt, falls protoEnd statt funcBody folgt */
	tcCurrentFuncIndex = f;
	/* KEINE IR-Emission hier -- "FUNC <name> <nargs>" wird erst emittiert, wenn
	   tatsaechlich ein Rumpf folgt (siehe tc_funcbodybegin); an dieser Stelle
	   (Ende von funcHead) ist noch nicht bekannt, ob "{" oder ";" folgt. */
}

/* Feuert bei "{" (Beginn eines echten Funktionsrumpfs) -- siehe tc_funcbegin. */
void tc_funcbodybegin(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcCurrentFuncIndex >= 0) tcFunctionIsDeclOnly[tcCurrentFuncIndex] = 0;
	printf("FUNC %s %d %d\n", tcFuncName, tcLocalCount, tcCurrentFuncIndex >= 0 ? tcFunctionIsStatic[tcCurrentFuncIndex] : 0);
}

/* Feuert bei ";" statt einem Rumpf (protoEnd) -- reine Prototyp-Deklaration:
   "definiert in einer anderen QCC-Datei", normale interne bsr/bl-ABI (KEIN
   CALLEXT/Microware-ABI wie beim bestehenden extern-Funktions-Feature). Setzt
   nur das Flag, KEINE FUNC/ENDFUNC-IR -- der Aufrufer (tc_call) emittiert
   trotzdem ganz normal CALL/CALLP, da die Signatur (tcFunctionNargs etc.)
   bereits ueber tc_funcbegin registriert wurde. */
void tc_funcdeclend(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcCurrentFuncIndex < 0) return;
	if (tcFunctionIsStatic[tcCurrentFuncIndex]) {
		fprintf(stderr, "qcc: static function declaration without body is meaningless\n");
		tcSemanticErrors++;
	}
	tcFunctionIsDeclOnly[tcCurrentFuncIndex] = 1;
	printf("FUNCDECL %s %d\n", tcFuncName, tcFunctionNargs[tcCurrentFuncIndex]);
}

void tc_funcend(const char* start, const char* end) {
	int gi;
	(void)start; (void)end;
	/* Sprungmarken sind funktionslokal -- ein "goto" auf eine nie definierte
	   Marke faellt deshalb genau hier auf, nicht erst im Backend (das wuerde
	   sonst ein "JMP L<n>" ohne zugehoeriges "LABEL L<n>" erzeugen und der
	   Assembler meldete einen unaufloesbaren Bezug). */
	for (gi = 0; gi < tcGotoCount; gi++) {
		if (tcGotoUsed[gi] && !tcGotoDefined[gi]) {
			fprintf(stderr, "qcc: undefined label '%s'\n", tcGotoNames[gi]);
			tcSemanticErrors++;
		}
	}
	if (tcCurrentFuncIndex >= 0 && tcFunctionIsDeclOnly[tcCurrentFuncIndex]) return;
	/* Fallthrough-Rueckgabe 0 absichern; ein explizites return davor ist harmlos */
	printf("PUSH 0\n%s\nENDFUNC\n", tcIsPointer(tcFuncType) ? "RETP" : "RET");
}

void tc_local(const char* start, const char* end) {
	if (tcLocalCount >= MAX_LOCALS) { fprintf(stderr, "qcc: too many locals\n"); tcSemanticErrors++; return; }
	tcCopy(tcNames[tcLocalCount], start, end);
	tcLocalTypes[tcLocalCount] = tcCurrentType;
	if (!tcIsPointer(tcLocalTypes[tcLocalCount]) && tcLocalTypes[tcLocalCount].base == 'v') {
		fprintf(stderr, "qcc: void is not a valid variable type\n"); tcSemanticErrors++;
		tcLocalTypes[tcLocalCount] = tcCurrentType = tcMakeType('i', 0);
	}
	/* NUR 0 hier -- die tatsaechliche Array-Laenge (falls "[N]" folgt) wird erst
	   in tc_localdecl bekannt (siehe dort). Frueher wurde hier faelschlich die
	   FELDANZAHL des structs als Array-Laenge zweckentfremdet (bedeutungslos
	   fuer "ist das ein Array" -- siehe FORTSCHRITT.md "Bewusst offen" fuer den
	   2026-07-25 gefundenen Bug, den das hier behebt). */
	tcLocalArrayLen[tcLocalCount] = 0;
	tcLocalArrayNDims[tcLocalCount] = 1;
	/* siehe tc_param: "const" auf einem Pointertyp ist Pointee-Constness (pointeeConst-Bit),
	   nicht Bindungs-Immutabilitaet. */
	tcLocalConst[tcLocalCount] = tcPendingConst && !tcIsPointer(tcLocalTypes[tcLocalCount]);
	if (tcPendingConst && tcIsPointer(tcLocalTypes[tcLocalCount])) tcLocalTypes[tcLocalCount].pointeeConst = 1;
	tcPendingConst = 0;
	tcLocalCount++;
}

/* Struct-Locals (2026-07-25 repariert): frueher wurde hier IMMER genau EIN
   Struct alloziert, ein evtl. vorhandenes "[N]"-Suffix komplett ignoriert --
   "struct Rec arr[3];" reservierte nur Platz fuer 1 statt 3 Elemente (stiller
   Speicherfehler). Jetzt wird wie bei jedem anderen Typ nach "[" gescannt;
   Array von structs bleibt bewusst EINDIMENSIONAL (wie structField/paramArray
   -- ein 2D-Array von structs ist ein sauberer Parse-/Semantikfehler statt
   still falsch geschnitten). */
void tc_localdecl(const char* start, const char* end) {
	const char* p = start; int slot = tcLocalCount - 1;
	int dims[TC_MAXDIMS]; int ndims = 0; int total; int k;
	int isStruct = tcLocalTypes[slot].base == 's';
	int structSize = isStruct ? tcStructByteSize[tcLocalTypes[slot].structId - 1] : 0;
	while (p < end && *p != '[') p++;
	if (p == end) {
		if (isStruct) printf("LARRAY %d c %d\n", slot, structSize);
		return;
	}
	while (p < end && *p == '[') {
		int len = 0;
		p++;
		while (p < end && *p >= '0' && *p <= '9') len = len * 10 + (*p++ - '0');
		if (len <= 0) { fprintf(stderr, "qcc: array size must be positive\n"); return; }
		if (p >= end || *p != ']') { fprintf(stderr, "qcc: bad array declaration\n"); return; }
		p++;
		if (ndims >= TC_MAXDIMS) { fprintf(stderr, "qcc: too many array dimensions (max %d)\n", TC_MAXDIMS); return; }
		dims[ndims++] = len;
	}
	if (isStruct && ndims > 1) {
		fprintf(stderr, "qcc: multi-dimensional struct arrays not supported in this version\n");
		tcSemanticErrors++; return;
	}
	total = dims[0];
	for (k = 1; k < ndims; k++) { total *= dims[k]; tcLocalArrayDims[slot][k - 1] = dims[k]; }
	tcLocalArrayNDims[slot] = ndims;
	tcLocalArrayLen[slot] = total;
	if (isStruct) printf("LARRAY %d c %d\n", slot, total * structSize);
	else printf("LARRAY %d %c %d\n", slot, tcTypeTag(tcLocalTypes[slot]), total);
}

void tc_staticlocalname(const char* start, const char* end) {
	tcCopy(tcStaticLocalName, start, end);
}

/* staticRuntimeInit = expr . -- feuert NUR, wenn staticInit ueber den expr-Zweig
   gematcht hat (der globalValue-Zweig hat KEINE Aktion, siehe tc_staticlocal),
   also genau das Signal, das tc_staticlocal braucht, um zwischen den beiden
   Faellen zu unterscheiden. Der Ausdruckswert liegt zu diesem Zeitpunkt bereits
   auf dem Operandenstack (normale expr-Auswertung, wie bei jeder anderen
   Zuweisung) -- tc_staticlocal emittiert den eigentlichen Runs-once-Guard erst
   danach. */
void tc_staticruntimeinit(const char* start, const char* end) {
	TCType exprType = tcTypePop(); (void)start; (void)end;
	if (!tcCompatible(tcCurrentType, exprType)) tcTypeError("static initializer", tcCurrentType, exprType);
	tcStaticRuntimeInitPending = 1;
}

/* "static" lokale Variable: KEIN Frame-Slot (tcNames/tcLocalTypes bleiben unberuehrt),
   sondern eine ganz normale globale Variable unter ihrem Klarnamen -- dadurch
   greifen tc_target/tc_varref/tcLoadTarget/tc_preincdec/tc_postincdec/addressRef/
   sizeof(variable) automatisch ueber den bereits vorhandenen tcLookupGlobal-Pfad
   zu, ohne dass eine einzige dieser Stellen geaendert werden muss (siehe
   docs/FORTSCHRITT.md fuer die Namensraum-Einschraenkung: kein Funktions-Scoping,
   zwei Funktionen duerfen noch keine gleichnamige static-Variable haben).
   Konstanter Initialisierer (globalValue: Zahl/Negativ/bool-Literal) -- KEINE
   Aktion auf globalValue selbst, also kein Laufzeit-PUSH waehrend des Parsens;
   der Wert wird hier per Rohtext-Scan ab dem "=" extrahiert, genau wie
   tc_globalend es fuer echte globale Variablen tut. Bewusst OHNE struct/Array
   (kein GARRAY-Pfad, Skalare + Pointer, Pointer-Initialisierer muss 0 sein).
   NICHT-konstanter Initialisierer (staticRuntimeInit, seit 2026-07-24, signalisiert
   durch tcStaticRuntimeInitPending): der Rohtext-Scan wird uebersprungen (der
   Ausdruck ist nicht als Ziffernfolge lesbar), die Variable startet mit dem
   neutralen Wert 0 und ein versteckter bool-Flag-Global (__static_init_<name>)
   sichert per Runs-once-Guard (LOADGC/JZ/STOREG.../STOREGC, dieselben Opcodes wie
   bei if/while) zu, dass NUR der ERSTE Aufruf den bereits (unconditional)
   berechneten Ausdruckswert tatsaechlich speichert -- bei allen weiteren Aufrufen
   wird der frisch berechnete, aber ungebrauchte Wert per DROP verworfen. Bewusste
   Vereinfachung: der Ausdruck selbst wird bei JEDEM Aufruf neu ausgewertet (nicht
   wie in echtem ISO C nur einmal) -- bei Seiteneffekten im Ausdruck (z.B. einem
   Funktionsaufruf) weicht das beobachtbare Verhalten von echtem C ab, bei
   seiteneffektfreien Ausdruecken (der ueberwiegende Regelfall) ist es identisch. */
void tc_staticlocal(const char* start, const char* end) {
	int isConst = tcPendingConst; int i, neg = 0; long value = 0; const char* p = start;
	int runtimeInit = tcStaticRuntimeInitPending;
	tcStaticRuntimeInitPending = 0;
	tcPendingConst = 0;
	tcPendingStatic = 0;
	if (tcCurrentType.base == 's') {
		fprintf(stderr, "qcc: static struct locals not yet supported\n");
		tcSemanticErrors++;
		return;
	}
	if (!tcIsPointer(tcCurrentType) && tcCurrentType.base == 'v') {
		fprintf(stderr, "qcc: void is not a valid variable type\n"); tcSemanticErrors++;
		return;
	}
	if (!runtimeInit) {
		while (p < end && *p != '=') p++;
		if (p < end) {
			p++;
			while (p < end && (*p == ' ' || *p == '\t')) p++;
			if (!tcCurrentType.pointers && tcCurrentType.base == 'b' && p + 4 <= end && p[0] == 't') value = 1;
			else {
				if (p < end && *p == '-') { neg = 1; p++; }
				while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
				if (neg) value = -value;
			}
			if (tcCurrentType.pointers && value != 0) {
				fprintf(stderr, "qcc: static local pointer initializer must be 0\n");
				tcSemanticErrors++;
			}
			if (!tcCurrentType.pointers && tcCurrentType.base == 'c') value &= 255;
			if (!tcCurrentType.pointers && tcCurrentType.base == 'b') value = value ? 1 : 0;
		}
	}
	if (tcGlobalCount >= MAX_GLOBALS) { fprintf(stderr, "qcc: too many globals\n"); tcSemanticErrors++; return; }
	for (i = 0; i < tcGlobalCount; i++) {
		if (tcEq(tcGlobalNames[i], tcStaticLocalName)) { fprintf(stderr, "qcc: duplicate global\n"); return; }
	}
	tcCopy(tcGlobalNames[tcGlobalCount], tcStaticLocalName, tcStaticLocalName + strlen(tcStaticLocalName));
	tcGlobalTypes[tcGlobalCount] = tcCurrentType;
	tcGlobalConst[tcGlobalCount] = isConst && !tcIsPointer(tcCurrentType);
	if (isConst && tcIsPointer(tcCurrentType)) tcGlobalTypes[tcGlobalCount].pointeeConst = 1;
	/* static-Lokale sind IMMER unsichtbar fuer andere Dateien (echtes C: interne
	   Verlinkung sogar staerker als eine file-scope static-Variable, siehe
	   docs/STATUS.md). */
	tcGlobalIsStatic[tcGlobalCount] = 1;
	tcGlobalIsDeclOnly[tcGlobalCount] = 0;
	tcGlobalArrayLen[tcGlobalCount++] = 0;
	printf("GLOBAL %s %ld %c 1\n", tcStaticLocalName, value, tcTypeTag(tcCurrentType));
	if (runtimeInit) {
		char flagName[40]; int doInitLabel = tcNextLabel++, afterLabel = tcNextLabel++;
		const char* storeSuffix = tcIsPointer(tcCurrentType) ? "P" : (tcTypeTag(tcCurrentType) == 'i' ? "" : "C");
		sprintf(flagName, "__static_init_%s", tcStaticLocalName);
		printf("GLOBAL %s 0 b 1\n", flagName);
		printf("LOADGC %s\nJZ L%d\n", flagName, doInitLabel);
		printf("DROP\nJMP L%d\n", afterLabel);
		printf("LABEL L%d\n", doInitLabel);
		printf("STOREG%s %s\n", storeSuffix, tcStaticLocalName);
		printf("PUSH 1\nSTOREGC %s\n", flagName);
		printf("LABEL L%d\n", afterLabel);
	}
}

void tc_globalend(const char* start, const char* end) {
const char* p = start; char name[32]; TCType type = tcMakeType('i', 0); int n = 0, neg = 0, arrayLen = 0, arrayNDims = 1, initCount = -1; long value = 0, initValues[256]; int i; int arrayDims[TC_MAXDIMS - 1];
	int isConst = tcPendingConst; int isStatic = 0; tcPendingConst = 0;
	tcPendingStatic = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	/* "static" bei einer globalen Variable: seit Mehrdatei-Uebersetzung echte
	   Bedeutung (Sichtbarkeitssteuerung fuer den Linker in den Backends), siehe
	   docs/STATUS.md -- deshalb hier per Rohtext-Scan erkennen UND merken (nicht
	   nur ueberspringen). */
	if (end - p >= 6 && p[0] == 's' && p[1] == 't' && p[2] == 'a' && p[3] == 't' && p[4] == 'i' && p[5] == 'c') {
		isStatic = 1;
		p += 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (end - p >= 5 && p[0] == 'c' && p[1] == 'o' && p[2] == 'n' && p[3] == 's' && p[4] == 't') {
		p += 5;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	/* struct-Basistyp (2026-07-25, globale struct-Unterstuetzung): mirrort die
	   struct-Erkennung in tc_type. Muss VOR den skalaren Basistyp-Checks stehen,
	   da "struct" selbst kein reservierter Modifikator wie "const"/"static" ist,
	   sondern ein eigener Basistyp. */
	if (end - p >= 6 && p[0] == 's' && p[1] == 't' && p[2] == 'r' && p[3] == 'u' && p[4] == 'c' && p[5] == 't') {
		const char* ne; int sid; p += 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		sid = tcLookupStruct(p, ne);
		if (sid < 0) { fprintf(stderr, "qcc: unknown struct\n"); tcSemanticErrors++; return; }
		type = tcMakeType('s', 0); type.structId = (unsigned char)(sid + 1);
		p = ne;
	}
	/* "unsigned" wird jetzt WORTWEISE ausgewertet statt pauschal 12 Zeichen zu
	   ueberspringen -- das traf nur "unsigned int" und lief bei "unsigned char"
	   (13 Zeichen) mitten in den Bezeichner hinein. */
	else if (end - p >= 8 && p[0] == 'u' && p[1] == 'n' && p[2] == 's' && p[3] == 'i'
	         && p[4] == 'g' && p[5] == 'n' && p[6] == 'e' && p[7] == 'd') {
		const char* q; const char* qe;
		p += 8;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		q = p; qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) { type.base = 'c'; p = qe; }
		else if (tcEqSpan(q, qe, "int") || tcEqSpan(q, qe, "long")) { type.base = 'u'; p = qe; }
		else type.base = 'u';                 /* blosses "unsigned" */
	}
	else if (end - p >= 3 && p[0] == 'i' && p[1] == 'n' && p[2] == 't') p += 3;
	/* long ist auf dem 68k-Ziel wortgleich mit int (32 Bit), type.base bleibt 'i'. */
	else if (end - p >= 4 && p[0] == 'l' && p[1] == 'o' && p[2] == 'n' && p[3] == 'g') p += 4;
	else if (end - p >= 4 && p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'r') { p += 4; type.base = 'c'; }
	else if (end - p >= 4 && p[0] == 'b' && p[1] == 'o' && p[2] == 'o' && p[3] == 'l') { p += 4; type.base = 'b'; }
	else if (end - p >= 4 && p[0] == 'v' && p[1] == 'o' && p[2] == 'i' && p[3] == 'd') { p += 4; type.base = 'v'; }
	else {
		/* Typedef-Name als Basistyp ("TCType x;") -- dieselbe Aufloesung wie in
		   tc_type. Steht ZULETZT, damit die eingebauten Schluesselwoerter nicht
		   als Typedefname missdeutet werden. Ohne diesen Zweig scheiterte JEDE
		   globale Variable eines typedef'ten Typs mit "bad global declaration",
		   obwohl dieselbe Deklaration als LOKALE laengst funktionierte. */
		const char* ne = tcWordEnd(p, end);
		int td = tcLookupTypedef(p, ne);
		if (td < 0) { fprintf(stderr, "qcc: bad global declaration\n"); return; }
		type = tcTypedefTypes[td];
		p = ne;
	}
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p == '*') { type = tcPointerTo(type); p++; while (p < end && (*p == ' ' || *p == '\t')) p++; }
	if (!tcIsPointer(type) && type.base == 'v') {
		fprintf(stderr, "qcc: void is not a valid variable type\n"); tcSemanticErrors++; return;
	}
	while (p < end && *p != ' ' && *p != '\t' && *p != '=' && *p != ';' && *p != '[' && n < 31) name[n++] = *p++;
	name[n] = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p < end && *p == '[') {
		int dims[TC_MAXDIMS]; int ndims = 0; int k;
		while (p < end && *p == '[') {
			int len = 0;
			p++;
			while (p < end && *p >= '0' && *p <= '9') len = len * 10 + (*p++ - '0');
			if (p >= end || *p != ']') { fprintf(stderr, "qcc: bad array declaration\n"); return; }
			p++;
			if (len <= 0) { fprintf(stderr, "qcc: array size must be positive\n"); return; }
			if (ndims >= TC_MAXDIMS) { fprintf(stderr, "qcc: too many array dimensions (max %d)\n", TC_MAXDIMS); return; }
			dims[ndims++] = len;
		}
		arrayLen = dims[0];
		for (k = 1; k < ndims; k++) { arrayLen *= dims[k]; arrayDims[k - 1] = dims[k]; }
		arrayNDims = ndims;
		if (!type.pointers && type.base == 's' && ndims > 1) {
			fprintf(stderr, "qcc: multi-dimensional struct arrays not supported in this version\n");
			tcSemanticErrors++; return;
		}
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (p < end && *p == '=') {
		if (!type.pointers && type.base == 's') {
			fprintf(stderr, "qcc: struct global cannot have an initializer in this version\n");
			tcSemanticErrors++; return;
		}
		p++;
		if (arrayLen) {
			/* String-Literal als Array-Initialisierer (char msg[6] = "hallo";):
			   erst pruefen, ob nach dem "=" (nach Whitespace) ein Anfuehrungszeichen
			   folgt -- tcInitList erkennt nur "{...}", ein Roh-String-Scan (analog
			   zu globalStringInit in der Grammatik, die selbst KEINE eigene ACTION
			   hat) findet hier das schliessende Anfuehrungszeichen (escape-bewusst),
			   dekodiert per tcDecodeStringLit (dieselbe Funktion wie tc_string/
			   tc_arrayinitstring) und speist die Bytes in denselben initValues/
			   initCount-Pfad ein wie ein numerischer initList. */
			const char* q = p; while (q < end && (*q == ' ' || *q == '\t')) q++;
			if (q < end && *q == '"') {
				const char* strEnd = q + 1; unsigned char strBytes[256]; int strLen;
				while (strEnd < end && *strEnd != '"') { if (*strEnd == '\\' && strEnd + 1 < end) strEnd++; strEnd++; }
				if (strEnd < end) strEnd++;
				if (!type.pointers && type.base == 'c') {
					strLen = tcDecodeStringLit(q, strEnd, strBytes, 256);
					if (strLen > arrayLen) { fprintf(stderr, "qcc: string literal too long for array\n"); tcSemanticErrors++; return; }
					initCount = strLen < arrayLen ? strLen + 1 : strLen;
					for (i = 0; i < strLen; i++) initValues[i] = strBytes[i];
					if (strLen < arrayLen) initValues[strLen] = 0;
				} else {
					fprintf(stderr, "qcc: string literal initializer requires a char array\n"); tcSemanticErrors++; return;
				}
			} else {
				initCount = tcInitList(p, end, initValues, 256);
				if (initCount < 0 || initCount > arrayLen) { fprintf(stderr, "qcc: bad or oversized array initializer\n"); return; }
			}
		} else {
			while (p < end && (*p == ' ' || *p == '\t')) p++;
			if (p < end && *p == '{') { fprintf(stderr, "qcc: scalar cannot use array initializer\n"); return; }
			if (!type.pointers && type.base == 'b' && p + 4 <= end && p[0] == 't') value = 1;
			else { if (p < end && *p == '-') { neg = 1; p++; }
				while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
				if (neg) value = -value; }
			if (type.pointers && value != 0) { fprintf(stderr, "qcc: global pointer initializer must be 0\n"); tcSemanticErrors++; return; }
		}
	}
	if (tcGlobalCount >= MAX_GLOBALS) { fprintf(stderr, "qcc: too many globals\n"); tcSemanticErrors++; return; }
	for (i = 0; i < tcGlobalCount; i++) if (tcEq(tcGlobalNames[i], name)) { fprintf(stderr, "qcc: duplicate global\n"); return; }
	tcCopy(tcGlobalNames[tcGlobalCount], name, name + n);
	/* siehe tc_param: "const" auf einem Pointertyp ist Pointee-Constness (pointeeConst-Bit),
	   nicht Bindungs-Immutabilitaet. */
	if (isConst && type.pointers) type.pointeeConst = 1;
	tcGlobalTypes[tcGlobalCount] = type;
	tcGlobalConst[tcGlobalCount] = isConst && !type.pointers;
	tcGlobalIsStatic[tcGlobalCount] = isStatic;
	tcGlobalIsDeclOnly[tcGlobalCount] = 0;
	tcGlobalArrayNDims[tcGlobalCount] = arrayNDims;
	for (i = 0; i < arrayNDims - 1; i++) tcGlobalArrayDims[tcGlobalCount][i] = arrayDims[i];
	tcGlobalArrayLen[tcGlobalCount++] = arrayLen;
	/* globale structs (2026-07-25): wie bei lokalen struct-Variablen (tc_localdecl)
	   braucht ein struct IMMER Block-Speicher (GARRAY, byte-genau ueber tcStructByteSize),
	   NIE die einzellige GLOBAL-Form -- unabhaengig davon, ob ein "[N]"-Suffix dabeisteht
	   (arrayLen==0 fuer eine skalare struct-Variable heisst hier "ein Element", NICHT
	   "kein Block", anders als bei tcLocalArrayLen/tcGlobalArrayLen als Index-Laenge weiter
	   unten -- ADDRG braucht ohnehin IMMER einen Block, siehe qccvm.py: globals_ ist bei
	   GLOBAL wie bei GARRAY einheitlich eine Liste, ADDRG unterscheidet nicht). */
	if (!type.pointers && type.base == 's') {
		int structSize = tcStructByteSize[type.structId - 1];
		int total = (arrayLen > 0 ? arrayLen : 1) * structSize;
		printf("GARRAY %s c %d %d\n", name, total, isStatic);
		return;
	}
	if (arrayLen) {
		printf("GARRAY %s %c %d %d\n", name, tcTypeTag(type), arrayLen, isStatic);
		for (i = 0; i < initCount; i++) printf("GINIT %s %d %ld\n", name, i, !type.pointers && type.base == 'c' ? (initValues[i] & 255) : initValues[i]);
		return;
	}
	if (!type.pointers && type.base == 'c') value &= 255;
	if (!type.pointers && type.base == 'b') value = value ? 1 : 0;
	printf("GLOBAL %s %ld %c %d\n", name, value, tcTypeTag(type), isStatic);
}

/* "extern <typ> <name>;" -- Deklaration OHNE Speicherallokation, definiert in
   einer ANDEREN QCC-Datei. Bewusst EIGENE Aktion (nicht tc_globalend) --
   die Rohtext-Form unterscheidet sich (kein optionales static/const, keine
   Array-Groesse, kein Initialisierer) und die Semantik ist grundverschieden
   (keine Speicherallokation, kein GLOBAL/GARRAY, sondern GLOBALDECL). Siehe
   docs/SELFHOSTING_LUECKENLISTE.md / docs/STATUS.md fuer die Motivation
   (Mehrdatei-Uebersetzung). */
void tc_externglobaldecl(const char* start, const char* end) {
	const char* p = start; char name[32]; TCType type = tcMakeType('i', 0); int n = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (end - p >= 6 && p[0] == 'e' && p[1] == 'x' && p[2] == 't' && p[3] == 'e' && p[4] == 'r' && p[5] == 'n') {
		p += 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (end - p >= 12 && p[0] == 'u') { p += 12; type.base = 'u'; }
	else if (end - p >= 3 && p[0] == 'i' && p[1] == 'n' && p[2] == 't') p += 3;
	else if (end - p >= 4 && p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'r') { p += 4; type.base = 'c'; }
	else if (end - p >= 4 && p[0] == 'b' && p[1] == 'o' && p[2] == 'o' && p[3] == 'l') { p += 4; type.base = 'b'; }
	else if (end - p >= 4 && p[0] == 'v' && p[1] == 'o' && p[2] == 'i' && p[3] == 'd') { p += 4; type.base = 'v'; }
	else { fprintf(stderr, "qcc: bad extern global declaration\n"); return; }
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p == '*') { type = tcPointerTo(type); p++; while (p < end && (*p == ' ' || *p == '\t')) p++; }
	if (!tcIsPointer(type) && type.base == 'v') {
		fprintf(stderr, "qcc: void is not a valid variable type\n"); tcSemanticErrors++; return;
	}
	while (p < end && *p != ' ' && *p != '\t' && *p != ';' && n < 31) name[n++] = *p++;
	name[n] = 0;
	if (tcLookupGlobal(name, name + n) >= 0) { fprintf(stderr, "qcc: duplicate global\n"); tcSemanticErrors++; return; }
	if (tcGlobalCount >= MAX_GLOBALS) { fprintf(stderr, "qcc: too many globals\n"); tcSemanticErrors++; return; }
	tcCopy(tcGlobalNames[tcGlobalCount], name, name + n);
	tcGlobalTypes[tcGlobalCount] = type;
	tcGlobalConst[tcGlobalCount] = 0;
	tcGlobalArrayNDims[tcGlobalCount] = 1;
	tcGlobalIsStatic[tcGlobalCount] = 0;
	tcGlobalIsDeclOnly[tcGlobalCount] = 1;
	tcGlobalArrayLen[tcGlobalCount++] = 0;
	printf("GLOBALDECL %s %c\n", name, tcTypeTag(type));
}

void tc_varinit(const char* start, const char* end) {
	int slot = tcLocalCount - 1, count, i; long values[256];
	if (slot >= 0 && tcLocalArrayLen[slot]) {
		/* String-Literal als Array-Initialisierer (char msg[6] = "hallo";):
		   bereits vollstaendig von tc_arrayinitstring behandelt (arrayStringInit
		   wird VOR expr probiert, siehe Grammatik) -- hier nur noch erkennen und
		   NICHT nochmal ueber tcInitList (das faelschlich "kein '{'" meldete). */
		{ const char* q = start; while (q < end && (*q == ' ' || *q == '\t')) q++; if (q < end && *q == '"') return; }
		count = tcInitList(start, end, values, 256);
		if (count < 0 || count > tcLocalArrayLen[slot]) { fprintf(stderr, "qcc: bad or oversized array initializer\n"); return; }
		for (i = 0; i < count; i++) printf("PUSH %d\nPUSH %ld\nSTOREIDX L %d %c\n", i, !tcLocalTypes[slot].pointers && tcLocalTypes[slot].base == 'c' ? (values[i] & 255) : values[i], slot, tcTypeTag(tcLocalTypes[slot]));
		return;
	}
	if (tcInitList(start, end, values, 256) >= 0) { fprintf(stderr, "qcc: scalar cannot use array initializer\n"); return; }
	{ TCType got = tcTypePop(), wanted = tcLocalType(slot); if (!tcCompatible(wanted, got)) tcTypeError("initializer", wanted, got); }
	printf("STORE%s %d\n", tcIsPointer(tcLocalType(slot)) ? "P" : tcTypeTag(tcLocalType(slot)) == 'i' ? "L" : "C", slot);
}

void tc_number(const char* start, const char* end) {
	if (end - start == 4 && start[0] == 't') { printf("PUSH 1\n"); tcTypePush(tcMakeType('b', 0)); }
	else if (end - start == 5 && start[0] == 'f') { printf("PUSH 0\n"); tcTypePush(tcMakeType('b', 0)); }
	else { long value = tcNum(start, end); printf("PUSH %ld\n", value); tcTypePush(tcMakeType(value == 0 ? 'z' : 'i', 0)); }
}

/* Escapes eines String-Literals (start zeigt auf das oeffnende, end hinter das
   schliessende Anfuehrungszeichen) nach bytes[] dekodieren: \n \t \r \0 sowie
   \" und \\ (Fallback fuer unbekannte \x: x woertlich). Gemeinsam genutzt von
   tc_string (normaler Ausdruckskontext) UND tc_arrayinitstring (Array-
   Initialisierer) -- gleiche Regeln, ein Ort. */
static int tcDecodeStringLit(const char* start, const char* end, unsigned char* bytes, int cap) {
	int len = 0;
	const char* p = start + 1; const char* stop = end - 1;
	while (p < stop) {
		unsigned char c;
		if (*p == '\\' && p + 1 < stop) {
			char esc = p[1]; p += 2;
			c = esc == 'n' ? 10 : esc == 't' ? 9 : esc == 'r' ? 13 : esc == '0' ? 0 : (unsigned char)esc;
		} else {
			c = (unsigned char)*p++;
		}
		if (len >= cap) { fprintf(stderr, "qcc: string literal too long (max %d bytes)\n", cap); tcSemanticErrors++; break; }
		bytes[len++] = c;
	}
	return len;
}
/* String-Literal: erzeugt einen anonymen globalen char-Array-Konstant (__strN, GARRAY/
   GINIT + nullterminierendes Byte) und liefert dessen Adresse (ADDRG) als char* --
   dieselben IR-Opcodes wie ein initialisiertes globales char-Array, siehe Grammatik-
   Kommentar bei stringLit. */
void tc_string(const char* start, const char* end) {
	unsigned char bytes[256]; int len, i, id;
	len = tcDecodeStringLit(start, end, bytes, 256);
	id = tcStringCounter++;
	printf("GARRAY __str%d c %d 1\n", id, len + 1);
	for (i = 0; i < len; i++) printf("GINIT __str%d %d %d\n", id, i, bytes[i]);
	printf("GINIT __str%d %d 0\n", id, len);
	printf("ADDRG __str%d\n", id);
	tcTypePush(tcMakeType('c', 1));
}

/* String-Literal als Array-Initialisierer (char msg[6] = "hallo";). Feuert
   NACH tc_string (arrayStringInit umschliesst stringLit direkt, siehe
   Grammatik-Kommentar) -- tc_string hat also bereits eine anonyme
   GARRAY-Konstante emittiert und deren Adresse als char* auf den Stack
   gelegt. Fuer ein SKALARES Ziel (char* p = "..";) ist genau das schon die
   komplette Arbeit -- diese Routine tut dann NICHTS, tc_varinit uebernimmt
   die Adresse ganz normal (STOREP). Fuer ein ARRAY-Ziel wird die Adresse
   NICHT gebraucht (echte C-Semantik kopiert die BYTES, nicht die Adresse
   des anonymen Konstanten) -- DROP verwirft sie, dann werden die Bytes per
   PUSH/PUSH/STOREIDX direkt in die lokalen Array-Slots geschrieben, exakt
   wie im numerischen initList-Zweig von tc_varinit. Laenge muss in den
   deklarierten Array passen (len, oder len+1 mit Nullterminator, falls
   Platz ist) -- wie bei echtem C ist "char m[5]=\"hallo\";" (exakt passend,
   OHNE Nullterminator) erlaubt, "char m[4]=\"hallo\";" dagegen ein Fehler. */
void tc_arrayinitstring(const char* start, const char* end) {
	int slot = tcLocalCount - 1;
	unsigned char bytes[256]; int len, i;
	if (slot < 0 || !tcLocalArrayLen[slot]) return;
	if (tcIsPointer(tcLocalTypes[slot]) || tcLocalTypes[slot].base != 'c') {
		fprintf(stderr, "qcc: string literal initializer requires a char array\n"); tcSemanticErrors++; return;
	}
	len = tcDecodeStringLit(start, end, bytes, 256);
	if (len > tcLocalArrayLen[slot]) { fprintf(stderr, "qcc: string literal too long for array\n"); tcSemanticErrors++; return; }
	(void)tcTypePop();
	printf("DROP\n");
	for (i = 0; i < len; i++) printf("PUSH %d\nPUSH %d\nSTOREIDX L %d c\n", i, bytes[i], slot);
	if (len < tcLocalArrayLen[slot]) printf("PUSH %d\nPUSH 0\nSTOREIDX L %d c\n", len, slot);
}

void tc_neg(const char* start, const char* end) {
	TCType operand = tcTypePop();
	(void)end;
	if (*start == '!') {
		if (!tcIsTruthy(operand)) tcTypeError("logical negation", tcMakeType('b', 0), operand);
		tcTypePush(tcMakeType('b', 0)); printf("NOT\n");
	} else if (*start == '~') {
		if (!tcIsInteger(operand)) tcTypeError("bitwise negation", tcMakeType('i', 0), operand);
		tcTypePush(tcMakeType(operand.base == 'u' ? 'u' : 'i', 0)); printf("NOTBIT\n");
	} else {
		if (!tcIsInteger(operand)) tcTypeError("negation", tcMakeType('i', 0), operand);
		tcTypePush(tcMakeType(operand.base == 'u' ? 'u' : 'i', 0)); printf("NEG\n");
	}
}

void tc_varref(const char* start, const char* end) {
	const char* nameEnd = tcNameEnd(start, end); int indexed = nameEnd < end;
	int slot = tcLookupLocal(start, nameEnd), global;
	/* Die Grammatik erlaubt seit 2026-07-25 "ident [index...] [.member...]" als SEQUENZ
	   (vorher eine Alternation -- "arr[i].feld" haette gar nicht geparst). Das oeffnet
	   grammatisch auch Kombinationen, die (noch) KEINE Codegen-Unterstuetzung haben --
	   z.B. eine Pointer-auf-struct-Variable indiziert und dann ein Feld ("ptr[i].feld",
	   noetig fuer den Selfhosting-Piloten -- routinesC ist ActionRoutine*, kein festes
	   Array -- aber noch NICHT gebaut, siehe SELFHOSTING_LUECKENLISTE.md) oder ein
	   indiziertes Nicht-struct mit einem sinnlosen Feldanhang. OHNE diesen Schutz wuerden
	   die bestehenden Index-Zweige weiter unten die "."-Fortsetzung STILLSCHWEIGEND
	   ignorieren (stiller Fehlcode statt Diagnose) -- deshalb hier VOR jedem
	   spezifischen Zweig ablehnen, ausser fuer die ZWEI Kombinationen, die tatsaechlich
	   gebaut sind (lokales, als Array deklariertes struct; lokale Pointer-auf-struct-
	   Variable, siehe naechster Block -- Letzteres seit 2026-07-25 fuer Milestone B). */
	global = slot < 0 ? tcLookupGlobal(start, nameEnd) : -1;
	if (indexed && *nameEnd == '[' &&
	    !(slot >= 0 && tcLocalArrayLen[slot] > 0 && tcLocalTypes[slot].base == 's') &&
	    !(slot >= 0 && tcIsPointer(tcLocalTypes[slot]) && tcPointee(tcLocalTypes[slot]).base == 's') &&
	    !(global >= 0 && tcGlobalArrayLen[global] > 0 && tcGlobalType(global).base == 's') &&
	    !(global >= 0 && tcIsPointer(tcGlobalType(global)) && tcPointee(tcGlobalType(global)).base == 's')) {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			fprintf(stderr, "qcc: indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct, in this version\n");
			tcSemanticErrors++; tcTypePush(tcBadType()); return;
		}
	}
	/* ptr[i].feld (2026-07-25, Milestone B): eine LOKALE Pointer-auf-struct-Variable,
	   indiziert, dann Feldzugriff -- braucht der Selfhosting-Pilot fuer routinesC[i].name/
	   .text (ActionRoutine*, ein malloc/realloc-gewachsenes Array, kein festes lokales
	   Array wie beim bereits fertigen arr[i].feld oben). Stack-Reihenfolge identisch zum
	   Array-Fall, nur PUSHADDR (Blockadresse) durch LOADP (geladener Pointer-WERT) ersetzt --
	   IPADDN skaliert genauso um die Laufzeit-Byte-Groesse des Elements. */
	if (slot >= 0 && indexed && *nameEnd == '[' && tcIsPointer(tcLocalTypes[slot]) && tcPointee(tcLocalTypes[slot]).base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcPointee(tcLocalTypes[slot]).structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: ptr[i].field[j] not supported in this version\n");
				tcSemanticErrors++; tcTypePush(tcBadType()); return;
			}
			printf("LOADP %d\nIPADDN %d\nPUSH %d\nPADD c\n", slot, structSize, tcStructFieldOffset[sid][fi]);
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	/* arr[i].feld (2026-07-25): ein ARRAY von structs, indiziert, dann Feldzugriff --
	   siehe tcSkipOneIndex/IPADDN-Kommentar in qcc.ebnf. tcLocalArrayLen[slot] > 0
	   bedeutet seit der Allokations-Reparatur (siehe tc_local/tc_localdecl) jetzt
	   WIRKLICH "als Array deklariert", nicht mehr die Feldanzahl-Verwechslung von
	   frueher -- die Bedingung ist damit endlich korrekt auswertbar. */
	if (slot >= 0 && indexed && *nameEnd == '[' && tcLocalArrayLen[slot] > 0 && tcLocalTypes[slot].base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcLocalTypes[slot].structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: arr[i].field[j] not supported in this version\n");
				tcSemanticErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcLocalArrayLen[slot]);
			/* Index bereits gepusht (vor uns, durch die index-ACTION). PUSHADDR liefert die
			   Blockadresse des GESAMTEN Arrays; IPADDN skaliert den Index um die LAUFZEIT-
			   Byte-Groesse eines Elements (structSize, beliebig -- anders als IPADD, das nur
			   feste Typtag-Groessen kennt); danach PUSH+PADD c addiert den (konstanten,
			   byte-genauen) Feldoffset, exakt wie beim bestehenden Skalar-Feldzugriff oben. */
			printf("PUSHADDR L %d\nIPADDN %d\nPUSH %d\nPADD c\n", slot, structSize, tcStructFieldOffset[sid][fi]);
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	/* p->feld (2026-08-10): identisch zu (*p).feld, braucht also KEINEN Index --
	   schlicht Feldadresse = Zeigerwert + Feldoffset. IPADD poppt den Zeiger
	   zuerst (muss oben liegen), deshalb PUSH offset VOR dem Laden des Zeigers.
	   Ein Array-Feld liefert wie ueberall dessen ADRESSE statt eines Wertes. */
	if (indexed && *nameEnd == '-' && nameEnd + 1 < end && nameEnd[1] == '>') {
		TCType pt = slot >= 0 ? tcLocalTypes[slot] : (global >= 0 ? tcGlobalType(global) : tcBadType());
		const char* fieldStart = nameEnd + 2; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int sid, fi;
		if ((slot < 0 && global < 0) || !tcIsPointer(pt) || tcPointee(pt).base != 's') {
			fprintf(stderr, "qcc: '->' requires a pointer to struct\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return;
		}
		sid = tcPointee(pt).structId - 1;
		fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
		printf("PUSH %d\n", tcStructFieldOffset[sid][fi]);
		if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", tcGlobalNames[global]);
		printf("IPADD c\n");
		if (fieldEnd < end && *fieldEnd == '[') {
			if (!tcStructFieldArrayLen[sid][fi]) {
				fprintf(stderr, "qcc: scalar struct field cannot be indexed\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			printf("IPADD %c\nLOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]), tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	if (slot >= 0 && indexed && *nameEnd == '.' && tcLocalTypes[slot].base == 's') {
		int sid = tcLocalTypes[slot].structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
		/* IPADD poppt Pointer ZUERST (muss oben liegen), dann Count -- daher PUSH vor PUSHADDR. */
		printf("PUSH %d\nPUSHADDR L %d\nIPADD c\n", tcStructFieldOffset[sid][fi], slot);
		if (hasIndex) {
			/* p.field[i] (2026-07-24): der Index-Ausdruck hat seinen Wert bereits VOR uns
			   gepusht (ACTION AFTER index CALL tc_arg feuert vor dem umschliessenden
			   varRef) -- der Stack traegt hier [Indexwert, Feldadresse] (Feldadresse gerade
			   eben on top gepusht durch das IPADD oben), exakt die Reihenfolge, die ein
			   zweites IPADD braucht. */
			if (!tcStructFieldArrayLen[sid][fi]) {
				fprintf(stderr, "qcc: scalar struct field cannot be indexed\n"); tcSemanticErrors++;
				tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			printf("IPADD %c\nLOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]), tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	/* Globale structs (2026-07-25): dieselben drei Muster wie oben (Pointer-auf-struct
	   indiziert, Array-von-structs indiziert, direkter Member-Zugriff), aber fuer GLOBALE
	   Variablen -- ADDRG/LOADGP statt PUSHADDR L/LOADP. ADDRG und PUSHADDR L sind hier
	   austauschbar (beide liefern denselben Blockzeiger, siehe qccvm.py: globals_ ist bei
	   GLOBAL wie bei GARRAY einheitlich eine Liste) -- PUSHADDR G verwendet, um optisch
	   parallel zum lokalen Muster zu bleiben. */
	if (global >= 0 && indexed && *nameEnd == '[' && tcIsPointer(tcGlobalType(global)) && tcPointee(tcGlobalType(global)).base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcPointee(tcGlobalType(global)).structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: ptr[i].field[j] not supported in this version\n");
				tcSemanticErrors++; tcTypePush(tcBadType()); return;
			}
			printf("LOADGP %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	if (global >= 0 && indexed && *nameEnd == '[' && tcGlobalArrayLen[global] > 0 && tcGlobalType(global).base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcGlobalType(global).structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: arr[i].field[j] not supported in this version\n");
				tcSemanticErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcGlobalArrayLen[global]);
			printf("PUSHADDR G %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	if (global >= 0 && indexed && *nameEnd == '.' && tcGlobalType(global).base == 's') {
		char gname[32]; int sid = tcGlobalType(global).structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		tcCopy(gname, start, nameEnd);
		if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
		printf("PUSH %d\nPUSHADDR G %s\nIPADD c\n", tcStructFieldOffset[sid][fi], gname);
		if (hasIndex) {
			if (!tcStructFieldArrayLen[sid][fi]) {
				fprintf(stderr, "qcc: scalar struct field cannot be indexed\n"); tcSemanticErrors++;
				tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			printf("IPADD %c\nLOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]), tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	if (slot >= 0) {
		if (tcLocalArrayLen[slot]) {
			if (!indexed) {
				printf("PUSHADDR L %d\n", slot); tcTypePush(tcPointerTo(tcLocalType(slot))); return;
			}
			tcCheckNDIndex(tcLocalArrayNDims[slot], tcLocalArrayDims[slot], tcCountTopIndexes(nameEnd, end));
			tcCheckConstIndex(start, end, tcLocalArrayLen[slot]);
			printf("LOADIDX L %d %c\n", slot, tcTypeTag(tcLocalType(slot))); tcTypePush(tcLocalType(slot));
		} else if (indexed && tcIsPointer(tcLocalType(slot))) {
			TCType valueType = tcPointee(tcLocalType(slot));
			if (!tcIsPointer(valueType) && valueType.base == 'v') {
				fprintf(stderr, "qcc: cannot dereference void*\n"); tcSemanticErrors++; tcTypePush(tcMakeType('i', 0)); return;
			}
			printf("LOADP %d\nPTRINDEX %c\nLOADIND %c\n", slot, tcTypeTag(valueType), tcTypeTag(valueType)); tcTypePush(valueType);
		} else if (indexed) { fprintf(stderr, "qcc: scalar variable cannot be indexed\n"); tcSemanticErrors++; }
		else { TCType t = tcLocalType(slot); printf("LOAD%s %d\n", tcIsPointer(t) ? "P" : tcTypeTag(t) == 'i' ? "L" : "C", slot); tcTypePush(t); }
	} else if ((global = tcLookupGlobal(start, nameEnd)) >= 0) {
		char name[32]; tcCopy(name, start, nameEnd);
		if (tcGlobalArrayLen[global]) {
			if (!indexed) {
				printf("PUSHADDR G %s\n", name); tcTypePush(tcPointerTo(tcGlobalType(global))); return;
			}
			tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, end));
			tcCheckConstIndex(start, end, tcGlobalArrayLen[global]);
			printf("LOADIDX G %s %c\n", name, tcTypeTag(tcGlobalType(global))); tcTypePush(tcGlobalType(global));
		} else if (indexed && tcIsPointer(tcGlobalType(global))) {
			TCType valueType = tcPointee(tcGlobalType(global));
			if (!tcIsPointer(valueType) && valueType.base == 'v') {
				fprintf(stderr, "qcc: cannot dereference void*\n"); tcSemanticErrors++; tcTypePush(tcMakeType('i', 0)); return;
			}
			printf("LOADGP %s\nPTRINDEX %c\nLOADIND %c\n", name, tcTypeTag(valueType), tcTypeTag(valueType)); tcTypePush(valueType);
		} else if (indexed) { fprintf(stderr, "qcc: scalar variable cannot be indexed\n"); tcSemanticErrors++; }
		else { TCType t = tcGlobalType(global); printf("LOADG%s %s\n", tcIsPointer(t) ? "P" : tcTypeTag(t) == 'i' ? "" : "C", name); tcTypePush(t); }
	}
	else {
		int ec = tcLookupEnumConst(start, nameEnd);
		int fnv;
		if (ec >= 0 && !indexed) { printf("PUSH %ld\n", tcEnumConstValues[ec]); tcTypePush(tcMakeType('i', 0)); }
		/* Ein blosser Funktionsname AUSSERHALB eines Aufrufs ist sein eigener
		   Zeiger (implizites "&" wie in echtem C: "push(tc_foo)"). Dieser Zweig
		   wird nur erreicht, wenn der Name weder lokale noch globale Variable
		   noch Enum-Konstante ist -- ein echter Aufruf "foo(...)" laeuft ueber
		   die call-Regel und kommt hier gar nicht an. Der Typ ist ein
		   Funktionszeiger mit der Signatur der Funktion; sie wird bei Bedarf
		   angelegt, damit auch Funktionen ohne passendes typedef zuweisbar
		   bleiben. */
		else if (!indexed && (fnv = tcLookupFunction2(start, nameEnd)) >= 0) {
			int sig = tcFnSigForFunction(fnv);
			printf("PUSHFN %s\n", tcFunctionNames[fnv]);
			tcTypePush(sig >= 0 ? tcMakeFnPtr(sig) : tcBadType());
		}
		else { fprintf(stderr, "qcc: unknown variable\n"); tcSemanticErrors++; }
	}
}

void tc_addressref(const char* start, const char* end) {
	const char* name = start + 1; const char* nameEnd = tcNameEnd(name, end); int indexed = nameEnd < end;
	int slot = tcLookupLocal(name, nameEnd), global = tcLookupGlobal(name, nameEnd); TCType valueType;
	if (slot >= 0) {
		valueType = tcLocalType(slot);
		if (indexed) {
			if (tcLocalArrayLen[slot]) { tcCheckConstIndex(name, end, tcLocalArrayLen[slot]); printf("PUSHADDR L %d\n", slot); }
			else if (tcIsPointer(valueType)) { valueType = tcPointee(valueType); printf("LOADP %d\n", slot); }
			else { fprintf(stderr, "qcc: scalar variable cannot be indexed\n"); tcSemanticErrors++; return; }
			printf("PTRINDEX %c\n", tcTypeTag(valueType)); tcTypePush(tcPointerTo(valueType)); return;
		}
		if (tcLocalArrayLen[slot]) printf("PUSHADDR L %d\n", slot); else printf("ADDRL %d\n", slot);
		tcTypePush(tcPointerTo(valueType)); return;
	}
	if (global >= 0) {
		char globalName[32]; valueType = tcGlobalType(global); tcCopy(globalName, name, nameEnd);
		if (indexed) {
			if (tcGlobalArrayLen[global]) { tcCheckConstIndex(name, end, tcGlobalArrayLen[global]); printf("PUSHADDR G %s\n", globalName); }
			else if (tcIsPointer(valueType)) { valueType = tcPointee(valueType); printf("LOADGP %s\n", globalName); }
			else { fprintf(stderr, "qcc: scalar variable cannot be indexed\n"); tcSemanticErrors++; return; }
			printf("PTRINDEX %c\n", tcTypeTag(valueType)); tcTypePush(tcPointerTo(valueType)); return;
		}
		printf("ADDRG %s\n", globalName); tcTypePush(tcPointerTo(valueType)); return;
	}
	fprintf(stderr, "qcc: unknown variable in address expression\n"); tcSemanticErrors++;
}

void tc_derefref(const char* start, const char* end) {
	TCType pointer = tcTypePop(), valueType; (void)start; (void)end;
	if (!tcIsPointer(pointer)) { tcTypeError("dereference", tcPointerTo(tcMakeType('i', 0)), pointer); tcTypePush(tcMakeType('i', 0)); return; }
	valueType = tcPointee(pointer);
	if (!tcIsPointer(valueType) && valueType.base == 'v') {
		fprintf(stderr, "qcc: cannot dereference void*\n"); tcSemanticErrors++;
		tcTypePush(tcMakeType('i', 0)); return;
	}
	printf("LOADIND %c\n", tcTypeTag(valueType)); tcTypePush(valueType);
}

void tc_postfixindex(const char* start, const char* end) {
	/* postfixIndex = index . -- die verschachtelte index-Regel hat ihren eigenen
	   Indexwert bereits VOR uns ausgewertet und wieder vom Typstack entfernt
	   (ACTION AFTER index CALL tc_arg, gleicher Mechanismus wie bei "arr[i]");
	   hier liegt deshalb nur noch der Pointer-Typ von call/stringLit obenauf.
	   Laufzeitreihenfolge auf dem Stack: [Pointer (von call/stringLit zuerst
	   gepusht), Indexwert (von index zuletzt gepusht)] -- exakt dieselbe
	   Reihenfolge wie bei "p + n" (tc_term), daher PADD (kein PTRINDEX/IPADD,
	   die den Pointer ZUERST erwarten) gefolgt von LOADIND. */
	TCType pointer = tcTypePop(), valueType; (void)start; (void)end;
	if (!tcIsPointer(pointer)) { tcTypeError("index", tcPointerTo(tcMakeType('i', 0)), pointer); tcTypePush(tcMakeType('i', 0)); return; }
	valueType = tcPointee(pointer);
	if (!tcIsPointer(valueType) && valueType.base == 'v') {
		fprintf(stderr, "qcc: cannot index void*\n"); tcSemanticErrors++;
		tcTypePush(tcMakeType('i', 0)); return;
	}
	printf("PADD %c\nLOADIND %c\n", tcTypeTag(valueType), tcTypeTag(valueType)); tcTypePush(valueType);
}

void tc_addop(const char* start, const char* end) {
	(void)end;
	tcPendingAdd = *start;
}

void tc_term(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcPendingAdd) {
		TCType right = tcTypePop(), left = tcTypePop();
		/* void* traegt keine Elementgroesse -- Zeigerarithmetik/-differenz waere sonst
		   still (und falsch) mit Groesse 4 skaliert (tcTypeTag faellt fuer 'v' auf 'i'
		   zurueck). Bewusst wie echtes C behandelt: keine Arithmetik auf void*. */
		int leftVoidPtr = tcIsPointer(left) && !tcIsPointer(tcPointee(left)) && tcPointee(left).base == 'v';
		int rightVoidPtr = tcIsPointer(right) && !tcIsPointer(tcPointee(right)) && tcPointee(right).base == 'v';
		if (tcIsPointer(left) && tcIsInteger(right) && !leftVoidPtr) {
			printf("P%s %c\n", tcPendingAdd == '+' ? "ADD" : "SUB", tcTypeTag(tcPointee(left))); tcTypePush(left);
		} else if (tcPendingAdd == '+' && tcIsInteger(left) && tcIsPointer(right) && !rightVoidPtr) {
			printf("IPADD %c\n", tcTypeTag(tcPointee(right))); tcTypePush(right);
		} else if (tcPendingAdd == '-' && tcIsPointer(left) && tcIsPointer(right) && tcSameType(left, right) && !leftVoidPtr) {
			printf("PDIFF %c\n", tcTypeTag(tcPointee(left))); tcTypePush(tcMakeType('i', 0));
		} else if (tcIsInteger(left) && tcIsInteger(right)) {
			tcTypePush(tcPromoteInteger(left, right)); printf("%s", tcPendingAdd == '+' ? "ADD\n" : "SUB\n");
		} else if (leftVoidPtr || rightVoidPtr) {
			fprintf(stderr, "qcc: arithmetic on void* is not supported\n"); tcSemanticErrors++; tcTypePush(left);
		} else {
			tcTypeError("arithmetic", left, right); tcTypePush(left);
		}
		tcPendingAdd = 0;
	}
}

void tc_mulop(const char* start, const char* end) {
	(void)end;
	tcPendingMul = *start;
}

void tc_factor(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcPendingMul) {
		TCType right = tcTypePop(), left = tcTypePop();
		if (!tcIsInteger(left) || !tcIsInteger(right)) tcTypeError("arithmetic", tcMakeType('i', 0), !tcIsInteger(left) ? left : right);
		tcTypePush(tcPromoteInteger(left, right));
		printf("%s", tcPendingMul == '*' ? "MUL\n" : tcPendingMul == '%' ? (left.base == 'u' || right.base == 'u') ? "UMOD\n" : "MOD\n" : (left.base == 'u' || right.base == 'u') ? "UDIV\n" : "DIV\n");
		tcPendingMul = 0;
	}
}

void tc_relop(const char* start, const char* end) {
	tcRel0 = start[0];
	tcRel1 = (end - start > 1) ? start[1] : 0;
}

void tc_expr(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcRel0) {
		TCType right = tcTypePop(), left = tcTypePop(); int pointerCompare = tcIsPointer(left) || tcIsPointer(right);
		if (pointerCompare && !(tcSameType(left, right) || (tcIsPointer(left) && right.base == 'z' && !right.pointers) || (tcIsPointer(right) && left.base == 'z' && !left.pointers))) tcTypeError("pointer comparison", left, right);
		else if (!pointerCompare && !((tcIsInteger(left) || tcIsBool(left)) && (tcIsInteger(right) || tcIsBool(right)))) tcTypeError("comparison", left, right);
		tcTypePush(tcMakeType('b', 0));
		if (pointerCompare) {
			if (tcRel1 == '=' && tcRel0 == '=') printf("PCMPEQ\n");
			else if (tcRel1 == '=' && tcRel0 == '!') printf("PCMPNE\n");
			else if (tcRel0 == '<') printf(tcRel1 == '=' ? "PCMPLE\n" : "PCMPLT\n");
			else if (tcRel0 == '>') printf(tcRel1 == '=' ? "PCMPGE\n" : "PCMPGT\n");
		} else if (tcRel1 == '=') {
			if (tcRel0 == '<') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPULE\n" : "CMPLE\n");
			else if (tcRel0 == '>') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPUGE\n" : "CMPGE\n");
			else if (tcRel0 == '=') printf("CMPEQ\n");
			else if (tcRel0 == '!') printf("CMPNE\n");
		} else {
			if (tcRel0 == '<') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPULT\n" : "CMPLT\n");
			else if (tcRel0 == '>') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPUGT\n" : "CMPGT\n");
		}
		tcRel0 = 0; tcRel1 = 0;
	}
}

void tc_target(const char* start, const char* end) {
	const char* nameEnd = tcNameEnd(start, end);
	int global;
	tcTargetIsArray = nameEnd < end;
	tcTargetSlot = tcLookupLocal(start, nameEnd);
	tcTargetIsGlobal = 0; tcTargetIndirect = 0;
	tcTargetType = tcLocalType(tcTargetSlot);
	if (tcTargetSlot >= 0 && tcLocalConst[tcTargetSlot]) {
		fprintf(stderr, "qcc: cannot assign to const variable\n"); tcSemanticErrors++;
	}
	/* siehe tc_varref fuer die vollstaendige Erklaerung -- die Grammatik erlaubt jetzt
	   Kombinationen (z.B. "ptr[i].feld = ..") ohne Codegen-Unterstuetzung; ohne diesen
	   Schutz wuerden die Index-Zweige weiter unten die "."-Fortsetzung stillschweigend
	   ignorieren statt sauber zu diagnostizieren. */
	{
		int targetGlobal = tcTargetSlot < 0 ? tcLookupGlobal(start, nameEnd) : -1;
		TCType targetGlobalType = targetGlobal >= 0 ? tcGlobalType(targetGlobal) : tcMakeType('i', 0);
		if (tcTargetIsArray && *nameEnd == '[' &&
		    !(tcTargetSlot >= 0 && tcLocalArrayLen[tcTargetSlot] > 0 && tcTargetType.base == 's') &&
		    !(tcTargetSlot >= 0 && tcIsPointer(tcTargetType) && tcPointee(tcTargetType).base == 's') &&
		    !(targetGlobal >= 0 && tcGlobalArrayLen[targetGlobal] > 0 && targetGlobalType.base == 's') &&
		    !(targetGlobal >= 0 && tcIsPointer(targetGlobalType) && tcPointee(targetGlobalType).base == 's')) {
			const char* afterIdx = tcSkipOneIndex(nameEnd, end);
			if (afterIdx < end && *afterIdx == '.') {
				fprintf(stderr, "qcc: indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct, in this version\n");
				tcSemanticErrors++; return;
			}
		}
	}
	/* ptr[i].feld = .. (2026-07-25, Milestone B): siehe tc_varref fuer die Erklaerung.
	   LOADP statt PUSHADDR, sonst identisch zum Array-Fall (tcTargetIndirect=1). */
	/* p->feld = .. (2026-08-10): Gegenstueck zum Lesezweig in tc_varref.
	   Feldadresse = Zeigerwert + Feldoffset, ohne Index; tcTargetIndirect=1
	   laesst tc_assign daraus ein STOREIND machen. */
	if (tcTargetIsArray && *nameEnd == '-' && nameEnd + 1 < end && nameEnd[1] == '>') {
		int gslot = tcTargetSlot < 0 ? tcLookupGlobal(start, nameEnd) : -1;
		TCType pt = tcTargetSlot >= 0 ? tcTargetType : (gslot >= 0 ? tcGlobalType(gslot) : tcBadType());
		const char* fieldStart = nameEnd + 2; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int sid, fi;
		if ((tcTargetSlot < 0 && gslot < 0) || !tcIsPointer(pt) || tcPointee(pt).base != 's') {
			fprintf(stderr, "qcc: '->' requires a pointer to struct\n"); tcSemanticErrors++; return;
		}
		sid = tcPointee(pt).structId - 1;
		fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; return; }
		printf("PUSH %d\n", tcStructFieldOffset[sid][fi]);
		if (tcTargetSlot >= 0) printf("LOADP %d\n", tcTargetSlot); else printf("LOADGP %s\n", tcGlobalNames[gslot]);
		printf("IPADD c\n");
		if (fieldEnd < end && *fieldEnd == '[') {
			if (!tcStructFieldArrayLen[sid][fi]) {
				fprintf(stderr, "qcc: scalar struct field cannot be indexed\n"); tcSemanticErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			printf("IPADD %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		} else if (tcStructFieldArrayLen[sid][fi] > 0) {
			fprintf(stderr, "qcc: cannot assign to array field\n"); tcSemanticErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '[' && tcIsPointer(tcTargetType) && tcPointee(tcTargetType).base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcPointee(tcTargetType).structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: ptr[i].field[j] not supported in this version\n"); tcSemanticErrors++; return;
			}
			printf("LOADP %d\nIPADDN %d\nPUSH %d\nPADD c\n", tcTargetSlot, structSize, tcStructFieldOffset[sid][fi]);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	/* arr[i].feld = .. (2026-07-25): siehe tc_varref fuer die vollstaendige Erklaerung
	   (Allokations-Fix, IPADDN, tcSkipOneIndex). Schreibend identisch bis auf
	   tcTargetIndirect=1 statt LOADIND (wie beim bestehenden p.field = ..-Pfad). */
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '[' && tcLocalArrayLen[tcTargetSlot] > 0 && tcTargetType.base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcTargetType.structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: arr[i].field[j] not supported in this version\n"); tcSemanticErrors++; return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcLocalArrayLen[tcTargetSlot]);
			printf("PUSHADDR L %d\nIPADDN %d\nPUSH %d\nPADD c\n", tcTargetSlot, structSize, tcStructFieldOffset[sid][fi]);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '.' && tcTargetType.base == 's') {
		int sid = tcTargetType.structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; return; }
		/* Wie tc_varref: IPADD poppt Pointer ZUERST, daher PUSH vor PUSHADDR. tcTargetIndirect=1
		   laesst tc_assign/tcLoadTarget denselben STOREIND/DUPP+LOADIND-Pfad wie bei einer
		   echten Pointer-Dereferenz nehmen -- die Feldadresse liegt bereits auf dem Stack. */
		printf("PUSH %d\nPUSHADDR L %d\nIPADD c\n", tcStructFieldOffset[sid][fi], tcTargetSlot);
		if (hasIndex) {
			/* p.field[i] = .. (2026-07-24): siehe tc_varref -- der Index-Ausdruck hat seinen
			   Wert bereits VOR uns gepusht, ein zweites IPADD kombiniert Feldadresse+Index. */
			if (!tcStructFieldArrayLen[sid][fi]) {
				fprintf(stderr, "qcc: scalar struct field cannot be indexed\n"); tcSemanticErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			printf("IPADD %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) {
			fprintf(stderr, "qcc: cannot assign to array field\n"); tcSemanticErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	/* Globale structs (2026-07-25): dieselben drei Muster wie in tc_varref, schreibend --
	   siehe dort fuer die vollstaendige Erklaerung. tcTargetIndirect=1 laesst tc_assign
	   denselben STOREIND-Pfad nehmen wie bei den bereits vorhandenen lokalen Faellen. */
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, nameEnd)) >= 0 && tcTargetIsArray && *nameEnd == '[' &&
	    tcIsPointer(tcGlobalType(global)) && tcPointee(tcGlobalType(global)).base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcPointee(tcGlobalType(global)).structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: ptr[i].field[j] not supported in this version\n"); tcSemanticErrors++; return;
			}
			printf("LOADGP %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, nameEnd)) >= 0 && tcTargetIsArray && *nameEnd == '[' &&
	    tcGlobalArrayLen[global] > 0 && tcGlobalType(global).base == 's') {
		const char* afterIdx = tcSkipOneIndex(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcGlobalType(global).structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; return; }
			if (fieldEnd < end && *fieldEnd == '[') {
				fprintf(stderr, "qcc: arr[i].field[j] not supported in this version\n"); tcSemanticErrors++; return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcGlobalArrayLen[global]);
			printf("PUSHADDR G %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, nameEnd)) >= 0 && tcTargetIsArray && *nameEnd == '.' &&
	    tcGlobalType(global).base == 's') {
		char gname[32]; int sid = tcGlobalType(global).structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		tcCopy(gname, start, nameEnd);
		if (fi < 0) { fprintf(stderr, "qcc: unknown struct field\n"); tcSemanticErrors++; return; }
		printf("PUSH %d\nPUSHADDR G %s\nIPADD c\n", tcStructFieldOffset[sid][fi], gname);
		if (hasIndex) {
			if (!tcStructFieldArrayLen[sid][fi]) {
				fprintf(stderr, "qcc: scalar struct field cannot be indexed\n"); tcSemanticErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			printf("IPADD %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) {
			fprintf(stderr, "qcc: cannot assign to array field\n"); tcSemanticErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	if (tcTargetSlot < 0 && tcLookupGlobal(start, nameEnd) >= 0) {
		int global = tcLookupGlobal(start, nameEnd);
		tcCopy(tcTargetGlobal, start, nameEnd);
		tcTargetIsGlobal = 1;
		tcTargetType = tcGlobalType(global);
		if (tcGlobalConst[global]) { fprintf(stderr, "qcc: cannot assign to const variable\n"); tcSemanticErrors++; }
		if (tcTargetIsArray && tcGlobalArrayLen[global]) {
			tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, end));
			tcCheckConstIndex(start, end, tcGlobalArrayLen[global]);
		}
		else if (tcTargetIsArray && tcIsPointer(tcTargetType)) {
			if (tcTargetType.pointeeConst) { fprintf(stderr, "qcc: cannot assign through pointer to const\n"); tcSemanticErrors++; }
			tcTargetType = tcPointee(tcTargetType);
			if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
				fprintf(stderr, "qcc: cannot dereference void*\n"); tcSemanticErrors++; tcTargetType = tcMakeType('i', 0);
			}
			tcTargetIndirect = 1; printf("LOADGP %s\nPTRINDEX %c\n", tcTargetGlobal, tcTypeTag(tcTargetType));
		} else if (!!tcGlobalArrayLen[global] != tcTargetIsArray) { fprintf(stderr, "qcc: array index mismatch\n"); tcSemanticErrors++; }
	} else if (tcTargetSlot >= 0 && tcTargetIsArray && tcLocalArrayLen[tcTargetSlot]) {
		tcCheckNDIndex(tcLocalArrayNDims[tcTargetSlot], tcLocalArrayDims[tcTargetSlot], tcCountTopIndexes(nameEnd, end));
		tcCheckConstIndex(start, end, tcLocalArrayLen[tcTargetSlot]);
	} else if (tcTargetSlot >= 0 && tcTargetIsArray && tcIsPointer(tcTargetType)) {
		if (tcTargetType.pointeeConst) { fprintf(stderr, "qcc: cannot assign through pointer to const\n"); tcSemanticErrors++; }
		tcTargetType = tcPointee(tcTargetType);
		if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
			fprintf(stderr, "qcc: cannot dereference void*\n"); tcSemanticErrors++; tcTargetType = tcMakeType('i', 0);
		}
		tcTargetIndirect = 1; printf("LOADP %d\nPTRINDEX %c\n", tcTargetSlot, tcTypeTag(tcTargetType));
	} else if (tcTargetSlot >= 0 && tcTargetIsArray) {
		fprintf(stderr, "qcc: array index mismatch\n"); tcSemanticErrors++;
	}
}

void tc_indirecttarget(const char* start, const char* end) {
	TCType pointer = tcTypePop(); (void)start; (void)end;
	tcTargetSlot = -1; tcTargetIsGlobal = 0; tcTargetIsArray = 0; tcTargetIndirect = 1;
	if (!tcIsPointer(pointer)) { tcTypeError("indirect assignment", tcPointerTo(tcMakeType('i', 0)), pointer); tcTargetType = tcMakeType('i', 0); return; }
	if (pointer.pointeeConst) { fprintf(stderr, "qcc: cannot assign through pointer to const\n"); tcSemanticErrors++; }
	tcTargetType = tcPointee(pointer);
	if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
		fprintf(stderr, "qcc: cannot dereference void*\n"); tcSemanticErrors++;
		tcTargetType = tcMakeType('i', 0);
	}
}

void tc_assignop(const char* start, const char* end) {
	int n = 0; const char* p;
	for (p = start; p < end && n < 2; p++) tcAssignOp[n++] = *p;
	tcAssignOp[n] = 0;
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcLoadTarget();
}

void tc_assign(const char* start, const char* end) {
	const char* p; int hasAssign = 0; TCType got; char tag;
	for (p = start; p < end; p++) if (*p == '=') { hasAssign = 1; break; }
	if (!hasAssign) { if (!tcLastWasPrint) { printf("DROP\n"); (void)tcTypePop(); } return; }
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcCompoundAssign();
	got = tcTypePop(); if (!tcCompatible(tcTargetType, got)) tcTypeError("assignment", tcTargetType, got);
	tag = tcTypeTag(tcTargetType);
	if (tcTargetIndirect) printf("STOREIND %c\n", tag);
	else if (tcTargetIsGlobal && tcTargetIsArray) printf("STOREIDX G %s %c\n", tcTargetGlobal, tag);
	else if (tcTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "", tcTargetGlobal);
	else if (tcTargetSlot >= 0 && tcTargetIsArray) printf("STOREIDX L %d %c\n", tcTargetSlot, tag);
	else if (tcTargetSlot >= 0) printf("STORE%s %d\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "L", tcTargetSlot);
	else fprintf(stderr, "qcc: unknown assignment target\n");
}

/* Rettet/setzt zurueck: tcPendingAdd/tcPendingMul (wie schon immer) UND seit
   2026-07-25 auch tcRel0/tcRel1. Gefundener Bug: ein Vergleich wie "nc != name[j]"
   (die INDIZIERTE Seite RECHTS vom Vergleichsoperator) lieferte falsche Typfehler
   ("array index expects int, got bool") -- tc_relop setzt tcRel0/tcRel1 schon beim
   Sehen von "!=", BEVOR die rechte Seite geparst ist; wenn die rechte Seite selbst
   einen Index (oder einen Funktionsaufruf) enthaelt, feuert deren VERSCHACHTELTE
   expr-Regel (index = indexOpen expr "]"; arg = expr) ihre EIGENE tc_expr-Aktion,
   die die noch gesetzten (aber fuer den AEUSSEREN, noch nicht abgeschlossenen
   Vergleich gedachten) tcRel0/tcRel1 faelschlich auf den INNEREN Indexausdruck
   anwendet -- und dabei auf 0 zuruecksetzt, sodass der eigentliche aeussere
   Vergleich beim spaeteren echten tc_expr gar nicht mehr feuert. War bisher nie
   aufgefallen, da kein bestehender Test eine Indizierung/einen Aufruf als RECHTEN
   Operanden eines Vergleichs hatte (nur links, z.B. "arr[i] != 0"). Fix: exakt
   dasselbe Rette-und-nulle-Muster wie bei tcPendingAdd/tcPendingMul. */
void tc_callname(const char* start, const char* end) {
	if (start < end && *start == '[') {
		if (tcIndexDepth >= 64) { fprintf(stderr, "qcc: index nesting too deep\n"); return; }
		tcIndexSavedAdd[tcIndexDepth] = tcPendingAdd;
		tcIndexSavedMul[tcIndexDepth] = tcPendingMul;
		tcIndexSavedRel0[tcIndexDepth] = tcRel0;
		tcIndexSavedRel1[tcIndexDepth] = tcRel1;
		tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0; tcIndexDepth++; return;
	}
	if (tcCallDepth >= 64) { fprintf(stderr, "qcc: call nesting too deep\\n"); return; }
	tcCopy(tcCallName[tcCallDepth], start, end);
	tcCallArgCount[tcCallDepth] = 0;
	tcCallFnSig[tcCallDepth] = -1;   /* normaler Aufruf ueber einen Namen */
	/* Argument-Ausdruecke duerfen einen aeusseren +/*-Operator (oder Vergleich) nicht sehen. */
	tcCallSavedAdd[tcCallDepth] = tcPendingAdd;
	tcCallSavedMul[tcCallDepth] = tcPendingMul;
	tcCallSavedRel0[tcCallDepth] = tcRel0;
	tcCallSavedRel1[tcCallDepth] = tcRel1;
	tcPendingAdd = 0;
	tcPendingMul = 0;
	tcRel0 = 0;
	tcRel1 = 0;
	tcCallDepth++;
}

void tc_callstmt(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcLastWasPrint) { printf("DROP\n"); (void)tcTypePop(); }
	tcLastWasPrint = 0;
}

void tc_arg(const char* start, const char* end) {
	if (start < end && *start == '[') {
		int frame; TCType indexType = tcTypePop(); if (!tcIsInteger(indexType)) tcTypeError("array index", tcMakeType('i', 0), indexType);
		if (tcIndexDepth <= 0) { fprintf(stderr, "qcc: missing index frame\n"); return; }
		frame = --tcIndexDepth; tcPendingAdd = tcIndexSavedAdd[frame]; tcPendingMul = tcIndexSavedMul[frame];
		tcRel0 = tcIndexSavedRel0[frame]; tcRel1 = tcIndexSavedRel1[frame]; return;
	}
	(void)end;
	if (tcCallDepth <= 0) { fprintf(stderr, "qcc: missing call frame\\n"); return; }
	{ int f = tcLookupFunction(tcCallName[tcCallDepth - 1]); int n = tcCallArgCount[tcCallDepth - 1]; TCType got = tcTypePop();
	  if (f >= 0 && n < tcFunctionNargs[f] && !tcCompatible(tcFunctionParamTypes[f][n], got)) tcTypeError("argument", tcFunctionParamTypes[f][n], got); }
	tcCallArgCount[tcCallDepth - 1]++;
}

void tc_call(const char* start, const char* end) {
	int frame;
	(void)start; (void)end;
	if (tcCallDepth <= 0) { fprintf(stderr, "qcc: missing call frame\\n"); return; }
	frame = --tcCallDepth;
	tcPendingAdd = tcCallSavedAdd[frame];
	tcPendingMul = tcCallSavedMul[frame];
	tcRel0 = tcCallSavedRel0[frame];
	tcRel1 = tcCallSavedRel1[frame];
	if (tcEq(tcCallName[frame], "putint")) {
		printf("PRINT\n");
		tcLastWasPrint = 1;
	} else if (tcEq(tcCallName[frame], "putuint")) {
		printf("PRINTU\n");
		tcLastWasPrint = 1;
	} else if (tcEq(tcCallName[frame], "putchar")) {
		printf("PRINTC\n");
		tcLastWasPrint = 1;
	} else {
		int f = tcLookupFunction(tcCallName[frame]);
		if (f < 0) { fprintf(stderr, "qcc: unknown function\n"); return; }
		if (tcFunctionIsVariadic[f] ? tcCallArgCount[frame] < tcFunctionNargs[f] : tcFunctionNargs[f] != tcCallArgCount[frame]) {
			fprintf(stderr, "qcc: wrong argument count\n"); return;
		}
		if (tcFunctionIsExternal[f]) {
			/* Microware-ABI-Aufruf (siehe tc_externdeclend/Backend), kein "bsr tc_<name>".
			   Drittes Feld ist NICHT mehr ein reines variadic-Bool, sondern die Anzahl
			   der FEST deklarierten Parameter (tcFunctionNargs[f], "..." selbst zaehlt
			   nicht mit) -- am echten, von xcc erzeugten Aufrufcode (2026-07-24 live
			   gegen Q9 verifiziert) verifiziert: NUR die deklarierten Parameter gehen
			   nach d0/d1 (genau wie bei einem nicht-variadischen Aufruf), der variadische
			   Rest IMMER auf den Stack -- unabhaengig davon, WIEVIELE feste Parameter es
			   gibt (bei printf(fmt, x) liegt fmt selbst in d0, nur x auf dem Stack). */
			printf("%s %s %d %d\n", tcIsPointer(tcFunctionReturnTypes[f]) ? "CALLEXTP" : "CALLEXT",
				tcCallName[frame], tcCallArgCount[frame], tcFunctionNargs[f]);
		} else {
			printf("%s %s %d\n", tcIsPointer(tcFunctionReturnTypes[f]) ? "CALLP" : "CALL", tcCallName[frame], tcCallArgCount[frame]);
		}
		if (!tcIsPointer(tcFunctionReturnTypes[f]) && tcFunctionReturnTypes[f].base == 'c') printf("NARROWC\n");
		tcTypePush(tcFunctionReturnTypes[f]);
		tcLastWasPrint = 0;
	}
}

void tc_whileend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('w')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlTop[tcCtrlDepth - 1],
	       tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_retval(const char* start, const char* end) {
	(void)start; (void)end;
	tcRetHasVal = 1;
	tcRetType = tcTypePop();
}

void tc_return(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcRetHasVal) printf("PUSH 0\n");
	if (tcRetHasVal && !tcCompatible(tcFuncType, tcRetType)) tcTypeError("return", tcFuncType, tcRetType);
	if (!tcIsPointer(tcFuncType) && tcFuncType.base == 'c') printf("NARROWC\n");
	printf("%s\n", tcIsPointer(tcFuncType) ? "RETP" : "RET");
	tcRetHasVal = 0;
}

void tc_ifbegin(const char* start, const char* end) {
	int elseLabel, endLabel;
	(void)start; (void)end;
	elseLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	tcPushCtrl('i', elseLabel, 0, endLabel, 0);
}

void tc_ifcond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("if condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('i')) return;
	printf("JZ L%d\n", tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_thenend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('i')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlEnd[tcCtrlDepth - 1],
	       tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_ifend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('i')) return;
	printf("LABEL L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_whilebegin(const char* start, const char* end) {
	int topLabel, endLabel;
	(void)start; (void)end;
	topLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	tcPushCtrl('w', topLabel, topLabel, endLabel, 0);
	printf("LABEL L%d\n", topLabel);
}

void tc_whilecond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("while condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('w')) return;
	printf("JZ L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
}

void tc_forbegin(const char* start, const char* end) {
	int condLabel, stepLabel, endLabel, bodyLabel;
	(void)start; (void)end;
	condLabel = tcNextLabel++;
	stepLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	bodyLabel = tcNextLabel++;
	tcPushCtrl('f', condLabel, stepLabel, endLabel, bodyLabel);
}

void tc_forsep1(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("LABEL L%d\n", tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_forcond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("for condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('f')) return;
	printf("JZ L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
}

void tc_forsep2(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlExtra[tcCtrlDepth - 1], tcCtrlCont[tcCtrlDepth - 1]);
}

void tc_forclose(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlTop[tcCtrlDepth - 1], tcCtrlExtra[tcCtrlDepth - 1]);
}

void tc_forend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlCont[tcCtrlDepth - 1], tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_dobegin(const char* start, const char* end) {
	int bodyLabel, condLabel, endLabel;
	(void)start; (void)end;
	bodyLabel = tcNextLabel++;
	condLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	tcPushCtrl('d', bodyLabel, condLabel, endLabel, 0);
	printf("LABEL L%d\n", bodyLabel);
}

void tc_dowhiletok(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('d')) return;
	printf("LABEL L%d\n", tcCtrlCont[tcCtrlDepth - 1]);
}

void tc_docond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("do-while condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('d')) return;
	printf("JNZ L%d\n", tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_doend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('d')) return;
	printf("LABEL L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_break(const char* start, const char* end) {
	int frame = tcFindBreakTarget(); (void)start; (void)end;
	if (frame < 0) { fprintf(stderr, "qcc: break outside loop or switch\n"); tcSemanticErrors++; return; }
	printf("JMP L%d\n", tcCtrlEnd[frame]);
}

void tc_continue(const char* start, const char* end) {
	int frame = tcFindLoop(); (void)start; (void)end;
	if (frame < 0) { fprintf(stderr, "qcc: continue outside loop\n"); tcSemanticErrors++; return; }
	printf("JMP L%d\n", tcCtrlCont[frame]);
}

/* "goto name;" -- der Sprung darf VORWAERTS gehen (Marke noch nicht gesehen);
   tcGotoFind legt die Nummer dann schon jetzt an. Ob die Marke jemals
   definiert wird, prueft tc_funcend. */
void tc_goto(const char* start, const char* end) {
	char name[32];
	int i;
	tcIdentFromSpan(name, start, end, "goto");
	if (name[0] == 0) { fprintf(stderr, "qcc: goto without label name\n"); tcSemanticErrors++; return; }
	i = tcGotoFind(name);
	if (i < 0) return;
	tcGotoUsed[i] = 1;
	printf("JMP L%d\n", tcGotoLabel[i]);
}

/* "name:" -- Sprungmarke definieren. Bewusst KEINE Pruefung, ob die Marke
   ueberhaupt angesprungen wird: eine unbenutzte Marke ist in C zulaessig
   (hoechstens eine Warnung wert), und der generierte Parser Data/qcc_p.c
   enthaelt genau solche Faelle. */
void tc_label(const char* start, const char* end) {
	char name[32];
	int i;
	tcIdentFromSpan(name, start, end, 0);
	if (name[0] == 0) { fprintf(stderr, "qcc: empty label name\n"); tcSemanticErrors++; return; }
	i = tcGotoFind(name);
	if (i < 0) return;
	if (tcGotoDefined[i]) {
		fprintf(stderr, "qcc: duplicate label '%s'\n", name); tcSemanticErrors++; return;
	}
	tcGotoDefined[i] = 1;
	printf("LABEL L%d\n", tcGotoLabel[i]);
}

/* Zeichenliteral 'x' -> Zahlwert. Typ int wie in echtem C (siehe
   Grammatikkommentar bei charLit). Die Spanne ist inklusive der Hochkommata. */
void tc_charlit(const char* start, const char* end) {
	const char* p = start;
	int v;
	if (p < end && *p == 39) p++;            /* oeffnendes Hochkomma */
	if (p >= end) { fprintf(stderr, "qcc: empty character literal\n"); tcSemanticErrors++; tcTypePush(tcMakeType('i', 0)); return; }
	if (*p == 92) {                          /* Backslash: Escape-Form */
		p++;
		if (p >= end) { fprintf(stderr, "qcc: incomplete character escape\n"); tcSemanticErrors++; tcTypePush(tcMakeType('i', 0)); return; }
		if      (*p == 'n')  v = 10;
		else if (*p == 't')  v = 9;
		else if (*p == 'r')  v = 13;
		else if (*p == '0')  v = 0;
		else if (*p == 92)   v = 92;
		else { fprintf(stderr, "qcc: unsupported character escape\n"); tcSemanticErrors++; v = 0; }
	} else {
		v = (int)(unsigned char)*p;
	}
	printf("PUSH %d\n", v);
	tcTypePush(tcMakeType('i', 0));
}

/* "(void)ausdruck;" -- der Ausdruck wurde bereits ausgewertet und liegt auf
   dem Stack; hier nur noch verwerfen. Ein Ausdruck vom Typ void (z.B. der
   Aufruf einer void-Funktion) hat gar keinen Wert abgelegt, dann entfaellt
   das DROP. */
void tc_voidcast(const char* start, const char* end) {
	TCType t = tcTypePop();
	(void)start; (void)end;
	if (tcIsPointer(t) || t.base != 'v') printf("DROP\n");
}

/* Feuert auf dem "(" der Zeigerklammer in
   "typedef <rueckgabe> (*Name)(<params>);". An dieser Stelle steht in
   tcCurrentType noch der RUECKGABEtyp -- gleich darauf ueberschreibt ihn die
   Parameterliste (externParamList wird wiederverwendet, siehe Grammatik), also
   hier retten. Ausserdem den gemeinsamen extern-Parameterpuffer zuruecksetzen,
   den wir uns mit externDecl teilen (Deklarationen schachteln nicht). */
void tc_fnptrbegin(const char* start, const char* end) {
	(void)start; (void)end;
	tcFnPtrRetPending = tcCurrentType;
	tcExternBuildParamCount = 0;
	tcExternIsVariadic = 0;
}

/* Registriert "typedef <rueckgabe> (*Name)(<params>);" als eigenen Typ. Der
   Name steht zwischen "(*" und ")" -- die Spanne wird dafuer direkt abgesucht,
   weil die Rueckwaertssuche von tc_typedefend hier nicht passt (hinter dem
   Namen folgt noch die Parameterliste). */
void tc_fnptrtypedef(const char* start, const char* end) {
	const char* p = start;
	const char* nameStart;
	const char* nameEnd;
	int k, sig;
	while (p < end && *p != '(') p++;          /* "(" der Zeigerklammer */
	while (p < end && (*p == '(' || *p == '*' || *p == ' ' || *p == '\t')) p++;
	nameStart = p;
	while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
	                   (*p >= '0' && *p <= '9') || *p == '_')) p++;
	nameEnd = p;
	if (nameStart == nameEnd) { fprintf(stderr, "qcc: malformed function pointer typedef\n"); tcSemanticErrors++; return; }
	if (tcExternIsVariadic) { fprintf(stderr, "qcc: variadic function pointers are not supported\n"); tcSemanticErrors++; return; }
	if (tcTypedefCount >= MAX_TYPEDEFS) { fprintf(stderr, "qcc: too many typedefs\n"); tcSemanticErrors++; return; }
	if (tcLookupTypedef(nameStart, nameEnd) >= 0) { fprintf(stderr, "qcc: duplicate typedef\n"); tcSemanticErrors++; return; }
	if (tcExternBuildParamCount > MAX_FNSIG_PARAMS) { fprintf(stderr, "qcc: too many function pointer parameters\n"); tcSemanticErrors++; return; }
	if (tcFnSigCount >= MAX_FNSIGS) { fprintf(stderr, "qcc: too many function pointer signatures\n"); tcSemanticErrors++; return; }
	sig = tcFnSigCount++;
	tcFnSigRet[sig] = tcFnPtrRetPending;
	tcFnSigNargs[sig] = tcExternBuildParamCount;
	for (k = 0; k < tcExternBuildParamCount; k++) tcFnSigParams[sig][k] = tcExternBuildParamTypes[k];
	tcCopy(tcTypedefNames[tcTypedefCount], nameStart, nameEnd);
	tcTypedefTypes[tcTypedefCount] = tcMakeFnPtr(sig);
	tcTypedefCount++;
}

/* Feuert auf dem "(" eines indirekten Aufrufs "ausdruck(args)". Der Wert des
   Callee-Ausdrucks liegt zu diesem Zeitpunkt bereits auf dem Stack (varRef hat
   ihn emittiert) und sein Typ obenauf dem Typstapel -- beides wird hier
   uebernommen. Ansonsten identische Frame-Buchfuehrung wie tc_callname, damit
   tc_arg unveraendert mitzaehlt. */
void tc_indcallbegin(const char* start, const char* end) {
	TCType callee = tcTypePop();
	(void)start; (void)end;
	if (tcCallDepth >= 64) { fprintf(stderr, "qcc: call nesting too deep\n"); return; }
	tcCallName[tcCallDepth][0] = 0;          /* kein Name -- tc_arg findet keine Funktion und zaehlt nur */
	tcCallArgCount[tcCallDepth] = 0;
	if (!tcIsFnPtr(callee)) {
		fprintf(stderr, "qcc: called value is not a function pointer\n"); tcSemanticErrors++;
		tcCallFnSig[tcCallDepth] = -1;
	} else {
		tcCallFnSig[tcCallDepth] = (int)callee.structId - 1;
	}
	tcCallSavedAdd[tcCallDepth] = tcPendingAdd;
	tcCallSavedMul[tcCallDepth] = tcPendingMul;
	tcCallSavedRel0[tcCallDepth] = tcRel0;
	tcCallSavedRel1[tcCallDepth] = tcRel1;
	tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0;
	tcCallDepth++;
}

/* Schliesst den indirekten Aufruf ab. BEWUSSTE GRENZE: geprueft wird nur die
   ARGUMENTANZAHL, nicht die Argumenttypen -- tc_arg nimmt die Typen generisch
   vom Stapel und kann sie mangels Funktionsnamen keiner Signatur zuordnen. Die
   Rueckgabe traegt dagegen den korrekten Signaturtyp. */
void tc_indcall(const char* start, const char* end) {
	int frame, sig;
	(void)start; (void)end;
	if (tcCallDepth <= 0) { fprintf(stderr, "qcc: missing call frame\n"); return; }
	frame = --tcCallDepth;
	tcPendingAdd = tcCallSavedAdd[frame];
	tcPendingMul = tcCallSavedMul[frame];
	tcRel0 = tcCallSavedRel0[frame];
	tcRel1 = tcCallSavedRel1[frame];
	sig = tcCallFnSig[frame];
	if (sig < 0) { tcTypePush(tcBadType()); return; }
	if (tcFnSigNargs[sig] != tcCallArgCount[frame]) {
		fprintf(stderr, "qcc: wrong argument count\n"); tcSemanticErrors++;
	}
	printf("CALLIND%s %d\n", tcIsPointer(tcFnSigRet[sig]) ? "P" : "", tcCallArgCount[frame]);
	tcTypePush(tcFnSigRet[sig]);
}

void tc_structbegin(const char* start, const char* end) {
	tcCopy(tcStructBuildName, start, end);
	tcStructBuildFieldCount = 0;
}

void tc_anonstructbegin(const char* start, const char* end) {
	/* anonymes struct inline im typedef -- kein Name an dieser Stelle bekannt,
	   siehe Kommentar bei tcAnonStructPending und tc_typedefend. */
	(void)start; (void)end;
	tcStructBuildFieldCount = 0;
	tcAnonStructPending = 1;
}

void tc_structfield(const char* start, const char* end) {
	/* Spanne deckt "type fieldName [ "[" arraySize "]" ] ;" ab -- Feldname + optionales
	   Array-Suffix sind das letzte Wort vor ';' (kein Leerraum zwischen Name und "["). */
	const char* p = end - 1; const char* fieldEnd; const char* fieldStart; const char* bracket;
	int arrayLen = 0;
	while (p > start && (*p == ' ' || *p == '\t' || *p == ';')) p--;
	fieldEnd = p + 1;
	/* "*" ebenfalls als Abbruchzeichen (2026-07-25, Pointer-Felder): deckt sowohl
	   "T* name" als auch "T *name" ab, analog zu tc_typedefend. */
	while (p > start && *(p - 1) != ' ' && *(p - 1) != '\t' && *(p - 1) != '*') p--;
	fieldStart = p;
	bracket = fieldStart;
	while (bracket < fieldEnd && *bracket != '[') bracket++;
	if (bracket < fieldEnd) {
		const char* q = bracket + 1;
		while (q < fieldEnd && *q >= '0' && *q <= '9') arrayLen = arrayLen * 10 + (*q++ - '0');
		fieldEnd = bracket;
	}
	if (tcStructBuildFieldCount < MAX_STRUCT_FIELDS) {
		tcStructBuildFieldTypes[tcStructBuildFieldCount] = tcCurrentType;
		tcStructBuildFieldArrayLen[tcStructBuildFieldCount] = arrayLen;
		tcCopy(tcStructBuildFieldNames[tcStructBuildFieldCount], fieldStart, fieldEnd);
		tcStructBuildFieldCount++;
	} else { fprintf(stderr, "qcc: too many struct fields\n"); tcSemanticErrors++; }
}

/* Gemeinsamer Kern fuer benannte structs (tc_structend) UND anonyme structs
   inline im typedef (tc_typedefend, 2026-07-24) -- Name kommt bei letzterem
   erst NACH dem Feld-Body, daher als eigenstaendige Funktion mit Name als
   Parameter statt fest an tcStructBuildName gebunden. Gibt die neue sid oder
   -1 bei Fehler zurueck. */
static int tcRegisterStruct(const char* nameStart, const char* nameEnd) {
	int i; int offset = 0; int sid = tcStructCount;
	if (tcStructCount >= MAX_STRUCTS) { fprintf(stderr, "qcc: too many structs\n"); tcSemanticErrors++; return -1; }
	if (tcLookupStruct(nameStart, nameEnd) >= 0) { fprintf(stderr, "qcc: duplicate struct\n"); tcSemanticErrors++; return -1; }
	if (tcStructBuildFieldCount == 0) { fprintf(stderr, "qcc: struct needs at least one field\n"); tcSemanticErrors++; return -1; }
	/* Layout: natuerliches Alignment, skalare Feldtypen (inkl. Pointer, seit
	   2026-07-25) oder Pointer-Arrays -- struct-in-struct bleibt abgelehnt.
	   Array-Felder: Ausrichtung nach dem ELEMENTtyp, Gesamtgroesse =
	   Elementgroesse * Elementzahl (wie ein GARRAY, nur eingebettet).
	   Pointer-Felder (2026-07-25, Selfhosting L2): IMMER 8 Byte Groesse/
	   Ausrichtung, UNABHAENGIG vom Ziel-Backend -- 68k-Pointer sind 4 Byte,
	   ARM64-Pointer 8 Byte; ein einzelnes frontend-berechnetes Offset muss
	   aber fuer BEIDE Architekturen gueltig bleiben (dieselbe IR wird von
	   beiden Backends verarbeitet). Der 68k-Backend nutzt von diesem
	   8-Byte-Slot nur die ersten 4 Byte, identisch zum Layout einer lokalen
	   Pointer-Variable -- kein Backend-Code-Aenderung noetig, LOADIND/
	   STOREIND/IPADD kennen den Typtag 'p' bereits generisch. Nur EIN
	   Pointer-Level unterstuetzt (kein T**-Feld), keine Pointer-Arrays. */
	for (i = 0; i < tcStructBuildFieldCount; i++) {
		TCType ft = tcStructBuildFieldTypes[i]; int elemSize, align, size;
		if (ft.base == 's' || ft.base == 'v') {
			fprintf(stderr, "qcc: struct field type not supported in this version\n"); tcSemanticErrors++; return -1;
		}
		if (tcIsPointer(ft) && tcStructBuildFieldArrayLen[i] > 0) {
			fprintf(stderr, "qcc: pointer arrays as struct field not supported in this version\n"); tcSemanticErrors++; return -1;
		}
		elemSize = tcIsPointer(ft) ? 8 : (ft.base == 'c' || ft.base == 'b') ? 1 : 4;
		align = elemSize;
		size = tcStructBuildFieldArrayLen[i] > 0 ? elemSize * tcStructBuildFieldArrayLen[i] : elemSize;
		offset = (offset + align - 1) & ~(align - 1);
		tcStructFieldOffset[sid][i] = offset;
		offset += size;
	}
	tcStructByteSize[sid] = (offset + 3) & ~3;
	tcCopy(tcStructNames[tcStructCount], nameStart, nameEnd);
	tcStructFieldCount[tcStructCount] = tcStructBuildFieldCount;
	for (i = 0; i < tcStructBuildFieldCount; i++) {
		tcStructFieldTypes[tcStructCount][i] = tcStructBuildFieldTypes[i];
		tcStructFieldArrayLen[tcStructCount][i] = tcStructBuildFieldArrayLen[i];
		tcCopy(tcStructFieldNames[tcStructCount][i], tcStructBuildFieldNames[i], tcStructBuildFieldNames[i] + strlen(tcStructBuildFieldNames[i]));
	}
	tcStructCount++;
	return sid;
}

void tc_structend(const char* start, const char* end) {
	(void)start; (void)end;
	tcRegisterStruct(tcStructBuildName, tcStructBuildName + strlen(tcStructBuildName));
}

void tc_typedefend(const char* start, const char* end) {
	/* Spanne deckt "typedef type pointerDecl Name ;" ab (oder "typedef struct { ... } Name ;"
	   im anonymen Fall) -- Name ist das letzte Wort vor ';', unabhaengig vom Inhalt davor:
	   nach der schliessenden "}" im anonymen Fall steht ein Leerzeichen, das die
	   Rueckwaertssuche korrekt als Grenze erkennt. */
	const char* p = end - 1; const char* nameEnd; const char* nameStart;
	while (p > start && (*p == ' ' || *p == '\t' || *p == ';')) p--;
	nameEnd = p + 1;
	while (p > start && *(p - 1) != ' ' && *(p - 1) != '\t' && *(p - 1) != '*') p--;
	nameStart = p;
	if (tcTypedefCount >= MAX_TYPEDEFS) { fprintf(stderr, "qcc: too many typedefs\n"); tcSemanticErrors++; return; }
	if (tcLookupTypedef(nameStart, nameEnd) >= 0) { fprintf(stderr, "qcc: duplicate typedef\n"); tcSemanticErrors++; return; }
	if (tcAnonStructPending) {
		/* anonymes struct inline im typedef (2026-07-24): jetzt erst registrieren, mit dem
		   typedef-Zielnamen selbst als internem struct-Tag (siehe tc_anonstructbegin). */
		int sid = tcRegisterStruct(nameStart, nameEnd);
		tcAnonStructPending = 0;
		if (sid < 0) return;
		tcTypedefTypes[tcTypedefCount] = tcMakeType('s', 0);
		tcTypedefTypes[tcTypedefCount].structId = (unsigned char)(sid + 1);
	} else {
		tcTypedefTypes[tcTypedefCount] = tcCurrentType;
	}
	tcCopy(tcTypedefNames[tcTypedefCount], nameStart, nameEnd);
	tcTypedefCount++;
}

void tc_enumdecl(const char* start, const char* end) {
	/* Spanne deckt "enum Name { A, B, C } ;" ab -- Konstanten selbst per Rohtext, wie tc_globalend. */
	const char* p = start; long value = 0; const char* nameStart; const char* nameEnd;
	while (p < end && *p != ' ' && *p != '\t') p++;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	nameStart = p; nameEnd = tcWordEnd(p, end);
	if (nameEnd > nameStart) {
		if (tcEnumTypeCount < MAX_ENUM_TYPES) tcCopy(tcEnumTypeNames[tcEnumTypeCount++], nameStart, nameEnd);
		else { fprintf(stderr, "qcc: too many enum types\n"); tcSemanticErrors++; }
	}
	while (p < end && *p != '{') p++;
	if (p == end) return;
	p++;
	for (;;) {
		const char* ne;
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		if (p < end && *p == '}') break;
		ne = tcWordEnd(p, end);
		if (ne == p) break;
		if (tcEnumConstCount >= MAX_ENUM_CONSTANTS) { fprintf(stderr, "qcc: too many enum constants\n"); tcSemanticErrors++; }
		else if (tcLookupEnumConst(p, ne) >= 0) { fprintf(stderr, "qcc: duplicate enum constant\n"); tcSemanticErrors++; }
		else {
			tcCopy(tcEnumConstNames[tcEnumConstCount], p, ne);
			tcEnumConstValues[tcEnumConstCount] = value;
			tcEnumConstCount++;
		}
		value++;
		p = ne;
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		if (p < end && *p == ',') { p++; continue; }
		break;
	}
}

void tc_sizeof(const char* start, const char* end) {
	int size;
	tc_type(start, end);
	if (tcCurrentType.pointers) {
		fprintf(stderr, "qcc: sizeof of pointer types not supported in this version\n");
		tcSemanticErrors++; size = 4;
	} else if (tcCurrentType.base == 's') {
		size = tcStructByteSize[tcCurrentType.structId - 1];
	} else if (tcCurrentType.base == 'c' || tcCurrentType.base == 'b') size = 1;
	else size = 4;
	printf("PUSH %d\n", size);
	tcTypePush(tcMakeType('i', 0));
}

void tc_sizeofvar(const char* start, const char* end) {
	int slot = tcLookupLocal(start, end), global = -1; TCType t; int size; int count;
	if (slot < 0) global = tcLookupGlobal(start, end);
	if (slot < 0 && global < 0) { fprintf(stderr, "qcc: unknown variable in sizeof\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	count = slot >= 0 ? tcLocalArrayLen[slot] : tcGlobalArrayLen[global];
	if (tcIsPointer(t)) {
		fprintf(stderr, "qcc: sizeof of pointer types not supported in this version\n");
		tcSemanticErrors++; size = 4;
	} else if (t.base == 's') {
		size = (count > 0 ? count : 1) * tcStructByteSize[t.structId - 1];
	} else {
		int elemSize = (t.base == 'c' || t.base == 'b') ? 1 : 4;
		size = count > 0 ? count * elemSize : elemSize;
	}
	printf("PUSH %d\n", size);
	tcTypePush(tcMakeType('i', 0));
}

void tc_preincdec(const char* start, const char* end) {
	int isDec = (start[0] == '-');
	const char* p = start + 2; const char* nameStart; const char* nameEnd;
	int slot, global = -1; TCType t;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	nameStart = p; nameEnd = tcWordEnd(p, end);
	slot = tcLookupLocal(nameStart, nameEnd);
	if (slot < 0) global = tcLookupGlobal(nameStart, nameEnd);
	if (slot < 0 && global < 0) { fprintf(stderr, "qcc: unknown variable\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
	if ((slot >= 0 && tcLocalConst[slot]) || (global >= 0 && tcGlobalConst[global])) {
		fprintf(stderr, "qcc: cannot assign to const variable\n"); tcSemanticErrors++;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIncDecCheck(slot, global, t)) {
		fprintf(stderr, "qcc: ++/-- only supported for plain int/unsigned/char variables in this version\n");
		tcSemanticErrors++; tcTypePush(tcBadType()); return;
	}
	tcIncDecEmit(slot, global, t, isDec, 1);
}

void tc_postincdec(const char* start, const char* end) {
	const char* nameStart = start; const char* nameEnd = tcWordEnd(start, end);
	int isDec = (end[-1] == '-');
	int slot = tcLookupLocal(nameStart, nameEnd), global = -1; TCType t;
	if (slot < 0) global = tcLookupGlobal(nameStart, nameEnd);
	if (slot < 0 && global < 0) { fprintf(stderr, "qcc: unknown variable\n"); tcSemanticErrors++; tcTypePush(tcBadType()); return; }
	if ((slot >= 0 && tcLocalConst[slot]) || (global >= 0 && tcGlobalConst[global])) {
		fprintf(stderr, "qcc: cannot assign to const variable\n"); tcSemanticErrors++;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIncDecCheck(slot, global, t)) {
		fprintf(stderr, "qcc: ++/-- only supported for plain int/unsigned/char variables in this version\n");
		tcSemanticErrors++; tcTypePush(tcBadType()); return;
	}
	tcIncDecEmit(slot, global, t, isDec, 0);
}

void tc_incdecstmt(const char* start, const char* end) {
	(void)start; (void)end;
	printf("DROP\n"); (void)tcTypePop();
}

void tc_forstep(const char* start, const char* end) {
	/* forStep = target assignop expr | postIncDec | preIncDec -- unterschieden per '='-Suche,
	   genau wie tc_assign es fuer assignStmt intern schon tut. */
	const char* p;
	for (p = start; p < end; p++) if (*p == '=') { tc_assign(start, end); return; }
	printf("DROP\n"); (void)tcTypePop();
}

void tc_switchbegin(const char* start, const char* end) {
	int endLabel; (void)start; (void)end;
	if (tcSwitchDepth >= MAX_SWITCH) { fprintf(stderr, "qcc: switch nesting too deep\n"); tcSemanticErrors++; return; }
	endLabel = tcNextLabel++;
	tcSwitchBodyLabel[tcSwitchDepth] = -1;
	tcSwitchNextLabel[tcSwitchDepth] = -1;
	tcSwitchEndLabel[tcSwitchDepth] = endLabel;
	tcSwitchGroupOpen[tcSwitchDepth] = 0;
	tcSwitchHadDefault[tcSwitchDepth] = 0;
	tcPushCtrl('s', 0, 0, endLabel, 0);
	tcSwitchDepth++;
}

void tc_switchcond(const char* start, const char* end) {
	TCType t = tcTypePop(); (void)start; (void)end;
	if (!tcIsInteger(t)) tcTypeError("switch expression", tcMakeType('i', 0), t);
}

void tc_caselabel(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; long value; const char* p = start + 4; const char* ne;
	if (d < 0) { fprintf(stderr, "qcc: case outside switch\n"); tcSemanticErrors++; return; }
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (*p == '-' || (*p >= '0' && *p <= '9')) {
		int neg = 0;
		if (*p == '-') { neg = 1; p++; }
		value = 0;
		while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
		if (neg) value = -value;
	} else {
		int ec;
		ne = tcWordEnd(p, end);
		ec = tcLookupEnumConst(p, ne);
		if (ec < 0) { fprintf(stderr, "qcc: unknown case value\n"); tcSemanticErrors++; return; }
		value = tcEnumConstValues[ec];
	}
	if (!tcSwitchGroupOpen[d]) {
		tcSwitchBodyLabel[d] = tcNextLabel++;
		tcSwitchNextLabel[d] = tcNextLabel++;
		tcSwitchGroupOpen[d] = 1;
	}
	printf("DUP\nPUSH %ld\nCMPEQ\nJNZ L%d\n", value, tcSwitchBodyLabel[d]);
}

void tc_caselabelrun_end(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	printf("JMP L%d\nLABEL L%d\nDROP\n", tcSwitchNextLabel[d], tcSwitchBodyLabel[d]);
}

void tc_casegroup_end(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	printf("JMP L%d\nLABEL L%d\n", tcSwitchEndLabel[d], tcSwitchNextLabel[d]);
	tcSwitchGroupOpen[d] = 0;
}

void tc_defaultlabel(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	tcSwitchHadDefault[d] = 1;
	printf("DROP\n");
}

void tc_switchend(const char* start, const char* end) {
	int d; (void)start; (void)end;
	if (!tcNeedCtrl('s')) return;
	d = tcSwitchDepth - 1;
	if (!tcSwitchHadDefault[d]) printf("DROP\n");
	printf("LABEL L%d\n", tcSwitchEndLabel[d]);
	tcSwitchDepth--;
	tcCtrlDepth--;
}

void tc_castcapture(const char* start, const char* end) {
	tc_type(start, end);
	if (tcCastDepth < 16) tcCastStack[tcCastDepth++] = tcCurrentType;
	else { fprintf(stderr, "qcc: cast nesting too deep\n"); tcSemanticErrors++; }
}

void tc_cast(const char* start, const char* end) {
	TCType target; TCType src = tcTypePop(); (void)start; (void)end;
	if (tcCastDepth <= 0) { tcTypePush(tcBadType()); return; }
	target = tcCastStack[--tcCastDepth];
	if (!tcIsInteger(src)) {
		tcTypeError("cast", tcMakeType('i', 0), src);
		tcTypePush(tcMakeType(target.base, 0)); return;
	}
	if (target.base == 'c') printf("NARROWC\n");
	else if (target.base == 'b') printf("PUSH 0\nCMPNE\n");
	tcTypePush(tcMakeType(target.base, 0));
}

void tc_logicandop(const char* start, const char* end) {
	(void)start; (void)end;
	tcLogicBegin('&');
}

void tc_logicandend(const char* start, const char* end) {
	if (tcHasTopToken(start, end, '&', '&')) tcLogicEnd('&');
}

void tc_logicorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcLogicBegin('|');
}

void tc_logicorend(const char* start, const char* end) {
	if (tcHasTopToken(start, end, '|', '|')) tcLogicEnd('|');
}

void tc_bitandop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('&');
}

void tc_bitandend(const char* start, const char* end) {
	if (tcHasTopChar(start, end, '&') && tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '&') tcBitEnd('&');
}

void tc_bitxorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('^');
}

void tc_bitxorend(const char* start, const char* end) {
	if (tcHasTopChar(start, end, '^') && tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '^') tcBitEnd('^');
}

void tc_bitorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('|');
}

void tc_bitorend(const char* start, const char* end) {
	if (tcHasTopChar(start, end, '|') && tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '|') tcBitEnd('|');
}

void tc_shiftop(const char* start, const char* end) {
	(void)end;
	tcPendingShift0 = start[0];
	tcPendingShift1 = start[1];
}

void tc_shiftrhs(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcPendingShift0 && tcPendingShift1) tcShiftEnd();
}

void tc_ternarybegin(const char* start, const char* end) {
	(void)start; (void)end;
	tcTernaryBegin();
}

void tc_ternarymiddle(const char* start, const char* end) {
	(void)start; (void)end;
	tcTernaryMiddle();
}

void tc_ternaryend(const char* start, const char* end) {
	if (tcHasChar(start, end, '?')) tcTernaryEnd();
}

static int p_program(void);
static int p_externDecl(void);
static int p_externName(void);
static int p_externParamList(void);
static int p_externParam(void);
static int p_ellipsisTok(void);
static int p_enumDecl(void);
static int p_structDecl(void);
static int p_structName(void);
static int p_structField(void);
static int p_structDeclarator(void);
static int p_fieldName(void);
static int p_typedefDecl(void);
static int p_fnPtrTypedef(void);
static int p_fnPtrOpen(void);
static int p_fnPtrName(void);
static int p_typedefType(void);
static int p_anonStructType(void);
static int p_anonStructOpen(void);
static int p_typedefTargetName(void);
static int p_globalDecl(void);
static int p_externGlobalDecl(void);
static int p_plainGlobalDecl(void);
static int p_constKw(void);
static int p_staticKw(void);
static int p_globalInit(void);
static int p_globalStringInit(void);
static int p_arraySize(void);
static int p_arraySizeN(void);
static int p_globalValue(void);
static int p_globalBool(void);
static int p_globalNeg(void);
static int p_globalNumber(void);
static int p_funcdef(void);
static int p_funcBody(void);
static int p_funcBodyOpen(void);
static int p_protoEnd(void);
static int p_funcHead(void);
static int p_retConstKw(void);
static int p_funcParams(void);
static int p_voidParams(void);
static int p_normalParams(void);
static int p_paramList(void);
static int p_param(void);
static int p_paramDecl(void);
static int p_paramArray(void);
static int p_block(void);
static int p_statement(void);
static int p_unlabeledStmt(void);
static int p_staticVarDecl(void);
static int p_staticLocalName(void);
static int p_staticInit(void);
static int p_staticRuntimeInit(void);
static int p_switchStmt(void);
static int p_switchKw(void);
static int p_switchCond(void);
static int p_switchClose(void);
static int p_caseGroup(void);
static int p_caseLabelRun(void);
static int p_caseLabel(void);
static int p_caseValue(void);
static int p_caseNeg(void);
static int p_caseNumber(void);
static int p_caseBody(void);
static int p_defaultGroup(void);
static int p_defaultLabel(void);
static int p_varDecl(void);
static int p_varDeclarator(void);
static int p_localDecl(void);
static int p_varInit(void);
static int p_arrayStringInit(void);
static int p_initList(void);
static int p_initValue(void);
static int p_initBool(void);
static int p_initNeg(void);
static int p_initNumber(void);
static int p_assignStmt(void);
static int p_assignop(void);
static int p_callStmt(void);
static int p_emptyStmt(void);
static int p_voidCastStmt(void);
static int p_voidCastOpen(void);
static int p_returnStmt(void);
static int p_retVal(void);
static int p_ifStmt(void);
static int p_ifKw(void);
static int p_elseKw(void);
static int p_ifCond(void);
static int p_thenPart(void);
static int p_elsePart(void);
static int p_whileStmt(void);
static int p_whileKw(void);
static int p_whileCond(void);
static int p_whileBody(void);
static int p_forStmt(void);
static int p_forKw(void);
static int p_forSep1(void);
static int p_forSep2(void);
static int p_forClose(void);
static int p_forInit(void);
static int p_forCond(void);
static int p_forStep(void);
static int p_forBody(void);
static int p_doStmt(void);
static int p_doKw(void);
static int p_doWhileTok(void);
static int p_doCond(void);
static int p_doClose(void);
static int p_doBody(void);
static int p_breakStmt(void);
static int p_continueStmt(void);
static int p_gotoStmt(void);
static int p_labelStmt(void);
static int p_expr(void);
static int p_conditionalExpr(void);
static int p_qmark(void);
static int p_conditionalTrue(void);
static int p_colon(void);
static int p_conditionalFalse(void);
static int p_orExpr(void);
static int p_orop(void);
static int p_andExpr(void);
static int p_andop(void);
static int p_bitOrExpr(void);
static int p_bitorop(void);
static int p_bitXorExpr(void);
static int p_bitxorop(void);
static int p_bitAndExpr(void);
static int p_bitandop(void);
static int p_comparison(void);
static int p_shiftExpr(void);
static int p_shiftRhs(void);
static int p_shiftop(void);
static int p_relop(void);
static int p_addExpr(void);
static int p_addop(void);
static int p_term(void);
static int p_mulop(void);
static int p_factor(void);
static int p_postfixIndex(void);
static int p_stringLit(void);
static int p_charLit(void);
static int p_charLitBody(void);
static int p_charLitEscape(void);
static int p_charLitEscChar(void);
static int p_charLitPlain(void);
static int p_character(void);
static int p_strEscape(void);
static int p_strEscChar(void);
static int p_negFactor(void);
static int p_addressRef(void);
static int p_sizeofExpr(void);
static int p_sizeofArg(void);
static int p_sizeofType(void);
static int p_sizeofVarName(void);
static int p_castExpr(void);
static int p_castType(void);
static int p_castOperand(void);
static int p_preIncDec(void);
static int p_postIncDec(void);
static int p_incdecOp(void);
static int p_incDecStmt(void);
static int p_derefRef(void);
static int p_call(void);
static int p_indirectCall(void);
static int p_indCallOpen(void);
static int p_argList(void);
static int p_arg(void);
static int p_target(void);
static int p_directTarget(void);
static int p_indirectTarget(void);
static int p_varRef(void);
static int p_index(void);
static int p_indexOpen(void);
static int p_member(void);
static int p_defName(void);
static int p_paramName(void);
static int p_localName(void);
static int p_globalName(void);
static int p_funcName(void);
static int p_type(void);
static int p_structTypeRef(void);
static int p_enumTypeRef(void);
static int p_typedefRef(void);
static int p_pointerDecl(void);
static int p_pointerStar(void);
static int p_unsignedInt(void);
static int p_boolLit(void);
static int p_ident(void);
static int p_number(void);
static int p_hexNumber(void);
static int p_hexMark(void);
static int p_hexDigit(void);
static int p_decNumber(void);
static int p_letter(void);
static int p_digit(void);

/* program */
static int p_program(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L1:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalDecl()) goto L4;
	goto L3;
L4:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_structDecl()) goto L5;
	goto L3;
L5:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_typedefDecl()) goto L6;
	goto L3;
L6:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_enumDecl()) goto L7;
	goto L3;
L7:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_externDecl()) goto L8;
	goto L3;
L8:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_funcdef()) goto L9;
	goto L3;
L9:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L2;
L3:	sp--;
	sp--; goto L1;
L2:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L0:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* externDecl */
static int p_externDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "extern", 6) != 0) goto L10;
	if (idch((unsigned char)p[6])) goto L10;
	p += 6;
	if (!p_type()) goto L10;
	if (!p_pointerDecl()) goto L10;
	if (!p_externName()) goto L10;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L10;
	p += 1;
	if (!p_externParamList()) goto L10;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L10;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L10;
	p += 1;
	actionLogPush(tc_externdeclend, entry, p);	/* ACTION AFTER externDecl */
	return 1;
L10:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* externName */
static int p_externName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L11;
	actionLogPush(tc_externname, entry, p);	/* ACTION AFTER externName */
	return 1;
L11:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* externParamList */
static int p_externParamList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_externParam()) goto L13;
L15:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L16;
	p += 1;
	if (!p_externParam()) goto L16;
	sp--; goto L15;
L16:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L17;
	p += 1;
	if (!p_ellipsisTok()) goto L17;
	sp--; goto L18;
L17:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L18:	;
	sp--; goto L14;
L13:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L14:	;
	return 1;
L12:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* externParam */
static int p_externParam(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L20;
	sp--; goto L21;
L20:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L21:	;
	if (!p_type()) goto L19;
	if (!p_pointerDecl()) goto L19;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_ident()) goto L22;
	sp--; goto L23;
L22:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L23:	;
	actionLogPush(tc_externparam, entry, p);	/* ACTION AFTER externParam */
	return 1;
L19:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ellipsisTok */
static int p_ellipsisTok(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "...", 3) != 0) goto L24;
	p += 3;
	actionLogPush(tc_externvariadic, entry, p);	/* ACTION AFTER ellipsisTok */
	return 1;
L24:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* enumDecl */
static int p_enumDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L25;
	if (idch((unsigned char)p[4])) goto L25;
	p += 4;
	ws();
	if (!p_ident()) goto L25;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L25;
	p += 1;
	ws();
	if (!p_ident()) goto L25;
L26:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L27;
	p += 1;
	ws();
	if (!p_ident()) goto L27;
	sp--; goto L26;
L27:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L25;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L25;
	p += 1;
	actionLogPush(tc_enumdecl, entry, p);	/* ACTION AFTER enumDecl */
	return 1;
L25:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structDecl */
static int p_structDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L28;
	if (idch((unsigned char)p[6])) goto L28;
	p += 6;
	if (!p_structName()) goto L28;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L28;
	p += 1;
	if (!p_structField()) goto L28;
L29:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L30;
	sp--; goto L29;
L30:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L28;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L28;
	p += 1;
	actionLogPush(tc_structend, entry, p);	/* ACTION AFTER structDecl */
	return 1;
L28:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structName */
static int p_structName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L31;
	actionLogPush(tc_structbegin, entry, p);	/* ACTION AFTER structName */
	return 1;
L31:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structField */
static int p_structField(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_type()) goto L32;
	if (!p_pointerDecl()) goto L32;
	if (!p_structDeclarator()) goto L32;
L33:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L34;
	p += 1;
	if (!p_structDeclarator()) goto L34;
	sp--; goto L33;
L34:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L32;
	p += 1;
	return 1;
L32:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structDeclarator */
static int p_structDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_fieldName()) goto L35;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L36;
	sp--; goto L37;
L36:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L37:	;
	actionLogPush(tc_structfield, entry, p);	/* ACTION AFTER structDeclarator */
	return 1;
L35:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fieldName */
static int p_fieldName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L38;
	return 1;
L38:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* typedefDecl */
static int p_typedefDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "typedef", 7) != 0) goto L41;
	if (idch((unsigned char)p[7])) goto L41;
	p += 7;
	if (!p_typedefType()) goto L41;
	if (!p_typedefTargetName()) goto L41;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L41;
	p += 1;
	goto L40;
L41:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_fnPtrTypedef()) goto L42;
	goto L40;
L42:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L39;
L40:	sp--;
	actionLogPush(tc_typedefend, entry, p);	/* ACTION AFTER typedefDecl */
	return 1;
L39:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrTypedef */
static int p_fnPtrTypedef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "typedef", 7) != 0) goto L43;
	if (idch((unsigned char)p[7])) goto L43;
	p += 7;
	if (!p_type()) goto L43;
	if (!p_pointerDecl()) goto L43;
	if (!p_fnPtrOpen()) goto L43;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L43;
	if (strncmp(p, "*=", 2) == 0) goto L43;	/* Longest-Match */
	p += 1;
	if (!p_fnPtrName()) goto L43;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L43;
	p += 1;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L43;
	p += 1;
	if (!p_externParamList()) goto L43;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L43;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L43;
	p += 1;
	actionLogPush(tc_fnptrtypedef, entry, p);	/* ACTION AFTER fnPtrTypedef */
	return 1;
L43:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrOpen */
static int p_fnPtrOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L44;
	p += 1;
	actionLogPush(tc_fnptrbegin, entry, p);	/* ACTION AFTER fnPtrOpen */
	return 1;
L44:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrName */
static int p_fnPtrName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L45;
	return 1;
L45:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* typedefType */
static int p_typedefType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_anonStructType()) goto L48;
	goto L47;
L48:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_type()) goto L49;
	if (!p_pointerDecl()) goto L49;
	goto L47;
L49:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L46;
L47:	sp--;
	return 1;
L46:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* anonStructType */
static int p_anonStructType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L50;
	if (idch((unsigned char)p[6])) goto L50;
	p += 6;
	if (!p_anonStructOpen()) goto L50;
	if (!p_structField()) goto L50;
L51:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L52;
	sp--; goto L51;
L52:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L50;
	p += 1;
	return 1;
L50:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* anonStructOpen */
static int p_anonStructOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L53;
	p += 1;
	actionLogPush(tc_anonstructbegin, entry, p);	/* ACTION AFTER anonStructOpen */
	return 1;
L53:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* typedefTargetName */
static int p_typedefTargetName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L54;
	return 1;
L54:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalDecl */
static int p_globalDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_externGlobalDecl()) goto L57;
	goto L56;
L57:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_plainGlobalDecl()) goto L58;
	goto L56;
L58:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L55;
L56:	sp--;
	return 1;
L55:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* externGlobalDecl */
static int p_externGlobalDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "extern", 6) != 0) goto L59;
	if (idch((unsigned char)p[6])) goto L59;
	p += 6;
	if (!p_type()) goto L59;
	if (!p_pointerDecl()) goto L59;
	if (!p_globalName()) goto L59;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L59;
	p += 1;
	actionLogPush(tc_externglobaldecl, entry, p);	/* ACTION AFTER externGlobalDecl */
	return 1;
L59:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* plainGlobalDecl */
static int p_plainGlobalDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_staticKw()) goto L61;
	sp--; goto L62;
L61:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L62:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L63;
	sp--; goto L64;
L63:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L64:	;
	if (!p_type()) goto L60;
	if (!p_pointerDecl()) goto L60;
	if (!p_globalName()) goto L60;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L65;
L67:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L68;
	sp--; goto L67;
L68:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L66;
L65:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L66:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L69;
	if (strncmp(p, "==", 2) == 0) goto L69;	/* Longest-Match */
	p += 1;
	if (!p_globalInit()) goto L69;
	sp--; goto L70;
L69:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L70:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L60;
	p += 1;
	actionLogPush(tc_globalend, entry, p);	/* ACTION AFTER plainGlobalDecl */
	return 1;
L60:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* constKw */
static int p_constKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "const", 5) != 0) goto L71;
	if (idch((unsigned char)p[5])) goto L71;
	p += 5;
	actionLogPush(tc_const, entry, p);	/* ACTION AFTER constKw */
	return 1;
L71:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticKw */
static int p_staticKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "static", 6) != 0) goto L72;
	if (idch((unsigned char)p[6])) goto L72;
	p += 6;
	actionLogPush(tc_static, entry, p);	/* ACTION AFTER staticKw */
	return 1;
L72:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalInit */
static int p_globalInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalValue()) goto L75;
	goto L74;
L75:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L76;
	goto L74;
L76:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalStringInit()) goto L77;
	goto L74;
L77:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L73;
L74:	sp--;
	return 1;
L73:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalStringInit */
static int p_globalStringInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L78;
	p += 1;
L79:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_character()) goto L80;
	sp--; goto L79;
L80:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L78;
	p += 1;
	return 1;
L78:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* arraySize */
static int p_arraySize(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L81;
	p += 1;
	if (!p_globalNumber()) goto L81;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L81;
	p += 1;
	return 1;
L81:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* arraySizeN */
static int p_arraySizeN(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L82;
	p += 1;
	if (!p_globalNumber()) goto L82;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L82;
	p += 1;
	return 1;
L82:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalValue */
static int p_globalValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalNumber()) goto L85;
	goto L84;
L85:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalNeg()) goto L86;
	goto L84;
L86:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalBool()) goto L87;
	goto L84;
L87:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L83;
L84:	sp--;
	return 1;
L83:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalBool */
static int p_globalBool(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "true", 4) != 0) goto L90;
	if (idch((unsigned char)p[4])) goto L90;
	p += 4;
	goto L89;
L90:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L91;
	if (idch((unsigned char)p[5])) goto L91;
	p += 5;
	goto L89;
L91:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L88;
L89:	sp--;
	return 1;
L88:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalNeg */
static int p_globalNeg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L92;
	if (strncmp(p, "-=", 2) == 0) goto L92;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L92;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L92;	/* Longest-Match */
	p += 1;
	if (!p_globalNumber()) goto L92;
	return 1;
L92:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalNumber */
static int p_globalNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L93;
L94:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L95;
	sp--; goto L94;
L95:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L93:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcdef */
static int p_funcdef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcHead()) goto L96;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_funcBody()) goto L98;
	goto L97;
L98:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_protoEnd()) goto L99;
	goto L97;
L99:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L96;
L97:	sp--;
	actionLogPush(tc_funcend, entry, p);	/* ACTION AFTER funcdef */
	return 1;
L96:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcBody */
static int p_funcBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcBodyOpen()) goto L100;
L101:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L102;
	sp--; goto L101;
L102:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L100;
	p += 1;
	return 1;
L100:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcBodyOpen */
static int p_funcBodyOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L103;
	p += 1;
	actionLogPush(tc_funcbodybegin, entry, p);	/* ACTION AFTER funcBodyOpen */
	return 1;
L103:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* protoEnd */
static int p_protoEnd(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L104;
	p += 1;
	actionLogPush(tc_funcdeclend, entry, p);	/* ACTION AFTER protoEnd */
	return 1;
L104:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcHead */
static int p_funcHead(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_staticKw()) goto L106;
	sp--; goto L107;
L106:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L107:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_retConstKw()) goto L108;
	sp--; goto L109;
L108:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L109:	;
	if (!p_type()) goto L105;
	if (!p_pointerDecl()) goto L105;
	if (!p_defName()) goto L105;
	if (!p_funcParams()) goto L105;
	actionLogPush(tc_funcbegin, entry, p);	/* ACTION AFTER funcHead */
	return 1;
L105:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* retConstKw */
static int p_retConstKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "const", 5) != 0) goto L110;
	if (idch((unsigned char)p[5])) goto L110;
	p += 5;
	return 1;
L110:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcParams */
static int p_funcParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_voidParams()) goto L113;
	goto L112;
L113:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_normalParams()) goto L114;
	goto L112;
L114:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L111;
L112:	sp--;
	return 1;
L111:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* voidParams */
static int p_voidParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L115;
	p += 1;
	ws();
	if (strncmp(p, "void", 4) != 0) goto L115;
	if (idch((unsigned char)p[4])) goto L115;
	p += 4;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L115;
	p += 1;
	return 1;
L115:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* normalParams */
static int p_normalParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L116;
	p += 1;
	if (!p_paramList()) goto L116;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L116;
	p += 1;
	return 1;
L116:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* paramList */
static int p_paramList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_param()) goto L118;
L120:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L121;
	p += 1;
	if (!p_param()) goto L121;
	sp--; goto L120;
L121:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L119;
L118:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L119:	;
	return 1;
L117:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* param */
static int p_param(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L123;
	sp--; goto L124;
L123:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L124:	;
	if (!p_type()) goto L122;
	if (!p_pointerDecl()) goto L122;
	if (!p_paramDecl()) goto L122;
	return 1;
L122:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* paramDecl */
static int p_paramDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_paramName()) goto L125;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_paramArray()) goto L126;
	sp--; goto L127;
L126:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L127:	;
	actionLogPush(tc_param, entry, p);	/* ACTION AFTER paramDecl */
	return 1;
L125:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* paramArray */
static int p_paramArray(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L128;
	p += 1;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L128;
	p += 1;
	return 1;
L128:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* block */
static int p_block(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L129;
	p += 1;
L130:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L131;
	sp--; goto L130;
L131:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L129;
	p += 1;
	return 1;
L129:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* statement */
static int p_statement(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_labelStmt()) goto L134;
	goto L133;
L134:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_unlabeledStmt()) goto L135;
	goto L133;
L135:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L132;
L133:	sp--;
	return 1;
L132:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* unlabeledStmt */
static int p_unlabeledStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_ifStmt()) goto L138;
	goto L137;
L138:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_whileStmt()) goto L139;
	goto L137;
L139:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_forStmt()) goto L140;
	goto L137;
L140:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_doStmt()) goto L141;
	goto L137;
L141:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_switchStmt()) goto L142;
	goto L137;
L142:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_breakStmt()) goto L143;
	goto L137;
L143:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_continueStmt()) goto L144;
	goto L137;
L144:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_gotoStmt()) goto L145;
	goto L137;
L145:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_returnStmt()) goto L146;
	goto L137;
L146:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_block()) goto L147;
	goto L137;
L147:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_emptyStmt()) goto L148;
	goto L137;
L148:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_voidCastStmt()) goto L149;
	goto L137;
L149:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_callStmt()) goto L150;
	goto L137;
L150:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incDecStmt()) goto L151;
	goto L137;
L151:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_assignStmt()) goto L152;
	goto L137;
L152:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticVarDecl()) goto L153;
	goto L137;
L153:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varDecl()) goto L154;
	goto L137;
L154:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L136;
L137:	sp--;
	return 1;
L136:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticVarDecl */
static int p_staticVarDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_staticKw()) goto L155;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L156;
	sp--; goto L157;
L156:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L157:	;
	if (!p_type()) goto L155;
	if (!p_pointerDecl()) goto L155;
	if (!p_staticLocalName()) goto L155;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L158;
	if (strncmp(p, "==", 2) == 0) goto L158;	/* Longest-Match */
	p += 1;
	if (!p_staticInit()) goto L158;
	sp--; goto L159;
L158:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L159:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L155;
	p += 1;
	actionLogPush(tc_staticlocal, entry, p);	/* ACTION AFTER staticVarDecl */
	return 1;
L155:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticLocalName */
static int p_staticLocalName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L160;
	actionLogPush(tc_staticlocalname, entry, p);	/* ACTION AFTER staticLocalName */
	return 1;
L160:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticInit */
static int p_staticInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalValue()) goto L163;
	goto L162;
L163:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticRuntimeInit()) goto L164;
	goto L162;
L164:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L161;
L162:	sp--;
	return 1;
L161:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticRuntimeInit */
static int p_staticRuntimeInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L165;
	actionLogPush(tc_staticruntimeinit, entry, p);	/* ACTION AFTER staticRuntimeInit */
	return 1;
L165:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchStmt */
static int p_switchStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_switchKw()) goto L166;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L166;
	p += 1;
	if (!p_switchCond()) goto L166;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L166;
	p += 1;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L166;
	p += 1;
L167:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseGroup()) goto L168;
	sp--; goto L167;
L168:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_defaultGroup()) goto L169;
	sp--; goto L170;
L169:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L170:	;
	if (!p_switchClose()) goto L166;
	actionLogPush(tc_switchend, entry, p);	/* ACTION AFTER switchStmt */
	return 1;
L166:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchKw */
static int p_switchKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "switch", 6) != 0) goto L171;
	if (idch((unsigned char)p[6])) goto L171;
	p += 6;
	actionLogPush(tc_switchbegin, entry, p);	/* ACTION AFTER switchKw */
	return 1;
L171:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchCond */
static int p_switchCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L172;
	actionLogPush(tc_switchcond, entry, p);	/* ACTION AFTER switchCond */
	return 1;
L172:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchClose */
static int p_switchClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "}", 1) != 0) goto L173;
	p += 1;
	return 1;
L173:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseGroup */
static int p_caseGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabelRun()) goto L174;
	if (!p_caseBody()) goto L174;
	actionLogPush(tc_casegroup_end, entry, p);	/* ACTION AFTER caseGroup */
	return 1;
L174:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseLabelRun */
static int p_caseLabelRun(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabel()) goto L175;
L176:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseLabel()) goto L177;
	sp--; goto L176;
L177:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(tc_caselabelrun_end, entry, p);	/* ACTION AFTER caseLabelRun */
	return 1;
L175:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseLabel */
static int p_caseLabel(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "case", 4) != 0) goto L178;
	if (idch((unsigned char)p[4])) goto L178;
	p += 4;
	if (!p_caseValue()) goto L178;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L178;
	p += 1;
	actionLogPush(tc_caselabel, entry, p);	/* ACTION AFTER caseLabel */
	return 1;
L178:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseValue */
static int p_caseValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_ident()) goto L181;
	goto L180;
L181:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNeg()) goto L182;
	goto L180;
L182:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNumber()) goto L183;
	goto L180;
L183:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L179;
L180:	sp--;
	return 1;
L179:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseNeg */
static int p_caseNeg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L184;
	if (strncmp(p, "-=", 2) == 0) goto L184;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L184;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L184;	/* Longest-Match */
	p += 1;
	if (!p_caseNumber()) goto L184;
	return 1;
L184:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseNumber */
static int p_caseNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L185;
L186:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L187;
	sp--; goto L186;
L187:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L185:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseBody */
static int p_caseBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L189:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_unlabeledStmt()) goto L190;
	sp--; goto L189;
L190:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L188:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* defaultGroup */
static int p_defaultGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_defaultLabel()) goto L191;
	if (!p_caseBody()) goto L191;
	return 1;
L191:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* defaultLabel */
static int p_defaultLabel(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "default", 7) != 0) goto L192;
	if (idch((unsigned char)p[7])) goto L192;
	p += 7;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L192;
	p += 1;
	actionLogPush(tc_defaultlabel, entry, p);	/* ACTION AFTER defaultLabel */
	return 1;
L192:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* varDecl */
static int p_varDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L194;
	sp--; goto L195;
L194:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L195:	;
	if (!p_type()) goto L193;
	if (!p_pointerDecl()) goto L193;
	if (!p_varDeclarator()) goto L193;
L196:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L197;
	p += 1;
	if (!p_varDeclarator()) goto L197;
	sp--; goto L196;
L197:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L193;
	p += 1;
	return 1;
L193:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* varDeclarator */
static int p_varDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_localDecl()) goto L198;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L199;
	if (strncmp(p, "==", 2) == 0) goto L199;	/* Longest-Match */
	p += 1;
	if (!p_varInit()) goto L199;
	sp--; goto L200;
L199:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L200:	;
	return 1;
L198:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* localDecl */
static int p_localDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_localName()) goto L201;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L202;
L204:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L205;
	sp--; goto L204;
L205:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L203;
L202:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L203:	;
	actionLogPush(tc_localdecl, entry, p);	/* ACTION AFTER localDecl */
	return 1;
L201:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* varInit */
static int p_varInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arrayStringInit()) goto L208;
	goto L207;
L208:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_expr()) goto L209;
	goto L207;
L209:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L210;
	goto L207;
L210:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L206;
L207:	sp--;
	actionLogPush(tc_varinit, entry, p);	/* ACTION AFTER varInit */
	return 1;
L206:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* arrayStringInit */
static int p_arrayStringInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_stringLit()) goto L211;
	actionLogPush(tc_arrayinitstring, entry, p);	/* ACTION AFTER arrayStringInit */
	return 1;
L211:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initList */
static int p_initList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L212;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_initValue()) goto L213;
L215:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L216;
	p += 1;
	if (!p_initValue()) goto L216;
	sp--; goto L215;
L216:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L214;
L213:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L214:	;
	ws();
	if (strncmp(p, "}", 1) != 0) goto L212;
	p += 1;
	return 1;
L212:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initValue */
static int p_initValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_initNumber()) goto L219;
	goto L218;
L219:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initNeg()) goto L220;
	goto L218;
L220:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initBool()) goto L221;
	goto L218;
L221:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L217;
L218:	sp--;
	return 1;
L217:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initBool */
static int p_initBool(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "true", 4) != 0) goto L224;
	if (idch((unsigned char)p[4])) goto L224;
	p += 4;
	goto L223;
L224:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L225;
	if (idch((unsigned char)p[5])) goto L225;
	p += 5;
	goto L223;
L225:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L222;
L223:	sp--;
	return 1;
L222:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initNeg */
static int p_initNeg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L226;
	if (strncmp(p, "-=", 2) == 0) goto L226;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L226;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L226;	/* Longest-Match */
	p += 1;
	if (!p_initNumber()) goto L226;
	return 1;
L226:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initNumber */
static int p_initNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L227;
L228:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L229;
	sp--; goto L228;
L229:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L227:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* assignStmt */
static int p_assignStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L230;
	if (!p_assignop()) goto L230;
	if (!p_expr()) goto L230;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L230;
	p += 1;
	actionLogPush(tc_assign, entry, p);	/* ACTION AFTER assignStmt */
	return 1;
L230:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* assignop */
static int p_assignop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L233;
	if (strncmp(p, "==", 2) == 0) goto L233;	/* Longest-Match */
	p += 1;
	goto L232;
L233:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "+=", 2) != 0) goto L234;
	p += 2;
	goto L232;
L234:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-=", 2) != 0) goto L235;
	p += 2;
	goto L232;
L235:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "*=", 2) != 0) goto L236;
	p += 2;
	goto L232;
L236:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/=", 2) != 0) goto L237;
	p += 2;
	goto L232;
L237:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%=", 2) != 0) goto L238;
	p += 2;
	goto L232;
L238:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<<=", 3) != 0) goto L239;
	p += 3;
	goto L232;
L239:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>=", 3) != 0) goto L240;
	p += 3;
	goto L232;
L240:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "&=", 2) != 0) goto L241;
	p += 2;
	goto L232;
L241:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "^=", 2) != 0) goto L242;
	p += 2;
	goto L232;
L242:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "|=", 2) != 0) goto L243;
	p += 2;
	goto L232;
L243:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L231;
L232:	sp--;
	actionLogPush(tc_assignop, entry, p);	/* ACTION AFTER assignop */
	return 1;
L231:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* callStmt */
static int p_callStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_call()) goto L246;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L246;
	p += 1;
	goto L245;
L246:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectCall()) goto L247;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L247;
	p += 1;
	goto L245;
L247:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L244;
L245:	sp--;
	actionLogPush(tc_callstmt, entry, p);	/* ACTION AFTER callStmt */
	return 1;
L244:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* emptyStmt */
static int p_emptyStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L248;
	p += 1;
	return 1;
L248:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* voidCastStmt */
static int p_voidCastStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_voidCastOpen()) goto L249;
	if (!p_expr()) goto L249;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L249;
	p += 1;
	actionLogPush(tc_voidcast, entry, p);	/* ACTION AFTER voidCastStmt */
	return 1;
L249:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* voidCastOpen */
static int p_voidCastOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L250;
	p += 1;
	ws();
	if (strncmp(p, "void", 4) != 0) goto L250;
	if (idch((unsigned char)p[4])) goto L250;
	p += 4;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L250;
	p += 1;
	return 1;
L250:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* returnStmt */
static int p_returnStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "return", 6) != 0) goto L251;
	if (idch((unsigned char)p[6])) goto L251;
	p += 6;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_retVal()) goto L252;
	sp--; goto L253;
L252:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L253:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L251;
	p += 1;
	actionLogPush(tc_return, entry, p);	/* ACTION AFTER returnStmt */
	return 1;
L251:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* retVal */
static int p_retVal(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L254;
	actionLogPush(tc_retval, entry, p);	/* ACTION AFTER retVal */
	return 1;
L254:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ifStmt */
static int p_ifStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_ifKw()) goto L255;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L255;
	p += 1;
	if (!p_ifCond()) goto L255;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L255;
	p += 1;
	if (!p_thenPart()) goto L255;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_elseKw()) goto L256;
	if (!p_elsePart()) goto L256;
	sp--; goto L257;
L256:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L257:	;
	actionLogPush(tc_ifend, entry, p);	/* ACTION AFTER ifStmt */
	return 1;
L255:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ifKw */
static int p_ifKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "if", 2) != 0) goto L258;
	if (idch((unsigned char)p[2])) goto L258;
	p += 2;
	actionLogPush(tc_ifbegin, entry, p);	/* ACTION AFTER ifKw */
	return 1;
L258:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* elseKw */
static int p_elseKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "else", 4) != 0) goto L259;
	if (idch((unsigned char)p[4])) goto L259;
	p += 4;
	return 1;
L259:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ifCond */
static int p_ifCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L260;
	actionLogPush(tc_ifcond, entry, p);	/* ACTION AFTER ifCond */
	return 1;
L260:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* thenPart */
static int p_thenPart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L261;
	actionLogPush(tc_thenend, entry, p);	/* ACTION AFTER thenPart */
	return 1;
L261:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* elsePart */
static int p_elsePart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L262;
	return 1;
L262:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileStmt */
static int p_whileStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_whileKw()) goto L263;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L263;
	p += 1;
	if (!p_whileCond()) goto L263;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L263;
	p += 1;
	if (!p_whileBody()) goto L263;
	actionLogPush(tc_whileend, entry, p);	/* ACTION AFTER whileStmt */
	return 1;
L263:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileKw */
static int p_whileKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "while", 5) != 0) goto L264;
	if (idch((unsigned char)p[5])) goto L264;
	p += 5;
	actionLogPush(tc_whilebegin, entry, p);	/* ACTION AFTER whileKw */
	return 1;
L264:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileCond */
static int p_whileCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L265;
	actionLogPush(tc_whilecond, entry, p);	/* ACTION AFTER whileCond */
	return 1;
L265:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileBody */
static int p_whileBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L266;
	return 1;
L266:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forStmt */
static int p_forStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_forKw()) goto L267;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L267;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forInit()) goto L268;
	sp--; goto L269;
L268:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L269:	;
	if (!p_forSep1()) goto L267;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forCond()) goto L270;
	sp--; goto L271;
L270:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L271:	;
	if (!p_forSep2()) goto L267;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forStep()) goto L272;
	sp--; goto L273;
L272:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L273:	;
	if (!p_forClose()) goto L267;
	if (!p_forBody()) goto L267;
	actionLogPush(tc_forend, entry, p);	/* ACTION AFTER forStmt */
	return 1;
L267:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forKw */
static int p_forKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "for", 3) != 0) goto L274;
	if (idch((unsigned char)p[3])) goto L274;
	p += 3;
	actionLogPush(tc_forbegin, entry, p);	/* ACTION AFTER forKw */
	return 1;
L274:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forSep1 */
static int p_forSep1(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L275;
	p += 1;
	actionLogPush(tc_forsep1, entry, p);	/* ACTION AFTER forSep1 */
	return 1;
L275:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forSep2 */
static int p_forSep2(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L276;
	p += 1;
	actionLogPush(tc_forsep2, entry, p);	/* ACTION AFTER forSep2 */
	return 1;
L276:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forClose */
static int p_forClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L277;
	p += 1;
	actionLogPush(tc_forclose, entry, p);	/* ACTION AFTER forClose */
	return 1;
L277:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forInit */
static int p_forInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L278;
	if (!p_assignop()) goto L278;
	if (!p_expr()) goto L278;
	actionLogPush(tc_assign, entry, p);	/* ACTION AFTER forInit */
	return 1;
L278:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forCond */
static int p_forCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L279;
	actionLogPush(tc_forcond, entry, p);	/* ACTION AFTER forCond */
	return 1;
L279:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forStep */
static int p_forStep(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_target()) goto L282;
	if (!p_assignop()) goto L282;
	if (!p_expr()) goto L282;
	goto L281;
L282:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L283;
	goto L281;
L283:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L284;
	goto L281;
L284:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L280;
L281:	sp--;
	actionLogPush(tc_forstep, entry, p);	/* ACTION AFTER forStep */
	return 1;
L280:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forBody */
static int p_forBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L285;
	return 1;
L285:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doStmt */
static int p_doStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_doKw()) goto L286;
	if (!p_doBody()) goto L286;
	if (!p_doWhileTok()) goto L286;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L286;
	p += 1;
	if (!p_doCond()) goto L286;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L286;
	p += 1;
	if (!p_doClose()) goto L286;
	actionLogPush(tc_doend, entry, p);	/* ACTION AFTER doStmt */
	return 1;
L286:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doKw */
static int p_doKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "do", 2) != 0) goto L287;
	if (idch((unsigned char)p[2])) goto L287;
	p += 2;
	actionLogPush(tc_dobegin, entry, p);	/* ACTION AFTER doKw */
	return 1;
L287:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doWhileTok */
static int p_doWhileTok(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "while", 5) != 0) goto L288;
	if (idch((unsigned char)p[5])) goto L288;
	p += 5;
	actionLogPush(tc_dowhiletok, entry, p);	/* ACTION AFTER doWhileTok */
	return 1;
L288:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doCond */
static int p_doCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L289;
	actionLogPush(tc_docond, entry, p);	/* ACTION AFTER doCond */
	return 1;
L289:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doClose */
static int p_doClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L290;
	p += 1;
	return 1;
L290:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doBody */
static int p_doBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L291;
	return 1;
L291:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* breakStmt */
static int p_breakStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "break", 5) != 0) goto L292;
	if (idch((unsigned char)p[5])) goto L292;
	p += 5;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L292;
	p += 1;
	actionLogPush(tc_break, entry, p);	/* ACTION AFTER breakStmt */
	return 1;
L292:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* continueStmt */
static int p_continueStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "continue", 8) != 0) goto L293;
	if (idch((unsigned char)p[8])) goto L293;
	p += 8;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L293;
	p += 1;
	actionLogPush(tc_continue, entry, p);	/* ACTION AFTER continueStmt */
	return 1;
L293:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* gotoStmt */
static int p_gotoStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "goto", 4) != 0) goto L294;
	if (idch((unsigned char)p[4])) goto L294;
	p += 4;
	ws();
	if (!p_ident()) goto L294;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L294;
	p += 1;
	actionLogPush(tc_goto, entry, p);	/* ACTION AFTER gotoStmt */
	return 1;
L294:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* labelStmt */
static int p_labelStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L295;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L295;
	p += 1;
	actionLogPush(tc_label, entry, p);	/* ACTION AFTER labelStmt */
	return 1;
L295:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* expr */
static int p_expr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L296;
	return 1;
L296:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalExpr */
static int p_conditionalExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_orExpr()) goto L297;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_qmark()) goto L298;
	if (!p_conditionalTrue()) goto L298;
	if (!p_colon()) goto L298;
	if (!p_conditionalFalse()) goto L298;
	sp--; goto L299;
L298:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L299:	;
	actionLogPush(tc_ternaryend, entry, p);	/* ACTION AFTER conditionalExpr */
	return 1;
L297:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* qmark */
static int p_qmark(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "?", 1) != 0) goto L300;
	p += 1;
	actionLogPush(tc_ternarybegin, entry, p);	/* ACTION AFTER qmark */
	return 1;
L300:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalTrue */
static int p_conditionalTrue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L301;
	return 1;
L301:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* colon */
static int p_colon(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L302;
	p += 1;
	actionLogPush(tc_ternarymiddle, entry, p);	/* ACTION AFTER colon */
	return 1;
L302:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalFalse */
static int p_conditionalFalse(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L303;
	return 1;
L303:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* orExpr */
static int p_orExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_andExpr()) goto L304;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_orop()) goto L305;
	if (!p_orExpr()) goto L305;
	sp--; goto L306;
L305:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L306:	;
	actionLogPush(tc_logicorend, entry, p);	/* ACTION AFTER orExpr */
	return 1;
L304:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* orop */
static int p_orop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "||", 2) != 0) goto L307;
	p += 2;
	actionLogPush(tc_logicorop, entry, p);	/* ACTION AFTER orop */
	return 1;
L307:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* andExpr */
static int p_andExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitOrExpr()) goto L308;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_andop()) goto L309;
	if (!p_andExpr()) goto L309;
	sp--; goto L310;
L309:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L310:	;
	actionLogPush(tc_logicandend, entry, p);	/* ACTION AFTER andExpr */
	return 1;
L308:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* andop */
static int p_andop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "&&", 2) != 0) goto L311;
	p += 2;
	actionLogPush(tc_logicandop, entry, p);	/* ACTION AFTER andop */
	return 1;
L311:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitOrExpr */
static int p_bitOrExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitXorExpr()) goto L312;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitorop()) goto L313;
	if (!p_bitOrExpr()) goto L313;
	sp--; goto L314;
L313:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L314:	;
	actionLogPush(tc_bitorend, entry, p);	/* ACTION AFTER bitOrExpr */
	return 1;
L312:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitorop */
static int p_bitorop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "|", 1) != 0) goto L315;
	if (strncmp(p, "|=", 2) == 0) goto L315;	/* Longest-Match */
	if (strncmp(p, "||", 2) == 0) goto L315;	/* Longest-Match */
	p += 1;
	actionLogPush(tc_bitorop, entry, p);	/* ACTION AFTER bitorop */
	return 1;
L315:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitXorExpr */
static int p_bitXorExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitAndExpr()) goto L316;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitxorop()) goto L317;
	if (!p_bitXorExpr()) goto L317;
	sp--; goto L318;
L317:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L318:	;
	actionLogPush(tc_bitxorend, entry, p);	/* ACTION AFTER bitXorExpr */
	return 1;
L316:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitxorop */
static int p_bitxorop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "^", 1) != 0) goto L319;
	if (strncmp(p, "^=", 2) == 0) goto L319;	/* Longest-Match */
	p += 1;
	actionLogPush(tc_bitxorop, entry, p);	/* ACTION AFTER bitxorop */
	return 1;
L319:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitAndExpr */
static int p_bitAndExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_comparison()) goto L320;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitandop()) goto L321;
	if (!p_bitAndExpr()) goto L321;
	sp--; goto L322;
L321:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L322:	;
	actionLogPush(tc_bitandend, entry, p);	/* ACTION AFTER bitAndExpr */
	return 1;
L320:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitandop */
static int p_bitandop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "&", 1) != 0) goto L323;
	if (strncmp(p, "&=", 2) == 0) goto L323;	/* Longest-Match */
	if (strncmp(p, "&&", 2) == 0) goto L323;	/* Longest-Match */
	p += 1;
	actionLogPush(tc_bitandop, entry, p);	/* ACTION AFTER bitandop */
	return 1;
L323:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* comparison */
static int p_comparison(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_shiftExpr()) goto L324;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_relop()) goto L325;
	if (!p_shiftExpr()) goto L325;
	sp--; goto L326;
L325:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L326:	;
	actionLogPush(tc_expr, entry, p);	/* ACTION AFTER comparison */
	return 1;
L324:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* shiftExpr */
static int p_shiftExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L327;
L328:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_shiftop()) goto L329;
	if (!p_shiftRhs()) goto L329;
	sp--; goto L328;
L329:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L327:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* shiftRhs */
static int p_shiftRhs(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L330;
	actionLogPush(tc_shiftrhs, entry, p);	/* ACTION AFTER shiftRhs */
	return 1;
L330:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* shiftop */
static int p_shiftop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "<<", 2) != 0) goto L333;
	if (strncmp(p, "<<=", 3) == 0) goto L333;	/* Longest-Match */
	p += 2;
	goto L332;
L333:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>", 2) != 0) goto L334;
	if (strncmp(p, ">>=", 3) == 0) goto L334;	/* Longest-Match */
	p += 2;
	goto L332;
L334:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L331;
L332:	sp--;
	actionLogPush(tc_shiftop, entry, p);	/* ACTION AFTER shiftop */
	return 1;
L331:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* relop */
static int p_relop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "<=", 2) != 0) goto L337;
	p += 2;
	goto L336;
L337:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">=", 2) != 0) goto L338;
	p += 2;
	goto L336;
L338:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "==", 2) != 0) goto L339;
	p += 2;
	goto L336;
L339:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!=", 2) != 0) goto L340;
	p += 2;
	goto L336;
L340:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<", 1) != 0) goto L341;
	if (strncmp(p, "<<=", 3) == 0) goto L341;	/* Longest-Match */
	if (strncmp(p, "<<", 2) == 0) goto L341;	/* Longest-Match */
	if (strncmp(p, "<=", 2) == 0) goto L341;	/* Longest-Match */
	p += 1;
	goto L336;
L341:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">", 1) != 0) goto L342;
	if (strncmp(p, ">>=", 3) == 0) goto L342;	/* Longest-Match */
	if (strncmp(p, ">>", 2) == 0) goto L342;	/* Longest-Match */
	if (strncmp(p, ">=", 2) == 0) goto L342;	/* Longest-Match */
	p += 1;
	goto L336;
L342:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L335;
L336:	sp--;
	actionLogPush(tc_relop, entry, p);	/* ACTION AFTER relop */
	return 1;
L335:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* addExpr */
static int p_addExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_term()) goto L343;
L344:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_addop()) goto L345;
	if (!p_term()) goto L345;
	sp--; goto L344;
L345:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L343:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* addop */
static int p_addop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "+", 1) != 0) goto L348;
	if (strncmp(p, "+=", 2) == 0) goto L348;	/* Longest-Match */
	if (strncmp(p, "++", 2) == 0) goto L348;	/* Longest-Match */
	p += 1;
	goto L347;
L348:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-", 1) != 0) goto L349;
	if (strncmp(p, "-=", 2) == 0) goto L349;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L349;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L349;	/* Longest-Match */
	p += 1;
	goto L347;
L349:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L346;
L347:	sp--;
	actionLogPush(tc_addop, entry, p);	/* ACTION AFTER addop */
	return 1;
L346:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* term */
static int p_term(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L350;
L351:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_mulop()) goto L352;
	if (!p_factor()) goto L352;
	sp--; goto L351;
L352:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(tc_term, entry, p);	/* ACTION AFTER term */
	return 1;
L350:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* mulop */
static int p_mulop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L355;
	if (strncmp(p, "*=", 2) == 0) goto L355;	/* Longest-Match */
	p += 1;
	goto L354;
L355:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/", 1) != 0) goto L356;
	if (strncmp(p, "/=", 2) == 0) goto L356;	/* Longest-Match */
	p += 1;
	goto L354;
L356:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%", 1) != 0) goto L357;
	if (strncmp(p, "%=", 2) == 0) goto L357;	/* Longest-Match */
	p += 1;
	goto L354;
L357:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L353;
L354:	sp--;
	actionLogPush(tc_mulop, entry, p);	/* ACTION AFTER mulop */
	return 1;
L353:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* factor */
static int p_factor(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_sizeofExpr()) goto L360;
	goto L359;
L360:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_castExpr()) goto L361;
	goto L359;
L361:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L362;
	goto L359;
L362:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L363;
	goto L359;
L363:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "(", 1) != 0) goto L364;
	p += 1;
	if (!p_expr()) goto L364;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L364;
	p += 1;
	goto L359;
L364:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_call()) goto L365;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L366;
	sp--; goto L367;
L366:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L367:	;
	goto L359;
L365:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectCall()) goto L368;
	goto L359;
L368:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_boolLit()) goto L369;
	goto L359;
L369:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_addressRef()) goto L370;
	goto L359;
L370:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_derefRef()) goto L371;
	goto L359;
L371:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varRef()) goto L372;
	goto L359;
L372:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_charLit()) goto L373;
	goto L359;
L373:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_number()) goto L374;
	goto L359;
L374:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_stringLit()) goto L375;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L376;
	sp--; goto L377;
L376:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L377:	;
	goto L359;
L375:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_negFactor()) goto L378;
	goto L359;
L378:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L358;
L359:	sp--;
	actionLogPush(tc_factor, entry, p);	/* ACTION AFTER factor */
	return 1;
L358:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* postfixIndex */
static int p_postfixIndex(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_index()) goto L379;
	actionLogPush(tc_postfixindex, entry, p);	/* ACTION AFTER postfixIndex */
	return 1;
L379:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* stringLit (lexikalisch) */
static int p_stringLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\"", 1) != 0) goto L380;
	p += 1;
L381:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_character()) goto L382;
	sp--; goto L381;
L382:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	if (strncmp(p, "\"", 1) != 0) goto L380;
	p += 1;
	actionLogPush(tc_string, entry, p);	/* ACTION AFTER stringLit */
	return 1;
L380:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLit (lexikalisch) */
static int p_charLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "'", 1) != 0) goto L383;
	p += 1;
	if (!p_charLitBody()) goto L383;
	if (strncmp(p, "'", 1) != 0) goto L383;
	p += 1;
	actionLogPush(tc_charlit, entry, p);	/* ACTION AFTER charLit */
	return 1;
L383:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitBody (lexikalisch) */
static int p_charLitBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_charLitEscape()) goto L386;
	goto L385;
L386:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_charLitPlain()) goto L387;
	goto L385;
L387:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L384;
L385:	sp--;
	return 1;
L384:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitEscape (lexikalisch) */
static int p_charLitEscape(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\\", 1) != 0) goto L388;
	p += 1;
	if (!p_charLitEscChar()) goto L388;
	return 1;
L388:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitEscChar (lexikalisch) */
static int p_charLitEscChar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "n", 1) != 0) goto L391;
	p += 1;
	goto L390;
L391:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "t", 1) != 0) goto L392;
	p += 1;
	goto L390;
L392:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "r", 1) != 0) goto L393;
	p += 1;
	goto L390;
L393:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "0", 1) != 0) goto L394;
	p += 1;
	goto L390;
L394:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "\\", 1) != 0) goto L395;
	p += 1;
	goto L390;
L395:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L389;
L390:	sp--;
	return 1;
L389:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitPlain (lexikalisch) */
static int p_charLitPlain(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x26) goto L398;
	p++;
	goto L397;
L398:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x28 || (unsigned char)*p > 0x5B) goto L399;
	p++;
	goto L397;
L399:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x5D || (unsigned char)*p > 0x7E) goto L400;
	p++;
	goto L397;
L400:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L396;
L397:	sp--;
	return 1;
L396:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* character (lexikalisch) */
static int p_character(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_strEscape()) goto L403;
	goto L402;
L403:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x21) goto L404;
	p++;
	goto L402;
L404:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x23 || (unsigned char)*p > 0x7E) goto L405;
	p++;
	goto L402;
L405:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L401;
L402:	sp--;
	return 1;
L401:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* strEscape (lexikalisch) */
static int p_strEscape(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\\", 1) != 0) goto L406;
	p += 1;
	if (!p_strEscChar()) goto L406;
	return 1;
L406:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* strEscChar (lexikalisch) */
static int p_strEscChar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) goto L407;
	p++;
	return 1;
L407:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* negFactor */
static int p_negFactor(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L410;
	if (strncmp(p, "-=", 2) == 0) goto L410;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L410;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L410;	/* Longest-Match */
	p += 1;
	goto L409;
L410:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!", 1) != 0) goto L411;
	if (strncmp(p, "!=", 2) == 0) goto L411;	/* Longest-Match */
	p += 1;
	goto L409;
L411:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "~", 1) != 0) goto L412;
	p += 1;
	goto L409;
L412:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L408;
L409:	sp--;
	if (!p_factor()) goto L408;
	actionLogPush(tc_neg, entry, p);	/* ACTION AFTER negFactor */
	return 1;
L408:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* addressRef */
static int p_addressRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "&", 1) != 0) goto L413;
	if (strncmp(p, "&=", 2) == 0) goto L413;	/* Longest-Match */
	if (strncmp(p, "&&", 2) == 0) goto L413;	/* Longest-Match */
	p += 1;
	ws();
	if (!p_ident()) goto L413;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L414;
	sp--; goto L415;
L414:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L415:	;
	actionLogPush(tc_addressref, entry, p);	/* ACTION AFTER addressRef */
	return 1;
L413:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* sizeofExpr */
static int p_sizeofExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "sizeof", 6) != 0) goto L416;
	if (idch((unsigned char)p[6])) goto L416;
	p += 6;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L416;
	p += 1;
	if (!p_sizeofArg()) goto L416;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L416;
	p += 1;
	return 1;
L416:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* sizeofArg */
static int p_sizeofArg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_sizeofType()) goto L419;
	goto L418;
L419:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_sizeofVarName()) goto L420;
	goto L418;
L420:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L417;
L418:	sp--;
	return 1;
L417:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* sizeofType */
static int p_sizeofType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "unsigned", 8) != 0) goto L423;
	if (idch((unsigned char)p[8])) goto L423;
	p += 8;
	if (!p_unsignedInt()) goto L423;
	goto L422;
L423:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L424;
	if (idch((unsigned char)p[3])) goto L424;
	p += 3;
	goto L422;
L424:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L425;
	if (idch((unsigned char)p[4])) goto L425;
	p += 4;
	goto L422;
L425:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L426;
	if (idch((unsigned char)p[4])) goto L426;
	p += 4;
	goto L422;
L426:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L427;
	if (idch((unsigned char)p[6])) goto L427;
	p += 6;
	if (!p_structTypeRef()) goto L427;
	goto L422;
L427:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L421;
L422:	sp--;
	actionLogPush(tc_sizeof, entry, p);	/* ACTION AFTER sizeofType */
	return 1;
L421:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* sizeofVarName */
static int p_sizeofVarName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L428;
	actionLogPush(tc_sizeofvar, entry, p);	/* ACTION AFTER sizeofVarName */
	return 1;
L428:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* castExpr */
static int p_castExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L429;
	p += 1;
	if (!p_castType()) goto L429;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L429;
	p += 1;
	if (!p_castOperand()) goto L429;
	actionLogPush(tc_cast, entry, p);	/* ACTION AFTER castExpr */
	return 1;
L429:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* castType */
static int p_castType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "unsigned", 8) != 0) goto L432;
	if (idch((unsigned char)p[8])) goto L432;
	p += 8;
	if (!p_unsignedInt()) goto L432;
	goto L431;
L432:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L433;
	if (idch((unsigned char)p[3])) goto L433;
	p += 3;
	goto L431;
L433:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L434;
	if (idch((unsigned char)p[4])) goto L434;
	p += 4;
	goto L431;
L434:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L435;
	if (idch((unsigned char)p[4])) goto L435;
	p += 4;
	goto L431;
L435:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L430;
L431:	sp--;
	actionLogPush(tc_castcapture, entry, p);	/* ACTION AFTER castType */
	return 1;
L430:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* castOperand */
static int p_castOperand(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L436;
	return 1;
L436:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* preIncDec */
static int p_preIncDec(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_incdecOp()) goto L437;
	ws();
	if (!p_ident()) goto L437;
	actionLogPush(tc_preincdec, entry, p);	/* ACTION AFTER preIncDec */
	return 1;
L437:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* postIncDec */
static int p_postIncDec(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L438;
	if (!p_incdecOp()) goto L438;
	actionLogPush(tc_postincdec, entry, p);	/* ACTION AFTER postIncDec */
	return 1;
L438:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* incdecOp */
static int p_incdecOp(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "++", 2) != 0) goto L441;
	p += 2;
	goto L440;
L441:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "--", 2) != 0) goto L442;
	p += 2;
	goto L440;
L442:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L439;
L440:	sp--;
	return 1;
L439:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* incDecStmt */
static int p_incDecStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_preIncDec()) goto L445;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L445;
	p += 1;
	goto L444;
L445:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L446;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L446;
	p += 1;
	goto L444;
L446:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L443;
L444:	sp--;
	actionLogPush(tc_incdecstmt, entry, p);	/* ACTION AFTER incDecStmt */
	return 1;
L443:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* derefRef */
static int p_derefRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L447;
	if (strncmp(p, "*=", 2) == 0) goto L447;	/* Longest-Match */
	p += 1;
	if (!p_factor()) goto L447;
	actionLogPush(tc_derefref, entry, p);	/* ACTION AFTER derefRef */
	return 1;
L447:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* call */
static int p_call(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcName()) goto L448;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L448;
	p += 1;
	if (!p_argList()) goto L448;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L448;
	p += 1;
	actionLogPush(tc_call, entry, p);	/* ACTION AFTER call */
	return 1;
L448:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* indirectCall */
static int p_indirectCall(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_varRef()) goto L449;
	if (!p_indCallOpen()) goto L449;
	if (!p_argList()) goto L449;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L449;
	p += 1;
	actionLogPush(tc_indcall, entry, p);	/* ACTION AFTER indirectCall */
	return 1;
L449:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* indCallOpen */
static int p_indCallOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L450;
	p += 1;
	actionLogPush(tc_indcallbegin, entry, p);	/* ACTION AFTER indCallOpen */
	return 1;
L450:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* argList */
static int p_argList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arg()) goto L452;
L454:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L455;
	p += 1;
	if (!p_arg()) goto L455;
	sp--; goto L454;
L455:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L453;
L452:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L453:	;
	return 1;
L451:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* arg */
static int p_arg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L456;
	actionLogPush(tc_arg, entry, p);	/* ACTION AFTER arg */
	return 1;
L456:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* target */
static int p_target(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_directTarget()) goto L459;
	goto L458;
L459:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectTarget()) goto L460;
	goto L458;
L460:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L457;
L458:	sp--;
	return 1;
L457:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* directTarget */
static int p_directTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L461;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L462;
L464:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L465;
	sp--; goto L464;
L465:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L463;
L462:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L463:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L466;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L468;
	sp--; goto L469;
L468:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L469:	;
	sp--; goto L467;
L466:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L467:	;
	actionLogPush(tc_target, entry, p);	/* ACTION AFTER directTarget */
	return 1;
L461:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* indirectTarget */
static int p_indirectTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L470;
	if (strncmp(p, "*=", 2) == 0) goto L470;	/* Longest-Match */
	p += 1;
	if (!p_factor()) goto L470;
	actionLogPush(tc_indirecttarget, entry, p);	/* ACTION AFTER indirectTarget */
	return 1;
L470:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* varRef */
static int p_varRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L471;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L472;
L474:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L475;
	sp--; goto L474;
L475:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L473;
L472:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L473:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L476;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L478;
	sp--; goto L479;
L478:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L479:	;
	sp--; goto L477;
L476:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L477:	;
	actionLogPush(tc_varref, entry, p);	/* ACTION AFTER varRef */
	return 1;
L471:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* index */
static int p_index(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_indexOpen()) goto L480;
	if (!p_expr()) goto L480;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L480;
	p += 1;
	actionLogPush(tc_arg, entry, p);	/* ACTION AFTER index */
	return 1;
L480:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* indexOpen */
static int p_indexOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L481;
	p += 1;
	actionLogPush(tc_callname, entry, p);	/* ACTION AFTER indexOpen */
	return 1;
L481:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* member */
static int p_member(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ".", 1) != 0) goto L484;
	if (strncmp(p, "...", 3) == 0) goto L484;	/* Longest-Match */
	p += 1;
	if (!p_fieldName()) goto L484;
	goto L483;
L484:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "->", 2) != 0) goto L485;
	p += 2;
	if (!p_fieldName()) goto L485;
	goto L483;
L485:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L482;
L483:	sp--;
	return 1;
L482:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* defName */
static int p_defName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L486;
	actionLogPush(tc_defname, entry, p);	/* ACTION AFTER defName */
	return 1;
L486:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* paramName */
static int p_paramName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L487;
	return 1;
L487:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* localName */
static int p_localName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L488;
	actionLogPush(tc_local, entry, p);	/* ACTION AFTER localName */
	return 1;
L488:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalName */
static int p_globalName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L489;
	return 1;
L489:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcName */
static int p_funcName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L490;
	actionLogPush(tc_callname, entry, p);	/* ACTION AFTER funcName */
	return 1;
L490:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* type */
static int p_type(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "unsigned", 8) != 0) goto L493;
	if (idch((unsigned char)p[8])) goto L493;
	p += 8;
	if (!p_unsignedInt()) goto L493;
	goto L492;
L493:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L494;
	if (idch((unsigned char)p[3])) goto L494;
	p += 3;
	goto L492;
L494:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L495;
	if (idch((unsigned char)p[4])) goto L495;
	p += 4;
	goto L492;
L495:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L496;
	if (idch((unsigned char)p[4])) goto L496;
	p += 4;
	goto L492;
L496:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L497;
	if (idch((unsigned char)p[4])) goto L497;
	p += 4;
	goto L492;
L497:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L498;
	if (idch((unsigned char)p[6])) goto L498;
	p += 6;
	if (!p_structTypeRef()) goto L498;
	goto L492;
L498:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L499;
	if (idch((unsigned char)p[4])) goto L499;
	p += 4;
	if (!p_enumTypeRef()) goto L499;
	goto L492;
L499:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "void", 4) != 0) goto L500;
	if (idch((unsigned char)p[4])) goto L500;
	p += 4;
	goto L492;
L500:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_typedefRef()) goto L501;
	goto L492;
L501:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L491;
L492:	sp--;
	actionLogPush(tc_type, entry, p);	/* ACTION AFTER type */
	return 1;
L491:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structTypeRef */
static int p_structTypeRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L502;
	return 1;
L502:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* enumTypeRef */
static int p_enumTypeRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L503;
	return 1;
L503:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* typedefRef */
static int p_typedefRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L504;
	return 1;
L504:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* pointerDecl */
static int p_pointerDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L506:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_pointerStar()) goto L507;
	sp--; goto L506;
L507:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(tc_pointerdecl, entry, p);	/* ACTION AFTER pointerDecl */
	return 1;
L505:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* pointerStar */
static int p_pointerStar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L508;
	if (strncmp(p, "*=", 2) == 0) goto L508;	/* Longest-Match */
	p += 1;
	return 1;
L508:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* unsignedInt */
static int p_unsignedInt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "int", 3) != 0) goto L511;
	if (idch((unsigned char)p[3])) goto L511;
	p += 3;
	goto L510;
L511:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L512;
	if (idch((unsigned char)p[4])) goto L512;
	p += 4;
	goto L510;
L512:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L513;
	if (idch((unsigned char)p[4])) goto L513;
	p += 4;
	goto L510;
L513:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L509;
L510:	sp--;
	return 1;
L509:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* boolLit */
static int p_boolLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "true", 4) != 0) goto L516;
	if (idch((unsigned char)p[4])) goto L516;
	p += 4;
	goto L515;
L516:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L517;
	if (idch((unsigned char)p[5])) goto L517;
	p += 5;
	goto L515;
L517:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L514;
L515:	sp--;
	actionLogPush(tc_number, entry, p);	/* ACTION AFTER boolLit */
	return 1;
L514:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ident (lexikalisch) */
static int p_ident(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_letter()) goto L518;
L519:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_letter()) goto L522;
	goto L521;
L522:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_digit()) goto L523;
	goto L521;
L523:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L520;
L521:	sp--;
	sp--; goto L519;
L520:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L518:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* number (lexikalisch) */
static int p_number(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_hexNumber()) goto L526;
	goto L525;
L526:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_decNumber()) goto L527;
	goto L525;
L527:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L524;
L525:	sp--;
	actionLogPush(tc_number, entry, p);	/* ACTION AFTER number */
	return 1;
L524:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* hexNumber (lexikalisch) */
static int p_hexNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "0", 1) != 0) goto L528;
	p += 1;
	if (!p_hexMark()) goto L528;
	if (!p_hexDigit()) goto L528;
L529:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_hexDigit()) goto L530;
	sp--; goto L529;
L530:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L528:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* hexMark (lexikalisch) */
static int p_hexMark(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "x", 1) != 0) goto L533;
	p += 1;
	goto L532;
L533:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "X", 1) != 0) goto L534;
	p += 1;
	goto L532;
L534:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L531;
L532:	sp--;
	return 1;
L531:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* hexDigit (lexikalisch) */
static int p_hexDigit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L537;
	goto L536;
L537:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x61 || (unsigned char)*p > 0x66) goto L538;
	p++;
	goto L536;
L538:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x41 || (unsigned char)*p > 0x46) goto L539;
	p++;
	goto L536;
L539:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L535;
L536:	sp--;
	return 1;
L535:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* decNumber (lexikalisch) */
static int p_decNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_digit()) goto L540;
L541:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L542;
	sp--; goto L541;
L542:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L540:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* letter (lexikalisch) */
static int p_letter(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x61 || (unsigned char)*p > 0x7A) goto L545;
	p++;
	goto L544;
L545:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x41 || (unsigned char)*p > 0x5A) goto L546;
	p++;
	goto L544;
L546:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "_", 1) != 0) goto L547;
	p += 1;
	goto L544;
L547:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L543;
L544:	sp--;
	return 1;
L543:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* digit (lexikalisch) */
static int p_digit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if ((unsigned char)*p < 0x30 || (unsigned char)*p > 0x39) goto L548;
	p++;
	return 1;
L548:	p = entry; actionLogLen = entryLog;
	return 0;
}

#define INPUT_FILE_MAX 262144
static char inputFileBuf[INPUT_FILE_MAX];

int main(int argc, char* argv[]) {
	FILE* inputFile; size_t inputLen;
	if (argc < 2) { fprintf(stderr, "usage: %s <eingabe>\n", argv[0]); return 2; }
	if (argv[1][0] == '@') {
		inputFile = fopen(argv[1] + 1, "r");
		if (!inputFile) { fprintf(stderr, "can't open %s\n", argv[1] + 1); return 2; }
		inputLen = fread(inputFileBuf, 1, INPUT_FILE_MAX - 1, inputFile);
		fclose(inputFile); inputFileBuf[inputLen] = '\0'; p = inputFileBuf;
	} else p = argv[1];
	if (p_program()) { ws(); if (*p == '\0') { actionLogReplay(); printf("OK\n"); return 0; } }
	printf("FAIL\n");
	return 1;
}
