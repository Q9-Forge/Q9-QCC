/* qmake platform adapter for native OS-9/68K.
 *
 * Purpose:
 *   Implements platform.h's contract using only the small F$/I$ wrappers
 *   already proven in this repository (Q9-QCC/src/qcc_os9_bridge_q9.c),
 *   not Microware's own runtime. Microware's system() is deliberately NOT
 *   used here: live measurement (see the os9-dev skill's c/os9-clib-
 *   reference.md, "system() may launch nothing, and the return value will
 *   not tell you") shows it can silently launch nothing at all depending on
 *   the account's default shell, while still reporting success. Recipes are
 *   therefore split and forked directly, the same way qcc_os9_bridge_q9.c
 *   already does for the QCC driver's own pipeline.
 *
 * Edition history:
 *   2026-10-02  First version, written for the qmake Q9/68K port.
 */
#include <stdio.h>
#include <string.h>
#include "platform.h"

extern int _os_load(char *name, int mode, int color, void **module);
extern int _os_unlink(void *module);
extern int _os_fork(int typelang, long addmem, long paramsize, int numpaths,
		    int priority, char *name, char *params);
extern int _os_wait(int *status);
extern int _os_open(char *name, int mode, int *path);
extern int _os_close(int path);
extern int _os_gs_fd(int path, int size, char *fdbuf);
extern int _os_chgdir(char *path, int mode);
extern unsigned long _os_ticks(void);

/* qmake never needs an SDK auto-discovery on Q9 itself: the documented,
 * agreed configuration directory is the fixed /dd/SYS (see Q9-Make's
 * README), not an environment-driven search like on the development
 * hosts. Both functions therefore simply report "not available". */
int qmake_default_config_dir(const char *target_arch, char *out, size_t capacity)
{
	(void)target_arch;
	(void)out;
	(void)capacity;
	return 0;
}

int qmake_default_host_config_dir(char *out, size_t capacity)
{
	(void)out;
	(void)capacity;
	return 0;
}

/* No ANSI escape handling on the Q9 console for this first port; always
 * report a plain, non-terminal stream so qmake skips color codes. */
int qmake_stdout_is_terminal(void)
{
	return 0;
}

/* F$Time's tick counter, divided by the commonly documented 68k tick rate
 * (100 Hz). Only used for human-readable build-duration output, never for
 * build correctness, so an unconfirmed tick rate is an accepted risk. */
double qmake_time_seconds(void)
{
	return (double) _os_ticks() / 100.0;
}

/* qmake only ever needs its own data directory (chd), never the execution
 * directory (chx); mode 0 selects chd. */
int qmake_change_directory(const char *path)
{
	return _os_chgdir((char *) path, 0) == 0;
}

/* Equivalent of POSIX stat(): does the file exist, and (if so) a value that
 * only needs to compare correctly against another call's result, not decode
 * into a real calendar date. Built from the RBF file-descriptor sector's
 * fd_date field (year-1900/month/day/hour/minute, 5 bytes at offset 3) via
 * I$GetStt/SS_FD -- already wired and documented in qclib (rbf.h, os9call.a)
 * for chown(3); reused here read-only. Generous per-field radixes avoid any
 * chance of two different dates folding to the same value; absolute
 * magnitude is never interpreted. */
int qmake_file_info(const char *path, long *modified)
{
	int p;
	char fdbuf[8];
	long value;

	if (_os_open((char *) path, 1, &p) != 0) return 0;
	value = 0;
	if (_os_gs_fd(p, 8, fdbuf) == 0) {
		value = (unsigned char) fdbuf[3];
		value = value * 13 + (unsigned char) fdbuf[4];
		value = value * 32 + (unsigned char) fdbuf[5];
		value = value * 25 + (unsigned char) fdbuf[6];
		value = value * 61 + (unsigned char) fdbuf[7];
	}
	_os_close(p);
	*modified = value;
	return 1;
}

#define Q9_MAX_WORDS 64
#define Q9_MAX_PARAMS 480

/* Splits one recipe line into words in place, NUL-terminating each one.
 * Handles a single level of "double quoted" words (no escapes, no nesting
 * -- the only quoting style q9makefile recipes actually use) and leaves
 * "&&" as an ordinary word so the caller can treat it as a chain
 * separator. Returns the word count, or -1 if there are more than
 * Q9_MAX_WORDS. */
static int q9_split_words(char *command, char **words)
{
	int n;
	char *p;

	n = 0;
	p = command;
	while (*p != '\0') {
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0') break;
		if (n >= Q9_MAX_WORDS) return -1;
		if (*p == '"') {
			p++;
			words[n++] = p;
			while (*p != '\0' && *p != '"') p++;
			if (*p == '"') {
				*p = '\0';
				p++;
			}
		} else {
			words[n++] = p;
			while (*p != '\0' && *p != ' ' && *p != '\t') p++;
			if (*p != '\0') {
				*p = '\0';
				p++;
			}
		}
	}
	return n;
}

