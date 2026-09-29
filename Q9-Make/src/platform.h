#ifndef QMAKE_PLATFORM_H
#define QMAKE_PLATFORM_H
#include <stddef.h>
int qmake_file_info(const char *path, long *modified);
int qmake_run(const char *command);
int qmake_change_directory(const char *path);
int qmake_default_config_dir(const char *target_arch, char *out, size_t capacity);
#endif
