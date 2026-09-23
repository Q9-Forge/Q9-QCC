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
   IR lines). It was already guarded by fatal(), but the limit was too low.

   2026-09-15: increased from 98304. Self-hosting needed 97,822 lines, or
   99.5% of the cap; it had already been at 98.6% before the symbolic
   pointer size work, so this was tight independently of it. The cap grows
   with the frontend source itself, because QCC compiles its own parser.
   The cost is target RAM, not module size: ir[] is a static table of
   MAX_IR_LINES * 56 bytes on 68k (Instr = 24 + 6*4 + 8), so this step adds
   1.75 MB, taking the table from 5.25 to 7.0 MB against the 16 MB of the
   Q9. Verified on the real 68030 afterwards (qccb_68k.sh, qcc_68k.sh).
   If it gets tight again, the honest fix is a grown table rather than a
   larger literal: the 101 read sites use ir[i] and would not change if
   ir became a pointer grown with realloc, exactly as the parser already
   does for its action log. */
#define MAX_IR_LINES    131072
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
/* 2026-09-16 von 2048 auf 3072 angehoben. GEMESSEN, nicht geschaetzt: der
   Selbsthost (QCC uebersetzt seinen eigenen Parser) brauchte 2053 und riss
   damit die alte Grenze um fuenf. Der Zuwachs kommt fast ausschliesslich aus
   String-Literalen -- jede neue Diagnose im Frontend ist ein eigenes
   __strN-Global, und der Gleitkomma-Ausbau hat viele gebracht.
   Hier steht bewusst NICHT der gemessene Bedarf: eine Grenze, die genau
   passt, reisst bei der naechsten Fehlermeldung wieder (dieselbe Ueberlegung
   wie bei TC_MAX_CTRL). Preis: die Global-Tabelle ist ein statisches Feld von
   gut 100 Byte je Eintrag, die Anhebung kostet also rund 110 KB im
   Backend-Modul -- vertretbar neben den 16 MB des Q9, und weit entfernt von
   den 16,8 MB, an denen das alte init[]-im-Global-Feld gescheitert war. */
#define MAX_GLOBALS     3072
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
	/* Eigene variadische Funktionsdefinitionen (2026-09-23), viertes FUNC-Feld.
	   Aendert NUR die Richtung der Parameter-Offset-Formel in slotAddress()
	   (aufsteigend statt absteigend) -- s. dort. Ohne diese Umkehr haette der
	   letzte benannte Parameter je nach Aufrufstelle eine andere Adresse,
	   weil sie sonst von der (variablen) Gesamtzahl der Argumente abhaengt;
	   der Aufrufer gleicht das mit VAREVERSE aus (s. tc_call in qcc.lextab). */
	int isVariadic;
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
	int hasInitAddr; /* 2026-09-15: at least one GINITADDR -- forces the vsect */
	int declOnly, isStatic; /* see Function */
} Global;

/* GINITADDR carries the ADDRESS of another global as an initial value; it
   comes from a string literal inside an initializer list
   (char *tab[] = {"a","b"}). Such a value is only known at LOAD time: OS-9
   relocates it through M$IRefs, and the only place that happens is an
   initialized (NON-remote) vsect -- a "dc.l <label>" in the psect is NOT a
   relocated address according to the OS-9 manual. Globals with such an
   initializer therefore move into the vsect. */
#define MAX_INITADDR    4096
static int initAddrGidx[MAX_INITADDR];
static int initAddrIdx[MAX_INITADDR];
static char* initAddrSym[MAX_INITADDR];
static int initAddrCount;

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

static void fatal(const char* msg); /* Defined below; forward declaration for registerExtern()/externTableOffset(). */

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
	memcpy(dst, tok, len);   /* No (size_t) cast: QCC does not yet support "(unsigned)" without "int"; implicit conversion is sufficient. */
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
/* -peephole (2026-09-08): post-process the generated assembly text; see
   qcc_backend_peephole.c. Independent of -os9/-largedata/-remotedata, it
   operates purely on the output file. */
static int peepholeMode = 0;
static char psectName[NAME_LEN] = "tc_prog";
/* -unit=<name> groups artificially split IR parts that originated from one
   translation unit.  It deliberately affects only static-symbol mangling:
   normal multi-file builds keep using their individual psect name. */
static char staticUnit[NAME_LEN] = "";
static const char* fullCommentPrefix(void) { return os9Mode ? "*" : ";"; }
/* Register Use Table in the Ultra-C/C++ Processor Guide (ultrac_pg.pdf, chapter
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

/* Memory-model switch (2026-07-25, see -largedata in main()/usage()):
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
	/* IMPORTANT (2026-07-26, found live on Q9; see emitCall()): tc_gadata now
	   contains link-time offsets (target minus table base), not absolute
	   addresses. move.l loads the offset and adda.l a3,reg forms the runtime
	   address; reg is always an address register (a0).
	   SECOND FINDING: according to the Microware ABI, a3 (like a4 and a0-a2) is
	   a temporary register. Only d0/d1, a5, a6, and a7 are reserved. Any real
	   clib.l function may overwrite a3. The original design, which initialized
	   a3 once at program start, therefore failed after the first external call.
	   Reloading with lea X(pc) before every access was also rejected by r68 when
	   the table was beyond the 16-bit PC-relative range. The fix is to reload
	   a3/a4 immediately after every CALLEXT/CALLEXTP, where corruption can occur;
	   the refresh is always close to the call. emitLeaGlobal()/emitCall() remain
	   unchanged and use the most recently refreshed bases. */
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
	if (globals[gidx].hasInitAddr) return 0;   /* initialized, so not "remote" */
	return globalAllZero(&globals[gidx]);
}
/* Does this global live in the DATA AREA (addressed a6-relative) rather than
   in the psect? Two reasons lead there and they must not be confused: a
   zeroed global under -remotedata (vsect remote, "ds"), and a global with a
   GINITADDR (initialized vsect, "dc.l <symbol>"). The ACCESS path is the same
   for both, the OUTPUT is not -- which is why there are two predicates. */