/* Loads and forks one already-split command (argv[0] is the module name,
 * argv[1..n-1] its parameters), waits for it, and returns its exit status.
 *
 * Two module kinds need two different parameter encodings, and nothing in
 * a module's header tells them apart -- it depends on which startup code
 * the target was linked against:
 *   - this project's own QCC-built tools (qcc, qcpp, qcir, ...), linked
 *     against q9_start.a, expect the structured argv/offset table q9_start.a
 *     itself parses (see qcc_os9_bridge_q9.c, which this logic mirrors);
 *     they are staged under /dd/CMDS_QCC.
 *   - traditional OS-9 utilities (mkdir, rm, cp, ...), linked against
 *     Microware's own startup code, expect a single CR-terminated
 *     blank-separated parameter line instead; they live under /dd/CMDS.
 * Resolving the bare name under /dd/CMDS_QCC first, falling back to
 * /dd/CMDS, is the same proven heuristic qcc_os9_bridge_q9.c already uses
 * for exactly this reason. An explicit absolute path (argv[0][0] == '/')
 * is trusted as given and always uses the legacy encoding, since every
 * q9makefile recipe written so far only ever gives an absolute path for a
 * plain OS-9 utility, never for one of this project's own tools. */
static int q9_exec_words(char **argv, int argc)
{
	char load_path[256];
	char parameters[Q9_MAX_PARAMS + 32];
	int arg_offsets[32];
	void *header;
	int i;
	int j;
	int arg_count;
	int used;
	int param_size;
	int pid;
	int status;
	int rc;
	int legacy_params;

	if (argc < 1) return -1;
	used = 0;
	j = 0;
	while (argv[0][j] != '\0' && used < 254) {
		load_path[used++] = argv[0][j];
		j++;
	}
	load_path[used] = '\0';
	legacy_params = (argv[0][0] == '/');
	header = 0;
	rc = -1;
	if (!legacy_params) {
		strcpy(load_path, "/dd/CMDS_QCC/");
		j = 0;
		used = (int) strlen(load_path);
		while (argv[0][j] != '\0' && used < 254) {
			load_path[used++] = argv[0][j];
			j++;
		}
		load_path[used] = '\0';
		rc = _os_load(load_path, 1, 0, &header);
	}
	if (rc != 0 || header == 0) {
		legacy_params = 1;
		if (argv[0][0] == '/') {
			strcpy(load_path, argv[0]);
		} else {
			strcpy(load_path, "/dd/CMDS/");
			j = 0;
			used = (int) strlen(load_path);
			while (argv[0][j] != '\0' && used < 254) {
				load_path[used++] = argv[0][j];
				j++;
			}
			load_path[used] = '\0';
		}
		header = 0;
		rc = _os_load(load_path, 1, 0, &header);
		if (rc != 0 || header == 0) return -1;
	}

	if (!legacy_params) {
		used = 0;
		arg_count = 0;
		for (i = 1; i < argc; ++i) {
			if (arg_count >= 32 || used >= Q9_MAX_PARAMS) return -1;
			arg_offsets[arg_count++] = used;
			for (j = 0; argv[i][j] != '\0'; ++j) {
				if (used >= Q9_MAX_PARAMS) return -1;
				parameters[used++] = argv[i][j];
			}
			if (used >= Q9_MAX_PARAMS) return -1;
			parameters[used++] = '\0';
		}
		if (used & 1) parameters[used++] = '\0';
		for (j = 0; j < 4; ++j) parameters[used++] = '\0';
		for (i = 0; i < arg_count; ++i) {
			if (used > Q9_MAX_PARAMS + 24 || arg_offsets[i] < 0 ||
			    arg_offsets[i] > 65535) return -1;
			parameters[used++] = 0;
			parameters[used++] = 0;
			parameters[used++] = (char) ((arg_offsets[i] >> 8) & 255);
			parameters[used++] = (char) (arg_offsets[i] & 255);
		}
		for (i = 0; i < 8; ++i) parameters[used++] = '\0';
		param_size = used;
	} else {
		used = 0;
		for (i = 1; i < argc; ++i) {
			if (i != 1) parameters[used++] = ' ';
			for (j = 0; argv[i][j] != '\0'; ++j)
				parameters[used++] = argv[i][j];
		}
		parameters[used++] = 13;
		param_size = used;
	}

	pid = _os_fork(0, 0L, (long) param_size, 3, 0, argv[0], parameters);
	if (pid < 0) {
		_os_unlink(header);
		return -1;
	}
	status = 0;
	pid = _os_wait(&status);
	_os_unlink(header);
	if (pid < 0) return -1;
	return status;
}

/* Runs one q9makefile recipe line. Only "&&" chaining is understood (no
 * pipes, no redirection, no ';' or '&background') -- every Q9-target
 * recipe written so far is either a single command or an "a && b" chain of
 * plain commands, and this project's own system() measurement (see the
 * file header) rules out handing the line to a shell instead. */
int qmake_run(const char *command)
{
	char buffer[1024];
	char *words[Q9_MAX_WORDS];
	int count;
	int i;
	int start;
	int end;
	int status;

	i = 0;
	while (command[i] != '\0' && i < 1023) {
		buffer[i] = command[i];
		++i;
	}
	buffer[i] = '\0';

	count = q9_split_words(buffer, words);
	if (count < 0) return -1;
	if (count == 0) return 0;

	start = 0;
	while (start < count) {
		end = start;
		while (end < count && strcmp(words[end], "&&") != 0) ++end;
		if (end == start) return -1;
		status = q9_exec_words(words + start, end - start);
		if (status != 0) return status;
		start = end + 1;
	}
	return 0;
}

/* qclib's tmpfile() is a documented stub that always returns null (no
 * temporary-file service exposed yet; see qclib/STATUS.md), so qmake.c's
 * own run_command() never actually reaches this function -- it already
 * falls back to qmake_run() whenever tmpfile() fails. Kept as a safe
 * equivalent rather than left undefined, in case that changes later. */
int qmake_run_capture(const char *command, FILE *output)
{
	(void) output;
	return qmake_run(command);
}
