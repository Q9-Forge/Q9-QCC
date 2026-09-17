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

/* SELBSTREFERENZIELL (2026-09-16): erst seit tcStructPredeclare moeglich --
   vorher meldete das Feld "unknown struct or union 'N'", weil der Name erst
   NACH dem Rumpf registriert wurde. Damit war keine verkettete Liste baubar. */
struct N { int v; struct N *next; };

/* GLOBALER STRUCT-INITIALISIERER (2026-09-16). Hier zaehlt die
   BYTE-REIHENFOLGE: der Block ist ein char-Array, und ein int-Feld liegt auf
   dem 68k big-endian darin. Das VM-Orakel kann das nicht pruefen -- es fuehrt
   typisierte Zellen, keine Bytes. */
struct I ginit = { 71, 72 };

struct I tab[4];
struct I *gtab;            /* fuer den globalen Zeigerindex, Fall 37 */
static struct P grid[4][4];   /* fuer die 2D-Faelle */
static int gi, gj;
struct I g;
static struct P vals[8];
static int depth;

static struct I mk(int v)        { struct I t; t.a = v; t.b = v + 1; return t; }
static int      use(struct I s)  { return s.a; }
static int      listSum(struct N *p) { int s; s = 0; while (p) { s = s + p->v; p = p->next; } return s; }
static int      listLen(struct N *p) { int n; n = 0; while (p) { n = n + 1; p = p->next; } return n; }
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
union U2 { int a; int b; };
union U3 { int i; char c[4]; };
/* VERSCHACHTELTE structs als FELDWERT (2026-09-17). Auf echter Hardware
   zaehlt hier das BYTE-LAYOUT: "o.in.a" ist reine Offset-Addition, ein
   LOADIND zuviel wuerde die ersten vier Byte des Feldes als Adresse deuten.
   NE hat bewusst ein char VOR dem int, damit ein falsch gerechnetes
   Innen-Offset einen anderen Wert liefert statt zufaellig denselben. */
struct NI { int a; int b; };
struct NE { char c; int a; };
struct NO { struct NI in; int z; };
struct NP { int z; struct NI in; };
struct NM { struct NE in; int z; };
struct NN { struct NI a; struct NI b; };
struct ND { struct NI in; };
struct NL { struct ND d; };
static struct NO gno;
/* MEHRDIMENSIONALES ARRAY ALS PARAMETER (2026-09-17). Auf echter Hardware
   zaehlt die SCHRITTWEITE: "m[i][j]" wird zu i*zeilenlaenge+j, und der
   Zugriff skaliert das Ergebnis mit der Elementgroesse (int 4, char 1).
   Die Host-VM sieht das nicht -- sie fuehrt typisierte Zellen statt Bytes.
   Ein erster Anlauf wurde am 16.09.2026 zurueckgerollt, weil die Indizes
   gar nicht kombiniert wurden; Lesen und Schreiben lagen auf dieselbe Weise
   daneben und stimmten deshalb miteinander ueberein. */
static int m2dLesen(int m[2][3])    { return m[1][2]; }
static int m2dMix(int m[2][3])      { return m[0][0]*100 + m[0][2]*10 + m[1][0]; }
static void m2dSchreiben(int m[2][3]) { m[1][1] = 42; }
static int m2dOffen(int m[][3])     { return m[1][0]; }
static int m3d(int m[2][3][4])      { return m[1][2][3]; }
static int m2dChar(char m[2][3])    { return m[1][2]; }
static int m2dSumme(int m[2][3])
{
	int i; int j; int t;
	t = 0;
	for (i = 0; i < 2; i = i + 1)
		for (j = 0; j < 3; j = j + 1)
			t = t + m[i][j];
	return t;
}
static struct Z gz;
static int zfeld[8];
static char ztxt[8];
static char txt[8];

