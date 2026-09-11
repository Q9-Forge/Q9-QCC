/*
 * q9-qclib memory allocator
 *
 * Purpose:
 *   Provides the arena-backed realloc implementation used by the Q9 tools.
 *   The allocator grows the most recent allocation in place when possible.
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 */
/* realloc fuer qclib -- der C-Rumpf.
 *
 * QCCs erzeugter Parser braucht echte Umschichtung: das Aktions-Log
 * waechst durch VERDOPPELN (realloc(actionLog, n * sizeof(...))), der
 * Inhalt muss also erhalten bleiben. Die zweite Aufrufstelle,
 * realloc(0, 524288) fuer den Eingabepuffer, ist der einfache Fall.
 *
 * WARUM EINE ARENA UND NICHT JEDES MAL F$SRqMem -- das ist gemessen, und
 * die erste Fassung hat es falsch gemacht:
 *
 * Sie holte jeden neuen Block frisch vom System und gab den alten danach
 * zurueck. Damit leben beim Umschichten kurz der ALTE und der NEUE Block,
 * der Spitzenbedarf ist also das Dreifache der Endgroesse. Auf dem Ziel
 * (16 MB RAM, davon 14.348 K frei) ist der Lauf deshalb mit
 * "6291464 Byte angefragt, rc=237" gescheitert -- 237 ist E$NoRAM
 * (MWOS/SRC/DEFS/errno.h). Gebraucht wurden 3 MB alt plus 6 MB neu, dazu
 * das 858-KB-Modul, 1 MB Stack und der Eingabepuffer; der groesste
 * ZUSAMMENHAENGENDE Block reichte nicht mehr.
 *
 * Microwares clib macht es anders, und daran ist diese Fassung
 * ausgerichtet: ihr memory.c fuehrt eine eigene Segmentverwaltung
 * (_cmem_base, _cmem_segs, _cmem_allocp) ueber Systemspeicher, den es in
 * grossen Stuecken holt (TRAP $5c und $29, disassembliert).
 *
 * DIESE FASSUNG: EINE Arena, darin ein Belegungszeiger -- und der
 * ZULETZT ausgegebene Block waechst AN DER STELLE. Damit kostet die
 * Verdopplungsleiter des Parsers keine einzige Kopie und keinen
 * Spitzenbedarf: 512 KB Eingabepuffer plus 6 MB Log, fertig. Das ist
 * genau das Muster der Kette (ein fester Puffer, ein wachsender Block),
 * und es ist nachgemessen, nicht angenommen.
 *
 * WAS DIESE FASSUNG NICHT KANN: Speicher wieder hergeben. Es gibt keine
 * Freigabeliste, und ein Block, der nicht der letzte ist, laesst beim
 * Wachsen seinen alten Platz liegen. qclib hat kein free -- die Kette
 * ruft keines -- und beim Prozessende gibt OS-9 die Arena ohnehin
 * zurueck. Das ist bewusst so und keine Luecke, die noch zu schliessen
 * waere; sollte je ein Programm dieser Kette echtes free brauchen,
 * gehoert hierher eine Freigabeliste und nicht ein Flicken.
 *
 * VOR jedem ausgegebenen Block liegen acht Byte Kopf: im ersten Langwort
 * die nutzbare Groesse. Acht und nicht vier, damit der Rueckgabezeiger
 * die Ausrichtung des Blockanfangs behaelt.
 *
 * Registerbelegung und Servicenummer von F$SRqMem stehen in os9call.a,
 * dort dreifach belegt.
 *
 * Quelle im QCC-Subset: keine Zeichenkettenverkettung, kein
 * tab[i][k] auf Zeigerfeldern, kein static.
 */

extern int _os_srqmem(int want, int *granted, char **addr);
extern int _os_srtmem(int size, char *addr);

/* Memory left available to the system after acquiring the arena. OS-9 still
   needs memory when a program opens a file or starts a process. Two MiB is
   deliberately generous for this toolchain. */
#define QM_RESERVE 2097152

char *qm_base;                  /* Arena base; zero means not acquired. */
int qm_size;                    /* Granted arena size. */
int qm_top;                     /* Used bytes from arena start. */

/* Acquire the arena on first demand. Return 1 when available and 0 when
   memory cannot be provided. First query the largest free block with
   F$SRqMem(-1), return it, then request that size minus the reserve. This
   keeps the arena size machine-dependent instead of hard-coded. */
/* Function: qm_arena
 * Acquires the target memory arena on first allocation.
 * Parameters: None.
 * Returns: Non-zero on success, zero when OS-9 cannot provide memory. */
int qm_arena(void)
{
	char *p;
	int gr;
	int rc;
	int want;

	if (qm_base != 0)
		return 1;
	gr = 0;
	p = 0;
	rc = _os_srqmem(-1, &gr, &p);
	if (rc != 0 || p == 0)
		return 0;
	_os_srtmem(gr, p);
	want = gr - QM_RESERVE;
	if (want < 65536)
		want = gr;              /* Small machine: use the entire block. */
	gr = 0;
	p = 0;
	rc = _os_srqmem(want, &gr, &p);
	if (rc != 0 || p == 0)
		return 0;
	qm_base = p;
	qm_size = gr;
	qm_top = 0;
	/* Align to four bytes: the manual guarantees only an even address, while
	   the copy loop below operates on longwords. */
	while ((((int) qm_base) + qm_top) & 3)
		qm_top = qm_top + 1;
	return 1;
}

/* Function: qm_realloc
 * Allocates or grows a block in the qclib arena.
 * Parameters: a Runtime argument frame containing pointer and new size.
 * Returns: New block address, or null on allocation failure. */
char *qm_realloc(int *a)
{
	char *alt;
	char *neu;
	int *hdr;
	int *nh;
	int *lq;
	int *lz;
	int want;
	int n4;
	int cap;
	int off;
	int lang;
	int i;

	alt = (char *) a[0];
	want = a[1];
	if (want <= 0)
		return 0;
	if (qm_arena() == 0)
		return 0;
	/* Round every block size up to four bytes so the next header is aligned. */
	n4 = ((want + 3) / 4) * 4;

	cap = 0;
	if (alt != 0) {
		hdr = (int *) (alt - 8);
		cap = hdr[0];
		if (n4 <= cap)
			return alt;             /* Existing capacity is sufficient. */
		/* The most recent block grows in place, leaving the unused arena
		   directly behind it. This is the normal toolchain path and avoids
		   both copying and a temporary peak allocation. */
		off = (alt - 8) - qm_base;
		if (off + 8 + cap == qm_top) {
			if (off + 8 + n4 > qm_size)
				return 0;
			hdr[0] = n4;
			qm_top = off + 8 + n4;
			return alt;
		}
	}

	if (qm_top + 8 + n4 > qm_size)
		return 0;
	nh = (int *) (qm_base + qm_top);
	nh[0] = n4;
	nh[1] = 0;
	neu = qm_base + qm_top + 8;
	qm_top = qm_top + 8 + n4;

	/* Copy longword-wise: both blocks begin eight bytes after a four-byte
	   aligned address. This matters for multi-megabyte allocations. */
	if (cap > 0) {
		lang = cap;
		if (lang > n4)
			lang = n4;
		lq = (int *) alt;
		lz = (int *) neu;
		i = 0;
		while (i < lang / 4) {
			lz[i] = lq[i];
			i++;
		}
		i = (lang / 4) * 4;
		while (i < lang) {
			neu[i] = alt[i];
			i++;
		}
	}
	return neu;
}
