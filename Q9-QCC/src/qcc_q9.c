/* Compile the QCC driver itself for its native OS-9 runtime. */
#define _Q9OS 1
#define QCC_NATIVE_BRIDGE 1
#include "qcc_os9_bridge_q9.c"
#include "qcc.c"
