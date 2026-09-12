/* short regression test for the REAL 68k target (2026-09-10).
 *
 * Why not only qccvm.py (host VM): pointer_index()
 * computes offset // type_size(tag), so a struct layout with 2-byte and
 * 4-byte fields can map two fields to the same Python list slot. For example,
 * in "short a; short b; int c;", b has offset 2 and c offset 4, but both
 * produce slot 1. The host oracle cannot validate this. Case 7 below covers
 * exactly this layout and is meaningful only here, not in runtests.sh.
 *
 * Output format matches test_struct_68k.c: one "<id>:<value>\n" per case.
 */

struct M { int c; short a; short b; };
struct N { short a; short b; int c; };

static short g = 300;

static short retTooBig(void) { return 100000; }

static void mark(int id) { putint(id); putchar(58); }
static void val(int v)   { putint(v); putchar(10); }

static void byval(short x) { mark(12); val(x); }

int main(void) {
	short a;
	short arr[3];
	short x;
	short *p;
	struct M m;
	struct N n;
	short y;
	unsigned short u;
	int i;

	mark(1); val(sizeof(short));

	a = -1;
	mark(2); val(a);

	g = g + 1;
	mark(3); val(g);

	for (i = 0; i < 3; i = i + 1) arr[i] = i * 10;
	mark(4); val(arr[1]);

	x = 42;
	p = &x;
	*p = 7;
	mark(5); val(x);

	m.c = 99999;
	m.a = 100;
	m.b = 200;
	mark(6); val(m.b);
	mark(7); val(sizeof(struct M));

	n.a = 11;
	n.b = 22;
	n.c = 99999;
	mark(8);  val(n.a);
	mark(9);  val(n.b);
	mark(10); val(n.c);
	mark(11); val(sizeof(struct N));

	y = -3;
	byval(y);

	mark(13); val(retTooBig());

	u = 40000;
	mark(14); val(u);

	/* Eigener Emissionsort (NARROWH im castExpr-Zweig, Data/qcc.lextab
	   Zeile ~6472) -- unabhaengig von den beiden Rueckgabe-Narrowing-Stellen
	   (Fall 13 oben), die einen ANDEREN Zweig treffen. Beide bisher nicht
	   verwechselbar zu halten war Sinn dieses eigenen Falls. */
	mark(15); val((short)100000);

	return 0;
}
