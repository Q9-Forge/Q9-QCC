//=============================================================================
// qcc_backend_c.cpp -- Q9 Stack-IR to position-independent 68k assembly
//
// Purpose:
//   Translate Q9 Stack-IR into OS-9-compatible 68k assembly using fixed tables
//   and C-compatible runtime helpers so the backend can participate in QCC
//   self-hosting.
//
// Edition history:
//   2026-09-11  Introduced the English source-header format.
//=============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_LEN          24
#define ARG_LEN         64
#define NAME_LEN        64
#define LINE_LEN        512
#define MAX_ARGS        6
/* 2026-07-25: increased from 8192; the -largedata function-call mode
   (a4/a2 indirection table instead of bsr) made this cap too small for
   realistic programs (150 generated functions already produced over 36,000
   IR lines). It was already guarded by fatal(), but the limit was too low. */
#define MAX_IR_LINES    98304
/* 2026-08-10 increased from 256 to 1024: Data/qcc_p.c alone has 354
   functions, so self-hosting reached this limit. */
#define MAX_FUNCS       1024
/* 2026-07-25: increased from 256. During the SourceQCC/codegen.tc
   self-hosting test, this cap blocked verification: every string literal in
   the QCC source becomes an anonymous __strN global, and the cumulative
   compilation already contains well over 256 such literals, in addition to
   real globals such as nodes[8192]. It was already guarded by fatal(), but
   the limit was too low, just like MAX_IR_LINES above. */
/* The complete generated qcc_p parser contains about 1,053 globals, mostly
   string literals. 1024 was therefore an artificial bootstrap limit, not a
   memory limit. */
#define MAX_GLOBALS     2048
#define MAX_ARRAY_LEN   4096

/* 2026-08-11: `args` used to be `char args[MAX_ARGS][ARG_LEN]`, or 6x64 = 384
   bytes, making each instruction 416 bytes. At MAX_IR_LINES=65536 this
   required a 26 MB static field. On the Q9 (16 MB RAM), the compiler was
   therefore unusable regardless of language limitations. The entries now
   point into a shared text pool (see argPool), reducing Instr to
   24 + 6*sizeof(char*) + 8 bytes, or 56 bytes with 4-byte 68k pointers.
   The complete field is now about 3.7 MB.

   The entries deliberately remain POINTERS rather than offset indices, so
   all 101 read sites (`insP->args[i]` as `const char*`) remain valid. Only the
   single write site in readIR() needed adjustment. Unused arguments point to
   an empty string, preserving the former behavior of the zero-initialized
   array and allowing readers to rely on "" rather than NULL. */
typedef struct {
	char op[OP_LEN];
	char* args[MAX_ARGS];
	int argc;
	int line;
} Instr;

typedef struct {
	char name[NAME_LEN];
	int nargs, first, last, locals, frameBytes;
	/* Multi-file translation (2026-07-25): declOnly is registered by FUNCDECL,
	   without a body in this file (defined in another QCC file); first/last/
	   locals/frameBytes remain unused (0/-1). isStatic controls name mangling.
	   r68/l68 have no visibility concept, so mangling is the only way for two
	   files to use the same private helper name without a duplicate symbol. */
	int declOnly, isStatic;
} Function;

typedef struct {
	char name[NAME_LEN];
	int initialValue;
	/* 2026-09-09: formerly "isChar" (bool), now the actual size with short in
	   bytes (1/2/4); see tagSize(). Readers use a three-way switch instead of
	   "isChar ? X : Y". */
	int elemSize;
	int isArray;
	int length;
	/* 2026-08-11: formerly `int init[MAX_ARRAY_LEN]`, or 16 KB per global; with
	   MAX_GLOBALS=1024 required a 16.8 MB static field, the backend's largest
	   single allocation and already too large for the Q9's 16 MB RAM. It is now
	   a pointer into initPool, allocated only at the FIRST GINIT: arrays without
	   initializers cost nothing. Measured actual usage: ebnf.tc 20.5 KB and
	   codegen.tc 34.9 KB.
	   initLen is the number of actually allocated elements (<= MAX_ARRAY_LEN);
	   init == NULL means "no initializer; all values are zero". */
	int* init;
	int initLen;
	int hasGinit; /* 2026-07-25: at least one GINIT was seen for this array */
	int declOnly, isStatic; /* see Function */
} Global;

static Instr ir[MAX_IR_LINES];

/* Text pool for arguments of all IR lines (see the Instr comment).
   Sized from real data rather than guessed: SourceQCC/ebnf.tc (12,220 IR
   lines) needs 109 KB of argument text, while codegen.tc (16,469 lines) needs
   162 KB, or about 9 bytes per line. At MAX_IR_LINES=65536 this projects to
   about 0.6 MB; 1 MB leaves more than 60% headroom. Exhaustion is reported
   loudly with fatal(), like the other backend capacity limits, rather than
   being silently truncated. */
#define ARG_POOL_BYTES  1048576
static char argPool[ARG_POOL_BYTES];
static int  argPoolUsed;
/* Target for unused argument slots; see the Instr comment. */
static char argEmpty[1];

static int irCount = 0;

static Function funcs[MAX_FUNCS];
static int funcCount = 0;

static Global globals[MAX_GLOBALS];
static int globalCount = 0;

static void fatal(const char* msg); /* Definition weiter unten, hier nur fuer registerExtern()/externTableOffset() vorwaertsdeklariert */

/* Stores tok in the pool and returns its pointer. */
static char* argIntern(const char* tok, int irLine)
{
	int len;
	char msg[160];
	char* dst;

	len = (int)strlen(tok);
	if (len > ARG_LEN - 1) len = ARG_LEN - 1;   /* wie vormals strncpy(.., ARG_LEN-1) */
	if (argPoolUsed + len + 1 > ARG_POOL_BYTES) {
		sprintf(msg, "IR Zeile %d: Argument-Textpool erschoepft (%d Byte)", irLine, ARG_POOL_BYTES);
		fatal(msg);
	}
	dst = &argPool[argPoolUsed];
	memcpy(dst, tok, len);   /* kein (size_t)-Cast: QCC kennt "(unsigned)" ohne "int" noch nicht, und der implizite Uebergang genuegt */
	dst[len] = '\0';
	argPoolUsed += len + 1;
	return dst;
}

/* Pool for global-array initializers (see the Global.init comment).
   Groesse an echten Daten bemessen: ebnf.tc braucht 20,5 KB, codegen.tc
   34,9 KB -- 256 KB lassen damit ueber das Siebenfache Luft. Wie bei den
   uebrigen Kapazitaetsgrenzen meldet Erschoepfung laut per fatal(). */
#define INIT_POOL_INTS  65536
static int initPool[INIT_POOL_INTS];
static int initPoolUsed;

/* Allocates n zero-initialized elements and returns their pointer. */
static int* initAlloc(int n, int irLine)
{
	char msg[160];
	int* dst;
	int k;

	if (initPoolUsed + n > INIT_POOL_INTS) {
		sprintf(msg, "IR Zeile %d: Initialisierer-Pool erschoepft (%d Elemente)", irLine, INIT_POOL_INTS);
		fatal(msg);
	}
	dst = &initPool[initPoolUsed];
	for (k = 0; k < n; k++) dst[k] = 0;
	initPoolUsed += n;
	return dst;
}

/* 2026-07-26, found live on Q9 (see the emitLeaGlobal()/emitCall() comments
   in qcc_backend_c.cpp): every CALLEXT/CALLEXTP call previously used a raw
   "bsr <rawname>" directly to the external clib.l function, corrupting a3/a4
   (ABI temporary registers; see Ultra-C/C++ Processor Guide Table 1-12).
   Refreshing a3/a4 directly at the call site with "lea (pc)" also fails in
   r68 with "value out of range" once the call site is more than 32 KB from
   tc_functab/tc_gadata. The entire a3/a4 indirection exists specifically to
   overcome that limit. SOLUTION: every real external function called through
   CALLEXT/CALLEXTP receives its own small wrapper stub
   ("tc_extwrap_<name>", placed directly beside tc_gadata; see emitIR()).
   The wrapper performs the real "bsr <rawname>" (PC-relative and safe because
   it is close to the other early code) and then refreshes a3/a4. Call sites
   invoke the wrapper through the same a4 table-indirection mechanism used for
   internal QCC functions (emitCall()), which supports arbitrary distances via
   register-indirect jsr without a PC-relative distance limit. */
#define MAX_EXTERNS 128
static char externNames[MAX_EXTERNS][NAME_LEN];
static int externCount = 0;

static int findExtern(const char* name) {
	int i;
	for (i = 0; i < externCount; i++) if (strcmp(externNames[i], name) == 0) return i;
	return -1;
}

static int registerExtern(const char* name) {
	int idx = findExtern(name);
	if (idx >= 0) return idx;
	if (externCount >= MAX_EXTERNS) fatal("too many different external functions (CALLEXT/CALLEXTP)");
	strncpy(externNames[externCount], name, NAME_LEN - 1);
	return externCount++;
}

/* Table offset of one external wrapper, directly after the fixed helpers and
   QCC functions. */
static int externTableOffset(const char* name) {
	int idx = findExtern(name);
	if (idx < 0) fatal("internal error: external function not registered");
	return 8 * 4 + idx * 4;
}

/* -os9: Microware r68 output format instead of vasm-compatible "bare" Motorola
   syntax (see genParser68kTo in Source/codegen.cpp for the same parser-codegen
   technique). That behavior was verified empirically: r68 accepts label
   colons and trailing ';' comments unchanged, but requires '*' instead of ';'
   for full comment lines plus a nam/psect/ends wrapper. Instruction generation
   in the emitIR dispatch below is therefore mostly identical for both formats,
   with one important exception: the frame-pointer register (see framePtr()). */
static int os9Mode = 0;
/* -part (2026-07-25, multi-file translation): this file is ONE PART of a
   multi-file program, not a complete program by itself; the main/funcCount
   requirement is relaxed (see collectFunctions()/emitIR()). */
static int partMode = 0;
/* -runtime (2026-07-25, multi-file translation): the 68k core (mul/div,
   emitM68kCore) and putint/putuint/putchar/tc_io_write plus their scratch
   storage (tc_extcall_tmp/tc_io_buf/tc_io_cnt) are ALWAYS emitted without
   -part (complete-program assumption, unchanged). With -part, EVERY file
   would contain its OWN copy of these symbols, and l68 reliably rejects that
   at link time as "duplicate symbol" (see docs/STATUS.md; verified
   empirically). Therefore, under -part they are emitted ONLY when -runtime is
   also set: EXACTLY ONE file in the multi-file program carries the shared
   anchor, while all others reference it as an undefined symbol resolved by
   the linker, like any other cross-file call. */
static int runtimeMode = 0;
/* -largedata (2026-07-25, memory-model switch): see the detailed comment at
   emitLeaGlobal() below. The default model addresses every global exclusively
   PC-relatively (real 68000 limit: 16-bit displacement, +/-32 KB). This switch
   adds an indirection table with linker-resolved addresses, making globals at
   arbitrary distances reachable at the cost of one additional memory access
   per reference. */
static int largeDataMode = 0;
/* -remotedata (2026-09-08): zero-initialized globals are emitted in a
   "vsect remote" instead of as dc.l 0 in the psect. OS-9 clears that data area
   itself (measured on 2026-09-07; not documented in the manual), so the module
   does not need to carry the zero bytes. Access is a full 32-bit a6-relative
   operation (movea.l #sym,reg / adda.l a6,reg, the same pattern used in the
   production runtime/os9/q9_cstart.a), avoiding both the 32 KB PC-relative
   limit and the 64 KB limit of a non-remote vsect. See docs/FORTSCHRITT.md. */
static int remoteDataMode = 0;
/* Experimental long-call path; without -trampolines the tested table path
   remains unchanged. */
static int trampolineMode = 0;
static char psectName[NAME_LEN] = "tc_prog";
/* -unit=<name> groups artificially split IR parts that originated from one
   translation unit.  It deliberately affects only static-symbol mangling:
   normal multi-file builds keep using their individual psect name. */
static char staticUnit[NAME_LEN] = "";
static const char* fullCommentPrefix(void) { return os9Mode ? "*" : ";"; }
/* Register Use Table im Ultra-C/C++-Prozessorhandbuch (ultrac_pg.pdf, Kapitel
   "68K" -> "Register Usage"): a5 = Frame/local pointer, a6 = STATIC STORAGE
   POINTER (not the frame pointer!). The real cstart.r/clib.l uses a6 as a
   pointer to its own static data area THROUGHOUT the lifetime of the program
   (our code is linked into the same module). If our code overwrites it, as
   the default/vasm format does with "link a6,#N", every later real clib call
   fails with a PMMU error. This was verified on the real Q9 on 2026-07-24:
   a test module using a6 as its frame pointer and then calling _os_write
   crashed the emulator because a6 pointed to our frame instead of the real
   static data area. Therefore, only -os9 uses a5 as frame pointer; a6 remains
   untouched. The default/vasm format continues using a6, with no need to
   change the QCCVM/simulator tests. */
static const char* framePtr(void) { return os9Mode ? "a5" : "a6"; }
/* Name mangling for static symbols (multi-file translation, 2026-07-25):
   r68/l68 have NO visibility concept (no xdef/xref; every label is visible to
   every other linked file -- verified empirically, see docs/STATUS.md). Without
   mangling, "static" would fail its main purpose: independently compiled
   files could each define a private helper with the SAME name, such as their
   own "static int init()", which l68 otherwise rejects as a duplicate symbol.
   This is a convention, not enforcement; the psect name derived from the
   output filename (see main()) provides the natural per-file uniqueness. */
static char* mangledName(char* buf, const char* prefix, const char* name, int isStatic) {
	if (os9Mode && isStatic) sprintf(buf, "%s%s__%s", prefix, name,
		staticUnit[0] ? staticUnit : psectName);
	else sprintf(buf, "%s%s", prefix, name);
	return buf;
}
/* vasm supports "even" (alignment to an even address), but the real Microware
   r68 assembler does NOT know "even" (verified empirically: "bad mnemonic").
   It does support "align 4", which is stricter than "even" but is the intended
   alignment for dc.l data and was also verified empirically. */
static void emitAlign(FILE* out) {
	fputs(os9Mode ? "\talign\t4\n" : "\teven\n", out);
}

/* "Speichermodell"-Schalter (2026-07-25, siehe -largedata in main()/usage()):
   Standardmodell ("small") adressiert JEDES Globale ausschliesslich PC-relativ
   ("lea tc_g_X(pc),a0") -- das ist eine ECHTE 68000-Hardware-Grenze (16-Bit-
   Displacement, +-32 KB Reichweite von der jeweiligen Instruktion aus), keine
   willkuerliche Software-Grenze. Empirisch am echten r68-Assembler bestaetigt
   (siehe docs/FORTSCHRITT.md): ein 8192-Elemente-Array (458 KB) wird mit
   "value out of range" abgelehnt.
   "-largedata" wechselt auf eine zusaetzliche Indirektionstabelle (tc_gadata,
   ein 4-Byte-Eintrag "dc.l tc_g_X" pro Globaler IN globals[]-REIHENFOLGE,
   siehe emitIR() weiter unten) -- l68 loest "dc.l tc_g_X" als ABSOLUTE Adresse
   auf (normale Relokation, keine Distanzbeschraenkung).
   URSPRUENGLICHES Design (bis 2026-07-25 abends) lud JEDEN Tabelleneintrag per
   EIGENEM PC-relativem Label "movea.l tc_ga_X(pc),reg" -- das brach beim
   ersten genParserC-artigen Skalierungstest (SourceQCC/codegen.tc mit vielen
   Funktionen VOR der Tabelle, die selbst NACH dem gesamten Funktionscode
   liegt): "main" (per Funktionstabellen-Fix immer zuerst emittiert) konnte die
   Tabelle nicht mehr per PC-relativem Label erreichen, sobald der GESAMTE
   Funktionscode zwischen main und Tabelle mehr als 32 KB umfasste -- derselbe
   Grenzwert, nur diesmal fuer den TABELLENZUGRIFF SELBST statt fuer die Daten
   dahinter. GEFIXT nach EXAKT demselben Muster wie tc_functab/a4 (siehe
   emitCall()-Kommentar): EIN Register (a3) wird EINMAL beim Programmstart auf
   die absolute Adresse von tc_gadata gesetzt ("lea tc_gadata(pc),a3" -- die
   Tabelle liegt bewusst DIREKT nach tc_functab, also VOR allen Funktionsrumpf-
   Texten, bleibt also immer erreichbar). Jeder Globalzugriff wird dann zu
   "move.l <gidx*4>(a3),reg" -- a3-relative Adressierung hat zwar auch nur
   16-Bit-Displacement, aber die Tabelle waechst nur mit der ANZAHL der
   Globalen (4 Byte/Eintrag), nicht mit der Code-GROESSE. a3 war zuvor an
   keiner Stelle im Backend belegt (a0=Skalar-Scratch, a1=Puffer in tc_putint/
   tc_putuint/tc_putchar, a2=Aufruf-Scratch fuer emitCall, a4=Funktionstabelle,
   a5/a6=Frame-Pointer je nach os9Mode). */
