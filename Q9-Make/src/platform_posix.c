/*--------------------------------------------------------------------------------------
 * File:        platform_posix.c
 * Description: POSIX platform services for Q9-Make.
 * Owner:       cla / afoe
 * Version:     0.01
 *
 * This file is part of the Q9-Forge repository. See https://github.com/Q9-Forge.
 *
 * Edition History
 * Date    | Ver. | Description                                               | By
 * 26-11-26| 0.01 | Add file and function headers.                            | cla
 *--------------------------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"

/*--------------------------------------------------------------------------------------
 * Function:    qmake_file_info
 * Description: Read a file modification time.
 * Parameter:   const char * path: File path.
 *              long * modified: Receives the modification time.
 * Return:      int: 1 on success, 0 on failure.
 *--------------------------------------------------------------------------------------*/
int qmake_file_info(const char *path, long *modified)
{
    struct stat info;
    if (stat(path, &info) != 0) return 0;
    *modified = (long)info.st_mtime;
    return 1;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_run
 * Description: Run a command using the host shell.
 * Parameter:   const char * command: Command to run.
 * Return:      int: Host shell status.
 *--------------------------------------------------------------------------------------*/
int qmake_run(const char *command)
{
    return system(command);
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_run_capture
 * Description: Run a command and copy its standard output to a stream.
 * Parameter:   const char * command: Command to run.
 *              FILE * output: Destination stream.
 * Return:      int: Host command status, or -1 on a capture failure.
 *--------------------------------------------------------------------------------------*/
int qmake_run_capture(const char *command, FILE *output)
{
    FILE *pipe;
    char buffer[512];
    size_t count;
    int status;
    pipe = popen(command, "r");
    if (pipe == (FILE *)0) return -1;
    while ((count = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
        if (fwrite(buffer, 1, count, output) != count) {
            pclose(pipe);
            return -1;
        }
    status = pclose(pipe);
    return status;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_stdout_is_terminal
 * Description: Determine whether standard output is a terminal.
 * Parameter:   void: No parameters.
 * Return:      int: Nonzero if standard output is a terminal, otherwise 0.
 *--------------------------------------------------------------------------------------*/
int qmake_stdout_is_terminal(void)
{
    return isatty(STDOUT_FILENO);
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_time_seconds
 * Description: Read the host clock in seconds.
 * Parameter:   void: No parameters.
 * Return:      double: Clock value in seconds, or 0.0 on failure.
 *--------------------------------------------------------------------------------------*/
double qmake_time_seconds(void)
{
    struct timeval now;
    if (gettimeofday(&now, (struct timezone *)0) != 0) return 0.0;
    return (double)now.tv_sec + (double)now.tv_usec / 1000000.0;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_change_directory
 * Description: Change the current working directory.
 * Parameter:   const char * path: Destination directory.
 * Return:      int: 1 on success, 0 on failure.
 *--------------------------------------------------------------------------------------*/
int qmake_change_directory(const char *path)
{
    return chdir(path) == 0;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_default_config_dir
 * Description: Find the default configuration directory for a target.
 * Parameter:   const char * target_arch: Target architecture name.
 *              char * out: Receives the directory path.
 *              size_t capacity: Size of the destination buffer.
 * Return:      int: 1 if a path was written, otherwise 0.
 *--------------------------------------------------------------------------------------*/
int qmake_default_config_dir(const char *target_arch, char *out, size_t capacity)
{
    const char *root = getenv("Q9SDK");
    size_t needed;
    if (target_arch == (const char *)0 || *target_arch == '\0') return 0;
    if (root == (const char *)0 || *root == '\0') {
        root = getenv("HOME");
        if (root == (const char *)0 || *root == '\0') return 0;
        needed = strlen(root) + sizeof("/Q9SDK/Q9/") - 1 +
                 strlen(target_arch) + sizeof("/SYS");
        if (needed > capacity) return 0;
        sprintf(out, "%s/Q9SDK/Q9/%s/SYS", root, target_arch);
    } else {
        needed = strlen(root) + sizeof("/Q9/") - 1 +
                 strlen(target_arch) + sizeof("/SYS");
        if (needed > capacity) return 0;
        sprintf(out, "%s/Q9/%s/SYS", root, target_arch);
    }
    return 1;
}

/*--------------------------------------------------------------------------------------
 * Function:    qmake_default_host_config_dir
 * Description: Find the shared host configuration directory, falling back to the
 *              per-host directory on macOS.
 * Parameter:   char * out: Receives the directory path.
 *              size_t capacity: Size of the destination buffer.
 * Return:      int: 1 if a path was written, otherwise 0.
 *--------------------------------------------------------------------------------------*/
int qmake_default_host_config_dir(char *out, size_t capacity)
{
    const char *arch;
    const char *root;
    size_t needed;
    long modified;

    root = getenv("Q9SDK");
    if (root != (const char *)0 && *root != '\0' &&
        strlen(root) + sizeof("/Q9/SYS/qmake.conf") <= capacity) {
        sprintf(out, "%s/Q9/SYS/qmake.conf", root);
        if (qmake_file_info(out, &modified)) {
            sprintf(out, "%s/Q9/SYS", root);
            return 1;
        }
    }
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
    arch = "ARM64";
#elif defined(__APPLE__) && defined(__x86_64__)
    arch = "x86_64";
#else
    (void)out;
    (void)capacity;
    return 0;
#endif
    root = getenv("Q9SDK");
    if (root == (const char *)0 || *root == '\0') return 0;
    needed = strlen(root) + sizeof("/macOS//SYS") - 1 + strlen(arch);
    if (needed > capacity) return 0;
    sprintf(out, "%s/macOS/%s/SYS", root, arch);
    return 1;
}
