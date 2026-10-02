/* Bridges platform_posix.c's #include <sys/stat.h> to the Microware SDK's
 * own location for the same declarations (DEFS/UNIX/stat.h, no "sys/"
 * prefix on this SDK). The struct stat it declares has a real st_mtime,
 * so platform_posix.c's qmake_file_info() works unmodified against XCC. */
#include <UNIX/stat.h>
