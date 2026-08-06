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
		fprintf(stderr, "tinyc: Aktions-Log-Grenze (%d) ueberschritten -- Eingabe zu gross/komplex fuer diese Version.\n", ACTION_LOG_MAX);
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
static int tcSameType(TCType a, TCType b) { return a.base == b.base && a.pointers == b.pointers && (a.base != 's' || a.structId == b.structId); }
static int tcIsPointer(TCType t) { return t.pointers != 0; }
static int tcIsInteger(TCType t) { return !t.pointers && (t.base == 'i' || t.base == 'u' || t.base == 'c' || t.base == 'z'); }
static int tcIsBool(TCType t) { return !t.pointers && t.base == 'b'; }
static TCType tcPointerTo(TCType t) { if (t.pointers < 255) t.pointers++; else tcSemanticErrors++; return t; }
static TCType tcPointee(TCType t) { if (t.pointers) t.pointers--; else tcSemanticErrors++; return t; }
static char tcTypeTag(TCType t) { return t.pointers ? 'p' : (t.base == 'c' || t.base == 'b') ? t.base : 'i'; }
static TCType tcPromoteInteger(TCType a, TCType b) { return tcMakeType(a.base == 'u' || b.base == 'u' ? 'u' : 'i', 0); }
static TCType tcLocalType(int slot) { return slot >= 0 && slot < tcLocalCount ? tcLocalTypes[slot] : tcMakeType('i', 0); }
static TCType tcGlobalType(int slot) { return slot >= 0 && slot < tcGlobalCount ? tcGlobalTypes[slot] : tcMakeType('i', 0); }
static int tcLookupFunction(const char* name) { int i; for (i = 0; i < tcFunctionCount; i++) if (tcEq(tcFunctionNames[i], name)) return i; return -1; }
static void tcTypePush(TCType type) { if (tcValueDepth < 256) tcValueTypes[tcValueDepth++] = type; else tcSemanticErrors++; }
static TCType tcTypePop(void) { return tcValueDepth > 0 ? tcValueTypes[--tcValueDepth] : tcBadType(); }
/* Prae-/Postinkrement/-dekrement, nur einfache int/unsigned/char-Skalare (lokal/global) --
   Praefix: LOAD;PUSH 1;ADD-oder-SUB;DUP;STORE (laesst NEUEN Wert); Postfix: LOAD;DUP;PUSH 1;
   ADD-oder-SUB;STORE (laesst ALTEN Wert). STOREC/STOREGC uebernehmen die Byte-Kuerzung wie
   bei normalen Zuweisungen -- kein extra NARROWC noetig. */
static void tcIncDecEmit(int slot, int global, TCType t, int isDec, int isPre) {
	char tag = tcTypeTag(t);
	const char* loadOp = slot >= 0 ? (tag == 'i' ? "LOADL" : "LOADC") : (tag == 'i' ? "LOADG" : "LOADGC");
	const char* storeOp = slot >= 0 ? (tag == 'i' ? "STOREL" : "STOREC") : (tag == 'i' ? "STOREG" : "STOREGC");
	if (slot >= 0) printf("%s %d\n", loadOp, slot); else printf("%s %s\n", loadOp, tcGlobalNames[global]);
	if (isPre) printf("PUSH 1\n%s\nDUP\n", isDec ? "SUB" : "ADD");
	else printf("DUP\nPUSH 1\n%s\n", isDec ? "SUB" : "ADD");
	if (slot >= 0) printf("%s %d\n", storeOp, slot); else printf("%s %s\n", storeOp, tcGlobalNames[global]);
	tcTypePush(t);
}
static int tcIncDecCheck(int slot, int global, TCType t) {
	if (tcIsPointer(t) || t.base == 's' || t.base == 'b') return 0;
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
	if (!tcIsBool(left)) tcTypeError("logical operator", tcMakeType('b', 0), left);
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
	right = tcTypePop(); if (!tcIsBool(right)) tcTypeError("logical operator", tcMakeType('b', 0), right);
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
	if (!tcIsBool(condition)) tcTypeError("conditional condition", tcMakeType('b', 0), condition);
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
	for (q = s; q < e; q++) v = v * 10 + (*q - '0');
	return v;
}
static const char* tcNameEnd(const char* s, const char* e) {
	const char* p = s;
	while (p < e && *p != '[' && *p != '.') p++;
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
	if (tcEqSpan(start, we, "unsigned")) tcCurrentType = tcMakeType('u', 0);
	else if (tcEqSpan(start, we, "int")) tcCurrentType = tcMakeType('i', 0);
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
	(void)start; (void)end;
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
	else if (end - p >= 12 && p[0] == 'u') { p += 12; type.base = 'u'; }
	else if (end - p >= 3 && p[0] == 'i' && p[1] == 'n' && p[2] == 't') p += 3;
	else if (end - p >= 4 && p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'r') { p += 4; type.base = 'c'; }
	else if (end - p >= 4 && p[0] == 'b' && p[1] == 'o' && p[2] == 'o' && p[3] == 'l') { p += 4; type.base = 'b'; }
	else if (end - p >= 4 && p[0] == 'v' && p[1] == 'o' && p[2] == 'i' && p[3] == 'd') { p += 4; type.base = 'v'; }
	else { fprintf(stderr, "qcc: bad global declaration\n"); return; }
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
		if (!tcIsBool(operand)) tcTypeError("logical negation", tcMakeType('b', 0), operand);
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
		if (ec >= 0 && !indexed) { printf("PUSH %ld\n", tcEnumConstValues[ec]); tcTypePush(tcMakeType('i', 0)); }
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
		else if (!pointerCompare && !(tcIsInteger(left) && tcIsInteger(right)) && !(tcIsBool(left) && tcIsBool(right))) tcTypeError("comparison", left, right);
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
	if (!tcIsBool(condition)) tcTypeError("if condition", tcMakeType('b', 0), condition);
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
	if (!tcIsBool(condition)) tcTypeError("while condition", tcMakeType('b', 0), condition);
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
	if (!tcIsBool(condition)) tcTypeError("for condition", tcMakeType('b', 0), condition);
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
	if (!tcIsBool(condition)) tcTypeError("do-while condition", tcMakeType('b', 0), condition);
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
static int p_fieldName(void);
static int p_typedefDecl(void);
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
static int p_paramList(void);
static int p_param(void);
static int p_paramDecl(void);
static int p_paramArray(void);
static int p_block(void);
static int p_statement(void);
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
static int p_character(void);
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
L8:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L2;
L3:	sp--;
	sp--; goto L1;
L2:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L9:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_funcdef()) goto L10;
	sp--; goto L9;
