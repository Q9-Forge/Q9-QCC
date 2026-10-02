#ifndef QMAKE_PLATFORM_H
#define QMAKE_PLATFORM_H
#include <stddef.h>
#include <stdio.h>
int qmake_file_info(const char *path, long *modified);
int qmake_run(const char *command);
int qmake_run_capture(const char *command, FILE *output);
int qmake_stdout_is_terminal(void);
double qmake_time_seconds(void);
int qmake_change_directory(const char *path);
int qmake_default_config_dir(const char *target_arch, char *out, size_t capacity);
int qmake_default_host_config_dir(char *out, size_t capacity);
#endif
