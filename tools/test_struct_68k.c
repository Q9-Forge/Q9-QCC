/* Struct-Regressionstest fuer das ECHTE 68k-Ziel.
 *
 * Warum nicht in runtests.sh gegen qccvm.py: die Host-VM ist fuer Structs kein
 * gueltiges Orakel. Ihr pointer_index() rechnet offset // type_size(tag), der
 * Zellenindex haengt also vom ZUGRIFFSTYP ab -- eine byteweise Kopie und ein
 * int-Zugriff landen bei arr[i] in verschiedenen Zellen. Auf echtem 68k
 * (Byte-Speicher) ist dasselbe IR korrekt. Struct-Aenderungen muessen deshalb
 * im Emulator geprueft werden.
 *
 * Ausgabeformat: je Fall "<id>:<wert>\n". Die id wird VOR dem Fall gedruckt,
 * damit ein Absturz (PMMU, Stack Overflow) am letzten gedruckten Marker
 * ablesbar ist statt nur als fehlende Ausgabe.
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
/* Muster von tcTypePop4: schreibt ueber einen Struct-ZEIGER in eine lokale
   Struct des Aufrufers. Braucht &s -- und damit die Blockadresse, nicht die
   eines Skalarslots. */
static void     fill(struct P* out, int v) { out->base = v; out->pointers = v + 1; }
/* Muster von tcCompatible(tcFunctionParamTypes[f][n], got): ein Struct-Wert
   aus einem ZWEIdimensionalen Array, direkt als Argument. */
static int      same(struct P a, struct P b) { return a.base == b.base && a.pointers == b.pointers; }

static void mark(int id) { putint(id); putchar(58); }
static void val(int v)   { putint(v); putchar(10); }

int main(void)
{
	struct I y;
	struct I q;

	y.a = 98; y.b = 3;

	/* 1  Zuweisung lokal -> lokal */
	mark(1);  { struct I x; x = y; val(x.a); }
	/* 2  Initialisierung aus Variable (tc_varinit, nicht tcAssignStore) */
	mark(2);  { struct I x = y; val(x.b); }
	/* 3  Initialisierung aus Funktionsrueckgabe */
	mark(3);  { struct I x = mk(10); val(x.b); }
	/* 4  Zuweisung lokal -> global */
	mark(4);  g = y; val(g.a);
	/* 5  Initialisierung aus global */
	mark(5);  { struct I x = g; val(x.a); }
	/* 6  Zuweisung an Array-Element */
	mark(6);  tab[2] = y; val(tab[2].a);
	/* 7  Lesen eines ganzen Array-Elements */
	mark(7);  q = tab[2]; val(q.b);
	/* 8  Struct als Parameter per Wert */
	mark(8);  val(use(y));
	/* 9  Wertsemantik: der Aufgerufene darf das Original nicht aendern */
	mark(9);  { struct P p; p.base = 1; p.pointers = 5; p.structId = 0; p.pad = 0;
	            pointee(p); val(p.pointers); }
	/* 10 ternaer + Array-Element mit Prae-Dekrement + Rueckgabe (tcTypePop) */
	mark(10); vals[0].base = 105; vals[0].pointers = 0; vals[0].structId = 0; vals[0].pad = 0;
	          vals[1].base = 99;  vals[1].pointers = 7; vals[1].structId = 0; vals[1].pad = 0;
	          depth = 2;
	          { struct P p = pop(); val(p.pointers); }
	/* 11 derselbe Pfad, zweiter Pop */
	mark(11); { struct P p = pop(); val(p.base); }
	/* 12 leerer Stapel -> der andere Ternaer-Zweig */
	mark(12); { struct P p = pop(); val(p.base); }
	/* 13 Feldzugriff DIREKT auf einer Funktionsrueckgabe (f().feld) */
	mark(13); { struct P p; p.base = 105; p.pointers = 1; p.structId = 0; p.pad = 0;
	            val(pointee(p).base); }
	/* 14 dito, anderes Feld. pointers=3, damit der Sollwert 2 ist und nicht 0 --
	      0 ist der Wert, den der kaputte Pfad ohnehin liefert. */
	mark(14); { struct P p; p.base = 105; p.pointers = 3; p.structId = 0; p.pad = 0;
	            val(pointee(p).pointers); }
	/* 15 Rueckgabe direkt als Argument weitergereicht (Sollwert 1, nicht 0) */
	mark(15); { struct P p; p.base = 105; p.pointers = 2; p.structId = 0; p.pad = 0;
	            val(isPtr(pointee(p))); }

	/* 16 &structVar: der Aufgerufene muss das Objekt des Aufrufers treffen */
	mark(16); { struct P p; p.base = 0; p.pointers = 0; p.structId = 0; p.pad = 0;
	            fill(&p, 41); val(p.base); }
	/* 17 dito, zweites Feld -- beweist, dass nicht nur ein Byte ankommt */
	mark(17); { struct P p; p.base = 0; p.pointers = 0; p.structId = 0; p.pad = 0;
	            fill(&p, 41); val(p.pointers); }
	/* 18 && -- der Auslöser: tcLogicBegin holt seinen Typ ueber tcTypePop4(&left) */
	mark(18); { int a; int b; a = 1; b = 1; if (a && b) val(7); else val(0); }

	/* 19 ganze Struct aus einem 2D-Array lesen */
	mark(19); grid[1][2].base = 88; grid[1][2].pointers = 4;
	          grid[1][2].structId = 0; grid[1][2].pad = 0;
	          gi = 1; gj = 2;
	          { struct P x = grid[gi][gj]; val(x.base); }
	/* 20 dito direkt als Argument (die Stelle, an der tc_arg prueft) */
	mark(20); { struct P x; x.base = 88; x.pointers = 4; x.structId = 0; x.pad = 0;
	            val(same(grid[gi][gj], x)); }

	return 0;
}
