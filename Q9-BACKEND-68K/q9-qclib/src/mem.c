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
/* realloc implementation for qclib.
 *
 * The allocator uses one arena and grows the most recent allocation in place.
 * This avoids the temporary old-plus-new peak required by a conventional
 * copying realloc, which is important on the memory-constrained Q9 target.
 * qclib intentionally has no free operation: the toolchain does not need it
 * and OS-9 releases the arena when the process exits. The implementation also
 * stays within the QCC bootstrap subset. */

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

/* Function: qm_free
 * Releases an allocation logically. The arena is reclaimed at process exit.
 * Parameters: a IR argument frame containing the pointer. */
void qm_free(int *a)
{
	(void) a;
}

/* Function: qm_malloc
 * Allocates one block from the qclib arena.
 * Parameters: a IR argument frame containing the size.
 * Returns: Block address, or null on failure. */
char *qm_malloc(int *a)
{
	int args[2];
	args[0] = 0;
	args[1] = a[0];
	return qm_realloc(args);
}

/* Function: qm_calloc
 * Allocates and clears an array from the qclib arena.
 * Parameters: a IR argument frame containing count and element size.
 * Returns: Cleared block address, or null on failure. */
char *qm_calloc(int *a)
{
	int args[2];
	char *p;
	int n;
	int i;

	n = a[0] * a[1];
	args[0] = 0;
	args[1] = n;
	p = qm_realloc(args);
	if (p == 0)
		return 0;
	i = 0;
	while (i < n) {
		p[i] = 0;
		i++;
	}
	return p;
}
