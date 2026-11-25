/*
 * File: qcc_q9.c
 * Description: Build the Q9-native compiler driver translation unit.
 * Owner: cla / afoe
 * Version: 0.2
 * Repository: Q9-Forge/Q9-QCC
 *
 * Edition History
 * 2026-11-25  0.2  Normalized the source header for the module audit.
 */
/* Compile the QCC driver itself for its native OS-9 runtime. */
#define _Q9OS 1
#define QCC_NATIVE_BRIDGE 1
#include "qcc_os9_bridge_q9.c"
#include "qcc.c"
