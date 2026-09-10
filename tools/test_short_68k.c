/* short-Regressionstest fuer das ECHTE 68k-Ziel (2026-09-10).
 *
 * Warum nicht ausschliesslich gegen qccvm.py (Host-VM): pointer_index()
 * rechnet offset // type_size(tag) -- ein struct-Feld-Layout, in dem ein
 * 2-Byte- und ein 4-Byte-Feld auf denselben Python-Listenplatz fallen
 * (z. B. "short a; short b; int c;": b liegt auf Byteoffset 2, c auf
 * Byteoffset 4 -- 2/2=1 und 4/4=1 kollidieren), ist im Orakel NICHT pruefbar.
 * Fall 7 unten ist genau diese Reihenfolge und daher NUR hier, nicht in
 * runtests.sh, aussagekraeftig.
 *
 * Ausgabeformat wie test_struct_68k.c: je Fall "<id>:<wert>\n".
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

	return 0;
}
