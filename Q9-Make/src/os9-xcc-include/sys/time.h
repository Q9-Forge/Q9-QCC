/* Bridges platform_posix.c's #include <sys/time.h> (for gettimeofday) to
 * the Microware SDK's own location: DEFS/UNIX/os9time.h, not sys/time.h. */
#include <UNIX/os9time.h>
