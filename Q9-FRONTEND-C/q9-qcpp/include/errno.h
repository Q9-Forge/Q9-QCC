/* errno.h -- Q9-Werkzeugkette, bewusst minimal (s. stddef.h/string.h).
 *
 * Local interface; it does not copy Microware's copyrighted DEFS/errno.h.
 * Q9-Tools/System/link.c currently includes this guard without using errno or
 * any error code, so the header is intentionally empty. Add individual codes
 * when a tool needs them instead of importing the complete Microware list.
 */
#ifndef Q9_ERRNO_H
#define Q9_ERRNO_H

#endif
