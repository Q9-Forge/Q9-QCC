/* RPC/pmap_prot.h -- port mapper list structures.
 *
 * Derived from the specification, no template used.
 * Status: derived from the published port mapper protocol layout (four
 * 32-bit words per mapping, then the list link).
 */
#ifndef Q9_RPC_PMAP_PROT_H
#define Q9_RPC_PMAP_PROT_H

#include <SPF/BSD/sys/types.h>

/* derived: one port mapping, 16 bytes */
struct pmap {
	u_long pm_prog;    /* @0  program number */
	u_long pm_vers;    /* @4  version number */
	u_long pm_prot;    /* @8  protocol */
	u_long pm_port;    /* @12 port */
};

/* derived: linked list of port mappings */
struct pmaplist {
	struct pmap      pml_map;    /* @0  */
	struct pmaplist *pml_next;   /* @16 */
};

#endif /* Q9_RPC_PMAP_PROT_H */

/* not defined (no documented value): none */
