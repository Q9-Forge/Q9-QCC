/* netinet/in.h -- Internet address structures and protocol numbers.
 *
 * Derived from the specification, no template used.
 * Status: struct in_addr, struct sockaddr_in, inet_ntoa, INADDR_ANY,
 * IPPROTO_ICMP/TCP/UDP belegt; struct ip_mreq and IPPROTO_IP derived.
 * This header also makes the byte order functions of sys/endian.h
 * available.
 */
#ifndef Q9_SPF_BSD_NETINET_IN_H
#define Q9_SPF_BSD_NETINET_IN_H

#include <SPF/BSD/sys/types.h>
#include <sys/endian.h>

#define IPPROTO_IP    0    /* derived: protocol level IP */
#define IPPROTO_ICMP  1
#define IPPROTO_TCP   6
#define IPPROTO_UDP   17

#define INADDR_ANY    ((u_long)0x00000000)

struct in_addr {
	u_long s_addr;         /* network byte order */
};

struct sockaddr_in {
	u_char         sin_len;       /* @0  */
	u_char         sin_family;    /* @1  */
	u_short        sin_port;      /* @2  network byte order */
	struct in_addr sin_addr;      /* @4  */
	char           sin_zero[8];   /* @8  */
};

/* derived: multicast membership request, member order from the common convention */
struct ip_mreq {
	struct in_addr imr_multiaddr;   /* @0 multicast group address */
	struct in_addr imr_interface;   /* @4 local interface address */
};

extern char *inet_ntoa(struct in_addr in);

#endif /* Q9_SPF_BSD_NETINET_IN_H */

/* not defined (no documented value):
 *   IP_ADD_MEMBERSHIP
 */
