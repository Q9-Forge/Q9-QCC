/* SPF/BSD interface ioctl record, with a 16-byte name and 16-byte payload.
 * Field positions and option numbers follow the SDK interface contract;
 * the named union keeps QCC's struct grammar usable on both 32-bit targets. */
#ifndef Q9_SPF_BSD_NET_IF_H
#define Q9_SPF_BSD_NET_IF_H
#include <SPF/BSD/sys/socket.h>
#define IFNAMSIZ 16
union q9_ifreq_payload {
    struct sockaddr address;
    short flags;
    int metric;
    char *data;
    unsigned long mtu;
};
struct ifreq {
    char ifr_name[IFNAMSIZ];
    union q9_ifreq_payload payload;
};
#define ifr_addr payload.address
#define ifr_flags payload.flags
#define ifr_metric payload.metric
#define ifr_data payload.data
#define ifr_mtu payload.mtu
/* Read/write direction, record length, interface group, request number. */
#define SIOCGIFADDR  (0xc0000000U | (sizeof(struct ifreq) << 16) | (105 << 8) | 33)
#define SIOCGIFFLAGS (0xc0000000U | (sizeof(struct ifreq) << 16) | (105 << 8) | 17)
#define SIOCGIFMTU   (0xc0000000U | (sizeof(struct ifreq) << 16) | (105 << 8) | 51)
#endif
