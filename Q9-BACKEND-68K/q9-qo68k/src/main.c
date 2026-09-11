/*
 * qo68k command-line entry point
 *
 * Edition history:
 *   2026-09-11  Introduced the English source-header format.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fatal(const char *message)
{
	fprintf(stderr, "qo68k: %s\n", message);
	exit(1);
}

#include "qo68.c"

/* Function: main
 * Runs the standalone peephole optimizer command.
 * Parameters: argc, argv Command-line argument count and vector.
 * Returns: Process status, zero on success. */
int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: qo68k <input.s68k> <output.opt.s68k>\n");
		return 2;
	}
	peepholeRun(argv[1], argv[2]);
	return 0;
}
