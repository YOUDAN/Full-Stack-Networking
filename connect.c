#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ifaddrs.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include "global.h"

/* Information about files and sockets is kept here.  An outgoing
 * connection is one requested by the local user using the 'C'
 * command.  An incoming connection is one created by a remote user.
 * We don't try to re-establish incoming connections after they break.
 */
struct file_info {
    struct file_info *next;                     // linked list management
    char *uid;                                  // unique connection id
    int fd;                                     // file descriptor
    enum file_info_type {
        FI_FREE,                            // unused entry
        FI_FILE,                            // file descriptor, probably stdin
        FI_SERVER,                          // server socket
        FI_INCOMING,                        // incoming connection
        FI_OUTGOING,                        // outgoing connection
    } type;                                     // type of file
    enum { FI_UNKNOWN, FI_KNOWN } status;       // is the peer address known?
    struct sockaddr_in addr;                    // address of peer if known
    void (*handler)(struct file_info *, int events);        // event handler`
    int events;                                 // POLLIN, POLLOUT
    char *input_buffer;                         // stuff received
    int amount_received;                        // size of input buffer
    char *output_buffer;                        // stuff to send
    int amount_to_send;                         // size of output buffer
    union {
        struct fi_outgoing {
            enum { FI_CONNECTING, FI_CONNECTED } status;
            double connect_time;            // time of last attempt to connect
        } fi_outgoing;
    } u;
};
struct file_info *file_info;
int nfiles;
char *uid_gen = (char *) 1;
char *my_payload;

struct sockaddr_in my_addr;

long counter = -1;

void create_or_update_self_gossip();
void send_received(struct file_info *fi, char *line);

/* Add file info about 'fd'.
 */
static struct file_info *file_info_add(enum file_info_type type, int fd,
                        void (*handler)(struct file_info *, int events), int events){
    struct file_info *fi = calloc(1, sizeof(*fi));
    fi->uid = uid_gen++;
    fi->type = type;
    fi->fd = fd;
    fi->handler = handler;
    fi->events = events;
    fi->next = file_info;
    file_info = fi;
    nfiles++;
    return fi;
}

/* Destroy a file_info structure.
 */
static void file_info_delete(struct file_info *fi){
    struct file_info **pfi, *fi2;

    for (pfi = &file_info; (fi2 = *pfi) != 0; pfi = &fi2->next) {
        if (fi2 == fi) {
            *pfi = fi->next;
            break;
        }
    }
    free(fi->input_buffer);
    free(fi->output_buffer);
    free(fi);
}

/* Put the given message in the output buffer.
 */
void file_info_send(struct file_info *fi, char *buf, int size){
    fi->output_buffer = realloc(fi->output_buffer, fi->amount_to_send + size);
    memcpy(&fi->output_buffer[fi->amount_to_send], buf, size);
    fi->amount_to_send += size;
}

/* Same as file_info_send, but it's done on sockaddr_in instead of file_info
 */
struct file_info* sockaddr_to_file(struct sockaddr_in dst) {
    struct file_info* fi;
    for (fi = file_info; fi != 0; fi = fi->next) {
        if (addr_cmp(dst, fi->addr) == 0) {
            return fi;
        }
    }
    printf("sockaddr not connected to host");
    return NULL;
}

/* Broadcast to all connections except fi.
 */
void file_broadcast(char *buf, int size, struct file_info *fi){
    struct file_info *fi2;
    for (fi2 = file_info; fi2 != 0; fi2 = fi2->next) {
        if (fi2->type == FI_FREE || fi2 == fi) {
            continue;
        }
        if (fi2->type == FI_OUTGOING && fi2->u.fi_outgoing.status == FI_CONNECTING) {
            continue;
        }
        if (fi2->type == FI_OUTGOING || fi2->type == FI_INCOMING) {
            file_info_send(fi2, buf, size);
        }
    }
}

/* This is a timer handler to reconnect to a peer after a period of
   time elapsed.
 */
static void timer_reconnect(void *arg){
    void try_connect(struct file_info *fi);

    struct file_info *fi;
    for (fi = file_info; fi != 0; fi = fi->next) {
        if (fi->type != FI_FREE && fi->uid == arg) {
            printf("reconnecting\n");
            try_connect(fi);
            return;
        }
    }
    printf("reconnect: entry not found\n");
}

/* The user typed in a C<addr>:<port> command to connect to a peer.
 */
