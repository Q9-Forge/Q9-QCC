/*--------------------------------------------------------------------------------------
 * File:        platform_os9.c
 * Description: Native Q9/68k platform services for Q9-Make.
 * Owner:       cla / afoe
 * Version:     0.03
 *
 * This file is part of the Q9-Forge repository. See https://github.com/Q9-Forge.
 *
 * Edition History
 * Date    | Ver. | Description                                               | By
 * 26-11-26| 0.01 | Add file and function headers; revise comments.           | cla
 * 26-11-26| 0.02 | Reject overlong command parameters and release modules.   | cla
 * 26-11-26| 0.03 | Preserve module names at the path length limit.          | cla
 *--------------------------------------------------------------------------------------*/

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

/*--------------------------------------------------------------------------------------
 * Function:    qmake_default_config_dir
 * Description: Report that no target-specific directory is available.
 * Parameter:   const char * target_arch: Target architecture, unused.
 *              char * out: Destination buffer, unused.
 *              size_t capacity: Buffer size, unused.
 * Return:      int: Always 0.
 *--------------------------------------------------------------------------------------*/
int qmake_default_config_dir(const char *target_arch, char *out, size_t capacity)
{
	(void)target_arch;
	(void)out;
	(void)capacity;
	return 0;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_default_host_config_dir
 * Description: Return the native shared configuration directory.
 * Parameter:   char * out: Receives the directory path.
 *              size_t capacity: Size of the destination buffer.
 * Return:      int: 1 if a path was written, otherwise 0.
 *--------------------------------------------------------------------------------------*/
int qmake_default_host_config_dir(char *out, size_t capacity)
{
	if (capacity < sizeof("/dd/SYS"))
		return 0;
	strcpy(out, "/dd/SYS");
	return 1;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_stdout_is_terminal
 * Description: Disable terminal escape sequences for the native console.
 * Parameter:   void: No parameters.
 * Return:      int: Always 0.
 *--------------------------------------------------------------------------------------*/
int qmake_stdout_is_terminal(void)
{
	return 0;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_time_seconds
 * Description: Read the native tick counter using the assumed 100 Hz rate.
 * Parameter:   void: No parameters.
 * Return:      double: Elapsed time in seconds.
 *--------------------------------------------------------------------------------------*/
double qmake_time_seconds(void)
{
	return (double) _os_ticks() / 100.0;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_change_directory
 * Description: Change the current data directory.
 * Parameter:   const char * path: Destination directory.
 * Return:      int: 1 on success, 0 on failure.
 *--------------------------------------------------------------------------------------*/
int qmake_change_directory(const char *path)
{
	return _os_chgdir((char *) path, 0) == 0;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_file_info
 * Description: Read a file timestamp for comparison with other timestamps.
 *              The encoded value is not a calendar date; a status read failure
 *              leaves the timestamp at zero.
 * Parameter:   const char * path: File path.
 *              long * modified: Receives the encoded timestamp.
 * Return:      int: 1 if the file could be opened, otherwise 0.
 *--------------------------------------------------------------------------------------*/
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
#define Q9_ARG_HEADER_BYTES 4  /* Reserved prefix in the structured argument table. */
#define Q9_ARG_OFFSET_BYTES 4  /* Bytes per structured argument offset. */
#define Q9_ARG_TRAILER_BYTES 8 /* Reserved suffix in the structured argument table. */

/*--------------------------------------------------------------------------------------
 * Function:    q9_split_words
 * Description: Split a command into words in place; quoted words are supported.
 * Parameter:   char * command: Mutable command line.
 *              char ** words: Receives pointers to the resulting words.
 * Return:      int: Word count, or -1 if the limit is exceeded.
 *--------------------------------------------------------------------------------------*/
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

/*--------------------------------------------------------------------------------------
 * Function:    q9_exec_words
 * Description: Load and execute a command, then wait for its completion.
 *              Commands in /dd/CMDS_QCC use structured arguments; other commands
 *              use a carriage-return-terminated parameter line. Absolute paths use
 *              the latter form.
 * Parameter:   char ** argv: Command name and arguments.
 *              int argc: Number of command words.
 * Return:      int: Command status, or -1 on failure.
 *--------------------------------------------------------------------------------------*/
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

	if (argc < 1 || strlen(argv[0]) >= sizeof(load_path)) return -1;
	if (argv[0][0] != '/' &&
	    strlen(argv[0]) + sizeof("/dd/CMDS_QCC/") > sizeof(load_path)) return -1;
	used = 0;
	j = 0;
	while (argv[0][j] != '\0' && (size_t)used < sizeof(load_path) - 1) {
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
		while (argv[0][j] != '\0' && (size_t)used < sizeof(load_path) - 1) {
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
			while (argv[0][j] != '\0' && (size_t)used < sizeof(load_path) - 1) {
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
			if (arg_count >= 32 || used >= Q9_MAX_PARAMS) goto parameter_error;
			arg_offsets[arg_count++] = used;
			for (j = 0; argv[i][j] != '\0'; ++j) {
				if (used >= Q9_MAX_PARAMS) goto parameter_error;
				parameters[used++] = argv[i][j];
			}
			if (used >= Q9_MAX_PARAMS) goto parameter_error;
			parameters[used++] = '\0';
		}
		if (used & 1) parameters[used++] = '\0';
		if ((size_t)used + Q9_ARG_HEADER_BYTES +
		    (size_t)arg_count * Q9_ARG_OFFSET_BYTES + Q9_ARG_TRAILER_BYTES >
		    sizeof(parameters))
			goto parameter_error;
		for (j = 0; j < 4; ++j) parameters[used++] = '\0';
		for (i = 0; i < arg_count; ++i) {
			if (arg_offsets[i] < 0 || arg_offsets[i] > 65535)
				goto parameter_error;
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
			if (i != 1) {
				if ((size_t)used >= sizeof(parameters) - 1) goto parameter_error;
				parameters[used++] = ' ';
			}
			for (j = 0; argv[i][j] != '\0'; ++j) {
				if ((size_t)used >= sizeof(parameters) - 1) goto parameter_error;
				parameters[used++] = argv[i][j];
			}
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

parameter_error:
	_os_unlink(header);
	return -1;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_run
 * Description: Run one recipe with optional && command chaining.
 * Parameter:   const char * command: Recipe to run.
 * Return:      int: Command status, or -1 on failure.
 *--------------------------------------------------------------------------------------*/
int qmake_run(const char *command)
{
	char buffer[1024];
	char *words[Q9_MAX_WORDS];
	int count;
	int i;
	int start;
	int end;
	int status;

	if (strlen(command) >= sizeof(buffer)) return -1;
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

/*--------------------------------------------------------------------------------------
 * Function:    qmake_run_capture
 * Description: Run a recipe without capturing its output. The caller currently
 *              falls back to qmake_run when temporary files are unavailable.
 * Parameter:   const char * command: Recipe to run.
 *              FILE * output: Output stream, unused.
 * Return:      int: Command status, or -1 on failure.
 *--------------------------------------------------------------------------------------*/
int qmake_run_capture(const char *command, FILE *output)
{
	(void) output;
	return qmake_run(command);
}