L10:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
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
	if (strncmp(p, "extern", 6) != 0) goto L11;
	if (idch((unsigned char)p[6])) goto L11;
	p += 6;
	if (!p_type()) goto L11;
	if (!p_pointerDecl()) goto L11;
	if (!p_externName()) goto L11;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L11;
	p += 1;
	if (!p_externParamList()) goto L11;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L11;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L11;
	p += 1;
	actionLogPush(tc_externdeclend, entry, p);	/* ACTION AFTER externDecl */
	return 1;
L11:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L12;
	actionLogPush(tc_externname, entry, p);	/* ACTION AFTER externName */
	return 1;
L12:	p = entry; actionLogLen = entryLog;
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
	if (!p_externParam()) goto L14;
L16:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L17;
	p += 1;
	if (!p_externParam()) goto L17;
	sp--; goto L16;
L17:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L18;
	p += 1;
	if (!p_ellipsisTok()) goto L18;
	sp--; goto L19;
L18:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L19:	;
	sp--; goto L15;
L14:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L15:	;
	return 1;
L13:	p = entry; actionLogLen = entryLog;
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
	if (!p_constKw()) goto L21;
	sp--; goto L22;
L21:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L22:	;
	if (!p_type()) goto L20;
	if (!p_pointerDecl()) goto L20;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_ident()) goto L23;
	sp--; goto L24;
L23:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L24:	;
	actionLogPush(tc_externparam, entry, p);	/* ACTION AFTER externParam */
	return 1;
L20:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "...", 3) != 0) goto L25;
	p += 3;
	actionLogPush(tc_externvariadic, entry, p);	/* ACTION AFTER ellipsisTok */
	return 1;
L25:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "enum", 4) != 0) goto L26;
	if (idch((unsigned char)p[4])) goto L26;
	p += 4;
	ws();
	if (!p_ident()) goto L26;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L26;
	p += 1;
	ws();
	if (!p_ident()) goto L26;
L27:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L28;
	p += 1;
	ws();
	if (!p_ident()) goto L28;
	sp--; goto L27;
L28:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L26;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L26;
	p += 1;
	actionLogPush(tc_enumdecl, entry, p);	/* ACTION AFTER enumDecl */
	return 1;
L26:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "struct", 6) != 0) goto L29;
	if (idch((unsigned char)p[6])) goto L29;
	p += 6;
	if (!p_structName()) goto L29;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L29;
	p += 1;
	if (!p_structField()) goto L29;
L30:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L31;
	sp--; goto L30;
L31:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L29;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L29;
	p += 1;
	actionLogPush(tc_structend, entry, p);	/* ACTION AFTER structDecl */
	return 1;
L29:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L32;
	actionLogPush(tc_structbegin, entry, p);	/* ACTION AFTER structName */
	return 1;
L32:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structField */
static int p_structField(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_type()) goto L33;
	if (!p_pointerDecl()) goto L33;
	if (!p_fieldName()) goto L33;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L34;
	sp--; goto L35;
L34:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L35:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L33;
	p += 1;
	actionLogPush(tc_structfield, entry, p);	/* ACTION AFTER structField */
	return 1;
L33:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L36;
	return 1;
L36:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* typedefDecl */
static int p_typedefDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "typedef", 7) != 0) goto L37;
	if (idch((unsigned char)p[7])) goto L37;
	p += 7;
	if (!p_typedefType()) goto L37;
	if (!p_typedefTargetName()) goto L37;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L37;
	p += 1;
	actionLogPush(tc_typedefend, entry, p);	/* ACTION AFTER typedefDecl */
	return 1;
L37:	p = entry; actionLogLen = entryLog;
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
	if (!p_anonStructType()) goto L40;
	goto L39;
L40:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_type()) goto L41;
	if (!p_pointerDecl()) goto L41;
	goto L39;
L41:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L38;
L39:	sp--;
	return 1;
L38:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "struct", 6) != 0) goto L42;
	if (idch((unsigned char)p[6])) goto L42;
	p += 6;
	if (!p_anonStructOpen()) goto L42;
	if (!p_structField()) goto L42;
L43:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L44;
	sp--; goto L43;
L44:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L42;
	p += 1;
	return 1;
L42:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "{", 1) != 0) goto L45;
	p += 1;
	actionLogPush(tc_anonstructbegin, entry, p);	/* ACTION AFTER anonStructOpen */
	return 1;
L45:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L46;
	return 1;
L46:	p = entry; actionLogLen = entryLog;
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
	if (!p_externGlobalDecl()) goto L49;
	goto L48;
L49:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_plainGlobalDecl()) goto L50;
	goto L48;
L50:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L47;
L48:	sp--;
	return 1;
L47:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "extern", 6) != 0) goto L51;
	if (idch((unsigned char)p[6])) goto L51;
	p += 6;
	if (!p_type()) goto L51;
	if (!p_pointerDecl()) goto L51;
	if (!p_globalName()) goto L51;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L51;
	p += 1;
	actionLogPush(tc_externglobaldecl, entry, p);	/* ACTION AFTER externGlobalDecl */
	return 1;
L51:	p = entry; actionLogLen = entryLog;
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
	if (!p_staticKw()) goto L53;
	sp--; goto L54;
L53:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L54:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L55;
	sp--; goto L56;
L55:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L56:	;
	if (!p_type()) goto L52;
	if (!p_pointerDecl()) goto L52;
	if (!p_globalName()) goto L52;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L57;
L59:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L60;
	sp--; goto L59;
L60:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L58;
L57:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L58:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L61;
	if (strncmp(p, "==", 2) == 0) goto L61;	/* Longest-Match */
	p += 1;
	if (!p_globalInit()) goto L61;
	sp--; goto L62;
L61:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L62:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L52;
	p += 1;
	actionLogPush(tc_globalend, entry, p);	/* ACTION AFTER plainGlobalDecl */
	return 1;
L52:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "const", 5) != 0) goto L63;
	if (idch((unsigned char)p[5])) goto L63;
	p += 5;
	actionLogPush(tc_const, entry, p);	/* ACTION AFTER constKw */
	return 1;
L63:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "static", 6) != 0) goto L64;
	if (idch((unsigned char)p[6])) goto L64;
	p += 6;
	actionLogPush(tc_static, entry, p);	/* ACTION AFTER staticKw */
	return 1;
L64:	p = entry; actionLogLen = entryLog;
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
	if (!p_globalValue()) goto L67;
	goto L66;
L67:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L68;
	goto L66;
L68:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalStringInit()) goto L69;
	goto L66;
L69:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L65;
L66:	sp--;
	return 1;
L65:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "\"", 1) != 0) goto L70;
	p += 1;
L71:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_character()) goto L72;
	sp--; goto L71;
L72:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L70;
	p += 1;
	return 1;
L70:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L73;
	p += 1;
	if (!p_globalNumber()) goto L73;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L73;
	p += 1;
	return 1;
L73:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L74;
	p += 1;
	if (!p_globalNumber()) goto L74;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L74;
	p += 1;
	return 1;