/* WICHTIG (2026-07-26, live auf Q9 gefunden, siehe emitCall()-Kommentar):
   tc_gadata enthaelt KEINE absoluten Adressen mehr, sondern Link-Zeit-Offsets
   (Ziel minus Tabellenbasis) -- move.l laedt den Offset, "adda.l a3,reg" macht
   daraus die echte Laufzeitadresse (a3 ist per "lea (pc)" bereits korrekt
   geladen). reg ist an JEDER Aufrufstelle ein Adressregister (a0), adda.l
   akzeptiert ein Adressregister als Quelle problemlos.
   ZWEITER FUND (2026-07-26, live auf Q9, Ultra-C/C++ Processor Guide Table
   1-12 "Register Use"): a3 (wie a4, a0-a2) ist laut offizieller Microware-ABI
   ein reines TEMPORAER-Register ("The compiler uses all other registers for
   temporaries") -- NUR d0/d1 (Parameter/Rueckgabe), a5 (Frame), a6 (Static
   Storage) und a7 (Stack) sind reserviert. Jede ECHTE clib.l-Funktion
   (fopen/strlen/fprintf/_os_write/...) darf a3 also ungefragt ueberschreiben.
   Das urspruengliche Design ("a3 EINMAL beim Programmstart setzen, bleibt
   fuer immer gueltig") bricht deshalb beim ERSTEN echten externen Aufruf nach
   dem allerersten Globalzugriff -- live reproduziert (tc_putint ->
   tc_io_write -> bsr _os_write zerstoerte a3, der naechste Globalzugriff las
   von einer falschen Basisadresse).
   ERSTER FIX-VERSUCH (verworfen): a3 vor JEDEM Zugriff per "lea (pc)" neu
   laden -- brach den echten r68-Assembler ("value out of range"), weil "lea
   X(pc)" selbst wieder der 16-Bit-PC-relativ-Distanzgrenze unterliegt, die
   die ganze a3/a4-Indirektion ja gerade umgehen sollte. RICHTIGER FIX: a3/a4
   werden NUR direkt NACH jedem CALLEXT/CALLEXTP neu geladen (siehe dortigen
   Kommentar) -- das ist der EINZIGE Ort, an dem sie kaputtgehen koennen, und
   die Auffrischung steht IMMER im selben Funktionskoerper wie der Aufruf
   selbst (kurze Distanz, nie ueber 32 KB). emitLeaGlobal()/emitCall() selbst
   bleiben unveraendert (verlassen sich weiterhin auf den zuletzt
   aufgefrischten Wert). */
/* A global is ALL ZERO when no initializer assigns a value: an array without
   any GINIT or a scalar with initial value 0. These belong
   in the remote vsect; arrays WITH GINIT remain in the psect or their values
   would be lost. */
static int globalAllZero(Global* g) {
	if (g->isArray) return !g->hasGinit;
	return g->initialValue == 0;
}
/* Does this global reside in the remote vsect? Keep the decision in one place
   because it is needed by several emission sites (data output, tables, and
   eight access forms); an addressing rule must not be duplicated. */
static int globalRemote(int gidx) {
	if (!remoteDataMode) return 0;
	if (globals[gidx].declOnly) return 0;
	return globalAllZero(&globals[gidx]);
}
static void emitLeaGlobal(FILE* out, int gidx, const char* reg) {
	if (globalRemote(gidx)) {
		/* a6 = process data base (Ultra-C manual: static storage pointer). In
		   -os9 mode a5 is the frame pointer, so a6 remains untouched. A vsect
		   symbol value is an offset within this area, inserted by l68/ql68 at
		   link time; no runtime relocation or distance limit is needed because
		   the immediate is 32 bits wide. */
		char gAsmName[NAME_LEN + 40];
		mangledName(gAsmName, "tc_g_", globals[gidx].name, globals[gidx].isStatic);
		fprintf(out, "\tmovea.l\t#%s,%s\n\tadda.l\ta6,%s\n", gAsmName, reg, reg);
	} else if (largeDataMode) {
		fprintf(out, "\tmove.l\t%d(a3),%s\n\tadda.l\ta3,%s\n", gidx * 4, reg, reg);
	} else {
		char gAsmName[NAME_LEN + 40];
		mangledName(gAsmName, "tc_g_", globals[gidx].name, globals[gidx].isStatic);
		fprintf(out, "\tlea\t%s(pc),%s\n", gAsmName, reg);
	}
}

static void fatal(const char* msg) {
	fprintf(stderr, "qcc_backend: %s\n", msg);
	exit(1);
}
static int findFunction(const char* name) {
	int i;
	for (i = 0; i < funcCount; i++) if (strcmp(funcs[i].name, name) == 0) return i;
	return -1;
}

static int findGlobal(const char* name) {
	int i;
	for (i = 0; i < globalCount; i++) if (strcmp(globals[i].name, name) == 0) return i;
	return -1;
}

/* -largedata (function-call part, 2026-07-25, requested as "automatically
   build a jump table when calls are too far away"): like lea(pc), bsr uses a
   16-bit PC-relative displacement. This does NOT affect internal bra/beq/bne
   targets within one function (LABEL/JMP/JZ/JNZ are bounded by one function),
   but it does affect cross-function calls (CALL/CALLP and runtime helpers such
   as tc_mul_i32) whose call sites may be spread across an arbitrarily large
   program.
   Solution: one register (a4) is initialized ONCE at program start with the
   address of a small table (tc_functab), deliberately placed directly after
   tc_start/main so it remains reachable regardless of the size of the rest of
   the program. Each call becomes "move.l N(a4),a2\njsr (a2)" instead of
   "bsr X". The table grows only with the NUMBER of functions (4 bytes per
   entry), not with code size, so a4-relative addressing remains practical.
   a2 is used as scratch (NOT a0/a1): a0 carries a pointer across IPADDN's bsr,
   while a1 is used as a buffer pointer by tc_putint/tc_putuint/tc_putchar;
   a2 is demonstrably free at every affected call site. */
static int helperTableOffset(const char* rawName) {
	/* WRITTEN OUT INSTEAD OF A TABLE so QCC can compile this file
	   (2026-09-07): QCC's subset does not support a pointer array with an
	   initializer list, and a preceding "static" inside the function also
	   fails silently (final status FAIL, no diagnostic). The backend must be
	   self-compilable or it can never run on the 68030. This order is the table
	   order and determines the returned offset i * 4; inserting an entry shifts
	   all offsets in the -largedata function table. */
	if (strcmp(rawName, "tc_mul_i32")  == 0) return 0;
	if (strcmp(rawName, "tc_div_i32")  == 0) return 4;
	if (strcmp(rawName, "tc_udiv_u32") == 0) return 8;
	if (strcmp(rawName, "tc_mod_i32")  == 0) return 12;
	if (strcmp(rawName, "tc_umod_u32") == 0) return 16;
	if (strcmp(rawName, "tc_putint")   == 0) return 20;
	if (strcmp(rawName, "tc_putuint")  == 0) return 24;
	if (strcmp(rawName, "tc_putchar")  == 0) return 28;
	fatal("internal error: unknown runtime helper for -largedata function table");
	return -1;
}

/* Rebuild this psect's large-data table bases from an instruction-local
   anchor.  The anchor is deliberately close to its use: a function name or a
   synthetic return label can never exceed the 68000's PC-relative range. */
static void emitTableBases(FILE* out, const char* anchor, const char* psect) {
	fprintf(out, "\tlea\t%s(pc),a4\n", anchor);
	fprintf(out, "\tadda.l\t#(tc_functab__%s-%s),a4\n", psect, anchor);
	fprintf(out, "\tlea\t%s(pc),a3\n", anchor);
	fprintf(out, "\tadda.l\t#(tc_gadata__%s-%s),a3\n", psect, anchor);
}

/* Emits a call to an ALREADY MANGLED assembler name (for QCC-
   Funktionen, tableOffset = funcIndex*4) ODER einem rohen Laufzeit-Helfer-
   Namen (tableOffset = helperTableOffset(...)) -- small: unveraendert "bsr
   asmName"; large: Tabellen-Indirektion ueber a4/a2, siehe Kommentar oben.
   WICHTIG (2026-07-26, live auf Q9 gefunden): die Tabelle enthaelt KEINE
   absoluten Adressen mehr (siehe tc_functab-Emissionskommentar) -- a2 traegt
   nach dem move.l erst den Link-Zeit-Offset (Ziel minus Tabellenbasis), "adda.l
   a4,a2" macht daraus die echte Laufzeitadresse (a4 ist per "lea (pc)" bereits
   korrekt geladen).
   ZWEITER FUND (2026-07-26, siehe emitLeaGlobal()-Kommentar): a4 ist laut
   Ultra-C/C++-ABI (Table 1-12) genauso ein reines Temporaer-Register wie a3 --
   jede echte clib.l-Funktion darf es zerstoeren. Fix (nach verworfenem
   Versuch, a4 hier bei JEDEM Aufruf neu zu laden -- brach r68 mit "value out
   of range", siehe emitLeaGlobal()-Kommentar): a4 wird stattdessen NUR direkt
   NACH jedem CALLEXT/CALLEXTP neu geladen (dort, wo es kaputtgehen kann). */
/* BUG 5 (2026-07-26, live auf Q9 gefunden, FUENFTER -largedata-Bug dieser
   Sitzung): jede Funktion frischt a3/a4 seit dem Bug-4-Fix GLEICH NACH dem
   eigenen "link" auf IHRE EIGENE Tabelle auf -- das heisst aber auch: NACH
   der Rueckkehr aus JEDEM internen Aufruf (CALL/CALLP, auch Laufzeit-Helfer
   wie tc_putint) zeigen a3/a4 auf die Tabelle der AUFGERUFENEN Funktion,
   NICHT mehr auf die eigene! Bei einem gleichdatei-Aufruf faellt das nicht
   auf (dieselbe Tabelle), bei einem Cross-File-Aufruf (FUNCDECL-Ziel in
   einer ANDEREN Datei, z.B. tcCopyBounded in codegen.tc von ebnf.tc aus
   aufgerufen) zeigt a4 danach auf die TABELLE DER FREMDEN DATEI -- der
   naechste Tabellen-Zugriff im Aufrufer (z.B. ein simples putchar(...)
   direkt nach dem Aufruf) laedt dadurch einen voellig falschen
   Funktionszeiger und stuerzt ab ("PMMU: Unhandled Table C/D mode 0",
   sofortiger Komplettabsturz des Emulators). Live bewiesen per gezielter
   Bisektion: Eintritt in tcCopyBounded UND ihr kompletter Funktionskoerper
   (samt Schleife) laufen nachweislich fehlerfrei -- der Fehler tritt exakt
   zwischen ihrer Rueckkehr und dem naechsten Tabellenzugriff im Aufrufer
   auf. FIX: nach JEDEM internen Aufruf (jsr ueber die Tabelle) frischt der
   AUFRUFER a3/a4 sofort wieder auf SEINE EIGENE Tabelle auf -- per
   selbstreferenzierendem lea+adda auf ein NEUES, direkt an dieser Stelle
   emittiertes lokales Label (nicht auf den Funktionsnamen selbst, der bei
   einem Aufruf mitten in einer langen Funktion zu weit entfernt sein
   koennte -- exakt dasselbe Distanzproblem, das der Bug-4-Fix schon einmal
   loesen musste). */
static void emitCall(FILE* out, const char* asmName, int tableOffset, int* serial, const char* psectName) {
	if (largeDataMode) {
		int id = (*serial)++;
		if (trampolineMode) fprintf(out, "\tbsr\t%s\n", asmName);
		else fprintf(out, "\tmove.l\t%d(a4),a2\n\tadda.l\ta4,a2\n\tjsr\t(a2)\n", tableOffset);
		fprintf(out, "tc_callret_%d__%s:\n", id, psectName);
		/* The callee may use a3/a4 as ABI scratch registers.  Rebuild both
		   table bases from this nearby return label before the caller continues. */
		{
			char anchor[NAME_LEN + 40];
			sprintf(anchor, "tc_callret_%d__%s", id, psectName);
			emitTableBases(out, anchor, psectName);
		}
	} else {
		fprintf(out, "\tbsr\t%s\n", asmName);
	}
}

static int isNumWord(const char* w) {
	return strcmp(w, "i") == 0 || strcmp(w, "u") == 0 || strcmp(w, "c") == 0 || strcmp(w, "b") == 0 ||
	       strcmp(w, "h") == 0 || strcmp(w, "p") == 0;
}

/* Byte size of a type tag for LOAD/STORE width and pointer/index scaling
   (2026-09-09, replacing the former isByteWord(): short adds a third size, so
   a boolean is no longer sufficient). 'h' -> 2; everything else remains as
   before (pointer 'p' and all 32-bit scalars 'i'/'u' -> 4). */
static int tagSize(const char* w) {
	if (strcmp(w, "c") == 0 || strcmp(w, "b") == 0) return 1;
	if (strcmp(w, "h") == 0) return 2;
	return 4;
}
/* Shift amount for lsl.l/asr.l scaling in pointer arithmetic/indexing:
   Byte 1x (no shift), word 2x, long 4x. */
static int tagShift(const char* w) { int s = tagSize(w); return s == 1 ? 0 : s == 2 ? 1 : 2; }
/* 68k size suffix for move/dc/ds. */
static char tagSuffix(int size) { return size == 1 ? 'b' : size == 2 ? 'w' : 'l'; }

