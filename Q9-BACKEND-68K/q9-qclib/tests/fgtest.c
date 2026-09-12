/* Test program for fgets and ferror.
 *
 * WHY NOT hello.c: Microware clib is NOT a valid fgets oracle.
 * The host libc is the oracle. Measured with test/lineend68k.sh, clib's fgets
 * splits at $0d because Microware C maps \n to CR; QCC maps it to $0a, and all
 * files in this chain use that convention. The same program runs once with
 * clang on the host and once against qclib on the 68030; both outputs must be
 * character-identical.
 *
 * There is therefore no OS-9-specific path here: the filename is
 * relative so both sides can use it.
 *
 * The cases where fgets implementations differ are tested:
 *   - an ordinary line
 *   - an EMPTY line (only the line ending)
 *   - a line LONGER than the buffer (truncate at n-1, continue on the next call)
 *   - the final line WITHOUT a line ending
 *   - a call after end-of-file (must return 0)
 *   - ferror must remain clear afterward.
 */

#include <stdio.h>

int main()
{
	FILE *fp;
	char muster[24];
	char buf[8];
	char *r;
	int i;
	int n;
	int k;

	/* "a" LF LF "bbbbbbbbbb" LF "cc"  -- ohne Umbruch am Ende */
	muster[0] = 97;
	muster[1] = 10;
	muster[2] = 10;
	i = 3;
	while (i < 13) {
		muster[i] = 98;
		i++;
	}
	muster[13] = 10;
	muster[14] = 99;
	muster[15] = 99;

	fp = fopen("fgtest.bin", "w");
	if (fp == 0) {
		printf("fopen w geht nicht\n");
		return 1;
	}
	n = fwrite(muster, 1, 16, fp);
	printf("geschrieben %d ferror %d\n", n, ferror(fp));
	fclose(fp);

	fp = fopen("fgtest.bin", "r");
	if (fp == 0) {
		printf("fopen r geht nicht\n");
		return 1;
	}
	i = 1;
	r = fgets(buf, 6, fp);
	while (r != 0) {
		n = 0;
		while (buf[n] != 0)
			n++;
		printf("zeile %d laenge %d:", i, n);
		k = 0;
		while (k < n) {
			printf(" %d", buf[k] & 255);
			k++;
		}
		printf("\n");
		i++;
		r = fgets(buf, 6, fp);
	}
	printf("zeilen %d ferror %d\n", i - 1, ferror(fp));
	fclose(fp);
	return 0;
}