L74:	p = entry; actionLogLen = entryLog;
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
	if (!p_globalNumber()) goto L77;
	goto L76;
L77:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalNeg()) goto L78;
	goto L76;
L78:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalBool()) goto L79;
	goto L76;
L79:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L75;
L76:	sp--;
	return 1;
L75:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "true", 4) != 0) goto L82;
	if (idch((unsigned char)p[4])) goto L82;
	p += 4;
	goto L81;
L82:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L83;
	if (idch((unsigned char)p[5])) goto L83;
	p += 5;
	goto L81;
L83:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L80;
L81:	sp--;
	return 1;
L80:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L84;
	if (strncmp(p, "-=", 2) == 0) goto L84;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L84;	/* Longest-Match */
	p += 1;
	if (!p_globalNumber()) goto L84;
	return 1;
L84:	p = entry; actionLogLen = entryLog;
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
	if (!p_digit()) goto L85;
L86:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L87;
	sp--; goto L86;
L87:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L85:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcdef */
static int p_funcdef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcHead()) goto L88;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_funcBody()) goto L90;
	goto L89;
L90:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_protoEnd()) goto L91;
	goto L89;
L91:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L88;
L89:	sp--;
	actionLogPush(tc_funcend, entry, p);	/* ACTION AFTER funcdef */
	return 1;
L88:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcBody */
static int p_funcBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcBodyOpen()) goto L92;
L93:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L94;
	sp--; goto L93;
L94:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L92;
	p += 1;
	return 1;
L92:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "{", 1) != 0) goto L95;
	p += 1;
	actionLogPush(tc_funcbodybegin, entry, p);	/* ACTION AFTER funcBodyOpen */
	return 1;
L95:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L96;
	p += 1;
	actionLogPush(tc_funcdeclend, entry, p);	/* ACTION AFTER protoEnd */
	return 1;
L96:	p = entry; actionLogLen = entryLog;
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
	if (!p_staticKw()) goto L98;
	sp--; goto L99;
L98:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L99:	;
	if (!p_type()) goto L97;
	if (!p_pointerDecl()) goto L97;
	if (!p_defName()) goto L97;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L97;
	p += 1;
	if (!p_paramList()) goto L97;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L97;
	p += 1;
	actionLogPush(tc_funcbegin, entry, p);	/* ACTION AFTER funcHead */
	return 1;
L97:	p = entry; actionLogLen = entryLog;
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
	if (!p_param()) goto L101;
L103:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L104;
	p += 1;
	if (!p_param()) goto L104;
	sp--; goto L103;
L104:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L102;
L101:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L102:	;
	return 1;
L100:	p = entry; actionLogLen = entryLog;
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
	if (!p_constKw()) goto L106;
	sp--; goto L107;
L106:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L107:	;
	if (!p_type()) goto L105;
	if (!p_pointerDecl()) goto L105;
	if (!p_paramDecl()) goto L105;
	return 1;
L105:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* paramDecl */
static int p_paramDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_paramName()) goto L108;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_paramArray()) goto L109;
	sp--; goto L110;
L109:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L110:	;
	actionLogPush(tc_param, entry, p);	/* ACTION AFTER paramDecl */
	return 1;
L108:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L111;
	p += 1;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L111;
	p += 1;
	return 1;
L111:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "{", 1) != 0) goto L112;
	p += 1;
L113:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L114;
	sp--; goto L113;
L114:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L112;
	p += 1;
	return 1;
L112:	p = entry; actionLogLen = entryLog;
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
	if (!p_ifStmt()) goto L117;
	goto L116;
L117:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_whileStmt()) goto L118;
	goto L116;
L118:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_forStmt()) goto L119;
	goto L116;
L119:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_doStmt()) goto L120;
	goto L116;
L120:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_switchStmt()) goto L121;
	goto L116;
L121:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_breakStmt()) goto L122;
	goto L116;
L122:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_continueStmt()) goto L123;
	goto L116;
L123:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_returnStmt()) goto L124;
	goto L116;
L124:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_block()) goto L125;
	goto L116;
L125:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_callStmt()) goto L126;
	goto L116;
L126:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incDecStmt()) goto L127;
	goto L116;
L127:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_assignStmt()) goto L128;
	goto L116;
L128:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticVarDecl()) goto L129;
	goto L116;
L129:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varDecl()) goto L130;
	goto L116;
L130:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L115;
L116:	sp--;
	return 1;
L115:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticVarDecl */
static int p_staticVarDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_staticKw()) goto L131;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L132;
	sp--; goto L133;
L132:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L133:	;
	if (!p_type()) goto L131;
	if (!p_pointerDecl()) goto L131;
	if (!p_staticLocalName()) goto L131;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L134;
	if (strncmp(p, "==", 2) == 0) goto L134;	/* Longest-Match */
	p += 1;
	if (!p_staticInit()) goto L134;
	sp--; goto L135;
L134:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L135:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L131;
	p += 1;
	actionLogPush(tc_staticlocal, entry, p);	/* ACTION AFTER staticVarDecl */
	return 1;
L131:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L136;
	actionLogPush(tc_staticlocalname, entry, p);	/* ACTION AFTER staticLocalName */
	return 1;
L136:	p = entry; actionLogLen = entryLog;
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
	if (!p_globalValue()) goto L139;
	goto L138;
L139:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticRuntimeInit()) goto L140;
	goto L138;
L140:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L137;
L138:	sp--;
	return 1;
L137:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticRuntimeInit */
static int p_staticRuntimeInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L141;
	actionLogPush(tc_staticruntimeinit, entry, p);	/* ACTION AFTER staticRuntimeInit */
	return 1;
L141:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchStmt */
static int p_switchStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_switchKw()) goto L142;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L142;
	p += 1;
	if (!p_switchCond()) goto L142;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L142;
	p += 1;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L142;
	p += 1;
L143:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseGroup()) goto L144;
	sp--; goto L143;
L144:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_defaultGroup()) goto L145;
	sp--; goto L146;
L145:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L146:	;
	if (!p_switchClose()) goto L142;
	actionLogPush(tc_switchend, entry, p);	/* ACTION AFTER switchStmt */
	return 1;
L142:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "switch", 6) != 0) goto L147;
	if (idch((unsigned char)p[6])) goto L147;
	p += 6;
	actionLogPush(tc_switchbegin, entry, p);	/* ACTION AFTER switchKw */
	return 1;
L147:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchCond */
static int p_switchCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L148;
	actionLogPush(tc_switchcond, entry, p);	/* ACTION AFTER switchCond */
	return 1;
L148:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "}", 1) != 0) goto L149;
	p += 1;
	return 1;