static void connect_command(struct file_info *fi, char *addr_port){
    void try_connect(struct file_info *fi);

    if (fi->type != FI_FILE) {
        fprintf(stderr, "unexpected connect message\n");
        return;
    }

    char *p = index(addr_port, ':');
    if (p == 0) {
        fprintf(stderr, "do_connect: format is C<addr>:<port>\n");
        return;
    }
    *p++ = 0;

    struct sockaddr_in addr;
    if (addr_get(&addr, addr_port, atoi(p)) < 1) {
        return;
    }
    *--p = ':';

    fi = file_info_add(FI_OUTGOING, -1, 0, 0);
    fi->u.fi_outgoing.status = FI_CONNECTING;

    /* Even though we're connecting, you're allowed to connect using
     * any address to a peer (including a localhost address).  So we
     * have to wait for the Hello message until we're certain who it
     * is on the other side.  Until then, we keep the address we know
     * for debugging purposes.
     */
    fi->addr = addr;

    try_connect(fi);
}

/* We uniquely identify a socket connection by the lower of the local
 * TCP/IP address and the remote one.  Because two connections cannot
 * share TCP/IP addresses, this should work.
 */
static void get_id(int skt, struct sockaddr_in *addr){
    struct sockaddr_in this, that;
    socklen_t len;

    len = sizeof(that);
    if (getsockname(skt, (struct sockaddr *) &this, &len) < 0) {
        perror("getsockname");
        exit(1);
    }
    len = sizeof(that);
    if (getpeername(skt, (struct sockaddr *) &that, &len) < 0) {;
        perror("getpeername");
        exit(1);
    }
    *addr = addr_cmp(this, that) < 0 ? this : that;
}

/* Received a H(ello) message of the form H<addr>:<port>.  This is the
 * first message that's sent over a TCP connection (in both
 * directions) so the peers can identify one another.  The port is the
 * server port of the endpoint rather than the connection's port.
 *
 * Sometimes accidentally peers try to connect to one another at the same time.
 * This code kills one of the connections.
 */
