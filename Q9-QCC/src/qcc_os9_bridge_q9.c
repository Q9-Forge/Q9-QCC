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

/* The Q9 kernel's F$Load service asks the IOMan to load/validate a module;
 * F$Fork then starts its resident module-directory entry. OS-9 command
 * parameters are a single blank-separated string, not a host argv vector.
 */
int q9_os9exec(const char *module, char **argv, char **environment)
{
	char load_path[256];
	char parameters[256];
	void *header;
	int i;
	int used;
	int param_size;
	int pid;
	int status;
	int rc;
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
	for (i = 1; argv[i] != 0; ++i) {
		if (used != 0) {
			if (used >= 254) return -1;
			parameters[used++] = ' ';
		}
		used = q9_append(parameters, used, argv[i]);
		if (used < 0) return -1;
	}
	parameters[used] = '\0';
	param_size = used + 1;
	header = 0;
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
	}

	/* Fork's name lookup uses the resident module name, so pass the configured
	 * command name (not the filesystem path used for F$Load). */
	pid = _os_fork(0, 0L, (long)param_size, 3, 0, (char *)module,
		       parameters);
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