L149:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseGroup */
static int p_caseGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabelRun()) goto L150;
	if (!p_caseBody()) goto L150;
	actionLogPush(tc_casegroup_end, entry, p);	/* ACTION AFTER caseGroup */
	return 1;
L150:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseLabelRun */
static int p_caseLabelRun(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabel()) goto L151;
L152:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseLabel()) goto L153;
	sp--; goto L152;
L153:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(tc_caselabelrun_end, entry, p);	/* ACTION AFTER caseLabelRun */
	return 1;
L151:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "case", 4) != 0) goto L154;
	if (idch((unsigned char)p[4])) goto L154;
	p += 4;
	if (!p_caseValue()) goto L154;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L154;
	p += 1;
	actionLogPush(tc_caselabel, entry, p);	/* ACTION AFTER caseLabel */
	return 1;
L154:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L157;
	goto L156;
L157:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNeg()) goto L158;
	goto L156;
L158:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNumber()) goto L159;
	goto L156;
L159:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L155;
L156:	sp--;
	return 1;
L155:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L160;
	if (strncmp(p, "-=", 2) == 0) goto L160;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L160;	/* Longest-Match */
	p += 1;
	if (!p_caseNumber()) goto L160;
	return 1;
L160:	p = entry; actionLogLen = entryLog;
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
	if (!p_digit()) goto L161;
L162:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L163;
	sp--; goto L162;
L163:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L161:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseBody */
static int p_caseBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L165:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L166;
	sp--; goto L165;
L166:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L164:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* defaultGroup */
static int p_defaultGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_defaultLabel()) goto L167;
	if (!p_caseBody()) goto L167;
	return 1;
L167:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "default", 7) != 0) goto L168;
	if (idch((unsigned char)p[7])) goto L168;
	p += 7;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L168;
	p += 1;
	actionLogPush(tc_defaultlabel, entry, p);	/* ACTION AFTER defaultLabel */
	return 1;
L168:	p = entry; actionLogLen = entryLog;
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
	if (!p_constKw()) goto L170;
	sp--; goto L171;
L170:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L171:	;
	if (!p_type()) goto L169;
	if (!p_pointerDecl()) goto L169;
	if (!p_localDecl()) goto L169;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L172;
	if (strncmp(p, "==", 2) == 0) goto L172;	/* Longest-Match */
	p += 1;
	if (!p_varInit()) goto L172;
	sp--; goto L173;
L172:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L173:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L169;
	p += 1;
	return 1;
L169:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* localDecl */
static int p_localDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_localName()) goto L174;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L175;
L177:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L178;
	sp--; goto L177;
L178:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L176;
L175:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L176:	;
	actionLogPush(tc_localdecl, entry, p);	/* ACTION AFTER localDecl */
	return 1;
L174:	p = entry; actionLogLen = entryLog;
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
	if (!p_arrayStringInit()) goto L181;
	goto L180;
L181:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_expr()) goto L182;
	goto L180;
L182:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L183;
	goto L180;
L183:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L179;
L180:	sp--;
	actionLogPush(tc_varinit, entry, p);	/* ACTION AFTER varInit */
	return 1;
L179:	p = entry; actionLogLen = entryLog;
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
	if (!p_stringLit()) goto L184;
	actionLogPush(tc_arrayinitstring, entry, p);	/* ACTION AFTER arrayStringInit */
	return 1;
L184:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "{", 1) != 0) goto L185;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_initValue()) goto L186;
L188:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L189;
	p += 1;
	if (!p_initValue()) goto L189;
	sp--; goto L188;
L189:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L187;
L186:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L187:	;
	ws();
	if (strncmp(p, "}", 1) != 0) goto L185;
	p += 1;
	return 1;
L185:	p = entry; actionLogLen = entryLog;
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
	if (!p_initNumber()) goto L192;
	goto L191;
L192:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initNeg()) goto L193;
	goto L191;
L193:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initBool()) goto L194;
	goto L191;
L194:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L190;
L191:	sp--;
	return 1;
L190:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "true", 4) != 0) goto L197;
	if (idch((unsigned char)p[4])) goto L197;
	p += 4;
	goto L196;
L197:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L198;
	if (idch((unsigned char)p[5])) goto L198;
	p += 5;
	goto L196;
L198:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L195;
L196:	sp--;
	return 1;
L195:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L199;
	if (strncmp(p, "-=", 2) == 0) goto L199;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L199;	/* Longest-Match */
	p += 1;
	if (!p_initNumber()) goto L199;
	return 1;
L199:	p = entry; actionLogLen = entryLog;
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
	if (!p_digit()) goto L200;
L201:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L202;
	sp--; goto L201;
L202:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L200:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* assignStmt */
static int p_assignStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L203;
	if (!p_assignop()) goto L203;
	if (!p_expr()) goto L203;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L203;
	p += 1;
	actionLogPush(tc_assign, entry, p);	/* ACTION AFTER assignStmt */
	return 1;
L203:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "=", 1) != 0) goto L206;
	if (strncmp(p, "==", 2) == 0) goto L206;	/* Longest-Match */
	p += 1;
	goto L205;
L206:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "+=", 2) != 0) goto L207;
	p += 2;
	goto L205;
L207:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-=", 2) != 0) goto L208;
	p += 2;
	goto L205;
L208:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "*=", 2) != 0) goto L209;
	p += 2;
	goto L205;
L209:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/=", 2) != 0) goto L210;
	p += 2;
	goto L205;
L210:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%=", 2) != 0) goto L211;
	p += 2;
	goto L205;
L211:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<<=", 3) != 0) goto L212;
	p += 3;
	goto L205;
L212:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>=", 3) != 0) goto L213;
	p += 3;
	goto L205;
L213:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "&=", 2) != 0) goto L214;
	p += 2;
	goto L205;
L214:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "^=", 2) != 0) goto L215;
	p += 2;
	goto L205;
L215:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "|=", 2) != 0) goto L216;
	p += 2;
	goto L205;
L216:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L204;
L205:	sp--;
	actionLogPush(tc_assignop, entry, p);	/* ACTION AFTER assignop */
	return 1;
L204:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* callStmt */
static int p_callStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_call()) goto L217;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L217;
	p += 1;
	actionLogPush(tc_callstmt, entry, p);	/* ACTION AFTER callStmt */
	return 1;
L217:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "return", 6) != 0) goto L218;
	if (idch((unsigned char)p[6])) goto L218;
	p += 6;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_retVal()) goto L219;
	sp--; goto L220;
L219:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L220:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L218;
	p += 1;
	actionLogPush(tc_return, entry, p);	/* ACTION AFTER returnStmt */
	return 1;
L218:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* retVal */
static int p_retVal(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L221;
	actionLogPush(tc_retval, entry, p);	/* ACTION AFTER retVal */
	return 1;
L221:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ifStmt */
static int p_ifStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_ifKw()) goto L222;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L222;
	p += 1;
	if (!p_ifCond()) goto L222;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L222;
	p += 1;
	if (!p_thenPart()) goto L222;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_elseKw()) goto L223;
	if (!p_elsePart()) goto L223;
	sp--; goto L224;
