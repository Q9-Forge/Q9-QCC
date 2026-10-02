#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"

int qmake_file_info(const char *path, long *modified)
{
    struct stat info;
    if (stat(path, &info) != 0) return 0;
    *modified = (long)info.st_mtime;
    return 1;
}

int qmake_run(const char *command)
{
    return system(command);
}

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

int qmake_stdout_is_terminal(void)
{
    return isatty(STDOUT_FILENO);
}

double qmake_time_seconds(void)
{
    struct timeval now;
    if (gettimeofday(&now, (struct timezone *)0) != 0) return 0.0;
    return (double)now.tv_sec + (double)now.tv_usec / 1000000.0;
}

int qmake_change_directory(const char *path)
{
    return chdir(path) == 0;
}

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

int qmake_default_host_config_dir(char *out, size_t capacity)
{
    const char *arch;
    const char *root;
    size_t needed;
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
