/* net/if.h -- network interface request structure.
 *
 * Derived from the specification, no template used.
 * Status: IFNAMSIZ derived; struct ifreq itself has no documented layout.
 */
#ifndef Q9_SPF_BSD_NET_IF_H
#define Q9_SPF_BSD_NET_IF_H

#define IFNAMSIZ 16   /* derived: interface name fields are 16 bytes */

#endif /* Q9_SPF_BSD_NET_IF_H */

/* not defined (no documented value):
 *   struct ifreq (the overall layout is not documented, so its derived
 *   members ifr_name [char, IFNAMSIZ] and ifr_addr [struct sockaddr] are
 *   not defined either) and the members ifr_flags, ifr_mtu
 */