static int number(const char* text, int line) {
	char* end;
	long v;
	char msg[160];
	if (text[0] == '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	v = strtol(text, &end, 10);
	if (*end != '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	return (int)v;
}

static void readIR(const char* path) {
	FILE* fp;
	char raw[LINE_LEN];
	int line = 0;
	char* tok;
	Instr* insP;
	char msg[300];
	int ai;

	fp = fopen(path, "r");
	if (!fp) { sprintf(msg, "kann IR nicht lesen: %s", path); fatal(msg); }
	while (fgets(raw, sizeof(raw), fp)) {
		line++;
		tok = strtok(raw, " \t\r\n");
		if (!tok || tok[0] == ';' || tok[0] == '#') continue;
		if (strcmp(tok, "OK") == 0 || strcmp(tok, "FAIL") == 0) continue;
		if (irCount >= MAX_IR_LINES) fatal("IR: zu viele Zeilen");
		insP = &ir[irCount++];
		strncpy(insP->op, tok, OP_LEN - 1); insP->op[OP_LEN - 1] = '\0';
		insP->line = line;
		insP->argc = 0;
		/* Initialize every slot to the empty string. Unused arguments formerly
		   contained "" in the zero-initialized array, and readers may continue
		   relying on that behavior. */
		for (ai = 0; ai < MAX_ARGS; ai++) insP->args[ai] = argEmpty;
		while ((tok = strtok(NULL, " \t\r\n")) != NULL) {
			if (insP->argc < MAX_ARGS) {
				insP->args[insP->argc] = argIntern(tok, line);
			}
			insP->argc++;
		}
	}
	fclose(fp);
}

static void collectGlobals(void) {
	/* GLOBAL/GARRAY/GINIT may now appear INSIDE a function: a "static" local
	   variable (Data/qcc.lextab, tc_staticlocal) is registered as an ordinary
	   GLOBAL at the exact source position of its declaration, possibly in the
	   middle of a FUNC...ENDFUNC span. collectFunctions() still verifies that
	   every line is either GLOBAL/GARRAY/GINIT or inside an open function, so a
	   line between functions and outside every FUNC span remains an error. */
	int i, gi, idx, len;
	char msg[300];
	globalCount = 0;
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if (strcmp(insP->op, "GINIT") == 0) {
			int found = 0;
			if (insP->argc != 3) fatal("ungueltiges GINIT");
			for (gi = 0; gi < globalCount; gi++) {
				if (strcmp(globals[gi].name, insP->args[0]) == 0 && globals[gi].isArray) {
					idx = number(insP->args[1], insP->line);
					if (idx < 0 || idx >= globals[gi].length) fatal("GINIT-Index ausserhalb Array");
					/* 2026-07-25: MAX_ARRAY_LEN now limits only indices assigned by
					   GINIT (init[] is a fixed buffer), not the declared GARRAY length.
					   A large unused/zero-initialized array needs no initializer storage;
					   see the zero fill in emitIR(). */
					if (idx >= MAX_ARRAY_LEN) fatal("GINIT-Index ueberschreitet MAX_ARRAY_LEN");
					if (globals[gi].init == NULL) {
						/* Allocate only now, and only as much as GINIT can reach (the
						   check above rejects indices >= MAX_ARRAY_LEN). */
						int want = globals[gi].length < MAX_ARRAY_LEN ? globals[gi].length : MAX_ARRAY_LEN;
						globals[gi].init = initAlloc(want, insP->line);
						globals[gi].initLen = want;
					}
					/* Use an intermediate pointer so QCC can translate this file
					   (2026-09-07): nested indexing through a pointer field is outside
					   the supported subset. The intermediate pointer is the idiom used
					   throughout this file. */
					int* initP = globals[gi].init;
					initP[idx] = number(insP->args[2], insP->line);
					if (globals[gi].elemSize == 1) initP[idx] &= 255;
					else if (globals[gi].elemSize == 2) initP[idx] &= 65535;
					globals[gi].hasGinit = 1;
					found = 1;
					break;
				}
			}
			if (!found) fatal("GINIT fuer unbekanntes Array");
			continue;
		}
		if (strcmp(insP->op, "GLOBAL") != 0 && strcmp(insP->op, "GARRAY") != 0) continue;
		if (strcmp(insP->op, "GARRAY") == 0) {
			/* Fourth argument (2026-07-25, multi-file translation): optional
			   isstatic flag, not interpreted here (see collectFunctions/GLOBALDECL/
			   FUNCDECL). */
			if ((insP->argc != 3 && insP->argc != 4) || !isNumWord(insP->args[1])) fatal("ungueltiges GARRAY");
			if (findGlobal(insP->args[0]) >= 0) fatal("doppelte globale Variable");
			len = number(insP->args[2], insP->line);
			if (len <= 0) fatal("GARRAY-Laenge muss positiv sein");
			/* No MAX_ARRAY_LEN limit here; see the GINIT comment above. A large
			   array never assigned by GINIT needs no init[] storage and is filled
			   compactly below. */
			if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
			gi = globalCount++;
			memset(&globals[gi], 0, sizeof(Global));
			strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
			globals[gi].elemSize = tagSize(insP->args[1]);
			globals[gi].isArray = 1;
			globals[gi].length = len;
			globals[gi].isStatic = insP->argc >= 4 && number(insP->args[3], insP->line) != 0;
			continue;
		}
		if (insP->argc != 1 && insP->argc != 2 && insP->argc != 3 && insP->argc != 4) {
			sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", insP->line);
			fatal(msg);
		}
		if (findGlobal(insP->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: doppelte globale Variable %s", insP->line, insP->args[0]);
			fatal(msg);
		}
		/* argc>=3 rather than ==3 (2026-07-25): the fourth argument is the optional
		   isstatic flag for multi-file translation; the type tag remains at index 2. */
		if (insP->argc >= 3 && !isNumWord(insP->args[2])) {
			sprintf(msg, "IR Zeile %d: unbekannter Globaltyp", insP->line);
			fatal(msg);
		}
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
		globals[gi].initialValue = insP->argc >= 2 ? number(insP->args[1], insP->line) : 0;
		globals[gi].elemSize = insP->argc >= 3 ? tagSize(insP->args[2]) : 4;
		globals[gi].isArray = 0;
		globals[gi].length = 1;
		globals[gi].isStatic = insP->argc >= 4 && number(insP->args[3], insP->line) != 0;
	}
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if (strcmp(insP->op, "GLOBALDECL") != 0) continue;
		/* The optional third word carries file-static visibility when an IR
		   file was artificially split into several -part inputs. */
		if (insP->argc != 2 && insP->argc != 3) fatal("ungueltiges GLOBALDECL");
		if (findGlobal(insP->args[0]) >= 0) {
			sprintf(msg, "IR Zeile %d: doppelte globale Variable %s", insP->line, insP->args[0]);
			fatal(msg);
		}
		if (globalCount >= MAX_GLOBALS) fatal("zu viele globale Variablen");
		gi = globalCount++;
		memset(&globals[gi], 0, sizeof(Global));
		strncpy(globals[gi].name, insP->args[0], NAME_LEN - 1);
		globals[gi].elemSize = tagSize(insP->args[1]);
		globals[gi].isStatic = insP->argc == 3 && number(insP->args[2], insP->line) != 0;
		globals[gi].declOnly = 1;
	}
}

static void collectFunctions(void) {
	int i, open = 0, seenFunction = 0;
	Function current;
	char msg[300];
	funcCount = 0;
	memset(&current, 0, sizeof(current));
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if (strcmp(insP->op, "GLOBAL") == 0 || strcmp(insP->op, "GARRAY") == 0 || strcmp(insP->op, "GINIT") == 0) {
			/* Allowed before the first function (true globals), inside an open
			   function (static locals), and between functions since 2026-08-10.
			   C permits declarations and functions to be mixed freely; collectGlobals
			   already collects these directives independently of position. */
			if (insP->argc != 1 && insP->argc != 2 && insP->argc != 3 && insP->argc != 4) {
				sprintf(msg, "IR Zeile %d: ungueltiges GLOBAL", insP->line);
				fatal(msg);
			}
		} else if (strcmp(insP->op, "FUNCDECL") == 0 || strcmp(insP->op, "GLOBALDECL") == 0) {
			/* Multi-file translation (2026-07-25): "exists but is not defined here"
			   is allowed outside every FUNC span, like GLOBAL/GARRAY. FUNCDECL is
			   registered in a separate pass because it opens no FUNC/ENDFUNC span. */
		} else if (strcmp(insP->op, "FUNC") == 0) {
			/* Third argument (2026-07-25): optional isstatic flag (name mangling in
			   emitIR; see mangledName()). r68/l68 have no visibility concept. */
			if (open || (insP->argc != 2 && insP->argc != 3)) { sprintf(msg, "IR Zeile %d: ungueltiges FUNC", insP->line); fatal(msg); }
			memset(&current, 0, sizeof(current));
			strncpy(current.name, insP->args[0], NAME_LEN - 1);
			current.nargs = number(insP->args[1], insP->line);
			current.first = i + 1;
			current.last = -1;
			current.isStatic = insP->argc >= 3 && number(insP->args[2], insP->line) != 0;
			open = 1;
			seenFunction = 1;
		} else if (strcmp(insP->op, "ENDFUNC") == 0) {
			if (!open) { sprintf(msg, "IR Zeile %d: ENDFUNC ohne FUNC", insP->line); fatal(msg); }
			current.last = i;
			if (funcCount >= MAX_FUNCS) fatal("zu viele Funktionen");
			funcs[funcCount++] = current;
			open = 0;
		} else if (!open) {
			sprintf(msg, "IR Zeile %d: Opcode ausserhalb einer Funktion", insP->line);
			fatal(msg);
		}
	}
	if (open) fatal("IR: fehlendes ENDFUNC");
	/* -part (2026-07-25, multi-file translation): a file WITHOUT main/functions
	   is valid if it contains global declarations; a completely empty file
	   remains an error. Without -part, the complete-program assumption is
	   unchanged. */
	if (funcCount == 0 && (!partMode || globalCount == 0)) fatal("IR: keine Funktion");
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		int existing;
		if (strcmp(insP->op, "FUNCDECL") != 0) continue;
		/* As with GLOBALDECL, a third optional word retains file-static
		   mangling across artificial IR parts.  Existing two-word IR remains
		   fully compatible. */
		if (insP->argc != 2 && insP->argc != 3) fatal("ungueltiges FUNCDECL");
		existing = findFunction(insP->args[0]);
		if (existing >= 0) {
			/* A forward declaration in the SAME file whose real body was already
			   found elsewhere in the IR is normal for mutually recursive functions
			   (A calls B before B is defined), not a duplicate. It remains an error
			   only when the existing registration is itself declOnly (two FUNCDECL
			   entries for the same name without any real body). */
			if (!funcs[existing].declOnly) continue;
			fprintf(stderr, "qcc_backend: doppelte Funktion %s\n", insP->args[0]); fatal("doppelte Funktion");
		}
		if (funcCount >= MAX_FUNCS) fatal("zu viele Funktionen");
		memset(&current, 0, sizeof(current));
		strncpy(current.name, insP->args[0], NAME_LEN - 1);
		current.nargs = number(insP->args[1], insP->line);
		current.first = -1;
		current.last = -1;
		current.isStatic = insP->argc == 3 && number(insP->args[2], insP->line) != 0;
		current.declOnly = 1;
		funcs[funcCount++] = current;
	}

	for (i = 0; i < funcCount; i++) {
		Function* fn = &funcs[i];
		int highest = fn->nargs - 1;
		int k;
		for (k = fn->first; k < fn->last; k++) {
			Instr* x = &ir[k];
			/* LOADLH/STORELH (2026-09-09, short) MUST be included here. Otherwise a
			   slot accessed ONLY through them remains below "highest", making
			   fn->locals too small. The frame would then be too short and later
			   slots could overwrite one another. */
			if ((strcmp(x->op, "LOADL") == 0 || strcmp(x->op, "STOREL") == 0 || strcmp(x->op, "LOADC") == 0 ||
				strcmp(x->op, "STOREC") == 0 || strcmp(x->op, "LOADLH") == 0 || strcmp(x->op, "STORELH") == 0 ||
				strcmp(x->op, "LOADP") == 0 || strcmp(x->op, "STOREP") == 0 ||
				strcmp(x->op, "ADDRL") == 0 || strcmp(x->op, "LARRAY") == 0) && x->argc > 0) {
				int slotN = number(x->args[0], x->line);
				if (slotN < 0) { sprintf(msg, "IR Zeile %d: negativer lokaler Slot", x->line); fatal(msg); }
				if (slotN > highest) highest = slotN;
			}
		}
		fn->locals = highest >= fn->nargs ? highest - fn->nargs + 1 : 0;
		fn->frameBytes = fn->locals * 4;
		for (k = fn->first; k < fn->last; k++) {
			if (strcmp(ir[k].op, "LARRAY") == 0) {
				Instr* x = &ir[k];
				int len, align;
				if (x->argc != 3) fatal("ungueltiges LARRAY");
				len = number(x->args[2], x->line);
				if (len <= 0) fatal("LARRAY-Laenge muss positiv sein");
				/* Alignment was 1 (byte) or 2 (everything else, including
				   4-byte values) even before short: 68k word and long values need
				   only an even address, not 4-byte alignment. short therefore uses
				   the existing "otherwise" branch; only the actual element size
				   for frameBytes is now selected by tagSize(). */
				align = tagSize(x->args[1]) == 1 ? 1 : 2;
				fn->frameBytes = (fn->frameBytes + align - 1) & ~(align - 1);
				fn->frameBytes += len * tagSize(x->args[1]);
			}
		}
		fn->frameBytes = (fn->frameBytes + 3) & ~3;
	}
}

/* 2026-07-26 (see registerExtern() above): collect, before emission, all raw
   external names called in this file through CALLEXT/CALLEXTP
   (strlen/fopen/printf/...). This must happen BEFORE any code emission so the
   table indices and wrapper emission observe the same order. */
static void collectExterns(void) {
	int i;
	for (i = 0; i < irCount; i++) {
		Instr* insP = &ir[i];
		if ((strcmp(insP->op, "CALLEXT") == 0 || strcmp(insP->op, "CALLEXTP") == 0) && insP->argc == 3) {
			registerExtern(insP->args[0]);
		}
	}
}

static int arrayOffset(const Function* fn, int wanted, int* elemSize, int line) {
	int offset = fn->locals * 4;
	int k;
	char msg[200];
	for (k = fn->first; k < fn->last; k++) {
		if (strcmp(ir[k].op, "LARRAY") == 0) {
			Instr* x = &ir[k];
			int slotN, len, align;
			if (x->argc != 3) fatal("ungueltiges LARRAY");
			slotN = number(x->args[0], x->line);
			len = number(x->args[2], x->line);
			align = tagSize(x->args[1]) == 1 ? 1 : 2;
			offset = (offset + align - 1) & ~(align - 1);
			offset += len * tagSize(x->args[1]);
			if (slotN == wanted) {
				*elemSize = tagSize(x->args[1]);
				return offset;
			}
		}
	}
	sprintf(msg, "IR Zeile %d: unbekanntes lokales Array", line);
	fatal(msg);
	return 0;
}

static void slotAddress(char* out, int slotN, const Function* fn, int line) {
	char msg[200];
	if (slotN < fn->nargs) {
		sprintf(out, "%d(%s)", 8 + 4 * (fn->nargs - 1 - slotN), framePtr());
		return;
	}
	if (slotN >= fn->nargs + fn->locals) {
		sprintf(msg, "IR Zeile %d: Slot ausserhalb des Frames", line);
		fatal(msg);
	}
	sprintf(out, "%d(%s)", -4 * (slotN - fn->nargs + 1), framePtr());
}

static void emitCompare(FILE* out, const char* branch, int* serial) {
	/* tc_cmp_yes_<id>/tc_cmp_done_<id> are purely internal branch labels, NOT
	   QCC-Symbole -- ohne psectName-Suffix kollidieren sie beim Mehrdatei-
	   Link, sobald ZWEI separat kompilierte Dateien beide mindestens einen
	   Vergleichsoperator benutzen (r68/l68 kennen kein Sichtbarkeitskonzept,
	   siehe mangledName()-Kommentar -- id allein ist nur PRO DATEI eindeutig,
	   der serial-Zaehler startet in jeder Datei wieder bei 0). Live gefunden
	   beim ersten echten Zwei-Datei-Link von SourceQCC/ebnf.tc gegen
	   codegen.tc (2026-07-26, writeWorkfile-Chunk), siehe docs/FORTSCHRITT.md.
	   FUNDAMENTALER FUND (2026-07-26, live auf Q9 gefunden -- ALLE Vergleiche
	   waren betroffen, live reproduziert bis in ein winziges Standalone-
	   Programm): "moveq #0,d0" ZWISCHEN "cmp.l" und dem bedingten Branch
	   (frueherer Code hier) ZERSTOERT die von cmp.l gesetzten Flags, BEVOR der
	   Branch sie liest -- MOVEQ setzt selbst N/Z (loescht V/C) basierend auf
	   dem bewegten Wert, und "moveq #0,d0" bewegt IMMER eine 0, setzt also
	   IMMER Z=1. Ergebnis: "beq" (End-Test auf Z=1) sprang IMMER (jeder
	   "=="-Vergleich war IMMER wahr), "bne" sprang NIE (jeder "!="-Vergleich
	   war IMMER falsch) -- UNABHAENGIG von den tatsaechlichen Werten. Nie
	   vorher aufgefallen, weil QCCVM UND tools/qcc68sim.py (unser Test-
	   Simulator) Vergleiche als reinen Werttransport modellieren, NICHT ueber
	   echte CPU-Flags -- der Bug war fuer BEIDE unsichtbar, erst die echte
	   68030-Hardware auf Q9 zeigte ihn. FIX: "moveq #0,d0" NACH den Branch
	   verschoben (in den sonst-Zweig, der NUR erreicht wird, wenn der Branch
	   NICHT genommen wurde) -- die Flags von cmp.l bleiben bis zum Branch
	   selbst unangetastet. */
	int id = (*serial)++;
	/* Branch relaxation belongs to Microware r68 (-m3 -b).  Keeping the
	   source form short lets r68 choose a correct long encoding/trampoline,
	   while hand-emitting .l branches bypassed that relocation machinery. */
	const char* longBranch = "";
	fprintf(out, "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tcmp.l\td1,d0\n");
	/* The false-path skip is always local to this compare and therefore fits
	   the 68000 short branch.  Keeping this BRA short also avoids the Q9
	   assembler/emulator long-BRA corner case; only the conditional branch
	   to the compare arm needs the long form in large functions. */
	fprintf(out, "\t%s%s\ttc_cmp_yes_%d__%s\n\tmoveq\t#0,d0\n\tbra\ttc_cmp_done_%d__%s\n", branch, longBranch, id, psectName, id, psectName);
	fprintf(out, "tc_cmp_yes_%d__%s:\tmoveq\t#1,d0\ntc_cmp_done_%d__%s:\tmove.l\td0,-(a7)\n", id, psectName, id, psectName);
}

