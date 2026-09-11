/* Memory probe for the target system.
 *
 * CREATED AFTER A FAILURE: running QCC against qclib on the target
 * (test/qcc_68k.sh) failed with "qcc: kein Speicher fuer Aktions-Log" because
 * realloc returned 0.
 *
 * Four measurements are performed in one emulator run:
 *   1. The largest free block. F$SRqMem with -1 in d0.l returns exactly
 *      den (Handbuch: "If -1 is passed in d0.l, the largest block of free
 *      memory of the specified type is allocated").
 *   2. The parser's ACTUAL ladder: first the 524288-byte input buffer, then
 *      the action log from 1024 entries up to 262144. This is the host-measured
 *      final size; each entry occupies 12 bytes on 68k. The sequence must fit.
 *   3. The largest free block AFTERWARD. It decreases by the arena qclib obtains
 *      on the first realloc (largest block minus reserve), not by the sum of
 *      ladder steps. This measurement ruled out a leak in the first failure
 *      and identified the peak requirement.
 *   4. How far the ladder extends beyond the required size.
 *
 * The error code is printed as well: _os_srqmem returns it, and without
 * without it, the final output would only say "failed".
 */

#include <stdio.h>
#include <stdlib.h>
extern int _os_srqmem(int want, int *granted, char **addr);
extern int _os_srtmem(int size, char *addr);

/* Der groesste freie Block, ohne ihn zu behalten. */
int groesster(void)
{
	char *p;
	int gr;
	int rc;

	gr = 0;
	p = 0;
	rc = _os_srqmem(-1, &gr, &p);
	if (rc != 0)
		return -rc;
	_os_srtmem(gr, p);
	return gr;
}

int main()
{
	char *eingabe;
	char *log;
	char *q;
	int vorher;
	int nachher;
	int n;
	int summe;

	vorher = groesster();
	printf("groesster Block vorher: %d\n", vorher);

	eingabe = realloc(0, 524288);
	if (eingabe == 0) {
		printf("eingabepuffer: FEHL\n");
		return 1;
	}
	printf("eingabepuffer 524288: ok\n");

	/* Die Leiter des Aktions-Logs, 12 Byte je Eintrag. */
	summe = 0;
	log = 0;
	n = 12288;
	while (n <= 3145728) {
		q = realloc(log, n);
		if (q == 0) {
			printf("log %d: FEHL\n", n);
			printf("groesster Block dabei: %d\n", groesster());
			return 1;
		}
		q[0] = 1;
		q[n - 1] = 2;
		log = q;
		summe = summe + n;
		n = n * 2;
	}
	printf("log bis 3145728: ok, summe aller sprossen %d\n", summe);

	nachher = groesster();
	printf("groesster Block nachher: %d\n", nachher);
	printf("verbraucht: %d\n", vorher - nachher);

	/* How far does it extend beyond the required size? */
	while (n <= 50331648) {
		q = realloc(log, n);
		if (q == 0) {
			printf("darueber %d: FEHL (das ist in Ordnung)\n", n);
			break;
		}
		log = q;
		printf("darueber %d: ok\n", n);
		n = n * 2;
	}

	/* Nothing is released here: qclib keeps one arena and has no free (see
	   src/mem.c). OS-9 returns it when the process exits. The largest free block
	   is therefore smaller by the arena size after the first realloc; that is
	   the expected measurement, not a leak. */
	printf("sonde zu ende\n");
	return 0;
}
