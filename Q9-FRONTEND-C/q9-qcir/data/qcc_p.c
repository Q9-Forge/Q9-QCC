/* Automatisch erzeugt von parsec -- NICHT von Hand aendern.
 * Backtracking-Parser (rekursiver Abstieg, geordnete Auswahl).
 * Aufruf: parser "<eingabe>"  -> druckt OK/SEMERR/FAIL, exit 0/1/1.
 * LEXER aktiv: Whitespace/Kommentare werden zwischen Symbolen ueberlesen,
 * lexikalische Regeln (TOKEN-Abschluss) matchen adjazente Zeichen.
 * Startregel: program
 */
#include <stdio.h>
#include <string.h>
extern char* realloc(char*, int);
#ifdef QCC_BUFFERED_OUTPUT
#include <stdarg.h>
static char qccOutputBuffer[8192]; static int qccOutputUsed = 0;
static void qccOutputFlush(void) { if (qccOutputUsed) { fwrite(qccOutputBuffer, 1, qccOutputUsed, stdout); qccOutputUsed = 0; } }
static void qccOutputChar(int c) { if (qccOutputUsed == 8192) qccOutputFlush(); qccOutputBuffer[qccOutputUsed++] = (char)c; }
static void qccOutputString(const char* s) { while (*s) qccOutputChar(*s++); }
static void qccOutputLong(long v) { unsigned long u; char digits[16]; int n = 0; if (v < 0) { qccOutputChar('-'); u = (unsigned long)(-(v + 1)); u++; } else u = (unsigned long)v; do { digits[n++] = (char)('0' + (u % 10)); u /= 10; } while (u); while (n) qccOutputChar(digits[--n]); }
static int qccPrintf(const char* fmt, ...) { va_list ap; int longArg; va_start(ap, fmt); while (*fmt) { if (*fmt != '%') { qccOutputChar(*fmt++); continue; } fmt++; longArg = 0; if (*fmt == 'l') { longArg = 1; fmt++; } if (*fmt == 's') qccOutputString(va_arg(ap, const char*)); else if (*fmt == 'c') qccOutputChar(va_arg(ap, int)); else if (*fmt == 'd') qccOutputLong(longArg ? va_arg(ap, long) : (long)va_arg(ap, int)); else if (*fmt == '%') qccOutputChar('%'); if (*fmt) fmt++; } va_end(ap); return 0; }
#define printf qccPrintf
#define QCC_OUTPUT_FLUSH() qccOutputFlush()
#else
#define QCC_OUTPUT_FLUSH() (void)0
#endif

static const char* p;
static const char* parserInputStart;
static const char* parserActionAt;
static int actionLogLen = 0;	/* siehe ACTION-Routinen weiter unten */
static int actionErrors = 0;	/* ACTION-Routinen zaehlen hoch; != 0 => Rueckgabewert 1 */

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

extern void exit(int);
typedef struct { int id; const char* start; const char* end; } ActionLogEntry;
static ActionLogEntry* actionLog;
static int actionLogCap;
static void actionLogDispatch(int id, const char* start, const char* end);
static void actionLogPush(int id, const char* start, const char* end) {
	if (actionLogLen == actionLogCap) { int n = actionLogCap ? actionLogCap * 2 : 1024; ActionLogEntry* q = (ActionLogEntry*)realloc((char*)actionLog, n * sizeof(ActionLogEntry)); if (!q) { fprintf(stderr, "qcc: kein Speicher fuer Aktions-Log\n"); exit(1); } actionLog = q; actionLogCap = n; }
	actionLog[actionLogLen].id = id;
	actionLog[actionLogLen].start = start;
	actionLog[actionLogLen].end = end;
	actionLogLen++;
}
static void actionLogReplay(void) {
	int i;
	for (i = 0; i < actionLogLen; i++) actionLogDispatch(actionLog[i].id, actionLog[i].start, actionLog[i].end);
}

/* ACTION routines from [USER-CODE] (copied verbatim) */
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
   ein struct-Wert ist ein Byte-Blob fester Groesse (tcStructByteSizeK[id]), jedes Feld hat
   einen eigenen Typ (tcStructFieldTypes) und einen eigenen Byte-Offset (tcStructFieldOffsetK)
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
static int  tcStructFieldOffsetK[MAX_STRUCTS][MAX_STRUCT_FIELDS];
/* Zeigeranteil DESSELBEN Offsets: der Wert ist tcStructFieldOffsetK + n*P,
   wobei P die Zeigergroesse des Ziels ist (qir68k 4, qirarm64 8). Siehe
   den Layout-Kommentar in tcRegisterStruct. */
static int  tcStructFieldOffsetN[MAX_STRUCTS][MAX_STRUCT_FIELDS];
/* BIS 2026-09-15 stand hier TC_PTR_SLOT 8: ein Zeiger belegte in einer Struct
   IMMER acht Byte, unabhaengig vom Ziel, damit ein einzelnes
   frontend-berechnetes Offset fuer 68k UND ARM64 gilt. Auf dem 68k war damit
   die Haelfte jedes Zeigerfelds verschenkt. Seither rechnet das Frontend jedes
   Offset fuer BEIDE Zeigergroessen und gibt es als k+n*P weiter; das Backend
   setzt sein P ein. Die Invariante wohnt jetzt in tcRegisterStruct, die
   Ausgabe in tcEmitNum/tcEmitStructSize/tcEmitFieldOffset. */
/* Die beiden Zeigergroessen, fuer die das Layout gerechnet wird. Aus den
   zwei Ergebnissen leitet tcRegisterStruct die Form k+n*P ab. */
#define TC_PTR_SMALL 4
#define TC_PTR_LARGE 8

static int  tcStructFieldArrayLen[MAX_STRUCTS][MAX_STRUCT_FIELDS]; /* 0=Skalar, sonst Elementzahl (bei 2D: GESAMT) */
/* Nur bei zweidimensionalen Feldern != 0: Laenge EINER Zeile. "x.feld[i]"
   liefert dann base + i*Zeilenlaenge als ZEIGER statt eines Elementwerts. */
static int  tcStructFieldRowLen[MAX_STRUCTS][MAX_STRUCT_FIELDS];

/* "const" AM FELDTYP -- bis 2026-09-07 geparst und VERWORFEN.
 *
 * Die Grammatik hatte dafuer `fieldConstKw` als AKTIONSLOSE Kopie von
 * `constKw`: ein Verweis auf constKw haette tc_const ausgeloest, und dessen
 * tcPendingConst wird erst beim NAECHSTEN Parameter oder Lokalen konsumiert
 * -- dort haette es faelschlich Konstantheit erzwungen. Der Ausweg war
 * richtig, die Folge war es nicht: "struct P { const char *cp; }" mit
 * "p.cp[0] = 'x'" lief STILL durch, waehrend dasselbe bei einer VARIABLEN
 * korrekt gemeldet wird. Genauso "const int n" mit "s.n = 1".
 *
 * Jetzt hat fieldConstKw eine EIGENE Flagge, die nur Felder betrifft und am
 * Ende jeder structField-Zeile geloescht wird. Unterschieden wird wie bei
 * Variablen (tc_local): bei einem ZEIGER ist der Pointee konstant
 * (pointeeConst im Feldtyp), bei allem anderen das FELD selbst. */
static char tcStructFieldConst[MAX_STRUCTS][MAX_STRUCT_FIELDS];
static int  tcFieldConst = 0;
static int  tcStructByteSizeK[MAX_STRUCTS];
static int  tcStructByteSizeN[MAX_STRUCTS];  /* Zeigeranteil der Structgroesse */
static int  tcStructCount = 0;
static char tcStructBuildName[32];
static int  tcStructBuildFieldCount = 0;
static int  tcBuildIsUnion = 0;   /* 1 = die gesammelten Felder gehoeren zu einer union */
/* Vorwaerts: tc_unionend steht im erzeugten Parser VOR der Definition. */
static int tcRegisterStruct(const char* nameStart, const char* nameEnd);
/* dito: beide enum-Formen teilen sich diesen Leser. */
static void tcParseEnumBody(const char* start, const char* end);
/* dito: tc_fnptrvar legt die Variable ueber die normale Lokalenanlage an. */
void tc_local(const char* start, const char* end);
void tc_param(const char* start, const char* end);
void tc_paramend(const char* start, const char* end);
/* Hat paramDecl (also der NAME) fuer den laufenden Parameter gefeuert?
   tc_paramend traegt sonst einen namenlosen nach -- "int f(int);". */
static int tcParamNamed = 0;
static char tcStructBuildFieldNames[MAX_STRUCT_FIELDS][32];
static TCType tcStructBuildFieldTypes[MAX_STRUCT_FIELDS];
static int  tcStructBuildFieldArrayLen[MAX_STRUCT_FIELDS];
static int  tcStructBuildFieldRowLen[MAX_STRUCT_FIELDS];
static char tcStructBuildFieldConst[MAX_STRUCT_FIELDS];
/* anonymes struct inline im typedef (2026-07-24, siehe tc_anonstructbegin/tc_typedefend):
   der Zielname ("Name" in typedef struct {...} Name;) kommt in der Grammatik erst NACH
   dem Feld-Body -- die Felder werden wie gewohnt gesammelt, die Registrierung (mit dem
   typedef-Namen selbst als internem struct-Tag) passiert verzoegert in tc_typedefend. */
static int  tcAnonStructPending = 0;
/* Index eines struct/union, dessen NAME schon vor dem Rumpf eingetragen
   wurde, oder -1. Ohne diesen Vorgriff kennt sich
   "struct N { struct N *next; };" selbst nicht: registriert wurde bisher
   erst in tcRegisterStruct, also NACH dem Rumpf, und das Feld meldete
   "unknown struct or union". Damit waren verkettete Listen, Baeume und
   Graphen ueberhaupt nicht baubar.
   Der Vorabeintrag ist UNVOLLSTAENDIG (null Felder). Das genuegt: ein
   ZEIGER auf ihn braucht nur die Zeigergroesse. Ein Feld vom Typ
   "struct N" selbst bleibt abgelehnt (struct-in-struct geht ohnehin
   nicht), es kann also kein unendliches Layout entstehen. */
static int  tcStructPending = -1;
/* Hat der laufende struct/union einen RUMPF geoeffnet? Ohne diese Marke
   liesse sich "struct N;" (Vorwaertsdeklaration, gueltig) nicht von
   "struct S { };" (leerer Rumpf, in C ungueltig) unterscheiden -- beide
   kaemen mit null Feldern bei tc_structend an. */
static int  tcStructHasBody = 0;
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
/* 768 statt 512 seit 2026-09-16: der Gleitkomma-Umrechner in
   tc_floatlit bringt vierzehn eigene Funktionen mit, und damit
   ueberschritt der erzeugte Parser beim SELBSTUEBERSETZEN die alte
   Grenze -- "too many functions". Der Preis steht im Datenbereich:
   tcFunctionParamTypes ist mit Abstand das groesste Feld daran
   (MAX_FUNCTIONS * 64 * sizeof(TCType)). */
#define MAX_FUNCTIONS 768
static char tcNames[MAX_LOCALS][32];        /* lokale Variablen der aktuellen Funktion */
static int  tcLocalCount = 0;
static TCType tcLocalTypes[MAX_LOCALS];
static int  tcLocalArrayLen[MAX_LOCALS];     /* 0 = Skalar, sonst GESAMTE (flache) Elementzahl */
#define TC_MAXDIMS 6                 /* grosszuegige Obergrenze fuer Array-Dimensionen, siehe tcEmitNDCombine */
static int  tcLocalArrayNDims[MAX_LOCALS];   /* 1 = kein Mehrdim-Array (Skalar/1D zaehlt auch als 1) */
static int  tcLocalArrayDims[MAX_LOCALS][TC_MAXDIMS - 1]; /* [0]=dim2, [1]=dim3, ... -- nur die ersten NDims-1 Eintraege gueltig */
static int  tcLocalConst[MAX_LOCALS];        /* 1 = mit "const" deklariert (auch Parameter) */
/* Blockgueltigkeitsbereiche (2026-08-20, echter Fund im Bootstrap-Compiler auf
   dem Q9): tcNames[]/tcLocalTypes[]/... sind FLACH pro Funktion. Ein Block
   ("{ const char* q = p; ... }") legt fuer jede Deklaration einen NEUEN Slot
   an -- tcLookupLocal fand aber die ERSTE Namensgleichheit, also die Variable
   eines FRUEHEREN, laengst geschlossenen Blocks. Der Initialisierer schrieb
   damit in den neuen Slot, jeder spaetere Zugriff las den alten, nie
   beschriebenen: in tcGlobalOne ("const char* q = p;" im String-Initialisierer-
   Zweig, danach "*q == ' '") ergab das einen Nullpointer und auf dem echten Q9
   einen PMMU-Abbruch bei Aktion 310 des Bootstrap-Replays.
   tcScopeMark haelt tcLocalCount beim BETRETEN jedes Blocks; beim Verlassen
   werden alle seither angelegten Slots als tcLocalDead markiert. Slots werden
   dabei bewusst NICHT wiederverwendet -- die Rahmengroesse berechnet das
   Backend aus dem hoechsten benutzten Slot, Wiederverwendung waere eine eigene
   Optimierung mit eigenem Risiko. tcLookupLocal sucht seitdem RUECKWAERTS,
   damit eine innere Deklaration eine gleichnamige aeussere korrekt verdeckt.
   BEWUSSTE, DOKUMENTIERTE GRENZE: die Klammern von switchStmt oeffnen noch
   KEINEN eigenen Bereich -- eine Deklaration direkt in einem case-Rumpf bleibt
   also bis zum Funktionsende sichtbar (in echtem C endet sie mit dem
   switch-Rumpf). */
#define MAX_BLOCK_DEPTH 64
static int  tcLocalDead[MAX_LOCALS];         /* 1 = Slot gehoert zu einem verlassenen Block */
static int  tcScopeMark[MAX_BLOCK_DEPTH];    /* tcLocalCount beim Betreten von Block N */
static int  tcScopeDepth = 0;
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
static int  tcPtrIdxScratchDeclared[TC_MAXDIMS + 1]; /* dito fuer verschachtelte Pointer-Indizes */
static int  tcStructCopyScratchDeclared; /* dito fuer die Quelladresse einer Struct-Kopie */
static int  tcStructRetDeclared[MAX_STRUCTS]; /* je Struct-Typ ein Rueckgabepuffer, s. tc_return */
static int  tcStructArgSeq; /* laufende Nummer: je AUFRUFSTELLE ein eigener Argumentpuffer, s. tc_arg */
/* 1 = im Slot steht die ADRESSE einer Struct (Parameter), 0 = die Daten
   liegen im Slot selbst (lokale Variable mit LARRAY). Bei beiden ist
   tcLocalArrayLen 0, deshalb diese eigene Markierung. */
static int  tcLocalStructByAddr[MAX_LOCALS];
/* DOUBLE ALS PARAMETER / RUECKGABE (2026-09-16). Ein Parameter liegt in einem
   SLOT, und der ist im Rahmen fest vier Byte breit (68k: 8+4*(nargs-1-slot)).
   Ein double braucht acht. Statt das Rahmenlayout in allen drei Backends
   aufzubrechen, geht der Wert denselben Weg wie ein struct-Argument: der
   Aufrufer legt ihn in einen globalen Puffer und uebergibt dessen ADRESSE
   (vier Byte, passt in den Slot), der Aufgerufene holt ihn heraus.
   EIGENER PUFFER JE AUFRUFSTELLE, nicht je Position -- sonst ueberschriebe
   ein verschachtelter Aufruf ("f(1.0, g(2.0))") das schon abgelegte Argument.
   Dieselbe Ueberlegung wie bei __structArg_*, siehe tc_arg. */
static int  tcLocalDoubleByAddr[MAX_LOCALS];
static int  tcDoubleArgSeq = 0;
static int  tcDoubleRetDeclared = 0;
/* Vorwaertsdeklaration: der Generator gibt tc_varinit vor tcAssignStore
   aus, wo die Kopierhilfe definiert ist. */
static void tcEmitStructCopy(int dstSlot, const char* dstGlobal, int size, int sizeN);
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
static int  tcBasePointers = 0; /* Zeigergrad AUS DEM TYP ALLEIN (0, ausser bei einem
                                    Zeiger-typedef) -- von tc_type gesetzt, von
                                    tc_pointerdecl als Basis genommen. Noetig, seit
                                    pointerDecl PRO DEKLARATOR feuert (2026-09-09,
                                    "char *a, *b;"-Fix): ohne eigene Basis wuerde der
                                    zweite Deklarator entweder den Zeigergrad des
                                    ersten erben (Akkumulation) oder einen
                                    Zeiger-typedef verlieren (blosses Nullen). */
static TCType tcFuncType;
static char tcFuncName[32];
static char tcFunctionNames[MAX_FUNCTIONS][32];
/* QCC's bootstrap grammar intentionally accepts one declarator per
   declaration.  Keep these separate so the generated parser is itself
   valid QCC input. */
static TCType tcFunctionReturnTypes[MAX_FUNCTIONS];
static TCType tcFunctionParamTypes[MAX_FUNCTIONS][64];
static int tcFunctionNargs[MAX_FUNCTIONS];
static int tcFunctionCount = 0;
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
static char tcCallSavedAdd[64];
static char tcCallSavedMul[64];
static char tcCallSavedRel0[64];
static char tcCallSavedRel1[64];
static int  tcCallDepth = 0;
static char tcPendingAdd = 0;       /* '+' oder '-' */
static char tcPendingMul = 0;       /* '*' oder '/' */
static char tcParenSavedAdd[64];
static char tcParenSavedMul[64];
static char tcParenSavedRel0[64];
static char tcParenSavedRel1[64];
static int  tcParenDepth = 0;
/* Komma-Operator (2026-08-11): hat das zuletzt abgeschlossene Kommaglied einen
   Wert auf dem Operandenstapel hinterlassen? Siehe tc_commavalue. */
static int  tcCommaHasValue = 0;
/* ZIELZUSTAND JE KLAMMEREBENE (2026-08-11). Ein Komma-Glied darf eine Zuweisung
   sein, und tc_target/tc_assignop schreiben ihren Zustand in Globale. Eine
   innere Zuweisung ueberschrieb damit das Ziel einer AEUSSEREN: "x = (y = 1, 2)"
   legte 2 in y und 0 in x. Der einstufige tcPrevTarget*-Puffer (fuer
   tc_chainassign) genuegt dafuer nicht -- bei "(y = (z = 1, 2), 3)" ist er
   bereits mit y belegt, wenn das aeussere x gebraucht wuerde. Der Klammerrahmen
   ist der richtige Ort: er hat schon einen Tiefenstapel, und ein Komma-Ausdruck
   kann nur innerhalb von Klammern stehen. Gesichert wird bei parenOpen,
   zurueckgestellt bei parenClose -- also genau um die Spanne, in der eine
   innere Zuweisung stattfinden kann. */
static int  tcParenSavedTgtSlot[64];
static char tcParenSavedTgtGlobal[64][32];
static int  tcParenSavedTgtIsGlobal[64];
static TCType tcParenSavedTgtType[64];
static int  tcParenSavedTgtIsArray[64];
static int  tcParenSavedTgtIndirect[64];
static char tcParenSavedAssignOp[64][3];
/* tc_commaassign benutzt dieselbe Maschinerie wie eine Zuweisungsanweisung,
   steht im erzeugten Parser aber VOR tc_assign (Reihenfolge der ROUTINE-Bloecke
   dieser Arbeitsdatei) -- daher die Vorwaertsdeklaration. */
void tc_assign(const char* start, const char* end);
static void tcAssignStore(int leaveValue);
/* Dezimaltext -> IEEE-754, definiert in ROUTINE tc_floatlit (der Generator
   gibt Routinen in fester Reihenfolge aus, und tcGlobalOne kommt davor).
   Gebraucht fuer Initialisierer an globalen double. */
int qccDecToDouble(const char* start, const char* end, int neg,
                   unsigned long* hi, unsigned long* lo);
/* Ende eines Gleitkommaliterals ab p: Ziffern, optionaler Punkt mit
   Nachkommaziffern, optionaler Exponent. EINE Funktion, weil die Abgrenzung
   sonst an zwei Stellen stuende (mit und ohne Punkt) -- und genau so ist der
   Exponent beim ersten Anlauf STILL abgeschnitten worden: "double g = 1e2;"
   ergab 1 statt 100, ohne jede Meldung.
   Der Exponent zaehlt nur mit, wenn ihm wirklich Ziffern folgen; "1e" bleibt
   damit ein Fehler statt still zu 1 zu werden. */
/* Initialisiererliste eines double-Arrays: die Werte gehen als BITMUSTER in
   die Daten, eine Zahl kann sie nicht tragen -- deshalb eigene Felder statt
   initValues[]. Auf Dateiebene, weil der Stack eines OS-9-Moduls knapp ist
   (dieselbe Ueberlegung wie beim Umrechner in tc_floatlit). */
#define TC_MAX_DBL_INIT 64
static unsigned long tcDblInitHi[TC_MAX_DBL_INIT];
static unsigned long tcDblInitLo[TC_MAX_DBL_INIT];
static int tcDblInitCount = 0;
static const char* tcFloatLitEnd(const char* p, const char* end) {
	const char* q = p;
	const char* r;
	while (q < end && *q >= '0' && *q <= '9') q++;
	if (q < end && *q == '.') {
		q++;
		while (q < end && *q >= '0' && *q <= '9') q++;
	}
	if (q < end && (*q == 'e' || *q == 'E')) {
		r = q + 1;
		if (r < end && (*r == '+' || *r == '-')) r++;
		if (r < end && *r >= '0' && *r <= '9') {
			while (r < end && *r >= '0' && *r <= '9') r++;
			q = r;
		}
	}
	return q;
}
static char tcIndexSavedAdd[64];
static char tcIndexSavedMul[64];
static char tcIndexSavedRel0[64];
static char tcIndexSavedRel1[64];
static int  tcIndexDepth = 0;
static char tcRel0 = 0; /* Relop-Zeichen; tcRel1==0 wenn einstellig */
static char tcRel1 = 0;
static int  tcTargetSlot = -1;
static char tcTargetGlobal[32];
static int  tcTargetIsGlobal = 0;
static TCType tcTargetType;
static int  tcTargetIsArray = 0;
static int  tcTargetIndirect = 0;
/* Vorheriges Ziel -- tc_target sichert seinen bisherigen Zustand beim
   Betreten dorthin, damit die Kettenzuweisung BEIDE Ziele kennt. */
static int  tcPrevTargetSlot = -1;
static char tcPrevTargetGlobal[32];
static int  tcPrevTargetIsGlobal = 0;
static TCType tcPrevTargetType;
static int  tcPrevTargetIsArray = 0;
static int  tcPrevTargetIndirect = 0;
/* chainAssign ist eine ALTERNATIVE INNERHALB von assignStmt -- passt sie,
   feuert anschliessend auch noch die Aktion der umschliessenden Regel.
   Dieses Flag sagt tc_assign, dass die Zuweisung bereits erledigt ist. */
static int  tcChainHandled = 0;
static char tcAssignOp[3];
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
/* Die Verschachtelungstiefe der Kontrollstrukturen. Die Zahl stand vorher
   als Literal 64 an SECHS Stellen (fuenf Felder und die Pruefung) -- eine
   Invariante an sechs Orten. Angehoben 2026-09-07, weil
   qcc_backend_c.cpp daran scheiterte: seine Opcode-Verteilung ist eine
   lange "else if"-Kette, und jedes Glied ist im Modell eine Ebene tiefer.

   DIE ZAHL IST GEMESSEN, nicht geschaetzt: mit 67 kippt qcc_backend_c.cpp,
   mit 68 laeuft es durch -- die alten 64 waren um VIER zu knapp. Gesucht
   wurde binaer zwischen 64 und 256. Hier steht 128, also nicht der
   Grenzwert: eine Grenze auf dem gemessenen Bedarf reisst beim naechsten
   zusaetzlichen else-if. Der Platz kostet 128 * 17 = 2176 Byte, und da
   QCC sich selbst uebersetzt, wandert das in sein Modul.

   Zu tief zu schachteln bleibt eine echte Grenze mit Meldung -- das ist
   Absicht: an einer Modellgrenze abzubrechen ist besser als zu raten. */
#define TC_MAX_CTRL 128

static int tcCtrlTop[TC_MAX_CTRL];
static int tcCtrlEnd[TC_MAX_CTRL];
static int tcCtrlCont[TC_MAX_CTRL];
static int tcCtrlExtra[TC_MAX_CTRL];
static char tcCtrlKind[TC_MAX_CTRL];      /* 'i'=if, 'w'=while, 'f'=for, 'd'=do-while, 's'=switch */
static int  tcCtrlDepth = 0;
/* switch/case: gestapelte Case-Label (case A: case B: body) unterstuetzt, aber KEIN
   Fallthrough MIT Code zwischen verschiedenen Bodies (jeder Body endet implizit wie mit
   break) -- deckt genau das reale Nutzungsmuster in parsec.cpp/codegen.cpp ab. */
#define MAX_SWITCH 16
static int tcSwitchBodyLabel[MAX_SWITCH];
static int tcSwitchNextLabel[MAX_SWITCH];
/* Sprungziel fuer den DURCHFALL aus dem vorigen case-Rumpf in den
   naechsten, oder -1. Es wird am Ende einer case-Gruppe reserviert und
   vom naechsten Rumpf (bzw. vom switch-Ende) eingeloest -- siehe
   tc_casegroup_end. */
static int tcSwitchFallLabel[MAX_SWITCH];
static int tcSwitchEndLabel[MAX_SWITCH];
static int tcSwitchGroupOpen[MAX_SWITCH];
static int tcSwitchHadDefault[MAX_SWITCH];
static int  tcSwitchDepth = 0;
/* Cast-Zieltyp separat zwischenspeichern (nicht ueber tcCurrentType), weil das
   Operanden-factor (castOperand) selbst z.B. ein verschachteltes sizeof/cast
   enthalten und tcCurrentType dabei ueberschreiben kann, bevor castExpr's
   eigene Aktion sie liest. */
static TCType tcCastStack[16];
static int  tcCastDepth = 0;
static char tcLogicKind[64];
static int tcLogicBranch[64];
static int tcLogicDone[64];
static int tcLogicDepth = 0;
static char tcBitKind[64];
static int  tcBitDepth = 0;
static char tcPendingShift0 = 0;
static char tcPendingShift1 = 0;
static int tcTernaryFalse[64];
static int tcTernaryDone[64];
static int tcTernaryDepth = 0;
/* Wie bei Aufrufen (tc_callname) und Indizes (tc_arg) muessen die VERZOEGERTEN
   Operatoren um einen Ternaer herum gerettet werden: QCC emittiert "*"/"+"
   und Vergleiche nicht sofort, sondern gemerkt ueber tcPendingMul/tcPendingAdd/
   tcRel0/tcRel1. Ohne das Retten feuert ein umschliessendes "*" MITTEN in die
   Ternaer-Zweige hinein -- "(a?10:20) * (b?1:4)" multiplizierte mit dem blossen
   b statt mit dem Ergebnis des zweiten Ternaers: stiller Falschcode, keine
   Fehlermeldung. Latent vorhanden, sichtbar erst mit verschachtelten Ternaeren. */
static char tcTernSavedAdd[64];
static char tcTernSavedMul[64];
static char tcTernSavedRel0[64];
static char tcTernSavedRel1[64];
static TCType tcTernaryTrueType[64];
/* Positionsangabe in Diagnosen (2026-09-01). Bis dahin nannte KEINE der 209
   Meldungen einen Ort -- in der Bootstrap-Quelle (88.874 IR-Zeilen) ist ein
   blosses "qcc: unknown variable" praktisch nicht verwertbar. parserInputStart
   setzt der erzeugte Parser in main() auf den Anfang der Eingabe (s. genParserC
   in Q9-Parsec); daraus laesst sich zu jedem Zeiger in den Eingabepuffer Zeile
   und Spalte zaehlen. Aufrufer mit eigener Spanne uebergeben deren start (zeigt
   auf den Anfang des Konstrukts), alle uebrigen den globalen Parserzeiger p
   (steht dann hinter dem Konstrukt, gleiche Zeile). Ohne Anker faellt die
   Ausgabe auf das alte "qcc: " zurueck -- die Meldung geht also nie verloren. */
static void tcErrAt(const char* at) {
	const char* q; const char* lineStart; int line;
	if (at == 0 || parserInputStart == 0) { fprintf(stderr, "qcc: "); return; }
	line = 1; lineStart = parserInputStart;
	for (q = parserInputStart; q < at; q++) {
		if (*q == '\n') { line++; lineStart = q + 1; }
	}
	fprintf(stderr, "qcc: %d:%d: ", line, (int)(at - lineStart) + 1);
}
static void tcPushCtrl(char kind, int top, int cont, int end, int extra) {
	if (tcCtrlDepth >= TC_MAX_CTRL) { actionErrors++; tcErrAt(parserActionAt); fprintf(stderr, "control nesting too deep\n"); return; }
	tcCtrlKind[tcCtrlDepth] = kind;
	tcCtrlTop[tcCtrlDepth] = top;
	tcCtrlCont[tcCtrlDepth] = cont;
	tcCtrlEnd[tcCtrlDepth] = end;
	tcCtrlExtra[tcCtrlDepth] = extra;
	tcCtrlDepth++;
}
static int tcNeedCtrl(char kind) {
	if (tcCtrlDepth > 0 && tcCtrlKind[tcCtrlDepth - 1] == kind) return 1;
	actionErrors++; tcErrAt(parserActionAt); fprintf(stderr, "internal control-frame mismatch\n");
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
		tcErrAt(parserActionAt); fprintf(stderr, "too many goto labels\n"); actionErrors++; return -1;
	}
	tcCopy(tcGotoNames[tcGotoCount], name, name + strlen(name));
	tcGotoLabel[tcGotoCount] = tcNextLabel++;
	tcGotoDefined[tcGotoCount] = 0;
	tcGotoUsed[tcGotoCount] = 0;
	return tcGotoCount++;
}
/* RUECKWAERTS (2026-08-20, siehe tcScopeMark): die ZULETZT deklarierte
   Namensgleichheit gewinnt, damit eine innere Deklaration eine gleichnamige
   aeussere verdeckt. Slots verlassener Blocke sind tcLocalDead und damit
   unsichtbar. */
static int tcLookupLocal(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = tcLocalCount - 1; i >= 0; i--) if (!tcLocalDead[i] && tcEq(tcNames[i], name)) return i;
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
static char tcDiagVarRef[128];
static int tcLookupStructField(int sid, const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	if (sid < 0 || sid >= tcStructCount) {
		return -1;
	}
	for (i = 0; i < tcStructFieldCount[sid]; i++) if (tcEq(tcStructFieldNames[sid][i], name)) return i;
	return -1;
}
/* Traegt den NAMEN vor dem Rumpf ein, damit der Rumpf ihn schon sehen kann
   ("struct N { struct N *next; };"). Ein bereits bekannter Name wird nicht
   angetastet -- die Duplikatspruefung bleibt damit in tcRegisterStruct. */
static void tcStructPredeclare(const char* nameStart, const char* nameEnd) {
	tcStructPending = -1;
	if (tcLookupStruct(nameStart, nameEnd) >= 0) return;
	if (tcStructCount >= MAX_STRUCTS) return;
	tcCopy(tcStructNames[tcStructCount], nameStart, nameEnd);
	tcStructFieldCount[tcStructCount] = 0;
	tcStructPending = tcStructCount;
	tcStructCount++;
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
/* EINE STRUCT IST KEIN REGISTERWERT. Jede Stelle, die aus einer Adresse einen
   Wert laedt, muss fuer eine GANZE Struct die Adresse stehen lassen --
   tcTypeTag('s') liefert 'i', ein LOADIND laese sonst vier Byte der Struct als
   Zahl und gaebe sie als Adresse weiter (auf dem 68k landet damit ein
   Datenwert in A0). Als eigene Funktion, weil dieselbe Regel an mehreren
   Emissionsstellen gilt: tcEmitPtrIndexChain und tcEmitPtrFieldIndex
   (2026-09-07), tc_derefref und der globale Zeigerindex (2026-09-14).
   Argument als Zeiger, damit der Helfer nicht selbst wieder eine Struct per
   Wert nimmt. */
static int tcIsWholeStruct(const TCType* t) { return t->base == 's' && !t->pointers; }
/* Liegt eine LOKALE Variable als BLOCK (LARRAY) statt in einem Slot?
   Arrays, ganze structs -- und seit 2026-09-16 auch double, denn acht Byte
   passen nicht in einen Slot (im Rahmen vier Byte breit).
   Die Frage entscheidet, ob "&x" PUSHADDR L oder ADDRL braucht: ADDRL
   liefert die SLOT-Adresse, und die zeigt bei einem Block ins Leere --
   "double *p = &a; *p" las damit Muell, ohne jede Meldung. Genau derselbe
   Fehler war bei skalaren structs schon einmal aufgetreten und wurde
   damals an der Fundstelle geflickt. Deshalb steht die Regel jetzt als
   Funktion da: der naechste Blocktyp wird sonst wieder vergessen. */
static int tcLocalIsBlock(int slot, TCType t) {
	if (tcLocalArrayLen[slot]) return 1;
	if (t.pointers) return 0;
	return t.base == 's' || t.base == 'd';
}
static int tcIsFnPtr(TCType t) { return t.base == 'F' && t.pointers == 1; }
static TCType tcMakeFnPtr(int sigId) {
	TCType t = tcMakeType('F', 1);
	t.structId = (unsigned char)(sigId + 1);
	return t;
}
static int tcIsInteger(TCType t) { return !t.pointers && (t.base == 'i' || t.base == 'u' || t.base == 'c' || t.base == 'h' || t.base == 'z'); }
static int tcIsBool(TCType t) { return !t.pointers && t.base == 'b'; }
static int tcIsDouble(TCType t) { return !t.pointers && t.base == 'd'; }
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
static int tcIsTruthy(TCType t) { return t.pointers != 0 || t.base == 'b' || t.base == 'i' || t.base == 'u' || t.base == 'c' || t.base == 'h' || t.base == 'z'; }
/* C89 3.6.4.1/3.6.5: eine Bedingung darf JEDEN skalaren Typ haben und
   bedeutet "ungleich 0". Fuer Ganzzahlen, Zeichen und Zeiger gilt das in QCC
   laengst (tcIsTruthy); double kam nie dazu, obwohl "if (x)" damit genauso
   gueltiges C ist -- es wurde als "expects bool" abgelehnt.
   Der Vergleich muss EMITTIERT werden, nicht bloss im Typstapel vermerkt: auf
   dem Stapel liegen acht Byte, und JZ/NOT erwarten einen ganzzahligen Wert.
   0.0 ist als IEEE-754-Bitmuster schlicht null, deshalb "PUSHD 0 0".
   EINE Funktion fuer alle vier Bedingungsstellen (if, while, do-while, !) --
   eine vergessene liesse acht Byte auf dem Stapel liegen. */
static TCType tcCondValue(TCType t) {
	if (!tcIsDouble(t)) return t;
	printf("PUSHD 0 0\nDCMPNE\n");
	return tcMakeType('b', 0);
}
static TCType tcPointerTo(TCType t) { if (t.pointers < 255) t.pointers++; else actionErrors++; return t; }
static TCType tcPointee(TCType t) { if (t.pointers) t.pointers--; else actionErrors++; return t; }
/* 'h' (short, 2026-09-09) dazu -- dieselbe Fallgruppe wie 'c'/'b': der
   Basiswert IST schon der richtige einbuchstabige IR-Tag, kein Umweg auf
   'i' noetig. qcc_backend_c.cpp muss 'h' ab hier ueberall dort kennen, wo
   bisher nur zwischen "byteweise" (isByteWord: 'c'/'b') und "langwortweise"
   (alles andere) unterschieden wurde -- s. tagSize() dort. */
static char tcTypeTag(TCType t) { return t.pointers ? 'p' : (t.base == 'c' || t.base == 'b' || t.base == 'h' || t.base == 'd') ? t.base : 'i'; }
/* Opcode-Namenssuffix fuer LOADL/LOADLH/LOADC-artige Paare (2026-09-09, mit
   short dazugekommen): "" fuer den normalen 4-Byte-Fall (LOADL, LOADG, ...),
   "C" fuer char/bool (1 Byte), "H" fuer short (2 Byte) -- an DREI Stellen
   gebraucht (Praeinkrement/-dekrement, static-lokale Deklaration, globales
   Lesen); ein gemeinsamer Helfer statt drei Kopien, damit eine kuenftige
   vierte Groesse nicht an einer der drei Stellen vergessen wird. */
/* 'd' (double) ist seit 2026-09-16 dabei: LOADGD/STOREGD. Der Wert liegt
   als BLOCK, nicht im Slot -- die Slots sind zielabhaengig breit (68k 4
   Byte, ARM64 16), ein Block ist es nicht (docs/FLOAT_IR_ENTWURF_de.md). */
static const char* tcWordSuffix(char tag) { return tag == 'i' ? "" : tag == 'h' ? "H" : tag == 'd' ? "D" : "C"; }
/* Dieselbe Dreiteilung wie tcWordSuffix, aber fuer LOAD%s/STORE%s (lokal
   OHNE G-Praefix): der int-Fall braucht dort ein "L" statt "" (LOADL/
   STOREL sind eigene Opcode-Namen, es gibt kein blankes "LOAD"/"STORE"). */
static const char* tcWordSuffixL(char tag) { return tag == 'i' ? "L" : tag == 'h' ? "LH" : tag == 'd' ? "D" : "C"; }
/* Byte-Groesse eines Skalartyps fuer die 2D-Feld-Zeilenschrittweite
   (tcEmitFieldRowColIndex, zweimal dupliziert -- 2026-09-09 mit short
   dazugekommen). NICHT dieselbe Groesse wie im 68k-Backend (dort eigene,
   unabhaengige tagSize() fuer LOADIND/PTRINDEX & Co, s. qcc_backend_c.cpp) --
   dieser Wert geht hier als reine Zahl in eine echte Laufzeitmultiplikation
   (IPADDN), keine lsl.l-Kurzform. */
static int tcElemByteSize(TCType t) { char g = tcTypeTag(t); return g == 'c' ? 1 : g == 'h' ? 2 : g == 'd' ? 8 : 4; }
/* Eine Groesse oder ein Offset als IR-Text. Entweder eine schlichte Zahl
   oder die Form k+nP: k ist der zeigerfreie Anteil in Byte, n die Zahl der
   Zeigergroessen darin. Das Backend setzt sein P ein (qir68k 4, qirarm64 8),
   damit dieselbe IR fuer beide Ziele gilt. Ohne Zeigeranteil bleibt die
   alte Schreibweise stehen -- so aendern sich nur die IR-Zeilen, die es
   wirklich betrifft. */
static void tcEmitNum(int k, int n) {
	if (n == 0) printf("%d", k);
	else printf("%d+%dP", k, n);
}
/* Structgroesse bzw. Feldoffset als IR-Operand -- immer ueber diese beiden,
   nie die Tabellen direkt in ein printf: der Zeigeranteil wuerde sonst
   stillschweigend fehlen. */
static void tcEmitStructSize(int sid) {
	tcEmitNum(tcStructByteSizeK[sid], tcStructByteSizeN[sid]);
}
static void tcEmitFieldOffset(int sid, int fi) {
	tcEmitNum(tcStructFieldOffsetK[sid][fi], tcStructFieldOffsetN[sid][fi]);
}
/* Array-Initialisiererwert auf die Speicherbreite kuerzen (GINIT/STOREIDX-
   Init, 2026-09-09 mit short dazugekommen) -- dieselbe Kuerzung, die
   tc_staticlocal fuer eine EINZELNE static-lokale Variable schon macht. */
static long tcTruncInit(TCType t, long v) {
	if (t.pointers) return v;
	if (t.base == 'c') return v & 255;
	if (t.base == 'h') return v & 65535;
	return v;
}
/* Der Indexschritt fuer ein ARRAY-FELD einer Struct.
 *
 * IPADD skaliert den Index mit der Groesse des TYPTAGS -- fuer 'p' sind das
 * auf dem 68k vier Byte. Im Struct belegt ein Zeiger n*P Byte (s. tcEmitNum),
 * damit dasselbe Offset auch fuer ARM64 stimmt. Fuer Zeigerarrays muss die
 * Schrittweite deshalb in BYTE angegeben werden: IPADDN, dasselbe Mittel, das
 * die 2D-Zeilen schon nutzen.
 *
 * Das steht als EINE Funktion und nicht sechsmal ausgeschrieben: die Regel
 * gilt an drei lesenden und drei schreibenden Emissionsstellen, und eine
 * Invariante, die an sechs Orten wiederholt wird, geht bei der naechsten
 * Aenderung an einem davon verloren. */
/* Der Indexschritt fuer "&arr[i]" auf ein ARRAY VON STRUCTS.
 *
 * PTRINDEX skaliert mit der Groesse des TYPTAGS, und tcTypeTag gibt fuer eine
 * Struct 'i' -- also VIER Byte. Bei einer 80 Byte grossen Struct zeigt
 * &arr[1] damit vier Byte hinter arr[0] statt achtzig. Das war ein STILLER
 * Falschcode-Fehler (2026-09-07 gefunden): QCCs Backend legt seine
 * IR-Anweisungen als "Instr ir[98304]" ab und holt sie mit
 * "insP = &ir[irCount]" -- auf dem 68030 schrieben dadurch alle Anweisungen
 * uebereinander, sichtbar als ein op-Feld "FUNCLOADPUSHCMPLJZ" aus je vier
 * Zeichen. Im Korpus kam der Fall nie vor.
 *
 * Deshalb hier IPADDN mit der wirklichen Elementgroesse -- dasselbe Mittel
 * wie bei den 2D-Zeilen und den Zeigerarray-Feldern. */
static void tcEmitElemIndexStep(TCType t) {
	if (t.base == 's' && !t.pointers)
		{ printf("IPADDN "); tcEmitStructSize(t.structId - 1); printf("\n"); }
	else
		printf("PTRINDEX %c\n", tcTypeTag(t));
}

/* "s.p[j]" bzw. "sp->p[j]" -- Indizierung DURCH ein skalares ZEIGERfeld.
 *
 * Auf dem Stapel liegt der Index UNTEN und die ADRESSE DES FELDES oben; die
 * Aufrufer haben dafuer schon PUSH <offset> / LOADP|LOADGP|PUSHADDR /
 * IPADD c emittiert (die Ordnung ist bei tc_varref beschrieben: "der
 * Index-Ausdruck hat seinen Wert bereits VOR uns gepusht").
 *
 * Gebraucht wird aber der Zeiger IM Feld, nicht die Adresse des Feldes --
 * deshalb erst LOADIND p. Danach skaliert der Indexschritt um die Groesse
 * des ZIELtyps.
 *
 * laden = 1 fuer einen Lesezugriff, 0 fuer ein Zuweisungsziel (dort setzt
 * der Aufrufer tcTargetIndirect und der Store kommt spaeter). Zeigt das
 * Feld auf eine STRUCT, wird nie geladen: dort IST die Adresse der Wert --
 * dieselbe Regel wie in tcEmitPointerIndexChain.
 *
 * KEIN const-SCHUTZ HIER, und das ist nicht vergessen: ein "const" am
 * Feldtyp wird von der Grammatik geparst und VERWORFEN
 * (Data/qcc.ebnf: "fieldConstKw ist eine AKTIONSLOSE Kopie von constKw"),
 * weil ein Verweis auf constKw tc_const ausloesen wuerde und dessen
 * tcPendingConst erst beim naechsten Parameter oder Lokalen konsumiert
 * wird -- dort erzwaenge es faelschlich Konstantheit. Folge:
 * "struct P { const char *cp; }" mit "p.cp[0] = 'x'" laeuft durch. Bei
 * einer VARIABLEN wird dasselbe korrekt gemeldet. Eine Pruefung auf
 * pointeeConst stand hier zwischenzeitlich und war WIRKUNGSLOS, weil das
 * Feld die Qualifikation nie traegt; eine Pruefung, die Schutz
 * vortaeuscht, ist schlechter als diese Notiz. Wer es angeht, faengt bei
 * fieldConstKw an, nicht hier.
 *
 * Vorher war der Fall an sechs Stellen abgelehnt ("scalar struct field
 * cannot be indexed"). Eine SIEBTE bleibt abgelehnt: nach einem
 * Funktionsaufruf (f().feld[i]) gilt eine andere Stapelordnung -- dort
 * steht PADD mit vertauschten Operanden statt IPADD --, und dafuer gibt es
 * keinen Aufrufer. Lieber eine Meldung als eine zweite Ordnung nebenher. */
static TCType tcEmitPtrFieldIndex(int sid, int fi, int laden) {
	TCType el = tcPointee(tcStructFieldTypes[sid][fi]);

	/* Beim SCHREIBEN durch ein "const char *"-Feld: verboten. Die
	   Qualifikation liegt seit 2026-09-07 wirklich im Feldtyp (siehe
	   tcStructFieldConst), vorher stand hier eine wirkungslose Pruefung.
	   Beim LESEN ist alles erlaubt. Verankert wird an der Aktionsspanne,
	   weil der Helfer die Quellstelle nicht kennt. */
	if (!laden && tcStructFieldTypes[sid][fi].pointeeConst
	    && tcStructFieldTypes[sid][fi].pointers == 1) {
		tcErrAt(parserActionAt);
		fprintf(stderr, "cannot assign through pointer to const\n");
		actionErrors++;
	}
	printf("LOADIND p\n");
	if (el.base == 's' && !el.pointers) {
		{ printf("IPADDN "); tcEmitStructSize(el.structId - 1); printf("\n"); }
		return el;
	}
	printf("IPADD %c\n", tcTypeTag(el));
	if (laden)
		printf("LOADIND %c\n", tcTypeTag(el));
	return el;
}

static void tcEmitFieldIndexStep(int sid, int fi) {
	if (tcIsPointer(tcStructFieldTypes[sid][fi])) { printf("IPADDN "); tcEmitNum(0, 1); printf("\n"); }
	else printf("IPADD %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
}
/* feld[i][j] (2D-Array-FELD) und arr[i].feld[j]/ptr[i].feld[j] (1D-Array-FELD
   hinter einer Array-von-structs- bzw. Pointer-auf-struct-Indizierung)
   brauchen beide einen ZWEITEN Indexwert, waehrend dazwischenliegender Code
   (Feldadress- bzw. Element-von-arr-Adressberechnung) den ERSTEN konsumiert.
   Dieselbe Technik wie tcEmitPointerIndexChain (Scratch-Global statt
   Stack-Rotation, die die IR nicht kennt) -- hier auf GENAU EINEN gemerkten
   Wert vereinfacht, weil an beiden Stellen nie mehr als zwei Indexebenen
   vorkommen koennen (ein Feld ist hoechstens 2D, structs schachteln nicht). */
static void tcStashChainedIndex(void) {
	if (!tcPtrIdxScratchDeclared[2]) { printf("GLOBAL __ptrIdx_2 0 i 1\n"); tcPtrIdxScratchDeclared[2] = 1; }
	printf("STOREG __ptrIdx_2\n");
}
static void tcUnstashChainedIndex(void) {
	printf("LOADG __ptrIdx_2\nSWAP\n");
}
/* feld[i][j] eines zweidimensionalen Array-Felds (tcStructFieldRowLen>0).
   Stack bei Aufruf: [ersterIndex, Feldadresse] -- der zweite Index wurde
   vorher mit tcStashChainedIndex() zwischengelagert. laden=1: Lesekontext
   (haengt LOADIND an); laden=0: Zuweisungsziel (Adresse bleibt liegen,
   Aufrufer setzt tcTargetIndirect). */
static TCType tcEmitFieldRowColIndex(int sid, int fi, int laden) {
	int elemSize = tcElemByteSize(tcStructFieldTypes[sid][fi]);
	printf("IPADDN %d\n", tcStructFieldRowLen[sid][fi] * elemSize);
	tcUnstashChainedIndex();
	tcEmitFieldIndexStep(sid, fi);
	if (laden) printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
	return tcStructFieldTypes[sid][fi];
}
/* arr[i].feld[j] / ptr[i].feld[j] eines EINDIMENSIONALEN Array-Felds: arr[i]s
   eigener Index verbraucht sich in der Adressberechnung DAZWISCHEN, hier wird
   nur noch der zwischengelagerte Feldindex entnommen und der bestehende
   Einzel-Indexschritt angewandt. */
static TCType tcEmitStashedFieldIndex(int sid, int fi, int laden) {
	tcUnstashChainedIndex();
	tcEmitFieldIndexStep(sid, fi);
	if (laden) printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
	return tcStructFieldTypes[sid][fi];
}
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
		tcErrAt(parserActionAt); fprintf(stderr, "function has too many parameters for a function pointer\n");
		actionErrors++; return -1;
	}
	for (i = 0; i < tcFnSigCount; i++) {
		if (tcFnSigNargs[i] != tcFunctionNargs[fnIdx]) continue;
		if (!tcSameType(tcFnSigRet[i], tcFunctionReturnTypes[fnIdx])) continue;
		for (k = 0; k < tcFnSigNargs[i]; k++)
			if (!tcSameType(tcFnSigParams[i][k], tcFunctionParamTypes[fnIdx][k])) break;
		if (k == tcFnSigNargs[i]) return i;
	}
	if (tcFnSigCount >= MAX_FNSIGS) {
		tcErrAt(parserActionAt); fprintf(stderr, "too many function pointer signatures\n");
		actionErrors++; return -1;
	}
	tcFnSigRet[tcFnSigCount] = tcFunctionReturnTypes[fnIdx];
	tcFnSigNargs[tcFnSigCount] = tcFunctionNargs[fnIdx];
	for (k = 0; k < tcFunctionNargs[fnIdx]; k++)
		tcFnSigParams[tcFnSigCount][k] = tcFunctionParamTypes[fnIdx][k];
	return tcFnSigCount++;
}
static void tcTypePush(TCType type) { if (tcValueDepth < 256) tcValueTypes[tcValueDepth++] = type; else actionErrors++; }
static TCType tcTypePop(void) { return tcValueDepth > 0 ? tcValueTypes[--tcValueDepth] : tcBadType(); }
/* Die Typwert-Stacks duerfen im selbstgehosteten 68k-Pfad keine TCType-Werte
   als Funktionsargument/Rueckgabe bewegen. Stattdessen werden die vier Bytes
   explizit kopiert; Zeigerparameter sind im ABI stabil. */
static void tcTypePush4(int base, int pointers, int structId, int pointeeConst) {
	if (tcValueDepth >= 256) { actionErrors++; return; }
	tcValueTypes[tcValueDepth].base = (unsigned char)base;
	tcValueTypes[tcValueDepth].pointers = (unsigned char)pointers;
	tcValueTypes[tcValueDepth].structId = (unsigned char)structId;
	tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)pointeeConst;
	tcValueDepth++;
}
static void tcTypePop4(TCType* out) {
	if (tcValueDepth <= 0) { out->base = '?'; out->pointers = 0; out->structId = 0; out->pointeeConst = 0; actionErrors++; return; }
	tcValueDepth--;
	out->base = tcValueTypes[tcValueDepth].base;
	out->pointers = tcValueTypes[tcValueDepth].pointers;
	out->structId = tcValueTypes[tcValueDepth].structId;
	out->pointeeConst = tcValueTypes[tcValueDepth].pointeeConst;
}
static int tcCompatible4(const TCType* wanted, const TCType* got) {
	if (wanted->base == got->base && wanted->pointers == got->pointers &&
	    ((wanted->base != 's' && wanted->base != 'F') || wanted->structId == got->structId)) return 1;
	/* ISO C permits an implicit conversion between void* and every object
	   pointer.  The pointer DEPTH is intentionally irrelevant here: malloc()
	   (void*) must initialize char**, T** and other object-pointer values too. */
	if (wanted->pointers && got->pointers &&
	    (wanted->base == 'v' || got->base == 'v')) return 1;
	if (wanted->pointers) return !got->pointers && got->base == 'z';
	/* 2026-09-09: auf tcIsInteger() umgestellt (vorher hier dreifach inline
	   dupliziert, ohne 'h' geraten haette short lautlos wieder eine eigene
	   Ausnahme gebraucht -- s. [[schrittweiten-geschwister-suchen]]). */
	if (tcIsInteger(*wanted) && !wanted->pointers && !got->pointers && got->base == 'b') return 1;
	return !wanted->pointers && !got->pointers && tcIsInteger(*wanted) && tcIsInteger(*got);
}
#define TC_SET_CURRENT(B, P) do { tcCurrentType.base = (unsigned char)(B); tcCurrentType.pointers = (unsigned char)(P); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0)
#define TC_TYPE_PUSH(B, P, S, C) do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)(B); tcValueTypes[tcValueDepth].pointers = (unsigned char)(P); tcValueTypes[tcValueDepth].structId = (unsigned char)(S); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(C); tcValueDepth++; } else actionErrors++; } while (0)
#define TC_TYPE_POP(OUT) do { \
	if (tcValueDepth > 0) { \
		tcValueDepth--; \
		OUT.base = tcValueTypes[tcValueDepth].base; \
		OUT.pointers = tcValueTypes[tcValueDepth].pointers; \
		OUT.structId = tcValueTypes[tcValueDepth].structId; \
		OUT.pointeeConst = tcValueTypes[tcValueDepth].pointeeConst; \
	} else { \
		OUT.base = '?'; OUT.pointers = 0; \
		OUT.structId = 0; OUT.pointeeConst = 0; actionErrors++; \
	} \
} while (0)
/* Prae-/Postinkrement/-dekrement, nur einfache int/unsigned/char-Skalare (lokal/global) --
   Praefix: LOAD;PUSH 1;ADD-oder-SUB;DUP;STORE (laesst NEUEN Wert); Postfix: LOAD;DUP;PUSH 1;
   ADD-oder-SUB;STORE (laesst ALTEN Wert). STOREC/STOREGC uebernehmen die Byte-Kuerzung wie
   bei normalen Zuweisungen -- kein extra NARROWC noetig. */
/* (*p)++ / ++(*p): Inkrement durch einen Zeiger. Braucht KEINEN SWAP --
   Adresse und Wert werden schlicht mehrfach geladen (der Zeiger steht in
   einer Variablen, das erneute Laden ist also seiteneffektfrei).
   Postfix laesst den ALTEN Wert ganz unten liegen, Praefix laedt den neuen
   nach dem Speichern einfach neu. */
/* ++/-- AUF EINEM double (2026-09-16). Der Fehler, der das noetig machte:
   'd' fiel in tcIncDecEmit in den char-Auffangzweig (LOADC/STOREC) und in den
   drei Adressformen durch den Waechter hindurch -- das Inkrement war
   WIRKUNGSLOS bzw. rechnete ganzzahlig auf einem FPU-Bitmuster, in beiden
   Faellen OHNE Meldung. Ein Schrittweiten-Fehler sitzt eben nie an einer
   Stelle allein (docs/FORTSCHRITT.md), deshalb stehen beide Regeln hier als
   je EINE Funktion statt als Kopien an fuenf Emissionsstellen. */

/* Die drei ADRESSFORMEN (Struct-Feld, Index-Element, Zeigerziel) laden ueber
   LOADIND/LOADIDX. Fuer double fehlte lange die Rotation: beim POSTFIX muss
   der alte Wert als Ergebnis UNTER der Adresse liegen bleiben, und SWAP
   taugt dafuer nicht -- es tauscht zwei Langworte und zerrisse die acht Byte.
   Seit 2026-09-16 gibt es dafuer DSWAP (tauscht das oberste double mit dem
   4-Byte-Wert darunter, also einer Adresse oder einem Index). Der PRAEFIX-Fall
   braucht ihn nicht; dort genuegt die vorhandene Choreographie.
   "1.0" steht als IEEE-754-Bitmuster da (0x3FF0000000000000, hi zuerst) --
   PUSHD nimmt zwei 32-Bit-Haelften, weil die IR keinen Gleitkommatext traegt. */
#define TC_DBL_ONE "PUSHD 1072693248 0"

/* Wird das Ergebnis eines ++/-- als ANWEISUNG verworfen ("a++;" oder der
   for-Schritt), liegt bei double ein 8-Byte-Block auf dem Stapel -- ein
   schlichtes DROP raeumt nur einen Slot ab und laesst den Stapel schief.
   Beide Verwerfstellen gehen deshalb durch diese eine Funktion. */
static void tcDropExprResult(void) {
	TCType t = tcTypePop();
	if (tcIsDouble(t)) printf("DDROP\n");
	else printf("DROP\n");
}

static void tcDerefIncDec(const char* start, const char* end, int isDec, int isPre) {
	const char* p = start;
	const char* ns; const char* ne;
	int slot, global = -1; TCType pt, vt; char tag; const char* ld;
	while (p < end && (*p == '(' || *p == '*' || *p == ' ' || *p == '\t')) p++;
	ns = p; ne = tcWordEnd(ns, end);
	slot = tcLookupLocal(ns, ne);
	if (slot < 0) global = tcLookupGlobal(ns, ne);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(ne - ns), ns); actionErrors++; tcTypePush(tcBadType()); return; }
	pt = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIsPointer(pt)) { tcErrAt(start); fprintf(stderr, "'*' requires a pointer\n"); actionErrors++; tcTypePush(tcBadType()); return; }
	vt = tcPointee(pt);
	if (tcIsPointer(vt) || vt.base == 's' || vt.base == 'b') {
		tcErrAt(start); fprintf(stderr, "++/-- through a pointer is only supported for int/unsigned/char in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tag = tcTypeTag(vt);
	ld = slot >= 0 ? "LOADP" : "LOADGP";
	if (!isPre) {
		/* alter Wert zuerst -- er bleibt als Ergebnis unter allem liegen */
		if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
		printf("LOADIND %c\n", tag);
	}
	if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
	if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
	/* tcDerefIncDec laedt den Zeiger mehrfach, statt ihn umzuschieben --
	   deshalb kommt dieser Fall ohne DSWAP aus. */
	if (tcIsDouble(vt)) printf("LOADIND d\n%s\n%s\nSTOREIND d\n", TC_DBL_ONE, isDec ? "DSUB" : "DADD");
	else printf("LOADIND %c\nPUSH 1\n%s\nSTOREIND %c\n", tag, isDec ? "SUB" : "ADD", tag);
	if (isPre) {
		if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
		printf("LOADIND %c\n", tag);
	}
	tcTypePush(vt);
}
/* a[i]++ / ++a[i]: der Indexwert liegt beim Aufruf bereits auf dem Stack
   (die index-Regel hat ihn emittiert) und darf NICHT neu berechnet werden.
   Postfix braucht SWAP, um den alten Wert unter den Index zu bekommen;
   Praefix kommt mit zweimaligem DUP aus und laedt den neuen Wert am Ende neu. */
static void tcIndexIncDec(const char* start, const char* end, int isDec, int isPre) {
	const char* ne = tcWordEnd(start, end);
	int slot = tcLookupLocal(start, ne), global = -1;
	TCType et; char tag; char buf[64];
	if (slot < 0) global = tcLookupGlobal(start, ne);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(ne - start), start); actionErrors++; tcTypePush(tcBadType()); return; }
	et = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (tcIsPointer(et) || et.base == 's' || et.base == 'b') {
		tcErrAt(start); fprintf(stderr, "++/-- on an indexed element is only supported for int/unsigned/char in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tag = tcTypeTag(et);
	if (slot >= 0) sprintf(buf, "L %d %c", slot, tag); else sprintf(buf, "G %s %c", tcGlobalNames[global], tag);
	if (isPre) {
		/* DUP bleibt hier richtig: vervielfaeltigt wird der INDEX, kein
		   Zeiger (anders als in tcMemberIncDec, wo DUPP noetig ist). */
		if (tcIsDouble(et))
			printf("DUP\nDUP\nLOADIDX %s\n%s\n%s\nSTOREIDX %s\nLOADIDX %s\n",
			       buf, TC_DBL_ONE, isDec ? "DSUB" : "DADD", buf, buf);
		else
		printf("DUP\nDUP\nLOADIDX %s\nPUSH 1\n%s\nSTOREIDX %s\nLOADIDX %s\n",
		       buf, isDec ? "SUB" : "ADD", buf, buf);
	} else {
		if (tcIsDouble(et))
			printf("DUP\nLOADIDX %s\nDSWAP\nDUP\nLOADIDX %s\n%s\n%s\nSTOREIDX %s\n",
			       buf, buf, TC_DBL_ONE, isDec ? "DSUB" : "DADD", buf);
		else
		printf("DUP\nLOADIDX %s\nSWAP\nDUP\nLOADIDX %s\nPUSH 1\n%s\nSTOREIDX %s\n",
		       buf, buf, isDec ? "SUB" : "ADD", buf);
	}
	tcTypePush(et);
}
/* x.feld++ / p->feld++ (und die Praefixformen). Die Feldadresse wird wie in
   tc_varref berechnet (Basis + Offset, IPADD erwartet den Zeiger OBEN, deshalb
   PUSH offset zuerst). Danach dieselbe Stapelchoreografie wie beim
   Arrayelement: Postfix schiebt den alten Wert per SWAP unter die Adresse,
   Praefix dupliziert die Adresse und laedt den neuen Wert am Ende neu. */
static void tcMemberIncDec(const char* start, const char* end, int isDec, int isPre) {
	const char* ne = tcWordEnd(start, end);
	const char* fs; const char* fe;
	int slot, global = -1, viaPtr, sid, fi;
	TCType bt, ft; char tag;
	if (ne >= end) { tcErrAt(start); fprintf(stderr, "bad member increment\n"); actionErrors++; tcTypePush(tcBadType()); return; }
	viaPtr = (*ne == '-');
	fs = ne + (viaPtr ? 2 : 1);
	fe = tcWordEnd(fs, end);
	slot = tcLookupLocal(start, ne);
	if (slot < 0) global = tcLookupGlobal(start, ne);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(ne - start), start); actionErrors++; tcTypePush(tcBadType()); return; }
	bt = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (viaPtr) {
		if (!tcIsPointer(bt) || tcPointee(bt).base != 's') { tcErrAt(start); fprintf(stderr, "'->' requires a pointer to struct\n"); actionErrors++; tcTypePush(tcBadType()); return; }
		sid = tcPointee(bt).structId - 1;
	} else {
		if (tcIsPointer(bt) || bt.base != 's') { tcErrAt(start); fprintf(stderr, "'.' requires a struct\n"); actionErrors++; tcTypePush(tcBadType()); return; }
		sid = bt.structId - 1;
	}
	fi = tcLookupStructField(sid, fs, fe);
	if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fe - fs), fs); actionErrors++; tcTypePush(tcBadType()); return; }
	ft = tcStructFieldTypes[sid][fi];
	if (tcIsPointer(ft) || ft.base == 's' || ft.base == 'b') {
		tcErrAt(start); fprintf(stderr, "++/-- on a struct field is only supported for int/unsigned/char in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	/* x.field[i]++ / ++x.field[i].  The index action has already emitted
	   its value; calculate the field base and consume that value with IPADD.
	   Keep the existing address-based choreography below so prefix/postfix
	   preserve C's result value in exactly the same way as scalar fields. */
	if (tcStructFieldArrayLen[sid][fi] > 0) {
		if (fe >= end || *fe != '[') {
			tcErrAt(start); fprintf(stderr, "++/-- requires an indexed struct array field\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
	}
	tag = tcTypeTag(ft);
	{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\n"); }
	if (viaPtr) { if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", tcGlobalNames[global]); }
	else        { if (slot >= 0) {
			/* Struct-Parameter: im Slot steht die Adresse (s. tc_arg/tc_param),
			   also LOADP statt der Adresse DES Slots. */
			if (tcLocalStructByAddr[slot]) printf("LOADP %d\n", slot);
			else printf("PUSHADDR L %d\n", slot);
		} else printf("PUSHADDR G %s\n", tcGlobalNames[global]); }
	printf("IPADD c\n");
	if (tcStructFieldArrayLen[sid][fi] > 0) printf("IPADD %c\n", tag);
	/* DUPP, nicht DUP: hier wird eine ADRESSE vervielfaeltigt. Auf dem 68k
	   sind beide derselbe "move.l (a7),-(a7)", auf ARM64 NICHT -- dort laedt
	   DUP 32 Bit (w0) und DUPP 64 (x0). Mit DUP ging die obere Haelfte der
	   Adresse verloren, und "s.n++" endete nativ im Segfault, waehrend das
	   VM-Orakel gruen blieb (2026-09-16). Beim Index-Zweig darunter bleibt
	   DUP richtig: dort wird ein INDEX vervielfaeltigt, kein Zeiger. */
	if (tcIsDouble(ft)) {
		if (isPre) printf("DUPP\nDUPP\nLOADIND d\n%s\n%s\nSTOREIND d\nLOADIND d\n", TC_DBL_ONE, isDec ? "DSUB" : "DADD");
		else       printf("DUPP\nLOADIND d\nDSWAP\nDUPP\nLOADIND d\n%s\n%s\nSTOREIND d\n", TC_DBL_ONE, isDec ? "DSUB" : "DADD");
	}
	else if (isPre) printf("DUPP\nDUPP\nLOADIND %c\nPUSH 1\n%s\nSTOREIND %c\nLOADIND %c\n", tag, isDec ? "SUB" : "ADD", tag, tag);
	else       printf("DUPP\nLOADIND %c\nSWAP\nDUPP\nLOADIND %c\nPUSH 1\n%s\nSTOREIND %c\n", tag, tag, isDec ? "SUB" : "ADD", tag);
	tcTypePush(ft);
}
static void tcIncDecEmit(int slot, int global, TCType t, int isDec, int isPre) {
	char tag = tcTypeTag(t);
	const char* loadOp;
	const char* storeOp;
	const char* ld;
	const char* st;
	char ptag;
	if (tcIsPointer(t)) {
		/* Zeiger-Inkrement rechnet in ELEMENTEN, nicht in Bytes -- deshalb IPADD
		   mit dem Tag des ZIELtyps statt eines schlichten ADD. IPADD erwartet den
		   Zaehler UNTEN und den Zeiger OBEN (siehe docs/IR_OPCODES.md), weshalb
		   der Zeiger beim Postfix zweimal geladen statt per DUP vervielfaeltigt
		   wird: ein DUP laege an der falschen Stelle relativ zum Zaehler. */
		ld = slot >= 0 ? "LOADP" : "LOADGP";
		st = slot >= 0 ? "STOREP" : "STOREGP";
		ptag = tcTypeTag(tcPointee(t));
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
	if (tcIsDouble(t)) {
		/* Ein double liegt als BLOCK, nicht im Slot -- also LOADD/STORED
		   bzw. LOADGD/STOREGD, DDUP statt DUP und DADD/DSUB statt ADD/SUB.
		   Vorher fiel 'd' in den char-Auffangzweig unten (LOADC/STOREC,
		   "maskiert auf 0xff") und das Inkrement blieb wirkungslos.
		   Die 1.0 steht als IEEE-754-Bitmuster (0x3FF0000000000000, hi zuerst):
		   PUSHD nimmt zwei 32-Bit-Haelften, weil die IR keinen
		   Gleitkommatext traegt -- sie wird von Werkzeugen gelesen, die
		   selbst kein Gleitkomma haben. */
		ld = slot >= 0 ? "LOADD" : "LOADGD";
		st = slot >= 0 ? "STORED" : "STOREGD";
		if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
		/* Praefix liefert den NEUEN Wert, Postfix den alten -- der Unterschied
		   ist allein, ob das DDUP vor oder nach der Addition steht. */
		if (isPre) printf("PUSHD 1072693248 0\n%s\nDDUP\n", isDec ? "DSUB" : "DADD");
		else       printf("DDUP\nPUSHD 1072693248 0\n%s\n", isDec ? "DSUB" : "DADD");
		if (slot >= 0) printf("%s %d\n", st, slot); else printf("%s %s\n", st, tcGlobalNames[global]);
		tcTypePush(t);
		return;
	}
	/* 2026-09-09, short dazu: bei genau EINEM Byte-Tag half die alte
	   Zwei-Wege-Ternaerkette (LOADC deckte 'c' UND 'b' ab); mit 'h' als
	   dritter Groesse jetzt ein echtes if/else statt einer dritten
	   verschachtelten Ternaerstufe (bleibt lesbar). */
	if (slot >= 0) {
		if (tag == 'i')      { loadOp = "LOADL";  storeOp = "STOREL"; }
		else if (tag == 'h') { loadOp = "LOADLH"; storeOp = "STORELH"; }
		else                 { loadOp = "LOADC";  storeOp = "STOREC"; }
	} else {
		if (tag == 'i')      { loadOp = "LOADG";  storeOp = "STOREG"; }
		else if (tag == 'h') { loadOp = "LOADGH"; storeOp = "STOREGH"; }
		else                 { loadOp = "LOADGC"; storeOp = "STOREGC"; }
	}
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
	/* void*: generischer Objektzeiger. ISO C erlaubt die implizite Umwandlung
	   in BEIDE Richtungen auch fuer mehrstufige Zieltypen: malloc() liefert
	   void* und darf deshalb beispielsweise char** oder T** initialisieren. */
	if (tcIsPointer(wanted) && tcIsPointer(got) &&
	    (wanted.base == 'v' || got.base == 'v')) return 1;
	if (tcIsPointer(wanted)) return !tcIsPointer(got) && got.base == 'z';
	/* bool ist in C ein Ganzzahltyp (Wert 0/1) -- "int x = (a == b);" und
	   "return (a && b);" aus einer int-Funktion sind legal. */
	if (tcIsInteger(wanted) && tcIsBool(got)) return 1;
	return tcIsInteger(wanted) && tcIsInteger(got);
}
/* IMPLIZITE KONVERSION BEI ZUWEISUNG (2026-09-16). C89 3.1.2.5: eine
   Zuweisung wandelt den Wert in den Typ des Ziels um. Zwischen double und
   den ganzzahligen Typen heisst das I2D oder D2I -- und zwar EMITTIERT,
   nicht bloss im Typstapel vermerkt.
   Bewusst NICHT ueber tcCompatible geloest: dieselbe Vertraeglichkeit gilt
   sonst auch fuer Kettenzuweisung, Argumente und Rueckgaben, und dort
   wuerde ohne Emission still der falsche Wert landen. Lieber hier gezielt
   umwandeln und dort weiter melden.
   Rueckgabe: der Typ, der jetzt auf dem Stapel liegt. */
static TCType tcCoerceToTarget(TCType wanted, TCType got) {
	if (tcIsDouble(wanted) && (tcIsInteger(got) || tcIsBool(got))) {
		printf("I2D\n");
		return wanted;
	}
	if ((tcIsInteger(wanted) || tcIsBool(wanted)) && tcIsDouble(got)) {
		printf("D2I\n");
		/* Die Zielverengung auf char/short macht der Store selbst nicht --
		   dieselbe Regel wie beim Cast. */
		if (wanted.base == 'c') printf("NARROWC\n");
		else if (wanted.base == 'h') printf("NARROWH\n");
		else if (wanted.base == 'b') printf("PUSH 0\nCMPNE\n");
		return wanted;
	}
	return got;
}

static void tcPrintType(FILE* out, TCType t) {
	int i; const char* name;
	/* 2026-09-01: 's' (struct) und 'F' (Funktionszeiger) fehlten hier und kamen
	   deshalb als "?" heraus -- gerade in Cast- und Zuweisungsdiagnosen, wo der
	   Typ die eigentliche Aussage ist. Der Strukturname steht in tcStructNames,
	   structId ist 1-basiert (0 = unbesetzt). */
	if (t.base == 's' && t.structId > 0 && t.structId <= MAX_STRUCTS) {
		fputs("struct ", out); fputs(tcStructNames[t.structId - 1], out);
		for (i = 0; i < t.pointers; i++) fputc('*', out);
		return;
	}
	if (t.base == 'F') {
		fputs("function pointer", out);
		return;
	}
	name = t.base == 'u' ? "unsigned int" : t.base == 'c' ? "char" : t.base == 'h' ? "short" : t.base == 'b' ? "bool" : t.base == 'z' ? "null" : t.base == 'i' ? "int" : t.base == 'v' ? "void" : t.base == 'd' ? "double" : t.base == 's' ? "struct" : "?";
	fputs(name, out); for (i = 0; i < t.pointers; i++) fputc('*', out);
}
static void tcTypeError(const char* what, TCType wanted, TCType got) {
	tcErrAt(parserActionAt); fprintf(stderr, "%s expects ", what); tcPrintType(stderr, wanted);
	fputs(", got ", stderr); tcPrintType(stderr, got); fputc('\n', stderr); actionErrors++;
}
static void tcLogicBegin(char kind) {
	TCType left; int branch, end;
	tcTypePop4(&left);
	if (!tcIsTruthy(left)) tcTypeError("logical operator", tcMakeType('b', 0), left);
	if (tcLogicDepth >= 64) { tcErrAt(parserActionAt); fprintf(stderr, "logical nesting too deep\n"); actionErrors++; return; }
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
	const char* p; int round = 0, square = 0; char quote = 0;
	for (p = s; p < e; p++) {
		if (quote) {
			if (*p == '\\' && p + 1 < e) { p++; continue; }
			if (*p == quote) quote = 0;
			continue;
		}
	/* Zahlen statt eines escaped Apostrophs: QCCs Bootstrap-Scanner kennt
	   noch keine Zeichenkonstante '\\''. */
	if (*p == 39 || *p == 34) { quote = *p; continue; }
		if (*p == '(') round++;
		else if (*p == ')') round--;
		else if (*p == '[') square++;
		else if (*p == ']') square--;
		else if (!round && !square && p + 1 < e && p[0] == a && p[1] == b) return 1;
	}
	return 0;
}
static int tcHasTopChar(const char* s, const char* e, char c) {
	const char* p; int round = 0, square = 0; char quote = 0;
	for (p = s; p < e; p++) {
		if (quote) {
			if (*p == '\\' && p + 1 < e) { p++; continue; }
			if (*p == quote) quote = 0;
			continue;
		}
	if (*p == 39 || *p == 34) { quote = *p; continue; }
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
	if (tcLogicDepth <= 0 || tcLogicKind[tcLogicDepth - 1] != kind) { tcErrAt(parserActionAt); fprintf(stderr, "logical-frame mismatch\n"); actionErrors++; return; }
	tcTypePop4(&right); if (!tcIsTruthy(right)) tcTypeError("logical operator", tcMakeType('b', 0), right);
	frame = --tcLogicDepth;
	printf("%s L%d\nPUSH %d\nJMP L%d\nLABEL L%d\nPUSH %d\nLABEL L%d\n",
		kind == '&' ? "JZ" : "JNZ", tcLogicBranch[frame], kind == '&' ? 1 : 0,
		tcLogicDone[frame], tcLogicBranch[frame], kind == '&' ? 0 : 1, tcLogicDone[frame]);
	tcTypePush4('b', 0, 0, 0);
}
static void tcBitBegin(char kind) {
	if (tcBitDepth >= 64) { tcErrAt(parserActionAt); fprintf(stderr, "bitwise nesting too deep\n"); actionErrors++; return; }
	tcBitKind[tcBitDepth++] = kind;
}
static void tcBitEnd(char kind) {
	TCType right, left;
	if (tcBitDepth <= 0 || tcBitKind[tcBitDepth - 1] != kind) { tcErrAt(parserActionAt); fprintf(stderr, "bitwise-frame mismatch\n"); actionErrors++; return; }
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
	if (tcTernaryDepth >= 64) { tcErrAt(parserActionAt); fprintf(stderr, "conditional nesting too deep\n"); actionErrors++; return; }
	falseLabel = tcNextLabel++; endLabel = tcNextLabel++;
	tcTernaryFalse[tcTernaryDepth] = falseLabel;
	tcTernaryDone[tcTernaryDepth] = endLabel;
	tcTernSavedAdd[tcTernaryDepth] = tcPendingAdd;
	tcTernSavedMul[tcTernaryDepth] = tcPendingMul;
	tcTernSavedRel0[tcTernaryDepth] = tcRel0;
	tcTernSavedRel1[tcTernaryDepth] = tcRel1;
	tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0;
	tcTernaryDepth++;
	printf("JZ L%d\n", falseLabel);
}
static void tcTernaryMiddle(void) {
	if (tcTernaryDepth <= 0) { tcErrAt(parserActionAt); fprintf(stderr, "conditional-frame mismatch\n"); actionErrors++; return; }
	tcTernaryTrueType[tcTernaryDepth - 1] = tcTypePop();
	printf("JMP L%d\nLABEL L%d\n", tcTernaryDone[tcTernaryDepth - 1], tcTernaryFalse[tcTernaryDepth - 1]);
}
static void tcTernaryEnd(void) {
	TCType falseType, trueType, result;
	if (tcTernaryDepth <= 0) { tcErrAt(parserActionAt); fprintf(stderr, "conditional-frame mismatch\n"); actionErrors++; return; }
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
	tcPendingAdd = tcTernSavedAdd[tcTernaryDepth];
	tcPendingMul = tcTernSavedMul[tcTernaryDepth];
	tcRel0 = tcTernSavedRel0[tcTernaryDepth];
	tcRel1 = tcTernSavedRel1[tcTernaryDepth];
}
static void tcLoadTarget(void) {
	char tag = tcTypeTag(tcTargetType);
	if (tcTargetIndirect) printf("DUPP\nLOADIND %c\n", tag);
	else if (tcTargetIsGlobal && tcTargetIsArray) printf("DUP\nLOADIDX G %s %c\n", tcTargetGlobal, tag);
	else if (tcTargetIsGlobal) printf("LOADG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffix(tag), tcTargetGlobal);
	else if (tcTargetSlot >= 0 && tcTargetIsArray) printf("DUP\nLOADIDX L %d %c\n", tcTargetSlot, tag);
	else if (tcTargetSlot >= 0) printf("LOAD%s %d\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffixL(tag), tcTargetSlot);
	else { tcErrAt(parserActionAt); fprintf(stderr, "unknown assignment target\n"); actionErrors++; return; }
	tcTypePush(tcTargetType);
}
static void tcCompoundAssign(void) {
	TCType right = tcTypePop(), left = tcTypePop();
	if (tcIsPointer(left)) {
		if ((tcAssignOp[0] != '+' && tcAssignOp[0] != '-') || !tcIsInteger(right)) tcTypeError("pointer compound assignment", tcMakeType('i', 0), right);
		else printf("P%s %c\n", tcAssignOp[0] == '+' ? "ADD" : "SUB", tcTypeTag(tcPointee(left)));
		tcTypePush(left); return;
	}
	/* GLEITKOMMA (2026-09-16). C89 3.3.16.2: "E1 op= E2" wirkt wie
	   "E1 = E1 op E2" -- es greifen also die ueblichen arithmetischen
	   Konversionen, und das Ergebnis wandert beim Speichern in den Zieltyp
	   zurueck (das erledigt tcCoerceToTarget, dieselbe Funktion wie bei der
	   einfachen Zuweisung, samt Verengung auf char/short).
	   WELCHER Konversionsopcode noetig ist, haengt an der STAPELLAGE: der
	   linke Operand liegt beim Emittieren schon UNTER dem rechten, also
	   I2DUNDER fuer links und I2D fuer rechts -- dieselbe Unterscheidung
	   wie bei den gemischten Ausdruecken ("a + 1" gegen "1 + a").
	   Nur die vier Grundrechenarten sind auf double definiert: % verlangt in
	   C ganzzahlige Operanden (C89 3.3.5), ebenso die Bitoperatoren und die
	   Schiebeoperatoren (3.3.7 ff). */
	if (tcIsDouble(left) || tcIsDouble(right)) {
		char op = tcAssignOp[0];
		if (!tcIsDouble(left) && !tcIsInteger(left) && !tcIsBool(left)) {
			tcTypeError("compound assignment", tcMakeType('d', 0), left);
			tcTypePush(tcTargetType); return;
		}
		if (!tcIsDouble(right) && !tcIsInteger(right) && !tcIsBool(right)) {
			tcTypeError("compound assignment", tcMakeType('d', 0), right);
			tcTypePush(tcTargetType); return;
		}
		if (op != '+' && op != '-' && op != '*' && op != '/') {
			tcErrAt(parserActionAt);
			fprintf(stderr, "'%s' requires integer operands, got double\n", tcAssignOp);
			actionErrors++; tcTypePush(tcTargetType); return;
		}
		if (!tcIsDouble(left)) printf("I2DUNDER\n");
		if (!tcIsDouble(right)) printf("I2D\n");
		if (op == '+') printf("DADD\n");
		else if (op == '-') printf("DSUB\n");
		else if (op == '*') printf("DMUL\n");
		else printf("DDIV\n");
		tcTypePush(tcCoerceToTarget(tcTargetType, tcMakeType('d', 0)));
		return;
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
/* Wertet eine Arraygroesse aus dem Rohtext aus: Zahlen, verknuepft mit
   + - * (linksassoziativ, ohne Vorrang -- fuer Groessenangaben wie
   "64 + 40" oder "8 * 4" voellig ausreichend). Liefert den Wert und setzt
   *pp hinter den Ausdruck. */
static int tcConstArrayLen(const char** pp, const char* end) {
	const char* p = *pp;
	int v = 0, digits = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); digits = 1; }
	if (!digits) { *pp = p; return 0; }
	for (;;) {
		const char* q = p;
		int rhs = 0, rdig = 0; char op;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		if (q >= end || (*q != '+' && *q != '-' && *q != '*')) break;
		op = *q++;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		while (q < end && *q >= '0' && *q <= '9') { rhs = rhs * 10 + (*q++ - '0'); rdig = 1; }
		if (!rdig) break;
		if (op == '+') v += rhs; else if (op == '-') v -= rhs; else v *= rhs;
		p = q;
	}
	*pp = p;
	return v;
}
/* ZIELBEZOGEN NORMALISIEREN (2026-09-16). "int" ist auf beiden Zielen
   32 Bit breit, "long" im Compiler selbst aber nicht: auf dem Mac 64, auf
   dem 68030 32. Ohne die Klammer unten erzeugte derselbe Quelltext zwei
   verschiedene IR-TEXTE -- am Selbsthost-Lauf gemessen, als der neue
   Gleitkomma-Umrechner das erste 0x80000000UL in den Compiler brachte:
   Host "PUSH 2147483648", Ziel "PUSH -2147483648". Dasselbe Bitmuster,
   zwei Schreibweisen, und der Fixpunktvergleich war dahin.

   Geschrieben mit HEXADEZIMALEN Literalen und unsigned long: xcc lehnt
   "2147483647L" als "integer constant is too large" ab (gemessen), und auf
   einem 32-Bit-long duerfen auch die Zwischenwerte nicht ueberlaufen.
   Dort ist die Maskierung wirkungslos, auf dem 64-Bit-Host schneidet sie
   die oberen Bits weg und zieht das Vorzeichen nach. */
static long tcNarrowTo32(long v) {
	unsigned long u;
	unsigned long neg;

	u = (unsigned long)v & 0xFFFFFFFFUL;
	if ((u & 0x80000000UL) != 0UL) {
		/* Der Betrag von -2^31 passt selbst nicht mehr in ein signed long,
		   deshalb eins weniger negieren und danach abziehen. */
		neg = 0xFFFFFFFFUL - u;
		return -(long)neg - 1L;
	}
	return (long)u;
}

static long tcNum(const char* s, const char* e) {
	long v = 0; const char* q; const char* limit = e;
	/* Integer suffixes affect the C type, not the numeric value. */
	while (limit > s && (limit[-1] == 'u' || limit[-1] == 'U' || limit[-1] == 'l' || limit[-1] == 'L')) limit--;
	/* Hexform "0x.."/"0X.." (siehe hexNumber in der Grammatik). */
	if (limit - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		for (q = s + 2; q < limit; q++) {
			int d;
			if (*q >= '0' && *q <= '9') d = *q - '0';
			else if (*q >= 'a' && *q <= 'f') d = *q - 'a' + 10;
			else if (*q >= 'A' && *q <= 'F') d = *q - 'A' + 10;
			else break;
			v = v * 16 + d;
		}
		return tcNarrowTo32(v);
	}
	for (q = s; q < limit; q++) v = v * 10 + (*q - '0');
	return tcNarrowTo32(v);
}
static const char* tcNameEnd(const char* s, const char* e) {
	const char* p = s;
	/* auch am "-" anhalten: "p->f" (das "-" kann hier nur der Pfeil sein --
	   ein Minus innerhalb eines Index steht hinter "[", wo schon abgebrochen wird).
	   Und am Leerraum anhalten (2026-09-09): ISO C erlaubt Leerraum zwischen
	   JEDEM Token, auch "s . a"/"s. a"/"a [0]" -- ohne diesen Stopp haette die
	   Namensspanne den Leerraum mitgeschleppt und die Variable waere unter dem
	   falschen (leerraumbehafteten) Namen gesucht worden. Das eigentliche
	   Ueberspringen bis zum naechsten Zeichen macht tcSkipWs() an den
	   Aufrufstellen, die WISSEN wollen was NACH dem Namen kommt. */
	while (p < e && *p != '[' && *p != '.' && *p != '-' &&
	       *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
	return p;
}
/* Leerraum zwischen zwei Token ist in C stets bedeutungslos (s. tcNameEnd oben).
   Wird nach tcNameEnd() gebraucht, um das Zeichen NACH einem Namen zu pruefen
   (".", "[", "->"), und nach einem "."/"->"/Index-Ende, um den Feldnamen bzw.
   die naechste Klammer zu finden. */
static const char* tcSkipWs(const char* p, const char* e) {
	while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	return p;
}
/* VERKETTETER MEMBER-ZUGRIFF (2026-09-16): "p->n->v", "a.n->v".
   Emittiert die ganze Kette selbst, statt sich in die Verzweigung von
   tc_varref einzuhaengen -- die ist die verwinkeltste Routine des Frontends,
   und ein Eingriff dort hatte beim ersten Versuch still die ADRESSE statt
   des Wertes geliefert.
   Das Muster je Stufe ist dasselbe wie beim einstufigen Zugriff:

     PUSH <offset>   <Basis>   IPADD c        -- Adresse des Feldes
     LOADIND p                                -- Zwischenstufe: Zeiger laden
     PUSH <offset>   PADD c                   -- naechstes Feld
     LOADIND <tag>                            -- letzte Stufe: Wert

   IPADD erwartet den Zaehler UNTEN und den Zeiger OBEN, PADD umgekehrt --
   deshalb der Wechsel nach der ersten Stufe.
   JEDE ZWISCHENSTUFE MUSS EIN ZEIGER AUF STRUCT SEIN. Ein struct-Feld IN
   einem struct gibt es in QCC nicht, ein Nicht-Zeiger koennte also nur ein
   Skalar sein -- und den kann man nicht weiter aufloesen. Wird gemeldet.
   Rueckgabe 0 = nicht zustaendig, der Aufrufer meldet dann selbst. */
/* Typ des LETZTEN Feldes einer Kette -- die Zielseite braucht ihn, nachdem
   tcEmitMemberChain nur die Adresse gelegt hat. */
static TCType tcChainFieldType;
static int tcEmitMemberChain(const char* start, const char* end, int slot, int wantAddr) {
	const char* p = tcNameEnd(start, end);
	int global = -1;
	int sid;
	int first = 1;
	TCType base;
	TCType cur;
	if (slot < 0) global = tcLookupGlobal(start, tcNameEnd(start, end));
	if (slot < 0 && global < 0) return 0;
	base = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	/* Basis: entweder eine struct-Variable oder ein Zeiger darauf. */
	if (base.pointers > 1) return 0;
	cur = base;
	if (cur.pointers) cur.pointers--;
	if (cur.base != 's') return 0;
	sid = cur.structId - 1;
	if (sid < 0 || sid >= tcStructCount) return 0;
	for (;;) {
		const char* fs; const char* fe; int fi; int viaPtr;
		TCType ft;
		p = tcSkipWs(p, end);
		if (p >= end) break;
		if (*p == '.') { viaPtr = 0; p++; }
		else if (*p == '-' && p + 1 < end && p[1] == '>') { viaPtr = 1; p += 2; }
		else return 0;                      /* Index o.ae. -- nicht zustaendig */
		p = tcSkipWs(p, end);
		fs = p; fe = tcNameEnd(fs, end);
		if (fe == fs) return 0;
		p = fe;                              /* hinter den Feldnamen ruecken */
		fi = tcLookupStructField(sid, fs, fe);
		if (fi < 0) {
			tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fe - fs), fs);
			actionErrors++; tcTypePush(tcBadType()); return 1;
		}
		ft = tcStructFieldTypes[sid][fi];
		if (first) {
			/* Erste Stufe: Offset, dann die Basisadresse, dann IPADD. */
			{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\n"); }
			if (base.pointers) {
				if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", tcGlobalNames[global]);
			} else {
				if (slot >= 0) printf("PUSHADDR L %d\n", slot); else printf("PUSHADDR G %s\n", tcGlobalNames[global]);
			}
			printf("IPADD c\n");
			first = 0;
		} else {
			/* Folgestufe: der Zeiger liegt schon oben, also PADD. */
			{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\n"); }
			printf("PADD c\n");
		}
		p = tcSkipWs(p, end);
		if (p >= end) {
			/* letzte Stufe -- den WERT laden */
			if (ft.base == 's' && !ft.pointers) {
				tcErrAt(start); fprintf(stderr, "a whole struct field at the end of a chain is not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return 1;
			}
			tcChainFieldType = ft;
			/* wantAddr: die ZIELSEITE will die Adresse stehen lassen und
			   selbst speichern (tcTargetIndirect), nicht den Wert laden. */
			if (!wantAddr) {
				printf("LOADIND %c\n", tcTypeTag(ft));
				tcTypePush(ft);
			}
			return 1;
		}
		/* Zwischenstufe: das Feld muss ein Zeiger auf struct sein. */
		if (!ft.pointers || ft.base != 's') {
			tcErrAt(start);
			fprintf(stderr, "chained member access requires a pointer to struct at each step\n");
			actionErrors++; tcTypePush(tcBadType()); return 1;
		}
		if (!viaPtr && base.pointers == 0 && 0) { /* Platzhalter, s. Kommentar oben */ }
		printf("LOADIND p\n");
		cur = ft; cur.pointers--;
		sid = cur.structId - 1;
		if (sid < 0 || sid >= tcStructCount) return 0;
	}
	return 0;
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
		tcErrAt(parserActionAt); fprintf(stderr, "constant array index %d out of range (length %d)\n", index, length);
		actionErrors++;
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
/* Wie tcSkipOneIndex, aber bis hinter die letzte direkt folgende Klammer.
   Ein struct-Array darf mehrdimensional sein: bei a[i][j].field muss die
   Feld-Logik deshalb das '.' HINTER beiden Indizes finden. */
static const char* tcSkipAllIndexes(const char* p, const char* end) {
	while (p < end && *p == '[') p = tcSkipOneIndex(p, end);
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
   -- ein Kompilierfehler ist ohnehin schon gemeldet). Ein echter Teilzugriff
   arr[i] bzw. arr[i][j] liefert jetzt korrekt einen Zeiger auf die verbleibende
   Zeile/Teilmatrix; nur zu viele oder gar keine Indizes bleiben Fehler. */
/* Liefert 1 fuer ein Element, 2 fuer einen Zeiger auf das verbleibende
   Teilarray und 0 bei einem ungueltigen Zugriff. Bei Teilindizierung wird
   der Offset bereits auf die Basiselemente abgeflacht. Beispiel m[2][3][4]:
   m[i] -> i*3*4, m[i][j] -> i*3+j, jeweils als int*. */
static int tcCheckNDIndex(int ndims, const int* trailingDims, int idxCount) {
	char name[24]; int lvl;
	if (idxCount == ndims) { if (ndims > 1) tcEmitNDCombine(ndims, trailingDims); return 1; }
	if (idxCount > 0 && idxCount < ndims) {
		/* Wie tcEmitNDCombine, aber nur die vorhandenen Indizes zusammenfassen. */
		for (lvl = idxCount; lvl >= 2; lvl--) {
			sprintf(name, "__idxNd_%d", lvl);
			if (!tcIdxNDScratchDeclared[lvl]) { printf("GLOBAL %s 0 i 1\n", name); tcIdxNDScratchDeclared[lvl] = 1; }
			printf("STOREG %s\n", name);
		}
		for (lvl = 2; lvl <= idxCount; lvl++) {
			sprintf(name, "__idxNd_%d", lvl);
			printf("PUSH %d\nMUL\nLOADG %s\nADD\n", trailingDims[lvl - 2], name);
		}
		for (lvl = idxCount; lvl < ndims; lvl++) printf("PUSH %d\nMUL\n", trailingDims[lvl - 1]);
		return 2;
	}
	if (ndims == 1 && idxCount == 2) { tcErrAt(parserActionAt); fprintf(stderr, "array is not two-dimensional\n"); actionErrors++; return 0; }
	tcErrAt(parserActionAt); fprintf(stderr, "array has %d dimension(s), but %d index(es) were given\n", ndims, idxCount);
	actionErrors++; return 0;
}
/* p[i][j] fuer einen Pointer p: die Index-Aktionen haben die Werte bereits
   als [i,j] auf den Stack gelegt. Fuer jeden weiteren Index wird der oberste
   Wert kurz gesichert, damit zuerst p+i geladen und danach (p[i])+j berechnet
   werden kann. PTRINDEX erwartet dabei den Zeiger oben auf dem Stack. */
static TCType tcEmitPointerIndexChain(int slot, const char* globalName, char base, int pointers, int structId, int pointeeConst, int idxCount) {
	char name[24]; int level; TCType pointer = tcMakeType(base, pointers);
	pointer.structId = (unsigned char)structId;
	pointer.pointeeConst = (unsigned char)pointeeConst;
	for (level = idxCount; level >= 2; level--) {
		sprintf(name, "__ptrIdx_%d", level);
		if (!tcPtrIdxScratchDeclared[level]) { printf("GLOBAL %s 0 i 1\n", name); tcPtrIdxScratchDeclared[level] = 1; }
		printf("STOREG %s\n", name);
	}
	if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", globalName);
	for (level = 1; level <= idxCount; level++) {
		TCType value = tcPointee(pointer);
		if (!tcIsPointer(pointer)) { tcErrAt(parserActionAt); fprintf(stderr, "too many pointer indexes\n"); actionErrors++; return tcBadType(); }
		if (!tcIsPointer(value) && value.base == 'v') { tcErrAt(parserActionAt); fprintf(stderr, "cannot dereference void*\n"); actionErrors++; return tcMakeType('i', 0); }
		if (level > 1) {
			sprintf(name, "__ptrIdx_%d", level);
			printf("LOADG %s\nSWAP\n", name);
		}
		if (value.base == 's' && !value.pointers) {
			/* EINE STRUCT IST KEIN REGISTERWERT: "p[i]" liefert ihre
			   ADRESSE, und der Schritt geht um ihre BYTE-Groesse.
			   Vorher stand hier PTRINDEX/LOADIND mit dem Typtag, und
			   tcTypeTag('s') gibt 'i' -- also vier Byte Schrittweite UND
			   ein Ladebefehl, der vier Byte der Struct als Zahl liest und
			   sie als Quelladresse weitergibt. Auf dem 68k landet damit
			   ein Datenwert in A0 (2026-09-07 gefunden).
			   Fuer den ARRAY-Fall "v = arr[i]" ist dieselbe Bauform am
			   2026-09-01 repariert worden; der Zeigerfall blieb stehen.
			   Eine Struct kann nur die LETZTE Ebene sein -- struct-in-
			   struct ist abgelehnt --, der naechste Durchlauf faellt
			   deshalb in "too many pointer indexes". */
			{ printf("IPADDN "); tcEmitStructSize(value.structId - 1); printf("\n"); }
		} else {
			printf("PTRINDEX %c\nLOADIND %c\n", tcTypeTag(value), tcTypeTag(value));
		}
		pointer = value;
	}
	return pointer;
}
/* Vorwaerts: der Generator gibt die ROUTINE-Bloecke spaeter aus als diese
   Hilfsfunktion, die sie braucht. */
static int tcEmitStringGlobal(const char* start, const char* end);
/* syms[i] ist -1 fuer einen Zahlenwert und sonst die Nummer des anonymen
   __strN, dessen ADRESSE an dieser Stelle stehen soll (2026-09-15). */
static int tcInitList(const char* start, const char* end, long* values, int* syms, int cap, int rowLen) {
	const char* p = start;
	int count = 0;
	int depth = 0;
	int rowStart = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p == end || *p != '{') return -1;
	p++;
	depth = 1;
	for (;;) {
		int neg = 0; long value = 0; int digits = 0; int hd = 0;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p >= end) return -2;
		if (*p == ',') { p++; continue; }
		/* VERSCHACHTELTE ZEILE (2026-09-15): "{{1,2},{3,4}}". Das Array ist
		   intern flach, die geschweiften Klammern gliedern es nur -- deshalb
		   wird abgeflacht. NAIVES Abflachen waere aber falsch, sobald eine
		   Zeile KUERZER ist als die Zeilenlaenge: "int m[2][3] = {{1},{2}}"
		   heisst in C m[0][0]=1 und m[1][0]=2, nicht m[0][0]=1, m[0][1]=2.
		   Deshalb wird am Zeilenende auf die Zeilenlaenge aufgefuellt. */
		if (*p == '{') {
			if (depth >= 2) return -4;
			if (rowLen <= 1) return -4;
			depth++;
			p++;
			rowStart = count;
			continue;
		}
		if (*p == '}') {
			p++;
			if (depth == 2) {
				if (count - rowStart > rowLen) return -2;
				while (count - rowStart < rowLen) {
					if (count >= cap) return -2;
					if (syms) syms[count] = -1;
					values[count++] = 0;
				}
				depth--;
				continue;
			}
			return count;
		}
		if (*p == '"') {
			const char* q = p + 1;
			while (q < end && *q != '"') { if (*q == '\\' && q + 1 < end) q++; q++; }
			if (q >= end || count >= cap) return -2;
			if (syms == 0) return -3;
			syms[count] = tcEmitStringGlobal(p, q + 1);
			values[count++] = 0;
			p = q + 1;
			continue;
		}
		if (*p == '-') { neg = 1; p++; }
		/* Hexzahl wie im Ausdruck: "0x7FF00000UL". Ohne diesen Zweig
		   blieb digits null und die ganze Liste scheiterte still. */
		if (p + 1 < end && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
			p += 2;
			while (p < end) {
				if (*p >= '0' && *p <= '9') hd = *p - '0';
				else if (*p >= 'a' && *p <= 'f') hd = *p - 'a' + 10;
				else if (*p >= 'A' && *p <= 'F') hd = *p - 'A' + 10;
				else break;
				value = value * 16 + hd;
				digits = 1;
				p++;
			}
		} else {
			while (p < end && *p >= '0' && *p <= '9') { value = value * 10 + (*p++ - '0'); digits = 1; }
		}
		/* Ganzzahlsuffixe gehoeren zum Token, nicht zum naechsten Wert. */
		while (p < end && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L')) p++;
		if (!digits || count >= cap) return -2;
		if (syms) syms[count] = -1;
		values[count++] = neg ? -value : value;
	}
}
void tc_defname(const char* start, const char* end) {
	int i;
	tcLocalCount = 0;
	/* Slots werden pro Funktion neu von 0 gezaehlt -- die Totmarkierungen der
	   VORIGEN Funktion muessen deshalb hier fallen (siehe tcScopeMark). */
	for (i = 0; i < MAX_LOCALS; i++) tcLocalDead[i] = 0;
	tcScopeDepth = 0;
	tcGotoCount = 0;          /* Sprungmarken sind funktionslokal (wie in echtem C) */
	tcFuncType.base = tcCurrentType.base;
	tcFuncType.pointers = tcCurrentType.pointers;
	tcFuncType.structId = tcCurrentType.structId;
	tcFuncType.pointeeConst = tcCurrentType.pointeeConst;
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

void tc_fieldconst(const char* start, const char* end) {
	(void)start; (void)end;
	tcFieldConst = 1;
}

void tc_fieldconstend(const char* start, const char* end) {
	/* Nach der ganzen Zeile, nicht nach jedem Deklarator: sonst waere bei
	   "const int a, b;" nur a konstant. */
	(void)start; (void)end;
	tcFieldConst = 0;
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
	else { tcErrAt(start); fprintf(stderr, "too many extern parameters\n"); actionErrors++; }
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
	if (tcLookupFunction(tcExternName) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate function '%s'\n", tcExternName); actionErrors++; return; }
	if (tcFunctionCount >= MAX_FUNCTIONS) { tcErrAt(start); fprintf(stderr, "too many functions\n"); actionErrors++; return; }
	i = tcFunctionCount++;
	tcCopy(tcFunctionNames[i], tcExternName, tcExternName + strlen(tcExternName));
	tcFunctionReturnTypes[i] = tcExternReturnType;
	tcFunctionNargs[i] = tcExternBuildParamCount;
	{ int p; for (p = 0; p < tcExternBuildParamCount; p++) tcFunctionParamTypes[i][p] = tcExternBuildParamTypes[p]; }
	tcFunctionIsExternal[i] = 1;
	tcFunctionIsVariadic[i] = tcExternIsVariadic;
}

static void tc_setcurrenttype(int base, int pointers) {
	/* Keine TCType-Rueckgabe: die 68k-Selbsthost-ABI kann einen 4-Byte-Struct-
	   Rueckgabewert nicht verlaesslich transportieren. */
	tcCurrentType.base = (unsigned char)base;
	tcCurrentType.pointers = (unsigned char)pointers;
	tcCurrentType.structId = 0;
	tcCurrentType.pointeeConst = 0;
}

void tc_type(const char* start, const char* end) {
	const char* we = tcWordEnd(start, end);
	/* Basis fuer tc_pointerdecl (siehe tcBasePointers-Deklaration): 0 fuer
	   jeden eingebauten Basistyp, ueberschrieben unten im typedef-Zweig, falls
	   der typedef selbst schon ein Zeigertyp ist. */
	tcBasePointers = 0;
	if (tcEqSpan(start, we, "unsigned")) {
		/* "unsigned char" -> QCCs char (der 68k-Codegen laedt char nullerweitert,
		   verhaelt sich also schon vorzeichenlos); "unsigned short" (2026-09-09)
		   analog -> 'h' (short wird ebenso nullerweitert geladen, s. unten);
		   "unsigned int"/"unsigned long" -> 'u'. Dafuer muss das ZWEITE Wort
		   geprueft werden, tcWordEnd liefert nur das erste. */
		const char* q = we; const char* qe;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) TC_SET_CURRENT('c', 0);
		else if (tcEqSpan(q, qe, "short")) TC_SET_CURRENT('h', 0);
		else TC_SET_CURRENT('u', 0);
	}
	else if (tcEqSpan(start, we, "signed")) {
		/* "signed" (2026-09-16): dieselbe Folgewort-Pruefung wie bei
		   "unsigned", nur bleibt der Typ vorzeichenbehaftet. "signed" allein
		   ist in C ein int. */
		const char* q = we; const char* qe;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) TC_SET_CURRENT('c', 0);
		else if (tcEqSpan(q, qe, "short")) TC_SET_CURRENT('h', 0);
		else TC_SET_CURRENT('i', 0);
	}
	else if (tcEqSpan(start, we, "int")) TC_SET_CURRENT('i', 0);
	/* long ist auf dem 68k-Ziel wortgleich mit int (32 Bit) -- siehe Grammatik.
	   "long double" ist dagegen ein double: C89 laesst zu, dass long double
	   dieselbe Genauigkeit hat. Das zweite Wort muss deshalb geprueft werden,
	   BEVOR "long" allein zuschlaegt. */
	else if (tcEqSpan(start, we, "long")) {
		const char* q2 = we; const char* qe2;
		while (q2 < end && (*q2 == ' ' || *q2 == '\t')) q2++;
		qe2 = tcWordEnd(q2, end);
		if (tcEqSpan(q2, qe2, "double")) TC_SET_CURRENT('d', 0);
		else TC_SET_CURRENT('i', 0);
	}
	/* short (2026-09-09) ist NICHT wortgleich mit int -- echter 16-Bit-Typ,
	   eigener Basiswert 'h'. tcTypeTag/qcc_backend_c.cpp legen ihn als 2 Byte
	   an und laden ihn nullerweitert (dieselbe Vereinfachung wie bei char:
	   ein negativer short-Wert verhaelt sich hier wie ein positiver
	   Bitmuster-gleicher -- fuer Groessen/Zaehler/Flags aus OS-9-Headern, der
	   eigentliche Anlass, ohne Belang; echte Vorzeichenerweiterung waere ein
	   zweiter Basistyp und ist bewusst nicht gebaut). */
	else if (tcEqSpan(start, we, "short")) TC_SET_CURRENT('h', 0);
	else if (tcEqSpan(start, we, "char")) TC_SET_CURRENT('c', 0);
	else if (tcEqSpan(start, we, "bool")) TC_SET_CURRENT('b', 0);
	/* GLEITKOMMA (2026-09-16): 'd' ist der Typtag fuer double. Acht Byte auf
	   beiden Zielen -- anders als beim Zeiger gibt es hier also nichts
	   symbolisch zu tragen. "float" ist bewusst noch nicht dabei: die
	   ueblichen Konversionen ziehen in C ohnehin auf double hoch, und mit
	   32 Bit kaemen Rundungsfragen dazu, die einen eigenen Testaufbau
	   brauchen (s. docs/FLOAT_PLAN_de.md). */
	else if (tcEqSpan(start, we, "double")) TC_SET_CURRENT('d', 0);
	else if (tcEqSpan(start, we, "void")) TC_SET_CURRENT('v', 0);
	else if (tcEqSpan(start, we, "struct") || tcEqSpan(start, we, "union")) {
		/* union wird als struct gefuehrt (s. tc_unionbegin) -- derselbe
		   Lookup, derselbe Typ, nur ein anderes Layout. */
		const char* p = we; const char* ne; int sid;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		sid = tcLookupStruct(p, ne);
		if (sid < 0) { tcErrAt(start); fprintf(stderr, "unknown struct or union '%.*s'\n", (int)(ne - p), p); actionErrors++; TC_SET_CURRENT('?', 0); return; }
		TC_SET_CURRENT('s', 0); tcCurrentType.structId = (unsigned char)(sid + 1);
	} else if (tcEqSpan(start, we, "enum")) {
		const char* p = we; const char* ne;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		if (tcLookupEnumType(p, ne) < 0) { tcErrAt(start); fprintf(stderr, "unknown enum '%.*s'\n", (int)(ne - p), p); actionErrors++; TC_SET_CURRENT('?', 0); return; }
		TC_SET_CURRENT('i', 0);
	} else {
		int td = tcLookupTypedef(start, we);
		if (td < 0) { tcErrAt(start); fprintf(stderr, "unknown type name '%.*s'\n", (int)(we - start), start); actionErrors++; TC_SET_CURRENT('?', 0); return; }
		tcCurrentType.base = tcTypedefTypes[td].base;
		tcCurrentType.pointers = tcTypedefTypes[td].pointers;
		tcCurrentType.structId = tcTypedefTypes[td].structId;
		tcCurrentType.pointeeConst = tcTypedefTypes[td].pointeeConst;
		tcBasePointers = tcCurrentType.pointers;
	}
}

/* Zeigergrad IMMER NEU aus tcBasePointers + den hier gematchten Sternen
   SETZEN, nicht auf den alten tcCurrentType.pointers AUFADDIEREN (2026-09-09-
   Fix). pointerDecl feuert seit diesem Fix pro Deklarator ("char *a, *b;"),
   nicht mehr einmal fuer die ganze Zeile -- ein Aufaddieren haette den
   zweiten Deklarator faelschlich zum Doppelzeiger gemacht (Zeigergrad des
   ersten Deklarators haette sich mit dem des zweiten summiert). Ein reines
   Nullen waere stattdessen fuer einen Zeiger-typedef falsch (der Grad des
   typedefs selbst ginge verloren) -- daher die Basis aus tc_type statt einer
   festen 0. */
void tc_pointerdecl(const char* start, const char* end) {
	const char* p;
	int n = tcBasePointers;
	/* TCType nicht durch eine Funktionsgrenze reichen: der selfhostende
	   68k-Aufrufpfad behandelt den 4-Byte-Struct-Rueckgabewert nicht korrekt. */
	for (p = start; p < end; p++) if (*p == '*') n++;
	if (n > 255) { n = 255; actionErrors++; }
	tcCurrentType.pointers = (unsigned char)n;
}

void tc_param(const char* start, const char* end) {
	const char* nameEnd = tcNameEnd(start, end);
	if (tcLocalCount >= MAX_LOCALS) { tcErrAt(start); fprintf(stderr, "too many locals\n"); actionErrors++; return; }
	tcCopy(tcNames[tcLocalCount], start, nameEnd);
	/* TCType weder per Rueckgabewert noch per Struct-Zuweisung transportieren. */
	tcLocalTypes[tcLocalCount].base = tcCurrentType.base;
	tcLocalTypes[tcLocalCount].pointers = tcCurrentType.pointers + (nameEnd < end ? 1 : 0);
	tcLocalTypes[tcLocalCount].structId = tcCurrentType.structId;
	tcLocalTypes[tcLocalCount].pointeeConst = tcCurrentType.pointeeConst;
	/* DOUBLE ALS PARAMETER (2026-09-16): noch nicht umgesetzt, und ohne
	   diese Meldung waere es STILL FALSCH -- ein Parameter liegt in einem
	   SLOT, und der ist auf dem 68k vier Byte breit. Ein double braucht
	   acht und liegt deshalb ueberall sonst als Block; fuer Parameter gibt
	   es diesen Weg noch nicht. Im VM-Orakel endete es bisher in einem
	   KeyError, also einem Absturz statt einer Diagnose. */
	/* DOUBLE ALS PARAMETER (2026-09-16 umgesetzt): der Slot nimmt die ADRESSE
	   des Wertes auf (also ein double*), nicht den Wert selbst. Damit der
	   Rumpf davon nichts wissen muss -- sonst muesste JEDE Lesestelle zwischen
	   "Slot haelt Wert" und "Slot haelt Adresse" unterscheiden, und eine
	   vergessene waere wieder ein stiller Rechenfehler -- kopiert
	   tc_funcbodybegin den Wert beim Eintritt in einen echten lokalen Block
	   und laesst den NAMEN auf diesen zeigen. Ab dem Rumpf ist der Parameter
	   damit eine ganz normale lokale double-Variable. */
	if (!tcLocalTypes[tcLocalCount].pointers && tcLocalTypes[tcLocalCount].base == 'd') {
		tcLocalDoubleByAddr[tcLocalCount] = 1;
	} else {
		tcLocalDoubleByAddr[tcLocalCount] = 0;
	}
	if (!tcLocalTypes[tcLocalCount].pointers && tcLocalTypes[tcLocalCount].base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid parameter type\n"); actionErrors++;
		tcLocalTypes[tcLocalCount].base = 'i';
		tcLocalTypes[tcLocalCount].pointers = 0;
		tcLocalTypes[tcLocalCount].structId = 0;
		tcLocalTypes[tcLocalCount].pointeeConst = 0;
	}
	tcLocalArrayLen[tcLocalCount] = 0;
	tcLocalArrayNDims[tcLocalCount] = 1;
	/* "const" vor einem Pointertyp ist Pointee-Constness (const char* p laesst p selbst
	   frei zuweisbar, aber *p/p[i] = .. wird verboten -- tcTargetType.pointeeConst greift
	   in tc_target/tc_indirecttarget), keine Bindungs-Immutabilitaet (das uebliche
	   "p++"-Idiom bleibt erlaubt). */
	tcLocalConst[tcLocalCount] = tcPendingConst && !tcLocalTypes[tcLocalCount].pointers;
	if (tcPendingConst && tcLocalTypes[tcLocalCount].pointers) tcLocalTypes[tcLocalCount].pointeeConst = 1;
	tcLocalDead[tcLocalCount] = 0;
	tcParamNamed = 1;
	/* Ein Struct-Parameter kommt als Adresse an (s. tc_arg), eine lokale
	   Struct-Variable liegt dagegen selbst im Slot. */
	tcLocalStructByAddr[tcLocalCount] =
		tcLocalTypes[tcLocalCount].base == 's' && !tcLocalTypes[tcLocalCount].pointers;
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
			actionErrors++; tcErrAt(start); fprintf(stderr, "duplicate function '%s'\n", tcFuncName); return;
		}
		for (i = 0; i < tcLocalCount; i++) {
			if (!tcSameType(tcFunctionParamTypes[f][i], tcLocalTypes[i])) {
				tcErrAt(start); fprintf(stderr, "function prototype mismatch\n");
				actionErrors++; return;
			}
		}
		tcCurrentFuncIndex = f;
		return;
	}
	if (tcFunctionCount >= MAX_FUNCTIONS) { tcErrAt(start); fprintf(stderr, "too many functions\n"); actionErrors++; return; }
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

/* Feuert nach JEDEM Parameter. Hat paramDecl einen Namen geliefert, ist
   nichts zu tun. Sonst war es ein namenloser Parameter ("int f(int);", die
   uebliche Header-Schreibweise) -- er muss trotzdem in die Signatur, sonst
   stimmt die Argumentzahl nicht.
   tc_param wird dafuer mit einer LEEREN Namensspanne aufgerufen (start,
   start): der Name wird leer, tcNameEnd liefert start, und weil damit
   "nameEnd < end" falsch ist, kommt auch kein Array-Suffix-Zeiger dazu. Die
   Fehlerposition bleibt korrekt, weil start in den Eingabepuffer zeigt. */
void tc_paramend(const char* start, const char* end) {
	(void)end;
	if (tcParamNamed) { tcParamNamed = 0; return; }
	tc_param(start, start);
	tcParamNamed = 0;
}

/* Feuert bei "{" (Beginn eines echten Funktionsrumpfs) -- siehe tc_funcbegin. */
void tc_funcbodybegin(const char* start, const char* end) {
	(void)start; (void)end;
	tcLogicDepth = 0;
	tcBitDepth = 0;
	tcPendingShift0 = 0;
	tcPendingShift1 = 0;
	tcIndexDepth = 0;
	if (tcCurrentFuncIndex >= 0) tcFunctionIsDeclOnly[tcCurrentFuncIndex] = 0;
	printf("FUNC %s %d %d\n", tcFuncName, tcLocalCount, tcCurrentFuncIndex >= 0 ? tcFunctionIsStatic[tcCurrentFuncIndex] : 0);
	/* DOUBLE-PARAMETER AUSPACKEN (2026-09-16). Der Slot haelt eine Adresse
	   (s. tc_param). Hier -- und nur hier -- wird daraus eine normale lokale
	   double-Variable: ein eigener Block, der Wert hineinkopiert, und der
	   NAME wandert vom Adress-Slot auf den Block. Der Adress-Slot behaelt
	   einen Namen, den kein C-Bezeichner haben kann, damit die Namenssuche
	   ihn nicht mehr findet.
	   DASS HIER KOPIERT WIRD, ist der Grund, warum REKURSION traegt: der
	   globale Argumentpuffer ist ab dem ersten Befehl des Rumpfs wieder frei,
	   lange bevor ein rekursiver Aufruf ihn erneut beschreibt.
	   Die Schleife laeuft ueber die urspruengliche Parameterzahl, waehrend
	   tcLocalCount dabei waechst -- deshalb wird sie VORHER festgehalten. */
	{
		int pn = tcLocalCount;
		int pi;
		for (pi = 0; pi < pn; pi++) {
			if (tcLocalDoubleByAddr[pi]) {
				int nslot;
				if (tcLocalCount >= MAX_LOCALS) {
					tcErrAt(start); fprintf(stderr, "too many locals\n"); actionErrors++;
					return;
				}
				nslot = tcLocalCount++;
				tcCopy(tcNames[nslot], tcNames[pi], tcNames[pi] + strlen(tcNames[pi]));
				tcLocalTypes[nslot].base = 'd';
				tcLocalTypes[nslot].pointers = 0;
				tcLocalTypes[nslot].structId = 0;
				tcLocalTypes[nslot].pointeeConst = 0;
				tcLocalArrayLen[nslot] = 0;
				tcLocalArrayNDims[nslot] = 1;
				tcLocalConst[nslot] = 0;
				tcLocalDead[nslot] = 0;
				tcLocalStructByAddr[nslot] = 0;
				tcLocalDoubleByAddr[nslot] = 0;
				tcNames[pi][0] = '.';
				tcNames[pi][1] = 0;
				printf("LARRAY %d d 1\n", nslot);
				printf("LOADP %d\nLOADIND d\nSTORED %d\n", pi, nslot);
			}
		}
	}
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
	/* GELOCKERT 2026-08-10: ein rumpfloser Prototyp bedeutet NICHT
	   zwangslaeufig "in einer anderen Datei definiert". In C ist
	   "static int f(int);" mit spaeterer Definition in DERSELBEN Datei der
	   voellig uebliche Weg fuer Vorwaertsdeklarationen und gegenseitige
	   Rekursion -- Data/qcc_p.c macht davon 177-mal Gebrauch. Die fruehere
	   Ablehnung war deshalb schlicht falsch.
	   Unbedenklich fuer den Codegen: collectFunctions() im Backend sammelt in
	   einem ERSTEN Durchlauf alle echten FUNC-RueMpfe und erst danach die
	   FUNCDECLs, uebernimmt eine Vorwaertsdeklaration also nur dann, wenn kein
	   echter Rumpf existiert -- die Reihenfolge im Quelltext spielt keine Rolle.
	   BEWUSST AUFGEGEBEN: ein static-Prototyp, dem NIE eine Definition folgt,
	   wird jetzt nicht mehr hier gemeldet; er faellt erst beim Linken als
	   unaufgeloestes Symbol auf. */
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
			tcErrAt(start); fprintf(stderr, "undefined label '%s'\n", tcGotoNames[gi]);
			actionErrors++;
		}
	}
	if (tcCurrentFuncIndex >= 0 && tcFunctionIsDeclOnly[tcCurrentFuncIndex]) return;
	/* Fallthrough-Rueckgabe 0 absichern; ein explizites return davor ist harmlos */
	printf("PUSH 0\n%s\nENDFUNC\n", tcIsPointer(tcFuncType) ? "RETP" : "RET");
}

void tc_local(const char* start, const char* end) {
	if (tcLocalCount >= MAX_LOCALS) { tcErrAt(start); fprintf(stderr, "too many locals\n"); actionErrors++; return; }
	tcCopy(tcNames[tcLocalCount], start, end);
	tcLocalTypes[tcLocalCount] = tcCurrentType;
	if (!tcIsPointer(tcLocalTypes[tcLocalCount]) && tcLocalTypes[tcLocalCount].base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++;
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
	tcLocalDead[tcLocalCount] = 0;
	/* Die Slot-Tabellen ueberdauern die Funktion -- ohne dieses Loeschen
	   erbt eine lokale Variable die Markierung eines frueheren Parameters
	   auf demselben Slot. */
	tcLocalStructByAddr[tcLocalCount] = 0;
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
	/* Ein double braucht wie ein struct IMMER Block-Speicher (8 Byte), nie
	   den einzelligen Slot -- s. docs/FLOAT_IR_ENTWURF_de.md. */
	int isDouble = tcLocalTypes[slot].base == 'd' && !tcLocalTypes[slot].pointers;
	int structSizeK = isStruct ? tcStructByteSizeK[tcLocalTypes[slot].structId - 1] : 0;
	int structSizeN = isStruct ? tcStructByteSizeN[tcLocalTypes[slot].structId - 1] : 0;
	while (p < end && *p != '[') p++;
	if (p == end) {
		if (isStruct) { printf("LARRAY %d c ", slot); tcEmitNum(structSizeK, structSizeN); printf("\n"); }
		else if (isDouble) printf("LARRAY %d d 1\n", slot);
		return;
	}
	while (p < end && *p == '[') {
		int len;
		p++;
		len = tcConstArrayLen(&p, end);
		if (len <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "array size must be positive\n"); return; }
		if (p >= end || *p != ']') { actionErrors++; tcErrAt(start); fprintf(stderr, "bad array declaration\n"); return; }
		p++;
		if (ndims >= TC_MAXDIMS) { actionErrors++; tcErrAt(start); fprintf(stderr, "too many array dimensions (max %d)\n", TC_MAXDIMS); return; }
		dims[ndims++] = len;
	}
	total = dims[0];
	for (k = 1; k < ndims; k++) { total *= dims[k]; tcLocalArrayDims[slot][k - 1] = dims[k]; }
	tcLocalArrayNDims[slot] = ndims;
	tcLocalArrayLen[slot] = total;
	if (isStruct) { printf("LARRAY %d c ", slot); tcEmitNum(total * structSizeK, total * structSizeN); printf("\n"); }
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
	int arrayLen = 0; const char* q = start;
	int runtimeInit = tcStaticRuntimeInitPending;
	tcStaticRuntimeInitPending = 0;
	tcPendingConst = 0;
	tcPendingStatic = 0;
	/* "[N]" aus dem Rohtext lesen -- vor einem etwaigen "=", damit eine
	   Zahl im Initialisierer nicht als Laenge missverstanden wird. */
	while (q < end && *q != '[' && *q != '=') q++;
	if (q < end && *q == '[') {
		q++;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		if (q + 1 < end && *q == '0' && (q[1] == 'x' || q[1] == 'X')) {
			q += 2;
			for (;;) {
				int hd;
				if (*q >= '0' && *q <= '9') hd = *q - '0';
				else if (*q >= 'a' && *q <= 'f') hd = *q - 'a' + 10;
				else if (*q >= 'A' && *q <= 'F') hd = *q - 'A' + 10;
				else break;
				arrayLen = arrayLen * 16 + hd;
				q++;
				if (q >= end) break;
			}
		} else {
			while (q < end && *q >= '0' && *q <= '9') arrayLen = arrayLen * 10 + (*q++ - '0');
		}
	}
	if (tcCurrentType.base == 's') {
		tcErrAt(start); fprintf(stderr, "static struct locals not yet supported\n");
		actionErrors++;
		return;
	}
	if (arrayLen < 0 || arrayLen > 65535) {
		tcErrAt(start); fprintf(stderr, "static local array length out of range\n");
		actionErrors++;
		return;
	}
	if (!tcIsPointer(tcCurrentType) && tcCurrentType.base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++;
		return;
	}
	if (arrayLen > 0) {
		const char* r = start;
		while (r < end && *r != '=') r++;
		if (r < end || runtimeInit) {
			tcErrAt(start);
			fprintf(stderr, "static local array initializers are not supported yet\n");
			actionErrors++;
			return;
		}
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
				tcErrAt(start); fprintf(stderr, "static local pointer initializer must be 0\n");
				actionErrors++;
			}
			if (!tcCurrentType.pointers && tcCurrentType.base == 'c') value &= 255;
			if (!tcCurrentType.pointers && tcCurrentType.base == 'h') value &= 65535;
			if (!tcCurrentType.pointers && tcCurrentType.base == 'b') value = value ? 1 : 0;
		}
	}
	if (tcGlobalCount >= MAX_GLOBALS) { tcErrAt(start); fprintf(stderr, "too many globals\n"); actionErrors++; return; }
	for (i = 0; i < tcGlobalCount; i++) {
		if (tcEq(tcGlobalNames[i], tcStaticLocalName)) { actionErrors++; tcErrAt(start); fprintf(stderr, "duplicate global '%s'\n", tcStaticLocalName); return; }
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
	tcGlobalArrayNDims[tcGlobalCount] = arrayLen > 0 ? 1 : 0;
	tcGlobalArrayLen[tcGlobalCount++] = arrayLen;
	/* Ein static-lokales Array ist dasselbe wie ein unsichtbares globales:
	   Block-Speicher per GARRAY, nicht die einzellige GLOBAL-Form. */
	if (arrayLen > 0) {
		printf("GARRAY %s %c %d 1\n", tcStaticLocalName, tcTypeTag(tcCurrentType), arrayLen);
		return;
	}
	printf("GLOBAL %s %ld %c 1\n", tcStaticLocalName, value, tcTypeTag(tcCurrentType));
	if (runtimeInit) {
		char flagName[40]; int doInitLabel = tcNextLabel++, afterLabel = tcNextLabel++;
		const char* storeSuffix = tcIsPointer(tcCurrentType) ? "P" : tcWordSuffix(tcTypeTag(tcCurrentType));
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

/* Mehrere Deklaratoren in einer globalen Deklaration
   ("static TCType a[512], b[512][64];", 31 Fundstellen in den beiden
   Bootstrap-Zieldateien). tcGlobalOne parst eine VOLLSTAENDIGE Deklaration aus
   dem Rohtext (Typ + EIN Deklarator), deshalb wird hier an den Kommas der
   OBERSTEN Ebene geteilt und fuer jeden weiteren Deklarator der Typ-Praefix
   davorgesetzt. Kommas innerhalb von "[...]" oder "{...}" (Initialisiererlisten)
   zaehlen dabei nicht. */
static void tc_globalend(const char* start, const char* end);
static void tcGlobalOne(const char* start, const char* end);
void tc_globalend(const char* start, const char* end) {
	const char* p = start;
	const char* declStart;
	const char* seg;
	char buf[1024];
	int depth = 0, n, i;
	/* Ende des Typ-Praefix suchen: erster Bezeichner, der von "[", "=", ","
	   oder ";" gefolgt wird -- also der erste Deklaratorname. */
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	declStart = p;
	{
		const char* q = p; const char* lastWord = p;
		while (q < end && *q != '[' && *q != '=' && *q != ',' && *q != ';') {
			if (*q == ' ' || *q == '\t') {
				const char* next = q;
				while (next < end && (*next == ' ' || *next == '\t')) next++;
				/* Leerraum NACH dem ersten Deklarator gehoert nicht mehr zum
				   Typ-Praefix. Vorher machte "char a = 0, b = 0" aus dem
				   zweiten Segment still "char a b = 0". */
				if (next >= end || *next == '[' || *next == '=' || *next == ',' || *next == ';') break;
				q = next; lastWord = q;
			} else if (*q == '*') { q++; lastWord = q; }
			else q++;
		}
		declStart = lastWord;
	}
	/* Gibt es ueberhaupt ein Komma auf oberster Ebene? */
	depth = 0; seg = 0;
	for (p = declStart; p < end; p++) {
		if (*p == '[' || *p == '{') depth++;
		else if (*p == ']' || *p == '}') depth--;
		else if (*p == ',' && depth == 0) { seg = p; break; }
	}
	if (!seg) { tcGlobalOne(start, end); return; }
	/* erster Deklarator: Originaltext bis zum Komma, mit ";" abgeschlossen */
	n = (int)(seg - start);
	if (n > 1000) { tcErrAt(start); fprintf(stderr, "declaration too long\n"); actionErrors++; return; }
	for (i = 0; i < n; i++) buf[i] = start[i];
	buf[n] = ';'; buf[n + 1] = 0;
	tcGlobalOne(buf, buf + n + 1);
	/* weitere Deklaratoren: Typ-Praefix + Segment */
	p = seg + 1;
	for (;;) {
		const char* stop = 0;
		int pre = (int)(declStart - start);
		depth = 0;
		for (seg = p; seg < end; seg++) {
			if (*seg == '[' || *seg == '{') depth++;
			else if (*seg == ']' || *seg == '}') depth--;
			else if ((*seg == ',' || *seg == ';') && depth == 0) { stop = seg; break; }
		}
		if (!stop) stop = end;
		n = 0;
		/* "*" NICHT mitkopieren (2026-09-09-Fix): der Typ-Praefix reicht laut
		   obigem Scan bis zum ERSTEN Deklarator, schliesst also dessen EIGENEN
		   Stern mit ein ("char *a, *b" -> Praefix "char *"). Kopiert man den
		   unveraendert vor jedes weitere Segment, bekommt "*b" faelschlich
		   einen ZWEITEN Stern spendiert ("char **b") -- und bei "char* a, b"
		   (kein Stern beim zweiten Deklarator) wuerde "b" faelschlich ZUM
		   ZEIGER, obwohl in echtem C nur "a" einer ist. Jeder Stern gehoert
		   dem Deklarator, VOR dessen Namen er im Rohtext steht -- der
		   erste hat seinen schon in seinem eigenen (unveraendert kopierten)
		   Segment, jeder weitere bringt seinen eigenen (falls vorhanden) im
		   Text NACH dem Komma mit. Der Praefix selbst darf deshalb nie einen
		   Stern beisteuern. */
		for (i = 0; i < pre; i++) if (start[i] != '*') buf[n++] = start[i];
		/* Trenner ERZWINGEN (2026-09-09): ohne Leerraum zwischen Typwort und
		   Sternen im Original ("char** p, q;") faellt beim Sternefiltern jeder
		   Abstand weg ("char" + "q" -> "charq", ein einziges Bezeichnerwort
		   statt Schluesselwort+Name). Ein zusaetzliches Leerzeichen ist immer
		   sicher -- tcGlobalOne ueberspringt Leerraum ohnehin an jeder
		   Token-Grenze, ein doppeltes stoert nicht. */
		buf[n++] = ' ';
		while (p < stop && (*p == ' ' || *p == '\t')) p++;
		while (p < stop) buf[n++] = *p++;
		buf[n++] = ';'; buf[n] = 0;
		tcGlobalOne(buf, buf + n);
		if (stop >= end || *stop == ';') break;
		p = stop + 1;
	}
}
static void tcGlobalOne(const char* start, const char* end) {
const char* p = start; char name[32]; TCType type = tcMakeType('i', 0); int n = 0, neg = 0, arrayLen = 0, arrayNDims = 1, initCount = -1; int hadBrackets = 0; long value = 0, initValues[256]; int i; int arrayDims[TC_MAXDIMS - 1]; int initSyms[256];
	int ptrStrSym = -1;   /* __strN eines skalaren Zeiger-Initialisierers */
	int rowLen = 1;
	int dimI;
	/* Initialisierer an einem globalen double: der Wert geht als BITMUSTER
	   in die Daten, nicht als Zahl -- "value" (long) kann ihn nicht tragen. */
	unsigned long dblHi = 0UL, dblLo = 0UL;
	int hasDbl = 0;
	int isConst = tcPendingConst; int isStatic = 0; tcPendingConst = 0;
	tcPendingStatic = 0;
	/* -1 heisst "gewoehnlicher Zahlenwert". MUSS gesetzt sein, BEVOR
	   irgendein Initialisiererpfad laeuft: der String-Zweig fuer
	   char-Arrays (char msg[6] = "hallo") fuellt nur initValues, und ein
	   uninitialisiertes initSyms haette dort GINITADDR statt GINIT erzeugt.
	   Steht HINTER der letzten Deklaration des Blocks: C89 erlaubt keine
	   Deklaration nach einer Anweisung, und xcc besteht darauf -- clang
	   nimmt es klaglos an, weshalb es erst der Bootstrap-Bau gemeldet hat. */
	for (i = 0; i < 256; i++) initSyms[i] = -1;
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
	/* "volatile" wird hier nur UEBERSPRUNGEN -- es hat in QCC keine eigene
	   Wirkung, weil der Codegen ohnehin jeden Zugriff ueber den Speicher
	   fuehrt; die Begruendung steht ausgeschrieben bei volatileKw in
	   qcc.ebnf und wird von der Suite am Assembler nachgeprueft. */
	if (end - p >= 8 && p[0] == 'v' && p[1] == 'o' && p[2] == 'l' && p[3] == 'a' && p[4] == 't' && p[5] == 'i' && p[6] == 'l' && p[7] == 'e') {
		p += 8;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	/* struct-Basistyp (2026-07-25, globale struct-Unterstuetzung): mirrort die
	   struct-Erkennung in tc_type. Muss VOR den skalaren Basistyp-Checks stehen,
	   da "struct" selbst kein reservierter Modifikator wie "const"/"static" ist,
	   sondern ein eigener Basistyp. */
	/* "union" fuehrt hierher wie "struct": eine union IST intern eine struct
	   mit anderem Layout (s. tc_unionbegin), also derselbe Lookup. */
	if ((end - p >= 6 && p[0] == 's' && p[1] == 't' && p[2] == 'r' && p[3] == 'u' && p[4] == 'c' && p[5] == 't') ||
	    (end - p >= 5 && p[0] == 'u' && p[1] == 'n' && p[2] == 'i' && p[3] == 'o' && p[4] == 'n')) {
		const char* ne; int sid;
		p += (p[0] == 'u') ? 5 : 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		sid = tcLookupStruct(p, ne);
		if (sid < 0) { tcErrAt(start); fprintf(stderr, "unknown struct or union '%.*s'\n", (int)(ne - p), p); actionErrors++; return; }
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
		else if (tcEqSpan(q, qe, "short")) { type.base = 'h'; p = qe; }
		else if (tcEqSpan(q, qe, "int") || tcEqSpan(q, qe, "long")) { type.base = 'u'; p = qe; }
		else type.base = 'u';                 /* blosses "unsigned" */
	}
	/* "signed" (2026-09-16): dieselbe Folgewort-Pruefung wie bei "unsigned"
	   weiter oben, nur bleibt der Typ vorzeichenbehaftet. Vorher meldete
	   "signed char g;" ein "bad global declaration". */
	else if (end - p >= 6 && p[0] == 's' && p[1] == 'i' && p[2] == 'g' && p[3] == 'n' && p[4] == 'e' && p[5] == 'd') {
		const char* sq; const char* sqe;
		p += 6;
		sq = tcSkipWs(p, end); sqe = tcWordEnd(sq, end);
		if (tcEqSpan(sq, sqe, "char")) { type.base = 'c'; p = sqe; }
		else if (tcEqSpan(sq, sqe, "short")) { type.base = 'h'; p = sqe; }
		else if (tcEqSpan(sq, sqe, "int") || tcEqSpan(sq, sqe, "long")) { type.base = 'i'; p = sqe; }
		else type.base = 'i';
	}
	else if (end - p >= 3 && p[0] == 'i' && p[1] == 'n' && p[2] == 't') p += 3;
	/* long ist auf dem 68k-Ziel wortgleich mit int (32 Bit), type.base bleibt 'i'. */
	/* "long double" vor dem blossen "long" pruefen (2026-09-16): sonst
	   schluckt "long" den Typ und der double-Anteil landet als
	   Deklaratortext. C89 laesst zu, dass long double dieselbe Genauigkeit
	   hat wie double -- genau so wird es hier behandelt. */
	else if (end - p >= 11 && p[0] == 'l' && p[1] == 'o' && p[2] == 'n' && p[3] == 'g' &&
	         (p[4] == ' ' || p[4] == '\t') && tcEqSpan(tcSkipWs(p + 4, end), tcWordEnd(tcSkipWs(p + 4, end), end), "double")) {
		p = tcWordEnd(tcSkipWs(p + 4, end), end); type.base = 'd';
	}
	else if (end - p >= 4 && p[0] == 'l' && p[1] == 'o' && p[2] == 'n' && p[3] == 'g') p += 4;
	/* short (2026-09-09): ECHTER 16-Bit-Typ, siehe tc_type/tcTypeTag. */
	else if (end - p >= 5 && p[0] == 's' && p[1] == 'h' && p[2] == 'o' && p[3] == 'r' && p[4] == 't') { p += 5; type.base = 'h'; }
	else if (end - p >= 4 && p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'r') { p += 4; type.base = 'c'; }
	else if (end - p >= 4 && p[0] == 'b' && p[1] == 'o' && p[2] == 'o' && p[3] == 'l') { p += 4; type.base = 'b'; }
	else if (end - p >= 4 && p[0] == 'v' && p[1] == 'o' && p[2] == 'i' && p[3] == 'd') { p += 4; type.base = 'v'; }
	/* double (2026-09-16) -- fehlte an BEIDEN Stellen, waehrend dieselbe
	   Deklaration als LOKALE laengst ging: "double g;" scheiterte mit "bad
	   global declaration". Die Basistyp-Erkennung steht in tcGlobalOne und
	   in tc_externglobaldecl getrennt -- wer nur eine anfasst, baut die
	   naechste stille Abweichung ein. */
	else if (end - p >= 6 && p[0] == 'd' && p[1] == 'o' && p[2] == 'u' && p[3] == 'b' && p[4] == 'l' && p[5] == 'e') { p += 6; type.base = 'd'; }
	else {
		/* Typedef-Name als Basistyp ("TCType x;") -- dieselbe Aufloesung wie in
		   tc_type. Steht ZULETZT, damit die eingebauten Schluesselwoerter nicht
		   als Typedefname missdeutet werden. Ohne diesen Zweig scheiterte JEDE
		   globale Variable eines typedef'ten Typs mit "bad global declaration",
		   obwohl dieselbe Deklaration als LOKALE laengst funktionierte. */
		const char* ne = tcWordEnd(p, end);
		int td = tcLookupTypedef(p, ne);
		if (td < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad global declaration\n"); return; }
		type = tcTypedefTypes[td];
		p = ne;
	}
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p == '*') { type = tcPointerTo(type); p++; while (p < end && (*p == ' ' || *p == '\t')) p++; }
	if (!tcIsPointer(type) && type.base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++; return;
	}
	while (p < end && *p != ' ' && *p != '\t' && *p != '=' && *p != ';' && *p != '[' && n < 31) name[n++] = *p++;
	name[n] = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p < end && *p == '[') {
		int dims[TC_MAXDIMS]; int ndims = 0; int k;
		while (p < end && *p == '[') {
			int len;
			p++;
			len = tcConstArrayLen(&p, end);
			if (p >= end || *p != ']') { actionErrors++; tcErrAt(start); fprintf(stderr, "bad array declaration\n"); return; }
			p++;
			/* len==0 heisst "[]" -- Groesse offen, wird weiter unten aus dem
			   String-Initialisierer abgeleitet. Nur echte Negativwerte sind ein Fehler. */
			if (len < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "array size must be positive\n"); return; }
			if (ndims >= TC_MAXDIMS) { actionErrors++; tcErrAt(start); fprintf(stderr, "too many array dimensions (max %d)\n", TC_MAXDIMS); return; }
			dims[ndims++] = len;
		}
		hadBrackets = 1;   /* auch bei "[]" -- arrayLen ist dann noch 0 */
		arrayLen = dims[0];
		for (k = 1; k < ndims; k++) { arrayLen *= dims[k]; arrayDims[k - 1] = dims[k]; }
		arrayNDims = ndims;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (p < end && *p == '=') {
		p++;
		/* hadBrackets statt arrayLen: bei "char x[] = \"abc\";" ist die Laenge
		   hier noch unbekannt und wird erst aus dem Literal abgeleitet. */
		if (arrayLen || hadBrackets) {
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
					/* "char x[] = \"abc\";" -- Groesse offen gelassen, also aus dem
					   Literal ableiten (Zeichen + abschliessendes Nullbyte). */
					if (arrayLen == 0) arrayLen = strLen + 1;
					if (strLen > arrayLen) { tcErrAt(start); fprintf(stderr, "string literal too long for array\n"); actionErrors++; return; }
					initCount = strLen < arrayLen ? strLen + 1 : strLen;
					for (i = 0; i < strLen; i++) initValues[i] = strBytes[i];
					if (strLen < arrayLen) initValues[strLen] = 0;
				} else {
					tcErrAt(start); fprintf(stderr, "string literal initializer requires a char array\n"); actionErrors++; return;
				}
			} else {
				/* Zeilenlaenge = Produkt ALLER Dimensionen ausser der ersten;
				   arrayDims traegt genau die (s. oben, dims[1..]). Bei einem
				   1D-Array bleibt sie 1, dann ist keine Verschachtelung
				   erlaubt. */
				rowLen = 1;
				for (dimI = 0; dimI < arrayNDims - 1; dimI++) rowLen *= arrayDims[dimI];
				/* Ein double-Array mit Liste geht noch nicht: tcInitList liest
				   Ganzzahlen und scheitert an "1.0" mit -1 -- das ergaebe
				   "bad or oversized array initializer", eine Meldung, die in
				   die Irre fuehrt. Deshalb VOR dem Listenparser abfangen. */
				/* EIGENER LISTENPARSER FUER double-ARRAYS (2026-09-16).
				   tcInitList liest Ganzzahlen und scheitert an "1.0"; die
				   Werte muessen hier ohnehin als BITMUSTER durch, nicht als
				   Zahl. Deshalb eine eigene, kurze Schleife -- der bestehende
				   Listenparser bleibt unberuehrt. Verschachtelung
				   ("{{1.0},{2.0}}") wird bewusst GEMELDET statt still
				   verschluckt; mehrdimensionale double-Arrays sind hier nicht
				   vorgesehen. */
				if (!type.pointers && type.base == 'd') {
					const char* q = p;
					tcDblInitCount = 0;
					while (q < end && (*q == ' ' || *q == '\t')) q++;
					if (q >= end || *q != '{') {
						actionErrors++; tcErrAt(start);
						fprintf(stderr, "bad or oversized array initializer\n");
						return;
					}
					q++;
					for (;;) {
						int dneg = 0;
						const char* fe;
						while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
						if (q < end && *q == '}') { q++; break; }
						if (q < end && *q == '{') {
							actionErrors++; tcErrAt(start);
							fprintf(stderr, "nested initializer list is not supported for this array in this version\n");
							return;
						}
						if (q < end && *q == '-') { dneg = 1; q++; }
						fe = tcFloatLitEnd(q, end);
						if (fe == q) {
							actionErrors++; tcErrAt(start);
							fprintf(stderr, "bad or oversized array initializer\n");
							return;
						}
						if (tcDblInitCount >= TC_MAX_DBL_INIT) {
							actionErrors++; tcErrAt(start);
							fprintf(stderr, "too many initializers for a double array\n");
							return;
						}
						if (!qccDecToDouble(q, fe, dneg, &tcDblInitHi[tcDblInitCount], &tcDblInitLo[tcDblInitCount])) {
							actionErrors++; tcErrAt(start);
							fprintf(stderr, "malformed floating point literal\n");
							return;
						}
						tcDblInitCount++;
						q = fe;
						while (q < end && (*q == 'f' || *q == 'F' || *q == 'l' || *q == 'L')) q++;
						while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
						if (q < end && *q == ',') { q++; continue; }
						if (q < end && *q == '}') { q++; break; }
						actionErrors++; tcErrAt(start);
						fprintf(stderr, "bad or oversized array initializer\n");
						return;
					}
					initCount = tcDblInitCount;
				}
				else initCount = tcInitList(p, end, initValues, initSyms, 256, rowLen);
				if (initCount == -4) { actionErrors++; tcErrAt(start); fprintf(stderr, "nested initializer list is not supported for this array in this version\n"); return; }
				if (initCount == -3) { actionErrors++; tcErrAt(start); fprintf(stderr, "string literal in initializer list requires a pointer array\n"); return; }
				if (initCount < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad or oversized array initializer\n"); return; }
				/* "int a[] = {1,2,3}" und "char *t[] = {\"a\",\"b\"}" -- Groesse
				   offen gelassen, also aus der Liste ableiten. Genau wie beim
				   String-Literal darueber; vorher scheiterte die Deklaration
				   an "initCount > arrayLen" (arrayLen war noch 0) und die
				   Variable wurde gar nicht erst registriert, was sich als
				   "unknown variable" an der BENUTZUNGSSTELLE zeigte. */
				if (arrayLen == 0 && hadBrackets && initCount > 0) arrayLen = initCount;
				if (initCount > arrayLen) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad or oversized array initializer\n"); return; }
			}
		} else {
			while (p < end && (*p == ' ' || *p == '\t')) p++;
			/* STRUCT MIT INITIALISIERERLISTE, GLOBAL (2026-09-16):
			   "struct S g = {1,2};". Ein struct ist weder Skalar noch Array,
			   deshalb landete es hier und bekam "scalar cannot use array
			   initializer" -- eine Meldung, die den Fall nicht sah.
			   Die Werte werden hier nur GELESEN; zugeordnet werden sie erst
			   bei der Emission, wo Feldoffsets und Feldtypen feststehen. */
			if (!type.pointers && type.base == 's' && p < end && *p == '{') {
				initCount = tcInitList(p, end, initValues, initSyms, 256, 0);
				if (initCount == -4) { actionErrors++; tcErrAt(start); fprintf(stderr, "nested initializer list is not supported for a struct in this version\n"); return; }
				if (initCount < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad struct initializer\n"); return; }
			}
			else if (p < end && *p == '{') { actionErrors++; tcErrAt(start); fprintf(stderr, "scalar cannot use array initializer\n"); return; }
			/* EIN EINZELNER Zeiger auf ein String-Literal (char *s = "ab";).
			   Bis 2026-09-15 wurde der Initialisierer hier STILL VERWORFEN --
			   herauskam "GLOBAL s 0 p 0", also ein Nullzeiger, und der erste
			   Zugriff lief ins Leere. Derselbe Weg wie bei der Zeigertabelle:
			   das Literal wird ein anonymes Global, seine Adresse geht als
			   GINITADDR in die Daten. Lokal ging es laengst, weil dort ein
			   Laufzeit-Store erzeugt wird. */
			if (type.pointers && p < end && *p == '"') {
				const char* sq = p + 1;
				while (sq < end && *sq != '"') { if (*sq == '\\' && sq + 1 < end) sq++; sq++; }
				if (sq >= end) { tcErrAt(start); fprintf(stderr, "unterminated string initializer\n"); actionErrors++; return; }
				ptrStrSym = tcEmitStringGlobal(p, sq + 1);
			}
			if (!type.pointers && type.base == 'b' && p + 4 <= end && p[0] == 't') value = 1;
			else {
				const char* dot;
				int isHexLit;
				if (p < end && *p == '-') { neg = 1; p++; }
				/* "0x1E" endet auf ein E, das KEIN Exponent ist -- Hexzahlen
				   muessen deshalb vor der Gleitkomma-Erkennung ausscheiden. */
				isHexLit = (p + 1 < end && *p == '0' && (p[1] == 'x' || p[1] == 'X'));
				/* Gleitkomma erkennen und MELDEN: der Wert muesste als
				   Bitmuster in den Datenbereich, und dafuer fehlt noch der
				   GINIT-Pfad. Ohne diesen Zweig war es ein stiller Abbruch. */
				dot = p;
				while (dot < end && *dot >= '0' && *dot <= '9') dot++;
				/* Ein Gleitkommaliteral erkennt man am Punkt ODER am
				   Exponenten: "1e2" hat keinen Punkt. Ist das Ziel ein
				   double, geht JEDE Zahl diesen Weg -- auch "double g = 5;",
				   denn auch sie muss als Bitmuster in die Daten. */
				if (!type.pointers && type.base == 'd' && !isHexLit && dot > p) {
					/* GLEITKOMMA-INITIALISIERER (2026-09-16 umgesetzt).
					   Umgerechnet wird mit demselben Konverter wie beim
					   Literal im Ausdruck (tc_floatlit), damit "double g =
					   0.1;" und "a = 0.1;" garantiert dasselbe Bitmuster
					   ergeben. Das Vorzeichen ist oben schon abgetrennt und
					   geht als Flag mit. */
					const char* fe = tcFloatLitEnd(p, end);
					if (!qccDecToDouble(p, fe, neg, &dblHi, &dblLo)) {
						tcErrAt(start);
						fprintf(stderr, "malformed floating point literal\n");
						actionErrors++;
						return;
					}
					hasDbl = 1;
					p = fe;
					while (p < end && (*p == 'f' || *p == 'F' || *p == 'u' || *p == 'U' || *p == 'l' || *p == 'L')) p++;
				}
				else if (!isHexLit && dot > p && dot < end &&
				         (*dot == '.' || *dot == 'e' || *dot == 'E')) {
					/* Gleitkomma an einem nicht-double Ziel */
					tcErrAt(start);
					fprintf(stderr, "floating point initializer requires a double\n");
					actionErrors++;
					return;
				}
				/* Hexzahl wie im Ausdruck: "0x7FF00000UL". */
				if (p + 1 < end && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
					int hd;
					p += 2;
					while (p < end) {
						if (*p >= '0' && *p <= '9') hd = *p - '0';
						else if (*p >= 'a' && *p <= 'f') hd = *p - 'a' + 10;
						else if (*p >= 'A' && *p <= 'F') hd = *p - 'A' + 10;
						else break;
						value = value * 16 + hd;
						p++;
					}
				} else {
					while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
				}
				while (p < end && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L')) p++;
				if (neg) value = -value; }
			if (type.pointers && value != 0) { tcErrAt(start); fprintf(stderr, "global pointer initializer must be 0\n"); actionErrors++; return; }
		}
	}
	if (tcGlobalCount >= MAX_GLOBALS) { tcErrAt(start); fprintf(stderr, "too many globals\n"); actionErrors++; return; }
	for (i = 0; i < tcGlobalCount; i++) if (tcEq(tcGlobalNames[i], name)) { actionErrors++; tcErrAt(start); fprintf(stderr, "duplicate global '%s'\n", name); return; }
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
	   braucht ein struct IMMER Block-Speicher (GARRAY, byte-genau ueber tcStructByteSizeK),
	   NIE die einzellige GLOBAL-Form -- unabhaengig davon, ob ein "[N]"-Suffix dabeisteht
	   (arrayLen==0 fuer eine skalare struct-Variable heisst hier "ein Element", NICHT
	   "kein Block", anders als bei tcLocalArrayLen/tcGlobalArrayLen als Index-Laenge weiter
	   unten -- ADDRG braucht ohnehin IMMER einen Block, siehe qccvm.py: globals_ ist bei
	   GLOBAL wie bei GARRAY einheitlich eine Liste, ADDRG unterscheidet nicht). */
	/* Ein double braucht wie ein struct Block-Speicher (8 Byte), nie die
	   einzellige GLOBAL-Form -- s. docs/FLOAT_IR_ENTWURF_de.md. */
	if (!type.pointers && type.base == 'd') {
		int dcount = arrayLen > 0 ? arrayLen : 1;
		printf("GARRAY %s d %d %d\n", name, dcount, isStatic);
		/* Initialisiererliste: je Element ein GINITD. Die Werte stehen schon
		   als Bitmuster bereit (eigener Listenparser weiter oben). */
		if (initCount > 0) {
			int di;
			for (di = 0; di < initCount; di++)
				printf("GINITD %s %d %lu %lu\n", name, di, tcDblInitHi[di], tcDblInitLo[di]);
		}
		/* Zwei 32-Bit-Haelften, hi zuerst -- wie PUSHD. Welche zuerst im
		   Speicher landet, entscheidet das BACKEND: auf dem 68k big-endian
		   hi, auf ARM64 little-endian umgekehrt. Das Frontend darf die
		   Reihenfolge deshalb nicht festlegen. */
		if (hasDbl) printf("GINITD %s 0 %lu %lu\n", name, dblHi, dblLo);
		return;
	}
	if (!type.pointers && type.base == 's') {
		int count = arrayLen > 0 ? arrayLen : 1;
		int ssid = type.structId - 1;
		printf("GARRAY %s c ", name);
		tcEmitNum(count * tcStructByteSizeK[ssid], count * tcStructByteSizeN[ssid]);
		printf(" %d\n", isStatic);
		/* Initialisiererliste: je Feld ein GINITAT mit BYTE-Offset, Typtag und
		   Wert. Ein eigener Opcode ist noetig, weil der Block ein char-Array
		   ist: ein int-Feld belegt darin vier Byte, und in welcher Reihenfolge
		   die im Speicher liegen, weiss nur das BACKEND (68k big-endian,
		   ARM64 little-endian). Der Feldoffset geht symbolisch durch
		   (tcEmitNum), damit der Zeigeranteil erhalten bleibt. */
		if (initCount > 0) {
			int fi2;
			if (arrayLen > 0) {
				tcErrAt(start);
				fprintf(stderr, "initializer for an array of structs is not supported in this version\n");
				actionErrors++; return;
			}
			if (initCount > tcStructFieldCount[ssid]) {
				tcErrAt(start);
				fprintf(stderr, "too many values in struct initializer (struct has %d fields)\n", tcStructFieldCount[ssid]);
				actionErrors++; return;
			}
			for (fi2 = 0; fi2 < initCount; fi2++) {
				TCType gft = tcStructFieldTypes[ssid][fi2];
				if (gft.base == 's' && !gft.pointers) {
					tcErrAt(start);
					fprintf(stderr, "struct field in an initializer list is not supported in this version\n");
					actionErrors++; return;
				}
				if (tcStructFieldArrayLen[ssid][fi2] > 0) {
					tcErrAt(start);
					fprintf(stderr, "array field in an initializer list is not supported in this version\n");
					actionErrors++; return;
				}
				printf("GINITAT %s ", name);
				tcEmitFieldOffset(ssid, fi2);
				printf(" %c %ld\n", tcTypeTag(gft), tcTruncInit(gft, initValues[fi2]));
			}
		}
		return;
	}
	if (arrayLen) {
		printf("GARRAY %s %c %d %d\n", name, tcTypeTag(type), arrayLen, isStatic);
		for (i = 0; i < initCount; i++) {
			/* GINITADDR statt GINIT, wo ein String-Literal stand: der Wert
			   ist dann die ADRESSE des anonymen __strN und steht erst zur
			   Ladezeit fest (OS-9 relokiert sie ueber M$IRefs). */
			if (initSyms[i] >= 0) printf("GINITADDR %s %d __str%d\n", name, i, initSyms[i]);
			else printf("GINIT %s %d %ld\n", name, i, tcTruncInit(type, initValues[i]));
		}
		return;
	}
	if (!type.pointers && type.base == 'c') value &= 255;
	if (!type.pointers && type.base == 'h') value &= 65535;
	if (!type.pointers && type.base == 'b') value = value ? 1 : 0;
	printf("GLOBAL %s %ld %c %d\n", name, value, tcTypeTag(type), isStatic);
	if (ptrStrSym >= 0) printf("GINITADDR %s 0 __str%d\n", name, ptrStrSym);
}

/* "extern <typ> <name>;" -- Deklaration OHNE Speicherallokation, definiert in
   einer ANDEREN QCC-Datei. Bewusst EIGENE Aktion (nicht tc_globalend) --
   die Rohtext-Form unterscheidet sich (kein optionales static/const, keine
   Array-Groesse, kein Initialisierer) und die Semantik ist grundverschieden
   (keine Speicherallokation, kein GLOBAL/GARRAY, sondern GLOBALDECL). Siehe
   docs/SELFHOSTING_LUECKENLISTE.md / docs/STATUS.md fuer die Motivation
   (Mehrdatei-Uebersetzung). */
void tc_externglobaldecl(const char* start, const char* end) {
	const char* p = start; const char* ne; char name[32]; TCType type = tcMakeType('i', 0); int n = 0, td;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (end - p >= 6 && p[0] == 'e' && p[1] == 'x' && p[2] == 't' && p[3] == 'e' && p[4] == 'r' && p[5] == 'n') {
		p += 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (end - p >= 8 && strncmp(p, "unsigned", 8) == 0) {
		const char* q; const char* qe;
		p += 8; while (p < end && (*p == ' ' || *p == '\t')) p++;
		q = p; qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) { type.base = 'c'; p = qe; }
		else if (tcEqSpan(q, qe, "short")) { type.base = 'h'; p = qe; }
		else if (tcEqSpan(q, qe, "int") || tcEqSpan(q, qe, "long")) { type.base = 'u'; p = qe; }
		else type.base = 'u';
	}
	/* "signed" (2026-09-16): dieselbe Folgewort-Pruefung wie bei "unsigned"
	   weiter oben, nur bleibt der Typ vorzeichenbehaftet. Vorher meldete
	   "signed char g;" ein "bad global declaration". */
	else if (end - p >= 6 && p[0] == 's' && p[1] == 'i' && p[2] == 'g' && p[3] == 'n' && p[4] == 'e' && p[5] == 'd') {
		const char* sq; const char* sqe;
		p += 6;
		sq = tcSkipWs(p, end); sqe = tcWordEnd(sq, end);
		if (tcEqSpan(sq, sqe, "char")) { type.base = 'c'; p = sqe; }
		else if (tcEqSpan(sq, sqe, "short")) { type.base = 'h'; p = sqe; }
		else if (tcEqSpan(sq, sqe, "int") || tcEqSpan(sq, sqe, "long")) { type.base = 'i'; p = sqe; }
		else type.base = 'i';
	}
	else if (end - p >= 3 && p[0] == 'i' && p[1] == 'n' && p[2] == 't') p += 3;
	/* "long double" vor dem blossen "long" pruefen (2026-09-16): sonst
	   schluckt "long" den Typ und der double-Anteil landet als
	   Deklaratortext. C89 laesst zu, dass long double dieselbe Genauigkeit
	   hat wie double -- genau so wird es hier behandelt. */
	else if (end - p >= 11 && p[0] == 'l' && p[1] == 'o' && p[2] == 'n' && p[3] == 'g' &&
	         (p[4] == ' ' || p[4] == '\t') && tcEqSpan(tcSkipWs(p + 4, end), tcWordEnd(tcSkipWs(p + 4, end), end), "double")) {
		p = tcWordEnd(tcSkipWs(p + 4, end), end); type.base = 'd';
	}
	else if (end - p >= 4 && p[0] == 'l' && p[1] == 'o' && p[2] == 'n' && p[3] == 'g') p += 4;
	else if (end - p >= 5 && p[0] == 's' && p[1] == 'h' && p[2] == 'o' && p[3] == 'r' && p[4] == 't') { p += 5; type.base = 'h'; }
	else if (end - p >= 4 && p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'r') { p += 4; type.base = 'c'; }
	else if (end - p >= 4 && p[0] == 'b' && p[1] == 'o' && p[2] == 'o' && p[3] == 'l') { p += 4; type.base = 'b'; }
	else if (end - p >= 4 && p[0] == 'v' && p[1] == 'o' && p[2] == 'i' && p[3] == 'd') { p += 4; type.base = 'v'; }
	/* double (2026-09-16) -- fehlte an BEIDEN Stellen, waehrend dieselbe
	   Deklaration als LOKALE laengst ging: "double g;" scheiterte mit "bad
	   global declaration". Die Basistyp-Erkennung steht in tcGlobalOne und
	   in tc_externglobaldecl getrennt -- wer nur eine anfasst, baut die
	   naechste stille Abweichung ein. */
	else if (end - p >= 6 && p[0] == 'd' && p[1] == 'o' && p[2] == 'u' && p[3] == 'b' && p[4] == 'l' && p[5] == 'e') { p += 6; type.base = 'd'; }
	else {
		ne = tcWordEnd(p, end); td = tcLookupTypedef(p, ne);
		if (td < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad extern global declaration\n"); return; }
		type = tcTypedefTypes[td]; p = ne;
	}
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p == '*') { type = tcPointerTo(type); p++; while (p < end && (*p == ' ' || *p == '\t')) p++; }
	if (!tcIsPointer(type) && type.base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++; return;
	}
	while (p < end && *p != ' ' && *p != '\t' && *p != ';' && n < 31) name[n++] = *p++;
	name[n] = 0;
	if (tcLookupGlobal(name, name + n) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate global '%s'\n", name); actionErrors++; return; }
	if (tcGlobalCount >= MAX_GLOBALS) { tcErrAt(start); fprintf(stderr, "too many globals\n"); actionErrors++; return; }
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
		count = tcInitList(start, end, values, 0, 256, 0);
		/* Die Rueckgabewerte auseinanderhalten, statt alles als "bad or
		   oversized" zu melden: -3 heisst String-Literal in der Liste, -4 eine
		   Verschachtelung, und "zu viele Werte" ist nochmal etwas anderes. Bis
		   2026-09-15 bekam man fuer alle drei denselben Satz, der bei keinem
		   die Ursache traf. */
		if (count == -3) { actionErrors++; tcErrAt(start); fprintf(stderr, "string literal in an initializer list is only supported for a global in this version\n"); return; }
		if (count == -4) { actionErrors++; tcErrAt(start); fprintf(stderr, "nested initializer list is only supported for a global in this version\n"); return; }
		if (count < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "malformed array initializer\n"); return; }
		if (count > tcLocalArrayLen[slot]) { actionErrors++; tcErrAt(start); fprintf(stderr, "too many values in array initializer (array holds %d)\n", tcLocalArrayLen[slot]); return; }
		for (i = 0; i < count; i++) printf("PUSH %d\nPUSH %ld\nSTOREIDX L %d %c\n", i, tcTruncInit(tcLocalTypes[slot], values[i]), slot, tcTypeTag(tcLocalTypes[slot]));
		return;
	}
	/* Hierher kommt auch "int a[] = {1,2,3};" -- ein ARRAY ohne angegebene
	   Groesse, das oben mangels tcLocalArrayLen nicht als Array erkannt wurde.
	   "scalar cannot use array initializer" allein schickte in die falsche
	   Richtung; der Zusatz nennt den Fall, der praktisch gemeint ist. */
	/* STRUCT MIT INITIALISIERERLISTE (2026-09-16): "struct S s = {1,2};".
	   Bis dahin kam hier "scalar cannot use array initializer" -- die Meldung
	   sah den Fall gar nicht. Umgesetzt wird er als Folge von Feld-Stores,
	   genau wie eine Kette von "s.a=1; s.b=2;": Feldoffset auf die
	   Blockadresse addieren, Wert speichern.
	   C89 3.5.7: weniger Werte als Felder sind erlaubt, der Rest bleibt
	   uninitialisiert (bei einer LOKALEN Variablen ist das ohnehin der
	   Normalfall). Mehr Werte als Felder sind ein Fehler. */
	{
		TCType listType = tcLocalType(slot);
		if (!listType.pointers && listType.base == 's' && tcLocalArrayLen[slot] == 0) {
			int lsid = listType.structId - 1;
			int lcount = tcInitList(start, end, values, 0, 256, 0);
			if (lcount >= 0) {
				int li;
				if (lsid < 0 || lsid >= tcStructCount) {
					actionErrors++; tcErrAt(start);
					fprintf(stderr, "initializer for an unknown struct type\n");
					return;
				}
				if (lcount > tcStructFieldCount[lsid]) {
					actionErrors++; tcErrAt(start);
					fprintf(stderr, "too many values in struct initializer (struct has %d fields)\n", tcStructFieldCount[lsid]);
					return;
				}
				for (li = 0; li < lcount; li++) {
					TCType lft = tcStructFieldTypes[lsid][li];
					if (lft.base == 's' && !lft.pointers) {
						actionErrors++; tcErrAt(start);
						fprintf(stderr, "struct field in an initializer list is not supported in this version\n");
						return;
					}
					if (tcStructFieldArrayLen[lsid][li] > 0) {
						actionErrors++; tcErrAt(start);
						fprintf(stderr, "array field in an initializer list is not supported in this version\n");
						return;
					}
					{ printf("PUSH "); tcEmitFieldOffset(lsid, li); printf("\n"); }
					printf("PUSHADDR L %d\nIPADD c\n", slot);
					printf("PUSH %ld\n", tcTruncInit(lft, values[li]));
					printf("STOREIND %c\n", tcTypeTag(lft));
				}
				return;
			}
		}
	}
	if (tcInitList(start, end, values, 0, 256, 0) >= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "scalar cannot use array initializer -- a local array needs a declared size here, as in \"int a[3] = {1,2,3};\"\n"); return; }
	{ TCType got = tcTypePop(), wanted = tcLocalType(slot); got = tcCoerceToTarget(wanted, got); if (!tcCompatible(wanted, got)) tcTypeError("initializer", wanted, got); }
	{
		/* Struct-Initialisierung: kopieren statt skalar speichern.  Ohne
		   diesen Zweig landet die Quelladresse als 4-Byte-Wert im Slot und
		   jeder spaetere Feldzugriff liest daneben -- lautlos. */
		TCType initType = tcLocalType(slot);
		if (initType.base == 's' && !initType.pointers) {
			int sid = initType.structId - 1;
			if (sid < 0 || sid >= tcStructCount) {
				tcErrAt(start); fprintf(stderr, "initializer for an unknown struct type\n");
				actionErrors++;
			} else if (slot < 0) {
				tcErrAt(start); fprintf(stderr, "struct initializer without a slot\n");
				actionErrors++;
			} else {
				tcEmitStructCopy(slot, 0, tcStructByteSizeK[sid], tcStructByteSizeN[sid]);
			}
			return;
		}
	}
	printf("STORE%s %d\n", tcIsPointer(tcLocalType(slot)) ? "P" : tcWordSuffixL(tcTypeTag(tcLocalType(slot))), slot);
}

void tc_number(const char* start, const char* end) {
	char base = 'i';
	const char* p = end;
	while (p > start && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L')) p--;
	if (p < end && (*p == 'u' || *p == 'U')) base = 'u';
	if (end - start == 4 && start[0] == 't') { printf("PUSH 1\n"); tcTypePush4('b', 0, 0, 0); }
	else if (end - start == 5 && start[0] == 'f') { printf("PUSH 0\n"); tcTypePush4('b', 0, 0, 0); }
	else { long value = tcNum(start, end); printf("PUSH %ld\n", value); tcTypePush4(value == 0 ? 'z' : base, 0, 0, 0); }
}

/* Escapes eines String-Literals (start zeigt auf das oeffnende, end hinter das
   schliessende Anfuehrungszeichen) nach bytes[] dekodieren: \n \t \r \0 sowie
   \" und \\ (Fallback fuer unbekannte \x: x woertlich). Gemeinsam genutzt von
   tc_string (normaler Ausdruckskontext) UND tc_arrayinitstring (Array-
   Initialisierer) -- gleiche Regeln, ein Ort. */
static int tcDecodeStringLit(const char* start, const char* end, unsigned char* bytes, int cap) {
	int len = 0;
	const char* q = start;
	/* MEHRERE LITERALE HINTEREINANDER sind EIN String (C89 3.1.4):
	   "ab" "cd" ergibt "abcd". Die uebergebene Spanne umfasst alle Teile
	   samt Leerraum und Zeilenumbruechen dazwischen -- deshalb hier eine
	   aeussere Schleife ueber die Teile; dekodiert wird jeder wie bisher
	   und das Ergebnis aneinandergehaengt.
	   Bis 2026-09-15 nahm diese Funktion GENAU EIN Paar Anfuehrungszeichen
	   (start+1 bis end-1), und die Grammatik liess auch nur eines zu -- ein
	   zweites Literal war ein STILLER Parse-Abbruch ohne Zeilenangabe. Die
	   eigenen Werkzeuge mussten lange Meldungstexte deshalb einzeilig
	   schreiben. */
	while (q < end) {
		const char* p;
		const char* stop;
		while (q < end && *q != '"') q++;
		if (q >= end) break;
		p = q + 1;
		stop = p;
		while (stop < end && *stop != '"') { if (*stop == '\\' && stop + 1 < end) stop++; stop++; }
	while (p < stop) {
		unsigned char c;
		if (*p == '\\' && p + 1 < stop) {
			char esc = p[1]; p += 2;
			/* Hex- ("\x09") und Oktal-Escapes ("\011"): ohne sie wurden die
			   Ziffern als EINZELNE Zeichen mitgezaehlt, was bei einer festen
			   Arraygroesse als "string literal too long" scheiterte -- und bei
			   offener Groesse eine falsche Laenge ergeben haette. */
			if (esc == 'x' || esc == 'X') {
				int v = 0, n = 0;
				while (p < stop && n < 2) {
					int d;
					if (*p >= '0' && *p <= '9') d = *p - '0';
					else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
					else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
					else break;
					v = v * 16 + d; p++; n++;
				}
				c = (unsigned char)v;
			} else if (esc >= '0' && esc <= '7') {
				int v = esc - '0', n = 1;
				while (p < stop && n < 3 && *p >= '0' && *p <= '7') { v = v * 8 + (*p++ - '0'); n++; }
				c = (unsigned char)v;
			}
			else c = esc == 'n' ? 10 : esc == 't' ? 9 : esc == 'r' ? 13 : (unsigned char)esc;
		} else {
			c = (unsigned char)*p++;
		}
		if (len >= cap) { tcErrAt(start); fprintf(stderr, "string literal too long (max %d bytes)\n", cap); actionErrors++; break; }
		bytes[len++] = c;
	}
		q = stop < end ? stop + 1 : end;
	}
	return len;
}
/* String-Literal: erzeugt einen anonymen globalen char-Array-Konstant (__strN, GARRAY/
   GINIT + nullterminierendes Byte) und liefert dessen Adresse (ADDRG) als char* --
   dieselben IR-Opcodes wie ein initialisiertes globales char-Array, siehe Grammatik-
   Kommentar bei stringLit. */
/* Legt das anonyme globale char-Array fuer ein String-Literal an und liefert
   seine Nummer. Getrennt von tc_string, weil ein String-Literal seit
   2026-09-15 auch in einer INITIALISIERERLISTE stehen darf
   (char *tab[] = {"a","b"}) -- dort wird die Adresse nicht auf den Stapel
   gelegt (ADDRG), sondern als GINITADDR in die Daten geschrieben. */
static int tcEmitStringGlobal(const char* start, const char* end) {
	unsigned char bytes[256]; int len; int i; int id;
	len = tcDecodeStringLit(start, end, bytes, 256);
	id = tcStringCounter++;
	printf("GARRAY __str%d c %d 1\n", id, len + 1);
	for (i = 0; i < len; i++) printf("GINIT __str%d %d %d\n", id, i, bytes[i]);
	printf("GINIT __str%d %d 0\n", id, len);
	return id;
}
void tc_string(const char* start, const char* end) {
	int id;
	id = tcEmitStringGlobal(start, end);
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
		tcErrAt(start); fprintf(stderr, "string literal initializer requires a char array\n"); actionErrors++; return;
	}
	len = tcDecodeStringLit(start, end, bytes, 256);
	if (len > tcLocalArrayLen[slot]) { tcErrAt(start); fprintf(stderr, "string literal too long for array\n"); actionErrors++; return; }
	(void)tcTypePop();
	printf("DROP\n");
	for (i = 0; i < len; i++) printf("PUSH %d\nPUSH %d\nSTOREIDX L %d c\n", i, bytes[i], slot);
	if (len < tcLocalArrayLen[slot]) printf("PUSH %d\nPUSH 0\nSTOREIDX L %d c\n", len, slot);
}

void tc_neg(const char* start, const char* end) {
	TCType operand = tcTypePop();
	(void)end;
	if (*start == '!') {
		operand = tcCondValue(operand);
		if (!tcIsTruthy(operand)) tcTypeError("logical negation", tcMakeType('b', 0), operand);
		tcTypePush(tcMakeType('b', 0)); printf("NOT\n");
	} else if (*start == '~') {
		if (!tcIsInteger(operand)) tcTypeError("bitwise negation", tcMakeType('i', 0), operand);
		tcTypePush(tcMakeType(operand.base == 'u' ? 'u' : 'i', 0)); printf("NOTBIT\n");
	} else if (tcIsDouble(operand)) {
		tcTypePush(operand); printf("DNEG\n");
	} else {
		if (!tcIsInteger(operand)) tcTypeError("negation", tcMakeType('i', 0), operand);
		tcTypePush(tcMakeType(operand.base == 'u' ? 'u' : 'i', 0)); printf("NEG\n");
	}
}

void tc_varref(const char* start, const char* end) {
	/* identEnd = genaues Namensende (fuer Lookup/Diagnose), nameEnd = danach bis
	   zum naechsten Nicht-Leerraum vorgespult (fuer alle "was kommt als Naechstes"-
	   Pruefungen unten, s. tcNameEnd/tcSkipWs). "s . a"/"s[ 0]" etc. brauchen beide
	   getrennt: der Name darf den Leerraum nicht enthalten, die Fortsetzung muss ihn
	   ueberspringen koennen. */
	const char* identEnd = tcNameEnd(start, end);
	const char* nameEnd = tcSkipWs(identEnd, end); int indexed = nameEnd < end;
	int slot = tcLookupLocal(start, identEnd), global;
	TCType globalType, globalPointee;
	tcCopy(tcDiagVarRef, start, end);
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
	/* VERKETTETE MEMBER-ZUGRIFFE (2026-09-16). Die Grammatik liest "p->n->v"
	   seit heute -- vorher brach der Parser mit "FAIL" ab, ohne ein Wort.
	   Der Codegen kann die Kette aber NICHT, und die Zweige weiter unten
	   ignorieren den zweiten Member STILLSCHWEIGEND: gemessen kam statt des
	   Wertes die ADRESSE heraus. Genau davor warnt der Kommentar oben.
	   Deshalb hier melden -- mit dem Weg, der funktioniert.
	   Gezaehlt wird nur auf der OBERSTEN Ebene: in "a[b.c].d" gehoert der
	   erste Punkt zum Index, nicht zur Kette. */
	{
		const char* mp = start; int mcount = 0; int mdepth = 0;
		while (mp < end) {
			if (*mp == '[') mdepth++;
			else if (*mp == ']') mdepth--;
			else if (mdepth == 0) {
				if (*mp == '.') mcount++;
				else if (*mp == '-' && mp + 1 < end && mp[1] == '>') { mcount++; mp++; }
			}
			mp++;
		}
		if (mcount > 1 && mdepth == 0 && tcEmitMemberChain(start, end, slot, 0)) return;
		if (mcount > 1) {
			tcErrAt(start);
			fprintf(stderr, "chained member access is not supported in this form -- use an intermediate variable, as in \"q = p->n; q->v\"\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
	}
	global = slot < 0 ? tcLookupGlobal(start, identEnd) : -1;
	/* TCType ist ein 4-Byte-Wert. Nicht einen structwertigen Funktionsrueckgabewert
	   direkt als Argument an tcPointee weiterreichen: der Bootstrap-68k-Pfad kann
	   diese verschachtelte Uebergabe nicht korrekt materialisieren. */
	globalType = global >= 0 ? tcGlobalTypes[global] : tcMakeType('i', 0);
	globalPointee = globalType;
	if (globalPointee.pointers) globalPointee.pointers--; else globalPointee = tcMakeType('i', 0);
	if (indexed && *nameEnd == '[' &&
	    !(slot >= 0 && tcLocalArrayLen[slot] > 0 && tcLocalTypes[slot].base == 's') &&
	    !(slot >= 0 && tcLocalTypes[slot].pointers && tcLocalTypes[slot].base == 's') &&
	    !(global >= 0 && tcGlobalArrayLen[global] > 0 && globalType.base == 's') &&
	    !(global >= 0 && globalType.pointers && globalPointee.base == 's')) {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			tcErrAt(start); fprintf(stderr, "indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct, in this version\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
	}
	/* ptr[i].feld (2026-07-25, Milestone B): eine LOKALE Pointer-auf-struct-Variable,
	   indiziert, dann Feldzugriff -- braucht der Selfhosting-Pilot fuer routinesC[i].name/
	   .text (ActionRoutine*, ein malloc/realloc-gewachsenes Array, kein festes lokales
	   Array wie beim bereits fertigen arr[i].feld oben). Stack-Reihenfolge identisch zum
	   Array-Fall, nur PUSHADDR (Blockadresse) durch LOADP (geladener Pointer-WERT) ersetzt --
	   IPADDN skaliert genauso um die Laufzeit-Byte-Groesse des Elements. */
	if (slot >= 0 && indexed && *nameEnd == '[' && tcLocalTypes[slot].pointers && tcLocalTypes[slot].base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcLocalTypes[slot].structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (chain) tcStashChainedIndex();
			{ printf("LOADP %d\nIPADDN ", slot); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) {
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) { tcUnstashChainedIndex(); tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); }
				else tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1));
				return;
			}
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
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcLocalTypes[slot].structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (tcCheckNDIndex(tcLocalArrayNDims[slot], tcLocalArrayDims[slot], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcLocalArrayLen[slot]);
			if (chain) tcStashChainedIndex();
			/* Index bereits gepusht (vor uns, durch die index-ACTION). PUSHADDR liefert die
			   Blockadresse des GESAMTEN Arrays; IPADDN skaliert den Index um die LAUFZEIT-
			   Byte-Groesse eines Elements (structSize, beliebig -- anders als IPADD, das nur
			   feste Typtag-Groessen kennt); danach PUSH+PADD c addiert den (konstanten,
			   byte-genauen) Feldoffset, exakt wie beim bestehenden Skalar-Feldzugriff oben. */
			{ printf("PUSHADDR L %d\nIPADDN ", slot); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) {
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) { tcUnstashChainedIndex(); tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); }
				else tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1));
				return;
			}
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
		TCType pt;
		const char* fieldStart = tcSkipWs(nameEnd + 2, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
		int sid, fi;
		/* Kein Fehler, wenn die Basis nicht passt: der Zweig wird allein am
		   Rohtext ("-" gefolgt von ">") erkannt, und ein Fehlalarm darf keine
		   falsche Meldung erzeugen -- dann uebernehmen die regulaeren Zweige.
		   Ein echtes "p->f" mit falscher Basis faellt weiter unten ohnehin als
		   "unknown variable" bzw. Typfehler auf. */
		if (slot >= 0) pt = tcLocalTypes[slot];
		else if (global >= 0) pt = tcGlobalTypes[global];
		else goto tcArrowSkip;
		if (!pt.pointers || pt.base != 's') goto tcArrowSkip;
		sid = pt.structId - 1;
		fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
		{
			int chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\n"); }
			if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", tcGlobalNames[global]);
			printf("IPADD c\n");
			if (chain) { tcTypePush(tcEmitFieldRowColIndex(sid, fi, 1)); return; }
		}
		if (fieldEnd < end && *fieldEnd == '[') {
			/* VERKETTETE Indizierung eines Strukturfelds (feld[i][j]) --
			   seit 2026-09-07 GEMELDET statt still. Vorher nahm die Grammatik
			   die Form gar nicht an (member hatte nur EINEN optionalen
			   index), und ein Parse-Abbruch hat keine Meldung: bei einem
			   2D-Feld im struct ("char t[4][8]") war damit die
			   NATUERLICHSTE Zugriffsform ein stilles FAIL.
			   Umgesetzt ist sie weiterhin nicht -- dafuer braeuchte es zwei
			   Indizes in einem Ausdruck, also einen zweiten Index-Scratch wie
			   in tcEmitPointerIndexChain; dieselbe Maschinerie wie fuer
			   arr[i].feld[j]. Der Zugriff auf eine ZEILE geht dagegen:
			   "z = sp->t[i]" liefert den Zeiger darauf. */
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				/* Ein ZEIGERfeld laesst sich indizieren -- durch den Zeiger
				   hindurch (2026-09-07). Ein Feld ohne Arraylaenge und ohne
				   Zeigertyp bleibt ein Skalar und wird gemeldet. */
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
					tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				/* ZWEIDIMENSIONALES Feld: "x->args[i]" adressiert ZEILE i, liefert also
				   einen ZEIGER auf deren erstes Element -- kein LOADIND. Der Sprung geht
				   um rowLen ELEMENTE, deshalb IPADDN mit der Zeilengroesse in Bytes
				   (IPADD kennt nur feste Typtag-Groessen). */
				int elemSize = tcElemByteSize(tcStructFieldTypes[sid][fi]);
				tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi] / tcStructFieldRowLen[sid][fi]);
				printf("IPADDN %d\n", tcStructFieldRowLen[sid][fi] * elemSize);
				tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				/* Bewusst NICHT umgesetzt (kommt im Bootstrap-Ziel nicht vor): lieber
				   diagnostizieren als still mit falscher Schrittweite rechnen. */
				tcErrAt(start); fprintf(stderr, "indexing a two-dimensional struct field is only supported via '->' in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	tcArrowSkip: ;
	if (slot >= 0 && indexed && *nameEnd == '.' && tcLocalTypes[slot].base == 's') {
		int sid = tcLocalTypes[slot].structId - 1;
		const char* fieldStart = tcSkipWs(nameEnd + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
		/* IPADD poppt Pointer ZUERST (muss oben liegen), dann Count -- daher PUSH vor PUSHADDR. */
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			if (tcLocalStructByAddr[slot])
				{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\nLOADP %d\nIPADD c\n", slot); }
			else
				{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\nPUSHADDR L %d\nIPADD c\n", slot); }
			if (chain) { tcTypePush(tcEmitFieldRowColIndex(sid, fi, 1)); return; }
		}
		if (hasIndex) {
			/* p.field[i] (2026-07-24): der Index-Ausdruck hat seinen Wert bereits VOR uns
			   gepusht (ACTION AFTER index CALL tc_arg feuert vor dem umschliessenden
			   varRef) -- der Stack traegt hier [Indexwert, Feldadresse] (Feldadresse gerade
			   eben on top gepusht durch das IPADD oben), exakt die Reihenfolge, die ein
			   zweites IPADD braucht. */
			/* VERKETTETE Indizierung eines Strukturfelds (feld[i][j]) --
			   seit 2026-09-07 GEMELDET statt still. Vorher nahm die Grammatik
			   die Form gar nicht an (member hatte nur EINEN optionalen
			   index), und ein Parse-Abbruch hat keine Meldung: bei einem
			   2D-Feld im struct ("char t[4][8]") war damit die
			   NATUERLICHSTE Zugriffsform ein stilles FAIL.
			   Umgesetzt ist sie weiterhin nicht -- dafuer braeuchte es zwei
			   Indizes in einem Ausdruck, also einen zweiten Index-Scratch wie
			   in tcEmitPointerIndexChain; dieselbe Maschinerie wie fuer
			   arr[i].feld[j]. Der Zugriff auf eine ZEILE geht dagegen:
			   "z = sp->t[i]" liefert den Zeiger darauf. */
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				/* Ein ZEIGERfeld laesst sich indizieren -- durch den Zeiger
				   hindurch (2026-09-07). Ein Feld ohne Arraylaenge und ohne
				   Zeigertyp bleibt ein Skalar und wird gemeldet. */
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
					tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++;
				tcTypePush(tcBadType()); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				/* Bewusst NICHT umgesetzt (kommt im Bootstrap-Ziel nicht vor): lieber
				   diagnostizieren als still mit falscher Schrittweite rechnen. */
				tcErrAt(start); fprintf(stderr, "indexing a two-dimensional struct field is only supported via '->' in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
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
	if (global >= 0 && indexed && *nameEnd == '[' && globalType.pointers && globalPointee.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = globalPointee.structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			tcCopy(gname, start, identEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (chain) tcStashChainedIndex();
			{ printf("LOADGP %s\nIPADDN ", gname); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) { tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1)); return; }
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	if (global >= 0 && indexed && *nameEnd == '[' && tcGlobalArrayLen[global] > 0 && tcGlobalTypes[global].base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcGlobalTypes[global].structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			tcCopy(gname, start, identEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcGlobalArrayLen[global]);
			if (chain) tcStashChainedIndex();
			{ printf("PUSHADDR G %s\nIPADDN ", gname); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) { tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1)); return; }
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	if (global >= 0 && indexed && *nameEnd == '.' && tcGlobalTypes[global].base == 's') {
		char gname[32]; int sid = tcGlobalTypes[global].structId - 1;
		const char* fieldStart = tcSkipWs(nameEnd + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		tcCopy(gname, start, identEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\nPUSHADDR G %s\nIPADD c\n", gname); }
			if (chain) { tcTypePush(tcEmitFieldRowColIndex(sid, fi, 1)); return; }
		}
		if (hasIndex) {
			/* VERKETTETE Indizierung eines Strukturfelds (feld[i][j]) --
			   seit 2026-09-07 GEMELDET statt still. Vorher nahm die Grammatik
			   die Form gar nicht an (member hatte nur EINEN optionalen
			   index), und ein Parse-Abbruch hat keine Meldung: bei einem
			   2D-Feld im struct ("char t[4][8]") war damit die
			   NATUERLICHSTE Zugriffsform ein stilles FAIL.
			   Umgesetzt ist sie weiterhin nicht -- dafuer braeuchte es zwei
			   Indizes in einem Ausdruck, also einen zweiten Index-Scratch wie
			   in tcEmitPointerIndexChain; dieselbe Maschinerie wie fuer
			   arr[i].feld[j]. Der Zugriff auf eine ZEILE geht dagegen:
			   "z = sp->t[i]" liefert den Zeiger darauf. */
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				/* Ein ZEIGERfeld laesst sich indizieren -- durch den Zeiger
				   hindurch (2026-09-07). Ein Feld ohne Arraylaenge und ohne
				   Zeigertyp bleibt ein Skalar und wird gemeldet. */
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
					tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++;
				tcTypePush(tcBadType()); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				/* Bewusst NICHT umgesetzt (kommt im Bootstrap-Ziel nicht vor): lieber
				   diagnostizieren als still mit falscher Schrittweite rechnen. */
				tcErrAt(start); fprintf(stderr, "indexing a two-dimensional struct field is only supported via '->' in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	if (slot >= 0) {
		TCType localValueType = tcLocalTypes[slot];
		if (tcLocalArrayLen[slot]) {
			if (!indexed) {
				localValueType.pointers++; printf("PUSHADDR L %d\n", slot); tcTypePush(localValueType); return;
			}
			if (tcCheckNDIndex(tcLocalArrayNDims[slot], tcLocalArrayDims[slot], tcCountTopIndexes(nameEnd, end)) == 2) {
				printf("PUSHADDR L %d\nIPADD %c\n", slot, tcTypeTag(localValueType));
				localValueType.pointers++; tcTypePush(localValueType); return;
			}
			tcCheckConstIndex(start, end, tcLocalArrayLen[slot]);
			if (localValueType.base == 's' && !localValueType.pointers) {
				/* Ganze Struct aus einem Array: die ADRESSE des Elements, nicht
				   sein erstes Wort (siehe Patch 6). Der Index liegt bereits auf
				   dem Stapel, IPADDN skaliert ihn mit der Structgroesse. */
				printf("PUSHADDR L %d\nIPADDN ", slot);
				tcEmitStructSize(localValueType.structId - 1); printf("\n");
				TC_TYPE_PUSH(localValueType.base, localValueType.pointers,
				             localValueType.structId, localValueType.pointeeConst);
			} else {
			printf("LOADIDX L %d %c\n", slot, tcTypeTag(localValueType)); tcTypePush(localValueType);
			}
		} else if (indexed && tcIsPointer(localValueType)) {
			TCType valueType = tcEmitPointerIndexChain(slot, 0, localValueType.base, localValueType.pointers, localValueType.structId, localValueType.pointeeConst, tcCountTopIndexes(nameEnd, end));
			tcTypePush(valueType);
		} else if (indexed) { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; }
		else if (tcLocalTypes[slot].base == 's' && !tcLocalTypes[slot].pointers) {
			/* Eine Struct als Ganzes passt in kein Register.  Wie bei einem
			   Feld-/Array-Zugriff wird die Adresse abgelegt; die eigentliche
			   Kopie erzeugt tcAssignStore.  Vorher lief dieser Fall in den
			   'C'-Zweig darunter und lud EIN Byte. */
			if (tcLocalStructByAddr[slot]) printf("LOADP %d\n", slot);
			else printf("PUSHADDR L %d\n", slot);
			TC_TYPE_PUSH(tcLocalTypes[slot].base, tcLocalTypes[slot].pointers,
			             tcLocalTypes[slot].structId, tcLocalTypes[slot].pointeeConst);
		}
		else {
			/* 2026-09-09: direkter .base-Vergleich statt tcWordSuffixL(tcTypeTag(...))
			   -- war vorher 'i'/'u' -> "L" sonst "C", eine bislang uebersehene
			   dritte Fassung derselben Weiche (s. tcWordSuffix/tcWordSuffixL). */
			printf("LOAD%s %d\n", tcLocalTypes[slot].pointers ? "P" : tcWordSuffixL(tcTypeTag(tcLocalTypes[slot])), slot);
			TC_TYPE_PUSH(tcLocalTypes[slot].base, tcLocalTypes[slot].pointers,
			             tcLocalTypes[slot].structId, tcLocalTypes[slot].pointeeConst);
		}
	} else if ((global = tcLookupGlobal(start, identEnd)) >= 0) {
		char name[32]; TCType globalValueType = tcGlobalTypes[global]; tcCopy(name, start, identEnd);
		if (tcGlobalArrayLen[global]) {
			if (!indexed) {
				globalValueType.pointers++; printf("PUSHADDR G %s\n", name); tcTypePush(globalValueType); return;
			}
			if (tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, end)) == 2) {
				printf("PUSHADDR G %s\nIPADD %c\n", name, tcTypeTag(globalValueType));
				globalValueType.pointers++; tcTypePush(globalValueType); return;
			}
			tcCheckConstIndex(start, end, tcGlobalArrayLen[global]);
			if (globalValueType.base == 's' && !globalValueType.pointers) {
				/* siehe lokalen Zweig */
				printf("PUSHADDR G %s\nIPADDN ", name);
				tcEmitStructSize(globalValueType.structId - 1); printf("\n");
				TC_TYPE_PUSH(globalValueType.base, globalValueType.pointers,
				             globalValueType.structId, globalValueType.pointeeConst);
			} else {
			printf("LOADIDX G %s %c\n", name, tcTypeTag(globalValueType)); tcTypePush(globalValueType);
			}
		} else if (indexed && tcIsPointer(globalValueType)) {
			/* Ein einfacher Pointerindex ist der Bootstrap-Hauptpfad. Direkt
			   emittieren statt TCType durch eine weitere Funktionsgrenze zu geben. */
			TCType valueType = globalValueType;
			char valueTag;
			valueType.pointers = valueType.pointers - 1;
			valueTag = tcTypeTag(valueType);
			/* Dieselbe Regel wie in tcEmitPtrIndexChain: bei einer ganzen
			   Struct traegt IPADDN die echte Byte-Groesse und laesst die
			   Adresse stehen. PTRINDEX/LOADIND wuerden mit vier Byte
			   schreiten und den Inhalt als Adresse weitergeben. */
			if (tcIsWholeStruct(&valueType))
				{ printf("LOADGP %s\nIPADDN ", name); tcEmitStructSize(valueType.structId - 1); printf("\n"); }
			else
				printf("LOADGP %s\nPTRINDEX %c\nLOADIND %c\n", name, valueTag, valueTag);
			if (tcValueDepth < 256) tcValueTypes[tcValueDepth++] = valueType; else actionErrors++;
		} else if (indexed) { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; }
		else if (globalValueType.base == 's' && !globalValueType.pointers) {
			/* Wie im lokalen Fall (siehe dort): eine Struct als Ganzes wird als
			   Adresse weitergegeben, tcAssignStore macht daraus die Kopie. */
			printf("ADDRG %s\n", name);
			TC_TYPE_PUSH(globalValueType.base, globalValueType.pointers,
			             globalValueType.structId, globalValueType.pointeeConst);
		}
		else { printf("LOADG%s %s\n", tcIsPointer(globalValueType) ? "P" : tcWordSuffix(tcTypeTag(globalValueType)), name); tcTypePush(globalValueType); }
	}
	else {
		int ec = tcLookupEnumConst(start, identEnd);
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
		else if (!indexed && (fnv = tcLookupFunction2(start, identEnd)) >= 0) {
			int sig = tcFnSigForFunction(fnv);
			printf("PUSHFN %s\n", tcFunctionNames[fnv]);
			tcTypePush(sig >= 0 ? tcMakeFnPtr(sig) : tcBadType());
		}
		else { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(identEnd - start), start); actionErrors++; }
	}
}

void tc_addressref(const char* start, const char* end) {
	const char* name = start + 1;
	/* identEnd/nameEnd: siehe tc_varref -- derselbe Leerraum-Grund ("& a [0]"). */
	const char* identEnd = tcNameEnd(name, end);
	const char* nameEnd = tcSkipWs(identEnd, end); int indexed = nameEnd < end;
	int slot = tcLookupLocal(name, identEnd), global = tcLookupGlobal(name, identEnd); TCType valueType;
	if (slot >= 0) {
		valueType = tcLocalType(slot);
		if (indexed) {
			if (tcLocalArrayLen[slot]) { tcCheckConstIndex(name, end, tcLocalArrayLen[slot]); printf("PUSHADDR L %d\n", slot); }
			else if (tcIsPointer(valueType)) { valueType = tcPointee(valueType); printf("LOADP %d\n", slot); }
			else { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; return; }
			tcEmitElemIndexStep(valueType); tcTypePush(tcPointerTo(valueType)); return;
		}
		/* Eine skalare Struct und ein double liegen ebenfalls als Block vor
		   (LARRAY, s. tc_localdecl), haben aber tcLocalArrayLen 0 -- ohne
		   tcLocalIsBlock liefert "&x" die Adresse eines leeren Skalarslots
		   statt die des Objekts. */
		if (tcLocalIsBlock(slot, valueType))
			printf("PUSHADDR L %d\n", slot);
		else printf("ADDRL %d\n", slot);
		tcTypePush(tcPointerTo(valueType)); return;
	}
	if (global >= 0) {
		char globalName[32]; valueType = tcGlobalType(global); tcCopy(globalName, name, identEnd);
		if (indexed) {
			if (tcGlobalArrayLen[global]) { tcCheckConstIndex(name, end, tcGlobalArrayLen[global]); printf("PUSHADDR G %s\n", globalName); }
			else if (tcIsPointer(valueType)) { valueType = tcPointee(valueType); printf("LOADGP %s\n", globalName); }
			else { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; return; }
			tcEmitElemIndexStep(valueType); tcTypePush(tcPointerTo(valueType)); return;
		}
		printf("ADDRG %s\n", globalName); tcTypePush(tcPointerTo(valueType)); return;
	}
	tcErrAt(start); fprintf(stderr, "unknown variable in address expression: '%.*s'\n", (int)(identEnd - name), name); actionErrors++;
}

void tc_derefref(const char* start, const char* end) {
	TCType pointer = tcTypePop(), valueType; (void)start; (void)end;
	if (!tcIsPointer(pointer)) { tcTypeError("dereference", tcPointerTo(tcMakeType('i', 0)), pointer); tcTypePush(tcMakeType('i', 0)); return; }
	valueType = tcPointee(pointer);
	if (!tcIsPointer(valueType) && valueType.base == 'v') {
		tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++;
		tcTypePush(tcMakeType('i', 0)); return;
	}
	/* "*p" auf eine ganze Struct liefert deren ADRESSE, kein LOADIND
	   (s. tcIsWholeStruct). Genau hier lief der selbstuebersetzte Compiler
	   am 2026-09-14 in einen PMMU-Fault: die vier Bytes einer TCType
	   ('i',0,0,0) landeten als Adresse $69000000 in A0. */
	if (tcIsWholeStruct(&valueType)) { tcTypePush(valueType); return; }
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
		tcErrAt(start); fprintf(stderr, "cannot index void*\n"); actionErrors++;
		tcTypePush(tcMakeType('i', 0)); return;
	}
	/* Dritte Stelle derselben Regel (s. tcIsWholeStruct): eine ganze Struct
	   bleibt eine Adresse. PADD erwartet den Pointer ZUERST, IPADDN zuletzt --
	   deshalb SWAP davor. */
	if (tcIsWholeStruct(&valueType)) {
		{ printf("SWAP\nIPADDN "); tcEmitStructSize(valueType.structId - 1); printf("\n"); }
		tcTypePush(valueType);
		return;
	}
	printf("PADD %c\nLOADIND %c\n", tcTypeTag(valueType), tcTypeTag(valueType)); tcTypePush(valueType);
}

void tc_callmember(const char* start, const char* end) {
	/* call [callMember]: tc_call hat den Rueckgabewert bereits auf den
	   Wert-/Typstack gelegt. Der Ausdruck ist damit anders als p->field nicht
	   ueber einen lokalen Slot erreichbar, sondern der Pointer liegt direkt
	   auf dem Laufzeitstack. PADD erwartet [pointer, offset]. */
	const char* fieldStart; const char* fieldEnd;
	int viaPtr, sid, fi, hasIndex;
	TCType bt, ft;
	if (start >= end) { tcErrAt(start); fprintf(stderr, "bad call member\n"); actionErrors++; tcTypePush(tcBadType()); return; }
	viaPtr = (*start == '-');
	fieldStart = start + (viaPtr ? 2 : 1);
	fieldEnd = tcWordEnd(fieldStart, end);
	bt = tcTypePop();
	if (viaPtr) {
		if (!tcIsPointer(bt) || tcPointee(bt).base != 's') {
			tcErrAt(start); fprintf(stderr, "'->' requires a function returning pointer to struct\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
		sid = tcPointee(bt).structId - 1;
	} else {
		if (tcIsPointer(bt) || bt.base != 's') {
			tcErrAt(start); fprintf(stderr, "'.' requires a function returning struct\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
		sid = bt.structId - 1;
	}
	fi = tcLookupStructField(sid, fieldStart, fieldEnd);
	if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
	ft = tcStructFieldTypes[sid][fi];
	hasIndex = fieldEnd < end && *fieldEnd == '[';
	if (hasIndex && !tcStructFieldArrayLen[sid][fi]) {
		tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	if (hasIndex) {
		if (tcStructFieldRowLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "two-dimensional field access after a function call is not supported yet\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
		tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
		/* Der Indexwert liegt bereits ueber dem Rueckgabe-Pointer. Erst
		   SWAP/PADD den Feldoffset auf den Pointer anwenden, dann den Index
		   wieder nach oben holen und das Arrayelement adressieren. */
		{ printf("SWAP\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\nSWAP\n"); }
		printf("PADD %c\nLOADIND %c\n", tcTypeTag(ft), tcTypeTag(ft));
		tcTypePush(ft); return;
	}
	{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
	if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(ft)); return; }
	printf("LOADIND %c\n", tcTypeTag(ft));
	tcTypePush(ft);
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
		} else if (tcIsDouble(left) && tcIsDouble(right)) {
			/* GLEITKOMMA (2026-09-16). Gemischt mit Ganzzahlen geht noch
			   nicht: der linke Operand liegt beim Emittieren schon unter
			   dem rechten auf dem Stapel, eine Konversion an dieser Stelle
			   brauchte also entweder SWAP (mit zwei verschiedenen Breiten
			   heikel) oder eigene Opcodes wie die "fadd.l"-Form der FPU.
			   Bis das entschieden ist, lieber eine Meldung als ein stiller
			   Fehlgriff -- s. docs/FLOAT_IR_ENTWURF_de.md. */
			printf("%s", tcPendingAdd == '+' ? "DADD\n" : "DSUB\n");
			tcTypePush(left);
			/* UEBLICHE ARITHMETISCHE KONVERSIONEN (C89 3.2.1.5): ist ein
			   Operand double, wird der andere hochgezogen. Welcher Opcode
			   dafuer taugt, haengt an der Stapellage -- der LINKE Operand
			   liegt beim Emittieren schon UNTER dem rechten, deshalb gibt
			   es neben I2D auch I2DUNDER. */
		} else if (tcIsDouble(left) && (tcIsInteger(right) || tcIsBool(right))) {
			printf("I2D\n%s", tcPendingAdd == '+' ? "DADD\n" : "DSUB\n");
			tcTypePush(left);
		} else if ((tcIsInteger(left) || tcIsBool(left)) && tcIsDouble(right)) {
			printf("I2DUNDER\n%s", tcPendingAdd == '+' ? "DADD\n" : "DSUB\n");
			tcTypePush(right);
		} else if (tcIsDouble(left) || tcIsDouble(right)) {
			tcErrAt(start);
			fprintf(stderr, "arithmetic between double and ");
			tcPrintType(stderr, tcIsDouble(left) ? right : left);
			fprintf(stderr, " is not defined\n");
			actionErrors++;
			tcTypePush(tcIsDouble(left) ? left : right);
		} else if (leftVoidPtr || rightVoidPtr) {
			tcErrAt(start); fprintf(stderr, "arithmetic on void* is not supported\n"); actionErrors++; tcTypePush(left);
		} else {
			tcTypeError("arithmetic", left, right); tcTypePush(left);
		}
		tcPendingAdd = 0;
	}
}

/* Ein geklammerter Ausdruck darf die verzoegerten Operatoren des
   UMSCHLIESSENDEN Ausdrucks nicht sehen. QCC emittiert "*"/"+" und Vergleiche
   naemlich nicht sofort, sondern gemerkt (tcPendingMul/tcPendingAdd/tcRel0/
   tcRel1) und wendet sie in tc_factor NACH JEDEM Faktor an -- also auch nach
   dem ersten Faktor INNERHALB der Klammern. "x * (a + b)" erzeugte dadurch
   "LOADL x / LOADL a / MUL / LOADL b / ADD", rechnete also (x*a)+b statt
   x*(a+b): stiller Falschcode ohne jede Meldung. Dasselbe Rette-und-nulle-
   Muster wie bei Aufrufen (tc_callname) und Indizes (tc_arg). */
void tc_parenbegin(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcParenDepth >= 64) { tcErrAt(start); fprintf(stderr, "parenthesis nesting too deep\n"); actionErrors++; return; }
	tcParenSavedAdd[tcParenDepth] = tcPendingAdd;
	tcParenSavedMul[tcParenDepth] = tcPendingMul;
	tcParenSavedRel0[tcParenDepth] = tcRel0;
	tcParenSavedRel1[tcParenDepth] = tcRel1;
	/* Zielzustand mitsichern -- eine Zuweisung als Komma-Glied wuerde ihn
	   sonst dem umschliessenden Ausdruck wegnehmen (siehe Deklaration). */
	tcParenSavedTgtSlot[tcParenDepth] = tcTargetSlot;
	tcCopy(tcParenSavedTgtGlobal[tcParenDepth], tcTargetGlobal, tcTargetGlobal + strlen(tcTargetGlobal));
	tcParenSavedTgtIsGlobal[tcParenDepth] = tcTargetIsGlobal;
	tcParenSavedTgtType[tcParenDepth] = tcTargetType;
	tcParenSavedTgtIsArray[tcParenDepth] = tcTargetIsArray;
	tcParenSavedTgtIndirect[tcParenDepth] = tcTargetIndirect;
	tcCopy(tcParenSavedAssignOp[tcParenDepth], tcAssignOp, tcAssignOp + strlen(tcAssignOp));
	tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0;
	tcParenDepth++;
}

/* Stellt die geretteten Operatoren wieder her -- feuert VOR tc_factor der
   umschliessenden Klammer, sodass dort das richtige "*" angewendet wird. */
void tc_parenend(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcParenDepth <= 0) { tcErrAt(start); fprintf(stderr, "parenthesis-frame mismatch\n"); actionErrors++; return; }
	tcParenDepth--;
	tcPendingAdd = tcParenSavedAdd[tcParenDepth];
	tcPendingMul = tcParenSavedMul[tcParenDepth];
	tcRel0 = tcParenSavedRel0[tcParenDepth];
	tcRel1 = tcParenSavedRel1[tcParenDepth];
	tcTargetSlot = tcParenSavedTgtSlot[tcParenDepth];
	tcCopy(tcTargetGlobal, tcParenSavedTgtGlobal[tcParenDepth],
	       tcParenSavedTgtGlobal[tcParenDepth] + strlen(tcParenSavedTgtGlobal[tcParenDepth]));
	tcTargetIsGlobal = tcParenSavedTgtIsGlobal[tcParenDepth];
	tcTargetType = tcParenSavedTgtType[tcParenDepth];
	tcTargetIsArray = tcParenSavedTgtIsArray[tcParenDepth];
	tcTargetIndirect = tcParenSavedTgtIndirect[tcParenDepth];
	tcCopy(tcAssignOp, tcParenSavedAssignOp[tcParenDepth],
	       tcParenSavedAssignOp[tcParenDepth] + strlen(tcParenSavedAssignOp[tcParenDepth]));
}

/* KOMMA-OPERATOR (2026-08-11). Vier winzige Aktionen, ein Merker.

   tcCommaHasValue sagt, ob das ZULETZT abgeschlossene Kommaglied einen Wert
   auf dem Operandenstapel hinterlassen hat. Eine Zuweisung tut das nicht:
   tc_assign poppt den Wert und gibt STORE aus. Ein gewoehnlicher Ausdruck tut
   es. tc_commadrop, das nach dem Komma-Zeichen feuert, muss deshalb den Wert
   des VORANGEHENDEN Glieds nur dann verwerfen, wenn es einen hatte.

   EIN Merker genuegt trotz Verschachtelung ("((x = 1, 2), 3)"): die Aktionen
   feuern beim Replay strikt von links nach rechts, und die Aktion eines
   Glieds feuert NACH allen Aktionen in seinem Inneren. Der Merker beschreibt
   damit immer genau das letzte abgeschlossene Glied der gerade offenen Ebene.
   Ein Stapel waere hier Beiwerk ohne Wirkung. */
void tc_commavalue(const char* start, const char* end) {
	(void)start; (void)end;
	tcCommaHasValue = 1;
}

void tc_commaassign(const char* start, const char* end) {
	/* Eine Zuweisung ist in C selbst ein Wertausdruck.  Im Komma-Ausdruck darf
	   sie den zugewiesenen Wert deshalb NICHT wie eine Anweisung verbrauchen:
	   "(x = 1)" und "(x = 1, y)" sind beide gueltig.  tcAssignStore(1)
	   dupliziert den fertig berechneten Wert vor dem Store und laesst die Kopie
	   auf dem Wertestapel; normale assignStmt verwendet weiterhin (0). */
	(void)start; (void)end;
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcCompoundAssign();
	tcAssignStore(1);
	tcCommaHasValue = 1;
}

void tc_commadrop(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcCommaHasValue) {
		printf("DROP\n");
		(void)tcTypePop();
	}
	tcCommaHasValue = 0;
}

void tc_commaend(const char* start, const char* end) {
	(void)start; (void)end;
	/* BEWUSSTE GRENZE: das LETZTE Glied muss einen Wert liefern, denn der
	   geklammerte Ausdruck ist ein factor und wird als Wert weiterverwendet.
	   "(x = 1)" waere in C erlaubt (Wert ist der zugewiesene), hier ist es ein
	   diagnostizierter Fehler -- lieber laut als ein Faktor ohne Wert, der den
	   Operandenstapel unter dem umgebenden Ausdruck wegzieht. */
	if (!tcCommaHasValue) {
		actionErrors++;
		tcErrAt(start); fprintf(stderr, "a comma expression must end in a value, not an assignment\n");
		tcTypePush(tcBadType());
		tcCommaHasValue = 1;
	}
}

void tc_mulop(const char* start, const char* end) {
	(void)end;
	tcPendingMul = *start;
}

void tc_factor(const char* start, const char* end) {
	(void)end;
	if (tcPendingMul) {
		TCType right = tcTypePop(), left = tcTypePop();
		/* GLEITKOMMA (2026-09-16): "%" gibt es fuer double nicht -- C89
		   3.3.5 verlangt dort ganzzahlige Operanden. */
		if (tcIsDouble(left) && tcIsDouble(right)) {
			if (tcPendingMul == '%') {
				tcErrAt(start);
				fprintf(stderr, "the remainder operator requires integer operands\n");
				actionErrors++;
			} else {
				printf("%s", tcPendingMul == '*' ? "DMUL\n" : "DDIV\n");
			}
			tcTypePush(left);
			tcPendingMul = 0;
			return;
		}
		if (tcIsDouble(left) || tcIsDouble(right)) {
			int rightIsInt = tcIsInteger(right) || tcIsBool(right);
			int leftIsInt = tcIsInteger(left) || tcIsBool(left);
			if (tcPendingMul == '%') {
				tcErrAt(start);
				fprintf(stderr, "the remainder operator requires integer operands\n");
				actionErrors++;
			} else if (tcIsDouble(left) && rightIsInt) {
				printf("I2D\n%s", tcPendingMul == '*' ? "DMUL\n" : "DDIV\n");
			} else if (leftIsInt && tcIsDouble(right)) {
				printf("I2DUNDER\n%s", tcPendingMul == '*' ? "DMUL\n" : "DDIV\n");
			} else {
				tcErrAt(start);
				fprintf(stderr, "arithmetic between double and ");
				tcPrintType(stderr, tcIsDouble(left) ? right : left);
				fprintf(stderr, " is not defined\n");
				actionErrors++;
			}
			tcTypePush(tcIsDouble(left) ? left : right);
			tcPendingMul = 0;
			return;
		}
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
	(void)end;
	if (tcRel0) {
		TCType right, left; int pointerCompare, leftInteger, rightInteger, leftBool, rightBool;
		TC_TYPE_POP(right); TC_TYPE_POP(left);
		pointerCompare = tcIsPointer(left) || tcIsPointer(right);
		/* GLEITKOMMA (2026-09-16): eigene Vergleichsopcodes, weil ein
		   double 8 Byte breit ist und die ganzzahligen CMPxx darauf nicht
		   passen. Gemischt mit Ganzzahlen gilt dasselbe wie bei der
		   Arithmetik: gemeldet statt geraten. */
		if (!pointerCompare && (tcIsDouble(left) || tcIsDouble(right))) {
			if (tcIsDouble(left) && (tcIsInteger(right) || tcIsBool(right))) printf("I2D\n");
			else if ((tcIsInteger(left) || tcIsBool(left)) && tcIsDouble(right)) printf("I2DUNDER\n");
			if (!tcIsDouble(left) && !(tcIsInteger(left) || tcIsBool(left))) {
				tcTypeError("comparison", left, right);
			} else if (!tcIsDouble(right) && !(tcIsInteger(right) || tcIsBool(right))) {
				tcTypeError("comparison", left, right);
			} else if (tcRel1 == '=') {
				if (tcRel0 == '<') printf("DCMPLE\n");
				else if (tcRel0 == '>') printf("DCMPGE\n");
				else if (tcRel0 == '=') printf("DCMPEQ\n");
				else if (tcRel0 == '!') printf("DCMPNE\n");
			} else {
				if (tcRel0 == '<') printf("DCMPLT\n");
				else if (tcRel0 == '>') printf("DCMPGT\n");
			}
			TC_TYPE_PUSH('b', 0, 0, 0);
			tcRel0 = 0; tcRel1 = 0;
			return;
		}
		leftInteger = tcIsInteger(left);
		rightInteger = tcIsInteger(right);
		leftBool = !left.pointers && left.base == 'b';
		rightBool = !right.pointers && right.base == 'b';
		if (pointerCompare && !(tcSameType(left, right) || (tcIsPointer(left) && right.base == 'z' && !right.pointers) || (tcIsPointer(right) && left.base == 'z' && !left.pointers))) tcTypeError("pointer comparison", left, right);
		else if (!pointerCompare && !((leftInteger || leftBool) && (rightInteger || rightBool))) tcTypeError("comparison", left, right);
		TC_TYPE_PUSH('b', 0, 0, 0);
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
	const char* nameEnd;
	const char* identEnd;
	int global;
	/* VERKETTETES ZIEL (2026-09-16): "p->n->v = 7". Dieselbe Kette wie beim
	   Lesen, nur bleibt die ADRESSE stehen -- gespeichert wird ueber
	   tcTargetIndirect. Gezaehlt wird wieder nur auf der obersten Ebene,
	   damit der Punkt in "t[s.i]" nicht mitzaehlt. */
	{
		const char* mp = start; int mcount = 0; int mdepth = 0;
		while (mp < end) {
			if (*mp == '[') mdepth++;
			else if (*mp == ']') mdepth--;
			else if (mdepth == 0) {
				if (*mp == '.') mcount++;
				else if (*mp == '-' && mp + 1 < end && mp[1] == '>') { mcount++; mp++; }
			}
			mp++;
		}
		if (mcount > 1 && mdepth == 0) {
			int cslot = tcLookupLocal(start, tcNameEnd(start, end));
			if (tcEmitMemberChain(start, end, cslot, 1)) {
				tcTargetType = tcChainFieldType;
				tcTargetIndirect = 1;
				tcTargetSlot = -1;
				tcTargetIsGlobal = 0;
				tcTargetIsArray = 0;
				return;
			}
			tcErrAt(start);
			fprintf(stderr, "chained member access is not supported in this form -- use an intermediate variable, as in \"q = p->n; q->v = x\"\n");
			actionErrors++;
			return;
		}
	}
	/* bisheriges Ziel retten -- die Kettenzuweisung braucht beide (siehe tc_chainassign) */
	tcPrevTargetSlot = tcTargetSlot;
	tcPrevTargetIsGlobal = tcTargetIsGlobal;
	tcPrevTargetType = tcTargetType;
	tcPrevTargetIsArray = tcTargetIsArray;
	tcPrevTargetIndirect = tcTargetIndirect;
	tcCopy(tcPrevTargetGlobal, tcTargetGlobal, tcTargetGlobal + strlen(tcTargetGlobal));

	/* identEnd/nameEnd: siehe tc_varref -- derselbe Leerraum-Grund ("s .a"/"s. a"/"a [0]"). */
	identEnd = tcNameEnd(start, end);
	nameEnd = tcSkipWs(identEnd, end);
	tcTargetIsArray = nameEnd < end;
	tcTargetSlot = tcLookupLocal(start, identEnd);
	tcTargetIsGlobal = 0; tcTargetIndirect = 0;
	tcTargetType = tcTargetSlot >= 0 ? tcLocalType(tcTargetSlot) : tcMakeType('i', 0);
	if (tcTargetSlot >= 0 && tcLocalConst[tcTargetSlot]) {
		tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++;
	}
	/* siehe tc_varref fuer die vollstaendige Erklaerung -- die Grammatik erlaubt jetzt
	   Kombinationen (z.B. "ptr[i].feld = ..") ohne Codegen-Unterstuetzung; ohne diesen
	   Schutz wuerden die Index-Zweige weiter unten die "."-Fortsetzung stillschweigend
	   ignorieren statt sauber zu diagnostizieren. */
	{
		int targetGlobal = tcTargetSlot < 0 ? tcLookupGlobal(start, identEnd) : -1;
		TCType targetGlobalType = targetGlobal >= 0 ? tcGlobalTypes[targetGlobal] : tcMakeType('i', 0);
		TCType targetGlobalPointee = targetGlobalType;
		if (targetGlobalPointee.pointers) targetGlobalPointee.pointers--; else targetGlobalPointee = tcMakeType('i', 0);
		if (tcTargetIsArray && *nameEnd == '[' &&
		    !(tcTargetSlot >= 0 && tcLocalArrayLen[tcTargetSlot] > 0 && tcTargetType.base == 's') &&
		    /* TCType ist im Bootstrap-ABI kein sicher verschachtelter Rueckgabewert:
		       bei einem Pointer bleibt die Basis beim Dereferenzieren unveraendert. */
		    !(tcTargetSlot >= 0 && tcTargetType.pointers && tcTargetType.base == 's') &&
		    !(targetGlobal >= 0 && tcGlobalArrayLen[targetGlobal] > 0 && targetGlobalType.base == 's') &&
		    !(targetGlobal >= 0 && targetGlobalType.pointers && targetGlobalPointee.base == 's')) {
			const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
			if (afterIdx < end && *afterIdx == '.') {
				tcErrAt(start); fprintf(stderr, "indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct, in this version\n");
				actionErrors++; return;
			}
		}
	}
	/* ptr[i].feld = .. (2026-07-25, Milestone B): siehe tc_varref fuer die Erklaerung.
	   LOADP statt PUSHADDR, sonst identisch zum Array-Fall (tcTargetIndirect=1). */
	/* p->feld = .. (2026-08-10): Gegenstueck zum Lesezweig in tc_varref.
	   Feldadresse = Zeigerwert + Feldoffset, ohne Index; tcTargetIndirect=1
	   laesst tc_assign daraus ein STOREIND machen. */
	if (tcTargetIsArray && *nameEnd == '-' && nameEnd + 1 < end && nameEnd[1] == '>') {
		int gslot = tcTargetSlot < 0 ? tcLookupGlobal(start, identEnd) : -1;
		TCType pt;
		const char* fieldStart = tcSkipWs(nameEnd + 2, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
		int sid, fi;
		/* siehe tc_varref: bei nicht passender Basis nicht melden, durchfallen.
		   Kein tcPointee(pt): der Bootstrap kann dessen Struct-Rueckgabe nicht
		   direkt als Argument/Feldzugriff transportieren. */
		if (tcTargetSlot >= 0) pt = tcTargetType;
		else if (gslot >= 0) pt = tcGlobalTypes[gslot];
		else goto tcArrowSkipT;
		if (!pt.pointers || pt.base != 's') goto tcArrowSkipT;
		sid = pt.structId - 1;
		fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
		/* SCHREIBEN AUF EIN const-FELD (2026-09-07). Bei einem
		   ZEIGERfeld ist nur der Pointee konstant -- "s.cp = b" bleibt
		   also erlaubt, und tcStructFieldConst ist dort 0; das Schreiben
		   DURCH den Zeiger prueft tcEmitPtrFieldIndex. Hier geht es um
		   das Feld selbst: "const int n" mit "s.n = 1", und ebenso ein
		   Arrayfeld aus const-Elementen. Beides lief bis 2026-09-07
		   STILL durch, waehrend dasselbe bei einer Variablen gemeldet
		   wird. */
		if (tcStructFieldConst[sid][fi]) {
			tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
			actionErrors++;
		}
		{
			int chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\n"); }
			if (tcTargetSlot >= 0) printf("LOADP %d\n", tcTargetSlot); else printf("LOADGP %s\n", tcGlobalNames[gslot]);
			printf("IPADD c\n");
			if (chain) { tcTargetType = tcEmitFieldRowColIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
		}
		if (fieldEnd < end && *fieldEnd == '[') {
			/* VERKETTETE Indizierung eines Strukturfelds (feld[i][j]) --
			   seit 2026-09-07 GEMELDET statt still. Vorher nahm die Grammatik
			   die Form gar nicht an (member hatte nur EINEN optionalen
			   index), und ein Parse-Abbruch hat keine Meldung: bei einem
			   2D-Feld im struct ("char t[4][8]") war damit die
			   NATUERLICHSTE Zugriffsform ein stilles FAIL.
			   Umgesetzt ist sie weiterhin nicht -- dafuer braeuchte es zwei
			   Indizes in einem Ausdruck, also einen zweiten Index-Scratch wie
			   in tcEmitPointerIndexChain; dieselbe Maschinerie wie fuer
			   arr[i].feld[j]. Der Zugriff auf eine ZEILE geht dagegen:
			   "z = sp->t[i]" liefert den Zeiger darauf. */
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				/* Zuweisungsziel durch ein ZEIGERfeld hindurch (2026-09-07). */
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
								tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
					tcTargetIndirect = 1;
					return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
		} else if (tcStructFieldArrayLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "cannot assign to array field\n"); actionErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	tcArrowSkipT: ;
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '[' && tcTargetType.pointers && tcTargetType.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcTargetType.structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			/* SCHREIBEN AUF EIN const-FELD (2026-09-07). Bei einem
			   ZEIGERfeld ist nur der Pointee konstant -- "s.cp = b" bleibt
			   also erlaubt, und tcStructFieldConst ist dort 0; das Schreiben
			   DURCH den Zeiger prueft tcEmitPtrFieldIndex. Hier geht es um
			   das Feld selbst: "const int n" mit "s.n = 1", und ebenso ein
			   Arrayfeld aus const-Elementen. Beides lief bis 2026-09-07
			   STILL durch, waehrend dasselbe bei einer Variablen gemeldet
			   wird. */
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (chain) tcStashChainedIndex();
			{ printf("LOADP %d\nIPADDN ", tcTargetSlot); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) {
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) { tcUnstashChainedIndex(); tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0); }
				else tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0);
				tcTargetIndirect = 1; return;
			}
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	/* arr[i].feld = .. (2026-07-25): siehe tc_varref fuer die vollstaendige Erklaerung
	   (Allokations-Fix, IPADDN, tcSkipOneIndex). Schreibend identisch bis auf
	   tcTargetIndirect=1 statt LOADIND (wie beim bestehenden p.field = ..-Pfad). */
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '[' && tcLocalArrayLen[tcTargetSlot] > 0 && tcTargetType.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcTargetType.structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			/* SCHREIBEN AUF EIN const-FELD (2026-09-07). Bei einem
			   ZEIGERfeld ist nur der Pointee konstant -- "s.cp = b" bleibt
			   also erlaubt, und tcStructFieldConst ist dort 0; das Schreiben
			   DURCH den Zeiger prueft tcEmitPtrFieldIndex. Hier geht es um
			   das Feld selbst: "const int n" mit "s.n = 1", und ebenso ein
			   Arrayfeld aus const-Elementen. Beides lief bis 2026-09-07
			   STILL durch, waehrend dasselbe bei einer Variablen gemeldet
			   wird. */
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (tcCheckNDIndex(tcLocalArrayNDims[tcTargetSlot], tcLocalArrayDims[tcTargetSlot], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcLocalArrayLen[tcTargetSlot]);
			if (chain) tcStashChainedIndex();
			{ printf("PUSHADDR L %d\nIPADDN ", tcTargetSlot); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) {
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) { tcUnstashChainedIndex(); tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0); }
				else tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0);
				tcTargetIndirect = 1; return;
			}
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '.' && tcTargetType.base == 's') {
		int sid = tcTargetType.structId - 1;
		const char* fieldStart = tcSkipWs(nameEnd + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
		/* SCHREIBEN AUF EIN const-FELD (2026-09-07). Bei einem
		   ZEIGERfeld ist nur der Pointee konstant -- "s.cp = b" bleibt
		   also erlaubt, und tcStructFieldConst ist dort 0; das Schreiben
		   DURCH den Zeiger prueft tcEmitPtrFieldIndex. Hier geht es um
		   das Feld selbst: "const int n" mit "s.n = 1", und ebenso ein
		   Arrayfeld aus const-Elementen. Beides lief bis 2026-09-07
		   STILL durch, waehrend dasselbe bei einer Variablen gemeldet
		   wird. */
		if (tcStructFieldConst[sid][fi]) {
			tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
			actionErrors++;
		}
		/* Wie tc_varref: IPADD poppt Pointer ZUERST, daher PUSH vor PUSHADDR. tcTargetIndirect=1
		   laesst tc_assign/tcLoadTarget denselben STOREIND/DUPP+LOADIND-Pfad wie bei einer
		   echten Pointer-Dereferenz nehmen -- die Feldadresse liegt bereits auf dem Stack. */
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			if (tcLocalStructByAddr[tcTargetSlot])
				{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\nLOADP %d\nIPADD c\n", tcTargetSlot); }
			else
				{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\nPUSHADDR L %d\nIPADD c\n", tcTargetSlot); }
			if (chain) { tcTargetType = tcEmitFieldRowColIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
		}
		if (hasIndex) {
			/* p.field[i] = .. (2026-07-24): siehe tc_varref -- der Index-Ausdruck hat seinen
			   Wert bereits VOR uns gepusht, ein zweites IPADD kombiniert Feldadresse+Index. */
			/* VERKETTETE Indizierung eines Strukturfelds (feld[i][j]) --
			   seit 2026-09-07 GEMELDET statt still. Vorher nahm die Grammatik
			   die Form gar nicht an (member hatte nur EINEN optionalen
			   index), und ein Parse-Abbruch hat keine Meldung: bei einem
			   2D-Feld im struct ("char t[4][8]") war damit die
			   NATUERLICHSTE Zugriffsform ein stilles FAIL.
			   Umgesetzt ist sie weiterhin nicht -- dafuer braeuchte es zwei
			   Indizes in einem Ausdruck, also einen zweiten Index-Scratch wie
			   in tcEmitPointerIndexChain; dieselbe Maschinerie wie fuer
			   arr[i].feld[j]. Der Zugriff auf eine ZEILE geht dagegen:
			   "z = sp->t[i]" liefert den Zeiger darauf. */
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				/* Zuweisungsziel durch ein ZEIGERfeld hindurch (2026-09-07). */
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
								tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
					tcTargetIndirect = 1;
					return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "cannot assign to array field\n"); actionErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	/* Globale structs (2026-07-25): dieselben drei Muster wie in tc_varref, schreibend --
	   siehe dort fuer die vollstaendige Erklaerung. tcTargetIndirect=1 laesst tc_assign
	   denselben STOREIND-Pfad nehmen wie bei den bereits vorhandenen lokalen Faellen. */
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, identEnd)) >= 0 && tcTargetIsArray && *nameEnd == '[' &&
	    tcGlobalTypes[global].pointers) {
		TCType globalPointee = tcGlobalTypes[global];
		globalPointee.pointers--;
		if (globalPointee.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = globalPointee.structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			tcCopy(gname, start, identEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			/* SCHREIBEN AUF EIN const-FELD (2026-09-07). Bei einem
			   ZEIGERfeld ist nur der Pointee konstant -- "s.cp = b" bleibt
			   also erlaubt, und tcStructFieldConst ist dort 0; das Schreiben
			   DURCH den Zeiger prueft tcEmitPtrFieldIndex. Hier geht es um
			   das Feld selbst: "const int n" mit "s.n = 1", und ebenso ein
			   Arrayfeld aus const-Elementen. Beides lief bis 2026-09-07
			   STILL durch, waehrend dasselbe bei einer Variablen gemeldet
			   wird. */
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (chain) tcStashChainedIndex();
			{ printf("LOADGP %s\nIPADDN ", gname); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) {
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
				else tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0);
				tcTargetIndirect = 1; return;
			}
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		}
	}
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, identEnd)) >= 0 && tcTargetIsArray && *nameEnd == '[' &&
	    tcGlobalArrayLen[global] > 0 && tcGlobalTypes[global].base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcGlobalType(global).structId - 1;
			const char* fieldStart = tcSkipWs(afterIdx + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int chain;
			tcCopy(gname, start, identEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			/* SCHREIBEN AUF EIN const-FELD (2026-09-07). Bei einem
			   ZEIGERfeld ist nur der Pointee konstant -- "s.cp = b" bleibt
			   also erlaubt, und tcStructFieldConst ist dort 0; das Schreiben
			   DURCH den Zeiger prueft tcEmitPtrFieldIndex. Hier geht es um
			   das Feld selbst: "const int n" mit "s.n = 1", und ebenso ein
			   Arrayfeld aus const-Elementen. Beides lief bis 2026-09-07
			   STILL durch, waehrend dasselbe bei einer Variablen gemeldet
			   wird. */
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 &&
			        (tcStructFieldArrayLen[sid][fi] > 0 || tcIsPointer(tcStructFieldTypes[sid][fi]));
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcGlobalArrayLen[global]);
			if (chain) tcStashChainedIndex();
			{ printf("PUSHADDR G %s\nIPADDN ", gname); tcEmitStructSize(sid); printf("\nPUSH "); tcEmitFieldOffset(sid, fi); printf("\nPADD c\n"); }
			if (chain) {
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
				else tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0);
				tcTargetIndirect = 1; return;
			}
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, identEnd)) >= 0 && tcTargetIsArray && *nameEnd == '.' &&
	    tcGlobalTypes[global].base == 's') {
		char gname[32]; int sid = tcGlobalTypes[global].structId - 1;
		const char* fieldStart = tcSkipWs(nameEnd + 1, end); const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		tcCopy(gname, start, identEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
		/* SCHREIBEN AUF EIN const-FELD (2026-09-07). Bei einem
		   ZEIGERfeld ist nur der Pointee konstant -- "s.cp = b" bleibt
		   also erlaubt, und tcStructFieldConst ist dort 0; das Schreiben
		   DURCH den Zeiger prueft tcEmitPtrFieldIndex. Hier geht es um
		   das Feld selbst: "const int n" mit "s.n = 1", und ebenso ein
		   Arrayfeld aus const-Elementen. Beides lief bis 2026-09-07
		   STILL durch, waehrend dasselbe bei einer Variablen gemeldet
		   wird. */
		if (tcStructFieldConst[sid][fi]) {
			tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
			actionErrors++;
		}
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			{ printf("PUSH "); tcEmitFieldOffset(sid, fi); printf("\nPUSHADDR G %s\nIPADD c\n", gname); }
			if (chain) { tcTargetType = tcEmitFieldRowColIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
		}
		if (hasIndex) {
			/* VERKETTETE Indizierung eines Strukturfelds (feld[i][j]) --
			   seit 2026-09-07 GEMELDET statt still. Vorher nahm die Grammatik
			   die Form gar nicht an (member hatte nur EINEN optionalen
			   index), und ein Parse-Abbruch hat keine Meldung: bei einem
			   2D-Feld im struct ("char t[4][8]") war damit die
			   NATUERLICHSTE Zugriffsform ein stilles FAIL.
			   Umgesetzt ist sie weiterhin nicht -- dafuer braeuchte es zwei
			   Indizes in einem Ausdruck, also einen zweiten Index-Scratch wie
			   in tcEmitPointerIndexChain; dieselbe Maschinerie wie fuer
			   arr[i].feld[j]. Der Zugriff auf eine ZEILE geht dagegen:
			   "z = sp->t[i]" liefert den Zeiger darauf. */
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				/* Zuweisungsziel durch ein ZEIGERfeld hindurch (2026-09-07). */
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
								tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
					tcTargetIndirect = 1;
					return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "cannot assign to array field\n"); actionErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	if (tcTargetSlot < 0 && tcLookupGlobal(start, identEnd) >= 0) {
		int global = tcLookupGlobal(start, identEnd);
		tcCopy(tcTargetGlobal, start, identEnd);
		tcTargetIsGlobal = 1;
		tcTargetType = tcGlobalTypes[global];
		if (tcGlobalConst[global]) { tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++; }
		if (tcTargetIsArray && tcGlobalArrayLen[global]) {
			tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, end));
			tcCheckConstIndex(start, end, tcGlobalArrayLen[global]);
		}
		else if (tcTargetIsArray && tcIsPointer(tcTargetType)) {
			/* const char **pp: *pp ist ein veraenderbarer char*-Slot; erst **pp
			   trifft auf das const char. Das kompakte TCType-Modell merkt die
			   Qualifikation nur an der Basis, deshalb gilt sie nur auf Ebene 1. */
			if (tcTargetType.pointeeConst && tcTargetType.pointers == 1) { tcErrAt(start); fprintf(stderr, "cannot assign through pointer to const\n"); actionErrors++; }
			if (tcTargetType.pointers) tcTargetType.pointers--; else tcTargetType = tcMakeType('i', 0);
			if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
				tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++; tcTargetType = tcMakeType('i', 0);
			}
			tcTargetIndirect = 1; printf("LOADGP %s\nPTRINDEX %c\n", tcTargetGlobal, tcTypeTag(tcTargetType));
		} else if (!!tcGlobalArrayLen[global] != tcTargetIsArray) { tcErrAt(start); fprintf(stderr, "array index mismatch\n"); actionErrors++; }
	} else if (tcTargetSlot >= 0 && tcTargetIsArray && tcLocalArrayLen[tcTargetSlot]) {
		tcCheckNDIndex(tcLocalArrayNDims[tcTargetSlot], tcLocalArrayDims[tcTargetSlot], tcCountTopIndexes(nameEnd, end));
		tcCheckConstIndex(start, end, tcLocalArrayLen[tcTargetSlot]);
	} else if (tcTargetSlot >= 0 && tcTargetIsArray && tcIsPointer(tcTargetType)) {
		if (tcTargetType.pointeeConst && tcTargetType.pointers == 1) { tcErrAt(start); fprintf(stderr, "cannot assign through pointer to const\n"); actionErrors++; }
		if (tcTargetType.pointers) tcTargetType.pointers--; else tcTargetType = tcMakeType('i', 0);
		if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
			tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++; tcTargetType = tcMakeType('i', 0);
		}
		tcTargetIndirect = 1; printf("LOADP %d\nPTRINDEX %c\n", tcTargetSlot, tcTypeTag(tcTargetType));
	} else if (tcTargetSlot >= 0 && tcTargetIsArray) {
		tcErrAt(start); fprintf(stderr, "array index mismatch\n"); actionErrors++;
	}
}

void tc_indirecttarget(const char* start, const char* end) {
	TCType pointer = tcTypePop(); (void)start; (void)end;
	tcTargetSlot = -1; tcTargetIsGlobal = 0; tcTargetIsArray = 0; tcTargetIndirect = 1;
	if (!tcIsPointer(pointer)) { tcTypeError("indirect assignment", tcPointerTo(tcMakeType('i', 0)), pointer); tcTargetType = tcMakeType('i', 0); return; }
	if (pointer.pointeeConst && pointer.pointers == 1) { tcErrAt(start); fprintf(stderr, "cannot assign through pointer to const\n"); actionErrors++; }
	tcTargetType = tcPointee(pointer);
	if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
		tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++;
		tcTargetType = tcMakeType('i', 0);
	}
}

void tc_assignop(const char* start, const char* end) {
	int n = 0; const char* p;
	for (p = start; p < end && n < 2; p++) tcAssignOp[n++] = *p;
	tcAssignOp[n] = 0;
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcLoadTarget();
}

/* "a = b = 0;" -- der Wert liegt einmal auf dem Stack und wird in BEIDE Ziele
   geschrieben (in C ist die Zuweisung ein Ausdruck, dessen Wert nach links
   weitergereicht wird). tc_target hat das erste Ziel nach tcPrevTarget*
   gerettet, das zweite steht in tcTarget*. Bewusst nur EINFACHE Ziele
   (Variable/Global, nicht indiziert, nicht ueber Zeiger) -- bei allem anderen
   liegt bereits Adressrechnung auf dem Stack, die hier nicht doppelt
   verwendet werden darf; das wird diagnostiziert statt still falsch gerechnet. */
void tc_chainassign(const char* start, const char* end) {
	TCType got; char tag;
	(void)start; (void)end;
	tcChainHandled = 1;
	/* a[i] = x = value: Der Index von a[i] liegt schon unter dem Wert auf dem
	   Laufzeitstack. Erst x speichern (DUP laesst den Wert fuer a[i]), danach
	   STOREIDX. Dasselbe Prinzip funktioniert fuer *p = x = value. */
	if (!tcTargetIndirect && !tcTargetIsArray && (tcPrevTargetIndirect || tcPrevTargetIsArray)) {
		got = tcTypePop();
		if (!tcCompatible(tcTargetType, got)) tcTypeError("assignment", tcTargetType, got);
		if (!tcCompatible(tcPrevTargetType, got)) tcTypeError("assignment", tcPrevTargetType, got);
		printf("DUP\n");
		tag = tcTypeTag(tcTargetType);
		if (tcTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffix(tag), tcTargetGlobal);
		else printf("STORE%s %d\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffixL(tag), tcTargetSlot);
		tag = tcTypeTag(tcPrevTargetType);
		if (tcPrevTargetIndirect) printf("STOREIND %c\n", tag);
		else if (tcPrevTargetIsGlobal) printf("STOREIDX G %s %c\n", tcPrevTargetGlobal, tag);
		else printf("STOREIDX L %d %c\n", tcPrevTargetSlot, tag);
		return;
	}
	if (tcTargetIndirect || tcTargetIsArray || tcPrevTargetIndirect || tcPrevTargetIsArray) {
		tcErrAt(start); fprintf(stderr, "chained assignment is only supported for plain variables in this version\n");
		actionErrors++; (void)tcTypePop(); return;
	}
	got = tcTypePop();
	if (!tcCompatible(tcTargetType, got)) tcTypeError("assignment", tcTargetType, got);
	if (!tcCompatible(tcPrevTargetType, got)) tcTypeError("assignment", tcPrevTargetType, got);
	printf("DUP\n");
	tag = tcTypeTag(tcTargetType);
	if (tcTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffix(tag), tcTargetGlobal);
	else printf("STORE%s %d\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffixL(tag), tcTargetSlot);
	tag = tcTypeTag(tcPrevTargetType);
	if (tcPrevTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcPrevTargetType) ? "P" : tcWordSuffix(tag), tcPrevTargetGlobal);
	else printf("STORE%s %d\n", tcIsPointer(tcPrevTargetType) ? "P" : tcWordSuffixL(tag), tcPrevTargetSlot);
}

void tc_assign(const char* start, const char* end) {
	const char* p; int hasAssign = 0;
	/* Kettenzuweisung hat bereits alles erledigt (siehe tc_chainassign). */
	if (tcChainHandled) { tcChainHandled = 0; return; }
	for (p = start; p < end; p++) if (*p == '=') { hasAssign = 1; break; }
	if (!hasAssign) { if (!tcLastWasPrint) { printf("DROP\n"); (void)tcTypePop(); } return; }
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcCompoundAssign();
	tcAssignStore(0);
}

/* Gemeinsamer letzter Schritt aller Zuweisungen.  leaveValue ist fuer eine
   Zuweisung IM Ausdruck gesetzt: DUP bewahrt dann eine Kopie fuer den
   umgebenden Ausdruck, waehrend STORE die andere wie gewohnt verbraucht. */
/* Struct-Zuweisung `x = y`.  Eine Struct ist hier ein char-Feld (siehe
   LARRAY ... c <groesse> in tc_localdecl), deshalb ist die Kopie eine
   schlichte Byteschleife: typunabhaengig und damit auch fuer verschachtelte
   Structs und Feld-Arrays richtig.  Die QUELLADRESSE liegt beim Aufruf oben
   auf dem Stapel -- tc_varref legt fuer eine Struct die Adresse ab statt
   eines Registerwerts.  Sie wird in einem Scratch-Global gesichert, weil sie
   fuer jedes Byte erneut gebraucht wird; DUP je Byte waere kuerzer, wuerde
   aber die Stapeltiefe an die Structgroesse koppeln. */
/* Beide Scratch-Globals einmalig deklarieren: die Quelladresse braucht
   jede Kopie, die Zieladresse nur die indizierte Variante. */
static void tcDeclStructCopyScratch(void) {
	if (!tcStructCopyScratchDeclared) {
		printf("GLOBAL __structCopySrc 0 i 1\n");
		printf("GLOBAL __structCopyDst 0 i 1\n");
		printf("GLOBAL __structCopyIdx 0 i 1\n");
		tcStructCopyScratchDeclared = 1;
	}
}
/* DIE BLOCKKOPIE WAR BIS 2026-09-15 ENTROLLT -- acht IR-Zeilen je Byte, fuer
   eine 28-Byte-Struct 237 Zeilen fuer EINE Zuweisung. Das ging nur, solange
   die Groesse zur Uebersetzungszeit eine Zahl war. Mit der zielabhaengigen
   Zeigergroesse (k+nP, s. tcEmitNum) ist sie das nicht mehr: erst das Backend
   kennt P, also muss die Zahl zur LAUFZEIT wirken.
   Die Schleife kommt ohne neuen IR-Opcode aus -- alles, was sie braucht,
   koennen LABEL/CMPLT/JZ/JMP schon --, und macht die IR nebenbei um zwei
   Groessenordnungen kleiner. */
static void tcEmitStructCopyLoop(int size, int sizeN) {
	int top;
	int done;
	int off;
	/* OHNE ZEIGERANTEIL IST DIE GROESSE EINE ZAHL -- dann wird weiter
	   entrollt, wie vor dem 15.09.2026. Das ist nicht nur schneller: die
	   Schleife braucht ZWEI LABELS je Kopie, und QCCs eigener Parser kopiert
	   1340 structs (alle zeigerfrei, TCType sind vier char-Felder). Mit
	   Schleife lief qr68s Symboltabelle ueber (SYM_MAX). Die Schleife ist
	   also nur fuer den Fall da, fuer den sie noetig ist: eine Groesse, die
	   erst das Backend kennt. */
	if (sizeN == 0) {
		for (off = 0; off < size; off++) {
			printf("PUSH %d\nLOADGP __structCopyDst\nIPADD c\n", off);
			printf("PUSH %d\nLOADGP __structCopySrc\nIPADD c\nLOADIND c\n", off);
			printf("STOREIND c\n");
		}
		return;
	}
	top = tcNextLabel++;
	done = tcNextLabel++;
	printf("PUSH 0\nSTOREG __structCopyIdx\n");
	printf("LABEL L%d\n", top);
	printf("LOADG __structCopyIdx\nPUSH ");
	tcEmitNum(size, sizeN);
	printf("\nCMPLT\nJZ L%d\n", done);
	/* Zieladresse + Index, dann Quellbyte -- dieselbe Stapelordnung wie die
	   entrollte Fassung: erst der Offset, dann die Basis, dann IPADD. */
	printf("LOADG __structCopyIdx\nLOADGP __structCopyDst\nIPADD c\n");
	printf("LOADG __structCopyIdx\nLOADGP __structCopySrc\nIPADD c\nLOADIND c\n");
	printf("STOREIND c\n");
	printf("LOADG __structCopyIdx\nPUSH 1\nADD\nSTOREG __structCopyIdx\n");
	printf("JMP L%d\nLABEL L%d\n", top, done);
}
/* Kopie, wenn BEIDE Adressen bereits in den Scratch-Globals stehen --
   fuer `arr[i] = s`, wo das Ziel erst zur Laufzeit feststeht. */
static void tcEmitStructCopyDyn(int size, int sizeN) {
	tcDeclStructCopyScratch();
	tcEmitStructCopyLoop(size, sizeN);
}
static void tcEmitStructCopy(int dstSlot, const char* dstGlobal, int size, int sizeN) {
	int off;
	tcDeclStructCopyScratch();
	printf("STOREGP __structCopySrc\n");
	/* Zeigerfrei: die ALTE, entrollte Fassung, Ziel direkt statt ueber das
	   Scratch-Global -- Zeile fuer Zeile wie vor dem 15.09.2026. Der Umweg
	   ueber __structCopyDst kostete zwei Zeilen je Kopie, und bei den 1340
	   Kopien in QCCs eigenem Parser lief damit MAX_IR_LINES im Backend
	   ueber. Nur wo die Groesse erst das Backend kennt, ist die Schleife
	   noetig -- und dort ist sie ein Gewinn, nicht ein Aufschlag. */
	if (sizeN == 0) {
		for (off = 0; off < size; off++) {
			printf("PUSH %d\n", off);
			if (dstGlobal) printf("ADDRG %s\n", dstGlobal);
			else printf("PUSHADDR L %d\n", dstSlot);
			printf("IPADD c\n");
			printf("PUSH %d\nLOADGP __structCopySrc\nIPADD c\nLOADIND c\n", off);
			printf("STOREIND c\n");
		}
		return;
	}
	if (dstGlobal) printf("ADDRG %s\n", dstGlobal);
	else printf("PUSHADDR L %d\n", dstSlot);
	printf("STOREGP __structCopyDst\n");
	tcEmitStructCopyLoop(size, sizeN);
}
static void tcAssignStore(int leaveValue) {
	TCType got; char tag;
	got = tcTypePop();
	got = tcCoerceToTarget(tcTargetType, got);
	if (!tcCompatible(tcTargetType, got)) tcTypeError("assignment", tcTargetType, got);
	/* Struct als Ganzes: kein Registerwert, sondern eine Kopie.  Muss VOR
	   den skalaren Zweigen stehen -- tcTypeTag('s') liefert 'i', wodurch
	   frueher ein 4-Byte-Store fuer ein groesseres Objekt erzeugt wurde. */
	if (tcTargetType.base == 's' && !tcTargetType.pointers && !tcTargetIndirect) {
		int sid = tcTargetType.structId - 1;
		if (sid < 0 || sid >= tcStructCount) {
			tcErrAt(parserActionAt); fprintf(stderr, "struct assignment to an unknown struct type\n");
			actionErrors++; return;
		}
		if (leaveValue) {
			tcErrAt(parserActionAt); fprintf(stderr, "struct assignment inside an expression is not supported\n");
			actionErrors++; return;
		}
		if (tcTargetIsArray) {
			/* arr[i] = s -- Stapel: Index unten, Quelladresse oben. Die
			   Zieladresse (Basis + Index * Groesse) entsteht erst zur
			   Laufzeit, also ueber den zweiten Scratch-Global. Das ist der
			   Fall, mit dem der Compiler seine eigene Typtabelle fuellt
			   (tc_local: tcLocalTypes[tcLocalCount] = tcCurrentType). */
			tcDeclStructCopyScratch();
			printf("STOREGP __structCopySrc\n");
			if (tcTargetIsGlobal) printf("PUSHADDR G %s\n", tcTargetGlobal);
			else if (tcTargetSlot >= 0) printf("PUSHADDR L %d\n", tcTargetSlot);
			else {
				tcErrAt(parserActionAt); fprintf(stderr, "indexed struct assignment without a target\n");
				actionErrors++; return;
			}
			{ printf("IPADDN "); tcEmitStructSize(sid); printf("\n"); }
			printf("STOREGP __structCopyDst\n");
			tcEmitStructCopyDyn(tcStructByteSizeK[sid], tcStructByteSizeN[sid]);
		}
		else if (tcTargetIsGlobal) tcEmitStructCopy(-1, tcTargetGlobal, tcStructByteSizeK[sid], tcStructByteSizeN[sid]);
		else if (tcTargetSlot >= 0) tcEmitStructCopy(tcTargetSlot, 0, tcStructByteSizeK[sid], tcStructByteSizeN[sid]);
		else {
			tcErrAt(parserActionAt); fprintf(stderr, "struct assignment without a target\n");
			actionErrors++;
		}
		return;
	}
	tag = tcTypeTag(tcTargetType);
	/* Bei einer indirekten oder indizierten Zuweisung liegen Adresse/Index
	   UNTER dem Wert. DUP wuerde dort die Stapelfolge zerstoeren (p,v,v --
	   STOREIND erwartet aber p,v). Die KEEP-IR-Varianten verbrauchen p/v bzw.
	   i/v und legen genau den gespeicherten Wert wieder ab. Bei einfachen
	   Variablen ist DUP vor dem normalen Store korrekt und bleibt kompakter. */
	/* Ein double liegt als 8-Byte-Block auf dem Stapel; DUP verdoppelt nur
	   einen Slot -- docs/IR_OPCODES_de.md fuehrt DDUP/DDROP eigens auf,
	   "weil DUP/DROP bei 8 Byte mehrdeutig waeren". Betrifft die Zuweisung
	   als WERT, also "(a = 1.5)" im Komma-Ausdruck. */
	if (leaveValue && !tcTargetIndirect && !tcTargetIsArray) {
		if (tcIsDouble(tcTargetType)) printf("DDUP\n");
		else printf("DUP\n");
	}
	if (tcTargetIndirect) printf("STOREIND%s %c\n", leaveValue ? "KEEP" : "", tag);
	else if (tcTargetIsGlobal && tcTargetIsArray) printf("STOREIDX%s G %s %c\n", leaveValue ? "KEEP" : "", tcTargetGlobal, tag);
	else if (tcTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffix(tag), tcTargetGlobal);
	else if (tcTargetSlot >= 0 && tcTargetIsArray) printf("STOREIDX%s L %d %c\n", leaveValue ? "KEEP" : "", tcTargetSlot, tag);
	else if (tcTargetSlot >= 0) printf("STORE%s %d\n", tcIsPointer(tcTargetType) ? "P" : tcWordSuffixL(tag), tcTargetSlot);
	else { actionErrors++; tcErrAt(parserActionAt); fprintf(stderr, "unknown assignment target slot=%d global=%d array=%d indirect=%d\n", tcTargetSlot, tcTargetIsGlobal, tcTargetIsArray, tcTargetIndirect); return; }
	if (leaveValue) tcTypePush(tcTargetType);
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
	int fpSlot; int fpGlobal; int haveFp; TCType fpType;
	if (start < end && *start == '[') {
		if (tcIndexDepth >= 64) { actionErrors++; tcErrAt(start); fprintf(stderr, "index nesting too deep\n"); return; }
		tcIndexSavedAdd[tcIndexDepth] = tcPendingAdd;
		tcIndexSavedMul[tcIndexDepth] = tcPendingMul;
		tcIndexSavedRel0[tcIndexDepth] = tcRel0;
		tcIndexSavedRel1[tcIndexDepth] = tcRel1;
		tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0; tcIndexDepth++; return;
	}
	if (tcCallDepth >= 64) { actionErrors++; tcErrAt(start); fprintf(stderr, "call nesting too deep\n"); return; }
	tcCopy(tcCallName[tcCallDepth], start, end);
	tcCallArgCount[tcCallDepth] = 0;
	tcCallFnSig[tcCallDepth] = -1;   /* normaler Aufruf ueber einen Namen */
	/* AUFRUF UEBER EINE FUNKTIONSZEIGER-VARIABLE (2026-08-11).
	   "f(7)" mit "Fn f;" wurde bis hierher als DIREKTER Aufruf geparst: die
	   Grammatik probiert in "callStmt = call | indirectCall" die direkte Regel
	   zuerst, und bei einem nackten Bezeichner greift sie. tc_call fand dann
	   keine Funktion des Namens, meldete "unknown function" und kehrte zurueck
	   OHNE einen Aufruf-Opcode auszugeben -- die Argumente blieben liegen und
	   der Aufruf verschwand STILL aus der IR. (Ueber Arrayelement "t[0](7)"
	   oder struct-Feld "s.f(3)" greift indirectCall und alles war korrekt.)
	   Hier, VOR den Argumenten, ist die einzig richtige Stelle: das Backend
	   erwartet den Zeiger ZUUNTERST und liest ihn bei nargs*4(a7) -- in
	   tc_call sind die Argumente laengst ausgegeben, dort waere die Reihenfolge
	   nicht mehr herstellbar. Ist der Name keine Funktion, aber eine Variable
	   mit Funktionszeigertyp, wird der Zeiger jetzt geladen und die Signatur im
	   Rahmen vermerkt; tc_call gibt daraufhin CALLIND statt CALL aus. */
	if (tcLookupFunction(tcCallName[tcCallDepth]) < 0) {
		haveFp = 0;
		fpSlot = tcLookupLocal(start, end);
		if (fpSlot >= 0) {
			fpType = tcLocalTypes[fpSlot];
			if (tcIsFnPtr(fpType)) { printf("LOADP %d\n", fpSlot); haveFp = 1; }
		} else {
			fpGlobal = tcLookupGlobal(start, end);
			if (fpGlobal >= 0) {
				fpType = tcGlobalTypes[fpGlobal];
				if (tcIsFnPtr(fpType)) { printf("LOADGP %s\n", tcGlobalNames[fpGlobal]); haveFp = 1; }
			}
		}
		if (haveFp) { tcCallFnSig[tcCallDepth] = (int)fpType.structId - 1; }
	}
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
		if (tcIndexDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing index frame\n"); return; }
		frame = --tcIndexDepth; tcPendingAdd = tcIndexSavedAdd[frame]; tcPendingMul = tcIndexSavedMul[frame];
		tcRel0 = tcIndexSavedRel0[frame]; tcRel1 = tcIndexSavedRel1[frame]; return;
	}
	(void)end;
	if (tcCallDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing call frame\n"); return; }
	{ int f = tcLookupFunction(tcCallName[tcCallDepth - 1]); int n = tcCallArgCount[tcCallDepth - 1]; TCType got = tcTypePop();
	  /* Argumentkonversion (2026-09-16): ein Argument wird wie bei einer
	     Zuweisung in den Parametertyp umgewandelt (C89 3.3.2.2) -- "f(1)" an
	     einem double-Parameter ist gueltiges C. tcCoerceToTarget EMITTIERT
	     dabei I2D/D2I; laesst sich der Typ nicht wandeln, gibt es ihn
	     unveraendert zurueck und die Pruefung darunter greift wie bisher. */
	  if (f >= 0 && n < tcFunctionNargs[f]) got = tcCoerceToTarget(tcFunctionParamTypes[f][n], got);
	  if (f >= 0 && n < tcFunctionNargs[f] && !tcCompatible(tcFunctionParamTypes[f][n], got)) tcTypeError("argument", tcFunctionParamTypes[f][n], got);
	  /* DOUBLE PER WERT: acht Byte passen nicht in einen Slot (vier Byte im
	     Rahmen). Wie beim struct-Argument wandert der Wert deshalb in einen
	     globalen Puffer, und uebergeben wird dessen ADRESSE; der Aufgerufene
	     packt ihn beim Eintritt aus (tc_funcbodybegin).
	     EIGENER PUFFER JE AUFRUFSTELLE: mit einem Puffer je Argumentposition
	     wuerde "f(1.0, g(2.0))" das bereits abgelegte erste Argument beim
	     Auswerten von g ueberschreiben. */
	  if (tcIsDouble(got)) {
	  	char dargName[40];
	  	sprintf(dargName, "__dblArg_%d", tcDoubleArgSeq++);
	  	printf("GARRAY %s d 1 0\n", dargName);
	  	printf("STOREGD %s\n", dargName);
	  	printf("ADDRG %s\n", dargName);
	  }
	  /* Struct per Wert: auf dem Stapel liegt die Adresse des Originals.
	     Der Aufgerufene darf seinen Parameter aendern (tcPointerTo/
	     tcPointee tun genau das), also bekommt er eine Kopie. Ein Puffer
	     je Typ und Argumentposition reicht: die Argumente eines Aufrufs
	     werden nacheinander ausgewertet und sofort verbraucht. */
	  if (got.base == 's' && !got.pointers) {
	  	int sid = got.structId - 1;
	  	if (sid < 0 || sid >= tcStructCount) {
	  		tcErrAt(start); fprintf(stderr, "struct argument of an unknown struct type\n");
	  		actionErrors++;
	  	} else {
	  		/* Eigener Puffer je Aufrufstelle: ein Puffer je Position wuerde
	  		   von einem verschachtelten Aufruf ueberschrieben, der dieselbe
	  		   Position benutzt -- siehe tcCompatible/tcIsPointer. */
	  		char argName[40];
	  		sprintf(argName, "__structArg_%d_%d", sid, tcStructArgSeq++);
	  		{ printf("GARRAY %s c ", argName); tcEmitNum(tcStructByteSizeK[sid], tcStructByteSizeN[sid]); printf(" 1\n"); }
	  		tcEmitStructCopy(-1, argName, tcStructByteSizeK[sid], tcStructByteSizeN[sid]);
	  		printf("ADDRG %s\n", argName);
	  	}
	  } }
	tcCallArgCount[tcCallDepth - 1]++;
}

void tc_call(const char* start, const char* end) {
	int frame; int sig;
	(void)start; (void)end;
	if (tcCallDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing call frame\n"); return; }
	frame = --tcCallDepth;
	tcPendingAdd = tcCallSavedAdd[frame];
	tcPendingMul = tcCallSavedMul[frame];
	tcRel0 = tcCallSavedRel0[frame];
	tcRel1 = tcCallSavedRel1[frame];
	/* Von tc_callname als Aufruf ueber eine Funktionszeiger-Variable erkannt:
	   der Zeiger liegt bereits unter den Argumenten (siehe dort). Ab hier
	   identisch zu tc_indcall -- geprueft wird nur die Argumentanzahl, nicht
	   die Argumenttypen, weil tc_arg sie mangels Funktionsnamen keiner
	   Signatur zuordnen kann. */
	if (tcCallFnSig[frame] >= 0) {
		sig = tcCallFnSig[frame];
		if (tcFnSigNargs[sig] != tcCallArgCount[frame]) {
			actionErrors++; tcErrAt(start); fprintf(stderr, "wrong argument count (expected %d, got %d)\n", tcFnSigNargs[sig], tcCallArgCount[frame]);
		}
		printf("CALLIND%s %d\n", tcIsPointer(tcFnSigRet[sig]) ? "P" : "", tcCallArgCount[frame]);
		tcTypePush(tcFnSigRet[sig]);
		return;
	}
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
		if (f < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "unknown function '%s'\n", tcCallName[frame]); return; }
		if (tcFunctionIsVariadic[f] ? tcCallArgCount[frame] < tcFunctionNargs[f] : tcFunctionNargs[f] != tcCallArgCount[frame]) {
			actionErrors++; tcErrAt(start); fprintf(stderr, "wrong argument count (expected %d, got %d)\n", tcFunctionNargs[f], tcCallArgCount[frame]); return;
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
		/* Eine double-Funktion gibt die ADRESSE ihres Rueckgabepuffers zurueck
		   (s. tc_return) -- hier wird daraus wieder ein Wert. Das geschieht
		   UNMITTELBAR nach dem Aufruf, deshalb genuegt EIN Puffer fuer das
		   ganze Programm: er ist frei, bevor der naechste Aufruf ihn braucht. */
		if (!tcIsPointer(tcFunctionReturnTypes[f]) && tcFunctionReturnTypes[f].base == 'd') printf("LOADIND d\n");
		if (!tcIsPointer(tcFunctionReturnTypes[f]) && tcFunctionReturnTypes[f].base == 'c') printf("NARROWC\n");
		else if (!tcIsPointer(tcFunctionReturnTypes[f]) && tcFunctionReturnTypes[f].base == 'h') printf("NARROWH\n");
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
	tcTypePop4(&tcRetType);
}

void tc_return(const char* start, const char* end) {
	(void)end;
	/* Eine double-Funktion gibt die ADRESSE eines globalen Puffers zurueck --
	   acht Byte passen nicht durch d0. Auch der wertlose Fall bekommt diese
	   Adresse statt einer 0: "return;" in einer double-Funktion ist zwar
	   undefiniertes Verhalten, ein LOADIND auf die Adresse 0 beim Aufrufer
	   waere aber ein Absturz statt eines undefinierten Wertes. */
	if (!tcRetHasVal && tcIsDouble(tcFuncType)) {
		if (!tcDoubleRetDeclared) { printf("GARRAY __dblRet d 1 0\n"); tcDoubleRetDeclared = 1; }
		printf("ADDRG __dblRet\n");
	}
	else if (!tcRetHasVal) printf("PUSH 0\n");
	/* Rueckgabekonversion (C89 3.6.6.4: der Wert wird in den Rueckgabetyp
	   umgewandelt) -- "double f(void){ return 3; }" ist gueltiges C. */
	if (tcRetHasVal) tcRetType = tcCoerceToTarget(tcFuncType, tcRetType);
	if (tcRetHasVal && !tcCompatible4(&tcFuncType, &tcRetType)) tcTypeError("return", tcFuncType, tcRetType);
	if (tcRetHasVal && tcIsDouble(tcFuncType)) {
		if (!tcDoubleRetDeclared) { printf("GARRAY __dblRet d 1 0\n"); tcDoubleRetDeclared = 1; }
		printf("STOREGD __dblRet\n");
		printf("ADDRG __dblRet\n");
	}
	/* DOUBLE ALS RUECKGABEWERT (2026-09-16): noch nicht umgesetzt, und ohne
	   diese Meldung waere es STILL FALSCH. Das Backend holt den Rueckgabe-
	   wert mit einem einzigen "move.l (a7)+,d0" -- bei acht Byte ist das
	   die obere Haelfte, die untere bliebe auf dem Stapel liegen.
	   Fuer die Umsetzung braucht die IR ein eigenes RETD und der Aufrufer
	   eine Entsprechung; xcc gibt double in d0/d1 zurueck (gemessen, s.
	   docs/FLOAT_PLAN_de.md). */

	/* Struct-Rueckgabe: auf dem Stapel liegt die Adresse einer lokalen
	   Variable DIESES Rahmens (die Leseseite legt fuer Structs Adressen ab).
	   Sie ueberlebt das RET nicht, also vorher in den Rueckgabepuffer des
	   Typs kopieren und dessen Adresse zurueckgeben. Der Aufrufer sieht
	   damit wieder eine Struct-Adresse und kopiert sie regulaer weiter. */
	if (tcRetHasVal && !tcIsPointer(tcFuncType) && tcFuncType.base == 's') {
		int sid = tcFuncType.structId - 1;
		if (sid < 0 || sid >= tcStructCount) {
			tcErrAt(start); fprintf(stderr, "return of an unknown struct type\n");
			actionErrors++;
		} else {
			char retName[32];
			sprintf(retName, "__structRet_%d", sid);
			if (!tcStructRetDeclared[sid]) {
				{ printf("GARRAY %s c ", retName); tcEmitNum(tcStructByteSizeK[sid], tcStructByteSizeN[sid]); printf(" 1\n"); }
				tcStructRetDeclared[sid] = 1;
			}
			tcEmitStructCopy(-1, retName, tcStructByteSizeK[sid], tcStructByteSizeN[sid]);
			printf("ADDRG %s\n", retName);
		}
	}
	if (!tcIsPointer(tcFuncType) && tcFuncType.base == 'c') printf("NARROWC\n");
	else if (!tcIsPointer(tcFuncType) && tcFuncType.base == 'h') printf("NARROWH\n");
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
	condition = tcCondValue(condition);
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
	condition = tcCondValue(condition);
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
	condition = tcCondValue(condition);
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
	if (frame < 0) { tcErrAt(start); fprintf(stderr, "break outside loop or switch\n"); actionErrors++; return; }
	printf("JMP L%d\n", tcCtrlEnd[frame]);
}

void tc_continue(const char* start, const char* end) {
	int frame = tcFindLoop(); (void)start; (void)end;
	if (frame < 0) { tcErrAt(start); fprintf(stderr, "continue outside loop\n"); actionErrors++; return; }
	printf("JMP L%d\n", tcCtrlCont[frame]);
}

/* "goto name;" -- der Sprung darf VORWAERTS gehen (Marke noch nicht gesehen);
   tcGotoFind legt die Nummer dann schon jetzt an. Ob die Marke jemals
   definiert wird, prueft tc_funcend. */
void tc_goto(const char* start, const char* end) {
	char name[32];
	int i;
	tcIdentFromSpan(name, start, end, "goto");
	if (name[0] == 0) { tcErrAt(start); fprintf(stderr, "goto without label name\n"); actionErrors++; return; }
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
	if (name[0] == 0) { tcErrAt(start); fprintf(stderr, "empty label name\n"); actionErrors++; return; }
	i = tcGotoFind(name);
	if (i < 0) return;
	if (tcGotoDefined[i]) {
		tcErrAt(start); fprintf(stderr, "duplicate label '%s'\n", name); actionErrors++; return;
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
	if (p >= end) { tcErrAt(start); fprintf(stderr, "empty character literal\n"); actionErrors++; TC_TYPE_PUSH('i', 0, 0, 0); return; }
	if (*p == 92) {                          /* Backslash: Escape-Form */
		p++;
		if (p >= end) { tcErrAt(start); fprintf(stderr, "incomplete character escape\n"); actionErrors++; TC_TYPE_PUSH('i', 0, 0, 0); return; }
		if      (*p == 'n')  v = 10;
		else if (*p == 't')  v = 9;
		else if (*p == 'r')  v = 13;
		else if (*p == '0')  v = 0;
		else if (*p == 92)   v = 92;
		else { tcErrAt(start); fprintf(stderr, "unsupported character escape\n"); actionErrors++; v = 0; }
	} else {
		v = (int)(unsigned char)*p;
	}
	printf("PUSH %d\n", v);
	TC_TYPE_PUSH('i', 0, 0, 0);
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

/* "int f(int (*fp)(int))" -- wie tc_fnptrvar, nur legt es am Ende einen
   PARAMETER an statt einer Lokalen. Die Aufrufsignatur der umschliessenden
   Funktion baut tc_funcend ohnehin aus tcLocalTypes, der Zeiger landet also
   von selbst darin. */
void tc_fnptrparam(const char* start, const char* end) {
	const char* p = start;
	const char* nameStart;
	const char* nameEnd;
	int k;
	int sig;
	while (p < end && *p != '(') p++;
	while (p < end && (*p == '(' || *p == '*' || *p == ' ' || *p == '\t')) p++;
	nameStart = p;
	while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
	                   (*p >= '0' && *p <= '9') || *p == '_')) p++;
	nameEnd = p;
	if (nameStart == nameEnd) { tcErrAt(start); fprintf(stderr, "malformed function pointer parameter\n"); actionErrors++; return; }
	if (tcExternIsVariadic) { tcErrAt(start); fprintf(stderr, "variadic function pointers are not supported\n"); actionErrors++; return; }
	if (tcExternBuildParamCount > MAX_FNSIG_PARAMS) { tcErrAt(start); fprintf(stderr, "too many function pointer parameters\n"); actionErrors++; return; }
	if (tcFnSigCount >= MAX_FNSIGS) { tcErrAt(start); fprintf(stderr, "too many function pointer signatures\n"); actionErrors++; return; }
	sig = tcFnSigCount++;
	tcFnSigRet[sig] = tcFnPtrRetPending;
	tcFnSigNargs[sig] = tcExternBuildParamCount;
	for (k = 0; k < tcExternBuildParamCount; k++) tcFnSigParams[sig][k] = tcExternBuildParamTypes[k];
	tcCurrentType = tcMakeFnPtr(sig);
	tc_param(nameStart, nameEnd);
}

/* "int (*fp)(int);" als lokale Variable. Baugleich zu tc_fnptrtypedef, nur
   dass am Ende keine typedef-Tabelle gefuellt wird, sondern die gewoehnliche
   Lokalenanlage laeuft -- so gelten Slotvergabe und alles Weitere
   unveraendert. Der Name steht zwischen "(*" und ")", die Rueckwaertssuche
   der normalen Deklaratoren passt hier also nicht. */
void tc_fnptrvar(const char* start, const char* end) {
	const char* p = start;
	const char* nameStart;
	const char* nameEnd;
	int k;
	int sig;
	while (p < end && *p != '(') p++;
	while (p < end && (*p == '(' || *p == '*' || *p == ' ' || *p == '\t')) p++;
	nameStart = p;
	while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
	                   (*p >= '0' && *p <= '9') || *p == '_')) p++;
	nameEnd = p;
	if (nameStart == nameEnd) { tcErrAt(start); fprintf(stderr, "malformed function pointer declaration\n"); actionErrors++; return; }
	if (tcExternIsVariadic) { tcErrAt(start); fprintf(stderr, "variadic function pointers are not supported\n"); actionErrors++; return; }
	if (tcExternBuildParamCount > MAX_FNSIG_PARAMS) { tcErrAt(start); fprintf(stderr, "too many function pointer parameters\n"); actionErrors++; return; }
	if (tcFnSigCount >= MAX_FNSIGS) { tcErrAt(start); fprintf(stderr, "too many function pointer signatures\n"); actionErrors++; return; }
	sig = tcFnSigCount++;
	tcFnSigRet[sig] = tcFnPtrRetPending;
	tcFnSigNargs[sig] = tcExternBuildParamCount;
	for (k = 0; k < tcExternBuildParamCount; k++) tcFnSigParams[sig][k] = tcExternBuildParamTypes[k];
	tcCurrentType = tcMakeFnPtr(sig);
	tc_local(nameStart, nameEnd);
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
	if (nameStart == nameEnd) { tcErrAt(start); fprintf(stderr, "malformed function pointer typedef\n"); actionErrors++; return; }
	if (tcExternIsVariadic) { tcErrAt(start); fprintf(stderr, "variadic function pointers are not supported\n"); actionErrors++; return; }
	if (tcTypedefCount >= MAX_TYPEDEFS) { tcErrAt(start); fprintf(stderr, "too many typedefs\n"); actionErrors++; return; }
	if (tcLookupTypedef(nameStart, nameEnd) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate typedef '%.*s'\n", (int)(nameEnd - nameStart), nameStart); actionErrors++; return; }
	if (tcExternBuildParamCount > MAX_FNSIG_PARAMS) { tcErrAt(start); fprintf(stderr, "too many function pointer parameters\n"); actionErrors++; return; }
	if (tcFnSigCount >= MAX_FNSIGS) { tcErrAt(start); fprintf(stderr, "too many function pointer signatures\n"); actionErrors++; return; }
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
	if (tcCallDepth >= 64) { actionErrors++; tcErrAt(start); fprintf(stderr, "call nesting too deep\n"); return; }
	tcCallName[tcCallDepth][0] = 0;          /* kein Name -- tc_arg findet keine Funktion und zaehlt nur */
	tcCallArgCount[tcCallDepth] = 0;
	if (!tcIsFnPtr(callee)) {
		tcErrAt(start); fprintf(stderr, "called value is not a function pointer\n"); actionErrors++;
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
	if (tcCallDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing call frame\n"); return; }
	frame = --tcCallDepth;
	tcPendingAdd = tcCallSavedAdd[frame];
	tcPendingMul = tcCallSavedMul[frame];
	tcRel0 = tcCallSavedRel0[frame];
	tcRel1 = tcCallSavedRel1[frame];
	sig = tcCallFnSig[frame];
	if (sig < 0) { tcTypePush(tcBadType()); return; }
	if (tcFnSigNargs[sig] != tcCallArgCount[frame]) {
		tcErrAt(start); fprintf(stderr, "wrong argument count (expected %d, got %d)\n", tcFnSigNargs[sig], tcCallArgCount[frame]); actionErrors++;
	}
	printf("CALLIND%s %d\n", tcIsPointer(tcFnSigRet[sig]) ? "P" : "", tcCallArgCount[frame]);
	tcTypePush(tcFnSigRet[sig]);
}

void tc_structbegin(const char* start, const char* end) {
	tcCopy(tcStructBuildName, start, end);
	tcStructBuildFieldCount = 0;
	tcBuildIsUnion = 0;
	tcStructPredeclare(start, end);
}

/* Eine union wird wie eine struct gesammelt und auch als struct registriert;
   nur das LAYOUT unterscheidet sich (s. tcRegisterStruct). Die Flagge wird in
   tc_structbegin geloescht, damit eine struct nach einer union nicht deren
   Layout erbt. */
void tc_unionbegin(const char* start, const char* end) {
	tcCopy(tcStructBuildName, start, end);
	tcStructPredeclare(start, end);
	tcStructBuildFieldCount = 0;
	tcBuildIsUnion = 1;
}

void tc_unionend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcStructHasBody) { tcStructPending = -1; return; }
	tcStructHasBody = 0;
	tcRegisterStruct(tcStructBuildName, tcStructBuildName + strlen(tcStructBuildName));
	tcBuildIsUnion = 0;
}

/* "typedef enum { A, B } Flags;" -- die Spanne deckt "enum [Tag] { ... }" ab.
   Die Konstanten kommen ueber denselben Rohtext-Leser wie bei einer
   gewoehnlichen Aufzaehlung; der Typ selbst ist ein int, wie ueberall in
   QCC (s. den enum-Zweig in tc_type). */
void tc_anonenum(const char* start, const char* end) {
	tcParseEnumBody(start, end);
	TC_SET_CURRENT('i', 0);
	tcBasePointers = 0;
}

void tc_anonstructbegin(const char* start, const char* end) {
	/* anonymes struct inline im typedef -- kein Name an dieser Stelle bekannt,
	   siehe Kommentar bei tcAnonStructPending und tc_typedeftarget.
	   tcBasePointers auf 0: es gibt hier kein "type", das es sonst setzt
	   (typedefType = anonStructType OHNE type-Zweig), ohne den Reset wuerde
	   der erste Deklarator einen fremden, laengst veralteten Zeigergrad
	   erben (2026-09-09, Mehrfachdeklaratoren-Fix). */
	(void)start; (void)end;
	tcStructPending = -1;   /* kein Name an dieser Stelle -- nichts vorzumerken */
	tcStructBuildFieldCount = 0;
	tcAnonStructPending = 1;
	tcBasePointers = 0;
}

/* GLEITKOMMA-LITERAL -> IEEE-754 (2026-09-16).
 *
 * Der Umrechner darunter ist WORTGLEICH tools/dec2ieee.c -- dort steht er
 * eigenstaendig mit Testtreiber, hier ist er im Einsatz. Beide Fassungen
 * werden von runtests.sh gegeneinander geprueft; laufen sie auseinander,
 * faellt das auf.
 *
 * Warum der Aufwand: QCC uebersetzt sich selbst und hat dabei kein
 * Gleitkomma zur Verfuegung. Aus "3.14" muss trotzdem das Bitmuster
 * 0x40091EB851EB851F werden -- mit Ganzzahlen. Gerechnet wird in Gliedern
 * zu 16 Bit, weil ein Produkt zweier Glieder samt Uebertrag gerade noch in
 * 32 Bit passt; so breit ist unsigned long auf dem 68030.
 *
 * Geprueft gegen ein Orakel: 894 Werte quer durch alle Groessenordnungen,
 * 510 exakt ausgeschriebene Mittelpunkte zwischen zwei benachbarten double
 * (dort entscheidet sich Ties-to-even), und 25 Faelle auf echtem 68030.
 */

typedef struct {
    int n;                      /* Anzahl belegter Glieder, 0 = Wert null */
    unsigned long d[260]; /* Basis 2^16, kleinstes Glied zuerst.
                             260 Glieder = 4160 Bit, genug fuer 10^325
                             samt Schiebeplatz. */
} Big;

/* Zwischenwerte auf Dateiebene statt auf dem Stack: jede dieser Strukturen
 * ist gut ein Kilobyte gross, und der Stack eines OS-9-Moduls ist knapp.
 * Der Konverter ist dadurch nicht wiedereintrittsfaehig -- ein Compiler
 * braucht das nicht. (QCC kennt ausserdem kein 'static' an lokalen
 * Variablen, s. docs/KNOWN_BUGS_C89_de.md.) */
static Big d2iCur, d2iRound, d2iMant, d2iDen, d2iNum, d2iQuot, d2iRem;

static void bigZero(Big* a) { a->n = 0; }

static void bigTrim(Big* a) {
    while (a->n > 0 && a->d[a->n - 1] == 0UL) a->n--;
}

static int bigIsZero(const Big* a) { return a->n == 0; }

static void bigSetSmall(Big* a, unsigned long v) {
    bigZero(a);
    while (v != 0UL && a->n < 260) {
        a->d[a->n++] = v & 0xFFFFUL;
        v >>= 16;
    }
}

/* a = a * d2iRound + add,  d2iRound und add unter 2^16 */
static int bigMulAddSmall(Big* a, unsigned long d2iRound, unsigned long add) {
    unsigned long carry = add;
    int i;
    for (i = 0; i < a->n; i++) {
        unsigned long t = a->d[i] * d2iRound + carry;
        a->d[i] = t & 0xFFFFUL;
        carry = t >> 16;
    }
    while (carry != 0UL) {
        if (a->n >= 260) return 0;      /* Ueberlauf */
        a->d[a->n++] = carry & 0xFFFFUL;
        carry >>= 16;
    }
    return 1;
}

/* Anzahl signifikanter Bits */
static long bigBitLen(const Big* a) {
    unsigned long top;
    long bits;
    if (a->n == 0) return 0L;
    top = a->d[a->n - 1];
    bits = 0L;
    while (top != 0UL) { bits++; top >>= 1; }
    return (long)(a->n - 1) * 16L + bits;
}

static int bigGetBit(const Big* a, long bit) {
    long limb = bit / 16L;
    if (bit < 0L || limb >= (long)a->n) return 0;
    return (int)((a->d[limb] >> (bit % 16L)) & 1UL);
}

/* Sind unter Bit `bit` noch gesetzte Bits? (Sticky) */
static int bigAnyBitBelow(const Big* a, long bit) {
    long i;
    long limb = bit / 16L;
    int off = (int)(bit % 16L);
    for (i = 0L; i < limb && i < (long)a->n; i++)
        if (a->d[i] != 0UL) return 1;
    if (off > 0 && limb < (long)a->n) {
        unsigned long mask = (1UL << off) - 1UL;
        if ((a->d[limb] & mask) != 0UL) return 1;
    }
    return 0;
}

static int bigShiftLeft(Big* a, long bits) {
    long limbShift = bits / 16L;
    int bitShift = (int)(bits % 16L);
    long i;
    if (bigIsZero(a) || bits <= 0L) return 1;
    if (a->n + limbShift + 1L > (long)260) return 0;
    if (limbShift > 0L) {
        for (i = (long)a->n - 1L; i >= 0L; i--) a->d[i + limbShift] = a->d[i];
        for (i = 0L; i < limbShift; i++) a->d[i] = 0UL;
        a->n += (int)limbShift;
    }
    if (bitShift > 0) {
        unsigned long carry = 0UL;
        for (i = limbShift; i < (long)a->n; i++) {
            unsigned long t = (a->d[i] << bitShift) | carry;
            a->d[i] = t & 0xFFFFUL;
            carry = t >> 16;
        }
        if (carry != 0UL) {
            if (a->n >= 260) return 0;
            a->d[a->n++] = carry;
        }
    }
    return 1;
}

static void bigShiftRight(Big* a, long bits) {
    long limbShift = bits / 16L;
    int bitShift = (int)(bits % 16L);
    long i;
    if (bigIsZero(a) || bits <= 0L) return;
    if (limbShift >= (long)a->n) { bigZero(a); return; }
    if (limbShift > 0L) {
        for (i = 0L; i + limbShift < (long)a->n; i++) a->d[i] = a->d[i + limbShift];
        a->n -= (int)limbShift;
    }
    if (bitShift > 0) {
        for (i = 0L; i < (long)a->n; i++) {
            unsigned long lo = a->d[i] >> bitShift;
            unsigned long hi = (i + 1L < (long)a->n) ? a->d[i + 1L] : 0UL;
            a->d[i] = (lo | (hi << (16 - bitShift))) & 0xFFFFUL;
        }
    }
    bigTrim(a);
}

/* -1, 0, +1 */
static int bigCmp(const Big* a, const Big* b) {
    int i;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* a -= b, setzt voraus a >= b */
static void bigSub(Big* a, const Big* b) {
    long borrow = 0L;
    int i;
    for (i = 0; i < a->n; i++) {
        long t = (long)a->d[i] - borrow - (i < b->n ? (long)b->d[i] : 0L);
        if (t < 0L) { t += 65536L; borrow = 1L; } else borrow = 0L;
        a->d[i] = (unsigned long)t;
    }
    bigTrim(a);
}

static void bigCopy(Big* dst, const Big* src) {
    int i;
    dst->n = src->n;
    for (i = 0; i < src->n; i++) dst->d[i] = src->d[i];
}

static int bigSetPow10(Big* a, long e) {
    long i;
    bigSetSmall(a, 1UL);
    for (i = 0L; i + 4L <= e; i += 4L)
        if (!bigMulAddSmall(a, 10000UL, 0UL)) return 0;
    for (; i < e; i++)
        if (!bigMulAddSmall(a, 10UL, 0UL)) return 0;
    return 1;
}

/* q = d2iNum / d2iDen, rest bleibt in d2iNum. Schulmethode, bitweise. */
static int bigDivMod(const Big* d2iNum, const Big* d2iDen, Big* q, Big* d2iRem) {
    long shift, i;
    if (bigIsZero(d2iDen)) return 0;
    bigZero(q);
    bigZero(&d2iCur);
    shift = bigBitLen(d2iNum) - 1L;
    if (shift < 0L) { bigZero(d2iRem); return 1; }
    q->n = (int)(shift / 16L) + 1;
    for (i = 0L; i < (long)q->n; i++) q->d[i] = 0UL;
    for (i = shift; i >= 0L; i--) {
        if (!bigShiftLeft(&d2iCur, 1L)) return 0;
        if (bigGetBit(d2iNum, i)) {
            if (d2iCur.n == 0) { d2iCur.n = 1; d2iCur.d[0] = 1UL; }
            else d2iCur.d[0] |= 1UL;
        }
        if (bigCmp(&d2iCur, d2iDen) >= 0) {
            bigSub(&d2iCur, d2iDen);
            q->d[i / 16L] |= (1UL << (i % 16L));
        }
    }
    bigTrim(q);
    bigCopy(d2iRem, &d2iCur);
    return 1;
}

static unsigned long bigLimb(const Big* a, int i) {
    return (i < a->n) ? a->d[i] : 0UL;
}

/* Rundet d2iDen Wert q * 2^e0 (zuzueglich eines Restes, d2iDen stickyIn anzeigt)
 * auf binary64 und liefert die beiden 32-Bit-Haelften.
 *
 * Alles laeuft ueber die Stelle `drop`: so viele Bits fallen unten weg.
 * Fuer normale Zahlen ergibt sie sich aus der Bitlaenge, fuer denormale aus
 * der festen kleinsten Stufe 2^-1074 -- daher das Maximum der beiden.
 */
static void roundToDouble(const Big* q, long e0, int stickyIn, int neg,
                          unsigned long* hi, unsigned long* lo) {
    long len, drop, exp2, biased;   /* 53 = Mantissenbits von binary64 */
    int roundBit, sticky;
    unsigned long field_lo, field_hi;

    if (bigIsZero(q) && !stickyIn) {
        *hi = neg ? 0x80000000UL : 0UL;
        *lo = 0UL;
        return;
    }

    len = bigBitLen(q);
    drop = len - (long)53;
    if (drop < -1074L - e0) drop = -1074L - e0;   /* nicht unter die kleinste Stufe */

    bigCopy(&d2iRound, q);
    if (drop <= 0L) {
        bigShiftLeft(&d2iRound, -drop);
        roundBit = 0;
        sticky = stickyIn;
    } else {
        roundBit = bigGetBit(q, drop - 1L);
        sticky = bigAnyBitBelow(q, drop - 1L) || stickyIn;
        bigShiftRight(&d2iRound, drop);
    }

    /* Zur naechsten Zahl, bei genau der Haelfte zur geraden (Ties-to-even) */
    if (roundBit && (sticky || (bigLimb(&d2iRound, 0) & 1UL))) {
        unsigned long carry = 1UL;
        int i = 0;
        while (carry != 0UL) {
            if (i >= d2iRound.n) d2iRound.d[d2iRound.n++] = 0UL;
            d2iRound.d[i] += carry;
            carry = d2iRound.d[i] >> 16;
            d2iRound.d[i] &= 0xFFFFUL;
            i++;
        }
    }

    /* Traegt das Aufrunden ein Bit ueber, verschiebt sich der Exponent. */
    if (bigBitLen(&d2iRound) > (long)53) {
        bigShiftRight(&d2iRound, 1L);
        drop += 1L;
    }

    exp2 = e0 + drop;

    if (bigBitLen(&d2iRound) == (long)53) {
        biased = exp2 + 52L + 1023L;
    } else {
        biased = 0L;                 /* denormal: kein implizites Bit */
    }

    if (biased >= 2047L) {           /* Ueberlauf -> unendlich */
        *hi = (neg ? 0x80000000UL : 0UL) | 0x7FF00000UL;
        *lo = 0UL;
        return;
    }

    field_lo = bigLimb(&d2iRound, 0) | (bigLimb(&d2iRound, 1) << 16);
    field_hi = (bigLimb(&d2iRound, 2) | ((bigLimb(&d2iRound, 3) & 0xFUL) << 16)) & 0xFFFFFUL;

    *lo = field_lo;
    *hi = (neg ? 0x80000000UL : 0UL)
        | ((unsigned long)(biased & 0x7FFL) << 20)
        | field_hi;
}

/* Wandelt das Literal zwischen start und end um.
 * Rueckgabe: 1 = in Ordnung, 0 = kein gueltiges Gleitkommaliteral.
 *
 * Die Big-Zwischenwerte stehen bewusst als static im Datenbereich und nicht
 * auf dem Stack: unter OS-9 ist der Stack eines Moduls knapp bemessen, und
 * fuenf dieser Strukturen waeren gut sechs Kilobyte.
 */

int qccDecToDouble(const char* start, const char* end, int neg,
                   unsigned long* hi, unsigned long* lo) {
    const char* p = start;
    long decExp = 0L;
    long ndig = 0L;
    int seenDigit = 0, seenDot = 0, cut = 0;
    long e;

    bigSetSmall(&d2iMant, 0UL);

    while (p < end) {
        if (*p >= '0' && *p <= '9') {
            seenDigit = 1;
            if (ndig < 800) {
                if (!(bigIsZero(&d2iMant) && *p == '0')) {
                    if (!bigMulAddSmall(&d2iMant, 10UL, (unsigned long)(*p - '0'))) return 0;
                    ndig++;
                }
            } else {
                if (*p != '0') cut = 1;      /* abgeschnittene Ziffern nur als Rest */
                decExp++;
            }
            if (seenDot) decExp--;
            p++;
        } else if (*p == '.' && !seenDot) {
            seenDot = 1;
            p++;
        } else break;
    }
    if (!seenDigit) return 0;

    if (p < end && (*p == 'e' || *p == 'E')) {
        int esign = 0;
        long ev = 0L;
        const char* q = p + 1;
        if (q < end && (*q == '+' || *q == '-')) { esign = (*q == '-'); q++; }
        if (q >= end || *q < '0' || *q > '9') return 0;
        while (q < end && *q >= '0' && *q <= '9') {
            if (ev < 100000L) ev = ev * 10L + (long)(*q - '0');
            q++;
        }
        decExp += esign ? -ev : ev;
        p = q;
    }
    while (p < end && (*p == 'f' || *p == 'F' || *p == 'l' || *p == 'L')) p++;
    if (p != end) return 0;

    if (bigIsZero(&d2iMant)) {
        *hi = neg ? 0x80000000UL : 0UL;
        *lo = 0UL;
        return 1;
    }

    /* Grob abschaetzen, bevor gerechnet wird: sonst waechst 10^E ins Uferlose */
    if (decExp + ndig > 330L) {
        *hi = (neg ? 0x80000000UL : 0UL) | 0x7FF00000UL;
        *lo = 0UL;
        return 1;
    }
    if (decExp + ndig < -360L) {
        *hi = neg ? 0x80000000UL : 0UL;
        *lo = 0UL;
        return 1;
    }

    if (decExp >= 0L) {
        /* Ganzzahl: Mantisse mal 10^E, dann runden */
        bigCopy(&d2iNum, &d2iMant);
        for (e = 0L; e < decExp; e++)
            if (!bigMulAddSmall(&d2iNum, 10UL, 0UL)) return 0;
        roundToDouble(&d2iNum, 0L, cut, neg, hi, lo);
    } else {
        /* Bruch: Zaehler so weit hochschieben, dass der Quotient reichlich
         * ueber 53 Bit hat -- der Divisionsrest wird zum Sticky-Bit. */
        long s;
        if (!bigSetPow10(&d2iDen, -decExp)) return 0;
        bigCopy(&d2iNum, &d2iMant);
        s = 64L + bigBitLen(&d2iDen) - bigBitLen(&d2iNum);
        if (s < 0L) s = 0L;
        if (!bigShiftLeft(&d2iNum, s)) return 0;
        if (!bigDivMod(&d2iNum, &d2iDen, &d2iQuot, &d2iRem)) return 0;
        roundToDouble(&d2iQuot, -s, cut || !bigIsZero(&d2iRem), neg, hi, lo);
    }
    return 1;
}

void tc_floatlit(const char* start, const char* end) {
	unsigned long hi;
	unsigned long lo;

	hi = 0UL;
	lo = 0UL;
	if (!qccDecToDouble(start, end, 0, &hi, &lo)) {
		tcErrAt(start);
		fprintf(stderr, "malformed floating point literal\n");
		actionErrors++;
		printf("PUSH 0\n");
		tcTypePush(tcMakeType('i', 0));
		return;
	}
	/* Zwei Ganzzahlen statt eines Gleitkommatextes: die IR wird von
	   Werkzeugen gelesen, die selbst kein Gleitkomma haben. */
	printf("PUSHD %lu %lu\n", hi, lo);
	tcTypePush(tcMakeType('d', 0));
}

/* "long long" wird GELESEN und ABGELEHNT. Vorher brach die Deklaration still
   ab, weil die Typregel nur ein "long" kannte und das zweite zum
   Variablennamen wurde. Umgesetzt ist der Typ nicht: 64 Bit auf dem 68k
   brauchen Hilfsroutinen fuer Multiplikation und Division -- ein eigenes
   Vorhaben. Im Microware-Korpus kommt er in 76 von 4109 Quellen vor,
   ueberwiegend fuer Zeitstempel. */
void tc_longlong(const char* start, const char* end) {
	(void)end;
	tcErrAt(start);
	fprintf(stderr, "long long is not supported in this version\n");
	actionErrors++;
	TC_SET_CURRENT('i', 0);
}

/* Bitfelder werden GELESEN und ABGELEHNT. Vorher brach "unsigned a:3;" still
   im Parser ab -- kein Wort, keine Zeile. Umgesetzt sind sie nicht: dafuer
   braeuchte es Bitpacking im Struct-Layout und maskierten Zugriff an jeder
   Feldzugriffsstelle, und im Microware-Korpus kommen sie in 5 von 4109
   Quellen vor. Lieber eine ehrliche Meldung als ein stiller Abbruch. */
void tc_bitfield(const char* start, const char* end) {
	(void)end;
	tcErrAt(start);
	fprintf(stderr, "bit fields are not supported in this version\n");
	actionErrors++;
}

void tc_structfield(const char* start, const char* end) {
	/* Spanne deckt "type fieldName [ "[" arraySize "]" ] ;" ab -- Feldname + optionales
	   Array-Suffix sind das letzte Wort vor ';' (kein Leerraum zwischen Name und "["). */
	const char* p = end - 1; const char* fieldEnd; const char* fieldStart; const char* bracket;
	int arrayLen = 0; int rowLen = 0;
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
		/* optionale ZWEITE Dimension ("char args[6][64];"): arrayLen wird die
		   GESAMTelementzahl (6*64), rowLen merkt sich die Zeilenlaenge (64),
		   damit "x.feld[i]" spaeter um ganze ZEILEN springt statt um Elemente. */
		while (q < fieldEnd && (*q == ']' || *q == ' ' || *q == '\t')) q++;
		if (q < fieldEnd && *q == '[') {
			int dim2 = 0;
			q++;
			while (q < fieldEnd && *q >= '0' && *q <= '9') dim2 = dim2 * 10 + (*q++ - '0');
			if (dim2 > 0) { rowLen = dim2; arrayLen = arrayLen * dim2; }
		}
		fieldEnd = bracket;
	}
	if (tcStructBuildFieldCount < MAX_STRUCT_FIELDS) {
		tcStructBuildFieldTypes[tcStructBuildFieldCount] = tcCurrentType;
		/* Wie bei Variablen (tc_local): bei einem ZEIGER macht const den
		   Pointee konstant, sonst das Feld selbst. */
		if (tcFieldConst && tcIsPointer(tcCurrentType))
			tcStructBuildFieldTypes[tcStructBuildFieldCount].pointeeConst = 1;
		tcStructBuildFieldConst[tcStructBuildFieldCount] =
			(char)(tcFieldConst && !tcIsPointer(tcCurrentType));
		tcStructBuildFieldArrayLen[tcStructBuildFieldCount] = arrayLen;
		tcStructBuildFieldRowLen[tcStructBuildFieldCount] = rowLen;
		tcCopy(tcStructBuildFieldNames[tcStructBuildFieldCount], fieldStart, fieldEnd);
		tcStructBuildFieldCount++;
	} else { tcErrAt(start); fprintf(stderr, "too many struct fields\n"); actionErrors++; }
}

/* Gemeinsamer Kern fuer benannte structs (tc_structend) UND anonyme structs
   inline im typedef (tc_typedefend, 2026-07-24) -- Name kommt bei letzterem
   erst NACH dem Feld-Body, daher als eigenstaendige Funktion mit Name als
   Parameter statt fest an tcStructBuildName gebunden. Gibt die neue sid oder
   -1 bei Fehler zurueck. */
/* Aus den beiden Layoutergebnissen (P=4 und P=8) die Zahl der Zeigergroessen
   in einem Offset. */
static int tcPtrN(int v4, int v8) {
	return (v8 - v4) / (TC_PTR_LARGE - TC_PTR_SMALL);
}
/* Prueft, ob ein Wertepaar wirklich die Form k+n*P hat. Die Ausrichtung
   rundet auf, und eine Rundung ist keine lineare Funktion von P -- fuer die
   hier erlaubten Feldtypen geht es auf, aber darauf wird sich nicht
   VERLASSEN, sondern es wird nachgerechnet. */
static int tcPtrLinear(int v4, int v8) {
	if (v8 < v4) return 0;
	if (((v8 - v4) % (TC_PTR_LARGE - TC_PTR_SMALL)) != 0) return 0;
	if (v4 - TC_PTR_SMALL * tcPtrN(v4, v8) < 0) return 0;
	return 1;
}
static int tcRegisterStruct(const char* nameStart, const char* nameEnd) {
	int i; int offset = 0; int sid = tcStructCount; int pass;
	int predeclared = 0;
	/* Vorab eingetragen (tcStructPredeclare)? Dann DIESEN Eintrag fuellen --
	   sonst gaebe es den Namen zweimal, und die Duplikatspruefung unten
	   schluege gegen den eigenen Vorgriff an. */
	if (tcStructPending >= 0 && tcEqSpan(nameStart, nameEnd, tcStructNames[tcStructPending])) {
		sid = tcStructPending;
		predeclared = 1;
	}
	if (!predeclared) {
		int existing = tcLookupStruct(nameStart, nameEnd);
		/* Ein Eintrag mit NULL Feldern ist eine Vorwaertsdeklaration
		   ("struct N;" oder der Vorgriff aus tc_structbegin) und wird jetzt
		   vervollstaendigt -- kein Duplikat. Erst ein Eintrag MIT Feldern ist
		   eine zweite Definition und damit ein Fehler. */
		if (existing >= 0 && tcStructFieldCount[existing] == 0) {
			sid = existing;
			predeclared = 1;
		}
		else if (existing >= 0) { tcErrAt(parserActionAt); fprintf(stderr, "duplicate struct\n"); actionErrors++; return -1; }
		else if (tcStructCount >= MAX_STRUCTS) { tcErrAt(parserActionAt); fprintf(stderr, "too many structs\n"); actionErrors++; return -1; }
	}
	if (tcStructBuildFieldCount == 0) { tcErrAt(parserActionAt); fprintf(stderr, "struct needs at least one field\n"); actionErrors++; return -1; }
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
	/* VALIDIERUNG der Feldtypen. Seit 2026-09-15 vom Layout GETRENNT, weil
	   das Layout zweimal laeuft -- sonst kaeme jede Meldung doppelt. */
	for (i = 0; i < tcStructBuildFieldCount; i++) {
		TCType ft = tcStructBuildFieldTypes[i];
		/* Ein struct-WERT waere eine rekursive/verschachtelte Einbettung und
		   bleibt absichtlich offen. Ein Pointer auf struct ist dagegen ein
		   normaler Pointer-Slot (z.B. ein Linked-List-next oder Q9-Run's
		   qrun_stackval_t* stack) und muss wie jeder andere Pointer erlaubt
		   sein. */
		if ((ft.base == 's' && !tcIsPointer(ft)) ||
		    (ft.base == 'v' && !tcIsPointer(ft))) {
			tcErrAt(parserActionAt); fprintf(stderr, "struct field type not supported in this version\n"); actionErrors++; return -1;
		}
		/* ZEIGERARRAYS ALS FELD gehen seit 2026-09-07 (vorher abgelehnt).
		   Gebraucht hat sie qcc_backend_c.cpp: "char* args[6]" in seiner
		   Instr-Struktur -- daran ist das Backend bis dahin gescheitert und
		   konnte deshalb nie auf dem 68030 laufen. Die Schrittweite beim
		   Indizieren kommt aus tcEmitFieldIndexStep.
		   ZWEIDIMENSIONAL bleibt abgelehnt: der 2D-Zweig der Zugriffe rechnet
		   die Zeilengroesse mit 1 oder 4 Byte je Element aus, und lieber eine
		   Meldung als still eine falsche Schrittweite. */
		if (tcIsPointer(ft) && tcStructBuildFieldRowLen[i] > 0) {
			tcErrAt(parserActionAt); fprintf(stderr, "two-dimensional pointer arrays as struct field not supported in this version\n"); actionErrors++; return -1;
		}
	}
	/* LAYOUT ZWEIMAL RECHNEN (2026-09-15). Ein Zeigerfeld belegt auf dem 68k
	   vier, auf ARM64 acht Byte. Frueher stand hier fest die Acht, damit EIN
	   frontend-berechnetes Offset fuer beide Ziele gilt -- auf dem 68k war
	   damit die Haelfte jedes Zeigerfelds verschenkt. Jetzt wird jedes Offset
	   fuer BEIDE Zeigergroessen gerechnet und als Paar k+n*P weitergegeben;
	   das Backend setzt sein P ein. Die IR bleibt fuer beide dieselbe. */
	for (pass = 0; pass < 2; pass++) {
		int psize;
		int maxSize;
		psize = pass == 0 ? TC_PTR_SMALL : TC_PTR_LARGE;
		offset = 0;
		maxSize = 0;
		for (i = 0; i < tcStructBuildFieldCount; i++) {
			TCType ft = tcStructBuildFieldTypes[i]; int elemSize, align, size;
			elemSize = tcIsPointer(ft) ? psize : (ft.base == 'c' || ft.base == 'b') ? 1 : ft.base == 'h' ? 2 : ft.base == 'd' ? 8 : 4;
			/* Die AUSRICHTUNG ist bei double nicht die Groesse: xcc richtet ein
			   double nur auf 2 Byte aus (68k-Wortausrichtung), nicht auf 8 --
			   nachgemessen an "struct { char c; double d; }", das xcc mit 10
			   Byte belegt und nicht mit 16. Ohne diese Unterscheidung waeren
			   QCC-Strukturen mit double nicht mehr ABI-gleich zu xcc, und
			   genau dort sitzen die MWOS-Header. */
			align = (ft.base == 'd' && !tcIsPointer(ft)) ? 2 : elemSize;
			size = tcStructBuildFieldArrayLen[i] > 0 ? elemSize * tcStructBuildFieldArrayLen[i] : elemSize;
			/* UNION: alle Felder beginnen bei 0 und teilen sich denselben
			   Speicher; die Groesse ist die des groessten Felds. Sonst wie
			   bei einer struct -- und weil die union auch als struct
			   REGISTRIERT wird, gilt der ganze Rest (Zugriff, Kopie,
			   Parameter) unveraendert. */
			if (tcBuildIsUnion) {
				if (pass == 0) tcStructFieldOffsetK[sid][i] = 0;
				else tcStructFieldOffsetN[sid][i] = 0;
				if (size > maxSize) maxSize = size;
				continue;
			}
			offset = (offset + align - 1) & ~(align - 1);
			if (pass == 0) tcStructFieldOffsetK[sid][i] = offset;
			else tcStructFieldOffsetN[sid][i] = offset;
			offset += size;
		}
		if (tcBuildIsUnion) offset = maxSize;
		if (pass == 0) tcStructByteSizeK[sid] = (offset + 3) & ~3;
		else tcStructByteSizeN[sid] = (offset + 3) & ~3;
	}
	/* Aus den beiden Ergebnissen k und n ableiten -- und NACHRECHNEN, statt
	   die Linearitaet anzunehmen (s. tcPtrLinear). */
	for (i = 0; i < tcStructBuildFieldCount; i++) {
		int v4; int v8; int n;
		v4 = tcStructFieldOffsetK[sid][i];
		v8 = tcStructFieldOffsetN[sid][i];
		if (!tcPtrLinear(v4, v8)) {
			tcErrAt(parserActionAt); fprintf(stderr, "struct field offset is not linear in the pointer size\n"); actionErrors++; return -1;
		}
		n = tcPtrN(v4, v8);
		tcStructFieldOffsetK[sid][i] = v4 - TC_PTR_SMALL * n;
		tcStructFieldOffsetN[sid][i] = n;
	}
	{
		int v4; int v8; int n;
		v4 = tcStructByteSizeK[sid];
		v8 = tcStructByteSizeN[sid];
		if (!tcPtrLinear(v4, v8)) {
			tcErrAt(parserActionAt); fprintf(stderr, "struct size is not linear in the pointer size\n"); actionErrors++; return -1;
		}
		n = tcPtrN(v4, v8);
		tcStructByteSizeK[sid] = v4 - TC_PTR_SMALL * n;
		tcStructByteSizeN[sid] = n;
	}
	/* Ueber sid, nicht ueber tcStructCount: bei einem vorab eingetragenen
	   struct sind die beiden NICHT gleich. */
	tcCopy(tcStructNames[sid], nameStart, nameEnd);
	tcStructFieldCount[sid] = tcStructBuildFieldCount;
	for (i = 0; i < tcStructBuildFieldCount; i++) {
		tcStructFieldTypes[sid][i] = tcStructBuildFieldTypes[i];
		tcStructFieldConst[sid][i] = tcStructBuildFieldConst[i];
		tcStructFieldArrayLen[sid][i] = tcStructBuildFieldArrayLen[i];
		tcStructFieldRowLen[sid][i] = tcStructBuildFieldRowLen[i];
		tcCopy(tcStructFieldNames[sid][i], tcStructBuildFieldNames[i], tcStructBuildFieldNames[i] + strlen(tcStructBuildFieldNames[i]));
	}
	if (!predeclared) tcStructCount++;
	tcStructPending = -1;
	return sid;
}

/* Feuert am "{" eines benannten struct/union -- s. tcStructHasBody. */
void tc_structbodyopen(const char* start, const char* end) {
	(void)start; (void)end;
	tcStructHasBody = 1;
}

void tc_structend(const char* start, const char* end) {
	(void)start; (void)end;
	/* Ohne Rumpf ist es eine Vorwaertsdeklaration ("struct N;"): der Name ist
	   von tc_structbegin bereits angemeldet (tcStructPredeclare), die Felder
	   traegt die spaetere vollstaendige Definition nach. Hier ist dann nichts
	   zu tun -- tcRegisterStruct wuerde "struct needs at least one field"
	   melden, was fuer diesen Fall falsch waere. */
	if (!tcStructHasBody) { tcStructPending = -1; return; }
	tcStructHasBody = 0;
	tcRegisterStruct(tcStructBuildName, tcStructBuildName + strlen(tcStructBuildName));
}

/* NACHTRAG 2026-09-09 -- Mehrfachdeklaratoren-Fix, ersetzt das fruehere
   tc_typedefend (das per Rueckwaertssuche ueber die GANZE "typedef ...;"-
   Spanne den letzten Namen vor ';' suchte -- ging nur, solange es hoechstens
   einen Namen gab). Die Aktion haengt jetzt an typedefTargetName selbst
   (siehe tc_local/tc_structfield fuer dasselbe Muster): start/end SIND
   bereits der Name, kein Scannen noetig. pointerDecl ist als Teil von
   typedefDeclarator vorher gelaufen und hat tcCurrentType.pointers schon
   auf den Zeigergrad DIESES Deklarators gesetzt (tcBasePointers-Basis, wie
   bei var-/struct-/globalDeclarator). */
void tc_typedeftarget(const char* start, const char* end) {
	if (tcTypedefCount >= MAX_TYPEDEFS) { tcErrAt(start); fprintf(stderr, "too many typedefs\n"); actionErrors++; return; }
	if (tcLookupTypedef(start, end) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate typedef '%.*s'\n", (int)(end - start), start); actionErrors++; return; }
	if (tcAnonStructPending) {
		/* anonymes struct inline im typedef (2026-07-24): jetzt erst registrieren, mit dem
		   ERSTEN typedef-Zielnamen als internem struct-Tag (siehe tc_anonstructbegin).
		   tcCurrentType.pointers bleibt unberuehrt (kam schon von diesem Deklarator
		   eigenem pointerDecl, etwa bei "typedef struct {...} *P;") -- nur base/
		   structId werden auf die gerade registrierte Struktur gesetzt, UND bleiben
		   in tcCurrentType stehen, damit ein zweiter Deklarator ("...,*Q;") im
		   else-Zweig unten dieselbe Struktur sieht. tcBasePointers=0: ab jetzt hat
		   diese typedef-Zeile kein "type" mehr, das ihn setzen wuerde. */
		int sid = tcRegisterStruct(start, end);
		tcAnonStructPending = 0;
		if (sid < 0) return;
		tcCurrentType.base = 's';
		tcCurrentType.structId = (unsigned char)(sid + 1);
		tcCurrentType.pointeeConst = 0;
		tcBasePointers = 0;
	}
	tcTypedefTypes[tcTypedefCount] = tcCurrentType;
	tcCopy(tcTypedefNames[tcTypedefCount], start, end);
	tcTypedefCount++;
}

/* Rumpf einer Aufzaehlung ab dem "{" -- gemeinsam genutzt von der normalen
   Deklaration (tc_enumdecl) und der Form im typedef (tc_anonenum). Die
   Konstanten werden als ROHTEXT gelesen; die Grammatik muss die Form nur
   annehmen. */
static void tcParseEnumBody(const char* start, const char* end) {
	const char* p = start;
	long value = 0;
	while (p < end && *p != '{') p++;
	if (p == end) return;
	p++;
	for (;;) {
		const char* ne;
		const char* ns;
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		if (p < end && *p == '}') break;
		ns = p;
		ne = tcWordEnd(p, end);
		if (ne == p) break;
		p = ne;
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		/* EXPLIZITER WERT (C89 3.5.2.2, seit 2026-09-15): "A = 5". Er setzt
		   den Zaehler NEU, die folgenden Konstanten zaehlen von dort weiter --
		   "enum { A = 5, B }" macht B zu 6. Deshalb wird der Name erst
		   GEMERKT und der Eintrag danach geschrieben: vorher stand der Wert
		   schon in der Tabelle, bevor das "=" ueberhaupt gelesen war. */
		if (p < end && *p == '=') {
			int neg = 0; long v = 0; int digits = 0;
			p++;
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
			if (p < end && *p == '-') { neg = 1; p++; }
			while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); digits = 1; }
			if (!digits) { tcErrAt(ns); fprintf(stderr, "enum constant value must be a number\n"); actionErrors++; return; }
			value = neg ? -v : v;
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		}
		if (tcEnumConstCount >= MAX_ENUM_CONSTANTS) { tcErrAt(start); fprintf(stderr, "too many enum constants\n"); actionErrors++; }
		else if (tcLookupEnumConst(ns, ne) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate enum constant\n"); actionErrors++; }
		else {
			tcCopy(tcEnumConstNames[tcEnumConstCount], ns, ne);
			tcEnumConstValues[tcEnumConstCount] = value;
			tcEnumConstCount++;
		}
		value++;
		if (p < end && *p == ',') { p++; continue; }
		break;
	}
}
void tc_enumdecl(const char* start, const char* end) {
	/* Spanne deckt "enum Name { A, B, C } ;" ab -- Konstanten selbst per Rohtext, wie tc_globalend. */
	const char* p = start; const char* nameStart; const char* nameEnd;
	while (p < end && *p != ' ' && *p != '\t') p++;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	nameStart = p; nameEnd = tcWordEnd(p, end);
	if (nameEnd > nameStart) {
		if (tcEnumTypeCount < MAX_ENUM_TYPES) tcCopy(tcEnumTypeNames[tcEnumTypeCount++], nameStart, nameEnd);
		else { tcErrAt(start); fprintf(stderr, "too many enum types\n"); actionErrors++; }
	}
	tcParseEnumBody(p, end);
}

void tc_sizeof(const char* start, const char* end) {
	int size;
	int sizeN = 0;   /* Zeigeranteil der Groesse, s. tcEmitNum */
	const char* q;
	int ptrs;
	tc_type(start, end);
	/* DEN ZEIGERGRAD HIER SELBST ZAEHLEN. tc_type setzt tcCurrentType NEU
	   (TC_SET_CURRENT) und loescht damit, was tc_pointerdecl fuer dieselbe
	   Spanne schon gezaehlt hat -- die Aktion an pointerDecl laeuft VOR
	   dieser hier, sizeofType ist "sizeofBaseType pointerDecl". Ohne diese
	   Schleife lieferte sizeof(char *) STILL die 1, also die Groesse von
	   char: der Zweig unten ist eigens fuer Zeiger da, wurde aber nie
	   erreicht. Der Mangel war nicht die fehlende Unterstuetzung -- die
	   ist bewusst und in docs/ISO_C_GAP_LIST_de.md vermerkt -- sondern
	   dass sie SCHWEIGEND ein falsches Ergebnis lieferte. Ein
	   malloc(n * sizeof(char*)) bekam so ein Viertel des Noetigen.
	   Additiv, damit ein zeigerwertiges typedef mit weiteren Sternen
	   (sizeof(TCPtr *)) richtig zaehlt -- dieselbe Zaehlschleife wie in
	   tc_pointerdecl. */
	ptrs = 0;
	for (q = start; q < end; q++) if (*q == '*') ptrs++;
	while (ptrs > 0 && tcCurrentType.pointers < 255) {
		tcCurrentType.pointers++;
		ptrs--;
	}
	if (tcCurrentType.pointers) {
		/* SEIT 2026-09-15 UNTERSTUETZT. Vorher abgelehnt, weil die Groesse
		   eines Zeigers vom Ziel abhaengt (68k 4, ARM64 8) und das Frontend
		   nur EINE Zahl schreiben konnte -- eine falsche Zahl waere ein
		   malloc(n * sizeof(char*)) mit einem Viertel des Noetigen gewesen.
		   Jetzt geht die Groesse symbolisch als 0+1P hinaus und das Backend
		   setzt sein P ein. */
		size = 0; sizeN = 1;
	} else if (tcCurrentType.base == 's') {
		size = tcStructByteSizeK[tcCurrentType.structId - 1];
		sizeN = tcStructByteSizeN[tcCurrentType.structId - 1];
	} else if (tcCurrentType.base == 'c' || tcCurrentType.base == 'b') size = 1;
	else if (tcCurrentType.base == 'h') size = 2;
	else if (tcCurrentType.base == 'd') size = 8;
	else size = 4;
	{ printf("PUSH "); tcEmitNum(size, sizeN); printf("\n"); }
	tcTypePush(tcMakeType('i', 0));
}

void tc_sizeofvar(const char* start, const char* end) {
	int slot = tcLookupLocal(start, end), global = -1; TCType t; int size; int count;
	int sizeN = 0;   /* Zeigeranteil, s. tcEmitNum */
	if (slot < 0) global = tcLookupGlobal(start, end);
	if (slot < 0 && global < 0) {
		/* Zweiter legitimer Fall dieses Zweigs seit der Grammatikkorrektur
		   (s. sizeofBaseType in qcc.ebnf): ein nackter typedef-Name wie
		   sizeof(ActionLogEntry). Der kam frueher ueber sizeofType herein.
		   Bewusst ohne TCType-Zwischenvariable, nur ueber die drei Felder --
		   das haelt die Routine frei von Struct-Kopien auf dem 68k-Weg. */
		int td = tcLookupTypedef(start, end);
		int tdBase; int tdPtrs; int tdSid; int tdSize; int tdSizeN = 0;
		if (td < 0) { tcErrAt(start); fprintf(stderr, "unknown variable in sizeof: '%.*s'\n", (int)(end - start), start); actionErrors++; tcTypePush(tcBadType()); return; }
		tdBase = tcTypedefTypes[td].base;
		tdPtrs = tcTypedefTypes[td].pointers;
		tdSid = tcTypedefTypes[td].structId;
		if (tdPtrs) {
			tdSize = 0; tdSizeN = 1;   /* s. tc_sizeof */
		} else if (tdBase == 's') { tdSize = tcStructByteSizeK[tdSid - 1]; tdSizeN = tcStructByteSizeN[tdSid - 1]; }
		else if (tdBase == 'c' || tdBase == 'b') tdSize = 1;
		else if (tdBase == 'h') tdSize = 2;
		else tdSize = 4;
		{ printf("PUSH "); tcEmitNum(tdSize, tdSizeN); printf("\n"); }
		tcTypePush(tcMakeType('i', 0));
		return;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	count = slot >= 0 ? tcLocalArrayLen[slot] : tcGlobalArrayLen[global];
	if (tcIsPointer(t)) {
		/* Ein Zeiger-ARRAY zaehlt seine Elemente mit, s. tc_sizeof. */
		size = 0; sizeN = count > 0 ? count : 1;
	} else if (t.base == 's') {
		size = (count > 0 ? count : 1) * tcStructByteSizeK[t.structId - 1];
		sizeN = (count > 0 ? count : 1) * tcStructByteSizeN[t.structId - 1];
	} else {
		int elemSize = (t.base == 'c' || t.base == 'b') ? 1 : t.base == 'h' ? 2 : t.base == 'd' ? 8 : 4;
		size = count > 0 ? count * elemSize : elemSize;
	}
	{ printf("PUSH "); tcEmitNum(size, sizeN); printf("\n"); }
	tcTypePush(tcMakeType('i', 0));
}

void tc_preincdec(const char* start, const char* end) {
	int isDec = (start[0] == '-');
	const char* p = start + 2; const char* nameStart; const char* nameEnd;
	int slot, global = -1; TCType t;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	/* "++(*p)": Klammerform, siehe tcDerefIncDec */
	if (p < end && *p == '(') { tcDerefIncDec(p, end, isDec, 1); return; }
	if (tcWordEnd(p, end) < end && *tcWordEnd(p, end) == '[') { tcIndexIncDec(p, end, isDec, 1); return; }
	/* siehe tc_postincdec: "--x" darf nicht als Member-Zugriff gelten. */
	{ const char* we = tcWordEnd(p, end);
	  if (we < end && (*we == '.' || (*we == '-' && we + 1 < end && we[1] == '>'))) { tcMemberIncDec(p, end, isDec, 1); return; } }
	nameStart = p; nameEnd = tcWordEnd(p, end);
	slot = tcLookupLocal(nameStart, nameEnd);
	if (slot < 0) global = tcLookupGlobal(nameStart, nameEnd);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(nameEnd - nameStart), nameStart); actionErrors++; tcTypePush(tcBadType()); return; }
	if ((slot >= 0 && tcLocalConst[slot]) || (global >= 0 && tcGlobalConst[global])) {
		tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIncDecCheck(slot, global, t)) {
		tcErrAt(start); fprintf(stderr, "++/-- only supported for plain int/unsigned/char variables in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tcIncDecEmit(slot, global, t, isDec, 1);
}

void tc_postincdec(const char* start, const char* end) {
	const char* nameStart = start; const char* nameEnd;
	int isDec = (end[-1] == '-');
	int slot, global = -1; TCType t;
	if (start < end && *start == '(') { tcDerefIncDec(start, end, isDec, 0); return; }
	if (tcWordEnd(start, end) < end && *tcWordEnd(start, end) == '[') { tcIndexIncDec(start, end, isDec, 0); return; }
	/* NUR bei "." oder einem echten "->": bei "x--" endet das Wort ebenfalls
	   vor einem "-", das ist aber der Dekrement-Operator und kein Member-
	   Zugriff. Ohne die ">"-Pruefung meldete jedes "x--" faelschlich
	   "'->' requires a pointer to struct". */
	{ const char* we = tcWordEnd(start, end);
	  if (we < end && (*we == '.' || (*we == '-' && we + 1 < end && we[1] == '>'))) { tcMemberIncDec(start, end, isDec, 0); return; } }
	nameEnd = tcWordEnd(start, end);
	slot = tcLookupLocal(nameStart, nameEnd);
	if (slot < 0) global = tcLookupGlobal(nameStart, nameEnd);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(nameEnd - nameStart), nameStart); actionErrors++; tcTypePush(tcBadType()); return; }
	if ((slot >= 0 && tcLocalConst[slot]) || (global >= 0 && tcGlobalConst[global])) {
		tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIncDecCheck(slot, global, t)) {
		tcErrAt(start); fprintf(stderr, "++/-- only supported for plain int/unsigned/char variables in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tcIncDecEmit(slot, global, t, isDec, 0);
}

void tc_incdecstmt(const char* start, const char* end) {
	(void)start; (void)end;
	tcDropExprResult();
}

void tc_forstep(const char* start, const char* end) {
	/* forStep = target assignop expr | postIncDec | preIncDec -- unterschieden per '='-Suche,
	   genau wie tc_assign es fuer assignStmt intern schon tut. */
	const char* p;
	for (p = start; p < end; p++) if (*p == '=') { tc_assign(start, end); return; }
	tcDropExprResult();
}

void tc_switchbegin(const char* start, const char* end) {
	int endLabel; (void)start; (void)end;
	if (tcSwitchDepth >= MAX_SWITCH) { tcErrAt(start); fprintf(stderr, "switch nesting too deep\n"); actionErrors++; return; }
	endLabel = tcNextLabel++;
	tcSwitchBodyLabel[tcSwitchDepth] = -1;
	tcSwitchNextLabel[tcSwitchDepth] = -1;
	tcSwitchFallLabel[tcSwitchDepth] = -1;
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
	if (d < 0) { tcErrAt(start); fprintf(stderr, "case outside switch\n"); actionErrors++; return; }
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
		if (ec < 0) { tcErrAt(start); fprintf(stderr, "unknown case value\n"); actionErrors++; return; }
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
	/* Durchfall aus dem vorigen Rumpf landet HINTER dem DROP. */
	if (tcSwitchFallLabel[d] >= 0) {
		printf("LABEL L%d\n", tcSwitchFallLabel[d]);
		tcSwitchFallLabel[d] = -1;
	}
}

void tc_casegroup_end(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	/* DURCHFALL (C89 3.6.4.2): ohne break laeuft die Ausfuehrung in den
	   naechsten case-Rumpf weiter. Bis 2026-09-15 stand hier ein
	   unbedingtes "JMP <switch-Ende>" -- der Durchfall wurde also still
	   uebersprungen, ohne Meldung und mit falschem Ergebnis.
	   Das Ziel ist der naechste RUMPF, nicht der naechste TEST: der Wert
	   darf nicht erneut verglichen werden. Weil dessen Label hier noch
	   nicht vergeben ist, wird es reserviert und vom naechsten Rumpf
	   eingeloest -- und zwar HINTER dessen DROP, denn auf diesem Weg ist
	   der switch-Wert bereits vom Stapel (sonst liefe der Stapel leer). */
	tcSwitchFallLabel[d] = tcNextLabel++;
	printf("JMP L%d\nLABEL L%d\n", tcSwitchFallLabel[d], tcSwitchNextLabel[d]);
	tcSwitchGroupOpen[d] = 0;
}

void tc_defaultlabel(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	tcSwitchHadDefault[d] = 1;
	printf("DROP\n");
	/* Durchfall aus dem letzten case in den default-Rumpf, s. oben. */
	if (tcSwitchFallLabel[d] >= 0) {
		printf("LABEL L%d\n", tcSwitchFallLabel[d]);
		tcSwitchFallLabel[d] = -1;
	}
}

void tc_switchend(const char* start, const char* end) {
	int d; (void)start; (void)end;
	if (!tcNeedCtrl('s')) return;
	d = tcSwitchDepth - 1;
	if (!tcSwitchHadDefault[d]) printf("DROP\n");
	/* Faellt der LETZTE Rumpf durch, folgt kein weiterer -- das reservierte
	   Label wird hier eingeloest, hinter dem DROP des Test-Pfads. */
	if (tcSwitchFallLabel[d] >= 0) {
		printf("LABEL L%d\n", tcSwitchFallLabel[d]);
		tcSwitchFallLabel[d] = -1;
	}
	printf("LABEL L%d\n", tcSwitchEndLabel[d]);
	tcSwitchDepth--;
	tcCtrlDepth--;
}

void tc_castcapture(const char* start, const char* end) {
	const char* p;
	tc_type(start, end);
	/* tc_type() resolves the base (including typedef ActionFn).  The cast
	   grammar additionally permits pointer stars after it, just as a normal
	   declaration does. */
	for (p = start; p < end; p++) if (*p == '*') tcCurrentType = tcPointerTo(tcCurrentType);
	if (tcCastDepth < 16) tcCastStack[tcCastDepth++] = tcCurrentType;
	else { tcErrAt(start); fprintf(stderr, "cast nesting too deep\n"); actionErrors++; }
}

void tc_cast(const char* start, const char* end) {
	TCType target; TCType src = tcTypePop(); (void)start; (void)end;
	if (tcCastDepth <= 0) { tcTypePush(tcBadType()); return; }
	target = tcCastStack[--tcCastDepth];
	/* ANSI C stellt an den Cast-Operanden genau eine Anforderung, C89 3.3.4
	   "Cast operators" (wortgleich als C99 6.5.4p2 uebernommen): "the operand
	   shall have scalar type". Skalar heisst arithmetischer Typ ODER
	   Zeigertyp. Zulaessig sind hier also int/unsigned/char/enum, bool, Zeiger
	   und Funktionszeiger; eine Constraint-Verletzung -- und damit eine von der
	   Norm VORGESCHRIEBENE Diagnose -- sind struct und void.
	   ZWEI ECHTE ABWEICHUNGEN GEFIXT (2026-09-01, gemessen):
	   - bool fehlte in der Zulassung, weil tcIsInteger() 'b' ausschliesst
	     (dieselbe Wurzel wie der Zielseiten-Fehler weiter unten). (int)b auf
	     einem bool wurde damit abgelehnt, obwohl bool ein arithmetischer und
	     folglich skalarer Typ ist.
	   - Der Meldungstext lautete "cast expects int" und lief ueber
	     tcTypeError(), das einen TCType als Sollwert druckt. Verlangt wird
	     aber nicht int, sondern ein SKALARER Operand -- die Meldung war
	     irrefuehrend, deshalb hier eine eigene statt tcTypeError().
	   Die Zeiger-Quelle ist NICHT zu diagnostizieren: C89 3.3.4 erlaubt sie
	   ausdruecklich, die Umwandlung selbst ist implementierungsdefiniert
	   (C99 6.3.2.3p6). Auf dem 68k-Ziel sind int und Zeiger beide 32 Bit, der
	   Wert passt also exakt. */
	if (!tcIsInteger(src) && !tcIsBool(src) && !tcIsDouble(src) && !tcIsPointer(src) && !tcIsFnPtr(src)) {
		tcErrAt(start); fprintf(stderr, "cast expects scalar operand, got ");
		tcPrintType(stderr, src); fputc('\n', stderr); actionErrors++;
		tcTypePush(target); return;
	}
	/* GLEITKOMMA (2026-09-16). double ist ein arithmetischer und damit
	   skalarer Typ, aber zwischen double und Zeiger gibt es keine
	   Umwandlung -- C89 3.3.4 laesst Zeiger nur gegen Ganzzahlen zu.
	   D2I schneidet Richtung null ab, wie es C vorschreibt (und wie
	   fintrz auf dem 68k es tut), rundet also nicht. */
	if (tcIsDouble(src) && (tcIsPointer(target) || tcIsFnPtr(target))) {
		tcErrAt(start); fprintf(stderr, "cannot cast double to a pointer\n");
		actionErrors++; tcTypePush(target); return;
	}
	if (tcIsDouble(target) && (tcIsPointer(src) || tcIsFnPtr(src))) {
		tcErrAt(start); fprintf(stderr, "cannot cast a pointer to double\n");
		actionErrors++; tcTypePush(target); return;
	}
	if (tcIsDouble(src) && !tcIsDouble(target)) printf("D2I\n");
	if (!tcIsDouble(src) && tcIsDouble(target)) printf("I2D\n");
	/* ECHTER BUG GEFUNDEN + GEFIXT (2026-09-01, aus dem Diff von 3c37198
	   belegt): der Waechter hiess dort nur tcIsInteger(target) -- und
	   tcIsInteger() schliesst bool ausdruecklich aus (Definition weiter oben:
	   'i','u','c','z'). Der 'b'-Zweig darunter war damit ab 3c37198 TOTER
	   CODE, und (bool)x lieferte den Rohwert statt 0/1 (Testfall erwartet
	   '1\n0', geliefert wurde '5\n0'). Der Waechter selbst ist richtig und
	   gewollt: er verhindert, dass fuer ZEIGER-Ziele normalisiert wird. Dafuer
	   genuegt tcIsBool() daneben -- beide Praedikate schliessen pointers
	   ohnehin aus. */
	if (tcIsInteger(target) || tcIsBool(target)) {
		if (target.base == 'c') printf("NARROWC\n");
		else if (target.base == 'h') printf("NARROWH\n");
		else if (target.base == 'b') printf("PUSH 0\nCMPNE\n");
	}
	tcTypePush(target);
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
	if (tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '&' && tcHasTopChar(start, end, '&')) tcBitEnd('&');
}

void tc_bitxorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('^');
}

void tc_bitxorend(const char* start, const char* end) {
	if (tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '^' && tcHasTopChar(start, end, '^')) tcBitEnd('^');
}

void tc_bitorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('|');
}

void tc_bitorend(const char* start, const char* end) {
	if (tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '|' && tcHasTopChar(start, end, '|')) tcBitEnd('|');
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
	/* tcHasTopChar statt tcHasChar (2026-08-10): das "?" muss auf DIESER Ebene
	   stehen, nicht irgendwo in einer geklammerten Teilspanne. Bei einem
	   verschachtelten Ternaer ("a ? 1 : (b ? 2 : 3)") ist conditionalFalse
	   selbst eine conditionalExpr OHNE eigenen Ternaer -- ihre Spanne enthaelt
	   aber das "?" des geklammerten inneren Ausdrucks. Mit tcHasChar feuerte
	   sie deshalb ein zusaetzliches tcTernaryEnd, der Frame-Zaehler lief unter
	   und es gab "conditional-frame mismatch". */
	if (tcHasTopChar(start, end, '?')) tcTernaryEnd();
}

/* Betreten eines Blocks: aktuellen Slot-Stand merken (siehe tcScopeMark). */
void tc_blockopen(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcScopeDepth >= MAX_BLOCK_DEPTH) {
		tcErrAt(start); fprintf(stderr, "blocks nested too deeply\n"); actionErrors++; return;
	}
	tcScopeMark[tcScopeDepth] = tcLocalCount;
	tcScopeDepth++;
}

/* Verlassen eines Blocks: alle darin angelegten Slots unsichtbar machen. Die
   Slots selbst bleiben belegt (siehe tcScopeMark), nur ihre Namen sind fuer
   tcLookupLocal ab hier nicht mehr auffindbar. */
void tc_blockend(const char* start, const char* end) {
	int mark; int i;
	(void)start; (void)end;
	if (tcScopeDepth <= 0) return;   /* kann nur bei einem Parse-Fehler auftreten */
	tcScopeDepth--;
	mark = tcScopeMark[tcScopeDepth];
	for (i = mark; i < tcLocalCount; i++) tcLocalDead[i] = 1;
}

static void actionLogDispatch(int id, const char* start, const char* end) {
	parserActionAt = start;
	if (id == 1) { tc_externdeclend(start, end); return; }
	if (id == 2) { tc_externname(start, end); return; }
	if (id == 6) { tc_externparam(start, end); return; }
	if (id == 7) { tc_externvariadic(start, end); return; }
	if (id == 8) { tc_enumdecl(start, end); return; }
	if (id == 11) { tc_structend(start, end); return; }
	if (id == 13) { tc_structbodyopen(start, end); return; }
	if (id == 14) { tc_unionend(start, end); return; }
	if (id == 15) { tc_unionbegin(start, end); return; }
	if (id == 16) { tc_structbegin(start, end); return; }
	if (id == 17) { tc_fieldconstend(start, end); return; }
	if (id == 18) { tc_fieldconst(start, end); return; }
	if (id == 19) { tc_structfield(start, end); return; }
	if (id == 20) { tc_bitfield(start, end); return; }
	if (id == 24) { tc_fnptrtypedef(start, end); return; }
	if (id == 26) { tc_fnptrbegin(start, end); return; }
	if (id == 29) { tc_anonenum(start, end); return; }
	if (id == 34) { tc_anonstructbegin(start, end); return; }
	if (id == 35) { tc_typedeftarget(start, end); return; }
	if (id == 37) { tc_externglobaldecl(start, end); return; }
	if (id == 38) { tc_globalend(start, end); return; }
	if (id == 40) { tc_const(start, end); return; }
	if (id == 42) { tc_static(start, end); return; }
	if (id == 54) { tc_funcend(start, end); return; }
	if (id == 56) { tc_funcbodybegin(start, end); return; }
	if (id == 57) { tc_funcdeclend(start, end); return; }
	if (id == 58) { tc_funcbegin(start, end); return; }
	if (id == 64) { tc_paramend(start, end); return; }
	if (id == 65) { tc_fnptrparam(start, end); return; }
	if (id == 67) { tc_param(start, end); return; }
	if (id == 69) { tc_blockend(start, end); return; }
	if (id == 70) { tc_blockopen(start, end); return; }
	if (id == 73) { tc_fnptrvar(start, end); return; }
	if (id == 75) { tc_staticlocal(start, end); return; }
	if (id == 76) { tc_staticlocalname(start, end); return; }
	if (id == 79) { tc_staticruntimeinit(start, end); return; }
	if (id == 80) { tc_switchend(start, end); return; }
	if (id == 81) { tc_switchbegin(start, end); return; }
	if (id == 82) { tc_switchcond(start, end); return; }
	if (id == 84) { tc_casegroup_end(start, end); return; }
	if (id == 85) { tc_caselabelrun_end(start, end); return; }
	if (id == 86) { tc_caselabel(start, end); return; }
	if (id == 92) { tc_defaultlabel(start, end); return; }
	if (id == 96) { tc_localdecl(start, end); return; }
	if (id == 97) { tc_varinit(start, end); return; }
	if (id == 98) { tc_arrayinitstring(start, end); return; }
	if (id == 106) { tc_assign(start, end); return; }
	if (id == 107) { tc_chainassign(start, end); return; }
	if (id == 108) { tc_assignop(start, end); return; }
	if (id == 109) { tc_callstmt(start, end); return; }
	if (id == 111) { tc_voidcast(start, end); return; }
	if (id == 113) { tc_return(start, end); return; }
	if (id == 114) { tc_retval(start, end); return; }
	if (id == 115) { tc_ifend(start, end); return; }
	if (id == 116) { tc_ifbegin(start, end); return; }
	if (id == 118) { tc_ifcond(start, end); return; }
	if (id == 119) { tc_thenend(start, end); return; }
	if (id == 121) { tc_whileend(start, end); return; }
	if (id == 122) { tc_whilebegin(start, end); return; }
	if (id == 123) { tc_whilecond(start, end); return; }
	if (id == 125) { tc_forend(start, end); return; }
	if (id == 126) { tc_forbegin(start, end); return; }
	if (id == 127) { tc_forsep1(start, end); return; }
	if (id == 128) { tc_forsep2(start, end); return; }
	if (id == 129) { tc_forclose(start, end); return; }
	if (id == 131) { tc_assign(start, end); return; }
	if (id == 133) { tc_forcond(start, end); return; }
	if (id == 135) { tc_forstep(start, end); return; }
	if (id == 137) { tc_doend(start, end); return; }
	if (id == 138) { tc_dobegin(start, end); return; }
	if (id == 139) { tc_dowhiletok(start, end); return; }
	if (id == 140) { tc_docond(start, end); return; }
	if (id == 143) { tc_break(start, end); return; }
	if (id == 144) { tc_continue(start, end); return; }
	if (id == 145) { tc_goto(start, end); return; }
	if (id == 146) { tc_label(start, end); return; }
	if (id == 148) { tc_ternaryend(start, end); return; }
	if (id == 149) { tc_parenbegin(start, end); return; }
	if (id == 150) { tc_parenend(start, end); return; }
	if (id == 151) { tc_commaend(start, end); return; }
	if (id == 152) { tc_commadrop(start, end); return; }
	if (id == 154) { tc_commaassign(start, end); return; }
	if (id == 155) { tc_commavalue(start, end); return; }
	if (id == 156) { tc_ternarybegin(start, end); return; }
	if (id == 158) { tc_ternarymiddle(start, end); return; }
	if (id == 160) { tc_logicorend(start, end); return; }
	if (id == 161) { tc_logicorop(start, end); return; }
	if (id == 162) { tc_logicandend(start, end); return; }
	if (id == 163) { tc_logicandop(start, end); return; }
	if (id == 164) { tc_bitorend(start, end); return; }
	if (id == 165) { tc_bitorop(start, end); return; }
	if (id == 166) { tc_bitxorend(start, end); return; }
	if (id == 167) { tc_bitxorop(start, end); return; }
	if (id == 168) { tc_bitandend(start, end); return; }
	if (id == 169) { tc_bitandop(start, end); return; }
	if (id == 170) { tc_expr(start, end); return; }
	if (id == 172) { tc_shiftrhs(start, end); return; }
	if (id == 173) { tc_shiftop(start, end); return; }
	if (id == 174) { tc_relop(start, end); return; }
	if (id == 176) { tc_addop(start, end); return; }
	if (id == 177) { tc_term(start, end); return; }
	if (id == 178) { tc_mulop(start, end); return; }
	if (id == 179) { tc_factor(start, end); return; }
	if (id == 180) { tc_postfixindex(start, end); return; }
	if (id == 181) { tc_string(start, end); return; }
	if (id == 185) { tc_charlit(start, end); return; }
	if (id == 193) { tc_neg(start, end); return; }
	if (id == 194) { tc_addressref(start, end); return; }
	if (id == 197) { tc_sizeof(start, end); return; }
	if (id == 199) { tc_sizeofvar(start, end); return; }
	if (id == 200) { tc_cast(start, end); return; }
	if (id == 201) { tc_castcapture(start, end); return; }
	if (id == 203) { tc_preincdec(start, end); return; }
	if (id == 204) { tc_postincdec(start, end); return; }
	if (id == 209) { tc_incdecstmt(start, end); return; }
	if (id == 210) { tc_derefref(start, end); return; }
	if (id == 211) { tc_call(start, end); return; }
	if (id == 212) { tc_callmember(start, end); return; }
	if (id == 213) { tc_indcall(start, end); return; }
	if (id == 214) { tc_indcallbegin(start, end); return; }
	if (id == 216) { tc_arg(start, end); return; }
	if (id == 218) { tc_target(start, end); return; }
	if (id == 219) { tc_indirecttarget(start, end); return; }
	if (id == 220) { tc_varref(start, end); return; }
	if (id == 221) { tc_arg(start, end); return; }
	if (id == 222) { tc_callname(start, end); return; }
	if (id == 224) { tc_defname(start, end); return; }
	if (id == 226) { tc_local(start, end); return; }
	if (id == 228) { tc_callname(start, end); return; }
	if (id == 229) { tc_type(start, end); return; }
	if (id == 231) { tc_longlong(start, end); return; }
	if (id == 236) { tc_pointerdecl(start, end); return; }
	if (id == 239) { tc_number(start, end); return; }
	if (id == 241) { tc_number(start, end); return; }
	if (id == 247) { tc_floatlit(start, end); return; }
	actionErrors++;
}

static int p_program(void);
static int p_externDecl(void);
static int p_externName(void);
static int p_externParams(void);
static int p_externRealParams(void);
static int p_externParamList(void);
static int p_externParam(void);
static int p_ellipsisTok(void);
static int p_enumDecl(void);
static int p_enumItem(void);
static int p_enumValue(void);
static int p_structDecl(void);
static int p_structTail(void);
static int p_structBodyOpen(void);
static int p_unionDecl(void);
static int p_unionName(void);
static int p_structName(void);
static int p_structField(void);
static int p_fieldConstKw(void);
static int p_structDeclarator(void);
static int p_bitFieldWidth(void);
static int p_fieldName(void);
static int p_typedefDecl(void);
static int p_typedefDeclarator(void);
static int p_fnPtrTypedef(void);
static int p_fnPtrParams(void);
static int p_fnPtrOpen(void);
static int p_fnPtrName(void);
static int p_typedefType(void);
static int p_anonEnumType(void);
static int p_enumTagName(void);
static int p_anonEnumOpen(void);
static int p_anonStructType(void);
static int p_structTagName(void);
static int p_anonStructOpen(void);
static int p_typedefTargetName(void);
static int p_globalDecl(void);
static int p_externGlobalDecl(void);
static int p_plainGlobalDecl(void);
static int p_globalDeclarator(void);
static int p_constKw(void);
static int p_volatileKw(void);
static int p_staticKw(void);
static int p_globalInit(void);
static int p_globalStringInit(void);
static int p_arraySize(void);
static int p_arraySizeN(void);
static int p_constSize(void);
static int p_constSizeOp(void);
static int p_globalValue(void);
static int p_globalFloat(void);
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
static int p_fnPtrParam(void);
static int p_fnPtrParamName(void);
static int p_paramDecl(void);
static int p_paramArray(void);
static int p_block(void);
static int p_blockOpen(void);
static int p_statement(void);
static int p_unlabeledStmt(void);
static int p_fnPtrVarDecl(void);
static int p_fnPtrVarName(void);
static int p_staticVarDecl(void);
static int p_staticLocalName(void);
static int p_staticArrayDim(void);
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
static int p_registerKw(void);
static int p_varDeclarator(void);
static int p_localDecl(void);
static int p_varInit(void);
static int p_arrayStringInit(void);
static int p_initList(void);
static int p_initValue(void);
static int p_initString(void);
static int p_initBool(void);
static int p_initNeg(void);
static int p_initFloat(void);
static int p_initNumber(void);
static int p_assignStmt(void);
static int p_chainAssign(void);
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
static int p_forInitItem(void);
static int p_forComma(void);
static int p_forCond(void);
static int p_forStep(void);
static int p_forStepItem(void);
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
static int p_parenOpen(void);
static int p_parenClose(void);
static int p_commaExpr(void);
static int p_commaTok(void);
static int p_commaItem(void);
static int p_commaAssign(void);
static int p_commaValue(void);
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
static int p_stringPiece(void);
static int p_stringSep(void);
static int p_stringSpace(void);
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
static int p_sizeofBaseType(void);
static int p_sizeofVarName(void);
static int p_castExpr(void);
static int p_castType(void);
static int p_castOperand(void);
static int p_preIncDec(void);
static int p_postIncDec(void);
static int p_memberIncTarget(void);
static int p_indexIncTarget(void);
static int p_derefIncTarget(void);
static int p_incdecOp(void);
static int p_incDecStmt(void);
static int p_derefRef(void);
static int p_call(void);
static int p_callMember(void);
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
static int p_longDoubleType(void);
static int p_longLongType(void);
static int p_unionTypeRef(void);
static int p_structTypeRef(void);
static int p_enumTypeRef(void);
static int p_typedefRef(void);
static int p_pointerDecl(void);
static int p_pointerStar(void);
static int p_unsignedInt(void);
static int p_boolLit(void);
static int p_ident(void);
static int p_number(void);
static int p_integerSuffix(void);
static int p_hexNumber(void);
static int p_hexMark(void);
static int p_hexDigit(void);
static int p_decNumber(void);
static int p_floatLit(void);
static int p_floatTail(void);
static int p_floatDotTail(void);
static int p_floatExp(void);
static int p_floatExpSign(void);
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
	if (!p_unionDecl()) goto L6;
	goto L3;
L6:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_typedefDecl()) goto L7;
	goto L3;
L7:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_enumDecl()) goto L8;
	goto L3;
L8:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_externDecl()) goto L9;
	goto L3;
L9:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_funcdef()) goto L10;
	goto L3;
L10:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L2;
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
	if (strncmp(p, "extern", 6) != 0) goto L11;
	if (idch((unsigned char)p[6])) goto L11;
	p += 6;
	if (!p_type()) goto L11;
	if (!p_pointerDecl()) goto L11;
	if (!p_externName()) goto L11;
	if (!p_externParams()) goto L11;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L11;
	p += 1;
	actionLogPush(1, entry, p);	/* ACTION AFTER externDecl */
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
	actionLogPush(2, entry, p);	/* ACTION AFTER externName */
	return 1;
L12:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* externParams */
static int p_externParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_voidParams()) goto L15;
	goto L14;
L15:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_externRealParams()) goto L16;
	goto L14;
L16:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L13;
L14:	sp--;
	return 1;
L13:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* externRealParams */
static int p_externRealParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L17;
	p += 1;
	if (!p_externParamList()) goto L17;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L17;
	p += 1;
	return 1;
L17:	p = entry; actionLogLen = entryLog;
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
	if (!p_externParam()) goto L19;
L21:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L22;
	p += 1;
	if (!p_externParam()) goto L22;
	sp--; goto L21;
L22:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L23;
	p += 1;
	if (!p_ellipsisTok()) goto L23;
	sp--; goto L24;
L23:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L24:	;
	sp--; goto L20;
L19:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L20:	;
	return 1;
L18:	p = entry; actionLogLen = entryLog;
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
	if (!p_constKw()) goto L26;
	sp--; goto L27;
L26:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L27:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_volatileKw()) goto L28;
	sp--; goto L29;
L28:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L29:	;
	if (!p_type()) goto L25;
	if (!p_pointerDecl()) goto L25;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_ident()) goto L30;
	sp--; goto L31;
L30:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L31:	;
	actionLogPush(6, entry, p);	/* ACTION AFTER externParam */
	return 1;
L25:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "...", 3) != 0) goto L32;
	p += 3;
	actionLogPush(7, entry, p);	/* ACTION AFTER ellipsisTok */
	return 1;
L32:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "enum", 4) != 0) goto L33;
	if (idch((unsigned char)p[4])) goto L33;
	p += 4;
	ws();
	if (!p_ident()) goto L33;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L33;
	p += 1;
	if (!p_enumItem()) goto L33;
L34:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L35;
	p += 1;
	if (!p_enumItem()) goto L35;
	sp--; goto L34;
L35:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L33;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L33;
	p += 1;
	actionLogPush(8, entry, p);	/* ACTION AFTER enumDecl */
	return 1;
L33:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* enumItem */
static int p_enumItem(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L36;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L37;
	if (strncmp(p, "==", 2) == 0) goto L37;	/* Longest-Match */
	p += 1;
	if (!p_enumValue()) goto L37;
	sp--; goto L38;
L37:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L38:	;
	return 1;
L36:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* enumValue */
static int p_enumValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalNeg()) goto L41;
	goto L40;
L41:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalNumber()) goto L42;
	goto L40;
L42:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L39;
L40:	sp--;
	return 1;
L39:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "struct", 6) != 0) goto L43;
	if (idch((unsigned char)p[6])) goto L43;
	p += 6;
	if (!p_structName()) goto L43;
	if (!p_structTail()) goto L43;
	actionLogPush(11, entry, p);	/* ACTION AFTER structDecl */
	return 1;
L43:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structTail */
static int p_structTail(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structBodyOpen()) goto L46;
	if (!p_structField()) goto L46;
L47:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L48;
	sp--; goto L47;
L48:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L46;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L46;
	p += 1;
	goto L45;
L46:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L49;
	p += 1;
	goto L45;
L49:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L44;
L45:	sp--;
	return 1;
L44:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structBodyOpen */
static int p_structBodyOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L50;
	p += 1;
	actionLogPush(13, entry, p);	/* ACTION AFTER structBodyOpen */
	return 1;
L50:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* unionDecl */
static int p_unionDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "union", 5) != 0) goto L51;
	if (idch((unsigned char)p[5])) goto L51;
	p += 5;
	if (!p_unionName()) goto L51;
	if (!p_structTail()) goto L51;
	actionLogPush(14, entry, p);	/* ACTION AFTER unionDecl */
	return 1;
L51:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* unionName */
static int p_unionName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L52;
	actionLogPush(15, entry, p);	/* ACTION AFTER unionName */
	return 1;
L52:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L53;
	actionLogPush(16, entry, p);	/* ACTION AFTER structName */
	return 1;
L53:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structField */
static int p_structField(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_fieldConstKw()) goto L55;
	sp--; goto L56;
L55:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L56:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_volatileKw()) goto L57;
	sp--; goto L58;
L57:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L58:	;
	if (!p_type()) goto L54;
	if (!p_structDeclarator()) goto L54;
L59:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L60;
	p += 1;
	if (!p_structDeclarator()) goto L60;
	sp--; goto L59;
L60:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L54;
	p += 1;
	actionLogPush(17, entry, p);	/* ACTION AFTER structField */
	return 1;
L54:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fieldConstKw */
static int p_fieldConstKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "const", 5) != 0) goto L61;
	if (idch((unsigned char)p[5])) goto L61;
	p += 5;
	actionLogPush(18, entry, p);	/* ACTION AFTER fieldConstKw */
	return 1;
L61:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structDeclarator */
static int p_structDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L62;
	if (!p_fieldName()) goto L62;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitFieldWidth()) goto L63;
	sp--; goto L64;
L63:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L64:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L65;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L67;
	sp--; goto L68;
L67:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L68:	;
	sp--; goto L66;
L65:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L66:	;
	actionLogPush(19, entry, p);	/* ACTION AFTER structDeclarator */
	return 1;
L62:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitFieldWidth */
static int p_bitFieldWidth(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L69;
	p += 1;
	if (!p_globalNumber()) goto L69;
	actionLogPush(20, entry, p);	/* ACTION AFTER bitFieldWidth */
	return 1;
L69:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L70;
	return 1;
L70:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "typedef", 7) != 0) goto L73;
	if (idch((unsigned char)p[7])) goto L73;
	p += 7;
	if (!p_typedefType()) goto L73;
	if (!p_typedefDeclarator()) goto L73;
L74:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L75;
	p += 1;
	if (!p_typedefDeclarator()) goto L75;
	sp--; goto L74;
L75:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L73;
	p += 1;
	goto L72;
L73:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_fnPtrTypedef()) goto L76;
	goto L72;
L76:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L71;
L72:	sp--;
	return 1;
L71:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* typedefDeclarator */
static int p_typedefDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L77;
	if (!p_typedefTargetName()) goto L77;
	return 1;
L77:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "typedef", 7) != 0) goto L78;
	if (idch((unsigned char)p[7])) goto L78;
	p += 7;
	if (!p_type()) goto L78;
	if (!p_pointerDecl()) goto L78;
	if (!p_fnPtrOpen()) goto L78;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L78;
	if (strncmp(p, "*=", 2) == 0) goto L78;	/* Longest-Match */
	p += 1;
	if (!p_fnPtrName()) goto L78;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L78;
	p += 1;
	if (!p_fnPtrParams()) goto L78;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L78;
	p += 1;
	actionLogPush(24, entry, p);	/* ACTION AFTER fnPtrTypedef */
	return 1;
L78:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrParams */
static int p_fnPtrParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_voidParams()) goto L81;
	goto L80;
L81:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_externRealParams()) goto L82;
	goto L80;
L82:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L79;
L80:	sp--;
	return 1;
L79:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "(", 1) != 0) goto L83;
	p += 1;
	actionLogPush(26, entry, p);	/* ACTION AFTER fnPtrOpen */
	return 1;
L83:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L84;
	return 1;
L84:	p = entry; actionLogLen = entryLog;
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
	if (!p_anonStructType()) goto L87;
	goto L86;
L87:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_anonEnumType()) goto L88;
	goto L86;
L88:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_type()) goto L89;
	goto L86;
L89:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L85;
L86:	sp--;
	return 1;
L85:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* anonEnumType */
static int p_anonEnumType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L90;
	if (idch((unsigned char)p[4])) goto L90;
	p += 4;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_enumTagName()) goto L91;
	sp--; goto L92;
L91:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L92:	;
	if (!p_anonEnumOpen()) goto L90;
	if (!p_enumItem()) goto L90;
L93:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L94;
	p += 1;
	if (!p_enumItem()) goto L94;
	sp--; goto L93;
L94:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L90;
	p += 1;
	actionLogPush(29, entry, p);	/* ACTION AFTER anonEnumType */
	return 1;
L90:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* enumTagName */
static int p_enumTagName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L95;
	return 1;
L95:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* anonEnumOpen */
static int p_anonEnumOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L96;
	p += 1;
	return 1;
L96:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "struct", 6) != 0) goto L97;
	if (idch((unsigned char)p[6])) goto L97;
	p += 6;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structTagName()) goto L98;
	sp--; goto L99;
L98:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L99:	;
	if (!p_anonStructOpen()) goto L97;
	if (!p_structField()) goto L97;
L100:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L101;
	sp--; goto L100;
L101:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L97;
	p += 1;
	return 1;
L97:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* structTagName */
static int p_structTagName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L102;
	return 1;
L102:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "{", 1) != 0) goto L103;
	p += 1;
	actionLogPush(34, entry, p);	/* ACTION AFTER anonStructOpen */
	return 1;
L103:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L104;
	actionLogPush(35, entry, p);	/* ACTION AFTER typedefTargetName */
	return 1;
L104:	p = entry; actionLogLen = entryLog;
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
	if (!p_externGlobalDecl()) goto L107;
	goto L106;
L107:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_plainGlobalDecl()) goto L108;
	goto L106;
L108:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L105;
L106:	sp--;
	return 1;
L105:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "extern", 6) != 0) goto L109;
	if (idch((unsigned char)p[6])) goto L109;
	p += 6;
	if (!p_type()) goto L109;
	if (!p_pointerDecl()) goto L109;
	if (!p_globalName()) goto L109;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L109;
	p += 1;
	actionLogPush(37, entry, p);	/* ACTION AFTER externGlobalDecl */
	return 1;
L109:	p = entry; actionLogLen = entryLog;
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
	if (!p_staticKw()) goto L111;
	sp--; goto L112;
L111:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L112:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L113;
	sp--; goto L114;
L113:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L114:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_volatileKw()) goto L115;
	sp--; goto L116;
L115:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L116:	;
	if (!p_type()) goto L110;
	if (!p_globalDeclarator()) goto L110;
L117:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L118;
	p += 1;
	if (!p_globalDeclarator()) goto L118;
	sp--; goto L117;
L118:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L110;
	p += 1;
	actionLogPush(38, entry, p);	/* ACTION AFTER plainGlobalDecl */
	return 1;
L110:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalDeclarator */
static int p_globalDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L119;
	if (!p_globalName()) goto L119;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L120;
L122:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L123;
	sp--; goto L122;
L123:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L121;
L120:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L121:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L124;
	if (strncmp(p, "==", 2) == 0) goto L124;	/* Longest-Match */
	p += 1;
	if (!p_globalInit()) goto L124;
	sp--; goto L125;
L124:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L125:	;
	return 1;
L119:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "const", 5) != 0) goto L126;
	if (idch((unsigned char)p[5])) goto L126;
	p += 5;
	actionLogPush(40, entry, p);	/* ACTION AFTER constKw */
	return 1;
L126:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* volatileKw */
static int p_volatileKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "volatile", 8) != 0) goto L127;
	if (idch((unsigned char)p[8])) goto L127;
	p += 8;
	return 1;
L127:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "static", 6) != 0) goto L128;
	if (idch((unsigned char)p[6])) goto L128;
	p += 6;
	actionLogPush(42, entry, p);	/* ACTION AFTER staticKw */
	return 1;
L128:	p = entry; actionLogLen = entryLog;
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
	if (!p_globalValue()) goto L131;
	goto L130;
L131:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L132;
	goto L130;
L132:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalStringInit()) goto L133;
	goto L130;
L133:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L129;
L130:	sp--;
	return 1;
L129:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "\"", 1) != 0) goto L134;
	p += 1;
L135:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_character()) goto L136;
	sp--; goto L135;
L136:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L134;
	p += 1;
	return 1;
L134:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L137;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constSize()) goto L138;
	sp--; goto L139;
L138:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L139:	;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L137;
	p += 1;
	return 1;
L137:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L140;
	p += 1;
	if (!p_constSize()) goto L140;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L140;
	p += 1;
	return 1;
L140:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* constSize */
static int p_constSize(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_globalNumber()) goto L141;
L142:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constSizeOp()) goto L143;
	if (!p_globalNumber()) goto L143;
	sp--; goto L142;
L143:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L141:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* constSizeOp */
static int p_constSizeOp(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "+", 1) != 0) goto L146;
	if (strncmp(p, "+=", 2) == 0) goto L146;	/* Longest-Match */
	if (strncmp(p, "++", 2) == 0) goto L146;	/* Longest-Match */
	p += 1;
	goto L145;
L146:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-", 1) != 0) goto L147;
	if (strncmp(p, "-=", 2) == 0) goto L147;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L147;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L147;	/* Longest-Match */
	p += 1;
	goto L145;
L147:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "*", 1) != 0) goto L148;
	if (strncmp(p, "*=", 2) == 0) goto L148;	/* Longest-Match */
	p += 1;
	goto L145;
L148:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L144;
L145:	sp--;
	return 1;
L144:	p = entry; actionLogLen = entryLog;
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
	if (!p_globalFloat()) goto L151;
	goto L150;
L151:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalNumber()) goto L152;
	goto L150;
L152:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalNeg()) goto L153;
	goto L150;
L153:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalBool()) goto L154;
	goto L150;
L154:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L149;
L150:	sp--;
	return 1;
L149:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalFloat */
static int p_globalFloat(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L155;
L156:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L157;
	sp--; goto L156;
L157:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (!p_floatTail()) goto L155;
	return 1;
L155:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "true", 4) != 0) goto L160;
	if (idch((unsigned char)p[4])) goto L160;
	p += 4;
	goto L159;
L160:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L161;
	if (idch((unsigned char)p[5])) goto L161;
	p += 5;
	goto L159;
L161:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L158;
L159:	sp--;
	return 1;
L158:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L162;
	if (strncmp(p, "-=", 2) == 0) goto L162;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L162;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L162;	/* Longest-Match */
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalFloat()) goto L164;
	goto L163;
L164:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalNumber()) goto L165;
	goto L163;
L165:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L162;
L163:	sp--;
	return 1;
L162:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* globalNumber */
static int p_globalNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_hexNumber()) goto L168;
	goto L167;
L168:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_decNumber()) goto L169;
	goto L167;
L169:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L166;
L167:	sp--;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_integerSuffix()) goto L170;
	sp--; goto L171;
L170:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L171:	;
	return 1;
L166:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcdef */
static int p_funcdef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcHead()) goto L172;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_funcBody()) goto L174;
	goto L173;
L174:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_protoEnd()) goto L175;
	goto L173;
L175:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L172;
L173:	sp--;
	actionLogPush(54, entry, p);	/* ACTION AFTER funcdef */
	return 1;
L172:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* funcBody */
static int p_funcBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcBodyOpen()) goto L176;
L177:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L178;
	sp--; goto L177;
L178:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L176;
	p += 1;
	return 1;
L176:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "{", 1) != 0) goto L179;
	p += 1;
	actionLogPush(56, entry, p);	/* ACTION AFTER funcBodyOpen */
	return 1;
L179:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L180;
	p += 1;
	actionLogPush(57, entry, p);	/* ACTION AFTER protoEnd */
	return 1;
L180:	p = entry; actionLogLen = entryLog;
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
	if (!p_staticKw()) goto L182;
	sp--; goto L183;
L182:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L183:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_retConstKw()) goto L184;
	sp--; goto L185;
L184:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L185:	;
	if (!p_type()) goto L181;
	if (!p_pointerDecl()) goto L181;
	if (!p_defName()) goto L181;
	if (!p_funcParams()) goto L181;
	actionLogPush(58, entry, p);	/* ACTION AFTER funcHead */
	return 1;
L181:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "const", 5) != 0) goto L186;
	if (idch((unsigned char)p[5])) goto L186;
	p += 5;
	return 1;
L186:	p = entry; actionLogLen = entryLog;
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
	if (!p_voidParams()) goto L189;
	goto L188;
L189:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_normalParams()) goto L190;
	goto L188;
L190:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L187;
L188:	sp--;
	return 1;
L187:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "(", 1) != 0) goto L191;
	p += 1;
	ws();
	if (strncmp(p, "void", 4) != 0) goto L191;
	if (idch((unsigned char)p[4])) goto L191;
	p += 4;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L191;
	p += 1;
	return 1;
L191:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "(", 1) != 0) goto L192;
	p += 1;
	if (!p_paramList()) goto L192;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L192;
	p += 1;
	return 1;
L192:	p = entry; actionLogLen = entryLog;
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
	if (!p_param()) goto L194;
L196:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L197;
	p += 1;
	if (!p_param()) goto L197;
	sp--; goto L196;
L197:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L195;
L194:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L195:	;
	return 1;
L193:	p = entry; actionLogLen = entryLog;
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
	if (!p_fnPtrParam()) goto L200;
	goto L199;
L200:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_registerKw()) goto L202;
	sp--; goto L203;
L202:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L203:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L204;
	sp--; goto L205;
L204:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L205:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_volatileKw()) goto L206;
	sp--; goto L207;
L206:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L207:	;
	if (!p_type()) goto L201;
	if (!p_pointerDecl()) goto L201;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_paramDecl()) goto L208;
	sp--; goto L209;
L208:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L209:	;
	goto L199;
L201:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L198;
L199:	sp--;
	actionLogPush(64, entry, p);	/* ACTION AFTER param */
	return 1;
L198:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrParam */
static int p_fnPtrParam(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_type()) goto L210;
	if (!p_pointerDecl()) goto L210;
	if (!p_fnPtrOpen()) goto L210;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L210;
	if (strncmp(p, "*=", 2) == 0) goto L210;	/* Longest-Match */
	p += 1;
	if (!p_fnPtrParamName()) goto L210;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L210;
	p += 1;
	if (!p_fnPtrParams()) goto L210;
	actionLogPush(65, entry, p);	/* ACTION AFTER fnPtrParam */
	return 1;
L210:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrParamName */
static int p_fnPtrParamName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L211;
	return 1;
L211:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* paramDecl */
static int p_paramDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_paramName()) goto L212;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_paramArray()) goto L213;
	sp--; goto L214;
L213:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L214:	;
	actionLogPush(67, entry, p);	/* ACTION AFTER paramDecl */
	return 1;
L212:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L215;
	p += 1;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L215;
	p += 1;
	return 1;
L215:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* block */
static int p_block(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_blockOpen()) goto L216;
L217:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L218;
	sp--; goto L217;
L218:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L216;
	p += 1;
	actionLogPush(69, entry, p);	/* ACTION AFTER block */
	return 1;
L216:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* blockOpen */
static int p_blockOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L219;
	p += 1;
	actionLogPush(70, entry, p);	/* ACTION AFTER blockOpen */
	return 1;
L219:	p = entry; actionLogLen = entryLog;
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
	if (!p_labelStmt()) goto L222;
	goto L221;
L222:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_unlabeledStmt()) goto L223;
	goto L221;
L223:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L220;
L221:	sp--;
	return 1;
L220:	p = entry; actionLogLen = entryLog;
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
	if (!p_ifStmt()) goto L226;
	goto L225;
L226:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_whileStmt()) goto L227;
	goto L225;
L227:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_forStmt()) goto L228;
	goto L225;
L228:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_doStmt()) goto L229;
	goto L225;
L229:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_switchStmt()) goto L230;
	goto L225;
L230:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_breakStmt()) goto L231;
	goto L225;
L231:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_continueStmt()) goto L232;
	goto L225;
L232:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_gotoStmt()) goto L233;
	goto L225;
L233:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_returnStmt()) goto L234;
	goto L225;
L234:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_block()) goto L235;
	goto L225;
L235:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_emptyStmt()) goto L236;
	goto L225;
L236:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_voidCastStmt()) goto L237;
	goto L225;
L237:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_callStmt()) goto L238;
	goto L225;
L238:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incDecStmt()) goto L239;
	goto L225;
L239:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_assignStmt()) goto L240;
	goto L225;
L240:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_fnPtrVarDecl()) goto L241;
	goto L225;
L241:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticVarDecl()) goto L242;
	goto L225;
L242:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varDecl()) goto L243;
	goto L225;
L243:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L224;
L225:	sp--;
	return 1;
L224:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrVarDecl */
static int p_fnPtrVarDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_type()) goto L244;
	if (!p_pointerDecl()) goto L244;
	if (!p_fnPtrOpen()) goto L244;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L244;
	if (strncmp(p, "*=", 2) == 0) goto L244;	/* Longest-Match */
	p += 1;
	if (!p_fnPtrVarName()) goto L244;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L244;
	p += 1;
	if (!p_fnPtrParams()) goto L244;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L244;
	p += 1;
	actionLogPush(73, entry, p);	/* ACTION AFTER fnPtrVarDecl */
	return 1;
L244:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* fnPtrVarName */
static int p_fnPtrVarName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L245;
	return 1;
L245:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticVarDecl */
static int p_staticVarDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_staticKw()) goto L246;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L247;
	sp--; goto L248;
L247:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L248:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_volatileKw()) goto L249;
	sp--; goto L250;
L249:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L250:	;
	if (!p_type()) goto L246;
	if (!p_pointerDecl()) goto L246;
	if (!p_staticLocalName()) goto L246;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_staticArrayDim()) goto L251;
	sp--; goto L252;
L251:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L252:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L253;
	if (strncmp(p, "==", 2) == 0) goto L253;	/* Longest-Match */
	p += 1;
	if (!p_staticInit()) goto L253;
	sp--; goto L254;
L253:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L254:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L246;
	p += 1;
	actionLogPush(75, entry, p);	/* ACTION AFTER staticVarDecl */
	return 1;
L246:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L255;
	actionLogPush(76, entry, p);	/* ACTION AFTER staticLocalName */
	return 1;
L255:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticArrayDim */
static int p_staticArrayDim(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L256;
	p += 1;
	if (!p_initNumber()) goto L256;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L256;
	p += 1;
	return 1;
L256:	p = entry; actionLogLen = entryLog;
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
	if (!p_globalValue()) goto L259;
	goto L258;
L259:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticRuntimeInit()) goto L260;
	goto L258;
L260:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L257;
L258:	sp--;
	return 1;
L257:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* staticRuntimeInit */
static int p_staticRuntimeInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L261;
	actionLogPush(79, entry, p);	/* ACTION AFTER staticRuntimeInit */
	return 1;
L261:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchStmt */
static int p_switchStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_switchKw()) goto L262;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L262;
	p += 1;
	if (!p_switchCond()) goto L262;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L262;
	p += 1;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L262;
	p += 1;
L263:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseGroup()) goto L264;
	sp--; goto L263;
L264:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_defaultGroup()) goto L265;
	sp--; goto L266;
L265:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L266:	;
	if (!p_switchClose()) goto L262;
	actionLogPush(80, entry, p);	/* ACTION AFTER switchStmt */
	return 1;
L262:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "switch", 6) != 0) goto L267;
	if (idch((unsigned char)p[6])) goto L267;
	p += 6;
	actionLogPush(81, entry, p);	/* ACTION AFTER switchKw */
	return 1;
L267:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* switchCond */
static int p_switchCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L268;
	actionLogPush(82, entry, p);	/* ACTION AFTER switchCond */
	return 1;
L268:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "}", 1) != 0) goto L269;
	p += 1;
	return 1;
L269:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseGroup */
static int p_caseGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabelRun()) goto L270;
	if (!p_caseBody()) goto L270;
	actionLogPush(84, entry, p);	/* ACTION AFTER caseGroup */
	return 1;
L270:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseLabelRun */
static int p_caseLabelRun(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabel()) goto L271;
L272:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseLabel()) goto L273;
	sp--; goto L272;
L273:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(85, entry, p);	/* ACTION AFTER caseLabelRun */
	return 1;
L271:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "case", 4) != 0) goto L274;
	if (idch((unsigned char)p[4])) goto L274;
	p += 4;
	if (!p_caseValue()) goto L274;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L274;
	p += 1;
	actionLogPush(86, entry, p);	/* ACTION AFTER caseLabel */
	return 1;
L274:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L277;
	goto L276;
L277:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNeg()) goto L278;
	goto L276;
L278:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNumber()) goto L279;
	goto L276;
L279:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L275;
L276:	sp--;
	return 1;
L275:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L280;
	if (strncmp(p, "-=", 2) == 0) goto L280;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L280;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L280;	/* Longest-Match */
	p += 1;
	if (!p_caseNumber()) goto L280;
	return 1;
L280:	p = entry; actionLogLen = entryLog;
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
	if (!p_digit()) goto L281;
L282:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L283;
	sp--; goto L282;
L283:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L281:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* caseBody */
static int p_caseBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L285:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_unlabeledStmt()) goto L286;
	sp--; goto L285;
L286:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L284:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* defaultGroup */
static int p_defaultGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_defaultLabel()) goto L287;
	if (!p_caseBody()) goto L287;
	return 1;
L287:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "default", 7) != 0) goto L288;
	if (idch((unsigned char)p[7])) goto L288;
	p += 7;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L288;
	p += 1;
	actionLogPush(92, entry, p);	/* ACTION AFTER defaultLabel */
	return 1;
L288:	p = entry; actionLogLen = entryLog;
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
	if (!p_registerKw()) goto L290;
	sp--; goto L291;
L290:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L291:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L292;
	sp--; goto L293;
L292:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L293:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_volatileKw()) goto L294;
	sp--; goto L295;
L294:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L295:	;
	if (!p_type()) goto L289;
	if (!p_varDeclarator()) goto L289;
L296:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L297;
	p += 1;
	if (!p_varDeclarator()) goto L297;
	sp--; goto L296;
L297:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L289;
	p += 1;
	return 1;
L289:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* registerKw */
static int p_registerKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "register", 8) != 0) goto L298;
	if (idch((unsigned char)p[8])) goto L298;
	p += 8;
	return 1;
L298:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* varDeclarator */
static int p_varDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L299;
	if (!p_localDecl()) goto L299;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L300;
	if (strncmp(p, "==", 2) == 0) goto L300;	/* Longest-Match */
	p += 1;
	if (!p_varInit()) goto L300;
	sp--; goto L301;
L300:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L301:	;
	return 1;
L299:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* localDecl */
static int p_localDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_localName()) goto L302;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L303;
L305:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L306;
	sp--; goto L305;
L306:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L304;
L303:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L304:	;
	actionLogPush(96, entry, p);	/* ACTION AFTER localDecl */
	return 1;
L302:	p = entry; actionLogLen = entryLog;
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
	if (!p_arrayStringInit()) goto L309;
	goto L308;
L309:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_expr()) goto L310;
	goto L308;
L310:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L311;
	goto L308;
L311:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L307;
L308:	sp--;
	actionLogPush(97, entry, p);	/* ACTION AFTER varInit */
	return 1;
L307:	p = entry; actionLogLen = entryLog;
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
	if (!p_stringLit()) goto L312;
	actionLogPush(98, entry, p);	/* ACTION AFTER arrayStringInit */
	return 1;
L312:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "{", 1) != 0) goto L313;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_initValue()) goto L314;
L316:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L317;
	p += 1;
	if (!p_initValue()) goto L317;
	sp--; goto L316;
L317:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L315;
L314:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L315:	;
	ws();
	if (strncmp(p, "}", 1) != 0) goto L313;
	p += 1;
	return 1;
L313:	p = entry; actionLogLen = entryLog;
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
	if (!p_initFloat()) goto L320;
	goto L319;
L320:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initNumber()) goto L321;
	goto L319;
L321:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initNeg()) goto L322;
	goto L319;
L322:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initBool()) goto L323;
	goto L319;
L323:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initString()) goto L324;
	goto L319;
L324:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L325;
	goto L319;
L325:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L318;
L319:	sp--;
	return 1;
L318:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initString */
static int p_initString(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L326;
	p += 1;
L327:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_character()) goto L328;
	sp--; goto L327;
L328:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L326;
	p += 1;
	return 1;
L326:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "true", 4) != 0) goto L331;
	if (idch((unsigned char)p[4])) goto L331;
	p += 4;
	goto L330;
L331:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L332;
	if (idch((unsigned char)p[5])) goto L332;
	p += 5;
	goto L330;
L332:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L329;
L330:	sp--;
	return 1;
L329:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L333;
	if (strncmp(p, "-=", 2) == 0) goto L333;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L333;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L333;	/* Longest-Match */
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_initFloat()) goto L335;
	goto L334;
L335:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initNumber()) goto L336;
	goto L334;
L336:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L333;
L334:	sp--;
	return 1;
L333:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initFloat */
static int p_initFloat(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L337;
L338:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L339;
	sp--; goto L338;
L339:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (!p_floatTail()) goto L337;
	return 1;
L337:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* initNumber */
static int p_initNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_hexNumber()) goto L342;
	goto L341;
L342:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_decNumber()) goto L343;
	goto L341;
L343:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L340;
L341:	sp--;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_integerSuffix()) goto L344;
	sp--; goto L345;
L344:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L345:	;
	return 1;
L340:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* assignStmt */
static int p_assignStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_chainAssign()) goto L348;
	goto L347;
L348:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_target()) goto L349;
	if (!p_assignop()) goto L349;
	if (!p_expr()) goto L349;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L349;
	p += 1;
	goto L347;
L349:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L346;
L347:	sp--;
	actionLogPush(106, entry, p);	/* ACTION AFTER assignStmt */
	return 1;
L346:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* chainAssign */
static int p_chainAssign(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L350;
	if (!p_assignop()) goto L350;
	if (!p_target()) goto L350;
	if (!p_assignop()) goto L350;
	if (!p_expr()) goto L350;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L350;
	p += 1;
	actionLogPush(107, entry, p);	/* ACTION AFTER chainAssign */
	return 1;
L350:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "=", 1) != 0) goto L353;
	if (strncmp(p, "==", 2) == 0) goto L353;	/* Longest-Match */
	p += 1;
	goto L352;
L353:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "+=", 2) != 0) goto L354;
	p += 2;
	goto L352;
L354:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-=", 2) != 0) goto L355;
	p += 2;
	goto L352;
L355:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "*=", 2) != 0) goto L356;
	p += 2;
	goto L352;
L356:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/=", 2) != 0) goto L357;
	p += 2;
	goto L352;
L357:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%=", 2) != 0) goto L358;
	p += 2;
	goto L352;
L358:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<<=", 3) != 0) goto L359;
	p += 3;
	goto L352;
L359:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>=", 3) != 0) goto L360;
	p += 3;
	goto L352;
L360:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "&=", 2) != 0) goto L361;
	p += 2;
	goto L352;
L361:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "^=", 2) != 0) goto L362;
	p += 2;
	goto L352;
L362:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "|=", 2) != 0) goto L363;
	p += 2;
	goto L352;
L363:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L351;
L352:	sp--;
	actionLogPush(108, entry, p);	/* ACTION AFTER assignop */
	return 1;
L351:	p = entry; actionLogLen = entryLog;
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
	if (!p_call()) goto L366;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L366;
	p += 1;
	goto L365;
L366:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectCall()) goto L367;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L367;
	p += 1;
	goto L365;
L367:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L364;
L365:	sp--;
	actionLogPush(109, entry, p);	/* ACTION AFTER callStmt */
	return 1;
L364:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L368;
	p += 1;
	return 1;
L368:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* voidCastStmt */
static int p_voidCastStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_voidCastOpen()) goto L369;
	if (!p_expr()) goto L369;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L369;
	p += 1;
	actionLogPush(111, entry, p);	/* ACTION AFTER voidCastStmt */
	return 1;
L369:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "(", 1) != 0) goto L370;
	p += 1;
	ws();
	if (strncmp(p, "void", 4) != 0) goto L370;
	if (idch((unsigned char)p[4])) goto L370;
	p += 4;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L370;
	p += 1;
	return 1;
L370:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "return", 6) != 0) goto L371;
	if (idch((unsigned char)p[6])) goto L371;
	p += 6;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_retVal()) goto L372;
	sp--; goto L373;
L372:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L373:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L371;
	p += 1;
	actionLogPush(113, entry, p);	/* ACTION AFTER returnStmt */
	return 1;
L371:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* retVal */
static int p_retVal(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L374;
	actionLogPush(114, entry, p);	/* ACTION AFTER retVal */
	return 1;
L374:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ifStmt */
static int p_ifStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_ifKw()) goto L375;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L375;
	p += 1;
	if (!p_ifCond()) goto L375;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L375;
	p += 1;
	if (!p_thenPart()) goto L375;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_elseKw()) goto L376;
	if (!p_elsePart()) goto L376;
	sp--; goto L377;
L376:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L377:	;
	actionLogPush(115, entry, p);	/* ACTION AFTER ifStmt */
	return 1;
L375:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "if", 2) != 0) goto L378;
	if (idch((unsigned char)p[2])) goto L378;
	p += 2;
	actionLogPush(116, entry, p);	/* ACTION AFTER ifKw */
	return 1;
L378:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "else", 4) != 0) goto L379;
	if (idch((unsigned char)p[4])) goto L379;
	p += 4;
	return 1;
L379:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ifCond */
static int p_ifCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L380;
	actionLogPush(118, entry, p);	/* ACTION AFTER ifCond */
	return 1;
L380:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* thenPart */
static int p_thenPart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L381;
	actionLogPush(119, entry, p);	/* ACTION AFTER thenPart */
	return 1;
L381:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* elsePart */
static int p_elsePart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L382;
	return 1;
L382:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileStmt */
static int p_whileStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_whileKw()) goto L383;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L383;
	p += 1;
	if (!p_whileCond()) goto L383;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L383;
	p += 1;
	if (!p_whileBody()) goto L383;
	actionLogPush(121, entry, p);	/* ACTION AFTER whileStmt */
	return 1;
L383:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "while", 5) != 0) goto L384;
	if (idch((unsigned char)p[5])) goto L384;
	p += 5;
	actionLogPush(122, entry, p);	/* ACTION AFTER whileKw */
	return 1;
L384:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileCond */
static int p_whileCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L385;
	actionLogPush(123, entry, p);	/* ACTION AFTER whileCond */
	return 1;
L385:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* whileBody */
static int p_whileBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L386;
	return 1;
L386:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forStmt */
static int p_forStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_forKw()) goto L387;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L387;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forInit()) goto L388;
	sp--; goto L389;
L388:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L389:	;
	if (!p_forSep1()) goto L387;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forCond()) goto L390;
	sp--; goto L391;
L390:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L391:	;
	if (!p_forSep2()) goto L387;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forStep()) goto L392;
	sp--; goto L393;
L392:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L393:	;
	if (!p_forClose()) goto L387;
	if (!p_forBody()) goto L387;
	actionLogPush(125, entry, p);	/* ACTION AFTER forStmt */
	return 1;
L387:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "for", 3) != 0) goto L394;
	if (idch((unsigned char)p[3])) goto L394;
	p += 3;
	actionLogPush(126, entry, p);	/* ACTION AFTER forKw */
	return 1;
L394:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L395;
	p += 1;
	actionLogPush(127, entry, p);	/* ACTION AFTER forSep1 */
	return 1;
L395:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L396;
	p += 1;
	actionLogPush(128, entry, p);	/* ACTION AFTER forSep2 */
	return 1;
L396:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ")", 1) != 0) goto L397;
	p += 1;
	actionLogPush(129, entry, p);	/* ACTION AFTER forClose */
	return 1;
L397:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forInit */
static int p_forInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_forInitItem()) goto L398;
L399:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forComma()) goto L400;
	if (!p_forInitItem()) goto L400;
	sp--; goto L399;
L400:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L398:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forInitItem */
static int p_forInitItem(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L401;
	if (!p_assignop()) goto L401;
	if (!p_expr()) goto L401;
	actionLogPush(131, entry, p);	/* ACTION AFTER forInitItem */
	return 1;
L401:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forComma */
static int p_forComma(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L402;
	p += 1;
	return 1;
L402:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forCond */
static int p_forCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L403;
	actionLogPush(133, entry, p);	/* ACTION AFTER forCond */
	return 1;
L403:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forStep */
static int p_forStep(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_forStepItem()) goto L404;
L405:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forComma()) goto L406;
	if (!p_forStepItem()) goto L406;
	sp--; goto L405;
L406:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L404:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forStepItem */
static int p_forStepItem(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_target()) goto L409;
	if (!p_assignop()) goto L409;
	if (!p_expr()) goto L409;
	goto L408;
L409:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L410;
	goto L408;
L410:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L411;
	goto L408;
L411:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L407;
L408:	sp--;
	actionLogPush(135, entry, p);	/* ACTION AFTER forStepItem */
	return 1;
L407:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* forBody */
static int p_forBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L412;
	return 1;
L412:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doStmt */
static int p_doStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_doKw()) goto L413;
	if (!p_doBody()) goto L413;
	if (!p_doWhileTok()) goto L413;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L413;
	p += 1;
	if (!p_doCond()) goto L413;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L413;
	p += 1;
	if (!p_doClose()) goto L413;
	actionLogPush(137, entry, p);	/* ACTION AFTER doStmt */
	return 1;
L413:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "do", 2) != 0) goto L414;
	if (idch((unsigned char)p[2])) goto L414;
	p += 2;
	actionLogPush(138, entry, p);	/* ACTION AFTER doKw */
	return 1;
L414:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "while", 5) != 0) goto L415;
	if (idch((unsigned char)p[5])) goto L415;
	p += 5;
	actionLogPush(139, entry, p);	/* ACTION AFTER doWhileTok */
	return 1;
L415:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doCond */
static int p_doCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L416;
	actionLogPush(140, entry, p);	/* ACTION AFTER doCond */
	return 1;
L416:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ";", 1) != 0) goto L417;
	p += 1;
	return 1;
L417:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* doBody */
static int p_doBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L418;
	return 1;
L418:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "break", 5) != 0) goto L419;
	if (idch((unsigned char)p[5])) goto L419;
	p += 5;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L419;
	p += 1;
	actionLogPush(143, entry, p);	/* ACTION AFTER breakStmt */
	return 1;
L419:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "continue", 8) != 0) goto L420;
	if (idch((unsigned char)p[8])) goto L420;
	p += 8;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L420;
	p += 1;
	actionLogPush(144, entry, p);	/* ACTION AFTER continueStmt */
	return 1;
L420:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "goto", 4) != 0) goto L421;
	if (idch((unsigned char)p[4])) goto L421;
	p += 4;
	ws();
	if (!p_ident()) goto L421;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L421;
	p += 1;
	actionLogPush(145, entry, p);	/* ACTION AFTER gotoStmt */
	return 1;
L421:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L422;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L422;
	p += 1;
	actionLogPush(146, entry, p);	/* ACTION AFTER labelStmt */
	return 1;
L422:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* expr */
static int p_expr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L423;
	return 1;
L423:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalExpr */
static int p_conditionalExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_orExpr()) goto L424;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_qmark()) goto L425;
	if (!p_conditionalTrue()) goto L425;
	if (!p_colon()) goto L425;
	if (!p_conditionalFalse()) goto L425;
	sp--; goto L426;
L425:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L426:	;
	actionLogPush(148, entry, p);	/* ACTION AFTER conditionalExpr */
	return 1;
L424:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* parenOpen */
static int p_parenOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L427;
	p += 1;
	actionLogPush(149, entry, p);	/* ACTION AFTER parenOpen */
	return 1;
L427:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* parenClose */
static int p_parenClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L428;
	p += 1;
	actionLogPush(150, entry, p);	/* ACTION AFTER parenClose */
	return 1;
L428:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* commaExpr */
static int p_commaExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_commaItem()) goto L429;
L430:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_commaTok()) goto L431;
	if (!p_commaItem()) goto L431;
	sp--; goto L430;
L431:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(151, entry, p);	/* ACTION AFTER commaExpr */
	return 1;
L429:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* commaTok */
static int p_commaTok(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L432;
	p += 1;
	actionLogPush(152, entry, p);	/* ACTION AFTER commaTok */
	return 1;
L432:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* commaItem */
static int p_commaItem(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_commaAssign()) goto L435;
	goto L434;
L435:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_commaValue()) goto L436;
	goto L434;
L436:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L433;
L434:	sp--;
	return 1;
L433:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* commaAssign */
static int p_commaAssign(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L437;
	if (!p_assignop()) goto L437;
	if (!p_expr()) goto L437;
	actionLogPush(154, entry, p);	/* ACTION AFTER commaAssign */
	return 1;
L437:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* commaValue */
static int p_commaValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L438;
	actionLogPush(155, entry, p);	/* ACTION AFTER commaValue */
	return 1;
L438:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "?", 1) != 0) goto L439;
	p += 1;
	actionLogPush(156, entry, p);	/* ACTION AFTER qmark */
	return 1;
L439:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalTrue */
static int p_conditionalTrue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L440;
	return 1;
L440:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ":", 1) != 0) goto L441;
	p += 1;
	actionLogPush(158, entry, p);	/* ACTION AFTER colon */
	return 1;
L441:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* conditionalFalse */
static int p_conditionalFalse(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L442;
	return 1;
L442:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* orExpr */
static int p_orExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_andExpr()) goto L443;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_orop()) goto L444;
	if (!p_orExpr()) goto L444;
	sp--; goto L445;
L444:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L445:	;
	actionLogPush(160, entry, p);	/* ACTION AFTER orExpr */
	return 1;
L443:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "||", 2) != 0) goto L446;
	p += 2;
	actionLogPush(161, entry, p);	/* ACTION AFTER orop */
	return 1;
L446:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* andExpr */
static int p_andExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitOrExpr()) goto L447;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_andop()) goto L448;
	if (!p_andExpr()) goto L448;
	sp--; goto L449;
L448:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L449:	;
	actionLogPush(162, entry, p);	/* ACTION AFTER andExpr */
	return 1;
L447:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "&&", 2) != 0) goto L450;
	p += 2;
	actionLogPush(163, entry, p);	/* ACTION AFTER andop */
	return 1;
L450:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitOrExpr */
static int p_bitOrExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitXorExpr()) goto L451;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitorop()) goto L452;
	if (!p_bitOrExpr()) goto L452;
	sp--; goto L453;
L452:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L453:	;
	actionLogPush(164, entry, p);	/* ACTION AFTER bitOrExpr */
	return 1;
L451:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "|", 1) != 0) goto L454;
	if (strncmp(p, "|=", 2) == 0) goto L454;	/* Longest-Match */
	if (strncmp(p, "||", 2) == 0) goto L454;	/* Longest-Match */
	p += 1;
	actionLogPush(165, entry, p);	/* ACTION AFTER bitorop */
	return 1;
L454:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitXorExpr */
static int p_bitXorExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitAndExpr()) goto L455;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitxorop()) goto L456;
	if (!p_bitXorExpr()) goto L456;
	sp--; goto L457;
L456:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L457:	;
	actionLogPush(166, entry, p);	/* ACTION AFTER bitXorExpr */
	return 1;
L455:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "^", 1) != 0) goto L458;
	if (strncmp(p, "^=", 2) == 0) goto L458;	/* Longest-Match */
	p += 1;
	actionLogPush(167, entry, p);	/* ACTION AFTER bitxorop */
	return 1;
L458:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* bitAndExpr */
static int p_bitAndExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_comparison()) goto L459;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitandop()) goto L460;
	if (!p_bitAndExpr()) goto L460;
	sp--; goto L461;
L460:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L461:	;
	actionLogPush(168, entry, p);	/* ACTION AFTER bitAndExpr */
	return 1;
L459:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "&", 1) != 0) goto L462;
	if (strncmp(p, "&=", 2) == 0) goto L462;	/* Longest-Match */
	if (strncmp(p, "&&", 2) == 0) goto L462;	/* Longest-Match */
	p += 1;
	actionLogPush(169, entry, p);	/* ACTION AFTER bitandop */
	return 1;
L462:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* comparison */
static int p_comparison(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_shiftExpr()) goto L463;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_relop()) goto L464;
	if (!p_shiftExpr()) goto L464;
	sp--; goto L465;
L464:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L465:	;
	actionLogPush(170, entry, p);	/* ACTION AFTER comparison */
	return 1;
L463:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* shiftExpr */
static int p_shiftExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L466;
L467:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_shiftop()) goto L468;
	if (!p_shiftRhs()) goto L468;
	sp--; goto L467;
L468:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L466:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* shiftRhs */
static int p_shiftRhs(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L469;
	actionLogPush(172, entry, p);	/* ACTION AFTER shiftRhs */
	return 1;
L469:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "<<", 2) != 0) goto L472;
	if (strncmp(p, "<<=", 3) == 0) goto L472;	/* Longest-Match */
	p += 2;
	goto L471;
L472:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>", 2) != 0) goto L473;
	if (strncmp(p, ">>=", 3) == 0) goto L473;	/* Longest-Match */
	p += 2;
	goto L471;
L473:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L470;
L471:	sp--;
	actionLogPush(173, entry, p);	/* ACTION AFTER shiftop */
	return 1;
L470:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "<=", 2) != 0) goto L476;
	p += 2;
	goto L475;
L476:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">=", 2) != 0) goto L477;
	p += 2;
	goto L475;
L477:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "==", 2) != 0) goto L478;
	p += 2;
	goto L475;
L478:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!=", 2) != 0) goto L479;
	p += 2;
	goto L475;
L479:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<", 1) != 0) goto L480;
	if (strncmp(p, "<<=", 3) == 0) goto L480;	/* Longest-Match */
	if (strncmp(p, "<<", 2) == 0) goto L480;	/* Longest-Match */
	if (strncmp(p, "<=", 2) == 0) goto L480;	/* Longest-Match */
	p += 1;
	goto L475;
L480:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">", 1) != 0) goto L481;
	if (strncmp(p, ">>=", 3) == 0) goto L481;	/* Longest-Match */
	if (strncmp(p, ">>", 2) == 0) goto L481;	/* Longest-Match */
	if (strncmp(p, ">=", 2) == 0) goto L481;	/* Longest-Match */
	p += 1;
	goto L475;
L481:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L474;
L475:	sp--;
	actionLogPush(174, entry, p);	/* ACTION AFTER relop */
	return 1;
L474:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* addExpr */
static int p_addExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_term()) goto L482;
L483:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_addop()) goto L484;
	if (!p_term()) goto L484;
	sp--; goto L483;
L484:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L482:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "+", 1) != 0) goto L487;
	if (strncmp(p, "+=", 2) == 0) goto L487;	/* Longest-Match */
	if (strncmp(p, "++", 2) == 0) goto L487;	/* Longest-Match */
	p += 1;
	goto L486;
L487:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-", 1) != 0) goto L488;
	if (strncmp(p, "-=", 2) == 0) goto L488;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L488;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L488;	/* Longest-Match */
	p += 1;
	goto L486;
L488:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L485;
L486:	sp--;
	actionLogPush(176, entry, p);	/* ACTION AFTER addop */
	return 1;
L485:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* term */
static int p_term(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L489;
L490:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_mulop()) goto L491;
	if (!p_factor()) goto L491;
	sp--; goto L490;
L491:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(177, entry, p);	/* ACTION AFTER term */
	return 1;
L489:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L494;
	if (strncmp(p, "*=", 2) == 0) goto L494;	/* Longest-Match */
	p += 1;
	goto L493;
L494:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/", 1) != 0) goto L495;
	if (strncmp(p, "/=", 2) == 0) goto L495;	/* Longest-Match */
	p += 1;
	goto L493;
L495:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%", 1) != 0) goto L496;
	if (strncmp(p, "%=", 2) == 0) goto L496;	/* Longest-Match */
	p += 1;
	goto L493;
L496:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L492;
L493:	sp--;
	actionLogPush(178, entry, p);	/* ACTION AFTER mulop */
	return 1;
L492:	p = entry; actionLogLen = entryLog;
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
	if (!p_sizeofExpr()) goto L499;
	goto L498;
L499:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_castExpr()) goto L500;
	goto L498;
L500:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L501;
	goto L498;
L501:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L502;
	goto L498;
L502:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_parenOpen()) goto L503;
	if (!p_commaExpr()) goto L503;
	if (!p_parenClose()) goto L503;
	goto L498;
L503:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_call()) goto L504;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L505;
	sp--; goto L506;
L505:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L506:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_callMember()) goto L507;
	sp--; goto L508;
L507:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L508:	;
	goto L498;
L504:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectCall()) goto L509;
	goto L498;
L509:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_boolLit()) goto L510;
	goto L498;
L510:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_addressRef()) goto L511;
	goto L498;
L511:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_derefRef()) goto L512;
	goto L498;
L512:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varRef()) goto L513;
	goto L498;
L513:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_charLit()) goto L514;
	goto L498;
L514:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_floatLit()) goto L515;
	goto L498;
L515:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_number()) goto L516;
	goto L498;
L516:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_stringLit()) goto L517;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L518;
	sp--; goto L519;
L518:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L519:	;
	goto L498;
L517:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_negFactor()) goto L520;
	goto L498;
L520:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L497;
L498:	sp--;
	actionLogPush(179, entry, p);	/* ACTION AFTER factor */
	return 1;
L497:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* postfixIndex */
static int p_postfixIndex(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_index()) goto L521;
	actionLogPush(180, entry, p);	/* ACTION AFTER postfixIndex */
	return 1;
L521:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* stringLit (lexikalisch) */
static int p_stringLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_stringPiece()) goto L522;
L523:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_stringSep()) goto L524;
	if (!p_stringPiece()) goto L524;
	sp--; goto L523;
L524:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(181, entry, p);	/* ACTION AFTER stringLit */
	return 1;
L522:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* stringPiece (lexikalisch) */
static int p_stringPiece(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\"", 1) != 0) goto L525;
	p += 1;
L526:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_character()) goto L527;
	sp--; goto L526;
L527:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	if (strncmp(p, "\"", 1) != 0) goto L525;
	p += 1;
	return 1;
L525:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* stringSep (lexikalisch) */
static int p_stringSep(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L529:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_stringSpace()) goto L530;
	sp--; goto L529;
L530:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L528:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* stringSpace (lexikalisch) */
static int p_stringSpace(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, " ", 1) != 0) goto L533;
	p += 1;
	goto L532;
L533:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "\x09", 1) != 0) goto L534;
	p += 1;
	goto L532;
L534:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "\x0d", 1) != 0) goto L535;
	p += 1;
	goto L532;
L535:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "\x0a", 1) != 0) goto L536;
	p += 1;
	goto L532;
L536:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L531;
L532:	sp--;
	return 1;
L531:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLit (lexikalisch) */
static int p_charLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "'", 1) != 0) goto L537;
	p += 1;
	if (!p_charLitBody()) goto L537;
	if (strncmp(p, "'", 1) != 0) goto L537;
	p += 1;
	actionLogPush(185, entry, p);	/* ACTION AFTER charLit */
	return 1;
L537:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitBody (lexikalisch) */
static int p_charLitBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_charLitEscape()) goto L540;
	goto L539;
L540:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_charLitPlain()) goto L541;
	goto L539;
L541:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L538;
L539:	sp--;
	return 1;
L538:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitEscape (lexikalisch) */
static int p_charLitEscape(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\\", 1) != 0) goto L542;
	p += 1;
	if (!p_charLitEscChar()) goto L542;
	return 1;
L542:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitEscChar (lexikalisch) */
static int p_charLitEscChar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "n", 1) != 0) goto L545;
	p += 1;
	goto L544;
L545:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "t", 1) != 0) goto L546;
	p += 1;
	goto L544;
L546:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "r", 1) != 0) goto L547;
	p += 1;
	goto L544;
L547:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "0", 1) != 0) goto L548;
	p += 1;
	goto L544;
L548:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "\\", 1) != 0) goto L549;
	p += 1;
	goto L544;
L549:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L543;
L544:	sp--;
	return 1;
L543:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* charLitPlain (lexikalisch) */
static int p_charLitPlain(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x26) goto L552;
	p++;
	goto L551;
L552:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x28 || (unsigned char)*p > 0x5B) goto L553;
	p++;
	goto L551;
L553:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x5D || (unsigned char)*p > 0x7E) goto L554;
	p++;
	goto L551;
L554:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L550;
L551:	sp--;
	return 1;
L550:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* character (lexikalisch) */
static int p_character(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_strEscape()) goto L557;
	goto L556;
L557:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x21) goto L558;
	p++;
	goto L556;
L558:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x23 || (unsigned char)*p > 0x7E) goto L559;
	p++;
	goto L556;
L559:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L555;
L556:	sp--;
	return 1;
L555:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* strEscape (lexikalisch) */
static int p_strEscape(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\\", 1) != 0) goto L560;
	p += 1;
	if (!p_strEscChar()) goto L560;
	return 1;
L560:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* strEscChar (lexikalisch) */
static int p_strEscChar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) goto L561;
	p++;
	return 1;
L561:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "-", 1) != 0) goto L564;
	if (strncmp(p, "-=", 2) == 0) goto L564;	/* Longest-Match */
	if (strncmp(p, "--", 2) == 0) goto L564;	/* Longest-Match */
	if (strncmp(p, "->", 2) == 0) goto L564;	/* Longest-Match */
	p += 1;
	goto L563;
L564:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!", 1) != 0) goto L565;
	if (strncmp(p, "!=", 2) == 0) goto L565;	/* Longest-Match */
	p += 1;
	goto L563;
L565:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "~", 1) != 0) goto L566;
	p += 1;
	goto L563;
L566:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L562;
L563:	sp--;
	if (!p_factor()) goto L562;
	actionLogPush(193, entry, p);	/* ACTION AFTER negFactor */
	return 1;
L562:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "&", 1) != 0) goto L567;
	if (strncmp(p, "&=", 2) == 0) goto L567;	/* Longest-Match */
	if (strncmp(p, "&&", 2) == 0) goto L567;	/* Longest-Match */
	p += 1;
	ws();
	if (!p_ident()) goto L567;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L568;
	sp--; goto L569;
L568:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L569:	;
	actionLogPush(194, entry, p);	/* ACTION AFTER addressRef */
	return 1;
L567:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "sizeof", 6) != 0) goto L570;
	if (idch((unsigned char)p[6])) goto L570;
	p += 6;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L572;
	p += 1;
	if (!p_sizeofArg()) goto L572;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L572;
	p += 1;
	goto L571;
L572:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_sizeofVarName()) goto L573;
	goto L571;
L573:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L570;
L571:	sp--;
	return 1;
L570:	p = entry; actionLogLen = entryLog;
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
	if (!p_sizeofType()) goto L576;
	goto L575;
L576:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_sizeofVarName()) goto L577;
	goto L575;
L577:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L574;
L575:	sp--;
	return 1;
L574:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* sizeofType */
static int p_sizeofType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_sizeofBaseType()) goto L578;
	if (!p_pointerDecl()) goto L578;
	actionLogPush(197, entry, p);	/* ACTION AFTER sizeofType */
	return 1;
L578:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* sizeofBaseType */
static int p_sizeofBaseType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "unsigned", 8) != 0) goto L581;
	if (idch((unsigned char)p[8])) goto L581;
	p += 8;
	if (!p_unsignedInt()) goto L581;
	goto L580;
L581:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L582;
	if (idch((unsigned char)p[3])) goto L582;
	p += 3;
	goto L580;
L582:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L583;
	if (idch((unsigned char)p[4])) goto L583;
	p += 4;
	goto L580;
L583:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L584;
	if (idch((unsigned char)p[4])) goto L584;
	p += 4;
	goto L580;
L584:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "short", 5) != 0) goto L585;
	if (idch((unsigned char)p[5])) goto L585;
	p += 5;
	goto L580;
L585:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L586;
	if (idch((unsigned char)p[4])) goto L586;
	p += 4;
	goto L580;
L586:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "double", 6) != 0) goto L587;
	if (idch((unsigned char)p[6])) goto L587;
	p += 6;
	goto L580;
L587:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L588;
	if (idch((unsigned char)p[6])) goto L588;
	p += 6;
	if (!p_structTypeRef()) goto L588;
	goto L580;
L588:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "union", 5) != 0) goto L589;
	if (idch((unsigned char)p[5])) goto L589;
	p += 5;
	if (!p_unionTypeRef()) goto L589;
	goto L580;
L589:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L590;
	if (idch((unsigned char)p[4])) goto L590;
	p += 4;
	if (!p_enumTypeRef()) goto L590;
	goto L580;
L590:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "void", 4) != 0) goto L591;
	if (idch((unsigned char)p[4])) goto L591;
	p += 4;
	goto L580;
L591:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L579;
L580:	sp--;
	return 1;
L579:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L592;
	actionLogPush(199, entry, p);	/* ACTION AFTER sizeofVarName */
	return 1;
L592:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "(", 1) != 0) goto L593;
	p += 1;
	if (!p_castType()) goto L593;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L593;
	p += 1;
	if (!p_castOperand()) goto L593;
	actionLogPush(200, entry, p);	/* ACTION AFTER castExpr */
	return 1;
L593:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* castType */
static int p_castType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_type()) goto L594;
	if (!p_pointerDecl()) goto L594;
	actionLogPush(201, entry, p);	/* ACTION AFTER castType */
	return 1;
L594:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* castOperand */
static int p_castOperand(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L595;
	return 1;
L595:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* preIncDec */
static int p_preIncDec(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_incdecOp()) goto L598;
	if (!p_derefIncTarget()) goto L598;
	goto L597;
L598:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incdecOp()) goto L599;
	if (!p_memberIncTarget()) goto L599;
	goto L597;
L599:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incdecOp()) goto L600;
	if (!p_indexIncTarget()) goto L600;
	goto L597;
L600:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incdecOp()) goto L601;
	ws();
	if (!p_ident()) goto L601;
	goto L597;
L601:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L596;
L597:	sp--;
	actionLogPush(203, entry, p);	/* ACTION AFTER preIncDec */
	return 1;
L596:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* postIncDec */
static int p_postIncDec(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_derefIncTarget()) goto L604;
	if (!p_incdecOp()) goto L604;
	goto L603;
L604:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_memberIncTarget()) goto L605;
	if (!p_incdecOp()) goto L605;
	goto L603;
L605:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indexIncTarget()) goto L606;
	if (!p_incdecOp()) goto L606;
	goto L603;
L606:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_ident()) goto L607;
	if (!p_incdecOp()) goto L607;
	goto L603;
L607:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L602;
L603:	sp--;
	actionLogPush(204, entry, p);	/* ACTION AFTER postIncDec */
	return 1;
L602:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* memberIncTarget */
static int p_memberIncTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L608;
	if (!p_member()) goto L608;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L609;
	sp--; goto L610;
L609:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L610:	;
	return 1;
L608:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* indexIncTarget */
static int p_indexIncTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L611;
	if (!p_index()) goto L611;
	return 1;
L611:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* derefIncTarget */
static int p_derefIncTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L612;
	p += 1;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L612;
	if (strncmp(p, "*=", 2) == 0) goto L612;	/* Longest-Match */
	p += 1;
	ws();
	if (!p_ident()) goto L612;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L612;
	p += 1;
	return 1;
L612:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "++", 2) != 0) goto L615;
	p += 2;
	goto L614;
L615:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "--", 2) != 0) goto L616;
	p += 2;
	goto L614;
L616:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L613;
L614:	sp--;
	return 1;
L613:	p = entry; actionLogLen = entryLog;
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
	if (!p_preIncDec()) goto L619;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L619;
	p += 1;
	goto L618;
L619:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L620;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L620;
	p += 1;
	goto L618;
L620:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L617;
L618:	sp--;
	actionLogPush(209, entry, p);	/* ACTION AFTER incDecStmt */
	return 1;
L617:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L621;
	if (strncmp(p, "*=", 2) == 0) goto L621;	/* Longest-Match */
	p += 1;
	if (!p_factor()) goto L621;
	actionLogPush(210, entry, p);	/* ACTION AFTER derefRef */
	return 1;
L621:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* call */
static int p_call(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcName()) goto L622;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L622;
	p += 1;
	if (!p_argList()) goto L622;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L622;
	p += 1;
	actionLogPush(211, entry, p);	/* ACTION AFTER call */
	return 1;
L622:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* callMember */
static int p_callMember(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_member()) goto L623;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L624;
	sp--; goto L625;
L624:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L625:	;
	actionLogPush(212, entry, p);	/* ACTION AFTER callMember */
	return 1;
L623:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* indirectCall */
static int p_indirectCall(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_varRef()) goto L626;
	if (!p_indCallOpen()) goto L626;
	if (!p_argList()) goto L626;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L626;
	p += 1;
	actionLogPush(213, entry, p);	/* ACTION AFTER indirectCall */
	return 1;
L626:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "(", 1) != 0) goto L627;
	p += 1;
	actionLogPush(214, entry, p);	/* ACTION AFTER indCallOpen */
	return 1;
L627:	p = entry; actionLogLen = entryLog;
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
	if (!p_arg()) goto L629;
L631:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L632;
	p += 1;
	if (!p_arg()) goto L632;
	sp--; goto L631;
L632:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L630;
L629:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L630:	;
	return 1;
L628:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* arg */
static int p_arg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L633;
	actionLogPush(216, entry, p);	/* ACTION AFTER arg */
	return 1;
L633:	p = entry; actionLogLen = entryLog;
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
	if (!p_directTarget()) goto L636;
	goto L635;
L636:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectTarget()) goto L637;
	goto L635;
L637:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L634;
L635:	sp--;
	return 1;
L634:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L638;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L639;
L641:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L642;
	sp--; goto L641;
L642:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L640;
L639:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L640:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L643;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L645;
L647:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L648;
	sp--; goto L647;
L648:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L646;
L645:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L646:	;
L649:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L650;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L651;
L653:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L654;
	sp--; goto L653;
L654:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L652;
L651:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L652:	;
	sp--; goto L649;
L650:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L644;
L643:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L644:	;
	actionLogPush(218, entry, p);	/* ACTION AFTER directTarget */
	return 1;
L638:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L655;
	if (strncmp(p, "*=", 2) == 0) goto L655;	/* Longest-Match */
	p += 1;
	if (!p_factor()) goto L655;
	actionLogPush(219, entry, p);	/* ACTION AFTER indirectTarget */
	return 1;
L655:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L656;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L657;
L659:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L660;
	sp--; goto L659;
L660:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L658;
L657:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L658:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L661;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L663;
L665:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L666;
	sp--; goto L665;
L666:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L664;
L663:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L664:	;
L667:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L668;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L669;
L671:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L672;
	sp--; goto L671;
L672:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L670;
L669:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L670:	;
	sp--; goto L667;
L668:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L662;
L661:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L662:	;
	actionLogPush(220, entry, p);	/* ACTION AFTER varRef */
	return 1;
L656:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* index */
static int p_index(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_indexOpen()) goto L673;
	if (!p_expr()) goto L673;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L673;
	p += 1;
	actionLogPush(221, entry, p);	/* ACTION AFTER index */
	return 1;
L673:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "[", 1) != 0) goto L674;
	p += 1;
	actionLogPush(222, entry, p);	/* ACTION AFTER indexOpen */
	return 1;
L674:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, ".", 1) != 0) goto L677;
	if (strncmp(p, "...", 3) == 0) goto L677;	/* Longest-Match */
	p += 1;
	if (!p_fieldName()) goto L677;
	goto L676;
L677:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "->", 2) != 0) goto L678;
	p += 2;
	if (!p_fieldName()) goto L678;
	goto L676;
L678:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L675;
L676:	sp--;
	return 1;
L675:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L679;
	actionLogPush(224, entry, p);	/* ACTION AFTER defName */
	return 1;
L679:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L680;
	return 1;
L680:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L681;
	actionLogPush(226, entry, p);	/* ACTION AFTER localName */
	return 1;
L681:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L682;
	return 1;
L682:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L683;
	actionLogPush(228, entry, p);	/* ACTION AFTER funcName */
	return 1;
L683:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "unsigned", 8) != 0) goto L686;
	if (idch((unsigned char)p[8])) goto L686;
	p += 8;
	if (!p_unsignedInt()) goto L686;
	goto L685;
L686:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "signed", 6) != 0) goto L687;
	if (idch((unsigned char)p[6])) goto L687;
	p += 6;
	if (!p_unsignedInt()) goto L687;
	goto L685;
L687:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L688;
	if (idch((unsigned char)p[3])) goto L688;
	p += 3;
	goto L685;
L688:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_longLongType()) goto L689;
	goto L685;
L689:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_longDoubleType()) goto L690;
	goto L685;
L690:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L691;
	if (idch((unsigned char)p[4])) goto L691;
	p += 4;
	goto L685;
L691:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "short", 5) != 0) goto L692;
	if (idch((unsigned char)p[5])) goto L692;
	p += 5;
	goto L685;
L692:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L693;
	if (idch((unsigned char)p[4])) goto L693;
	p += 4;
	goto L685;
L693:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L694;
	if (idch((unsigned char)p[4])) goto L694;
	p += 4;
	goto L685;
L694:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "double", 6) != 0) goto L695;
	if (idch((unsigned char)p[6])) goto L695;
	p += 6;
	goto L685;
L695:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L696;
	if (idch((unsigned char)p[6])) goto L696;
	p += 6;
	if (!p_structTypeRef()) goto L696;
	goto L685;
L696:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "union", 5) != 0) goto L697;
	if (idch((unsigned char)p[5])) goto L697;
	p += 5;
	if (!p_unionTypeRef()) goto L697;
	goto L685;
L697:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L698;
	if (idch((unsigned char)p[4])) goto L698;
	p += 4;
	if (!p_enumTypeRef()) goto L698;
	goto L685;
L698:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "void", 4) != 0) goto L699;
	if (idch((unsigned char)p[4])) goto L699;
	p += 4;
	goto L685;
L699:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_typedefRef()) goto L700;
	goto L685;
L700:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L684;
L685:	sp--;
	actionLogPush(229, entry, p);	/* ACTION AFTER type */
	return 1;
L684:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* longDoubleType */
static int p_longDoubleType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "long", 4) != 0) goto L701;
	if (idch((unsigned char)p[4])) goto L701;
	p += 4;
	ws();
	if (strncmp(p, "double", 6) != 0) goto L701;
	if (idch((unsigned char)p[6])) goto L701;
	p += 6;
	return 1;
L701:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* longLongType */
static int p_longLongType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "long", 4) != 0) goto L702;
	if (idch((unsigned char)p[4])) goto L702;
	p += 4;
	ws();
	if (strncmp(p, "long", 4) != 0) goto L702;
	if (idch((unsigned char)p[4])) goto L702;
	p += 4;
	actionLogPush(231, entry, p);	/* ACTION AFTER longLongType */
	return 1;
L702:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* unionTypeRef */
static int p_unionTypeRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L703;
	return 1;
L703:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L704;
	return 1;
L704:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L705;
	return 1;
L705:	p = entry; actionLogLen = entryLog;
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
	if (!p_ident()) goto L706;
	return 1;
L706:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* pointerDecl */
static int p_pointerDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L708:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_pointerStar()) goto L709;
	sp--; goto L708;
L709:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(236, entry, p);	/* ACTION AFTER pointerDecl */
	return 1;
L707:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "*", 1) != 0) goto L710;
	if (strncmp(p, "*=", 2) == 0) goto L710;	/* Longest-Match */
	p += 1;
	return 1;
L710:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "int", 3) != 0) goto L713;
	if (idch((unsigned char)p[3])) goto L713;
	p += 3;
	goto L712;
L713:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L714;
	if (idch((unsigned char)p[4])) goto L714;
	p += 4;
	goto L712;
L714:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L715;
	if (idch((unsigned char)p[4])) goto L715;
	p += 4;
	goto L712;
L715:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "short", 5) != 0) goto L716;
	if (idch((unsigned char)p[5])) goto L716;
	p += 5;
	goto L712;
L716:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L711;
L712:	sp--;
	return 1;
L711:	p = entry; actionLogLen = entryLog;
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
	if (strncmp(p, "true", 4) != 0) goto L719;
	if (idch((unsigned char)p[4])) goto L719;
	p += 4;
	goto L718;
L719:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L720;
	if (idch((unsigned char)p[5])) goto L720;
	p += 5;
	goto L718;
L720:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L717;
L718:	sp--;
	actionLogPush(239, entry, p);	/* ACTION AFTER boolLit */
	return 1;
L717:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* ident (lexikalisch) */
static int p_ident(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_letter()) goto L721;
L722:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_letter()) goto L725;
	goto L724;
L725:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_digit()) goto L726;
	goto L724;
L726:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L723;
L724:	sp--;
	sp--; goto L722;
L723:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L721:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* number (lexikalisch) */
static int p_number(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_hexNumber()) goto L729;
	goto L728;
L729:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_decNumber()) goto L730;
	goto L728;
L730:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L727;
L728:	sp--;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_integerSuffix()) goto L731;
	sp--; goto L732;
L731:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L732:	;
	actionLogPush(241, entry, p);	/* ACTION AFTER number */
	return 1;
L727:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* integerSuffix (lexikalisch) */
static int p_integerSuffix(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "u", 1) != 0) goto L735;
	p += 1;
	if (strncmp(p, "l", 1) != 0) goto L735;
	p += 1;
	goto L734;
L735:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "U", 1) != 0) goto L736;
	p += 1;
	if (strncmp(p, "L", 1) != 0) goto L736;
	p += 1;
	goto L734;
L736:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "l", 1) != 0) goto L737;
	p += 1;
	if (strncmp(p, "u", 1) != 0) goto L737;
	p += 1;
	goto L734;
L737:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "L", 1) != 0) goto L738;
	p += 1;
	if (strncmp(p, "U", 1) != 0) goto L738;
	p += 1;
	goto L734;
L738:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "u", 1) != 0) goto L739;
	p += 1;
	goto L734;
L739:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "U", 1) != 0) goto L740;
	p += 1;
	goto L734;
L740:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "l", 1) != 0) goto L741;
	p += 1;
	goto L734;
L741:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "L", 1) != 0) goto L742;
	p += 1;
	goto L734;
L742:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L733;
L734:	sp--;
	return 1;
L733:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* hexNumber (lexikalisch) */
static int p_hexNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "0", 1) != 0) goto L743;
	p += 1;
	if (!p_hexMark()) goto L743;
	if (!p_hexDigit()) goto L743;
L744:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_hexDigit()) goto L745;
	sp--; goto L744;
L745:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L743:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* hexMark (lexikalisch) */
static int p_hexMark(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "x", 1) != 0) goto L748;
	p += 1;
	goto L747;
L748:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "X", 1) != 0) goto L749;
	p += 1;
	goto L747;
L749:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L746;
L747:	sp--;
	return 1;
L746:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* hexDigit (lexikalisch) */
static int p_hexDigit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L752;
	goto L751;
L752:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x61 || (unsigned char)*p > 0x66) goto L753;
	p++;
	goto L751;
L753:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x41 || (unsigned char)*p > 0x46) goto L754;
	p++;
	goto L751;
L754:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L750;
L751:	sp--;
	return 1;
L750:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* decNumber (lexikalisch) */
static int p_decNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_digit()) goto L755;
L756:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L757;
	sp--; goto L756;
L757:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L755:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* floatLit (lexikalisch) */
static int p_floatLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_digit()) goto L758;
L759:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L760;
	sp--; goto L759;
L760:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	if (!p_floatTail()) goto L758;
	actionLogPush(247, entry, p);	/* ACTION AFTER floatLit */
	return 1;
L758:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* floatTail (lexikalisch) */
static int p_floatTail(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_floatDotTail()) goto L763;
	goto L762;
L763:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_floatExp()) goto L764;
	goto L762;
L764:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L761;
L762:	sp--;
	return 1;
L761:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* floatDotTail (lexikalisch) */
static int p_floatDotTail(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, ".", 1) != 0) goto L765;
	p += 1;
L766:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L767;
	sp--; goto L766;
L767:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_floatExp()) goto L768;
	sp--; goto L769;
L768:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L769:	;
	return 1;
L765:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* floatExp (lexikalisch) */
static int p_floatExp(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "e", 1) != 0) goto L772;
	p += 1;
	goto L771;
L772:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "E", 1) != 0) goto L773;
	p += 1;
	goto L771;
L773:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L770;
L771:	sp--;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_floatExpSign()) goto L774;
	sp--; goto L775;
L774:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L775:	;
	if (!p_digit()) goto L770;
L776:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L777;
	sp--; goto L776;
L777:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L770:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* floatExpSign (lexikalisch) */
static int p_floatExpSign(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "+", 1) != 0) goto L780;
	p += 1;
	goto L779;
L780:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "-", 1) != 0) goto L781;
	p += 1;
	goto L779;
L781:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L778;
L779:	sp--;
	return 1;
L778:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* letter (lexikalisch) */
static int p_letter(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x61 || (unsigned char)*p > 0x7A) goto L784;
	p++;
	goto L783;
L784:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x41 || (unsigned char)*p > 0x5A) goto L785;
	p++;
	goto L783;
L785:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "_", 1) != 0) goto L786;
	p += 1;
	goto L783;
L786:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L782;
L783:	sp--;
	return 1;
L782:	p = entry; actionLogLen = entryLog;
	return 0;
}

/* digit (lexikalisch) */
static int p_digit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if ((unsigned char)*p < 0x30 || (unsigned char)*p > 0x39) goto L787;
	p++;
	return 1;
L787:	p = entry; actionLogLen = entryLog;
	return 0;
}

#define INPUT_FILE_MAX 524288
static char* inputFileBuf;

int main(int argc, char** argv) {
	FILE* inputFile; size_t inputLen;
	if (argc < 2) { fprintf(stderr, "usage: %s <eingabe>\n", argv[0]); return 2; }
	if (*argv[1] == '@') {
		inputFile = fopen(argv[1] + 1, "r");
		if (!inputFile) { fprintf(stderr, "can't open %s\n", argv[1] + 1); return 2; }
		inputFileBuf = realloc(0, INPUT_FILE_MAX);
		if (!inputFileBuf) { fclose(inputFile); fprintf(stderr, "out of memory\n"); return 2; }
		inputLen = fread(inputFileBuf, 1, INPUT_FILE_MAX - 1, inputFile);
		fclose(inputFile); inputFileBuf[inputLen] = '\0'; p = inputFileBuf;
	} else p = argv[1];
	parserInputStart = p;
	if (p_program()) { ws(); if (*p == '\0') { actionLogReplay(); if (actionErrors != 0) { printf("SEMERR\n"); QCC_OUTPUT_FLUSH(); return 1; } printf("OK\n"); QCC_OUTPUT_FLUSH(); return 0; } }
	printf("FAIL\n"); QCC_OUTPUT_FLUSH();
	return 1;
}
