/*
 * File: qcc_os9_bridge.c
 * Description: Provide the Q9 process-launch bridge for the native target build.
 * Owner: cla / afoe
 * Version: 0.2
 * Repository: Q9-Forge/Q9-QCC
 *
 * Edition History
 * 2026-11-25  0.2  Normalized the source header for the module audit.
 */
#if defined(_Q9OS) || defined(_OSK)
#include <stdio.h>

extern int os9exec();
extern int os9forkc();
extern int wait();

static int q9_xcc_os9forkc(const char *module, int priority,
			   const char *parameters, int parameter_length,
			   int mode, int data_size, int stack_size, int language)
{
	return os9forkc(module, priority, parameters, parameter_length, mode,
			data_size, stack_size, language);
}

int q9_os9exec(const char *module, char **argv, char **environment)
{
	int pid;
	int status;
	unsigned int child_status;
	child_status = 0;
	pid = os9exec(q9_xcc_os9forkc, module, argv, environment, 0, 0, 3);
	if (pid < 0) return -1;
	status = wait(&child_status);
	if (status < 0) return status;
	if ((child_status & 0xFF00U) == 0x0100U)
		return (int)(child_status & 0x00FFU);
	return (int)child_status;
}

/* stdio redirection must use the same Microware ABI as the process launch:
 * QCC-generated calls to freopen/fclose do not reliably preserve the OS-9
 * standard path inherited by child modules. */
int q9_os9exec_stdout(const char *module, char **argv, char **environment,
		      const char *path)
{
	int result;
	if (freopen(path, "w", stdout) == NULL) return -1;
	result = q9_os9exec(module, argv, environment);
	fclose(stdout);
	return result;
}
#endif
