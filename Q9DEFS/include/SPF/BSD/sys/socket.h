/* sys/socket.h -- socket address structure, socket types and socket calls.
 *
 * Derived from the specification, no template used.
 * Status: struct sockaddr and the prototypes belegt; AF_INET and the
 * SOCK_* values derived from the BSD numbering.
 */
#ifndef Q9_SPF_BSD_SYS_SOCKET_H
#define Q9_SPF_BSD_SYS_SOCKET_H

#include <SPF/BSD/sys/types.h>
#include <UNIX/os9time.h>   /* derived: struct timeval is used with socket timeouts */

#define AF_INET      2   /* derived: BSD numbering */

#define SOCK_STREAM  1   /* derived: BSD numbering */
#define SOCK_DGRAM   2   /* derived: BSD numbering */
#define SOCK_RAW     3   /* derived: BSD numbering */

struct sockaddr {
	u_char sa_len;        /* @0  total length of the address */
	u_char sa_family;     /* @1  address family */
	char   sa_data[14];   /* @2  */
};

extern int accept(int s, struct sockaddr *addr, int *addrlen);
extern int bind(int s, struct sockaddr *name, int namelen);
extern int connect(int s, struct sockaddr *name, int namelen);
extern int listen(int s, int backlog);
extern int recv(int s, char *buf, int len, int flags);
extern int recvfrom(int s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
extern int send(int s, void *msg, int len, int flags);
extern int sendto(int s, void *msg, int len, int flags, struct sockaddr *to, int tolen);
extern int setsockopt(int s, int level, int optname, void *optval, int optlen);
extern int socket(int af, int type, int protocol);

#endif /* Q9_SPF_BSD_SYS_SOCKET_H */

/* not defined (no documented value):
 *   SOL_SOCKET, SO_BROADCAST, SO_RCVTIMEO
 */
