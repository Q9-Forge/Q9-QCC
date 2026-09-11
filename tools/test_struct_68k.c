/* Struct regression test for the REAL 68k target.
 *
 * Why not run it against qccvm.py in runtests.sh: the host VM is not a valid
 * oracle for structs. Its pointer_index() computes offset // type_size(tag), so
 * The cell index therefore depends on ACCESS TYPE: a byte copy and an int
 * access land in different cells for arr[i]. On real 68k
 * the same IR is correct on real 68k byte memory. Struct changes must therefore
 * be checked in the emulator.
 *
 * Output format: one "<id>:<value>\n" per case. The id is printed BEFORE the case,
 * so a crash (PMMU, stack overflow) can be located at the last printed marker
 * instead of appearing only as missing output.
 */

struct I { int a; int b; };
struct P { char base; unsigned char pointers; unsigned char structId; unsigned char pad; };

struct I tab[4];
static struct P grid[4][4];   /* fuer die 2D-Faelle */
static int gi, gj;
struct I g;
static struct P vals[8];
static int depth;

static struct I mk(int v)        { struct I t; t.a = v; t.b = v + 1; return t; }
static int      use(struct I s)  { return s.a; }
static struct P bad(void)        { struct P t; t.base = 63; t.pointers = 0; t.structId = 0; t.pad = 0; return t; }
static struct P pop(void)        { return depth > 0 ? vals[--depth] : bad(); }
static struct P pointee(struct P t) { if (t.pointers) t.pointers--; return t; }
static int      isPtr(struct P t)   { return t.pointers != 0; }
/* tcTypePop4 pattern: writes through a struct pointer into a caller-local
   struct. Requires &s, the block address rather than a scalar-slot address. */
static void     fill(struct P* out, int v) { out->base = v; out->pointers = v + 1; }
/* tcCompatible(tcFunctionParamTypes[f][n], got) pattern: a struct value from a
   TWO-dimensional array, passed directly as an argument. */
static int      same(struct P a, struct P b) { return a.base == b.base && a.pointers == b.pointers; }

/* Pointer arrays as struct fields (since 2026-09-07). This is exactly where
   qcc_backend_c.cpp gescheitert: "char* args[6]" in seiner Instr-Struktur.
   Der Witz ist die SCHRITTWEITE -- ein Zeiger belegt im Struct acht Byte,
   IPADD wuerde mit vier skalieren, deshalb IPADDN 8. Ein falscher Schritt
   faellt nur bei einem Index > 0 auf, darum indiziert hier jeder Fall
   ungleich null. Gelesen wird immer ueber einen lokalen Zeiger: args[i][k]
   waere tab[i][k] auf einem Zeigerfeld, und das kann QCC nicht. */
struct A { char op[4]; char* args[6]; int argc; };
static struct A ga;

/* Cases 29-35: a scalar POINTER field (not a pointer array) and a struct
   indexed through a pointer. */
struct Z { int *ip; char *cp; int n; };
static struct Z gz;
static int zfeld[8];
static char ztxt[8];
static char txt[8];

static void mark(int id) { putint(id); putchar(58); }
static void val(int v)   { putint(v); putchar(10); }