// 68000 hat MULS/DIVS nur fuer 16-Bit-Operanden. Diese festen, PIC-faehigen
// Schablonen bilden deshalb die definierte QCC-int32-Arithmetik nach. Sie
// erhalten d2-d5 (ABI-freundlich) und geben ausschliesslich d0 zurueck.
static void emitM68kCore(FILE* out) {
	fprintf(out, "%s 68k-Core: int32 MUL/DIV, keine OS- oder Q9-Abhaengigkeit\n", fullCommentPrefix());
	fputs("tc_mul_i32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n", out);
	fputs("\tmoveq\t#0,d2\n\tmoveq\t#0,d4\n\ttst.l\td0\n\tbpl\ttc_mul_a_pos\n", out);
	fputs("\tneg.l\td0\n\taddq.l\t#1,d4\n", out);
	fputs("tc_mul_a_pos:\ttst.l\td1\n\tbpl\ttc_mul_b_pos\n\tneg.l\td1\n\teori.l\t#1,d4\n", out);
	fputs("tc_mul_b_pos:\tmoveq\t#31,d3\n", out);
	fputs("tc_mul_loop:\tlsr.l\t#1,d1\n\tbcc\ttc_mul_skip\n\tadd.l\td0,d2\n", out);
	fputs("tc_mul_skip:\tadd.l\td0,d0\n\tdbra\td3,tc_mul_loop\n", out);
	fputs("\ttst.l\td4\n\tbeq\ttc_mul_done\n\tneg.l\td2\n", out);
	fputs("tc_mul_done:\tmove.l\td2,d0\n\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);

	fputs("tc_div_i32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n\tmove.l\td5,-(a7)\n", out);
	fputs("\tmoveq\t#0,d5\n\ttst.l\td1\n\tbne\ttc_div_nonzero\n\tmoveq\t#0,d0\n\tbra\ttc_div_done\n", out);
	fputs("tc_div_nonzero:\ttst.l\td0\n\tbpl\ttc_div_a_pos\n\tneg.l\td0\n\taddq.l\t#1,d5\n", out);
	fputs("tc_div_a_pos:\ttst.l\td1\n\tbpl\ttc_div_b_pos\n\tneg.l\td1\n\teori.l\t#1,d5\n", out);
	fputs("tc_div_b_pos:\tmoveq\t#0,d2\n\tmoveq\t#0,d3\n\tmoveq\t#31,d4\n", out);
	fputs("tc_div_loop:\tlsl.l\t#1,d0\n\troxl.l\t#1,d3\n\tlsl.l\t#1,d2\n", out);
	fputs("\tcmp.l\td1,d3\n\tbcs\ttc_div_skip\n\tsub.l\td1,d3\n\taddq.l\t#1,d2\n", out);
	fputs("tc_div_skip:\tdbra\td4,tc_div_loop\n\ttst.l\td5\n\tbeq\ttc_div_result\n\tneg.l\td2\n", out);
	fputs("tc_div_result:\tmove.l\td2,d0\n", out);
	fputs("tc_div_done:\tmove.l\t(a7)+,d5\n\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);

	fputs("tc_udiv_u32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n", out);
	fputs("\ttst.l\td1\n\tbne\ttc_udiv_nonzero\n\tmoveq\t#0,d0\n\tbra\ttc_udiv_done\n", out);
	fputs("tc_udiv_nonzero:\tmoveq\t#0,d2\n\tmoveq\t#0,d3\n\tmoveq\t#31,d4\n", out);
	fputs("tc_udiv_loop:\tlsl.l\t#1,d0\n\troxl.l\t#1,d3\n\tlsl.l\t#1,d2\n", out);
	fputs("\tcmp.l\td1,d3\n\tbcs\ttc_udiv_skip\n\tsub.l\td1,d3\n\taddq.l\t#1,d2\n", out);
	fputs("tc_udiv_skip:\tdbra\td4,tc_udiv_loop\n\tmove.l\td2,d0\n", out);
	fputs("tc_udiv_done:\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);

	/* Remainder = dividend - quotient*divisor. IMPORTANT (2026-07-24, found
	   through tools/qcc68sim.py while debugging the new -os9-putint digit
	   decomposition): before "bsr tc_mul_i32", d0 must contain the DIVISOR (d3),
	   not the dividend (d2) again; otherwise the code computes quotient*dividend
	   instead of quotient*divisor. This bug remained hidden since the introduction
	   of tc_mod_i32/tc_umod_u32 because no 68k backend test (only QCCVM tests)
	   executed the "%" operator through the real 68k path. */
	fputs("tc_mod_i32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_div_i32\n\tmove.l\td0,d1\n\tmove.l\td3,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);
	fputs("tc_umod_u32:\n", out);
	fputs("\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d1\n\tmove.l\td3,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n", out);
}

/* Data-access and pointer opcodes (PUSH..PDIFF), moved out of emitIR() on
	 2026-09-09. With short as a third size (tagSize/tagSuffix/tagShift instead
	 of isByteWord), emitIR()'s own 68k code, which QCC generates during
	 self-hosting, became large enough for qr68 to reject a branch as too far for
	 its word form. This is a real toolchain limit: neither r68 nor qr68 knows a
	 long branch form. Moving this code shortens the branch spans in emitIR()
	 again without changing semantics; it is purely a size reduction. Returns 1
	 when handled, otherwise 0 so emitIR() continues with the remaining dispatch. */
static int emitDataOp(FILE* out, const char* op, Instr* insP, const Function* fn, int* serial) {
	char addrBuf[64];
	char msg[300];
	if (strcmp(op, "PUSH") == 0 && insP->argc == 1) {
		fprintf(out, "\tmove.l\t#%s,-(a7)\n", insP->args[0]);
	} else if (strcmp(op, "LOADL") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmove.l\t%s,-(a7)\n", addrBuf);
	} else if (strcmp(op, "STOREL") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmove.l\t(a7)+,%s\n", addrBuf);
	} else if (strcmp(op, "LOADC") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmoveq\t#0,d0\n\tmove.b\t%s,d0\n\tmove.l\td0,-(a7)\n", addrBuf);
	} else if (strcmp(op, "STOREC") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmove.l\t(a7)+,d0\n\tmove.b\td0,%s\n", addrBuf);
	} else if (strcmp(op, "LOADLH") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmoveq\t#0,d0\n\tmove.w\t%s,d0\n\tmove.l\td0,-(a7)\n", addrBuf);
	} else if (strcmp(op, "STORELH") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmove.l\t(a7)+,d0\n\tmove.w\td0,%s\n", addrBuf);
	} else if (strcmp(op, "LOADP") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmove.l\t%s,-(a7)\n", addrBuf);
	} else if (strcmp(op, "STOREP") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tmove.l\t(a7)+,%s\n", addrBuf);
	} else if (strcmp(op, "ADDRL") == 0 && insP->argc == 1) {
		slotAddress(addrBuf, number(insP->args[0], insP->line), fn, insP->line);
		fprintf(out, "\tlea\t%s,a0\n\tmove.l\ta0,-(a7)\n", addrBuf);
	} else if (strcmp(op, "ADDRG") == 0 && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]);
		if (gidx < 0) fatal("unbekannte globale Variable");
		emitLeaGlobal(out, gidx, "a0");
		fputs("\tmove.l\ta0,-(a7)\n", out);
	} else if (strcmp(op, "LARRAY") == 0 && insP->argc == 3) {
		/* frame layout only, no code */
	} else if (strcmp(op, "PUSHADDR") == 0 && insP->argc == 2) {
		int ignored;
		if (strcmp(insP->args[0], "L") == 0) {
			int slotN = number(insP->args[1], insP->line);
			/* A struct containing exactly one longword (the bootstrap case
			   TCType: four char fields) is passed as an ordinary 32-bit parameter.
			   Its field accesses still use PUSHADDR L <param>, for which the
			   parameter address itself is correct. Therefore, not every L address
			   denotes an LARRAY. Larger struct-by-value parameters still need a
			   dedicated ABI. */
			if (slotN < fn->nargs) {
				slotAddress(addrBuf, slotN, fn, insP->line);
				fprintf(out, "\tlea\t%s,a0\n", addrBuf);
			} else {
				int off = arrayOffset(fn, slotN, &ignored, insP->line);
				fprintf(out, "\tlea\t-%d(%s),a0\n", off, framePtr());
			}
		} else if (strcmp(insP->args[0], "P") == 0) {
			slotAddress(addrBuf, number(insP->args[1], insP->line), fn, insP->line);
			fprintf(out, "\tmove.l\t%s,a0\n", addrBuf);
		} else if (findGlobal(insP->args[1]) >= 0 && strcmp(insP->args[0], "G") == 0) {
			emitLeaGlobal(out, findGlobal(insP->args[1]), "a0");
		} else {
			fatal("unbekanntes Array");
		}
		fputs("\tmove.l\ta0,-(a7)\n", out);
	} else if ((strcmp(op, "LOADIDX") == 0 || strcmp(op, "STOREIDX") == 0 || strcmp(op, "STOREIDXKEEP") == 0) && insP->argc == 3) {
		/* 2026-09-09: isChar (bool) became elemSize (1/2/4), with short
		   added as the third size. */
		int elemSize = tagSize(insP->args[2]);
		int keepValue = strcmp(op, "STOREIDXKEEP") == 0;
		if (strcmp(insP->args[2], "i") != 0 && strcmp(insP->args[2], "p") != 0 &&
		    strcmp(insP->args[2], "h") != 0 && strcmp(insP->args[2], "c") != 0 &&
		    strcmp(insP->args[2], "b") != 0) fatal("unbekannter Arraytyp");
		if (strcmp(op, "STOREIDX") == 0 || keepValue) fputs("\tmove.l\t(a7)+,d0\n", out);
		fputs("\tmove.l\t(a7)+,d1\n", out);
		if (elemSize > 1) fprintf(out, "\tlsl.l\t#%d,d1\n", tagShift(insP->args[2]));
		if (strcmp(insP->args[0], "L") == 0) {
			int off = arrayOffset(fn, number(insP->args[1], insP->line), &elemSize, insP->line);
			fprintf(out, "\tlea\t-%d(%s),a0\n", off, framePtr());
		} else if (strcmp(insP->args[0], "P") == 0) {
			slotAddress(addrBuf, number(insP->args[1], insP->line), fn, insP->line);
			fprintf(out, "\tmove.l\t%s,a0\n", addrBuf);
		} else if (findGlobal(insP->args[1]) >= 0 && strcmp(insP->args[0], "G") == 0) {
			emitLeaGlobal(out, findGlobal(insP->args[1]), "a0");
		} else {
			fatal("unbekanntes Array");
		}
		fputs("\tadd.l\td1,a0\n", out);
		if (strcmp(op, "LOADIDX") == 0) {
			if (elemSize == 4) fputs("\tmove.l\t(a0),d0\n", out);
			else fprintf(out, "\tmoveq\t#0,d0\n\tmove.%c\t(a0),d0\n", tagSuffix(elemSize));
			fputs("\tmove.l\td0,-(a7)\n", out);
		} else {
			if (elemSize == 1 && keepValue) fputs("\tand.l\t#$ff,d0\n", out);
			else if (elemSize == 2 && keepValue) fputs("\tand.l\t#$ffff,d0\n", out);
			fprintf(out, "\tmove.%c\td0,(a0)\n", tagSuffix(elemSize));
			if (keepValue) fputs("\tmove.l\td0,-(a7)\n", out);
		}
	} else if (strcmp(op, "LOADG") == 0 && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
		if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
		mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
		/* small: direct PC-relative value load (short form); large: first obtain
		   the address from the indirection table, then dereference it (see the
		   emitLeaGlobal() comment). */
		if (largeDataMode || globalRemote(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmove.l\t(a0),-(a7)\n", out); }
		else fprintf(out, "\tmove.l\t%s(pc),-(a7)\n", gAsmName);
	} else if (strcmp(op, "STOREG") == 0 && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
		if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
		mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
		fputs("\tmove.l\t(a7)+,d0\n", out);
		emitLeaGlobal(out, gidx, "a0");
		fputs("\tmove.l\td0,(a0)\n", out);
	} else if (strcmp(op, "LOADGC") == 0 && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
		if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
		mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
		if (largeDataMode || globalRemote(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmoveq\t#0,d0\n\tmove.b\t(a0),d0\n\tmove.l\td0,-(a7)\n", out); }
		else fprintf(out, "\tmoveq\t#0,d0\n\tmove.b\t%s(pc),d0\n\tmove.l\td0,-(a7)\n", gAsmName);
	} else if (strcmp(op, "STOREGC") == 0 && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
		if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
		mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
		fputs("\tmove.l\t(a7)+,d0\n", out);
		emitLeaGlobal(out, gidx, "a0");
		fputs("\tmove.b\td0,(a0)\n", out);
	} else if (strcmp(op, "LOADGH") == 0 && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
		if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
		mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
		if (largeDataMode || globalRemote(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmoveq\t#0,d0\n\tmove.w\t(a0),d0\n\tmove.l\td0,-(a7)\n", out); }
		else fprintf(out, "\tmoveq\t#0,d0\n\tmove.w\t%s(pc),d0\n\tmove.l\td0,-(a7)\n", gAsmName);
	} else if (strcmp(op, "STOREGH") == 0 && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
		if (gidx < 0) { sprintf(msg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(msg); }
		mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
		fputs("\tmove.l\t(a7)+,d0\n", out);
		emitLeaGlobal(out, gidx, "a0");
		fputs("\tmove.w\td0,(a0)\n", out);
	} else if ((strcmp(op, "LOADGP") == 0 || strcmp(op, "STOREGP") == 0) && insP->argc == 1) {
		int gidx = findGlobal(insP->args[0]); char gAsmName[NAME_LEN + 40];
		if (gidx < 0) fatal("unbekannte globale Variable");
		mangledName(gAsmName, "tc_g_", insP->args[0], globals[gidx].isStatic);
		if (strcmp(op, "LOADGP") == 0) {
			if (largeDataMode || globalRemote(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmove.l\t(a0),-(a7)\n", out); }
			else fprintf(out, "\tmove.l\t%s(pc),-(a7)\n", gAsmName);
		} else {
			fputs("\tmove.l\t(a7)+,d0\n", out);
			emitLeaGlobal(out, gidx, "a0");
			fputs("\tmove.l\td0,(a0)\n", out);
		}
	} else if (strcmp(op, "PTRINDEX") == 0 && insP->argc == 1) {
		fputs("\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n", out);
		if (tagSize(insP->args[0]) > 1) fprintf(out, "\tlsl.l\t#%d,d0\n", tagShift(insP->args[0]));
		fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
	} else if ((strcmp(op, "LOADIND") == 0 || strcmp(op, "STOREIND") == 0 || strcmp(op, "STOREINDKEEP") == 0) && insP->argc == 1) {
		/* 2026-09-09: byte flag (bool) -> elemSize (1/2/4). */
		int elemSize = tagSize(insP->args[0]);
		int keepValue = strcmp(op, "STOREINDKEEP") == 0;
		if (strcmp(op, "LOADIND") == 0) {
			fputs("\tmove.l\t(a7)+,a0\n", out);
			if (elemSize == 4) fputs("\tmove.l\t(a0),d0\n", out);
			else fprintf(out, "\tmoveq\t#0,d0\n\tmove.%c\t(a0),d0\n", tagSuffix(elemSize));
			fputs("\tmove.l\td0,-(a7)\n", out);
		} else {
			fprintf(out, "\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,a0\n\tmove.%c\td0,(a0)\n", tagSuffix(elemSize));
			if (elemSize == 1 && keepValue) fputs("\tand.l\t#$ff,d0\n", out);
			else if (elemSize == 2 && keepValue) fputs("\tand.l\t#$ffff,d0\n", out);
			if (keepValue) fputs("\tmove.l\td0,-(a7)\n", out);
		}
	} else if ((strcmp(op, "PADD") == 0 || strcmp(op, "PSUB") == 0) && insP->argc == 1) {
		fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,a0\n", out);
		if (tagSize(insP->args[0]) > 1) fprintf(out, "\tlsl.l\t#%d,d0\n", tagShift(insP->args[0]));
		if (strcmp(op, "PSUB") == 0) fputs("\tneg.l\td0\n", out);
		fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
	} else if (strcmp(op, "IPADD") == 0 && insP->argc == 1) {
		fputs("\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n", out);
		if (tagSize(insP->args[0]) > 1) fprintf(out, "\tlsl.l\t#%d,d0\n", tagShift(insP->args[0]));
		fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
	} else if (strcmp(op, "IPADDN") == 0 && insP->argc == 1) {
		/* Like IPADD, but scale by a RUNTIME byte size (for example structByteSize)
		   instead of a fixed type-tag size. No lsl.l is possible because the size
		   is arbitrary rather than limited to 1/2/4; use tc_mul_i32 instead (see
		   emitM68kCore). a0 (the pointer) remains untouched across bsr because
		   tc_mul_i32 uses only d0-d4. */
		fprintf(out, "\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n\tmove.l\t#%s,d1\n", insP->args[0]);
		emitCall(out, "tc_mul_i32", helperTableOffset("tc_mul_i32"), serial, psectName);
		fputs("\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n", out);
	} else if (strcmp(op, "PDIFF") == 0 && insP->argc == 1) {
		fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tsub.l\td1,d0\n", out);
		if (tagSize(insP->args[0]) > 1) fprintf(out, "\tasr.l\t#%d,d0\n", tagShift(insP->args[0]));
		fputs("\tmove.l\td0,-(a7)\n", out);
	} else return 0;
	return 1;
}

static void emitIR(FILE* out) {
	int serial = 0;
	int fi, k;
	char msg[300];
	char addrBuf[64];
	int order[MAX_FUNCS];
	int oi;
	int gi;

	/* -part (2026-07-25): main may reside in another file of a multi-file
	   program; the real linker (l68) reports an error if none of the linked
	   files provides it. */
	if (!partMode && findFunction("main") < 0) fatal("IR: Funktion main fehlt");

	fprintf(out, "%s QCC 68k backend -- PIC single module, generated from stack IR\n", fullCommentPrefix());
	fprintf(out, "%s a7: operand stack, %s: current frame, d0/d1: scratch/return value\n\n", fullCommentPrefix(), framePtr());
	if (os9Mode) {
		fprintf(out, "\tnam\t%s\n", psectName);
		fprintf(out, "\tpsect\t%s,0,0,%d,0,0\n", psectName, trampolineMode ? 0 : 1);
		if (trampolineMode)
			/* r68 requires an explicit register operand for -j. The register
			   value is used only during assembly; l68 -a creates the actual
			   OS-9-relocatable jump table. */
			fputs("\tspanreg\td7\n", out);
		fputs("\n", out);
		if (trampolineMode) {
			/* l68 -a places its jump table in the initialized data area. An
			   explicit vsect anchor ensures that cstart establishes an OS-9
			   data base in A6 for that area. */
			fputs("\tvsect\n\tds.b\t1\n\tends\n\n", out);
		}
		fprintf(out, "%s No private tc_start bootstrap here: cstart.r (the real Microware\n", fullCommentPrefix());
		fprintf(out, "%s C runtime) calls \"main\" directly and handles termination itself;\n", fullCommentPrefix());
		fprintf(out, "%s this leaves a6 untouched as its static data pointer (see the\n", fullCommentPrefix());
		fprintf(out, "%s framePtr() comment above in the source).\n\n", fullCommentPrefix());
	} else {
		int mainIdx = findFunction("main");
		fputs("tc_start:\n", out);
		if (largeDataMode) fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n", psectName);
			if (largeDataMode) fprintf(out, "\tlea\ttc_gadata__%s(pc),a3\n", psectName);
		if (mainIdx >= 0) {
			char mainAsmName[NAME_LEN + 40];
			mangledName(mainAsmName, "tc_", "main", funcs[mainIdx].isStatic);
				emitCall(out, mainAsmName, 8 * 4 + externCount * 4 + mainIdx * 4, &serial, psectName);
		} else {
			fputs("\tbsr\ttc_main\n", out); /* main is in another file; see -part above */
		}
		fputs("\tbra\ttc_exit\n\n", out);
	}
	if (largeDataMode) {
		/* Function indirection table (see the emitCall() comment): it MUST follow
		   tc_start/main directly, BEFORE potentially huge function bodies, so the
		   single "lea tc_functab(pc),a4" remains reachable regardless of program
		   size. The order MUST exactly match funcIndex*4 for QCC functions and
		   helperTableOffset() for runtime helpers. */
		/* IMPORTANT (2026-07-26, found live on Q9 -- an actual PMMU crash at the
		   first function call in main()): "dc.l <label>" is NOT an automatically
		   relocated absolute address on OS-9. According to the OS-9 for 68K
		   Processors Technical Manual, an assembler programmer must avoid absolute
		   addressing modes; the only built-in loader
		   relocation mechanism (M$IRefs/F$Fork) applies only to compiler-generated
		   initialized pointer variables in vsects (a dedicated raw
		   MS-Word/Count/LS-Word table format), not to arbitrary "dc.l label" in a
		   psect. l68 resolves such a "dc.l label" only as a psect-internal offset
		   (valid for an assumed load address of 0), NOT as a real runtime address.
		   Therefore, table entries are now link-time CONSTANT differences from the
		   table base ("label-tab"). l68 computes these entirely within the psect,
		   so no runtime relocation is needed because both labels have a fixed
		   relationship in the same psect. emitCall() adds the table base (a4),
		   already loaded correctly with "lea (pc)", to this offset BEFORE jumping. */
		fprintf(out, "%s Function indirection table (-largedata): link-time offsets relative to the table base (see emitCall())\n", fullCommentPrefix());
		emitAlign(out);
		fprintf(out, "tc_functab__%s:\n", psectName);
		fprintf(out, "\tdc.l\ttc_mul_i32-tc_functab__%s\n\tdc.l\ttc_div_i32-tc_functab__%s\n\tdc.l\ttc_udiv_u32-tc_functab__%s\n",
			psectName, psectName, psectName);
		fprintf(out, "\tdc.l\ttc_mod_i32-tc_functab__%s\n\tdc.l\ttc_umod_u32-tc_functab__%s\n", psectName, psectName);
		fprintf(out, "\tdc.l\ttc_putint-tc_functab__%s\n\tdc.l\ttc_putuint-tc_functab__%s\n\tdc.l\ttc_putchar-tc_functab__%s\n",
			psectName, psectName, psectName);
		/* 2026-07-26 (see the registerExtern() comment): external CALLEXT/CALLEXTP
		   targets do NOT get a direct table entry for the raw external name. That
		   name would be outside this psect, so "label-tab" would no longer be a
		   link-time constant within THIS psect. The actual reason for this table
		   is to refresh a3/a4 AFTER an external call (see emitCallExtWrapper()),
		   so entries point to the WRAPPER stub directly below instead. */
		for (fi = 0; fi < externCount; fi++) {
			fprintf(out, "\tdc.l\ttc_extwrap_%s__%s-tc_functab__%s\n", externNames[fi], psectName, psectName);
		}
		for (fi = 0; fi < funcCount; fi++) {
			char asmName[NAME_LEN + 40];
			mangledName(asmName, "tc_", funcs[fi].name, funcs[fi].isStatic);
			fprintf(out, "\tdc.l\t%s-tc_functab__%s\n", asmName, psectName);
		}
		/* Data indirection table (see the emitLeaGlobal() comment): like
		   tc_functab direkt nach tc_start/main stehen (VOR den potenziell
		   riesigen Funktionsrumpf-Texten), damit das einmalige
		   "lea tc_gadata(pc),a3" immer erreichbar bleibt, egal wie gross der
		   Rest des Programms wird. Reihenfolge MUSS exakt zu gidx*4 (siehe
		   findGlobal()) passen -- AUCH declOnly-Externe bekommen einen
		   Eintrag ("dc.l tc_g_X" ist fuer diese eine ganz normale externe
		   Symbolreferenz, von l68 wie jede andere aufgeloest). IMMER emittiert
		   (nicht nur bei globalCount > 0): der ZUSAETZLICHE Eintrag am Ende
		   (Offset globalCount*4, siehe extcallTmpTableOffset()) fuer
		   tc_extcall_tmp wird UNABHAENGIG von echten Globalen gebraucht, sobald
		   irgendein externer Aufruf mit Stack-Argumenten vorkommt -- dasselbe
		   Skalierungsproblem wie bei echten Globalen (PC-relatives
		   "lea tc_extcall_tmp(pc),a0" direkt an der Aufrufstelle wuerde brechen,
		   sobald der Abstand zum spaet liegenden Scratch-Puffer >32 KB wird). */
		/* WICHTIG (2026-07-26, siehe tc_functab-Kommentar oben): auch hier
		   Link-Zeit-Offsets relativ zur Tabellenbasis statt roher "dc.l label"
		   -- exakt dieselbe OS-9-Positionsunabhaengigkeits-Anforderung betrifft
		   Globalzugriffe genauso wie Funktionsaufrufe. emitLeaGlobal() addiert
		   die Tabellenbasis (a3) auf diesen Offset. */
		fprintf(out, "%s Daten-Indirektionstabelle (-largedata): Link-Zeit-Offsets relativ zur Tabellenbasis (siehe emitLeaGlobal())\n", fullCommentPrefix());
		emitAlign(out);
		fprintf(out, "tc_gadata__%s:\n", psectName);
		for (gi = 0; gi < globalCount; gi++) {
			char gAsmName[NAME_LEN + 40];
			mangledName(gAsmName, "tc_g_", globals[gi].name, globals[gi].isStatic);
			/* A global in the remote vsect is not reachable through THIS table:
			   its symbol value is an offset in the data area, while this table holds
			   psect offsets; subtracting them would mix two different bases. The
			   slot remains occupied so gidx*4 and the final tc_extcall_tmp entry
			   (globalCount*4) remain correct. */
			if (globalRemote(gi)) fprintf(out, "\tdc.l\t0\n");
			else fprintf(out, "\tdc.l\t%s-tc_gadata__%s\n", gAsmName, psectName);
		}
		fprintf(out, "\tdc.l\ttc_extcall_tmp-tc_gadata__%s\n", psectName);
		/* 2026-07-26 (see the registerExtern() comment above): one wrapper stub per
		   externer Funktion, DIREKT hier (nah an tc_functab/tc_gadata, also immer
		   PC-relativ sicher erreichbar) platziert. "jsr (a2)" (von der Aufrufstelle,
		   ueber den a4-Tabellenmechanismus) hat bereits EINE Ruecksprungadresse auf
		   a7 gepusht -- die wird zuerst nach d7 (freies Scratch-Register)
		   herausgeholt, DAMIT "bsr rawname" exakt dieselbe Stack-Position fuer
		   seine EIGENE Ruecksprungadresse UND fuer eventuelle Stack-Argumente
		   sieht, die die Aufrufstelle VOR dem jsr bereits gepusht hat (ohne diesen
		   Zwischenschritt saehe die externe Funktion ihre eigenen Stack-Argumente
		   um vier Byte verschoben -- durch die zusaetzliche jsr-Ruecksprungadresse
		   des Wrappers). Nach der Rueckkehr von rawname (d0 traegt den
		   Rueckgabewert, bleibt unangetastet) a3/a4 auffrischen (siehe
		   emitCall()/emitLeaGlobal()-Kommentar), dann die urspruengliche
		   Ruecksprungadresse aus d7 zurueckpushen und rts -- geht exakt zur
		   Aufrufstelle zurueck, als waere direkt "bsr rawname" aufgerufen worden,
		   nur mit aufgefrischtem a3/a4. */
		for (fi = 0; fi < externCount; fi++) {
			fprintf(out, "tc_extwrap_%s__%s:\n", externNames[fi], psectName);
			fprintf(out, "\tmove.l\t(a7)+,d7\n");
			fprintf(out, "\t%s\t%s\n", os9Mode ? "bsr" : "jsr", externNames[fi]);
			fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n\tlea\ttc_gadata__%s(pc),a3\n", psectName, psectName);
			fprintf(out, "\tmove.l\td7,-(a7)\n\trts\n");
		}
	}

	/* 2026-07-26, found live on Q9: emitM68kCore()/tc_putint/tc_putuint/
	   tc_putchar/tc_io_write MUESSEN (wie tc_functab/tc_gadata/die Extern-
	   Wrapper-Stubs oben) NAH BEIEINANDER UND NAH AN DEN TABELLEN liegen --
	   vorher stand dieser ganze Block NACH der kompletten Funktionsrumpf-
	   Schleife weiter unten, was bei einem grossen Programm (z.B. ebnf.tc
	   allein, >17000 Zeilen generierter Assembler) "value out of range" fuer
	   das "lea tc_functab/tc_gadata(pc)" INNERHALB von tc_io_write ausloeste
	   (echter r68-Assemblierungsfehler, live reproduziert) -- tc_io_write lag
	   dann selbst weit ausserhalb der 32-KB-PC-relativ-Reichweite zu den
	   Tabellen. Deshalb JETZT hier (VOR der Funktionsrumpf-Schleife) statt
	   danach emittiert -- inhaltlich unveraendert, nur die Position im
	   erzeugten Assemblertext verschoben (keine der INTERNEN "bsr"-Distanzen
	   innerhalb dieses Blocks aendert sich dadurch, nur seine absolute
	   Position im Gesamttext). Mehrdatei-Uebersetzung (2026-07-25): der
	   gemeinsame 68k-Core/I/O-Anker (siehe partMode/runtimeMode-Kommentar
	   oben) wird unter -part NUR in GENAU EINER Datei emittiert (-runtime) --
	   sonst meldet l68 fuer JEDES dieser Symbole "duplicate symbol", da jede
	   Datei sonst ihre eigene Kopie mitbraechte. Ohne -part unveraendert
	   immer emittiert (Vollprogramm). */
	if (!partMode || runtimeMode) {
	emitM68kCore(out);
	if (os9Mode) {
		/* Real output through the Microware clib.l function _os_write
		   (Signatur laut OS9/SRC/DEFS/modes.h: error_code _os_write(path_id,
		   const void*, u_int32 *count) -- count ist ein IN/OUT-Zeiger, path 1
		   = stdout, analog zu Unix-Filedeskriptoren). BEWUSST nicht ueber
		   printf/clib-Formatierung: QCC hat noch keine String-Literale, und
		   die direkte Ganzzahl->ASCII-Umwandlung hier (analog zu
		   runtime/arm64_darwin/start.s) haelt das gelinkte Programm klein --
		   kein printf-Formatstring-Parser wird ueberhaupt erst hereingezogen.
		   Ziffernzerlegung nutzt die BEREITS VORHANDENEN tc_udiv_u32/
		   tc_umod_u32-Routinen aus emitM68kCore (kein neuer Opcode). d2/d3/d4
		   ueberleben den bsr in diese Routinen unveraendert, da beide selbst
		   d2-d4 sichern/wiederherstellen (siehe deren Definition oben) --
		   deshalb hier KEIN eigenes Push/Pop noetig. */
		fprintf(out, "tc_putint:\n\tlink\t%s,#0\n", framePtr());
		fputs("\tmove.l\td0,d2\n\tmoveq\t#0,d3\n\ttst.l\td2\n\tbge\ttc_pi_nonneg\n", out);
		fputs("\tneg.l\td2\n\tmoveq\t#1,d3\n", out);
		fputs("tc_pi_nonneg:\tlea\ttc_io_buf+11(pc),a1\n\tmove.b\t#13,(a1)\n", out);
		fputs("tc_pi_loop:\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d4\n", out);
		fputs("\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_umod_u32\n", out);
		fputs("\taddi.b\t#48,d0\n\tsubq.l\t#1,a1\n\tmove.b\td0,(a1)\n", out);
		fputs("\tmove.l\td4,d2\n\ttst.l\td2\n\tbne\ttc_pi_loop\n", out);
		fputs("\ttst.l\td3\n\tbeq\ttc_pi_go\n\tsubq.l\t#1,a1\n\tmove.b\t#45,(a1)\n", out);
		fputs("tc_pi_go:\tlea\ttc_io_buf+12(pc),a2\n\tmove.l\ta2,d1\n\tsub.l\ta1,d1\n\tbsr\ttc_io_write\n", out);
		fprintf(out, "\tunlk\t%s\n\trts\n\n", framePtr());

		fprintf(out, "tc_putuint:\n\tlink\t%s,#0\n", framePtr());
		fputs("\tmove.l\td0,d2\n\tlea\ttc_io_buf+11(pc),a1\n\tmove.b\t#13,(a1)\n", out);
		fputs("tc_pu_loop:\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d4\n", out);
		fputs("\tmove.l\td2,d0\n\tmoveq\t#10,d1\n\tbsr\ttc_umod_u32\n", out);
		fputs("\taddi.b\t#48,d0\n\tsubq.l\t#1,a1\n\tmove.b\td0,(a1)\n", out);
		fputs("\tmove.l\td4,d2\n\ttst.l\td2\n\tbne\ttc_pu_loop\n", out);
		fputs("\tlea\ttc_io_buf+12(pc),a2\n\tmove.l\ta2,d1\n\tsub.l\ta1,d1\n\tbsr\ttc_io_write\n", out);
		fprintf(out, "\tunlk\t%s\n\trts\n\n", framePtr());

		fprintf(out, "tc_putchar:\n\tlink\t%s,#0\n", framePtr());
		fputs("\tlea\ttc_io_buf(pc),a1\n\tmove.b\td0,(a1)\n\tmoveq\t#1,d1\n\tbsr\ttc_io_write\n", out);
		fprintf(out, "\tunlk\t%s\n\trts\n\n", framePtr());

		/* a1=buffer, d1=length -- calls _os_write(1,a1,&tc_io_cnt).
		   2026-07-26, live auf Q9 gefunden (siehe emitLeaGlobal()/CALLEXT-
		   Kommentar): "bsr _os_write" ist ein ECHTER externer Aufruf wie jeder
		   CALLEXT, zerstoert also genauso a3/a4 (reine ABI-Temporaer-Register).
		   Dieser Aufruf hier ist aber FEST verdrahtet (nicht ueber die generische
		   CALLEXT-IR-Behandlung, siehe registerExtern()-Kommentar), muss daher
		   SEPARAT geschuetzt werden -- sonst korrumpiert JEDER putint/putuint/
		   putchar-Aufruf a3/a4 fuer den Rest des Programms (live reproduziert:
		   eigene Debug-putchar/putint-Aufrufe waehrend der Fehlersuche
		   korrumpierten a3/a4 und verfaelschten genau die Werte, die beobachtet
		   werden sollten). Auffrischung NUR bei largeDataMode noetig (ohne
		   -largedata gibt es keine a3/a4-Tabellenbasis) -- UND jetzt, da dieser
		   ganze Block nah an den Tabellen liegt, ist auch das "lea (pc)" hier
		   selbst immer sicher erreichbar. */
		fputs("tc_io_write:\n\tlea\ttc_io_cnt(pc),a2\n\tmove.l\td1,(a2)\n\tmove.l\ta1,d1\n", out);
		fputs("\tmove.l\ta2,-(a7)\n\tmoveq\t#1,d0\n", out);
		fputs("\tbsr\t_os_write\n", out);
		fputs("\tlea\t4(a7),a7\n", out);
		if (largeDataMode) fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n\tlea\ttc_gadata__%s(pc),a3\n", psectName, psectName);
		fputs("\trts\n\n", out);
	} else {
		// Target runtime stubs: replaceable; no absolute access, so they remain PIC-friendly.
		fputs("tc_putint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
		fputs("tc_putuint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
		fputs("tc_putchar:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n", out);
		fputs("tc_exit:\trts\t; Target Runtime beendet den Prozess\n", out);
	}
	/* Scratch field for CALLEXT/CALLEXTP (see above): up to 8 stack-passed
	   arguments of an external call. Always declared (32 bytes), regardless of
	   whether the program actually uses CALLEXT. vasm supports
	   "ds.l" (reservierter, uninitialisierter Speicher); der echte Microware-
	   r68-Assembler kennt "ds.l" NICHT (empirisch verifiziert: "bad mnemonic"),
	   daher im os9-Modus stattdessen 8x "dc.l 0" (funktional gleichwertig: alle
	   Backend-Opcodes lesen den Wert erst NACH einem STORE hierher). */
	emitAlign(out);
	fputs(os9Mode ? "tc_extcall_tmp:\tdc.l\t0,0,0,0,0,0,0,0\n" : "tc_extcall_tmp:\tds.l\t8\n", out);
	if (os9Mode) {
		/* tc_io_buf: digit buffer for tc_putint/tc_putuint (maximum
		   "-2147483648\r" = 12 bytes, filled backwards) and single-byte buffer
		   for tc_putchar (uses only the first byte). tc_io_cnt is the IN/OUT count
		   cell for the real _os_write call (see tc_io_write above). */
		fputs("tc_io_buf:\tdc.l\t0,0,0\n", out);
		fputs("tc_io_cnt:\tdc.l\t0\n", out);
	}
	} /* !partMode || runtimeMode */

	/* os9Mode + largeDataMode: main is the only entry point (no private
	   eigener tc_start) und muss dort "lea tc_functab(pc),a4" ausfuehren --
	   diese lea ist selbst PC-relativ und daher nur gueltig, wenn main DIREKT
	   nach der Tabelle liegt. main kann aber an beliebiger Stelle in funcs[]
	   registriert sein -- steht main nicht zuerst im Quelltext, koennen
	   riesige Funktionsrumpf-Texte ZWISCHEN Tabelle und main landen (genau
	   das brach beim 150-Funktionen-Skalierungstest fuer diese Erweiterung:
	   main stand am Ende, >32 KB entfernt, "value out of range" bei echtem
	   r68). Deshalb wird main hier -- NUR bei os9+largedata -- unabhaengig
	   von ihrer Position in funcs[] als allererste Funktion emittiert. Die
	   Funktionsindirektionstabelle selbst bleibt in Registrierungsreihenfolge
	   (ihre Eintraege sind absolute, vom Linker aufgeloeste Adressen und
	   haengen nicht von der Emissionsreihenfolge ab). */
	if (os9Mode && largeDataMode) {
		int mainIdx = findFunction("main");
		oi = 0;
		if (mainIdx >= 0) order[oi++] = mainIdx;
		for (fi = 0; fi < funcCount; fi++) if (fi != mainIdx) order[oi++] = fi;
	} else {
		for (fi = 0; fi < funcCount; fi++) order[fi] = fi;
	}
	for (oi = 0; oi < funcCount; oi++) {
		fi = order[oi];
		Function* fn = &funcs[fi];
		char asmName[NAME_LEN + 40];
		if (fn->declOnly) continue; /* defined in ANOTHER file; no body here */
		if (os9Mode && strcmp(fn->name, "main") == 0) {
			fputs("main:\n", out);
			/* cstart.r calls the exported entry point according to the Microware C ABI:
			   auf: argc in d0, argv in d1. QCC-interne CALLs verwenden dagegen
			   ausschliesslich den Operand-Stack (erstes Argument weiter oben).
			   Ein parameterloses main braucht keinen Adapter; bei main(argc,argv)
			   beziehungsweise jeder von QCC akzeptierten parameterbehafteten Form
			   macht dieser kurze Stub die beiden Welten kompatibel. */
			if (fn->nargs > 0) {
				if (fn->nargs >= 1) fputs("\tmove.l\td0,-(a7)\n", out);
				if (fn->nargs >= 2) fputs("\tmove.l\td1,-(a7)\n", out);
				if (fn->nargs > 2) {
					/* Cstart can provide only argc/argv. Additional parameters deliberately
					   remain zero instead of coming from undefined register contents. */
					int mi;
					for (mi = 2; mi < fn->nargs; mi++) fputs("\tmoveq\t#0,d0\n\tmove.l\td0,-(a7)\n", out);
				}
				fputs("\tbsr\ttc_main\n", out);
				fprintf(out, "\tlea\t%d(a7),a7\n\trts\n", fn->nargs * 4);
			}
		}
		mangledName(asmName, "tc_", fn->name, fn->isStatic);
		fprintf(out, "%s:\tlink\t%s,#%d\n", asmName, framePtr(), -fn->frameBytes);
		if (largeDataMode && os9Mode && strcmp(fn->name, "main") == 0) {
			/* OS-9 cstart enters main directly; establish the one shared table
			   basis before any QCC-internal call is made. */
			fprintf(out, "\tlea\ttc_functab__%s(pc),a4\n\tlea\ttc_gadata__%s(pc),a3\n", psectName, psectName);
		}
		if (largeDataMode) {
			/* Every function must establish its own table bases on entry. The caller
			   may come from another psect, and a3/a4 are ABI scratch registers. The
			   function's own label remains PC-relative and reachable; the two link-time
			   differences have no 16-bit PC-relative limit. */
			emitTableBases(out, asmName, psectName);
		}
		/* WICHTIG (2026-07-26, live auf Q9 gefunden -- vierter, tiefster
		   -largedata-Bug dieser Sitzung): a3/a4 werden bisher NUR beim
		   Programmstart (main:) einmalig gesetzt UND nach jedem CALLEXT/
		   CALLEXTP aufgefrischt (siehe emitCall()/emitLeaGlobal()-Kommentar)
		   -- das reicht NICHT, sobald eine Funktion PER FUNCDECL AUS EINER
		   ANDEREN DATEI aufgerufen wird (Mehrdatei-Uebersetzung, jede Datei
		   hat ihre EIGENE tc_functab/tc_gadata)! Live reproduziert: ruft
		   Datei A eine in Datei B definierte Funktion auf (a4 zeigt zu
		   diesem Zeitpunkt noch auf DATEI A's Tabelle, vom Aufrufer
		   gesetzt), und DIESE Funktion ruft INTERN eine dritte Funktion
		   (z.B. einen Laufzeit-Helfer wie tc_putint) per emitCall() auf, so
		   verwendet dieser interne Aufruf FAELSCHLICH weiterhin Datei A's
		   Tabelle (a4 wurde nie auf Datei B's EIGENE Tabelle umgestellt) --
		   der Tabellenoffset selbst ist korrekt (verifiziert), aber er zeigt
		   in die FALSCHE Tabelle, ruft also eine VOELLIG ANDERE Funktion an
		   derselben Indexposition auf. Symptom im minimalen Reproduktionsfall:
		   ein Aufruf zu "putint(99)" in einer cross-file Funktion rief
		   stattdessen lautlos "tc_div_i32" auf (kein Absturz, aber keine
		   Ausgabe) -- im echten, groesseren ebnf.tc+codegen.tc-Programm mit
		   VIEL laengeren, unterschiedlich sortierten Tabellen fuehrt derselbe
		   Mechanismus zum beobachteten PMMU-Absturz (falscher Tabelleneintrag
		   zeigt auf Datenmuell, der als Adresse interpretiert wird). FIX:
		   JEDE Funktion (nicht nur main) frischt a3/a4 auf IHRE EIGENE Tabelle
		   auf, GLEICH NACH dem eigenen "link" -- unabhaengig davon, ob sie
		   aus derselben oder einer anderen Datei aufgerufen wurde. Kostet
		   zwei zusaetzliche Instruktionen pro Funktionsaufruf (ueberschaubarer
		   Overhead), garantiert aber Korrektheit unabhaengig vom Aufrufer.
		   main() selbst behaelt sein bereits vorhandenes Refresh VOR dem
		   eigenen Label (siehe oben) -- das hier ist zusaetzlich, harmlos
		   redundant fuer main, aber noetig fuer ALLE anderen Funktionen.
		   ZWEITER FUND (direkt im Anschluss, live auf Q9): ein simples
		   "lea tc_functab(pc),a4" HIER (an JEDER Funktion, potenziell weit
		   von der eigenen Tabelle entfernt in einer grossen Datei) sprengt
		   sofort wieder die 16-Bit-PC-relativ-Grenze ("value out of range"
		   bei echtem r68, live reproduziert an writeWorkfile()) -- GENAU das
		   Problem, das die ganze a3/a4-Indirektion ja eigentlich umgehen
		   sollte. RICHTIGER FIX (verifiziert per direkter Byte-Analyse eines
		   Minimaltests mit 20000 nop dazwischen, sowohl r68-Assemblierung ALS
		   AUCH die erzeugten Bytes bestaetigt korrekt): PC-relative
		   Adressierung selbst hat KEINE Moeglichkeit, weiter als 32 KB zu
		   reichen -- das ist eine echte 68000-Hardwaregrenze, keine
		   Syntaxfrage. Aber eine Funktion kann IMMER sicher (Distanz 0) ihre
		   EIGENE Adresse per "lea <eigenerName>(pc),aX" laden (bezieht sich
		   auf sich selbst!), und DANACH per "adda.l #(ziel-eigenerName),aX"
		   eine LINK-ZEIT-KONSTANTE Differenz addieren -- diese Differenz ist
		   ein reiner arithmetischer 32-Bit-Immediate-Wert OHNE jede
		   Distanzbeschraenkung (nur "adda.l"/"add.l #imm32,Dn" selbst hat
		   keine PC-relativ-Grenze, im Gegensatz zu "d(pc)"-Adressierungs-
		   arten). So kann JEDE Funktion, egal wie weit von ihrer eigenen
		   Tabelle entfernt, diese trotzdem sicher erreichen. */
		/* a3/a4 werden am Programmeinstieg gesetzt und von internen QCC-
		   Funktionen nicht veraendert. Externe Aufrufe nutzen Wrapper, die
		   diese ABI-Temporaerregister wiederherstellen. */
		/* BIG-ENDIAN-KORREKTUR FUER char-PARAMETER (2026-08-10, live am
		   selbstgehosteten EBNF-Generator gefunden). Der Aufrufer legt JEDES
		   Argument als volles 32-Bit-Langwort ab ("move.l #wert,-(a7)", siehe
		   PUSH/emitCall) -- der Bytewert eines char-Parameters steht damit im
		   NIEDERWERTIGSTEN Byte des Slots, auf dem Big-Endian-68k also bei
		   Slot+3. LOADC/STOREC/ADDRL adressieren aber die Slot-BASIS (bei
		   LOKALEN Slots ist das korrekt und in sich konsistent, weil dort
		   STOREC und LOADC dieselbe Adresse benutzen) und lasen deshalb das
		   HOECHSTWERTIGE Byte -- fuer jeden ASCII-Wert konstant 0.
		   SYMPTOM: der von QCC uebersetzte Generator erzeugte fuer JEDEN
		   Zeichenbereich der Grammatik ("0"~"9", "A"~"F", "a"~"z", "A"~"Z")
		   die Grenzen 0x00/0x00 statt 0x30/0x39 usw. -- astPushRNG(char lo,
		   char hi) bekam beide Grenzen als 0. Sonst war die Ausgabe mit der
		   xcc-gebauten Referenz bitgleich.
		   WARUM BISHER UNENTDECKT: ARM64 ist Little-Endian (dort liegt das
		   niederwertige Byte ZUFAELLIG an der Slot-Basis, der Code war also
		   versehentlich richtig) und QCCVM haelt typisierte Slots statt roher
		   Stack-Langworte -- beide Referenzpfade konnten den Fehler prinzipiell
		   nicht zeigen. Nur echtes 68k-Big-Endian ist betroffen.
		   FIX: einmalig im Prolog das niederwertige Byte an die Slot-Basis
		   kopieren. Danach stimmen ALLE bestehenden Byte-Zugriffspfade
		   (LOADC, STOREC sowie ADDRL+LOADIND/STOREIND) unveraendert ueberein --
		   exakt wie bei lokalen char-Slots, kein Eingriff an den Opcodes noetig.
		   Betroffen sind nur Parameter, die im Rumpf TATSAECHLICH byteweise
		   benutzt werden (LOADC/STOREC auf ihrem Slot) -- das ist der
		   eindeutige Beleg, dass es ein char-Parameter ist; int- und
		   Pointer-Parameter bleiben unangetastet.
		   BEWUSST OFFENE RESTLUECKE: ein char-Parameter, dessen Adresse per
		   ADDRL genommen wird, OHNE dass er irgendwo per LOADC/STOREC
		   angefasst wird ("void f(char c){char* p; p=&c; ...}"), wird hier
		   nicht erkannt -- die IR ("FUNC <name> <nargs>", s. docs/IR_OPCODES.md)
		   traegt keine Parametertypen, und ADDRL allein ist kein Beleg fuer
		   char (bei einem int-Parameter waere die Verengung sogar falsch).
		   Im echten Generator kommt dieser Fall nicht vor. */
		{
			int pslot;
			int pk;
			for (pslot = 0; pslot < fn->nargs; pslot++) {
				int usedAsChar = 0, usedAsShort = 0;
				int off;
				for (pk = fn->first; pk < fn->last; pk++) {
					Instr* px = &ir[pk];
					/* Der Opcode-Name-Vergleich MUSS vor number() stehen (Kurzschluss-
					   Auswertung) -- sonst faellt number() ueber JEDE einargumentige
					   Instruktion her, auch "LABEL L0" oder "JMP L2", deren Argument
					   gar keine Zahl ist. Genau das brach hier beim Umbau auf zwei
					   Opcode-Paare (2026-09-09): number() lief unbedingt zuerst und
					   "IR Zeile N: Zahl erwartet: L0" schlug beim naechsten Selbsthost-
					   Lauf zu (SourceQCC/ebnf.tc, tcCopyBounded). */
					if (px->argc == 1) {
						if ((strcmp(px->op, "LOADC") == 0 || strcmp(px->op, "STOREC") == 0) &&
						    number(px->args[0], px->line) == pslot) usedAsChar = 1;
						else if ((strcmp(px->op, "LOADLH") == 0 || strcmp(px->op, "STORELH") == 0) &&
						         number(px->args[0], px->line) == pslot) usedAsShort = 1;
					}
				}
				off = 8 + 4 * (fn->nargs - 1 - pslot);
				if (usedAsChar) fprintf(out, "\tmove.b\t%d(%s),%d(%s)\n", off + 3, framePtr(), off, framePtr());
				else if (usedAsShort) fprintf(out, "\tmove.w\t%d(%s),%d(%s)\n", off + 2, framePtr(), off, framePtr());
			}
		}
		for (k = fn->first; k < fn->last; k++) {
			Instr* insP = &ir[k];
			const char* op = insP->op;

			if (emitDataOp(out, op, insP, fn, &serial)) {
				/* s. emitDataOp() -- PUSH..PDIFF, ausgelagert wegen der
				   Sprungweite (2026-09-09). */
			} else if (strcmp(op, "ADD") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tadd.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "SUB") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tsub.l\td1,d0\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "NEG") == 0) {
				fputs("\tneg.l\t(a7)\n", out);
			} else if (strcmp(op, "NOT") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tseq\td0\n\tandi.l\t#1,d0\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "NOTBIT") == 0) {
				fputs("\tnot.l\t(a7)\n", out);
			} else if (strcmp(op, "BAND") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tand.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "BXOR") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\teor.l\td1,d0\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "BOR") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tor.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "SHL") == 0 || strcmp(op, "SHR") == 0 || strcmp(op, "USHR") == 0) {
				const char* mnem = strcmp(op, "SHL") == 0 ? "lsl" : strcmp(op, "SHR") == 0 ? "asr" : "lsr";
				fprintf(out, "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\t%s.l\td1,d0\n\tmove.l\td0,-(a7)\n", mnem);
			} else if (strcmp(op, "NARROWC") == 0) {
				fputs("\tmove.l\t(a7),d0\n\tandi.l\t#255,d0\n\tmove.l\td0,(a7)\n", out);
			} else if (strcmp(op, "NARROWH") == 0) {
				fputs("\tmove.l\t(a7),d0\n\tandi.l\t#65535,d0\n\tmove.l\td0,(a7)\n", out);
			} else if (strcmp(op, "SWAP") == 0) {
				/* Vertauscht die obersten zwei Stackelemente. Gebraucht ueberall dort,
				   wo ein Ergebniswert UNTER einer Adresse liegen bleiben muss --
				   "(*p)++", "a[i]++" und die Kettenzuweisung scheiterten allesamt
				   daran, dass sich der Stack bisher nicht umordnen liess (es gab nur
				   DUP). */
				fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d1\n\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "DUP") == 0 || strcmp(op, "DUPP") == 0) {
				fputs("\tmove.l\t(a7),-(a7)\n", out);
			} else if (strcmp(op, "MUL") == 0 || strcmp(op, "DIV") == 0 || strcmp(op, "UDIV") == 0 || strcmp(op, "MOD") == 0 || strcmp(op, "UMOD") == 0) {
				const char* fn2 = strcmp(op, "MUL") == 0 ? "mul_i32" : strcmp(op, "DIV") == 0 ? "div_i32" : strcmp(op, "UDIV") == 0 ? "udiv_u32" : strcmp(op, "MOD") == 0 ? "mod_i32" : "umod_u32";
				char helperAsmName[24];
				sprintf(helperAsmName, "tc_%s", fn2);
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, helperAsmName, helperTableOffset(helperAsmName), &serial, psectName);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "CMPLT") == 0) { emitCompare(out, "blt", &serial);
			} else if (strcmp(op, "CMPGT") == 0) { emitCompare(out, "bgt", &serial);
			} else if (strcmp(op, "CMPLE") == 0) { emitCompare(out, "ble", &serial);
			} else if (strcmp(op, "CMPGE") == 0) { emitCompare(out, "bge", &serial);
			} else if (strcmp(op, "CMPEQ") == 0) { emitCompare(out, "beq", &serial);
			} else if (strcmp(op, "CMPNE") == 0) { emitCompare(out, "bne", &serial);
			} else if (strcmp(op, "CMPULT") == 0) { emitCompare(out, "bcs", &serial);
			} else if (strcmp(op, "CMPUGT") == 0) { emitCompare(out, "bhi", &serial);
			} else if (strcmp(op, "CMPULE") == 0) { emitCompare(out, "bls", &serial);
			} else if (strcmp(op, "CMPUGE") == 0) { emitCompare(out, "bcc", &serial);
			} else if (strcmp(op, "PCMPEQ") == 0) { emitCompare(out, "beq", &serial);
			} else if (strcmp(op, "PCMPNE") == 0) { emitCompare(out, "bne", &serial);
			} else if (strcmp(op, "PCMPLT") == 0) { emitCompare(out, "bcs", &serial);
			} else if (strcmp(op, "PCMPGT") == 0) { emitCompare(out, "bhi", &serial);
			} else if (strcmp(op, "PCMPLE") == 0) { emitCompare(out, "bls", &serial);
			} else if (strcmp(op, "PCMPGE") == 0) { emitCompare(out, "bcc", &serial);
			} else if (strcmp(op, "LABEL") == 0 && insP->argc == 1) {
				/* psectName-Suffix aus demselben Grund wie bei emitCompare oben:
				   LABEL-Namen (tc_L0, tc_L1, ...) kommen aus der QCC-Frontend-
				   eigenen Label-Nummerierung, die in JEDER Datei wieder bei 0
				   startet -- ohne Suffix kollidieren sie beim Mehrdatei-Link,
				   sobald zwei Dateien beide Kontrollfluss (if/while/for/...)
				   enthalten (praktisch immer der Fall). */
				fprintf(out, "tc_%s__%s:\n", insP->args[0], psectName);
			} else if (strcmp(op, "JMP") == 0 && insP->argc == 1) {
				fprintf(out, "\tbra\ttc_%s__%s\n", insP->args[0], psectName);
			} else if (strcmp(op, "JZ") == 0 && insP->argc == 1) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbeq\ttc_%s__%s\n", insP->args[0], psectName);
			} else if (strcmp(op, "JNZ") == 0 && insP->argc == 1) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbne\ttc_%s__%s\n", insP->args[0], psectName);
			} else if ((strcmp(op, "CALL") == 0 || strcmp(op, "CALLP") == 0) && insP->argc == 2) {
				int nargsC = number(insP->args[1], insP->line);
				int callee = findFunction(insP->args[0]);
				char asmName[NAME_LEN + 40];
				if (callee < 0) { sprintf(msg, "IR Zeile %d: unbekannte Funktion %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(asmName, "tc_", insP->args[0], funcs[callee].isStatic);
				emitCall(out, asmName, 8 * 4 + externCount * 4 + callee * 4, &serial, psectName);
				if (nargsC) fprintf(out, "\tlea\t%d(a7),a7\n", nargsC * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "PUSHFN") == 0 && insP->argc == 1) {
				/* Adresse einer QCC-Funktion als Wert auf den Stack (Funktionszeiger).
				   Im -largedata-Modus liegt sie NICHT als Symbol vor, sondern als
				   Link-Zeit-Offset in der Funktionsindirektionstabelle: die echte
				   Laufzeitadresse ist a4 + *(a4 + index*4) -- exakt dieselbe Rechnung,
				   die emitCall() fuer den direkten Aufruf macht (siehe dort). */
				int fnIdx = findFunction(insP->args[0]);
				char asmName[NAME_LEN + 40];
				if (fnIdx < 0) { sprintf(msg, "IR Zeile %d: unbekannte Funktion %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(asmName, "tc_", insP->args[0], funcs[fnIdx].isStatic);
				if (largeDataMode) {
					/* "add.l a4,d0", NICHT "adda.l": ADDA verlangt ein ADRESSregister
					   als Ziel (emitCall() rechnet deshalb in a2). Hier ist das Ziel
					   ein Datenregister, also das normale ADD -- "ADD.L An,Dn" ist
					   zulaessig. Der echte r68 weist "adda.l a4,d0" korrekt ab
					   ("incomplete line: code not generated"). */
					if (trampolineMode) {
						/* Funktionszeiger bleiben im Tabellenpfad; r68 -j gilt fuer
						   direkte Aufrufe, nicht fuer einen beliebigen Datenwert. */
						fprintf(out, "\tmove.l\t%d(a4),d0\n\tadd.l\ta4,d0\n\tmove.l\td0,-(a7)\n", (8 + externCount + fnIdx) * 4);
					} else {
						fprintf(out, "\tmove.l\t%d(a4),d0\n\tadd.l\ta4,d0\n\tmove.l\td0,-(a7)\n", (8 + externCount + fnIdx) * 4);
					}
				} else {
					fprintf(out, "\tlea\t%s(pc),a0\n\tmove.l\ta0,-(a7)\n", asmName);
				}
			} else if ((strcmp(op, "CALLIND") == 0 || strcmp(op, "CALLINDP") == 0) && insP->argc == 1) {
				/* Indirekter Aufruf ueber einen Funktionszeiger. Stapelbelegung beim
				   Eintritt (von UNTEN nach oben): zuerst der Zeiger, darueber
				   arg1..argN. Diese Reihenfolge ergibt sich zwangslaeufig aus dem
				   Parsen: bei "ausdruck(args)" wird der Callee-Ausdruck VOR den
				   Argumenten ausgewertet und legt seinen Wert deshalb zuerst ab
				   (anders als beim direkten CALL, wo der Name gar keinen Code
				   erzeugt). Der Zeiger wird folglich NICHT gepoppt, sondern in der
				   Tiefe nargs*4 gelesen; am Ende werden Argumente UND Zeiger
				   gemeinsam abgeraeumt. a3/a4 muessen nach dem jsr aufgefrischt
				   werden (gleiche Begruendung wie in emitCall(): der Aufgerufene hat
				   seine EIGENEN Tabellenzeiger gesetzt). */
				int nargsI = number(insP->args[0], insP->line);
				fprintf(out, "\tmove.l\t%d(a7),a2\n\tjsr\t(a2)\n", nargsI * 4);
				if (largeDataMode) {
					int id = serial++;
					char anchor[NAME_LEN + 40];
					fprintf(out, "tc_callret_%d__%s:\n", id, psectName);
					sprintf(anchor, "tc_callret_%d__%s", id, psectName);
					emitTableBases(out, anchor, psectName);
				}
				fprintf(out, "\tlea\t%d(a7),a7\n", (nargsI + 1) * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if ((strcmp(op, "CALLEXT") == 0 || strcmp(op, "CALLEXTP") == 0) && insP->argc == 3) {
				/* Aufruf einer NICHT in dieser IR definierten (externen) Funktion, z.B.
				   einer echten OS-9/Microware-clib-Funktion (strcmp, printf, malloc, ...).
				   Nutzt die dokumentierte Microware-68K-C/C++-ABI (Ultra C/C++ Processor
				   Guide, Kapitel "Passing Arguments to Functions") statt der sonst hier
				   verwendeten reinen Stack-ABI fuer QCC-EIGENE Funktionen: die ERSTEN
				   BEIDEN FEST DEKLARIERTEN Parameter -> d0/d1 (GENAU wie bei einem
				   nicht-variadischen Aufruf), ALLE weiteren Argumente (der variadische
				   "..."-Teil, z.B. printfs Werte nach dem Formatstring) auf den Stack, in
				   UMGEKEHRTER Erscheinungsreihenfolge gepusht (das erste ueberzaehlige
				   Argument landet dadurch am NAECHSTEN zur Ruecksprungadresse).
				   WICHTIG (2026-07-24, live gegen die echte Microware-clib.l auf Q9
				   gefunden UND korrigiert): das dritte IR-Feld ist NICHT die Gesamtzahl
				   der Argumente oder ein reines variadic-Bool, sondern tcFunctionNargs[f]
				   -- die Anzahl der FEST DEKLARIERTEN Parameter laut extern-Deklaration
				   ("..." selbst zaehlt nicht mit). Der fruehere Stand nahm faelschlich an,
				   ein variadischer Aufruf lege AUSNAHMSLOS alles auf den Stack (0 Register)
				   -- das entsprach nur unserem eigenen, nie gegen echten Compiler-Code
				   verifizierten Mock-Test. Der ECHTE, von xcc erzeugte Aufrufcode zu
				   printf(fmt, x) zeigt: der Formatstring selbst (1 fest deklarierter
				   Parameter) landet ganz normal in d0, NUR x (der variadische Teil) auf
				   dem Stack -- das PMMU-Absturzbild auf Q9 (Adresse 1 statt eines echten
				   Pointers) entstand exakt daraus, dass unser altes "0 Register bei
				   variadisch"-Schema den Formatstring faelschlich auf den Stack statt nach
				   d0 legte.
				   Unser eigener Stack-IR liefert alle Argumente bereits in
				   Erscheinungsreihenfolge auf a7 (1. Argument am weitesten unten, da
				   zuerst gepusht) -- die obersten "stackArgs" Werte werden daher zuerst
				   in ein festes Scratch-Feld ausgelagert (tc_extcall_tmp), damit sie
				   NICHT verloren gehen, wenn darunter noch d0/d1 herausgeholt werden
				   muessen; anschliessend werden sie in DERSELBEN (bereits umgekehrten)
				   Reihenfolge zurueckgepusht. Der Aufruf selbst geht per "jsr" auf den
				   ROHEN Funktionsnamen (kein "tc_"-Praefix wie bei internen Aufrufen) --
				   das eigentliche Linken gegen die reale clib.l (externe Symbolaufloesung
				   via l68 statt der aktuellen "vasm -Fbin"-Direktassemblierung) ist noch
				   NICHT Teil dieses Schritts, siehe docs/FORTSCHRITT.md. */
				int nargsC = number(insP->args[1], insP->line);
				int fixedCount = number(insP->args[2], insP->line);
				/* BUG (2026-07-26/27, live auf Q9 gefunden, per capstone-Disassemblierung
				   der ECHTEN clib.l-printf bestaetigt): die Annahme "NUR die FEST
				   deklarierten Parameter gehen nach d0/d1, der GESAMTE variadische Teil
				   auf den Stack" (2026-07-24-Fund) war FALSCH bzw. unvollstaendig. Die
				   echte, kompilierte printf(char* fmt, ...) beginnt mit "move.l d0,-(a7)"
				   gefolgt von "move.l d1,d0" -- sie erwartet also ihr ERSTES variadisches
				   Argument (falls vorhanden) IMMER in d1, unabhaengig davon, ob es laut
				   Deklaration "fest" oder Teil von "..." ist. Die REALE Regel ist: die
				   ERSTEN ZWEI ARGUMENTE INSGESAMT (fest+variadisch zusammengezaehlt) gehen
				   nach d0/d1, NUR ab dem DRITTEN Argument geht es auf den Stack -- exakt
				   wie bei einem nicht-variadischen Aufruf, OHNE Sonderrolle fuer "...".
				   fixedCount wird nicht mehr fuer die Register/Stack-Aufteilung gebraucht
				   (bleibt nur zur IR-Validierung erhalten). */
				int hasD0 = nargsC >= 1;
				int hasD1 = nargsC >= 2;
				int stackArgs = nargsC - (hasD0 ? 1 : 0) - (hasD1 ? 1 : 0);
				int ai;
				if (stackArgs > 8) { sprintf(msg, "IR Zeile %d: zu viele Stack-Argumente fuer externen Aufruf (max 8)", insP->line); fatal(msg); }
				/* Adresse EINMAL in a0 (a0 ist in diesem Backend generell ein freies
				   Scratch-Adressregister, wird von keinem IR-Opcode ueber dessen eigene
				   Emission hinaus als gueltig vorausgesetzt). small: PC-relative "lea"
				   passend zum PIC-Stil des restlichen Backends (vgl. tc_g_<name>(pc)-
				   Zugriffe); large: derselbe a3-Indirektionsmechanismus wie bei echten
				   Globalen (siehe emitLeaGlobal()-Kommentar) -- tc_extcall_tmp bekommt
				   dafuer einen zusaetzlichen Tabelleneintrag NACH allen echten Globalen
				   (Offset globalCount*4). */
				if (stackArgs > 0) {
					/* 2026-07-26: tc_gadata-Eintrag ist ein Link-Zeit-Offset, kein
					   absoluter Zeiger (siehe emitLeaGlobal()-Kommentar) -- adda.l
					   noetig wie ueberall sonst. */
					if (largeDataMode) fprintf(out, "\tmove.l\t%d(a3),a0\n\tadda.l\ta3,a0\n", globalCount * 4);
					else fputs("\tlea\ttc_extcall_tmp(pc),a0\n", out);
				}
				for (ai = 0; ai < stackArgs; ai++) fprintf(out, "\tmove.l\t(a7)+,%d(a0)\n", ai * 4);
				if (hasD1) fputs("\tmove.l\t(a7)+,d1\n", out);
				if (hasD0) fputs("\tmove.l\t(a7)+,d0\n", out);
				for (ai = 0; ai < stackArgs; ai++) fprintf(out, "\tmove.l\t%d(a0),-(a7)\n", ai * 4);
				/* WICHTIG (2026-07-24, live am echten Q9 gefunden): "jsr <name>" auf
				   ein externes Symbol wird von r68/l68 NUR dann PIC-sicher (PC-
				   relativ) aufgeloest, wenn es als "bsr" geschrieben wird -- ein
				   rohes "jsr" assembliert stattdessen zu einer ABSOLUTEN Adresse
				   (r68 kennt bei "jsr" keine automatische PC-relative Kodierung fuer
				   externe/undefinierte Symbole), was bei einem NICHT bei Adresse 0
				   geladenen OS-9-Modul sofort einen PMMU-Fehler ausloest (reproduziert:
				   ein Testaufruf gegen die echte clib.l stuerzte den Q9-Emulator ab,
				   Disassemblierung zeigte "JSR $xxxx.L" statt einer PC-relativen
				   Kodierung). "bsr" ist dagegen inhaerent PC-relativ; bei zu grosser
				   Distanz haengt der Microware-Linker automatisch eine PIC-taugliche
				   Jumptable-Indirektion ein (l68 -a). NUR im -os9-Modus relevant --
				   im Default-/vasm-/Simulator-Modus bleibt "jsr" (von
				   tools/qcc68sim.py als Mock-Aufruf-Marker erkannt, keine echte
				   Positionsunabhaengigkeit noetig, keine Regression riskieren).
				   ZWEITER FUND (2026-07-26, live auf Q9, Ultra-C/C++ Processor Guide
				   Table 1-12 "Register Use"): a3/a4 (unsere -largedata-Tabellenbasen)
				   sind reine ABI-Temporaer-Register -- JEDE echte clib.l-Funktion darf
				   sie zerstoeren. Ein direktes "bsr/jsr <name>" HIER wuerde a3/a4 also
				   unbemerkt korrumpieren. Deshalb (NUR largeDataMode): nicht direkt
				   rufen, sondern ueber denselben a4-Tabellen-Indirektionsmechanismus
				   wie interne QCC-Funktionen (emitCall()) einen kleinen Wrapper-Stub
				   rufen (tc_extwrap_<name>, siehe Tabellen-Emission weiter oben) -- der
				   macht den echten Aufruf UND frischt a3/a4 danach auf, physisch nah an
				   den Tabellen platziert (PC-relativ immer sicher erreichbar, anders als
				   die Aufrufstelle hier, die beliebig weit entfernt sein kann). Der
				   Wrapper ist transparent: er sieht/reicht dieselben d0/d1/Stack-Werte
				   durch wie ein direkter Aufruf, der Aufrufer hier aendert sich NICHT
				   (Push der Stack-Argumente vorher, Cleanup+d0-Auswertung danach exakt
				   wie zuvor). Ohne largeDataMode bleibt der direkte "bsr/jsr <name>"
				   unveraendert (keine Tabellen, kein Korruptionsrisiko in der Praxis,
				   da dieser Modus bisher nur fuer Simulator-Mocks genutzt wird). */
				if (largeDataMode) {
					if (trampolineMode) {
						fprintf(out, "\tbsr\ttc_extwrap_%s__%s\n", insP->args[0], psectName);
					}
					else fprintf(out, "\tmove.l\t%d(a4),a2\n\tadda.l\ta4,a2\n\tjsr\t(a2)\n", externTableOffset(insP->args[0]));
				} else {
					fprintf(out, "\t%s\t%s\n", os9Mode ? "bsr" : "jsr", insP->args[0]);
				}
				if (stackArgs) fprintf(out, "\tlea\t%d(a7),a7\n", stackArgs * 4);
				fputs("\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "RET") == 0 || strcmp(op, "RETP") == 0) {
				fprintf(out, "\tmove.l\t(a7)+,d0\n\tunlk\t%s\n\trts\n", framePtr());
			} else if (strcmp(op, "DROP") == 0) {
				fputs("\taddq.l\t#4,a7\n", out);
			} else if (strcmp(op, "PRINT") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putint", helperTableOffset("tc_putint"), &serial, psectName);
			} else if (strcmp(op, "PRINTU") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putuint", helperTableOffset("tc_putuint"), &serial, psectName);
			} else if (strcmp(op, "PRINTC") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n", out);
				emitCall(out, "tc_putchar", helperTableOffset("tc_putchar"), &serial, psectName);
			} else if (strcmp(op, "GLOBAL") == 0 || strcmp(op, "GARRAY") == 0 || strcmp(op, "GINIT") == 0) {
				/* static lokale Variable: bereits von collectGlobals() ausgewertet (Adresse/
				   Initialwert stehen im DATA/BSS-Abschnitt) -- an dieser Stelle im Funktions-
				   koerper ein reines No-op, keine Laufzeit-Aktion. */
			} else {
				sprintf(msg, "IR Zeile %d: unbekannter oder unvollstaendiger Opcode %s", insP->line, op);
				fatal(msg);
			}
		}
		fputs("\n", out);
	}

	{
		int hasData = 0, hasBss = 0, gi;
		/* Ein Array ohne GINIT ist C-semantisch vollstaendig nullinitialisiert.
		   Es gehoert deshalb in einen OS-9-vsect statt als Millionen explizite
		   "dc.b 0"-Zeichen in die r68-Eingabe geschrieben zu werden. Das ist
		   besonders wichtig fuer den QCC-Bootstrap (mehrere MB Action-Log).
		   Arrays MIT GINIT bleiben im DATA-Abschnitt, damit ihre Werte erhalten
		   bleiben. */
		for (gi = 0; gi < globalCount; gi++) {
			if (globals[gi].declOnly) continue; /* definiert in einer ANDEREN Datei, keine Speicherallokation hier */
			/* Ein NICHT-remoter vsect ist auf 64 KB begrenzt (l68 lehnt mehr ab),
			   deshalb bleiben die initialisierten Globals im psect; nur die ganz
			   genullten gehen in einen vsect remote, der diese Grenze nicht hat.
			   Ohne -remotedata ist globalRemote() immer 0 -- die Ausgabe bleibt
			   dann Byte fuer Byte die alte. */
			if (globalRemote(gi)) hasBss = 1;
			else hasData = 1;
		}
		/* Die -largedata-Datenindirektionstabelle (tc_gadata) wird NICHT mehr
		   hier emittiert (siehe emitLeaGlobal()-Kommentar) -- sie sitzt jetzt
		   VOR allen Funktionsrumpf-Texten, direkt nach tc_functab, damit sie
		   ueber das einmalige "lea tc_gadata(pc),a3" immer erreichbar bleibt,
		   egal wie gross der Rest des Programms (inkl. dieses DATA/BSS-Blocks)
		   wird. */
		if (hasData) {
			fprintf(out, "\n%s DATA-Aequivalent des flachen Einzelmoduls: statisch initialisierte int32-Globals\n", fullCommentPrefix());
			emitAlign(out);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->declOnly) continue;
				if (!globalRemote(gi)) {
					int e; char gAsmName[NAME_LEN + 40];
					mangledName(gAsmName, "tc_g_", g->name, g->isStatic);
					if (g->elemSize != 1) emitAlign(out);
					if (!g->isArray) {
						fprintf(out, "%s:\tdc.%c\t%d\n", gAsmName, tagSuffix(g->elemSize), g->initialValue);
					} else if (!g->hasGinit) {
						/* perLine nur Lesbarkeit des erzeugten Assemblers, keine
						   Korrektheitsfrage -- 2026-09-09 fuer short (elemSize 2)
						   einen Mittelwert dazugenommen. */
						int e, perLine = g->elemSize == 1 ? 40 : g->elemSize == 2 ? 30 : 20;
						fprintf(out, "%s:\n", gAsmName);
						for (e = 0; e < g->length; ) {
							int n = g->length - e < perLine ? g->length - e : perLine, k;
							fprintf(out, "\tdc.%c\t0", tagSuffix(g->elemSize));
							for (k = 1; k < n; k++) fprintf(out, ",0");
							fprintf(out, "\n");
							e += n;
						}
					} else {
						fprintf(out, "%s:\n", gAsmName);
						/* 2026-08-11 behobener Korrektheitsfehler: die Schleife lief bis
						   g->length, init[] fasste aber nur MAX_ARRAY_LEN Elemente. Ein
						   Array laenger als MAX_ARRAY_LEN MIT mindestens einem GINIT gab
						   dadurch Speicher HINTER init[] aus -- nachweisbar die Folgefelder
						   und der Name der naechsten globalen Variablen als Zahlen. Live
						   reproduziert mit GARRAY-Laenge 5000 + einem GINIT: ab Index 4096
						   erschienen Werte wie 1751343470 (= "nach", Name des Nachbarn).
						   Indizes ab initLen sind logisch null (GINIT lehnt sie ab), werden
						   also als 0 ausgegeben. */
						for (e = 0; e < g->length; e++) {
							/* Zwischenzeiger wie oben: durch ein skalares
							   Zeigerfeld hindurch zu indizieren kann QCCs
							   Teilmenge nicht. */
							int* gi2 = g->init;
							int v = e < g->initLen ? gi2[e] : 0;
							fprintf(out, "\tdc.%c\t%d\n", tagSuffix(g->elemSize), v);
						}
					}
				}
			}
		}
		if (hasBss) {
			fprintf(out, "\n%s VSECT REMOTE: genullte Globals -- OS-9 legt den Bereich an und nullt ihn (gemessen), das Modul traegt kein einziges Nullbyte dafuer\n", fullCommentPrefix());
			if (os9Mode) fputs("\tvsect\tremote\n", out);
			else fputs("\tsection .bss\n", out);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->declOnly) continue;
				if (globalRemote(gi)) {
					char gAsmName[NAME_LEN + 40];
					mangledName(gAsmName, "tc_g_", g->name, g->isStatic);
					/* ds.b richtet nicht aus; ein folgendes ds.l braucht die
					   Langwortgrenze, sonst liest der 68000 ein ungerades Langwort.
					   align im vsect ist gegen r68 geprueft (Q9-qr68/test/remotetest.sh). */
					if (g->elemSize != 1) emitAlign(out);
					fprintf(out, "%s:\tds.%c\t%d\n", gAsmName, tagSuffix(g->elemSize),
					        g->isArray ? g->length : 1);
				}
			}
			if (os9Mode) fputs("\tends\n", out);
		}
	}
	if (os9Mode) fputs("\tends\n", out);
}

int main(int argc, char* argv[]) {
	FILE* out;
	char msg[300];
	int i;
	if (argc < 3) {
		fprintf(stderr, "usage: %s <input.ir> <output.s68> [-os9] [-part] [-runtime] [-largedata] [-trampolines] [-unit=name]\n", argv[0]);
		fprintf(stderr, "  -os9:       Microware-r68-Ausgabeformat (nam/psect/ends, \"*\" statt \";\"\n");
		fprintf(stderr, "              fuer volle Kommentarzeilen) statt vasm-kompatiblem Format.\n");
		fprintf(stderr, "  -part:      diese Datei ist EIN TEIL eines Mehrdatei-Programms (kein\n");
		fprintf(stderr, "              eigenstaendiges main noetig) -- fuer echten Mehrdatei-Link\n");
		fprintf(stderr, "              mit l68 gegen andere -os9/-part-Module.\n");
		fprintf(stderr, "  -runtime:   nur mit -part: diese Datei traegt zusaetzlich den gemeinsamen\n");
		fprintf(stderr, "              68k-Core/I/O-Anker (tc_mul_i32 etc.) -- GENAU EINE Datei im\n");
		fprintf(stderr, "              Mehrdatei-Programm muss dies setzen, sonst 'duplicate symbol'.\n");
		fprintf(stderr, "  -largedata: \"grosses Speichermodell\" -- adressiert globale Variablen ueber\n");
		fprintf(stderr, "              eine Indirektionstabelle statt direkt PC-relativ. Noetig, sobald\n");
		fprintf(stderr, "              r68 bei -os9 \"value out of range\" meldet (echte 68000-Grenze:\n");
		fprintf(stderr, "              PC-relative Adressierung reicht nur +-32 KB) -- Standard (ohne\n");
		fprintf(stderr, "              diese Option) ist schneller/kompakter, reicht aber nur fuer\n");
		fprintf(stderr, "              kleinere Programme mit wenig globalem Zustand.\n");
		fprintf(stderr, "  -remotedata: genullte Globals in einen vsect remote statt als dc.l 0 in den\n");
		fprintf(stderr, "              psect. OS-9 nullt den Datenbereich selbst, das Modul wird dadurch\n");
		fprintf(stderr, "              erheblich kleiner; der Zugriff ist a6-relativ mit 32 Bit und kennt\n");
		fprintf(stderr, "              damit weder die 32-KB- noch die 64-KB-Grenze. Nur mit -os9.\n");
		fprintf(stderr, "  -trampolines: optionaler relokierbarer Fernaufrufpfad; nur mit -largedata.\n");
		fprintf(stderr, "  -unit=name: gemeinsamer static-Namensraum fuer kuenstlich gesplittete Teile.\n");
		fprintf(stderr, "                Zusammen mit r68 -j und l68 -a verwenden; erzeugt eine\n");
		fprintf(stderr, "                Microware-Jump-Tabelle und einen kleinen OS-9-Datenanker.\n");
		return 2;
	}
	for (i = 3; i < argc; i++) {
		if (strcmp(argv[i], "-os9") == 0) os9Mode = 1;
		else if (strcmp(argv[i], "-part") == 0) partMode = 1;
		else if (strcmp(argv[i], "-runtime") == 0) runtimeMode = 1;
		else if (strcmp(argv[i], "-largedata") == 0) largeDataMode = 1;
		else if (strcmp(argv[i], "-remotedata") == 0) remoteDataMode = 1;
		else if (strcmp(argv[i], "-trampolines") == 0) trampolineMode = 1;
		else if (strncmp(argv[i], "-unit=", 6) == 0 && argv[i][6] != '\0') {
			strncpy(staticUnit, argv[i] + 6, NAME_LEN - 1);
			staticUnit[NAME_LEN - 1] = '\0';
		}
		else { fprintf(stderr, "unbekannte Option: %s\n", argv[i]); return 2; }
	}
	readIR(argv[1]);
	collectGlobals();
	collectFunctions();
	collectExterns();
	if (remoteDataMode) {
		int gi;
		/* a6 ist nur im -os9-Modus frei. Im vasm-Format IST a6 der Frame-Pointer
		   (siehe framePtr()) -- ein adda.l a6,reg zeigte dort auf den Stackframe. */
		if (!os9Mode) fatal("-remotedata braucht -os9: nur dort ist a6 der Datenbereichszeiger und nicht der Frame-Pointer");
		/* Dateiuebergreifende Globals: diese Datei kann NICHT wissen, ob die
		   DEFINIERENDE Datei das Symbol in den psect oder in den vsect remote
		   gelegt hat -- der Zugriffsweg unterscheidet sich aber (PC-relativ oder
		   Tabelle gegen a6-relativ). Lieber melden als still falsch adressieren. */
		for (gi = 0; gi < globalCount; gi++) {
			if (globals[gi].declOnly) {
				sprintf(msg, "-remotedata und dateiuebergreifendes Globales %s: der Zugriffsweg haengt von der definierenden Datei ab", globals[gi].name);
				fatal(msg);
			}
		}
	}
	if (!largeDataMode) {
		/* Heuristik-Warnung (2026-07-25, siehe -largedata/emitLeaGlobal()): wir koennen
		   NICHT wissen, ob r68 die PC-relative Reichweite tatsaechlich ueberschreiten
		   wird (haengt von der GESAMTEN Code+Daten-Distanz ab, die erst der Assembler
		   kennt) -- aber ein grober Schwellenwert ueber die reine globale Datenmenge
		   gibt fruehzeitig einen Hinweis, BEVOR ein kryptisches "value out of range"
		   vom echten r68 kommt. 16000 Byte ist bewusst konservativ (deutlich unter den
		   theoretischen 32 KB), da Code UND andere Symbole denselben Adressraum teilen. */
		long totalGlobalBytes = 0; int gi;
		for (gi = 0; gi < globalCount; gi++) {
			Global* g = &globals[gi];
			if (g->declOnly) continue;
			if (globalRemote(gi)) continue; /* liegt im Datenbereich, nicht im psect -- keine PC-relative Distanz */
			totalGlobalBytes += (long)g->elemSize * (g->isArray ? g->length : 1);
		}
		if (totalGlobalBytes > 16000) {
			fprintf(stderr, "qcc_backend: Warnung: globale Daten sind mit %ld Byte recht gross fuer das\n", totalGlobalBytes);
			fprintf(stderr, "  Standard-Speichermodell (PC-relative Adressierung, echte 68000-Grenze ist\n");
			fprintf(stderr, "  +-32 KB von JEDER referenzierenden Instruktion aus). Falls r68 spaeter mit\n");
			fprintf(stderr, "  \"value out of range\" fehlschlaegt: mit -largedata neu uebersetzen.\n");
		}
	}
	if (os9Mode) {
		/* psect-Name aus dem Ausgabedateinamen ableiten (ohne Pfad/Endung), analog
		   zum Default in codegen.cpp (genParser68kTo: <basisname>_p). */
		const char* base = strrchr(argv[2], '/');
		const char* dot;
		int n, i;
		base = base ? base + 1 : argv[2];
		dot = strrchr(base, '.');
		n = dot ? (int)(dot - base) : (int)strlen(base);
		if (n > (int)sizeof(psectName) - 3) n = (int)sizeof(psectName) - 3;
		for (i = 0; i < n; i++) psectName[i] = base[i];
		psectName[n] = '\0';
		strcat(psectName, "_p");
	}
	out = fopen(argv[2], "w");
	if (!out) { sprintf(msg, "kann Ausgabe nicht schreiben: %s", argv[2]); fatal(msg); }
	emitIR(out);
	if (ferror(out)) fatal("Schreibfehler in Assembler-Ausgabe");
	fclose(out);
	return 0;
}