static struct I *giveTab(void) { return tab; }

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
	/* One case is intentionally missing: aarr[k].args[5], combining struct and
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

	/* 27 &arr[i] on an array of structs -- GLOBAL.
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

	/* 36-38 DIESELBE REGEL, DREI UEBERSEHENE GESCHWISTERSTELLEN (2026-09-14).
	      Fall 35 hatte "v = p[i]" repariert, "*p", "gp[i]" und "f()[i]"
	      blieben stehen: alle drei emittierten LOADIND auf eine GANZE Struct,
	      lasen also vier Byte ihres Inhalts als Zahl und gaben sie als
	      Adresse weiter. Im selbstuebersetzten Compiler wurde daraus ein
	      PMMU-Fault auf $69000000 -- das ist eine TCType {'i',0,0,0}.
	      Sichtbar wurde es erst, als tcCompatible4 auf tcIsInteger(*wanted)
	      umgestellt wurde. Jeder Fall indiziert ungleich null, sonst faellt
	      eine falsche Schrittweite nicht auf. */
	/* 36 ganze Struct per WERT aus einer Dereferenzierung: f(*p) */
	mark(36); { struct I *ip3; int k; k = 2;
	            tab[2].a = 91; ip3 = &tab[k]; val(use(*ip3)); }
	/* 37 ganze Struct per WERT aus einem GLOBALEN Zeigerindex: f(gp[i]) */
	mark(37); { int k; k = 1; tab[1].a = 92; gtab = tab;
	            val(use(gtab[k])); }
	/* 38 ganze Struct per WERT aus einem indizierten AUFRUFERGEBNIS: f(g()[i]) */
	mark(38); { int k; k = 3; tab[3].a = 93;
	            val(use(giveTab()[k])); }
	/* 39-42 UNION (2026-09-15). Intern eine struct, deren Felder alle auf
	   Offset 0 liegen. Fall 42 prueft die BYTE-REIHENFOLGE und kann genau
	   deshalb nur HIER stehen und nicht in runtests.sh: der 68k ist
	   big-endian, das VM-Orakel rechnet little-endian. u.i = 5 legt die 5
	   also ins LETZTE Byte, c[3], und c[0] bleibt 0. */
	/* 39 zwei gleich grosse Felder teilen den Speicher */
	mark(39); { union U2 u; u.a = 94; val(u.b); }
	/* 40 Groesse ist das groesste Feld, nicht die Summe */
	mark(40); { val(sizeof(union U3)); }
	/* 41 ueber einen Zeiger, mit -> */
	mark(41); { union U2 u; union U2 *up; up = &u; up->a = 95; val(up->b); }
	/* 42 big-endian: das niederwertigste Byte liegt HINTEN */
	mark(42); { union U3 u; u.i = 5; val(u.c[3] * 10 + u.c[0]); }

	/* 43-46 SELBSTREFERENZIELLE STRUCTS (2026-09-16). Auf echter Hardware
	   zaehlt hier, dass der Zeiger im Feld wirklich vier Byte belegt und die
	   Kette ueber echten Speicher laeuft -- im VM-Orakel ist ein Zeiger ein
	   Python-Objekt und sagt darueber nichts. */
	/* 43 deklarieren und das eigene Feld lesen */
	mark(43); { struct N a; a.v = 96; a.next = 0; val(a.v); }
	/* 44 ueber den Zeiger auf den Nachbarn zugreifen */
	mark(44); { struct N a; struct N b; struct N *p;
	            a.v = 1; b.v = 97; a.next = &b; p = a.next; val(p->v); }
	/* 45 die Kette durchlaufen und summieren -- das Kernidiom */
	mark(45); { struct N a; struct N b; struct N c;
	            a.v = 1; b.v = 2; c.v = 3;
	            a.next = &b; b.next = &c; c.next = 0;
	            val(listSum(&a)); }
	/* 46 Laenge derselben Kette */
	mark(46); { struct N a; struct N b; struct N c;
	            a.v = 1; b.v = 2; c.v = 3;
	            a.next = &b; b.next = &c; c.next = 0;
	            val(listLen(&a)); }

	/* 49-51 VERKETTETE MEMBER-ZUGRIFFE (2026-09-16). Auf echter Hardware
	   zaehlt, dass die Zwischenstufe wirklich einen Zeiger aus dem Speicher
	   laedt (LOADIND p) und die Adressrechnung ueber echte Bytes laeuft. */
	mark(49); { struct N x; struct N y; struct N *q;
	            y.v = 81; x.next = &y; q = &x; val(q->next->v); }
	mark(50); { struct N x; struct N y; struct N z;
	            z.v = 82; x.next = &y; y.next = &z; val(x.next->next->v); }
	mark(51); { struct N x; struct N y; struct N *q;
	            x.next = &y; q = &x; q->next->v = 83; val(y.v); }

	/* 52-60 VERSCHACHTELTE structs als FELDWERT (2026-09-17) */
	mark(52); { struct NO o; o.in.a = 52; o.in.b = 1; o.z = 2; val(o.in.a); }
	mark(53); { struct NO o; o.in.a = 1; o.in.b = 53; o.z = 2; val(o.in.b); }
	/* Das Feld HINTER dem eingebetteten struct -- sein Offset haengt an
	   dessen Groesse; eine falsche Groesse trifft genau hier. */
	mark(54); { struct NO o; o.in.a = 1; o.in.b = 2; o.z = 54; val(o.z); }
	/* und davor: dann ist das Innen-Offset verschoben */
	mark(55); { struct NP o; o.z = 1; o.in.a = 55; val(o.in.a); }
	/* char vor int im INNEREN struct: prueft dessen eigenes Layout */
	mark(56); { struct NM o; o.in.c = 7; o.in.a = 56; o.z = 8; val(o.in.a); }
	mark(57); { struct NM o; o.in.c = 57; o.in.a = 1; o.z = 2; val(o.in.c); }
	/* zwei gleiche structs nebeneinander -- das zweite darf das erste nicht
	   ueberschreiben */
	mark(58); { struct NN o; o.a.a = 1; o.a.b = 2; o.b.a = 58; o.b.b = 3;
	            val(o.b.a); }
	/* drei Ebenen und ein Zeiger auf das aeussere struct */
	mark(59); { struct NL l; struct NL *p; l.d.in.a = 59; p = &l;
	            val(p->d.in.a); }
	/* globales verschachteltes struct */
	mark(60); { gno.in.a = 60; gno.z = 1; val(gno.in.a); }

	/* 61-68 MEHRDIMENSIONALES ARRAY ALS PARAMETER (2026-09-17) */
	mark(61); { int a[2][3]; a[1][2] = 61; val(m2dLesen(a)); }
	/* diskriminierend: jede Zelle traegt zu einer anderen Stelle bei, eine
	   vertauschte oder fehlende Zeilenrechnung ergibt eine andere Zahl */
	mark(62); { int a[2][3];
	            a[0][0]=1; a[0][1]=9; a[0][2]=2; a[1][0]=3; a[1][1]=9; a[1][2]=9;
	            val(m2dMix(a)); }
	/* Schreiben durch den Parameter -- und die NACHBARN muessen unberuehrt
	   bleiben; ein Test, der nur dieselbe Zelle zurueckliest, faende den
	   alten Fehler nicht */
	mark(63); { int a[2][3]; int i; int j;
	            for (i=0;i<2;i=i+1) for (j=0;j<3;j=j+1) a[i][j] = 0;
	            m2dSchreiben(a);
	            val(a[1][1] + a[1][0]*1000 + a[0][1]*100); }
	mark(64); { int a[2][3]; a[1][0] = 64; val(m2dOffen(a)); }
	mark(65); { int a[2][3]; int i; int j;
	            for (i=0;i<2;i=i+1) for (j=0;j<3;j=j+1) a[i][j] = i*3+j;
	            val(m2dSumme(a) + 50); }
	mark(66); { int a[2][3][4]; a[1][2][3] = 66; val(m3d(a)); }
	/* char-Matrix: Schrittweite 1 statt 4 -- eine falsche Skalierung greift
	   hier voellig woanders hin */
	mark(67); { char a[2][3]; a[1][2] = 67; val(m2dChar(a)); }
	/* die eindimensionale Nachbarform darf sich nicht geaendert haben */
	mark(68); { int a[2][3]; a[0][1] = 68; val(m2dLesen(a) + a[0][1]); }

	/* 47-48 globaler struct-Initialisierer, big-endian abgelegt */
	mark(47); { val(ginit.a); }
	mark(48); { val(ginit.b); }

	/* 69-71 ZEIGER AUF ARRAY, "int (*p)[3];" (2026-09-17) -- p ist ein
	   Zeiger (EIN Slot), traegt die Zeilenlaenge im selben Mechanismus wie
	   ein mehrdimensionaler Array-Parameter oben. Nur lokale Variablen in
	   dieser Version. */
	mark(69); { int a[2][3]; int (*p)[3];
	            a[0][0]=1; a[1][0]=2; p=a; p[1][0]=99; val(a[1][0]); }
	mark(70); { int a[2][3][4]; int (*p)[3][4];
	            a[1][2][3]=70; p=a; val(p[1][2][3]); }
	mark(71); { int a[2][3]; int (*p)[3];
	            a[0][0]=1; a[0][1]=9; a[0][2]=3; a[1][0]=9; a[1][1]=9; a[1][2]=6;
	            p=a; val(p[0][0]*100+p[0][2]*10+p[1][2]); }

	return 0;
}