static int globalInDataArea(int gidx) {
	if (globals[gidx].hasInitAddr) return 1;
	return globalRemote(gidx);
}
static void emitLeaGlobal(FILE* out, int gidx, const char* reg) {
	if (globalInDataArea(gidx)) {
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
#include "qcc_backend_peephole.c"

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
	/* long long (2026-09-23): tc_mul_i64/tc_div_i64/tc_mod_i64 werden BEWUSST
	   NICHT hier eingetragen -- ihre Aufrufstellen (QMUL/QDIV/QMOD) nutzen
	   immer ein reines "bsr", nie emitCall()/diese Tabelle, s. dortigen
	   Kommentar. */
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

/* Emits a call to an ALREADY MANGLED assembler name (for QCC functions,
   tableOffset = funcIndex*4) OR to a raw runtime-helper name
   (tableOffset = helperTableOffset(...)). In small mode it emits the unchanged
   "bsr asmName"; in large mode it uses a4/a2 table indirection as described
   above.
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
/* BUG 5 (2026-07-26, found live on Q9, the fifth -largedata bug in this
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
	       strcmp(w, "h") == 0 || strcmp(w, "p") == 0 || strcmp(w, "d") == 0 || strcmp(w, "q") == 0;
}

/* Byte size of a type tag for LOAD/STORE width and pointer/index scaling
   (2026-09-09, replacing the former isByteWord(): short adds a third size, so
   a boolean is no longer sufficient). 'h' -> 2; everything else remains as
   before (pointer 'p' and all 32-bit scalars 'i'/'u' -> 4). */
static int tagSize(const char* w) {
	if (strcmp(w, "c") == 0 || strcmp(w, "b") == 0) return 1;
	if (strcmp(w, "h") == 0) return 2;
	/* 'd' = double, 8 Byte (2026-09-16). Ein double liegt immer als BLOCK,
	   nie in einem Slot -- s. docs/FLOAT_IR_ENTWURF_de.md. */
	if (strcmp(w, "d") == 0) return 8;
	/* 'q' = long long, ebenfalls 8 Byte (2026-09-23) -- dieselbe Block-
	   Notwendigkeit wie 'd'. OHNE diesen Zweig fiele 'q' auf die 4-Byte-
	   Standardgroesse zurueck: falsche Arrayschrittweite, falsche
	   GARRAY-Allozierung, falsche Skalierung ueberall, wo tagSize() den
	   Elementabstand bestimmt. */
	if (strcmp(w, "q") == 0) return 8;
	return 4;
}
/* Shift amount for lsl.l/asr.l scaling in pointer arithmetic/indexing:
   Byte 1x (no shift), word 2x, long 4x. */
static int tagShift(const char* w) { int s = tagSize(w); return s == 1 ? 0 : s == 2 ? 1 : s == 8 ? 3 : 2; }
/* 68k size suffix for move/dc/ds. */
static char tagSuffix(int size) { return size == 1 ? 'b' : size == 2 ? 'w' : 'l'; }

/* Wie number(), aber vorzeichenlos: die beiden Haelften eines
   double-Bitmusters (GINITD) reichen bis $FFFFFFFF und wuerden mit strtol
   ueberlaufen. Gespeichert wird das Bitmuster, nicht der Zahlwert. */
static int numberU(const char* text, int line) {
	char* end;
	unsigned long v;
	char msg[160];
	if (text[0] == '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	v = strtoul(text, &end, 10);
	if (*end != '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	return (int)(unsigned int)v;
}

static int number(const char* text, int line) {
	char* end;
	long v;
	char msg[160];
	if (text[0] == '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	v = strtol(text, &end, 10);
	if (*end != '\0') { sprintf(msg, "IR Zeile %d: Zahl erwartet: %s", line, text); fatal(msg); }
	return (int)v;
}

/* ZEIGERGROESSE DIESES BACKENDS.
   Groessen und Offsets mit Zeigeranteil kommen als "k+nP" aus dem Frontend
   (tcEmitNum in Data/qcc.lextab): k ist der zeigerfreie Anteil in Byte, n die
   Zahl der Zeigergroessen darin. Hier wird das eigene P eingesetzt -- deshalb
   gilt dieselbe IR fuer 68k und ARM64, ohne dass das Frontend das Ziel kennt.
   Die Aufloesung sitzt beim EINLESEN und nicht an den Verwendungsstellen:
   sonst muesste jedes number(args[i]) davon wissen, und eine vergessene
   Stelle waere ein stiller Rechenfehler statt eines Abbruchs. */
#define QIR_PTR_SIZE 4

/* "k+nP" zu seiner Zahl aufloesen; jedes andere Wort unveraendert lassen.
   Bewusst von Hand geparst statt mit sscanf: diese Quelle muss in der
   Teilmenge bleiben, die QCC selbst lesen kann. */
static const char* resolvePtrExpr(const char* tok, char* buf)
{
	int i;
	int k;
	int n;
	int digits;
	i = 0; k = 0; n = 0; digits = 0;
	if (tok[0] < '0' || tok[0] > '9') return tok;
	while (tok[i] >= '0' && tok[i] <= '9') { k = k * 10 + (tok[i] - '0'); i++; }
	if (tok[i] != '+') return tok;
	i++;
	while (tok[i] >= '0' && tok[i] <= '9') { n = n * 10 + (tok[i] - '0'); i++; digits++; }
	if (digits == 0) return tok;
	if (tok[i] != 'P') return tok;
	if (tok[i + 1] != 0) return tok;
	sprintf(buf, "%d", k + QIR_PTR_SIZE * n);
	return buf;
}

static void readIR(const char* path) {
	char ptrBuf[32];
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
				insP->args[insP->argc] = argIntern(resolvePtrExpr(tok, ptrBuf), line);
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
		if (strcmp(insP->op, "GINITADDR") == 0) {
			int found = 0;
			if (insP->argc != 3) fatal("ungueltiges GINITADDR");
			for (gi = 0; gi < globalCount; gi++) {
				if (strcmp(globals[gi].name, insP->args[0]) == 0) {
					idx = number(insP->args[1], insP->line);
					if (idx < 0 || (globals[gi].isArray && idx >= globals[gi].length))
						fatal("GINITADDR-Index ausserhalb Array");
					if (initAddrCount >= MAX_INITADDR) fatal("zu viele GINITADDR");
					initAddrGidx[initAddrCount] = gi;
					initAddrIdx[initAddrCount] = idx;
					initAddrSym[initAddrCount] = insP->args[2];
					initAddrCount++;
					globals[gi].hasInitAddr = 1;
					found = 1;
					break;
				}
			}
			if (!found) fatal("GINITADDR fuer unbekannte globale Variable");
			continue;
		}
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
		if (strcmp(insP->op, "GINITAT") == 0) {
			/* Feldwert eines globalen structs an einem BYTE-Offset. Der Block ist
			   ein char-Array, ein int-Feld belegt darin VIER Byte -- und in welcher
			   Reihenfolge die liegen, weiss nur das Backend. Der 68k ist
			   BIG-ENDIAN: das hoechstwertige Byte zuerst. Genau deshalb gibt es
			   den eigenen Opcode und nicht vier GINIT aus dem Frontend. */
			int foundA = 0;
			if (insP->argc != 4) fatal("ungueltiges GINITAT");
			for (gi = 0; gi < globalCount; gi++) {
				if (strcmp(globals[gi].name, insP->args[0]) == 0 && globals[gi].isArray) {
					int* initA;
					int fsz = tagSize(insP->args[2]);
					int val = number(insP->args[3], insP->line);
					int b;
					idx = number(insP->args[1], insP->line);
					if (idx < 0 || idx + fsz > globals[gi].length) fatal("GINITAT-Offset ausserhalb Objekt");
					if (idx + fsz > MAX_ARRAY_LEN) fatal("GINITAT-Offset ueberschreitet MAX_ARRAY_LEN");
					if (globals[gi].init == NULL) {
						int wantA = globals[gi].length < MAX_ARRAY_LEN ? globals[gi].length : MAX_ARRAY_LEN;
						globals[gi].init = initAlloc(wantA, insP->line);
						globals[gi].initLen = wantA;
					}
					initA = globals[gi].init;
					for (b = 0; b < fsz; b++)
						initA[idx + b] = (int)(((unsigned int)val >> (8 * (fsz - 1 - b))) & 0xffu);
					globals[gi].hasGinit = 1;
					foundA = 1;
					break;
				}
			}
			if (!foundA) fatal("GINITAT fuer unbekanntes Objekt");
			continue;
		}
		if (strcmp(insP->op, "GINITD") == 0) {
			/* Anfangswert eines globalen double: zwei 32-Bit-Haelften, hi
			   zuerst (wie PUSHD). init[] haelt EINEN int je Element, ein
			   double braucht also ZWEI Plaetze -- deshalb wird hier mit
			   length*2 alloziert und ueber 2*idx indiziert. */
			int foundD = 0;
			if (insP->argc != 4) fatal("ungueltiges GINITD");
			for (gi = 0; gi < globalCount; gi++) {
				if (strcmp(globals[gi].name, insP->args[0]) == 0 && globals[gi].isArray) {
					int* initD;
					idx = number(insP->args[1], insP->line);
					if (idx < 0 || idx >= globals[gi].length) fatal("GINITD-Index ausserhalb Array");
					if (idx >= MAX_ARRAY_LEN / 2) fatal("GINITD-Index ueberschreitet MAX_ARRAY_LEN");
					if (globals[gi].init == NULL) {
						int wantD = globals[gi].length < MAX_ARRAY_LEN / 2 ? globals[gi].length : MAX_ARRAY_LEN / 2;
						globals[gi].init = initAlloc(wantD * 2, insP->line);
						globals[gi].initLen = wantD * 2;
					}
					initD = globals[gi].init;
					initD[2 * idx] = numberU(insP->args[2], insP->line);
					initD[2 * idx + 1] = numberU(insP->args[3], insP->line);
					globals[gi].hasGinit = 1;
					foundD = 1;
					break;
				}
			}
			if (!foundD) fatal("GINITD fuer unbekanntes Array");
			continue;
		}
		if (strcmp(insP->op, "GINITQ") == 0) {
			/* long long (2026-09-23): dieselbe Zwei-Haelften-Ablage wie
			   GINITD zwei Zeilen darueber, nur ganzzahlig statt IEEE-754 --
			   die Ausgabeseite (s. unten, ".dc.l"-Emission) behandelt beide
			   ohnehin identisch, weil dort nur rohe 32-Bit-Worte geschrieben
			   werden. */
			int foundQ = 0;
			if (insP->argc != 4) fatal("ungueltiges GINITQ");
			for (gi = 0; gi < globalCount; gi++) {
				if (strcmp(globals[gi].name, insP->args[0]) == 0 && globals[gi].isArray) {
					int* initQ;
					idx = number(insP->args[1], insP->line);
					if (idx < 0 || idx >= globals[gi].length) fatal("GINITQ-Index ausserhalb Array");
					if (idx >= MAX_ARRAY_LEN / 2) fatal("GINITQ-Index ueberschreitet MAX_ARRAY_LEN");
					if (globals[gi].init == NULL) {
						int wantQ = globals[gi].length < MAX_ARRAY_LEN / 2 ? globals[gi].length : MAX_ARRAY_LEN / 2;
						globals[gi].init = initAlloc(wantQ * 2, insP->line);
						globals[gi].initLen = wantQ * 2;
					}
					initQ = globals[gi].init;
					initQ[2 * idx] = numberU(insP->args[2], insP->line);
					initQ[2 * idx + 1] = numberU(insP->args[3], insP->line);
					globals[gi].hasGinit = 1;
					foundQ = 1;
					break;
				}
			}
			if (!foundQ) fatal("GINITQ fuer unbekanntes Array");
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
		if (strcmp(insP->op, "GLOBAL") == 0 || strcmp(insP->op, "GARRAY") == 0 || strcmp(insP->op, "GINIT") == 0 || strcmp(insP->op, "GINITD") == 0 || strcmp(insP->op, "GINITQ") == 0 || strcmp(insP->op, "GINITAT") == 0 || strcmp(insP->op, "GINITADDR") == 0) {
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
			   emitIR; see mangledName()). r68/l68 have no visibility concept.
			   Viertes Argument (2026-09-23): optionales isVariadic-Flag, s.
			   Kopfkommentar bei Function.isVariadic. */
			if (open || (insP->argc != 2 && insP->argc != 3 && insP->argc != 4)) { sprintf(msg, "IR Zeile %d: ungueltiges FUNC", insP->line); fatal(msg); }
			memset(&current, 0, sizeof(current));
			strncpy(current.name, insP->args[0], NAME_LEN - 1);
			current.nargs = number(insP->args[1], insP->line);
			current.first = i + 1;
			current.last = -1;
			current.isStatic = insP->argc >= 3 && number(insP->args[2], insP->line) != 0;
			current.isVariadic = insP->argc >= 4 && number(insP->args[3], insP->line) != 0;
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
		if ((strcmp(insP->op, "CALLEXT") == 0 || strcmp(insP->op, "CALLEXTP") == 0) && (insP->argc == 3 || insP->argc == 4)) {
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
		/* Variadische Funktion (2026-09-23): AUFSTEIGEND statt absteigend --
		   s. Kopfkommentar bei Function.isVariadic. Slot 0 (erster benannter
		   Parameter) liegt dadurch IMMER bei 8(a6), unabhaengig von der
		   (variablen) Gesamtzahl der Argumente an einer konkreten Aufrufstelle;
		   VAARG setzt genau darauf auf (Slot-Index * 4 + 8). */
		if (fn->isVariadic) sprintf(out, "%d(%s)", 8 + 4 * slotN, framePtr());
		else sprintf(out, "%d(%s)", 8 + 4 * (fn->nargs - 1 - slotN), framePtr());
		return;
	}
	if (slotN >= fn->nargs + fn->locals) {
		sprintf(msg, "IR Zeile %d: Slot ausserhalb des Frames", line);
		fatal(msg);
	}
	sprintf(out, "%d(%s)", -4 * (slotN - fn->nargs + 1), framePtr());
}

/* ---------------------------------------------------------------------
 * GLEITKOMMA (2026-09-16). Der Operandenstapel IST der a7-Stapel, ein
 * double belegt dort 8 Byte. Die FPU-Register fp0/fp1 werden nur
 * INNERHALB eines Opcodes benutzt und tragen keinen Zustand darueber
 * hinaus -- deshalb braucht es hier kein fmovem zur Registerrettung,
 * anders als bei xcc, das Werte ueber Anweisungsgrenzen in fp0 haelt.
 *
 * Gerechnet wird mit .x (80 Bit intern), geladen und gespeichert mit .d
 * (64 Bit) -- genau wie xcc es tut, gemessen an dessen Ausgabe.
 * ------------------------------------------------------------------ */
static void emitFpuLoadTwo(FILE* out) {
	/* Der RECHTE Operand liegt oben: b nach fp1, a nach fp0. */
	fputs("\tfmove.d\t(a7)+,fp1\n\tfmove.d\t(a7)+,fp0\n", out);
}

static void emitFCompare(FILE* out, const char* branch, int* serial) {
	int id = (*serial)++;
	emitFpuLoadTwo(out);
	fputs("\tfcmp.x\tfp1,fp0\n", out);
	/* Dasselbe Muster wie bei emitCompare: das "moveq #0" steht NACH dem
	   bedingten Sprung. Bei der FPU zerstoert ein moveq zwar nur die
	   CPU-Flags und nicht das FPU-Statuswort, aber die Form bleibt
	   absichtlich dieselbe -- eine zweite Schreibweise waere die naechste
	   Stelle, an der jemand das Falsche kopiert. */
	fprintf(out, "\t%s\ttc_fcmp_yes_%d__%s\n\tmoveq\t#0,d0\n\tbra\ttc_fcmp_done_%d__%s\n",
	        branch, id, psectName, id, psectName);
	fprintf(out, "tc_fcmp_yes_%d__%s:\tmoveq\t#1,d0\ntc_fcmp_done_%d__%s:\tmove.l\td0,-(a7)\n",
	        id, psectName, id, psectName);
}

static void emitFpuArith(FILE* out, const char* insn) {
	emitFpuLoadTwo(out);
	fprintf(out, "\t%s.x\tfp1,fp0\n\tfmove.d\tfp0,-(a7)\n", insn);
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

// The 68000 provides MULS/DIVS only for 16-bit operands. These fixed PIC-safe
// templates therefore implement the defined QCC int32 arithmetic. They
// preserve d2-d5 for ABI compatibility and return only d0.
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

	/* long long (2026-09-23): eigene 64-Bit-Ganzzahl-Helfer, analog zu den
	   32-Bit-Routinen oben, aber mit EIGENER Aufrufkonvention -- diese drei
	   Routinen haben KEINEN anderen Aufrufer als QMUL/QDIV/QMOD weiter unten,
	   deshalb muss NICHTS fuer fremden Code preserved werden (anders als
	   tc_mul_i32 & Co, die "preserve d2-d5" beachten, weil aeltere Opcodes wie
	   IPADDN das voraussetzen). Aufrufkonvention: EIN d0=Lo/d1=Hi je 64-Bit-
	   Operand (A zuerst d0:d1, B danach d2:d3), Ergebnis in d0:d1.
	   Register a0 dient als Schleifenzaehler (movea.l/subq.l/cmpa.l -- CMPA
	   ist die einzige Vergleichsform, die auf einem Adressregister ohne
	   Umweg funktioniert; SUBQ auf An setzt laut 68000-PRM KEINE Flags).
	   a0 ist laut Kopfkommentar oben ("a0=Skalar-Scratch") frei, ebenso
	   a1/a2 -- a3/a4 (Tabellenbasen) und a5/a6 (Frame-Pointer je os9Mode)
	   werden hier bewusst NICHT beruehrt. */
	fprintf(out, "%s 68k-Core: int64 MUL/DIV fuer long long (2026-09-23)\n", fullCommentPrefix());
	fputs("tc_mul_i64:\n", out);
	/* URSPRUENGLICH mit MULU.L Dn,Dh:Dl (68020+, volles 32x32->64-Bit-
	   Produkt) entworfen -- LIVE GEFUNDEN (2026-09-23, runtests.sh M4a):
	   die Testsuite assembliert mit "vasm ... -m68000" (reiner 68000), und
	   MULU.L in der Dh:Dl-Form brach dort JEDES Programm, nicht nur
	   long-long-Code, weil diese Routine bedingungslos mitemittiert wird.
	   Ersetzt durch Schiebe-und-Addieren (dieselbe Grundidee wie
	   tc_mul_i32, nur ueber 64 statt 32 Bit, KEIN 68020-Befehl mehr) --
	   nur die unteren 64 Bit des Produkts zaehlen (C definiert
	   Ganzzahlueberlauf ohnehin nicht anders), und die sind bei
	   Zweierkomplement-Multiplikation UNABHAENGIG vom Vorzeichen der
	   Operanden dieselben wie bei vorzeichenloser Multiplikation der
	   rohen Bitmuster -- deshalb ist HIER, anders als bei der Division,
	   KEINE Vorzeichenkorrektur noetig. B(d3:d2) wird bitweise nach rechts
	   geschoben und getestet, A(d1:d0) waechst dabei parallel nach links;
	   ist das getestete Bit gesetzt, wird das aktuelle A in Q(d5:d4)
	   aufaddiert. */
	fputs("\tmoveq\t#0,d4\n\tmoveq\t#0,d5\n\tmovea.l\t#64,a0\n", out);
	fputs("tc_mul64_loop:\n\tbtst\t#0,d2\n\tbeq.s\ttc_mul64_skip\n", out);
	fputs("\tadd.l\td0,d4\n\taddx.l\td1,d5\n", out);
	fputs("tc_mul64_skip:\n\tlsr.l\t#1,d3\n\troxr.l\t#1,d2\n\tlsl.l\t#1,d0\n\troxl.l\t#1,d1\n", out);
	fputs("\tsubq.l\t#1,a0\n\tcmpa.l\t#0,a0\n\tbne.s\ttc_mul64_loop\n", out);
	fputs("\tmove.l\td4,d0\n\tmove.l\td5,d1\n\trts\n\n", out);

	fputs("tc_div_i64:\n", out);
	/* Vorzeichenbehandlung wie tc_div_i32 (Betraege bilden, Ergebnis am Ende
	   ggf. negieren) -- d6 traegt das Vorzeichenflag NUR bis zum Schleifenbeginn
	   (dort auf den Stapel gerettet, danach als Quotient-LO-Akkumulator
	   wiederverwendet: kein Registerkonflikt, weil beide Rollen sich nie
	   ueberlappen). Schulmethode ueber 64 Iterationen: A(d1:d0) wird
	   bitweise nach R(d5:d4) hineingeschoben (vierstufige lsl/roxl-Kette,
	   dieselbe Technik wie tc_div_i32s zweistufige, nur ueber vier statt
	   zwei Register), pro Bit ein Versuchsabzug von B(d3:d2) mit sub/subx
	   und Rueckbuchung bei Ausleihe (bcs) statt eines echten Zweiwortvergleichs
	   -- SUBX/ADDX kennen auf dem 68000 ohnehin nur Dn,Dn oder -(Ay),-(Ax),
	   kein Speicheroperand, B muss also in Registern bleiben. */
	fputs("\tmove.l\td3,d5\n\tor.l\td2,d5\n\tbne.s\ttc_div64_nz\n\tmoveq\t#0,d0\n\tmoveq\t#0,d1\n\trts\n", out);
	fputs("tc_div64_nz:\n\tmoveq\t#0,d6\n", out);
	fputs("\ttst.l\td1\n\tbpl.s\ttc_div64_apos\n\tneg.l\td0\n\tnegx.l\td1\n\taddq.l\t#1,d6\n", out);
	fputs("tc_div64_apos:\n\ttst.l\td3\n\tbpl.s\ttc_div64_bpos\n\tneg.l\td2\n\tnegx.l\td3\n\teor.l\t#1,d6\n", out);
	fputs("tc_div64_bpos:\n\tmove.l\td6,-(a7)\n", out);
	fputs("\tmoveq\t#0,d4\n\tmoveq\t#0,d5\n\tmoveq\t#0,d6\n\tmoveq\t#0,d7\n\tmovea.l\t#64,a0\n", out);
	fputs("tc_div64_loop:\n", out);
	fputs("\tlsl.l\t#1,d0\n\troxl.l\t#1,d1\n\troxl.l\t#1,d4\n\troxl.l\t#1,d5\n", out);
	fputs("\tlsl.l\t#1,d6\n\troxl.l\t#1,d7\n", out);
	fputs("\tsub.l\td2,d4\n\tsubx.l\td3,d5\n\tbcs.s\ttc_div64_restore\n\taddq.l\t#1,d6\n\tbra.s\ttc_div64_next\n", out);
	fputs("tc_div64_restore:\n\tadd.l\td2,d4\n\taddx.l\td3,d5\n", out);
	fputs("tc_div64_next:\n\tsubq.l\t#1,a0\n\tcmpa.l\t#0,a0\n\tbne.s\ttc_div64_loop\n", out);
	fputs("\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbeq.s\ttc_div64_ret\n\tneg.l\td6\n\tnegx.l\td7\n", out);
	fputs("tc_div64_ret:\n\tmove.l\td6,d0\n\tmove.l\td7,d1\n\trts\n\n", out);

	fputs("tc_mod_i64:\n", out);
	/* Rest = A - (A/B)*B, dieselbe Herleitung wie tc_mod_i32 -- A und B
	   ueberleben beide bsr (tc_div_i64/tc_mul_i64 preserven nichts) nur
	   dadurch, dass sie auf dem Stapel liegen statt in Registern. */
	fputs("\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n", out);
	fputs("\tbsr\ttc_div_i64\n", out);
	fputs("\tmove.l\td0,d4\n\tmove.l\td1,d5\n", out);
	fputs("\tmove.l\t4(a7),d2\n\tmove.l\t0(a7),d3\n", out);
	fputs("\tmove.l\td4,d0\n\tmove.l\td5,d1\n\tbsr\ttc_mul_i64\n", out);
	fputs("\tmove.l\t12(a7),d2\n\tmove.l\t8(a7),d3\n", out);
	fputs("\tsub.l\td0,d2\n\tsubx.l\td1,d3\n", out);
	fputs("\tmove.l\td2,d0\n\tmove.l\td3,d1\n\tlea\t16(a7),a7\n\trts\n\n", out);
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
	} else if (strcmp(op, "VAREVERSE") == 0 && insP->argc == 1) {
		/* Eigene variadische Funktionsdefinitionen (2026-09-23). Dreht die
		   obersten n bereits gepushten 4-Byte-Slots komplett um -- n ist
		   eine literale Konstante aus der IR (die Argumentzahl DIESES
		   Aufrufs), deshalb reicht eine zur Compilezeit abgerollte Folge von
		   floor(n/2) Vertauschungen ohne Laufzeitschleife. S. Kopfkommentar
		   bei tc_call/VAREVERSE in qcc.lextab fuer die Begruendung, warum
		   das noetig ist (Layout, das slotAddress() fuer eine variadische
		   Funktion erwartet). */
		int n = number(insP->args[0], insP->line);
		int vi;
		for (vi = 0; vi < n / 2; vi++) {
			int vj = n - 1 - vi;
			fprintf(out, "\tmove.l\t%d(a7),d0\n\tmove.l\t%d(a7),d1\n\tmove.l\td0,%d(a7)\n\tmove.l\td1,%d(a7)\n",
				vi * 4, vj * 4, vj * 4, vi * 4);
		}
	} else if (strcmp(op, "VASTART") == 0 && insP->argc == 2) {
		/* va_start(ap, last): ap wird intern als SLOT-INDEX gefuehrt (nicht
		   als Byteadresse) -- derselbe Slot-Index wie VAARG unten via
		   slotAddress() in eine echte Adresse uebersetzt. "last" ist der
		   letzte benannte Parameter (Slot last), der erste variadische Wert
		   liegt dank VAREVERSE+slotAddress()-Umkehrung IMMER bei Slot
		   last+1. Beide Werte (apSlot, lastSlot) sind literale Konstanten
		   aus der IR. */
		int apSlot = number(insP->args[0], insP->line);
		int lastSlot = number(insP->args[1], insP->line);
		slotAddress(addrBuf, apSlot, fn, insP->line);
		fprintf(out, "\tmove.l\t#%d,%s\n", lastSlot + 1, addrBuf);
	} else if (strcmp(op, "VAARG") == 0 && insP->argc == 2) {
		/* va_arg(ap, type): d0 = aktueller Slot-Index aus ap, a0 = dessen
		   ECHTE Adresse (8+4*Index(a6) -- exakt die Formel, die
		   slotAddress() fuer eine variadische Funktion fuer Slots < nargs
		   ansetzt, hier aber per LAUFZEIT-Register statt Literal, weil der
		   Slot-Index selbst zur Laufzeit waechst). 'd' (double) braucht eine
		   ZWEITE Indirektion: der Slot haelt nur die vom Aufrufer geboxte
		   ADRESSE (dieselbe Konvention wie bei einem echten double-
		   Parameter, s. tc_funcbodybegin/tc_arg) -- alle anderen Tags
		   liegen direkt als 4-Byte-Wert im Slot. Danach ap um 1 (einen
		   Slot, NICHT 4 Byte) weiterruecken. */
		int apSlot = number(insP->args[0], insP->line);
		char tag = insP->args[1][0];
		slotAddress(addrBuf, apSlot, fn, insP->line);
		fprintf(out, "\tmove.l\t%s,d0\n\tlea\t8(%s),a0\n\tlsl.l\t#2,d0\n\tadda.l\td0,a0\n",
			addrBuf, framePtr());
		if (tag == 'd') {
			fprintf(out, "\tmove.l\t(a0),a0\n\tfmove.d\t(a0),fp0\n\tfmove.d\tfp0,-(a7)\n");
		} else {
			fprintf(out, "\tmove.l\t(a0),-(a7)\n");
		}
		fprintf(out, "\tmove.l\t%s,d0\n\taddq.l\t#1,d0\n\tmove.l\td0,%s\n", addrBuf, addrBuf);
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
		/* 'd' (2026-09-16): ein Array von double war hier NICHT vorgesehen --
		   "double a[3]; a[1]=2.5;" brach mit "unbekannter Arraytyp" ab, auf
		   BEIDEN Zielen. Nur das VM-Orakel kannte den Fall. Die Skalierung
		   stimmte schon (tagShift kennt 8 -> 3), es fehlten die Typzulassung
		   und die FPU-Befehle fuer acht Byte. */
		int isD = strcmp(insP->args[2], "d") == 0;
		/* 'q' (long long, 2026-09-23): dieselbe Notwendigkeit wie 'd' zwei
		   Zeilen darueber -- OHNE eigenen Pfad fiele ein Array von long long
		   entweder auf "unbekannter Arraytyp" (Typzulassung fehlt) oder,
		   schlimmer, wuerde ueber fmove.d bewegt: das ist eine ECHTE IEEE-
		   Formatkonversion (double <-> 80-Bit erweitert), keine reine
		   Byteverschiebung -- fuer ein beliebiges 64-Bit-Ganzzahl-Bitmuster
		   (kein gueltiges/normalisiertes double) nicht verlaesslich
		   bitidentisch. Deshalb ein EIGENER, rein ganzzahliger Pfad mit
		   move.l-Paaren statt fmove.d. d2/d3 (statt d0/d1) halten HI/LO ueber
		   die Adressberechnung hinweg, die selbst nur a0/d1 anfasst. */
		int isQ = strcmp(insP->args[2], "q") == 0;
		if (strcmp(insP->args[2], "i") != 0 && strcmp(insP->args[2], "p") != 0 &&
		    strcmp(insP->args[2], "h") != 0 && strcmp(insP->args[2], "c") != 0 &&
		    strcmp(insP->args[2], "b") != 0 && !isD && !isQ) fatal("unbekannter Arraytyp");
		if (strcmp(op, "STOREIDX") == 0 || keepValue) {
			if (isD) fputs("\tfmove.d\t(a7)+,fp0\n", out);
			else if (isQ) fputs("\tmove.l\t(a7)+,d2\n\tmove.l\t(a7)+,d3\n", out);
			else fputs("\tmove.l\t(a7)+,d0\n", out);
		}
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
			if (isD) fputs("\tfmove.d\t(a0),fp0\n\tfmove.d\tfp0,-(a7)\n", out);
			else if (isQ) fputs("\tmove.l\t(a0),d0\n\tmove.l\t4(a0),d1\n\tmove.l\td1,-(a7)\n\tmove.l\td0,-(a7)\n", out);
			else {
				if (elemSize == 4) fputs("\tmove.l\t(a0),d0\n", out);
				else fprintf(out, "\tmoveq\t#0,d0\n\tmove.%c\t(a0),d0\n", tagSuffix(elemSize));
				fputs("\tmove.l\td0,-(a7)\n", out);
			}
		} else if (isD) {
			fputs("\tfmove.d\tfp0,(a0)\n", out);
			if (keepValue) fputs("\tfmove.d\tfp0,-(a7)\n", out);
		} else if (isQ) {
			fputs("\tmove.l\td2,(a0)\n\tmove.l\td3,4(a0)\n", out);
			if (keepValue) fputs("\tmove.l\td3,-(a7)\n\tmove.l\td2,-(a7)\n", out);
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
		if (largeDataMode || globalInDataArea(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmove.l\t(a0),-(a7)\n", out); }
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
		if (largeDataMode || globalInDataArea(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmoveq\t#0,d0\n\tmove.b\t(a0),d0\n\tmove.l\td0,-(a7)\n", out); }
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
		if (largeDataMode || globalInDataArea(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmoveq\t#0,d0\n\tmove.w\t(a0),d0\n\tmove.l\td0,-(a7)\n", out); }
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
			if (largeDataMode || globalInDataArea(gidx)) { emitLeaGlobal(out, gidx, "a0"); fputs("\tmove.l\t(a0),-(a7)\n", out); }
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
			if (elemSize == 8 && strcmp(insP->args[0], "q") == 0) {
				/* long long UEBER EINEN ZEIGER (2026-09-23): dieselbe
				   Notwendigkeit wie beim double-Zweig direkt darunter, aber
				   ueber move.l-Paare statt fmove.d -- eine beliebige 64-Bit-
				   Ganzzahl ist kein gueltiges IEEE-double-Bitmuster, dem die
				   FPU beim reinen Durchreichen (ohne jede Arithmetik) etwas
				   antun duerfte (Rundung/Quieting eines sNaN-aehnlichen
				   Musters). Reine Byteverschiebung ist hier die sichere Wahl. */
				fputs("\tmove.l\t(a0),d0\n\tmove.l\t4(a0),d1\n\tmove.l\td1,-(a7)\n\tmove.l\td0,-(a7)\n", out);
			} else if (elemSize == 8) {
				/* DOUBLE UEBER EINEN ZEIGER (2026-09-16). Acht Byte gehen wie
				   bei LOADD/LOADGD ueber die FPU. Vorher fiel 'd' in den
				   generischen Zweig darunter und lud VIER Byte, waehrend das
				   nachfolgende "fmove.d (a7)+" acht las -- ein halber Wert und
				   ein schiefer Stapel, ohne jede Meldung. Im VM-Orakel war das
				   unsichtbar, weil Python den Typ kennt; aufgefallen ist es
				   erst auf echter Hardware (zwei double-Parameter, dann PMMU). */
				fputs("\tfmove.d\t(a0),fp0\n\tfmove.d\tfp0,-(a7)\n", out);
			} else {
				if (elemSize == 4) fputs("\tmove.l\t(a0),d0\n", out);
				else fprintf(out, "\tmoveq\t#0,d0\n\tmove.%c\t(a0),d0\n", tagSuffix(elemSize));
				fputs("\tmove.l\td0,-(a7)\n", out);
			}
		} else if (elemSize == 8 && strcmp(insP->args[0], "q") == 0) {
			/* Der Wert liegt OBEN (zwei Langworte, HI dann LO Richtung
			   Stapelboden), die Adresse darunter -- s. Kommentar im
			   LOADIND-Zweig oben, gleicher Grund fuer move.l statt fmove.d. */
			fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,a0\n\tmove.l\td0,(a0)\n\tmove.l\td1,4(a0)\n", out);
			if (keepValue) fputs("\tmove.l\td1,-(a7)\n\tmove.l\td0,-(a7)\n", out);
		} else if (elemSize == 8) {
			/* Der Wert liegt OBEN (acht Byte), die Adresse darunter. */
			fputs("\tfmove.d\t(a7)+,fp0\n\tmove.l\t(a7)+,a0\n\tfmove.d\tfp0,(a0)\n", out);
			if (keepValue) fputs("\tfmove.d\tfp0,-(a7)\n", out);
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
	} else if (strcmp(op, "BFLOAD") == 0 && insP->argc == 3) {
		/* Bitfeld lesen -- native 68020-Befehle, byteidentisch zu dem, was
		   ein echter Microware-xcc fuer "unsigned int a:3;" & Co erzeugt
		   (gemessen per "xcc -e=be", s. docs/FLOAT_PLAN_de.md-Notiz und den
		   tcStructFieldBitWidth-Kommentar im Frontend). args[0]=Bit-Offset
		   ab MSB, args[1]=Breite, args[2]="1"=signed (bfexts) / "0"=unsigned
		   (bfextu). Adresse liegt oben auf dem Stapel. */
		fputs("\tmove.l\t(a7)+,a0\n", out);
		fprintf(out, "\tbfext%s\t(a0){%s:%s},d0\n", strcmp(insP->args[2], "1") == 0 ? "s" : "u", insP->args[0], insP->args[1]);
		fputs("\tmove.l\td0,-(a7)\n", out);
	} else if ((strcmp(op, "BFSTORE") == 0 || strcmp(op, "BFSTOREKEEP") == 0) && insP->argc == 3) {
		/* Bitfeld schreiben. Wert liegt oben, Adresse darunter (wie bei
		   STOREIND). bfins nimmt den einzufuegenden Wert aus einem
		   Datenregister -- die oberen, ausserhalb der Breite liegenden Bits
		   von d0 ignoriert es von selbst. Fuer die KEEP-Variante wird der
		   Wert danach per bfext[u|s] aus dem Speicher zurueckgelesen, damit
		   der auf dem Stapel verbleibende Wert exakt dem entspricht, was ein
		   nachfolgendes Lesen des Feldes liefern wuerde (maskiert bzw.
		   vorzeichenerweitert), statt des rohen, unmaskierten Eingabewerts. */
		int keep = strcmp(op, "BFSTOREKEEP") == 0;
		fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,a0\n", out);
		fprintf(out, "\tbfins\td0,(a0){%s:%s}\n", insP->args[0], insP->args[1]);
		if (keep) {
			fprintf(out, "\tbfext%s\t(a0){%s:%s},d0\n", strcmp(insP->args[2], "1") == 0 ? "s" : "u", insP->args[0], insP->args[1]);
			fputs("\tmove.l\td0,-(a7)\n", out);
		}
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
		/* long long (2026-09-23): KEIN Tabelleneintrag fuer tc_mul_i64/
		   tc_div_i64/tc_mod_i64 -- s. Kommentar bei helperTableOffset(). */
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
		/* IMPORTANT (2026-07-26, see the tc_functab comment above): here too,
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
			if (globalInDataArea(gi)) fprintf(out, "\tdc.l\t0\n");
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
		/* IMPORTANT (2026-07-26, found live on Q9 -- the fourth and deepest
		   -largedata bug in this session): a3/a4 were previously initialized ONLY
		   at program start (main:) and refreshed after each CALLEXT/CALLEXTP (see
		   the emitCall()/emitLeaGlobal() comments). That is NOT sufficient when a
		   function is called through FUNCDECL from ANOTHER file: each file has its
		   OWN tc_functab/tc_gadata. Reproduction: file A calls a function defined in
		   file B, so a4 still points to file A's table, and THAT function calls a
		   third function internally
		   (for example runtime helper tc_putint) through emitCall(). That internal
		   call then INCORRECTLY continues using file A's table because a4 was never
		   switched to file B's OWN table --
		   the table offset itself is correct, but it points into the WRONG table
		   and therefore calls a completely different function at the same index.
		   In the minimal reproduction, putint(99) silently called tc_div_i32.
		   Larger ebnf.tc+codegen.tc builds instead crashed with a PMMU error when
		   a wrong table entry was interpreted as an address. FIX: every function,
		   not only main, refreshes a3/a4 to its OWN table immediately after link,
		   regardless of the caller's file. This costs two instructions per
		   function but guarantees correctness. A direct lea of the distant table
		   would reintroduce the 68000 32 KB limit, so emitTableBases() first loads
		   the function's own nearby PC-relative address and then adds the link-time
		   32-bit difference to each table base. */
		/* a3/a4 are initialized at program entry and are not changed by internal
		   QCC functions. External calls use wrappers that restore these ABI scratch
		   registers. */
		/* BIG-ENDIAN FIX FOR char PARAMETERS (2026-08-10, found in the
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
		   copy the low byte to the slot base. All existing byte-access paths
		   (LOADC, STOREC, and ADDRL+LOADIND/STOREIND) then remain unchanged, just
		   as for local char slots. Only parameters actually used byte-wise in the
		   body (LOADC/STOREC on their slot) are adjusted; int and pointer parameters
		   remain untouched. A known limitation is that a char parameter whose address
		   is taken with ADDRL but never accessed through LOADC/STOREC cannot be
		   recognized: the IR carries no parameter types, and ADDRL alone does not
		   prove char (narrowing an int parameter would be wrong). The real generator
		   does not currently produce this case. */
		{
			int pslot;
			int pk;
			for (pslot = 0; pslot < fn->nargs; pslot++) {
				int usedAsChar = 0, usedAsShort = 0;
				int off;
				for (pk = fn->first; pk < fn->last; pk++) {
					Instr* px = &ir[pk];
					/* The opcode-name comparison MUST precede number() (short-circuit
					   evaluation); otherwise number() would process every one-argument
					   instruction, including "LABEL L0" and "JMP L2", whose argument is
					   not numeric. This broke the two-opcode-pair change (2026-09-09):
					   number() ran first and the next self-hosting run failed with
					   "IR line N: number expected: L0" (SourceQCC/ebnf.tc, tcCopyBounded). */
					if (px->argc == 1) {
						if ((strcmp(px->op, "LOADC") == 0 || strcmp(px->op, "STOREC") == 0) &&
						    number(px->args[0], px->line) == pslot) usedAsChar = 1;
						else if ((strcmp(px->op, "LOADLH") == 0 || strcmp(px->op, "STORELH") == 0) &&
						         number(px->args[0], px->line) == pslot) usedAsShort = 1;
					}
				}
				/* s. slotAddress()/Function.isVariadic fuer die Begruendung der
				   umgekehrten Richtung bei einer variadischen Funktion. */
				off = fn->isVariadic ? 8 + 4 * pslot : 8 + 4 * (fn->nargs - 1 - pslot);
				if (usedAsChar) fprintf(out, "\tmove.b\t%d(%s),%d(%s)\n", off + 3, framePtr(), off, framePtr());
				else if (usedAsShort) fprintf(out, "\tmove.w\t%d(%s),%d(%s)\n", off + 2, framePtr(), off, framePtr());
			}
		}
		for (k = fn->first; k < fn->last; k++) {
			Instr* insP = &ir[k];
			const char* op = insP->op;

			if (emitDataOp(out, op, insP, fn, &serial)) {
				/* See emitDataOp(): PUSH..PDIFF were moved out because of branch
				   distance (2026-09-09). */
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
			} else if (strcmp(op, "DSWAP") == 0) {
				/* Tauscht das oberste double (ACHT Byte) mit dem 4-Byte-Wert
				   darunter -- einer Adresse oder einem Index. Gebraucht fuer
				   "s.d++", "a[i]++" und "(*p)++": dort muss der ALTE Wert als
				   Ergebnis unter der Adresse liegen bleiben. SWAP kann das
				   nicht, es tauscht zwei Langworte und zerrisse den double. */
				fputs("\tfmove.d\t(a7)+,fp0\n\tmove.l\t(a7)+,d1\n\tfmove.d\tfp0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "SWAP") == 0) {
				/* Swap the top two stack elements. Needed wherever a result must remain
				   UNDER an address: "(*p)++", "a[i]++", and chained assignment all
				   failed because the stack could not previously be reordered (only DUP
				   existed). */
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
			/* --- Gleitkomma (2026-09-16) --- */
			} else if (strcmp(op, "PUSHD") == 0 && insP->argc == 2) {
				/* Zwei 32-Bit-Haelften, hi zuerst im Speicher: also lo
				   zuerst pushen. Als Hex, weil eine Haelfte groesser als
				   2^31 sein kann und r68 dezimal nur bis dahin liest. */
				unsigned long hi = strtoul(insP->args[0], 0, 10);
				unsigned long lo = strtoul(insP->args[1], 0, 10);
				fprintf(out, "\tmove.l\t#$%08lX,-(a7)\n\tmove.l\t#$%08lX,-(a7)\n", lo, hi);
			} else if (strcmp(op, "LOADD") == 0 && insP->argc == 1) {
				int ignored;
				int off = arrayOffset(fn, number(insP->args[0], insP->line), &ignored, insP->line);
				fprintf(out, "\tlea\t-%d(%s),a0\n\tfmove.d\t(a0),fp0\n\tfmove.d\tfp0,-(a7)\n", off, framePtr());
			} else if (strcmp(op, "STORED") == 0 && insP->argc == 1) {
				int ignored;
				int off = arrayOffset(fn, number(insP->args[0], insP->line), &ignored, insP->line);
				fprintf(out, "\tfmove.d\t(a7)+,fp0\n\tlea\t-%d(%s),a0\n\tfmove.d\tfp0,(a0)\n", off, framePtr());
			} else if (strcmp(op, "LOADGD") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]);
				char gmsg[200];
				if (gidx < 0) { sprintf(gmsg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(gmsg); }
				/* Immer ueber die Adresse, nie PC-relativ: die PC-relative
				   Form der FPU-Befehle ist ungemessen (s. tests/fpu.a). */
				emitLeaGlobal(out, gidx, "a0");
				fputs("\tfmove.d\t(a0),fp0\n\tfmove.d\tfp0,-(a7)\n", out);
			} else if (strcmp(op, "STOREGD") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]);
				char gmsg[200];
				if (gidx < 0) { sprintf(gmsg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(gmsg); }
				emitLeaGlobal(out, gidx, "a0");
				fputs("\tfmove.d\t(a7)+,fp0\n\tfmove.d\tfp0,(a0)\n", out);
			} else if (strcmp(op, "DADD") == 0) { emitFpuArith(out, "fadd");
			} else if (strcmp(op, "DSUB") == 0) { emitFpuArith(out, "fsub");
			} else if (strcmp(op, "DMUL") == 0) { emitFpuArith(out, "fmul");
			} else if (strcmp(op, "DDIV") == 0) { emitFpuArith(out, "fdiv");
			} else if (strcmp(op, "DNEG") == 0) {
				fputs("\tfmove.d\t(a7)+,fp0\n\tfneg.x\tfp0,fp0\n\tfmove.d\tfp0,-(a7)\n", out);
			} else if (strcmp(op, "I2D") == 0) {
				fputs("\tmove.l\t(a7)+,d0\n\tfmove.l\td0,fp0\n\tfmove.d\tfp0,-(a7)\n", out);
			} else if (strcmp(op, "I2DUNDER") == 0) {
				/* Die Ganzzahl liegt UNTER dem double: beides herunter,
				   umwandeln, in derselben Reihenfolge zurueck. */
				fputs("\tfmove.d\t(a7)+,fp0\n\tmove.l\t(a7)+,d0\n"
				      "\tfmove.l\td0,fp1\n\tfmove.d\tfp1,-(a7)\n\tfmove.d\tfp0,-(a7)\n", out);
			} else if (strcmp(op, "D2I") == 0) {
				/* fintrz schneidet Richtung null ab -- genau die C-Regel. */
				fputs("\tfmove.d\t(a7)+,fp0\n\tfintrz.x\tfp0,fp0\n\tfmove.l\tfp0,d0\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "DDUP") == 0) {
				fputs("\tmove.l\t4(a7),d0\n\tmove.l\t(a7),d1\n\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "DDROP") == 0) {
				fputs("\tlea\t8(a7),a7\n", out);
			} else if (strcmp(op, "DCMPLT") == 0) { emitFCompare(out, "fblt", &serial);
			} else if (strcmp(op, "DCMPGT") == 0) { emitFCompare(out, "fbgt", &serial);
			} else if (strcmp(op, "DCMPLE") == 0) { emitFCompare(out, "fble", &serial);
			} else if (strcmp(op, "DCMPGE") == 0) { emitFCompare(out, "fbge", &serial);
			} else if (strcmp(op, "DCMPEQ") == 0) { emitFCompare(out, "fbeq", &serial);
			} else if (strcmp(op, "DCMPNE") == 0) { emitFCompare(out, "fbne", &serial);
			/* --- long long (2026-09-23) ---
			   Stapel-Konvention wie bei double: HI liegt nach dem Push OBEN,
			   LO darunter (grosses Endian, dieselbe Reihenfolge wie PUSHD --
			   move.l lo zuerst, dann move.l hi). DDUP/DDROP/DSWAP werden
			   UNVERAENDERT mitbenutzt (reine Byteverschiebung, kein
			   IEEE-Formatwechsel -- deshalb an KEINER Stelle hier erwaehnt).
			   KEIN fmove.d irgendwo: eine beliebige 64-Bit-Ganzzahl ist kein
			   gueltiges double-Bitmuster, dem die FPU beim reinen
			   Durchreichen (ohne Arithmetik) etwas antun duerfte. */
			} else if (strcmp(op, "PUSHQ") == 0 && insP->argc == 2) {
				unsigned long hi = strtoul(insP->args[0], 0, 10);
				unsigned long lo = strtoul(insP->args[1], 0, 10);
				fprintf(out, "\tmove.l\t#$%08lX,-(a7)\n\tmove.l\t#$%08lX,-(a7)\n", lo, hi);
			} else if (strcmp(op, "LOADQ") == 0 && insP->argc == 1) {
				int ignored;
				int off = arrayOffset(fn, number(insP->args[0], insP->line), &ignored, insP->line);
				fprintf(out, "\tmove.l\t-%d(%s),d0\n\tmove.l\t-%d(%s),d1\n\tmove.l\td1,-(a7)\n\tmove.l\td0,-(a7)\n",
				        off, framePtr(), off - 4, framePtr());
			} else if (strcmp(op, "STOREQ") == 0 && insP->argc == 1) {
				int ignored;
				int off = arrayOffset(fn, number(insP->args[0], insP->line), &ignored, insP->line);
				fprintf(out, "\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d1\n\tmove.l\td0,-%d(%s)\n\tmove.l\td1,-%d(%s)\n",
				        off, framePtr(), off - 4, framePtr());
			} else if (strcmp(op, "LOADGQ") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]);
				char gmsg[200];
				if (gidx < 0) { sprintf(gmsg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(gmsg); }
				emitLeaGlobal(out, gidx, "a0");
				fputs("\tmove.l\t(a0),d0\n\tmove.l\t4(a0),d1\n\tmove.l\td1,-(a7)\n\tmove.l\td0,-(a7)\n", out);
			} else if (strcmp(op, "STOREGQ") == 0 && insP->argc == 1) {
				int gidx = findGlobal(insP->args[0]);
				char gmsg[200];
				if (gidx < 0) { sprintf(gmsg, "IR Zeile %d: unbekannte globale Variable %s", insP->line, insP->args[0]); fatal(gmsg); }
				emitLeaGlobal(out, gidx, "a0");
				fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d1\n\tmove.l\td0,(a0)\n\tmove.l\td1,4(a0)\n", out);
			} else if (strcmp(op, "QADD") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d2\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d0\n"
				      "\tadd.l\td2,d0\n\taddx.l\td1,d3\n\tmove.l\td0,-(a7)\n\tmove.l\td3,-(a7)\n", out);
			} else if (strcmp(op, "QSUB") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d2\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d0\n"
				      "\tsub.l\td2,d0\n\tsubx.l\td1,d3\n\tmove.l\td0,-(a7)\n\tmove.l\td3,-(a7)\n", out);
			} else if (strcmp(op, "QNEG") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tneg.l\td0\n\tnegx.l\td1\n\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "QNOT") == 0) {
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tnot.l\td0\n\tnot.l\td1\n\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "QAND") == 0 || strcmp(op, "QOR") == 0 || strcmp(op, "QXOR") == 0) {
				const char* mnem = strcmp(op, "QAND") == 0 ? "and" : strcmp(op, "QOR") == 0 ? "or" : "eor";
				fprintf(out, "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d2\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d0\n"
				             "\t%s.l\td2,d0\n\t%s.l\td1,d3\n\tmove.l\td0,-(a7)\n\tmove.l\td3,-(a7)\n", mnem, mnem);
			} else if (strcmp(op, "QMUL") == 0 || strcmp(op, "QDIV") == 0 || strcmp(op, "QMOD") == 0) {
				const char* fn2 = strcmp(op, "QMUL") == 0 ? "mul_i64" : strcmp(op, "QDIV") == 0 ? "div_i64" : "mod_i64";
				char helperAsmName[24];
				sprintf(helperAsmName, "tc_%s", fn2);
				/* Aufrufkonvention der eigenen 64-Bit-Helfer (s. emitM68kCore):
				   d0=A_lo,d1=A_hi,d2=B_lo,d3=B_hi, Ergebnis in d0:d1. B liegt
				   beim Emittieren oben (zuletzt gepusht), also zuerst gepoppt.
				   BEWUSST PLAIN "bsr", NICHT emitCall()/-largedata-Tabelle
				   (2026-09-23, live gefunden): irgendetwas an der Tabellen-
				   Indirektion fuehrte bei "-largedata" zu einem FALSCHEN
				   Sprungziel (landete mitten in tc_mul_i64 statt an dessen
				   Anfang) -- Ursache nicht abschliessend geklärt (vermutlich
				   ein Zusammenspiel mit qcc68sim.pys Label-Adressvergabe, s.
				   dortigen Kommentar zu global_addresses), Risiko aber zu
				   hoch fuer eine ungeprüfte Vermutung. tc_mul_i64/tc_div_i64/
				   tc_mod_i64 stehen als Teil von emitM68kCore IMMER nahe am
				   Programmanfang; ein reines bsr bleibt daher auch in
				   -largedata-Programmen in Reichweite, ausser die
				   AUFRUFSTELLE selbst liegt weit hinter der 32-KB-Grenze --
				   dann meldet r68 das klar als "value out of range" statt
				   still falsch zu rechnen. */
				fputs("\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n", out);
				fprintf(out, "\tbsr\t%s\n", helperAsmName);
				fputs("\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "QSHL") == 0 || strcmp(op, "QSHR") == 0) {
				int id = serial++;
				const char* body = strcmp(op, "QSHL") == 0
					? "\tlsl.l\t#1,d0\n\troxl.l\t#1,d1\n"
					: "\tasr.l\t#1,d1\n\troxr.l\t#1,d0\n";
				/* Variable Schiebeweite: der 68k kennt kein natives 64-Bit-
				   Schieben, deshalb bitweise in einer Schleife -- dieselbe
				   Grund-Idee wie die Divisionsschleife oben, nur einstufig
				   (kein Versuchsabzug). Zaehler bleibt ein PLAIN int (kein
				   I2Q auf der rechten Seite, s. tcShiftEnd im Frontend). */
				fputs("\tmove.l\t(a7)+,d2\n\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n", out);
				fprintf(out, "tc_qshift_loop_%d__%s:\n\ttst.l\td2\n\tbeq.s\ttc_qshift_done_%d__%s\n", id, psectName, id, psectName);
				fputs(body, out);
				fprintf(out, "\tsubq.l\t#1,d2\n\tbra.s\ttc_qshift_loop_%d__%s\n", id, psectName);
				fprintf(out, "tc_qshift_done_%d__%s:\n\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", id, psectName);
			} else if (strcmp(op, "QCMPEQ") == 0 || strcmp(op, "QCMPNE") == 0 || strcmp(op, "QCMPLT") == 0 ||
			           strcmp(op, "QCMPLE") == 0 || strcmp(op, "QCMPGT") == 0 || strcmp(op, "QCMPGE") == 0) {
				/* 64-Bit-Vergleich: erst HI vergleichen, nur bei Gleichheit
				   LO entscheiden -- der klassische Zweiwortvergleich (nicht
				   ueber eine Subtraktion, die bei extremen Werten selbst
				   ueberliefe und das Vorzeichen des Ergebnisses verfaelschen
				   koennte). d1/d0 = B_hi/B_lo (oben, zuletzt gepusht),
				   d3/d2 = A_hi/A_lo. cmp.l ist SIGNED fuer die HI-Haelfte
				   (long long ist vorzeichenbehaftet, s. tc_longlong-Kopf),
				   aber UNSIGNED fuer die LO-Haelfte (die unteren 32 Bit
				   tragen kein eigenes Vorzeichen) -- deshalb blt/bgt fuer
				   HI, aber bcs/bhi (unsigned) fuer LO. */
				int id = serial++;
				const char* hiLt = "blt"; const char* hiGt = "bgt";
				const char* loLt = "bcs"; const char* loGt = "bhi";
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n", out);
				fputs("\tcmp.l\td1,d3\n", out);
				fprintf(out, "\t%s\ttc_qcmp_lt_%d__%s\n\t%s\ttc_qcmp_gt_%d__%s\n", hiLt, id, psectName, hiGt, id, psectName);
				fputs("\tcmp.l\td0,d2\n", out);
				fprintf(out, "\t%s\ttc_qcmp_lt_%d__%s\n\t%s\ttc_qcmp_gt_%d__%s\n", loLt, id, psectName, loGt, id, psectName);
				fprintf(out, "\tbra\ttc_qcmp_eq_%d__%s\n", id, psectName);
				fprintf(out, "tc_qcmp_lt_%d__%s:\tmoveq\t#%d,d0\n\tbra\ttc_qcmp_done_%d__%s\n",
				        id, psectName, strcmp(op, "QCMPLT") == 0 || strcmp(op, "QCMPLE") == 0 || strcmp(op, "QCMPNE") == 0 ? 1 : 0, id, psectName);
				fprintf(out, "tc_qcmp_gt_%d__%s:\tmoveq\t#%d,d0\n\tbra\ttc_qcmp_done_%d__%s\n",
				        id, psectName, strcmp(op, "QCMPGT") == 0 || strcmp(op, "QCMPGE") == 0 || strcmp(op, "QCMPNE") == 0 ? 1 : 0, id, psectName);
				fprintf(out, "tc_qcmp_eq_%d__%s:\tmoveq\t#%d,d0\n",
				        id, psectName, strcmp(op, "QCMPEQ") == 0 || strcmp(op, "QCMPLE") == 0 || strcmp(op, "QCMPGE") == 0 ? 1 : 0);
				fprintf(out, "tc_qcmp_done_%d__%s:\tmove.l\td0,-(a7)\n", id, psectName);
			} else if (strcmp(op, "I2Q") == 0) {
				/* Vorzeichenrichtige Erweiterung 32->64: ext.l gibt es fuer
				   Langwort->Langwort nicht (nur byte/word->long) -- die
				   Standardform ist "sign in d1 durch Vergleich mit 0". */
				fputs("\tmove.l\t(a7)+,d0\n\tmoveq\t#0,d1\n\ttst.l\td0\n\tbpl.s\ttc_i2q_pos\n\tmoveq\t#-1,d1\n"
				      "tc_i2q_pos:\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "I2QUNDER") == 0) {
				/* Die Ganzzahl liegt UNTER dem long long (oben acht, macht
				   zwoelf Byte insgesamt) -- beides herunter, umwandeln, in
				   derselben Reihenfolge zurueck, wie bei I2DUNDER. */
				fputs("\tmove.l\t(a7)+,d2\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d0\n\tmoveq\t#0,d1\n\ttst.l\td0\n\tbpl.s\ttc_i2qu_pos\n\tmoveq\t#-1,d1\n"
				      "tc_i2qu_pos:\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td2,-(a7)\n", out);
			} else if (strcmp(op, "Q2I") == 0) {
				/* Untere 32 Bit -- wie D2I "ohne Rundung", hier ohnehin
				   verlustfrei innerhalb dieser 32 Bit. */
				fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "D2Q") == 0) {
				/* double -> long long: Richtung null abschneiden (fintrz),
				   dann das 32-Bit-Ergebnis vorzeichenrichtig auf 64 erweitern
				   -- fmove.l liefert nur 32 Bit, exakt wie bei D2I. */
				fputs("\tfmove.d\t(a7)+,fp0\n\tfintrz.x\tfp0,fp0\n\tfmove.l\tfp0,d0\n\tmoveq\t#0,d1\n\ttst.l\td0\n\tbpl.s\ttc_d2q_pos\n\tmoveq\t#-1,d1\n"
				      "tc_d2q_pos:\tmove.l\td0,-(a7)\n\tmove.l\td1,-(a7)\n", out);
			} else if (strcmp(op, "Q2D") == 0) {
				/* long long -> double: 64-Bit-Ganzzahl hat keine direkte
				   FPU-Ladeinstruktion -- ueber zwei fmove.l und eine Skalierung
				   (hi * 2^32 + lo). fp1 = hi, hoch skaliert per fmul, dann lo
				   dazu; lo muss dafuer als UNSIGNED behandelt werden (seine
				   oberen 32 Bit tragen kein eigenes Vorzeichen), deshalb der
				   Unsigned-Ausgleich mit 2^32, falls d0 negativ (als int32)
				   erscheint. */
				fputs("\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n", out);
				fputs("\tfmove.l\td1,fp0\n\tfmove.d\t#4294967296.0,fp2\n\tfmul.x\tfp2,fp0\n", out);
				fputs("\tfmove.l\td0,fp1\n\ttst.l\td0\n\tbpl.s\ttc_q2d_lopos\n\tfadd.x\tfp2,fp1\n", out);
				fputs("tc_q2d_lopos:\tfadd.x\tfp1,fp0\n\tfmove.d\tfp0,-(a7)\n", out);
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
				/* The psect suffix is needed for the same reason as in emitCompare:
				   LABEL names (tc_L0, tc_L1, ...) come from the QCC frontend's local
				   numbering, which restarts at 0 in EVERY file. Without the suffix,
				   labels collide during multi-file linking when two files contain
				   control flow (if/while/for/...; practically always). */
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
				/* Push the address of a QCC function as a value (function pointer).
				   Im -largedata-Modus liegt sie NICHT als Symbol vor, sondern als
				   Link-Zeit-Offset in der Funktionsindirektionstabelle: die echte
				   Laufzeitadresse ist a4 + *(a4 + index*4) -- exakt dieselbe Rechnung,
				   die emitCall() fuer den direkten Aufruf macht (siehe dort). */
				int fnIdx = findFunction(insP->args[0]);
				char asmName[NAME_LEN + 40];
				if (fnIdx < 0) { sprintf(msg, "IR Zeile %d: unbekannte Funktion %s", insP->line, insP->args[0]); fatal(msg); }
				mangledName(asmName, "tc_", insP->args[0], funcs[fnIdx].isStatic);
				if (largeDataMode) {
					/* "add.l a4,d0", NOT "adda.l": ADDA requires an ADDRESS register
					   als Ziel (emitCall() rechnet deshalb in a2). Hier ist das Ziel
					   ein Datenregister, also das normale ADD -- "ADD.L An,Dn" ist
					   zulaessig. Der echte r68 weist "adda.l a4,d0" korrekt ab
					   ("incomplete line: code not generated"). */
					if (trampolineMode) {
						/* Function pointers remain on the table path; r68 -j applies to
						   direct calls, not to an arbitrary data value. */
						fprintf(out, "\tmove.l\t%d(a4),d0\n\tadd.l\ta4,d0\n\tmove.l\td0,-(a7)\n", (8 + externCount + fnIdx) * 4);
					} else {
						fprintf(out, "\tmove.l\t%d(a4),d0\n\tadd.l\ta4,d0\n\tmove.l\td0,-(a7)\n", (8 + externCount + fnIdx) * 4);
					}
				} else {
					fprintf(out, "\tlea\t%s(pc),a0\n\tmove.l\ta0,-(a7)\n", asmName);
				}
			} else if ((strcmp(op, "CALLIND") == 0 || strcmp(op, "CALLINDP") == 0) && insP->argc == 1) {
				/* Indirect call through a function pointer. At entry, the stack layout
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
			} else if ((strcmp(op, "CALLEXT") == 0 || strcmp(op, "CALLEXTP") == 0) && (insP->argc == 3 || insP->argc == 4)) {
				/* Call an external function not defined in this IR, such as an OS-9/
				   Microware clib function (strcmp, printf, malloc, ...). Use the
				   documented Microware 68k C/C++ ABI instead of QCC's internal stack ABI.
				   QCC's IR presents arguments in source order on a7, so stack arguments
				   are first copied to tc_extcall_tmp while d0/d1 are extracted, then
				   pushed back in the required order. The raw external symbol is called
				   without the internal "tc_" prefix; final linking against clib.l is
				   performed by l68 (see docs/FORTSCHRITT.md).

				   BYTE-OFFSET-MODELL (2026-09-17, an echtem xcc-erzeugtem Code
				   gemessen -- sechs Proben mit int/double in allen Positionen,
				   printf("%f", x) eingeschlossen, s. docs/FLOAT_PLAN_de.md): d0:d1
				   sind EIN 8-Byte-Fenster. Jedes Argument bekommt in Aufrufreihenfolge
				   einen Byte-Offset in diesem Fenster; passt es dort KOMPLETT hinein
				   (offset+groesse <= 8), geht es in Register, sonst KOMPLETT auf den
				   Stack -- nie geteilt. SOBALD EIN ARGUMENT SPILLT, BLEIBT DAS FENSTER
				   FUER JEDES WEITERE GESCHLOSSEN, auch wenn eine Registerhaelfte
				   danach rechnerisch noch frei waere (gemessen mit
				   "probe6(int i, double d, int j)": d spillt bei Offset 4 (4+8>8),
				   und j landet TROTZDEM auf dem Stack statt in d1, obwohl 4+4<=8
				   waere). Ein einzelnes double (8 Byte) an Position 1 belegt d0:d1
				   VOLLSTAENDIG selbst -- ein nachfolgendes int hat dann KEINEN Platz
				   mehr, ungeachtet der alten Annahme "die ersten zwei Argumente
				   gehen nach d0/d1". Das vierte IR-Feld (Frontend, tcCallArgWidth)
				   traegt dafuer je Argument ein Breitenzeichen '4'/'8'; bei
				   nargsC==0 faellt das Feld durch strtok() weg (kein Token zwischen
				   zwei Leerzeichen) -- insP->args[3] ist dann der von der IR-Leseschleife
				   ohnehin vorbelegte Leerstring, was mit nargsC==0 konsistent ist. */
				int nargsC = number(insP->args[1], insP->line);
				int fixedCount = number(insP->args[2], insP->line);
				const char* widths = insP->args[3];
				/* argKind: 0 = Stack, 1 = nur d0, 2 = nur d1, 3 = d0:d1 (double). */
				int argSize[16], argKind[16], argTmpOff[16];
				int regBytes = 0, regsOpen = 1, stackBytes = 0, ai;
				(void)fixedCount; /* nicht mehr fuer die Aufteilung gebraucht, nur IR-Validierung */
				if (nargsC > 16) { sprintf(msg, "IR Zeile %d: zu viele Argumente fuer externen Aufruf (max 16)", insP->line); fatal(msg); }
				if ((int)strlen(widths) != nargsC) { sprintf(msg, "IR Zeile %d: CALLEXT-Breitenliste \"%s\" passt nicht zur Argumentzahl %d", insP->line, widths, nargsC); fatal(msg); }
				for (ai = 0; ai < nargsC; ai++) {
					argSize[ai] = (widths[ai] == '8') ? 8 : 4;
					if (regsOpen && regBytes + argSize[ai] <= 8) {
						argKind[ai] = (argSize[ai] == 8) ? 3 : (regBytes == 0 ? 1 : 2);
						regBytes += argSize[ai];
					} else {
						regsOpen = 0; /* sticky: ab hier geht ALLES auf den Stack */
						argKind[ai] = 0;
						argTmpOff[ai] = stackBytes;
						stackBytes += argSize[ai];
					}
				}
				if (stackBytes > 32) { sprintf(msg, "IR Zeile %d: zu viele Stack-Argumentbytes fuer externen Aufruf (max 32)", insP->line); fatal(msg); }
				/* Load the address ONCE into a0 (a0 is a free scratch address register
				   throughout this backend; no IR opcode requires it to remain valid beyond
				   its own emission). small uses PC-relative "lea" to match the backend's
				   PIC style; large uses the same a3 indirection mechanism as real globals.
				   tc_extcall_tmp consequently receives an additional table entry AFTER
				   all real globals (offset globalCount*4). */
				if (stackBytes > 0) {
					/* 2026-07-26: the tc_gadata entry is a link-time offset, not an
					   absolute pointer (see emitLeaGlobal()); adda.l is required as usual. */
					if (largeDataMode) fprintf(out, "\tmove.l\t%d(a3),a0\n\tadda.l\ta3,a0\n", globalCount * 4);
					else fputs("\tlea\ttc_extcall_tmp(pc),a0\n", out);
				}
				/* Oben auf dem IR-Operandenstapel liegt das LETZTE Argument (Quelltext-
				   reihenfolge) -- also rueckwaerts abbauen. Ein double auf dem Stack
				   liegt als hi/lo (PUSHD: lo zuerst gepusht, hi liegt oben) -- die erste
				   der beiden move.l holt deshalb hi. */
				for (ai = nargsC - 1; ai >= 0; ai--) {
					if (argKind[ai] == 0) {
						if (argSize[ai] == 8) fprintf(out, "\tmove.l\t(a7)+,%d(a0)\n\tmove.l\t(a7)+,%d(a0)\n", argTmpOff[ai], argTmpOff[ai] + 4);
						else fprintf(out, "\tmove.l\t(a7)+,%d(a0)\n", argTmpOff[ai]);
					} else if (argKind[ai] == 3) {
						fputs("\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,d1\n", out);
					} else if (argKind[ai] == 2) {
						fputs("\tmove.l\t(a7)+,d1\n", out);
					} else {
						fputs("\tmove.l\t(a7)+,d0\n", out);
					}
				}
				/* Zurueck auf den Maschinenstapel, rechts nach links (Quelltextreihenfolge
				   rueckwaerts durchlaufen), damit das ERSTE Stack-Argument an der
				   NIEDRIGSTEN Adresse landet -- die reale C-Konvention. Ein double wird
				   lo-dann-hi gepusht (wie PUSHD), damit hi wieder obenauf liegt. */
				for (ai = nargsC - 1; ai >= 0; ai--) {
					if (argKind[ai] != 0) continue;
					if (argSize[ai] == 8) fprintf(out, "\tmove.l\t%d(a0),-(a7)\n\tmove.l\t%d(a0),-(a7)\n", argTmpOff[ai] + 4, argTmpOff[ai]);
					else fprintf(out, "\tmove.l\t%d(a0),-(a7)\n", argTmpOff[ai]);
				}
				/* IMPORTANT (2026-07-24, found live on the real Q9): "jsr <name>" to
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
				if (stackBytes) fprintf(out, "\tlea\t%d(a7),a7\n", stackBytes);
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
			} else if (strcmp(op, "GLOBAL") == 0 || strcmp(op, "GARRAY") == 0 || strcmp(op, "GINIT") == 0 || strcmp(op, "GINITD") == 0 || strcmp(op, "GINITQ") == 0 || strcmp(op, "GINITAT") == 0) {
				/* Static local variable: already processed by collectGlobals() (its address
				   and initial value are emitted in the DATA/BSS section); this point in the
				   function body is a pure no-op with no runtime action. */
			} else {
				sprintf(msg, "IR Zeile %d: unbekannter oder unvollstaendiger Opcode %s", insP->line, op);
				fatal(msg);
			}
		}
		fputs("\n", out);
	}

	{
		int hasData = 0, hasBss = 0, gi;
		int hasIData = 0;   /* globals with GINITADDR -- initialized vsect */
		/* An array without GINIT is fully zero-initialized by C semantics. It
		   therefore belongs in an OS-9 vsect instead of expanding into millions of
		   explicit "dc.b 0" bytes in the r68 input. This is especially important
		   for the QCC bootstrap (several MB of action log). Arrays WITH GINIT remain
		   in the DATA section so their values are preserved. */
		for (gi = 0; gi < globalCount; gi++) {
			if (globals[gi].declOnly) continue; /* defined in ANOTHER file; no allocation here */
			/* A NON-remote vsect is limited to 64 KB (l68 rejects larger ones),
			   deshalb bleiben die initialisierten Globals im psect; nur die ganz
			   genullten gehen in einen vsect remote, der diese Grenze nicht hat.
			   Ohne -remotedata ist globalRemote() immer 0 -- die Ausgabe bleibt
			   dann Byte fuer Byte die alte. */
			if (globals[gi].hasInitAddr) hasIData = 1;
			else if (globalRemote(gi)) hasBss = 1;
			else hasData = 1;
		}
		/* The -largedata data indirection table (tc_gadata) is NO LONGER emitted
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
				if (!globalInDataArea(gi)) {
					int e; char gAsmName[NAME_LEN + 40];
					mangledName(gAsmName, "tc_g_", g->name, g->isStatic);
					if (g->elemSize != 1) emitAlign(out);
					if (!g->isArray) {
						fprintf(out, "%s:\tdc.%c\t%d\n", gAsmName, tagSuffix(g->elemSize), g->initialValue);
					} else if (!g->hasGinit && g->elemSize == 8) {
						/* Ein double belegt ACHT Byte; tagSuffix() kennt nur
						   b/w/l und haette pro Element nur vier geschrieben.
						   Ohne -remotedata laeuft ein uninitialisiertes
						   double-Global durch genau diesen Zweig. */
						int e;
						fprintf(out, "%s:\n", gAsmName);
						for (e = 0; e < g->length; e++) fputs("\tdc.l\t0\n\tdc.l\t0\n", out);
					} else if (!g->hasGinit) {
						/* perLine affects only readability of the generated assembly, not
						   correctness; a middle value was added for short (elemSize 2) on
						   2026-09-09. */
						int e, perLine = g->elemSize == 1 ? 40 : g->elemSize == 2 ? 30 : 20;
						fprintf(out, "%s:\n", gAsmName);
						for (e = 0; e < g->length; ) {
							int n = g->length - e < perLine ? g->length - e : perLine, k;
							fprintf(out, "\tdc.%c\t0", tagSuffix(g->elemSize));
							for (k = 1; k < n; k++) fprintf(out, ",0");
							fprintf(out, "\n");
							e += n;
						}
					} else if (g->elemSize == 8) {
						/* double mit Anfangswert: zwei dc.l je Element, hi
						   zuerst -- der 68k ist big-endian, damit steht das
						   Bitmuster genau so, wie fmove.d es liest. Hex, weil
						   die Haelften vorzeichenlos bis $FFFFFFFF gehen. */
						int e;
						fprintf(out, "%s:\n", gAsmName);
						for (e = 0; e < g->length; e++) {
							int* gd = g->init;
							unsigned long dhi = 2 * e < g->initLen ? (unsigned long)(unsigned int)gd[2 * e] : 0UL;
							unsigned long dlo = 2 * e + 1 < g->initLen ? (unsigned long)(unsigned int)gd[2 * e + 1] : 0UL;
							fprintf(out, "\tdc.l\t$%08lX\n\tdc.l\t$%08lX\n", dhi, dlo);
						}
					} else {
						fprintf(out, "%s:\n", gAsmName);
						/* 2026-08-11 correctness fix: the loop used to run to g->length,
						   although init[] held only MAX_ARRAY_LEN elements. An array longer
						   than MAX_ARRAY_LEN with at least one GINIT therefore read past init[],
						   exposing following fields and the next global's name as numbers.
						   Reproduced with length 5000 and one GINIT: values such as 1751343470
						   ("nach", the neighbor's name) appeared from index 4096 onward.
						   Indices at or above initLen are logically zero and are emitted as 0. */
						for (e = 0; e < g->length; e++) {
							/* Use an intermediate pointer as above: indexing through a scalar
							   pointer field is outside QCC's supported subset. */
							int* gi2 = g->init;
							int v = e < g->initLen ? gi2[e] : 0;
							fprintf(out, "\tdc.%c\t%d\n", tagSuffix(g->elemSize), v);
						}
					}
				}
			}
		}
		if (hasIData) {
			/* INITIALISIERTER VSECT: hier stehen Globals, deren Anfangswert die
			   ADRESSE eines anderen Globalen ist (char *tab[] = {"a","b"}).
			   Ein "dc.l <label>" im psect waere laut OS-9-Handbuch KEINE
			   relokierte Adresse; nur fuer initialisierte Zeiger in einem vsect
			   traegt der Binder eine M$IRefs-Liste ein, die der Lader beim
			   F$Fork auf die tatsaechliche Ladeadresse zieht. Geprueft: qr68
			   erzeugt dafuer dasselbe ROF wie r68, und ql68 wie l68 dieselbe
			   IRefs-Liste. Der ZUGRIFF ist derselbe wie beim vsect remote
			   (a6-relativ, s. emitLeaGlobal) -- deshalb entscheidet
			   globalInDataArea() darueber, nicht globalRemote(). */
			fprintf(out, "\n%s VSECT: initialisierte Zeiger -- OS-9 relokiert sie ueber M$IRefs\n", fullCommentPrefix());
			if (os9Mode) fputs("\tvsect\n", out);
			else fputs("\tsection .data\n", out);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				char gAsmName2[NAME_LEN + 40];
				int slot;
				int ai;
				if (g->declOnly) continue;
				if (!g->hasInitAddr) continue;
				emitAlign(out);
				mangledName(gAsmName2, "tc_g_", g->name, g->isStatic);
				fprintf(out, "%s:\n", gAsmName2);
				for (slot = 0; slot < (g->isArray ? g->length : 1); slot++) {
					const char* sym = 0;
					for (ai = 0; ai < initAddrCount; ai++) {
						if (initAddrGidx[ai] == gi && initAddrIdx[ai] == slot) { sym = initAddrSym[ai]; break; }
					}
					if (sym) {
						/* Der IR traegt den ROHEN Namen des Ziels ("__str0");
						   im Assembler heisst es wie jedes Global mit Praefix
						   und ggf. Modulsuffix -- sonst zeigt der Zeiger auf
						   ein Symbol, das es nicht gibt. */
						int tgt = findGlobal(sym);
						char tAsmName[NAME_LEN + 40];
						if (tgt < 0) fatal("GINITADDR verweist auf eine unbekannte globale Variable");
						mangledName(tAsmName, "tc_g_", globals[tgt].name, globals[tgt].isStatic);
						fprintf(out, "\tdc.l\t%s\n", tAsmName);
					}
					else if (g->init && slot < g->initLen) fprintf(out, "\tdc.l\t%d\n", g->init[slot]);
					else fputs("\tdc.l\t0\n", out);
				}
			}
			if (os9Mode) fputs("\tends\n", out);
		}
		if (hasBss) {
			fprintf(out, "\n%s VSECT REMOTE: zero-initialized globals -- OS-9 allocates and clears the area (measured); the module carries no zero bytes for it\n", fullCommentPrefix());
			if (os9Mode) fputs("\tvsect\tremote\n", out);
			else fputs("\tsection .bss\n", out);
			for (gi = 0; gi < globalCount; gi++) {
				Global* g = &globals[gi];
				if (g->declOnly) continue;
				if (globalRemote(gi)) {
					char gAsmName[NAME_LEN + 40];
					mangledName(gAsmName, "tc_g_", g->name, g->isStatic);
					/* ds.b does not align the next item; a following ds.l needs a
					   longword boundary or the 68000 reads an odd longword. Alignment in
					   the vsect was verified with r68 (Q9-qr68/test/remotetest.sh). */
					if (g->elemSize != 1) emitAlign(out);
					/* Ein double-Feld wird als doppelt so viele Langwoerter
					   reserviert: "ds.d" gibt es nicht (2026-09-16). */
					if (g->elemSize == 8)
						fprintf(out, "%s:\tds.l\t%d\n", gAsmName, 2 * (g->isArray ? g->length : 1));
					else
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
	char tmpPath[300];
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
		fprintf(stderr, "  -peephole:  Nachlauf ueber die Assemblerausgabe, entfernt ueberfluessige\n");
		fprintf(stderr, "              Speicherumwege (push, sofort gefolgt vom eigenen pop). Wirkt\n");
		fprintf(stderr, "              unabhaengig von -os9/-largedata/-remotedata.\n");
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
		else if (strcmp(argv[i], "-peephole") == 0) peepholeMode = 1;
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
		/* a6 is free only in -os9 mode. In vasm format a6 IS the frame pointer
		   (see framePtr()), so adda.l a6,reg would address the stack frame. */
		if (!os9Mode) fatal("-remotedata braucht -os9: nur dort ist a6 der Datenbereichszeiger und nicht der Frame-Pointer");
		/* Cross-file globals: this file CANNOT know whether the DEFINING file put
		   the symbol in the psect or remote vsect. The access path differs (PC-
		   relative/table-based versus a6-relative), so report the ambiguity rather
		   than silently generating an incorrect address. */
		for (gi = 0; gi < globalCount; gi++) {
			if (globals[gi].declOnly) {
				sprintf(msg, "-remotedata und dateiuebergreifendes Globales %s: der Zugriffsweg haengt von der definierenden Datei ab", globals[gi].name);
				fatal(msg);
			}
		}
	}
	if (!largeDataMode) {
		/* Heuristic warning (2026-07-25, see -largedata/emitLeaGlobal()): we CANNOT
		   know whether r68 will actually exceed the PC-relative range because the
		   assembler knows the complete code/data distance only later. A rough
		   threshold based on global data size nevertheless provides an early warning
		   before the real r68 reports the cryptic "value out of range". 16000 bytes
		   is deliberately conservative, well below the theoretical 32 KB, because
		   code and other symbols share the same address space. */
		long totalGlobalBytes = 0; int gi;
		for (gi = 0; gi < globalCount; gi++) {
			Global* g = &globals[gi];
			if (g->declOnly) continue;
			if (globalInDataArea(gi)) continue; /* in the data area, not the psect; no PC-relative distance */
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
		/* Derive the psect name from the output filename (without path/extension),
		   matching the default in codegen.cpp (genParser68kTo: <basename>_p). */
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
	/* -peephole writes to a temporary file FIRST: peepholeRun() may create the
	   REAL output file only once. On OS-9, mode "w" uses I$Create (qf_open in
	   qclib), which creates a new file and fails if it already exists, unlike
	   host fopen(...,"w"). Without the temporary file, peepholeRun() would try
	   to create argv[2] a second time; on 68030 this produced "cannot rewrite
	   output file". */
	if (peepholeMode) {
		sprintf(tmpPath, "%s.tmp", argv[2]);
		out = fopen(tmpPath, "w");
		if (!out) { sprintf(msg, "kann Zwischendatei nicht schreiben: %s", tmpPath); fatal(msg); }
	} else {
		out = fopen(argv[2], "w");
		if (!out) { sprintf(msg, "kann Ausgabe nicht schreiben: %s", argv[2]); fatal(msg); }
	}
	emitIR(out);
	if (ferror(out)) fatal("Schreibfehler in Assembler-Ausgabe");
	fclose(out);
	if (peepholeMode) peepholeRun(tmpPath, argv[2]);
	return 0;
}
