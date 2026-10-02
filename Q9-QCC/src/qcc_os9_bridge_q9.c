/* Native Q9 process bridge for the QCC driver.
 *
 * This unit is compiled by QCC and uses the small F$/I$ wrappers in qclib;
 * it deliberately has no Microware/XCC headers or runtime dependencies.
 */
extern int _os_load(char *name, int mode, int color, void **module);
extern int _os_unlink(void *module);
extern int _os_fork(int typelang, long addmem, long paramsize, int numpaths,
		    int priority, char *name, char *params);
extern int _os_wait(int *status);
extern int _os_create(char *name, int mode, int *path, int perms);
extern int _os_close(int path);
extern int _os_dup(int path);

static int q9_append(char *dst, int used, const char *src)
{
	int i;
	i = 0;
	while (src[i] != '\0') {
		if (used >= 254) return -1;
		dst[used] = src[i];
		++used;
		++i;
	}
	return used;
}

/* the C startup module's native argv layout is a string area followed by a table of
 * 32-bit offsets (argv[1..n], argv[0] sentinel, envp sentinel, final
 * sentinel).  Its startup code walks that table backwards and turns the
 * offsets into argv pointers in place. */
static int q9_append_offset(char *dst, int used, int offset)
{
	if (used > 508 || offset < 0 || offset > 65535) return -1;
	dst[used++] = 0;
	dst[used++] = 0;
	dst[used++] = (char)((offset >> 8) & 255);
	dst[used++] = (char)(offset & 255);
	return used;
}

/* The Q9 kernel's F$Load service asks the IOMan to load/validate a module;
 * F$Fork then starts its resident module-directory entry. OS-9 command
 * parameters are a single blank-separated string, not a host argv vector.
 */
int q9_os9exec(const char *module, char **argv, char **environment)
{
	char load_path[256];
	char parameters[512];
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
	(void)environment;

	if (module == 0 || argv == 0) return -1;
	used = 0;
	if (module[0] == '/') {
		used = q9_append(load_path, used, module);
	} else {
		used = q9_append(load_path, used, "/dd/CMDS_QCC/");
		if (used >= 0) used = q9_append(load_path, used, module);
	}
	if (used < 0) return -1;
	load_path[used] = '\0';

	used = 0;
	arg_count = 0;
	for (i = 1; argv[i] != 0; ++i) {
		if (arg_count >= 32 || used >= 480) return -1;
		arg_offsets[arg_count++] = used;
		for (j = 0; argv[i][j] != '\0'; ++j) {
			if (used >= 480) return -1;
			parameters[used++] = argv[i][j];
		}
		if (used >= 480) return -1;
		parameters[used++] = '\0';
	}
	if (used & 1) parameters[used++] = '\0';
	/* the startup module scans the environment terminator first, then walks the argv
	 * offsets backwards.  Therefore argv's zero sentinel belongs before the
	 * offsets in memory; the other two zero longwords follow them. */
	if (used > 508) return -1;
	for (j = 0; j < 4; ++j) parameters[used++] = '\0';
	for (i = 0; i < arg_count; ++i) {
		used = q9_append_offset(parameters, used, arg_offsets[i]);
		if (used < 0) return -1;
	}
	if (used > 504) return -1;
	for (i = 0; i < 8; ++i) parameters[used++] = '\0';
	param_size = used;
	header = 0;
	legacy_params = 0;
	rc = _os_load(load_path, 1, 0, &header);
	if (rc != 0 || header == 0) {
		/* Compiler stages live in CMDS_QCC; OS utilities such as makdir,
		 * copy and del live in the normal system command directory. */
		if (module[0] == '/') return -1;
		used = 0;
		used = q9_append(load_path, used, "/dd/CMDS/");
		if (used < 0) return -1;
		used = q9_append(load_path, used, module);
		if (used < 0) return -1;
		load_path[used] = '\0';
		header = 0;
		rc = _os_load(load_path, 1, 0, &header);
		if (rc != 0 || header == 0) return -1;
		legacy_params = 1;
	}
	if (legacy_params) {
		/* Traditional OS-9 system commands consume a CR-terminated raw
		 * parameter line; unlike C tools using the startup module they do not use
		 * the structured argv table. */
		used = 0;
		for (i = 1; argv[i] != 0; ++i) {
			if (i != 1) {
				if (used >= 510) return -1;
				parameters[used++] = ' ';
			}
			for (j = 0; argv[i][j] != '\0'; ++j) {
				if (used >= 510) return -1;
				parameters[used++] = argv[i][j];
			}
		}
		parameters[used++] = 13;
		param_size = used;
	}

	/* Fork's name lookup uses the resident module name, so pass the configured
	 * command name (not the filesystem path used for F$Load). */
	pid = _os_fork(0, 0L, (long)param_size, 3, 0, (char *)module,
		       parameters);
	if (pid < 0) {
		_os_unlink(header);
		return pid;
	}
	status = 0;
	pid = _os_wait(&status);
	_os_unlink(header);
	if (pid < 0) return -1;
	return status;
}

/* Redirect child stdout without relying on qclib's freopen implementation.
 * Duplicate the caller's stdout, create the destination, then use OS-9's
 * lowest-free-path I$Dup behavior to install it as path 1 for the child.
 */
int q9_os9exec_stdout(const char *module, char **argv, char **environment,
		      const char *path)
{
	int saved_stdout;
	int output_path;
	int installed;
	int result;

	if (path == 0) return -1;
	saved_stdout = _os_dup(1);
	if (saved_stdout < 0) return -1;
	output_path = 0;
	if (_os_create((char *)path, 2, &output_path, 0x03) != 0 || output_path == 0) {
		_os_close(saved_stdout);
		return -1;
	}
	if (_os_close(1) != 0) {
		_os_close(output_path);
		_os_dup(saved_stdout);
		_os_close(saved_stdout);
		return -1;
	}
	installed = _os_dup(output_path);
	_os_close(output_path);
	if (installed != 1) {
		if (installed >= 0) _os_close(installed);
		_os_dup(saved_stdout);
		_os_close(saved_stdout);
		return -1;
	}
	result = q9_os9exec(module, argv, environment);
	_os_close(1);
	installed = _os_dup(saved_stdout);
	_os_close(saved_stdout);
	if (installed != 1) return -1;
	return result;
}