L223:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L224:	;
	actionLogPush(tc_ifend, entry, p);	/* ACTION AFTER ifStmt */
	return 1;
L222:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "if", 2) != 0) goto L225;
	if (idch((unsigned char)p[2])) goto L225;
	p += 2;
	actionLogPush(tc_ifbegin, entry, p);	/* ACTION AFTER ifKw */
	return 1;
L225:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "else", 4) != 0) goto L226;
	if (idch((unsigned char)p[4])) goto L226;
	p += 4;
	return 1;
L226:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ifCond */
static int p_ifCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L227;
	actionLogPush(tc_ifcond, entry, p);	/* ACTION AFTER ifCond */
	return 1;
L227:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* thenPart */
static int p_thenPart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L228;
	actionLogPush(tc_thenend, entry, p);	/* ACTION AFTER thenPart */
	return 1;
L228:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* elsePart */
static int p_elsePart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L229;
	return 1;
L229:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileStmt */
static int p_whileStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_whileKw()) goto L230;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L230;
	p += 1;
	if (!p_whileCond()) goto L230;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L230;
	p += 1;
	if (!p_whileBody()) goto L230;
	actionLogPush(tc_whileend, entry, p);	/* ACTION AFTER whileStmt */
	return 1;
L230:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "while", 5) != 0) goto L231;
	if (idch((unsigned char)p[5])) goto L231;
	p += 5;
	actionLogPush(tc_whilebegin, entry, p);	/* ACTION AFTER whileKw */
	return 1;
L231:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileCond */
static int p_whileCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L232;
	actionLogPush(tc_whilecond, entry, p);	/* ACTION AFTER whileCond */
	return 1;
L232:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileBody */
static int p_whileBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L233;
	return 1;
L233:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forStmt */
static int p_forStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_forKw()) goto L234;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L234;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forInit()) goto L235;
	sp--; goto L236;
L235:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L236:	;
	if (!p_forSep1()) goto L234;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forCond()) goto L237;
	sp--; goto L238;
L237:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L238:	;
	if (!p_forSep2()) goto L234;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forStep()) goto L239;
	sp--; goto L240;
L239:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L240:	;
	if (!p_forClose()) goto L234;
	if (!p_forBody()) goto L234;
	actionLogPush(tc_forend, entry, p);	/* ACTION AFTER forStmt */
	return 1;
L234:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "for", 3) != 0) goto L241;
	if (idch((unsigned char)p[3])) goto L241;
	p += 3;
	actionLogPush(tc_forbegin, entry, p);	/* ACTION AFTER forKw */
	return 1;
L241:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L242;
	p += 1;
	actionLogPush(tc_forsep1, entry, p);	/* ACTION AFTER forSep1 */
	return 1;
L242:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L243;
	p += 1;
	actionLogPush(tc_forsep2, entry, p);	/* ACTION AFTER forSep2 */
	return 1;
L243:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ")", 1) != 0) goto L244;
	p += 1;
	actionLogPush(tc_forclose, entry, p);	/* ACTION AFTER forClose */
	return 1;
L244:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forInit */
static int p_forInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L245;
	if (!p_assignop()) goto L245;
	if (!p_expr()) goto L245;
	actionLogPush(tc_assign, entry, p);	/* ACTION AFTER forInit */
	return 1;
L245:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forCond */
static int p_forCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L246;
	actionLogPush(tc_forcond, entry, p);	/* ACTION AFTER forCond */
	return 1;
L246:	p = entry; actionLogLen = entryLog;
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
	if (!p_target()) goto L249;
	if (!p_assignop()) goto L249;
	if (!p_expr()) goto L249;
	goto L248;
L249:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L250;
	goto L248;
L250:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L251;
	goto L248;
L251:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L247;
L248:	sp--;
	actionLogPush(tc_forstep, entry, p);	/* ACTION AFTER forStep */
	return 1;
L247:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forBody */
static int p_forBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L252;
	return 1;
L252:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doStmt */
static int p_doStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_doKw()) goto L253;
	if (!p_doBody()) goto L253;
	if (!p_doWhileTok()) goto L253;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L253;
	p += 1;
	if (!p_doCond()) goto L253;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L253;
	p += 1;
	if (!p_doClose()) goto L253;
	actionLogPush(tc_doend, entry, p);	/* ACTION AFTER doStmt */
	return 1;
L253:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "do", 2) != 0) goto L254;
	if (idch((unsigned char)p[2])) goto L254;
	p += 2;
	actionLogPush(tc_dobegin, entry, p);	/* ACTION AFTER doKw */
	return 1;
L254:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "while", 5) != 0) goto L255;
	if (idch((unsigned char)p[5])) goto L255;
	p += 5;
	actionLogPush(tc_dowhiletok, entry, p);	/* ACTION AFTER doWhileTok */
	return 1;
L255:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doCond */
static int p_doCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L256;
	actionLogPush(tc_docond, entry, p);	/* ACTION AFTER doCond */
	return 1;
L256:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L257;
	p += 1;
	return 1;
L257:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doBody */
static int p_doBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L258;
	return 1;
L258:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "break", 5) != 0) goto L259;
	if (idch((unsigned char)p[5])) goto L259;
	p += 5;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L259;
	p += 1;
	actionLogPush(tc_break, entry, p);	/* ACTION AFTER breakStmt */
	return 1;
L259:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "continue", 8) != 0) goto L260;
	if (idch((unsigned char)p[8])) goto L260;
	p += 8;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L260;
	p += 1;
	actionLogPush(tc_continue, entry, p);	/* ACTION AFTER continueStmt */
	return 1;
L260:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* expr */
static int p_expr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L261;
	return 1;
L261:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalExpr */
static int p_conditionalExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_orExpr()) goto L262;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_qmark()) goto L263;
	if (!p_conditionalTrue()) goto L263;
	if (!p_colon()) goto L263;
	if (!p_conditionalFalse()) goto L263;
	sp--; goto L264;
L263:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L264:	;
	actionLogPush(tc_ternaryend, entry, p);	/* ACTION AFTER conditionalExpr */
	return 1;
L262:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "?", 1) != 0) goto L265;
	p += 1;
	actionLogPush(tc_ternarybegin, entry, p);	/* ACTION AFTER qmark */
	return 1;
L265:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalTrue */
static int p_conditionalTrue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L266;
	return 1;
L266:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ":", 1) != 0) goto L267;
	p += 1;
	actionLogPush(tc_ternarymiddle, entry, p);	/* ACTION AFTER colon */
	return 1;
