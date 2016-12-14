#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"

struct gossip {
    struct gossip *next;
    struct sockaddr_in src;
    long counter;
    char *latest;
};
static struct gossip *gossip;

extern struct sockaddr_in my_addr;
extern int* graph;
extern struct node_list *nl;

struct gossip* gossip_next(struct gossip* gossip) {
    return gossip->next;
}

struct sockaddr_in gossip_src(struct gossip* gossip) {
    return gossip->src;
}

char* gossip_latest(struct gossip* gossip) {
    return gossip->latest;
}

/* A gossip message has the following format:
 *
 *  G<src_addr:src_port>/counter/payload\n
 *
 * Here <src_addr:src_port>/counter uniquely identify a message from
 * the given source.
 */
void gossip_received(struct file_info *fi, char *line){

    char *port = index(line, ':');
    if (port == 0) {
        fprintf(stderr, "do_gossip: format is G<addr>:<port>/counter/payload\n");
        return;
    }
    *port++ = 0;

    char *ctr = index(port, '/');
    if (ctr == 0) {
        fprintf(stderr, "do_gossip: no counter\n");
        return;
    }
    *ctr++ = 0;

    char *payload = index(ctr, '/');
    if (payload == 0) {
        fprintf(stderr, "do_gossip: no payload\n");
        return;
    }
    *payload++ = 0;

    /* Get the source and message identifier.
     */
    struct sockaddr_in addr;
    if (addr_get(&addr, line, atoi(port)) < 0) {
        return;
    }
    long counter = atol(ctr);

    /* See if we already have this gossip.
     */
    struct gossip *g;
    for (g = gossip; g != 0; g = g->next) {
        if (addr_cmp(g->src, addr) != 0) {
            continue;
        }
        if (g->counter >= counter) {
            // printf("already know about this gossip\n");
            return;
        }
        free(g->latest);
        break;
    }

    int g_existed = g != 0;

    if (g == 0) {
        g = calloc(1, sizeof(*g));
        g->src = addr;
        g->next = gossip;
        gossip = g;
    }

    /* Restore the line.
     */
    *--port = ':';
    *--ctr = '/';
    *--payload = '/';

    /* Save the gossip.
     */
    int len = strlen(line);
    g->latest = malloc(len + 1);
    memcpy(g->latest, line, len + 1);
    g->counter = counter;

    /* Send the gossip to all connections except the one it came in on.
     */
    char *msg = malloc(len + 3);
    sprintf(msg, "G%s\n", g->latest);
    file_broadcast(msg, len + 2, fi);
    free(msg);

    if (!g_existed) {
        gossip_to_peer(fi);
    }
    printf("[gossip_received] %s\n", line);
    update_nl_and_graph(addr_to_string(g->src), ++payload);
}

/* Send all gossip I have to the given peer.
 */
void gossip_to_peer(struct file_info *fi){
    struct gossip *g;

    for (g = gossip; g != 0; g = g->next) {
        file_info_send(fi, "G", 1);
        file_info_send(fi, g->latest, strlen(g->latest));
        file_info_send(fi, "\n", 1);
    }
}