void hello_received(struct file_info *fi, char *addr_port){
    char *p = index(addr_port, ':');
    if (p == 0) {
        fprintf(stderr, "do_hello: format is H<addr>:<port>\n");
        return;
    }
    *p++ = 0;

    struct sockaddr_in addr;
    if (addr_get(&addr, addr_port, atoi(p)) < 1) {
        return;
    }
    *--p = ':';

    printf("Got hello from %s:%d on socket %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fi->fd);

    /* If a connection breaks and is re-established, a duplicate hello
     * message is likely to arrive, but we can ignore it as we already
     * know the peer.
     */
    if (fi->status == FI_KNOWN) {
        fprintf(stderr, "Duplicate hello (ignoring)\n");
        if (addr_cmp(addr, fi->addr) != 0) {
            fprintf(stderr, "do_hello: address has changed???\n");
        }
        return;
    }

    /* It is possible to set up a connection to self.  We deal with it
     * by ignoring the Hello message but keeping the connection
     * established.
     */
    if (addr_cmp(addr, my_addr) == 0) {
        fprintf(stderr, "Got hello from self??? (ignoring)\n");
        return;
    }

    /* Search the connections to see if there is already a connection
       to this peer.
     */
    struct file_info *fi2;
    for (fi2 = file_info; fi2 != 0; fi2 = fi2->next) {
        if (fi2->type == FI_FREE || fi2->status != FI_KNOWN) {
            continue;
        }
        if (addr_cmp(fi2->addr, addr) != 0) {
            continue;
        }

        /* Found another connection.  We're going to keep just one.
         * First see if this is actually an existing connection.  It
         * may have broken.
         */
        if (fi2->fd == -1) {
            printf("Found a defunct connection---dropping that one\n");
            if (fi2->type == FI_OUTGOING) {
                fi->type = FI_OUTGOING;
                fi->u.fi_outgoing = fi2->u.fi_outgoing;
            }
            fi2->type = FI_FREE;
            return;
        }

        /* Of the two, we keep the "lowest one".  We identify a
         * connection by the lowest endpoint address.
         */
        struct sockaddr_in mine, other;
        get_id(fi->fd, &mine);
        get_id(fi2->fd, &other);
        if (addr_cmp(mine, other) < 0) {
            /* We keep mine.
             */
            printf("duplicate connection -- keep mine\n");
            if (fi2->type == FI_OUTGOING) {
                fi->type = FI_OUTGOING;
                fi->u.fi_outgoing = fi2->u.fi_outgoing;
            }
            close(fi2->fd);
            fi2->type = FI_FREE;
        }
        else {
            printf("duplicate connection -- keep other\n");

            /* The other one wins.
             */
            if (fi->type == FI_OUTGOING) {
                fi2->type = FI_OUTGOING;
                fi2->u.fi_outgoing = fi->u.fi_outgoing;
            }
            close(fi->fd);
            fi->type = FI_FREE;
        }
    }

    /* No other connection.  Keep this one.
     */
    fi->addr = addr;
    fi->status = FI_KNOWN;

    create_or_update_self_gossip();
}

/* A line of input (a command) is received.  Look at the first
 * character to determine the type and take it from there.
 */
static void handle_line(struct file_info *fi, char *line){
    switch (line[0]) {
    case 0:
        break;
    case 'C':
        connect_command(fi, &line[1]);
        break;
    case 'G':
        gossip_received(fi, &line[1]);
        break;
    case 'H':
        hello_received(fi, &line[1]);
        break;
    case 'S':
        send_received(fi, &line[1]);
        break;
    case 'E':
    case 'e':
        exit(0);
        break;
    default:
        fprintf(stderr, "unknown command: '%s'\n", line);
    }
}

void handle_closed_socket(struct file_info *fi){
    printf("Lost connection on socket %d\n", fi->fd);

    close(fi->fd);

    /* Start a timer to reconnect if it's an outgoing connection.
     */
    if (fi->type == FI_OUTGOING) {
        double time = timer_now() + 1;
        timer_start(time, timer_reconnect, fi->uid);
        fi->fd = -1;
        fi->u.fi_outgoing.status = FI_CONNECTING;
    }
    else {
        fi->type = FI_FREE;
    }
    create_or_update_self_gossip();
    fflush(stdout);
}

/* Input is available on the given connection.  Add it to the input buffer and
 * see if there are any complete lines in it.
 */
static void add_input(struct file_info *fi){
    fi->input_buffer = realloc(fi->input_buffer, fi->amount_received + 100);
    int n = read(fi->fd, &fi->input_buffer[fi->amount_received], 100);
    if (n == 0) {
        handle_closed_socket(fi);
        return;
    }
    if (n < 0) {
        perror("read");
        exit(1);
    }
    if (n > 0) {
        fi->amount_received += n;
        for (;;) {
            char *p = index(fi->input_buffer, '\n');
            if (p == 0) {
                break;
            }
            *p++ = 0;
            handle_line(fi, fi->input_buffer);
            int n = fi->amount_received - (p - fi->input_buffer);
            memmove(fi->input_buffer, p, n);
            fi->amount_received = n;
            if (fi->fd == 0) {
                printf("> ");
                fflush(stdout);
            }
        }
    }
}

/* Activity on a socket: input, output, or error...
 */
static void message_handler(struct file_info *fi, int events){
    char buf[512];

    if (events & (POLLERR | POLLHUP)) {
        double time;
        int error;
        socklen_t len = sizeof(error);
        if (getsockopt(fi->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            perror("getsockopt");
        }
        switch (error) {
        case 0:
            printf("Lost connection on socket %d\n", fi->fd);
            time = timer_now() + 1;
            break;
        default:
            printf("Error '%s' on socket %d\n", strerror(error), fi->fd);
            time = timer_now() + 5;
        }

        close(fi->fd);

        /* Start a timer to reconnect.
         */
        if (fi->type == FI_OUTGOING) {
            timer_start(time, timer_reconnect, fi->uid);
            fi->fd = -1;
            fi->u.fi_outgoing.status = FI_CONNECTING;
        }
        else {
            fi->type = FI_FREE;
        }
        create_or_update_self_gossip();
        return;
    }
    if (events & POLLOUT) {
        int n = send(fi->fd, fi->output_buffer, fi->amount_to_send, 0);
        if (n < 0) {
            perror("send");
        }
        if (n > 0) {
            if (n == fi->amount_to_send) {
                fi->amount_to_send = 0;
            }
            else {
                memmove(&fi->output_buffer[n], fi->output_buffer, fi->amount_to_send - n);
                fi->amount_to_send -= n;
            }
        }
    }
    if (events & POLLIN) {
        add_input(fi);
    }
    if (events & ~(POLLIN|POLLOUT|POLLERR|POLLHUP)) {
        printf("message_handler: unknown events\n");
        fi->events = 0;
    }
}

/* Send a H(ello) message to the peer to identify ourselves.
 */
static void send_hello(struct file_info *fi){
    char buffer[64];
    sprintf(buffer, "H%s:%d\n", inet_ntoa(my_addr.sin_addr), ntohs(my_addr.sin_port));
    file_info_send(fi, buffer, strlen(buffer));
}

/* Send a 'send' message to the destination
 */
void send_received(struct file_info *fi, char *line){
    printf("\n[send_received] %s\n", line);


    char *des;
    size_t i = 0, line_len = strlen(line);
    for (i = 0; i < line_len; i++) {
        if (line[i] == '/') {
            des = malloc(i+1);
            memcpy(des, line, i);
            des[i] = '\0';
            break;
        }
    }

    if (des == 0) {
        fprintf(stderr, "send message: format is S<dst_addr:port>/<TTL>/<payload>\n");
        return;
    }
    size_t des_len = i;

    char* ttl_str;
    size_t ttl_len;
    for (i = des_len + 1; i < line_len+1; i++) {
        if (i == line_len || line[i] == '/') {
            ttl_len = i-des_len;
            ttl_str = malloc(ttl_len);
            memcpy(ttl_str, line+des_len+1, ttl_len-1);
            ttl_str[ttl_len-1] = '\0';
            break;
        }
    }
    if (ttl_str == 0) {
        fprintf(stderr, "send message: no TTL\n");
        return;
    }

    printf("ttl: %s\n", ttl_str);

    int ttl = atoi(ttl_str);

    char* payload;
    size_t payload_len = line_len - ttl_len - des_len + 1;
    printf("payload_len: %d\n", (int)payload_len);
    payload = malloc(payload_len);
    if (payload_len > 1) {
        memcpy(payload, line+ttl_len+des_len+1, payload_len-1);
    }
    payload[payload_len-1] = '\0';
    printf("payload: %s\n", payload);

    // arrive the destination, print the payload
    if (strcmp(des, addr_to_string(my_addr)) == 0) {
        printf("Message arrives the destination\n");
        if (my_payload == 0) {
            printf("No payload available\n");
        } else {
            printf("Payload:%s\n", payload);
        }
        return;
    }

    if (ttl-- <= 0) {
        printf("TTL <= 0, the message dropped\n");
        return;
    }

    char* my_addr_str = addr_to_string(my_addr);
    char* next = next_hop(my_addr_str, des);

    if (next == 0) {
        printf("Cannot find the next hop\n");
        return;
    }
    if (strcmp(next, my_addr_str) == 0) {
        perror("find next hop: next hop is myself\n");
        return;
    }
    printf("next: %s\n", next);

    char buffer[128];
    sprintf(buffer, "S%s/%d/%s\n", des, ttl, payload);
    printf("\bbuffer: %s\n", buffer);


    struct file_info *fi2 = file_info;
    char *fi_addr;
    for (;fi2 != 0; fi2 = fi2->next) {
        fi_addr = addr_to_string(fi2->addr);
        if (strcmp(fi_addr, next) == 0)
            break;
    }
    file_info_send(fi2, buffer, strlen(buffer));
}

/* This function is invoked in response to a non-blocking socket
 * connect() call once an outgoing connection is established or fails.
 */
static void connect_handler(struct file_info *fi, int events){
    char buf[512];

    if (events & (POLLERR | POLLHUP)) {
        double time;
        int error;
        socklen_t len = sizeof(error);
        if (getsockopt(fi->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            perror("getsockopt");
        }
        switch (error) {
        case 0:
            printf("No connection on socket %d\n", fi->fd);
            time = timer_now() + 3;
            break;
        case ETIMEDOUT:
            printf("Timeout on socket %d\n", fi->fd);
            time = fi->u.fi_outgoing.connect_time + 5;
            break;
        default:
            printf("Error '%s' on socket %d\n", strerror(error), fi->fd);
            time = timer_now() + 5;
        }

        /* Start a timer to reconnect.
         */
        timer_start(time, timer_reconnect, fi->uid);

        close(fi->fd);
        fi->fd = -1;
        fi->u.fi_outgoing.status = FI_CONNECTING;

        return;
    }
    if (events & POLLOUT) {
        printf("Socket %d connected\n", fi->fd);
        fi->handler = message_handler;
        fi->events = POLLIN;
        fi->u.fi_outgoing.status = FI_CONNECTED;

        send_hello(fi);
        gossip_to_peer(fi);
    }
    if (events & ~(POLLOUT|POLLERR|POLLHUP)) {
        printf("message_handler: unknown events %x\n", events);
        fi->events = 0;
    }
}

/* Invoke a non-blocking connect() to try and establish a connection.
 */
void try_connect(struct file_info *fi) {
    int skt = socket(AF_INET, SOCK_STREAM, 0);
    if (skt < 0) {
        perror("try_connect: socket");
        return;
    }

    /* Make the socket, skt, non-blocking.
     */
    // YOUR CODE HERE
    if (fcntl(skt, F_SETFL, fcntl(skt, F_GETFL, 0) | O_NONBLOCK)) {
      perror("try_connect: setting non-blocking");
    }

    // YOUR CODE DONE

    if (connect(skt, (struct sockaddr *) &fi->addr, sizeof(fi->addr)) < 0) {
        printf("Connecting to %s:%d on socket %d\n", inet_ntoa(fi->addr.sin_addr), ntohs(fi->addr.sin_port), skt);
        if (errno != EINPROGRESS) {
            perror("try_connect: connect");
            close(skt);
            return;
        }

        fi->fd = skt;
        fi->events = POLLOUT;
        fi->handler = connect_handler;
        fi->u.fi_outgoing.connect_time = timer_now();
    }
    else {
        printf("Connected to %s:%d on socket %d\n", inet_ntoa(fi->addr.sin_addr), ntohs(fi->addr.sin_port), skt);
        fi->fd = skt;
        fi->events = POLLIN;
        fi->handler = message_handler;
        fi->u.fi_outgoing.connect_time = timer_now();
        fi->u.fi_outgoing.status = FI_CONNECTED;
    }
}

/* Standard input is available.
 */
static void stdin_handler(struct file_info *fi, int events){
    if (events & POLLIN) {
        add_input(fi);
    }
    if (events & ~POLLIN) {
        fprintf(stderr, "input_handler: unknown events %x\n", events);
    }
}

/* Activity on the server socket.  Typically an incoming connection.
 * Accept the connection and create a new file_info descriptor for it.
 */
static void server_handler(struct file_info *fi, int events){
    if (events & POLLIN) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int skt = accept(fi->fd, (struct sockaddr *) &addr, &len);
        if (skt < 0) {
            perror("accept");
        }

        /* Make the socket non-blocking.
         */
        // YOUR CODE HERE
        if (fcntl(skt, F_SETFL, fcntl(skt, F_GETFL, 0) | O_NONBLOCK)) {
          perror("server_handler: setting non-blocking");
        }
        // YOUR CODE DONE

        /* Keep track of the socket.
         */
        struct file_info *fi = file_info_add(FI_INCOMING, skt, message_handler, POLLIN);

        /* We don't know yet who's on the other side exactly until we
         * get the H(ello) message.  But for debugging it's convenient
         * to keep the peer's socket address.
         */
        fi->addr = addr;

        printf("New connection from %s:%d on socket %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), skt);

        send_hello(fi);
        gossip_to_peer(fi);
    }
    if (events & ~POLLIN) {
        fprintf(stderr, "server_handler: unknown events %x\n", events);
    }
}

/* When getting new connection or connection lost, send gossip
 */
void create_or_update_self_gossip() {
    ++counter;

    // source/counter
    size_t capacity = 256;
    char *line = malloc(capacity);
    char *addr_str = addr_to_string(my_addr);
    sprintf(line, "%s/%lu/", addr_str, counter);
    size_t line_len = strlen(line);

    // constuct payload
    struct file_info *fi2;
    fflush(stdout);
    for (fi2 = file_info; fi2 != 0; fi2 = fi2->next) {
        if (fi2->type == FI_FREE) {
            continue;
        }
        if (fi2->type == FI_OUTGOING && fi2->u.fi_outgoing.status == FI_CONNECTING) {
            continue;
        }
        if (fi2->type == FI_OUTGOING || fi2->type == FI_INCOMING) {
            char *peer_addr_str = addr_to_string(fi2->addr);
            printf("peer: %s\n", peer_addr_str);
            size_t peer_len = strlen(peer_addr_str);
            if (peer_len + line_len + 2 >= capacity) {
                capacity *= 2;
                line = realloc(line, capacity);
            }
            line[line_len++] = ';';
            memcpy(line + line_len, peer_addr_str, peer_len);
            line_len += peer_len;
            free(peer_addr_str);
        }
    }
    line[line_len] = '\0';

    // copy payload to my_payload
    if (my_payload != 0) {
        free(my_payload);
    }
    size_t i;
    for (i = 0; i < strlen(line); i++) {
        if (line[i] == ';') {
            size_t payload_len = strlen(line+i) + 1;
            my_payload = malloc(payload_len);
            memcpy(my_payload, line+i, payload_len-1);
            my_payload[payload_len-1] = '\0';
            break;
        }
    }

    gossip_received(file_info, line);
}

/* Usage: a.out [port].  If no port is specified, a default port is used.
 */
int main(int argc, char *argv[]){
    int bind_port = argc > 1 ? atoi(argv[1]) : 0;
    int i;

    /* Read from standard input.
     */
    struct file_info *input = file_info_add(FI_FILE, 0, stdin_handler, POLLIN);

    /* Create a TCP socket.
     */
    int skt;
    // YOUR CODE HERE
    skt = socket(AF_INET, SOCK_STREAM, 0);
    if (skt < 0) {
      perror("create a socket");
    }
    if (setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
      perror("main: set TCP");
    }

    /* Make the socket non-blocking.
     */
    // YOUR CODE HERE
    if (fcntl(skt, F_SETFL, fcntl(skt, F_GETFL, 0) | O_NONBLOCK)) {
      perror("main: set non-blocking");
    }

    /* Initialize bind_addr in as far as possible.
     */
    // YOUR CODE HERE
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(bind_port);

    /* Bind the socket.
     */
    // YOUR CODE HERE
    if (bind(skt, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) < 0) {
      perror("main: bind");
    }

    /* Keep track of the socket.
     */
    file_info_add(FI_SERVER, skt, server_handler, POLLIN);

    /* Get my address.
     */
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(skt, (struct sockaddr *) &addr, &len) < 0) {
        perror("getsocknane");
    }

    /* Get my IP addresses.
     */
    struct ifaddrs *addr_list, *ifa;
    if (getifaddrs(&addr_list) < 0) {
        perror("getifaddrs");
        return 1;
    }
    for (ifa = addr_list; ifa != 0; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr != 0 && ifa->ifa_addr->sa_family == AF_INET &&
                !(ifa->ifa_flags & IFF_LOOPBACK)) {
            struct sockaddr_in *si = (struct sockaddr_in *) ifa->ifa_addr;
            printf("%s: %s:%d\n", ifa->ifa_name, inet_ntoa(si->sin_addr), ntohs(addr.sin_port));
            my_addr = *si;
            my_addr.sin_port = addr.sin_port;
        }
    }
    freeifaddrs(addr_list);

    /* Pretend standard input is a peer...
     */
    input->addr = my_addr;
    input->status = FI_KNOWN;

    /* Listen on the socket.
     */
    if (listen(skt, 5) < 0) {
        perror("listen");
        return 1;
    }
    create_or_update_self_gossip();

    printf("> "); fflush(stdout);
    for (;;) {
        /* Handle expired timers first.
         */
        int timeout = timer_check();

        /* Prepare poll.
         */
        struct pollfd *fds = calloc(nfiles, sizeof(*fds));
        struct file_info *fi, **fi_index = calloc(nfiles, sizeof(*fi_index));
        int i;
        for (i = 0, fi = file_info; fi != 0; fi = fi->next) {
            if (fi->type != FI_FREE && fi->fd >= 0) {
                fds[i].fd = fi->fd;
                fds[i].events = fi->events;
                if (fi->amount_to_send > 0) {
                    fds[i].events |= POLLOUT;
                }
                fi_index[i++] = fi;
            }
        }

        int n = i; // n may be less than nfiles
        if (poll(fds, n, timeout) < 0) {
            perror("poll");
            return 1;
        }

        /* See if there's activity on any of the files/sockets.
         */
        for (i = 0; i < n; i++) {
            if (fds[i].revents != 0 && fi_index[i]->type != FI_FREE) {
                (*fi_index[i]->handler)(fi_index[i], fds[i].revents);
            }
        }
        free(fds);
        free(fi_index);
    }

    return 0;
}