int main(void)
{
	struct I y;
	struct I q;

	y.a = 98; y.b = 3;

	/* 1  local -> local assignment */
	mark(1);  { struct I x; x = y; val(x.a); }
	/* 2  initialization from variable (tc_varinit, not tcAssignStore) */
	mark(2);  { struct I x = y; val(x.b); }
	/* 3  initialization from function return */
	mark(3);  { struct I x = mk(10); val(x.b); }
	/* 4  local -> global assignment */
	mark(4);  g = y; val(g.a);
	/* 5  initialization from global */
	mark(5);  { struct I x = g; val(x.a); }
	/* 6  assignment to array element */
	mark(6);  tab[2] = y; val(tab[2].a);
	/* 7  read complete array element */
	mark(7);  q = tab[2]; val(q.b);
	/* 8  struct passed by value */
	mark(8);  val(use(y));
	/* 9  value semantics: callee must not change the original */
	mark(9);  { struct P p; p.base = 1; p.pointers = 5; p.structId = 0; p.pad = 0;
	            pointee(p); val(p.pointers); }
	/* 10 ternary + array element with pre-decrement + return (tcTypePop) */
	mark(10); vals[0].base = 105; vals[0].pointers = 0; vals[0].structId = 0; vals[0].pad = 0;
	          vals[1].base = 99;  vals[1].pointers = 7; vals[1].structId = 0; vals[1].pad = 0;
	          depth = 2;
	          { struct P p = pop(); val(p.pointers); }
	/* 11 same path, second pop */
	mark(11); { struct P p = pop(); val(p.base); }
	/* 12 empty stack -> the other ternary branch */
	mark(12); { struct P p = pop(); val(p.base); }
	/* 13 field access DIRECTLY on a function return (f().field) */
	mark(13); { struct P p; p.base = 105; p.pointers = 1; p.structId = 0; p.pad = 0;
	            val(pointee(p).base); }
	/* 14 same, different field. pointers=3 makes the expected value 2, not 0;
	      0 is what the broken path would produce. */
	mark(14); { struct P p; p.base = 105; p.pointers = 3; p.structId = 0; p.pad = 0;
	            val(pointee(p).pointers); }
	/* 15 return passed directly as argument (expected value 1, not 0) */
	mark(15); { struct P p; p.base = 105; p.pointers = 2; p.structId = 0; p.pad = 0;
	            val(isPtr(pointee(p))); }

	/* 16 &structVar: callee must access the caller's object */
	mark(16); { struct P p; p.base = 0; p.pointers = 0; p.structId = 0; p.pad = 0;
	            fill(&p, 41); val(p.base); }
	/* 17 same, second field -- proves that more than one byte arrives */
	mark(17); { struct P p; p.base = 0; p.pointers = 0; p.structId = 0; p.pad = 0;
	            fill(&p, 41); val(p.pointers); }
	/* 18 && -- trigger case: tcLogicBegin obtains its type through tcTypePop4(&left) */
	mark(18); { int a; int b; a = 1; b = 1; if (a && b) val(7); else val(0); }

	/* 19 read complete struct from a 2D array */
	mark(19); grid[1][2].base = 88; grid[1][2].pointers = 4;
	          grid[1][2].structId = 0; grid[1][2].pad = 0;
	          gi = 1; gj = 2;
	          { struct P x = grid[gi][gj]; val(x.base); }
	/* 20 same, passed directly as an argument (where tc_arg checks it) */
	mark(20); { struct P x; x.base = 88; x.pointers = 4; x.structId = 0; x.pad = 0;
	            val(same(grid[gi][gj], x)); }


	/* 21 pointer-array field: write and read, fixed index */
	mark(21); { struct A x; char *p; txt[0] = 71; x.args[0] = txt;
	            p = x.args[0]; val(p[0]); }
	/* 22 same with VARIABLE index; stride matters here */
	mark(22); { struct A x; char *p; int i; txt[1] = 72;
	            x.args[2] = txt + 1; i = 2;
	            p = x.args[i]; val(p[0]); }
	/* 23 WRITE with variable index, read with fixed index */
	mark(23); { struct A x; char *p; int i; txt[2] = 73; i = 3;
	            x.args[i] = txt + 2;
	            p = x.args[3]; val(p[0]); }
	/* 24 through a pointer to the struct; the -> path has its own emission site */
	mark(24); { struct A x; struct A *xp; char *p; txt[3] = 74;
	            xp = &x; xp->args[1] = txt + 3;
	            p = xp->args[1]; val(p[0]); }
	/* 25 GLOBAL struct: separate emission sites again (LOADGP/PUSHADDR G) */
	mark(25); { char *p; txt[4] = 75; ga.args[4] = txt + 4;
	            p = ga.args[4]; val(p[0]); }
	/* Ein Fall FEHLT hier bewusst: aarr[k].args[5] -- also Structschritt und
	   Feldschritt in einem Ausdruck. QCC lehnt das mit
	   "arr[i].field[j] not supported in this version" ab, das ist eine
	   EIGENE Grenze und sauber gemeldet. Gebraucht wird sie nicht:
	   qcc_backend_c.cpp greift ausschliesslich ueber einen Zeiger zu
	   (insP->args[i], Fall 24), und nichts sonst in der Kette nutzt das
	   Muster. Deshalb hier kein Test fuer etwas, das absichtlich fehlt.

	   26 die Groesse haelt das Layout fest: op[4] auf 0, args ab 8
	      (Achtausrichtung), argc auf 56, aufgerundet 60 -- am erzeugten
	      IR nachgemessen, nicht gerechnet */
	mark(26); val(sizeof(struct A));

	/* 27 &arr[i] auf ein Array von Structs -- GLOBAL.
	      Hier stand ein STILLER Falschcode-Fehler (gefunden 2026-09-07):
	      tc_addressref emittierte PTRINDEX mit dem Typtag, und tcTypeTag
	      gibt fuer eine Struct 'i', also vier Byte Schrittweite. Bei
	      struct I (acht Byte) zeigt &tab[2] damit auf tab[1].
	      Aufgefallen ist es an QCCs Backend auf dem 68030: es holt seine
	      IR-Anweisungen mit "insP = &ir[irCount]", und alle 80-Byte-
	      Anweisungen landeten vier Byte auseinander uebereinander.
	      DER SOLLWERT DISKRIMINIERT: mit der falschen Schrittweite bleibt
	      tab[2].a auf 0. */
	mark(27); { struct I *ip; int k; k = 2;
	            tab[2].a = 0; ip = &tab[k]; ip->a = 55;
	            val(tab[2].a); }
	/* 28 same case LOCAL -- a separate emission site */
	mark(28); { struct I loc[3]; struct I *ip; int k; k = 2;
	            loc[2].a = 0; ip = &loc[k]; ip->a = 56;
	            val(loc[2].a); }

	/* 29-34 INDIZIERUNG DURCH EIN SKALARES ZEIGERFELD (2026-09-07).
	      Vorher an sechs Emissionsstellen abgelehnt ("scalar struct field
	      cannot be indexed"). Der Witz ist, dass erst der Zeiger IM Feld
	      geholt werden muss (LOADIND p) -- die Aufrufer haben nur die
	      ADRESSE des Feldes auf dem Stapel. Jeder Fall indiziert ungleich
	      null, sonst faellt eine falsche Schrittweite nicht auf. */
	/* 29 global struct, read through an int* field */
	mark(29); { int k; k = 3; zfeld[3] = 81; gz.ip = zfeld;
	            val(gz.ip[k]); }
	/* 30 global struct, write */
	mark(30); { int k; k = 3; zfeld[3] = 0; gz.ip = zfeld;
	            gz.ip[k] = 82; val(zfeld[3]); }
	/* 31 read through a struct pointer, char* field (stride 1) */
	mark(31); { struct Z *zp; int k; k = 2; ztxt[2] = 83;
	            zp = &gz; zp->cp = ztxt;
	            val(zp->cp[k] & 255); }
	/* 32 write through a struct pointer */
	mark(32); { struct Z *zp; int k; k = 2; zfeld[2] = 0;
	            zp = &gz; zp->ip = zfeld;
	            zp->ip[k] = 84; val(zfeld[2]); }
	/* 33 LOCAL struct, read -- another separate site */
	mark(33); { struct Z lz; int k; k = 5; zfeld[5] = 85; lz.ip = zfeld;
	            val(lz.ip[k]); }
	/* 34 LOCAL struct, write */
	mark(34); { struct Z lz; int k; k = 5; zfeld[5] = 0; lz.ip = zfeld;
	            lz.ip[k] = 86; val(zfeld[5]); }

	/* 35 COPY COMPLETE STRUCT THROUGH A POINTER (v = p[i]).
	      Das war ein STILLER Falschcode-Fehler (2026-09-07):
	      LOADP/PTRINDEX i/LOADIND i -- vier Byte Schrittweite UND ein
	      Ladebefehl, der vier Byte der Struct als Zahl liest und als
	      Quelladresse weitergibt. Fuer den ARRAY-Fall (v = arr[i]) war
	      dieselbe Bauform am 2026-09-01 repariert worden, der Zeigerfall
	      blieb stehen. Mit k=3 und acht Byte je Struct diskriminiert der
	      Sollwert. */
	mark(35); { struct I *ip2; struct I cv; int k; k = 3;
	            tab[3].a = 87; tab[3].b = 0;
	            ip2 = &tab[0]; cv = ip2[k]; val(cv.a); }

	return 0;
}
