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

enum clnt_stat {
    RPC_SUCCESS=0, RPC_CANTENCODEARGS=1, RPC_CANTDECODERES=2,
    RPC_CANTSEND=3, RPC_CANTRECV=4, RPC_TIMEDOUT=5,
    RPC_VERSMISMATCH=6, RPC_AUTHERROR=7, RPC_PROGUNAVAIL=8,
    RPC_PROGVERSMISMATCH=9, RPC_PROCUNAVAIL=10, RPC_CANTDECODEARGS=11,
    RPC_SYSTEMERROR=12, RPC_UNKNOWNHOST=13, RPC_PMAPFAILURE=14,
    RPC_PROGNOTREGISTERED=15, RPC_FAILED=16, RPC_UNKNOWNPROTO=17
};
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

#define XDR_ENCODE 0
#define XDR_DECODE 1
#define XDR_FREE 2
extern void xdrmem_create(XDR *,char *,u_int,int);
extern u_int xdr_getpos(XDR *);
extern bool_t xdr_setpos(XDR *,u_int);
extern bool_t xdr_u_int(XDR *,u_int *);
extern bool_t xdr_opaque(XDR *,char *,u_int);
extern void xdr_free(xdrproc_t,void *);

extern bool_t xdr_int(XDR *xdrs, int *ip);
extern bool_t xdr_string(XDR *xdrs, char **cpp, u_int maxsize);
extern bool_t xdr_wrapstring(XDR *xdrs, char **cpp);
extern bool_t xdr_array(XDR *xdrs, caddr_t *addrp, u_int *sizep, u_int maxsize,
                        u_int elsize, xdrproc_t elproc);
extern bool_t xdr_pointer(XDR *xdrs, char **objpp, u_int obj_size, xdrproc_t xdr_obj);
extern bool_t xdr_void(void);

/* ---- client handle ------------------------------------------------------- */

struct AUTH;
typedef struct AUTH AUTH;      /* authentication handle, internals not documented */
struct clnt_ops;               /* six function pointers (cl_call, cl_abort,
                                  cl_geterr, cl_freeres, cl_destroy,
                                  cl_control); signatures not documented */

typedef struct {
	AUTH            *cl_auth;      /* @0 */
	struct clnt_ops *cl_ops;       /* @4 */
	caddr_t          cl_private;   /* @8 */
} CLIENT;

/* Q9 runtime wrapper for the SDK client-operation dispatch macro. The
 * implementation must dispatch through cl_ops; no network success is stubbed. */
extern enum clnt_stat clnt_call(CLIENT *, u_long, xdrproc_t, void *, xdrproc_t, void *, struct timeval);
extern CLIENT *clnt_create(char *hostname, unsigned prog, unsigned vers, char *proto);
extern void clnt_pcreateerror(char *msg);
extern void clnt_perror(CLIENT *clnt, char *msg);

/* Server transport is opaque; the request's public prefix follows the
 * 32-bit RPC service ABI. Q9 runtime functions implement the SDK dispatch
 * macros behind these declarations. */
struct q9_svc_transport;
typedef struct q9_svc_transport SVCXPRT;
struct svc_req {
    u_long rq_prog;
    u_long rq_vers;
    u_long rq_proc;
    unsigned char rq_cred[12];
    char *rq_clntcred;
    SVCXPRT *rq_xprt;
};
typedef void (*svc_dispatch_fn)(struct svc_req *, SVCXPRT *);
#define RPC_ANYSOCK (-1)
extern SVCXPRT *svctcp_create(int, u_int, u_int);
extern bool_t svc_register(SVCXPRT *, u_long, u_long, svc_dispatch_fn, int);
extern void svc_run(void);
extern bool_t svc_getargs(SVCXPRT *, xdrproc_t, void *);
extern bool_t svc_freeargs(SVCXPRT *, xdrproc_t, void *);
extern bool_t svc_sendreply(SVCXPRT *, xdrproc_t, void *);
extern void svcerr_noproc(SVCXPRT *);
extern void svcerr_decode(SVCXPRT *);
extern void svcerr_systemerr(SVCXPRT *);
extern bool_t pmap_unset(u_long, u_long);

/* ---- port mapper --------------------------------------------------------- */

extern struct pmaplist *pmap_getmaps(struct sockaddr_in *addr);
extern u_short pmap_getport(struct sockaddr_in *addr, u_long prog, u_long vers, u_int protocol);

#endif /* Q9_RPC_RPC_H */
