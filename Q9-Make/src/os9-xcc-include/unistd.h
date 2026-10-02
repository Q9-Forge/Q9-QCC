/* Bridges platform_posix.c's #include <unistd.h> to the Microware SDK's
 * own declarations for isatty/chdir/popen/pclose, spread across modes.h
 * and DEFS/UNIX/os9def.h on this SDK (there is no unistd.h here at all).
 *
 * _OPT_PROTOS gates every prototype in os9def.h (including popen's, which
 * must return FILE* -- without it the declarations disappear entirely and
 * popen silently falls back to an implicit int return, a link-time type
 * mismatch against platform_posix.c's "pipe = popen(...)"). FILE itself
 * must already be known when os9def.h is read, so <stdio.h> comes first.
 */
#define _OPT_PROTOS
#include <stdio.h>
#include <modes.h>
#include <UNIX/os9def.h>

#define STDOUT_FILENO 1