L267:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalFalse */
static int p_conditionalFalse(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L268;
	return 1;
L268:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* orExpr */
static int p_orExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_andExpr()) goto L269;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_orop()) goto L270;
	if (!p_orExpr()) goto L270;
	sp--; goto L271;
L270:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L271:	;
	actionLogPush(tc_logicorend, entry, p);	/* ACTION AFTER orExpr */
	return 1;
L269:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "||", 2) != 0) goto L272;
	p += 2;
	actionLogPush(tc_logicorop, entry, p);	/* ACTION AFTER orop */
	return 1;
L272:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* andExpr */
static int p_andExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitOrExpr()) goto L273;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_andop()) goto L274;
	if (!p_andExpr()) goto L274;
	sp--; goto L275;
L274:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L275:	;
	actionLogPush(tc_logicandend, entry, p);	/* ACTION AFTER andExpr */
	return 1;
L273:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "&&", 2) != 0) goto L276;
	p += 2;
	actionLogPush(tc_logicandop, entry, p);	/* ACTION AFTER andop */
	return 1;
L276:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitOrExpr */
static int p_bitOrExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitXorExpr()) goto L277;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitorop()) goto L278;
	if (!p_bitOrExpr()) goto L278;
	sp--; goto L279;
L278:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L279:	;
	actionLogPush(tc_bitorend, entry, p);	/* ACTION AFTER bitOrExpr */
	return 1;
L277:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "|", 1) != 0) goto L280;
	if (strncmp(p, "|=", 2) == 0) goto L280;	/* Longest-Match */
	if (strncmp(p, "||", 2) == 0) goto L280;	/* Longest-Match */
	p += 1;
	actionLogPush(tc_bitorop, entry, p);	/* ACTION AFTER bitorop */
	return 1;
L280:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitXorExpr */
static int p_bitXorExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitAndExpr()) goto L281;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitxorop()) goto L282;
	if (!p_bitXorExpr()) goto L282;
	sp--; goto L283;
L282:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L283:	;
	actionLogPush(tc_bitxorend, entry, p);	/* ACTION AFTER bitXorExpr */
	return 1;
L281:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "^", 1) != 0) goto L284;
	if (strncmp(p, "^=", 2) == 0) goto L284;	/* Longest-Match */
	p += 1;
	actionLogPush(tc_bitxorop, entry, p);	/* ACTION AFTER bitxorop */
	return 1;
L284:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitAndExpr */
static int p_bitAndExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_comparison()) goto L285;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitandop()) goto L286;
	if (!p_bitAndExpr()) goto L286;
	sp--; goto L287;
L286:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L287:	;
	actionLogPush(tc_bitandend, entry, p);	/* ACTION AFTER bitAndExpr */
	return 1;
L285:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "&", 1) != 0) goto L288;
	if (strncmp(p, "&=", 2) == 0) goto L288;	/* Longest-Match */
	if (strncmp(p, "&&", 2) == 0) goto L288;	/* Longest-Match */
	p += 1;
	actionLogPush(tc_bitandop, entry, p);	/* ACTION AFTER bitandop */
	return 1;
L288:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* comparison */
static int p_comparison(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_shiftExpr()) goto L289;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_relop()) goto L290;
	if (!p_shiftExpr()) goto L290;
	sp--; goto L291;
L290:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L291:	;
	actionLogPush(tc_expr, entry, p);	/* ACTION AFTER comparison */
	return 1;
L289:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* shiftExpr */
static int p_shiftExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L292;
L293:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_shiftop()) goto L294;
	if (!p_shiftRhs()) goto L294;
	sp--; goto L293;
L294:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L292:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* shiftRhs */
static int p_shiftRhs(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L295;
	actionLogPush(tc_shiftrhs, entry, p);	/* ACTION AFTER shiftRhs */
	return 1;
L295:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "<<", 2) != 0) goto L298;
	if (strncmp(p, "<<=", 3) == 0) goto L298;	/* Longest-Match */
	p += 2;
	goto L297;
L298:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>", 2) != 0) goto L299;
	if (strncmp(p, ">>=", 3) == 0) goto L299;	/* Longest-Match */
	p += 2;
	goto L297;
L299:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L296;
L297:	sp--;
	actionLogPush(tc_shiftop, entry, p);	/* ACTION AFTER shiftop */
	return 1;
L296:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "<=", 2) != 0) goto L302;
	p += 2;
	goto L301;
L302:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">=", 2) != 0) goto L303;
	p += 2;
	goto L301;
L303:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "==", 2) != 0) goto L304;
	p += 2;
	goto L301;
L304:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!=", 2) != 0) goto L305;
	p += 2;
	goto L301;
L305:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<", 1) != 0) goto L306;
	if (strncmp(p, "<<=", 3) == 0) goto L306;	/* Longest-Match */
	if (strncmp(p, "<<", 2) == 0) goto L306;	/* Longest-Match */
	if (strncmp(p, "<=", 2) == 0) goto L306;	/* Longest-Match */
	p += 1;
	goto L301;
L306:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">", 1) != 0) goto L307;
	if (strncmp(p, ">>=", 3) == 0) goto L307;	/* Longest-Match */
	if (strncmp(p, ">>", 2) == 0) goto L307;	/* Longest-Match */
	if (strncmp(p, ">=", 2) == 0) goto L307;	/* Longest-Match */
	p += 1;
	goto L301;
L307:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L300;
L301:	sp--;
	actionLogPush(tc_relop, entry, p);	/* ACTION AFTER relop */
	return 1;
L300:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* addExpr */
static int p_addExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_term()) goto L308;
L309:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_addop()) goto L310;
	if (!p_term()) goto L310;
	sp--; goto L309;
L310:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L308:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "+", 1) != 0) goto L313;
	if (strncmp(p, "+=", 2) == 0) goto L313;	/* Longest-Match */
	if (strncmp(p, "++", 2) == 0) goto L313;	/* Longest-Match */
	p += 1;
	goto L312;
L313:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-", 1) != 0) goto L314;
	if (strncmp(p, "-=", 2) == 0) goto L314;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L314;	/* Longest-Match */
	p += 1;
	goto L312;
L314:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L311;
L312:	sp--;
	actionLogPush(tc_addop, entry, p);	/* ACTION AFTER addop */
	return 1;
L311:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* term */
static int p_term(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L315;
L316:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_mulop()) goto L317;
	if (!p_factor()) goto L317;
	sp--; goto L316;
L317:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(tc_term, entry, p);	/* ACTION AFTER term */
	return 1;
L315:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L320;
	if (strncmp(p, "*=", 2) == 0) goto L320;	/* Longest-Match */
	p += 1;
	goto L319;
L320:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/", 1) != 0) goto L321;
	if (strncmp(p, "/=", 2) == 0) goto L321;	/* Longest-Match */
	p += 1;
	goto L319;
