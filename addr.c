#include <arpa/inet.h>
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include "global.h"

/* Compare two addresses.  memcmp is unreliable because of padding.
 */
int addr_cmp(struct sockaddr_in a1, struct sockaddr_in a2){
    if (a1.sin_family != a2.sin_family) {
        return (int) a1.sin_family - (int) a2.sin_family;
    }
    if (a1.sin_port != a2.sin_port) {
        return (int) a1.sin_port - (int) a2.sin_port;
    }
    if (a1.sin_addr.s_addr != a2.sin_addr.s_addr) {
        return (int) a1.sin_addr.s_addr - (int) a2.sin_addr.s_addr;
    }
    return 0;
}

/* Given a name of a host or an address in the form a.b.c.d, and a
 * port, construct an address.
 */
int addr_get(struct sockaddr_in *sin, const char *addr, int port){
    struct hostent *h;

    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);

    /* See if it's a DNS name.
     */
    if (*addr < '0' || *addr > '9') {
        if ((h = gethostbyname(addr)) == 0) {
            fprintf(stderr, "addr_get: gethostbyname '%s' failed\n", addr);
            return 0;
        }
        sin->sin_addr = * (struct in_addr *) h->h_addr_list[0];
    }
    else {
        sin->sin_addr.s_addr = inet_addr(addr);
    }
    return 1;
}
