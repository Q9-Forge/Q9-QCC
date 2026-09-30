/* netdb.h -- host, protocol and service database entries, interface and
 * route database calls.
 *
 * Derived from the specification, no template used.
 * Status: struct hostent, protoent, servent, rtreq and the prototypes are
 * belegt; getroutent derived; putroutent see the note at its declaration.
 * Offsets assume 32-bit int and pointers.
 */
#ifndef Q9_SPF_BSD_NETDB_H
#define Q9_SPF_BSD_NETDB_H

#include <types.h>
#include <SPF/BSD/sys/types.h>
#include <SPF/BSD/sys/socket.h>
#include <SPF/BSD/netinet/in.h>   /* struct in_addr is used with h_addr */

struct hostent {
	char  *h_name;        /* @0  */
	char **h_aliases;     /* @4  */
	int    h_addrtype;    /* @8  */
	int    h_length;      /* @12 */
	char **h_addr_list;   /* @16 */
};
#define h_addr h_addr_list[0]

struct protoent {
	char  *p_name;        /* @0 */
	char **p_aliases;     /* @4 */
	int    p_proto;       /* @8 host byte order */
};

struct servent {
	char  *s_name;        /* @0  */
	char **s_aliases;     /* @4  */
	int    s_port;        /* @8  network byte order */
	char  *s_proto;       /* @12 */
};

struct rtreq {
	int             req;        /* @0  */
	int             flags;      /* @4  route type */
	struct sockaddr dst;        /* @8  */
	struct sockaddr gateway;    /* @24 */
	struct sockaddr netmask;    /* @40 */
};

extern struct hostent  *gethostbyname(const char *name);
extern struct protoent *getprotobyname(const char *name);
extern struct servent  *getservbyname(const char *name, const char *proto);
extern unsigned long    inet_addr(char *cp);

extern error_code endintent(void);
extern char *getintent(void);

extern error_code endroutent(void);
extern error_code delroutent(const struct rtreq *route_ptr);
/* derived: the documented prototype is written with a stray asterisk in
   the return type; the documented example assigns the result to errno and
   compares with -1, so it is treated as an error code here */
extern error_code putroutent(struct rtreq *route_ptr);
/* derived: the prototype is not documented, only the use in an example
   (no argument, returns a pointer to a route request, NULL on error) */
extern struct rtreq *getroutent(void);

#endif /* Q9_SPF_BSD_NETDB_H */

/* not defined (no documented value):
 *   struct hostconfent (members key, value), gethostconfent, endhostconfent
 *   struct inetdent (members protocol, service_name, socket_type,
 *   wait_status); getinetdent, whose return type is that struct
 */