L321:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%", 1) != 0) goto L322;
	if (strncmp(p, "%=", 2) == 0) goto L322;	/* Longest-Match */
	p += 1;
	goto L319;
L322:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L318;
L319:	sp--;
	actionLogPush(tc_mulop, entry, p);	/* ACTION AFTER mulop */
	return 1;
L318:	p = entry; actionLogLen = entryLog;
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
	if (!p_sizeofExpr()) goto L325;
	goto L324;
L325:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_castExpr()) goto L326;
	goto L324;
L326:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L327;
	goto L324;
L327:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L328;
	goto L324;
L328:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "(", 1) != 0) goto L329;
	p += 1;
	if (!p_expr()) goto L329;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L329;
	p += 1;
	goto L324;
L329:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_call()) goto L330;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L331;
	sp--; goto L332;
L331:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L332:	;
	goto L324;
L330:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_boolLit()) goto L333;
	goto L324;
L333:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_addressRef()) goto L334;
	goto L324;
L334:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_derefRef()) goto L335;
	goto L324;
L335:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varRef()) goto L336;
	goto L324;
L336:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_number()) goto L337;
	goto L324;
L337:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_stringLit()) goto L338;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L339;
	sp--; goto L340;
L339:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L340:	;
	goto L324;
L338:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_negFactor()) goto L341;
	goto L324;
L341:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L323;
L324:	sp--;
	actionLogPush(tc_factor, entry, p);	/* ACTION AFTER factor */
	return 1;
L323:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* postfixIndex */
static int p_postfixIndex(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_index()) goto L342;
	actionLogPush(tc_postfixindex, entry, p);	/* ACTION AFTER postfixIndex */
	return 1;
L342:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* stringLit (lexikalisch) */
static int p_stringLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\"", 1) != 0) goto L343;
	p += 1;
L344:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_character()) goto L345;
	sp--; goto L344;
L345:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	if (strncmp(p, "\"", 1) != 0) goto L343;
	p += 1;
	actionLogPush(tc_string, entry, p);	/* ACTION AFTER stringLit */
	return 1;
L343:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* character (lexikalisch) */
static int p_character(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x21) goto L348;
	p++;
	goto L347;
L348:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x23 || (unsigned char)*p > 0x7E) goto L349;
	p++;
	goto L347;
L349:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L346;
L347:	sp--;
	return 1;
L346:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L352;
	if (strncmp(p, "-=", 2) == 0) goto L352;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L352;	/* Longest-Match */
	p += 1;
	goto L351;
L352:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!", 1) != 0) goto L353;
	if (strncmp(p, "!=", 2) == 0) goto L353;	/* Longest-Match */
	p += 1;
	goto L351;
L353:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "~", 1) != 0) goto L354;
	p += 1;
	goto L351;
L354:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L350;
L351:	sp--;
	if (!p_factor()) goto L350;
	actionLogPush(tc_neg, entry, p);	/* ACTION AFTER negFactor */
	return 1;
L350:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "&", 1) != 0) goto L355;
	if (strncmp(p, "&=", 2) == 0) goto L355;	/* Longest-Match */
	if (strncmp(p, "&&", 2) == 0) goto L355;	/* Longest-Match */
	p += 1;
	ws();
	if (!p_ident()) goto L355;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L356;
	sp--; goto L357;
L356:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L357:	;
	actionLogPush(tc_addressref, entry, p);	/* ACTION AFTER addressRef */
	return 1;
L355:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "sizeof", 6) != 0) goto L358;
	if (idch((unsigned char)p[6])) goto L358;
	p += 6;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L358;
	p += 1;
	if (!p_sizeofArg()) goto L358;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L358;
	p += 1;
	return 1;
L358:	p = entry; actionLogLen = entryLog;
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
	if (!p_sizeofType()) goto L361;
	goto L360;
L361:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_sizeofVarName()) goto L362;
	goto L360;
L362:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L359;
L360:	sp--;
	return 1;
L359:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "unsigned", 8) != 0) goto L365;
	if (idch((unsigned char)p[8])) goto L365;
	p += 8;
	if (!p_unsignedInt()) goto L365;
	goto L364;
L365:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L366;
	if (idch((unsigned char)p[3])) goto L366;
	p += 3;
	goto L364;
L366:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L367;
	if (idch((unsigned char)p[4])) goto L367;
	p += 4;
	goto L364;
L367:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L368;
	if (idch((unsigned char)p[4])) goto L368;
	p += 4;
	goto L364;
L368:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L369;
	if (idch((unsigned char)p[6])) goto L369;
	p += 6;
	if (!p_structTypeRef()) goto L369;
	goto L364;
L369:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L363;
L364:	sp--;
	actionLogPush(tc_sizeof, entry, p);	/* ACTION AFTER sizeofType */
	return 1;
L363:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L370;
	actionLogPush(tc_sizeofvar, entry, p);	/* ACTION AFTER sizeofVarName */
	return 1;
L370:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "(", 1) != 0) goto L371;
	p += 1;
	if (!p_castType()) goto L371;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L371;
	p += 1;
	if (!p_castOperand()) goto L371;
	actionLogPush(tc_cast, entry, p);	/* ACTION AFTER castExpr */
	return 1;
L371:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "unsigned", 8) != 0) goto L374;
	if (idch((unsigned char)p[8])) goto L374;
	p += 8;
	if (!p_unsignedInt()) goto L374;
	goto L373;
L374:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L375;
	if (idch((unsigned char)p[3])) goto L375;
	p += 3;
	goto L373;
L375:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L376;
	if (idch((unsigned char)p[4])) goto L376;
	p += 4;
	goto L373;
L376:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L377;
	if (idch((unsigned char)p[4])) goto L377;
	p += 4;
	goto L373;
L377:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L372;
L373:	sp--;
	actionLogPush(tc_castcapture, entry, p);	/* ACTION AFTER castType */
	return 1;
L372:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* castOperand */
static int p_castOperand(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L378;
	return 1;
L378:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* preIncDec */
static int p_preIncDec(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_incdecOp()) goto L379;
	ws();
	if (!p_ident()) goto L379;
	actionLogPush(tc_preincdec, entry, p);	/* ACTION AFTER preIncDec */
	return 1;
L379:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L380;
	if (!p_incdecOp()) goto L380;
	actionLogPush(tc_postincdec, entry, p);	/* ACTION AFTER postIncDec */
	return 1;
L380:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "++", 2) != 0) goto L383;
	p += 2;
	goto L382;
L383:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "--", 2) != 0) goto L384;
	p += 2;
	goto L382;
L384:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L381;
L382:	sp--;
	return 1;
L381:	p = entry; actionLogLen = entryLog;
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
	if (!p_preIncDec()) goto L387;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L387;
	p += 1;
	goto L386;
L387:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L388;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L388;
	p += 1;
	goto L386;
