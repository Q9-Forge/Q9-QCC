/* RPC/rpc.h -- remote procedure call client and XDR declarations.
 *
 * Derived from the specification, no template used.
 * Status: XDR, CLIENT, TRUE, FALSE and the prototypes are belegt;
 * bool_t, RPC_SUCCESS, NULLPROC, xdrproc_t derived.
 * Structure offsets assume 32-bit int, enum and pointers.
 */
#ifndef Q9_RPC_RPC_H
#define Q9_RPC_RPC_H

#include <SPF/BSD/sys/types.h>
#include <SPF/BSD/netinet/in.h>
#include <UNIX/os9time.h>
#include <RPC/pmap_prot.h>
#include <UNIX/os9def.h>   /* derived: consumers reach bzero through this header */

/* derived: truth value returned by the XDR filters (1 = success, 0 = failure) */
typedef int bool_t;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define RPC_SUCCESS  0   /* derived: first result value of a call */
#define NULLPROC     0   /* derived: procedure number 0 */

/* ---- XDR stream ---------------------------------------------------------- */

struct xdr_ops;   /* eight function pointers (x_getlong, x_putlong, x_getbytes,
                     x_putbytes, x_getpostn, x_setpostn, x_inline, x_destroy);
                     their signatures are not documented, so the tag stays
                     incomplete */

typedef struct {
	int             x_op;        /* @0  operation (32-bit enumeration; its
	                                enumerators are not documented, so it is
	                                declared as int) */
	struct xdr_ops *x_ops;       /* @4  */
	caddr_t         x_public;    /* @8  */
	caddr_t         x_private;   /* @12 */
	caddr_t         x_base;      /* @16 */
	int             x_handy;     /* @20 */
} XDR;

/* derived: pointer to an XDR filter routine */
typedef bool_t (*xdrproc_t)();

extern bool_t xdr_int(XDR *xdrs, int *ip);
extern bool_t xdr_string(XDR *xdrs, char **cpp, u_int maxsize);
extern bool_t xdr_wrapstring(XDR *xdrs, char **cpp);
extern bool_t xdr_array(XDR *xdrs, caddr_t *addrp, u_int *sizep, u_int maxsize,
                        u_int elsize, xdrproc_t elproc);
extern bool_t xdr_pointer(XDR *xdrs, char *objpp, u_int obj_size, xdrproc_t xdr_obj);
extern bool_t xdr_void(void);

/* ---- client handle ------------------------------------------------------- */

typedef struct AUTH AUTH;      /* authentication handle, internals not documented */
struct clnt_ops;               /* six function pointers (cl_call, cl_abort,
                                  cl_geterr, cl_freeres, cl_destroy,
                                  cl_control); signatures not documented */

typedef struct {
	AUTH            *cl_auth;      /* @0 */
	struct clnt_ops *cl_ops;       /* @4 */
	caddr_t          cl_private;   /* @8 */
} CLIENT;

extern CLIENT *clnt_create(char *hostname, unsigned prog, unsigned vers, char *proto);
extern void clnt_pcreateerror(char *msg);
extern void clnt_perror(CLIENT *clnt, char *msg);

/* ---- port mapper --------------------------------------------------------- */

extern struct pmaplist *pmap_getmaps(struct sockaddr_in *addr);
extern u_short pmap_getport(struct sockaddr_in *addr, u_long prog, u_long vers, u_int protocol);

#endif /* Q9_RPC_RPC_H */

/* not defined (no documented value):
 *   enum clnt_stat (only the value RPC_SUCCESS is documented, and it is
 *   defined above as a plain constant)
 *   clnt_call (its result type is enum clnt_stat)
 */
