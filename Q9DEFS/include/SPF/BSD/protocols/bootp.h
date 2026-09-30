/* protocols/bootp.h -- BOOTP packet layout and constants.
 *
 * Derived from the specification, no template used.
 * Status: everything derived from the published BOOTP packet format;
 * member names are the ones the consumers use, all other members are
 * unnamed filler that keeps the documented offsets (300 bytes in total).
 */
#ifndef Q9_SPF_BSD_PROTOCOLS_BOOTP_H
#define Q9_SPF_BSD_PROTOCOLS_BOOTP_H

#include <types.h>
#include <SPF/BSD/sys/types.h>
#include <SPF/BSD/netinet/in.h>

#define BOOTREQUEST     1    /* derived: op value of a client request */
#define HTYPE_ETHERNET  1    /* derived: hardware type Ethernet */
#define IPPORT_BOOTPS   67   /* derived: UDP port of the server */
#define IPPORT_BOOTPC   68   /* derived: UDP port of the client */

/* derived: packet format of the BOOTP protocol */
struct bootp {
	u_char         bp_op;             /* @0   message type */
	u_char         bp_htype;          /* @1   hardware address type */
	u_char         bp_hlen;           /* @2   hardware address length */
	u_char         _filler_3;         /* @3   */
	u_int32        bp_xid;            /* @4   transaction id */
	u_char         _filler_8[2];      /* @8   */
	u_int16        bp_flags;          /* @10  */
	u_char         _filler_12[4];     /* @12  */
	struct in_addr bp_yiaddr;         /* @16  */
	u_char         _filler_20[8];     /* @20  */
	u_char         _filler_28[16];    /* @28  */
	u_char         _filler_44[64];    /* @44  */
	char           bp_file[128];      /* @108 boot file name */
	u_char         _filler_236[64];   /* @236 */
};

#endif /* Q9_SPF_BSD_PROTOCOLS_BOOTP_H */

/* not defined (no documented value): none */
