/* Eigenstaendiger qo68k-CLI um den bestehenden Peephole-Kern. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fatal(const char *message)
{
	fprintf(stderr, "qo68k: %s\n", message);
	exit(1);
}

#include "qo68.c"

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: qo68k <input.s68k> <output.opt.s68k>\n");
		return 2;
	}
	peepholeRun(argv[1], argv[2]);
	return 0;
}