L388:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L385;
L386:	sp--;
	actionLogPush(tc_incdecstmt, entry, p);	/* ACTION AFTER incDecStmt */
	return 1;
L385:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L389;
	if (strncmp(p, "*=", 2) == 0) goto L389;	/* Longest-Match */
	p += 1;
	if (!p_factor()) goto L389;
	actionLogPush(tc_derefref, entry, p);	/* ACTION AFTER derefRef */
	return 1;
L389:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* call */
static int p_call(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcName()) goto L390;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L390;
	p += 1;
	if (!p_argList()) goto L390;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L390;
	p += 1;
	actionLogPush(tc_call, entry, p);	/* ACTION AFTER call */
	return 1;
L390:	p = entry; actionLogLen = entryLog;
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
	if (!p_arg()) goto L392;
L394:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L395;
	p += 1;
	if (!p_arg()) goto L395;
	sp--; goto L394;
L395:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L393;
L392:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L393:	;
	return 1;
L391:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* arg */
static int p_arg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L396;
	actionLogPush(tc_arg, entry, p);	/* ACTION AFTER arg */
	return 1;
L396:	p = entry; actionLogLen = entryLog;
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
	if (!p_directTarget()) goto L399;
	goto L398;
L399:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectTarget()) goto L400;
	goto L398;
L400:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L397;
L398:	sp--;
	return 1;
L397:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L401;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L402;
L404:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L405;
	sp--; goto L404;
L405:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L403;
L402:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L403:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L406;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L408;
	sp--; goto L409;
L408:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L409:	;
	sp--; goto L407;
L406:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L407:	;
	actionLogPush(tc_target, entry, p);	/* ACTION AFTER directTarget */
	return 1;
L401:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L410;
	if (strncmp(p, "*=", 2) == 0) goto L410;	/* Longest-Match */
	p += 1;
	if (!p_factor()) goto L410;
	actionLogPush(tc_indirecttarget, entry, p);	/* ACTION AFTER indirectTarget */
	return 1;
L410:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L411;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L412;
L414:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L415;
	sp--; goto L414;
L415:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L413;
L412:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L413:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L416;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L418;
	sp--; goto L419;
L418:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L419:	;
	sp--; goto L417;
L416:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L417:	;
	actionLogPush(tc_varref, entry, p);	/* ACTION AFTER varRef */
	return 1;
L411:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* index */
static int p_index(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_indexOpen()) goto L420;
	if (!p_expr()) goto L420;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L420;
	p += 1;
	actionLogPush(tc_arg, entry, p);	/* ACTION AFTER index */
	return 1;
L420:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L421;
	p += 1;
	actionLogPush(tc_callname, entry, p);	/* ACTION AFTER indexOpen */
	return 1;
L421:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* member */
static int p_member(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ".", 1) != 0) goto L422;
	if (strncmp(p, "...", 3) == 0) goto L422;	/* Longest-Match */
	p += 1;
	if (!p_fieldName()) goto L422;
	return 1;
L422:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L423;
	actionLogPush(tc_defname, entry, p);	/* ACTION AFTER defName */
	return 1;
L423:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L424;
	return 1;
L424:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L425;
	actionLogPush(tc_local, entry, p);	/* ACTION AFTER localName */
	return 1;
L425:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L426;
	return 1;
L426:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L427;
	actionLogPush(tc_callname, entry, p);	/* ACTION AFTER funcName */
	return 1;
L427:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "unsigned", 8) != 0) goto L430;
	if (idch((unsigned char)p[8])) goto L430;
	p += 8;
	if (!p_unsignedInt()) goto L430;
	goto L429;
L430:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L431;
	if (idch((unsigned char)p[3])) goto L431;
	p += 3;
	goto L429;
L431:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L432;
	if (idch((unsigned char)p[4])) goto L432;
	p += 4;
	goto L429;
L432:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L433;
	if (idch((unsigned char)p[4])) goto L433;
	p += 4;
	goto L429;
L433:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L434;
	if (idch((unsigned char)p[6])) goto L434;
	p += 6;
	if (!p_structTypeRef()) goto L434;
	goto L429;
L434:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L435;
	if (idch((unsigned char)p[4])) goto L435;
	p += 4;
	if (!p_enumTypeRef()) goto L435;
	goto L429;
L435:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "void", 4) != 0) goto L436;
	if (idch((unsigned char)p[4])) goto L436;
	p += 4;
	goto L429;
L436:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_typedefRef()) goto L437;
	goto L429;
L437:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L428;
L429:	sp--;
	actionLogPush(tc_type, entry, p);	/* ACTION AFTER type */
	return 1;
L428:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L438;
	return 1;
L438:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L439;
	return 1;
L439:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L440;
	return 1;
L440:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* pointerDecl */
static int p_pointerDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L442:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_pointerStar()) goto L443;
	sp--; goto L442;
L443:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(tc_pointerdecl, entry, p);	/* ACTION AFTER pointerDecl */
	return 1;
L441:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L444;
	if (strncmp(p, "*=", 2) == 0) goto L444;	/* Longest-Match */
	p += 1;
	return 1;
L444:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* unsignedInt */
static int p_unsignedInt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "int", 3) != 0) goto L445;
	if (idch((unsigned char)p[3])) goto L445;
	p += 3;
	return 1;
L445:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "true", 4) != 0) goto L448;
	if (idch((unsigned char)p[4])) goto L448;
	p += 4;
	goto L447;
L448:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L449;
	if (idch((unsigned char)p[5])) goto L449;
	p += 5;
	goto L447;
L449:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L446;
L447:	sp--;
	actionLogPush(tc_number, entry, p);	/* ACTION AFTER boolLit */
	return 1;
L446:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ident (lexikalisch) */
static int p_ident(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_letter()) goto L450;
L451:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_letter()) goto L454;
	goto L453;
L454:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_digit()) goto L455;
	goto L453;
L455:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L452;
L453:	sp--;
	sp--; goto L451;
L452:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L450:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* number (lexikalisch) */
static int p_number(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_digit()) goto L456;
L457:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L458;
	sp--; goto L457;
L458:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(tc_number, entry, p);	/* ACTION AFTER number */
	return 1;
L456:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* letter (lexikalisch) */
static int p_letter(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x61 || (unsigned char)*p > 0x7A) goto L461;
	p++;
	goto L460;
L461:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x41 || (unsigned char)*p > 0x5A) goto L462;
	p++;
	goto L460;
L462:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "_", 1) != 0) goto L463;
	p += 1;
	goto L460;
L463:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L459;
L460:	sp--;
	return 1;
L459:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* digit (lexikalisch) */
static int p_digit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if ((unsigned char)*p < 0x30 || (unsigned char)*p > 0x39) goto L464;
	p++;
	return 1;
L464:	p = entry; actionLogLen = entryLog;
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
