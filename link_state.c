#include "global.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>

#define INFINITY                INT_MAX
#define UNDEFINED               (-1)
#define INDEX(x, y, nnodes)         ((x) + (nnodes) * (y))

struct node_list {
    char **nodes;
    int nnodes;
    int unsorted;
};

int nl_index(struct node_list *nl, char *node);

int* graph;

struct node_list *nl_create(void) {
    return (struct node_list *) calloc(1, sizeof(struct node_list));
}

struct node_list *nl;

int nl_nsites(struct node_list *nl){
    return nl->nnodes;
}

void nl_add(struct node_list *nl, char *node){
    /* No duplicate nodes.
     */
    if (nl_index(nl, node) != -1) {
        return;
    }
    /* Create a copy of the site.
     */
    int len = strlen(node);
    char *copy = malloc(len + 1);
    strcpy(copy, node);

    /* Add this copy to the list.
     */
    nl->nodes= (char **) realloc(nl->nodes, sizeof(char *) * (nl->nnodes + 1));
    nl->nodes[nl->nnodes++] = copy;
    nl->unsorted = 1;
}

int nl_compare(const void *e1, const void *e2){
    const char **p1 = (const char **) e1, **p2 = (const char **) e2;
    return strcmp(*p1, *p2);
}

void nl_sort(struct node_list *nl){
    qsort(nl->nodes, nl->nnodes, sizeof(char *), nl_compare);
    nl->unsorted = 0;
}

/* Return the rank of the given site in the given site list.
 */
int nl_index(struct node_list *nl, char *node){
    /* Sort the list if not yet sorted.
     */
    if (nl->unsorted) {
        nl_sort(nl);
    }

    /* Binary search.
     */
    int lb = 0, ub = nl->nnodes;
    while (lb < ub) {
        int i = (lb + ub) / 2;
        int cmp = strcmp(node, nl->nodes[i]);
        if (cmp < 0) {
            ub = i;
        }
        else if (cmp > 0) {
            lb = i + 1;
        }
        else {
            return i;
        }
    }
    return -1;
}

char *nl_name(struct node_list *nl, int index){
    if (index < 0) {
        return "UNDEFINED";
    }
    return nl->nodes[index];
}

void nl_destroy(struct node_list *nl){
    int i;

    for (i = 0; i < nl->nnodes; i++) {
        free(nl->nodes[i]);
    }
    free(nl->nodes);
    free(nl);
}

/* Set the distance from src to dst.
 */
void set_dist(struct node_list *nl, int graph[], int nnodes, char *src, char *dst, int dist){
    int x = nl_index(nl, src), y = nl_index(nl, dst);
    if (x < 0 || y < 0) {
        fprintf(stderr, "set_dist: bad source or destination\n");
        return;
    }
    graph[INDEX(x, y, nnodes)] = dist;
    graph[INDEX(y, x, nnodes)] = dist;
}

char* addr_to_string (struct sockaddr_in addr) {
    char* addr_string = malloc(40);
    strcpy(addr_string, inet_ntoa(addr.sin_addr));
    strcat(addr_string, ":");
    char* port = malloc(12);
    sprintf(port, "%d", ntohs(addr.sin_port));
    strcat(addr_string, port);
    free(port);
    return addr_string;
}

struct sockaddr_in string_to_addr(char* string) {
    char *port = index(string, ':');
    *port++ = '\0';

    struct sockaddr_in addr;
    memset((void*)&addr, 0, sizeof(addr));
    addr_get(&addr, string, atoi(port));
    // *--port = ':';
    return addr;
}


/*************************************************
    Dijkstra's algorithm
*************************************************/

//Return the index in dist[] has the minimum distance
int getMinIndex(int visited[], int dist[], int nnodes){
  int minIndex = UNDEFINED;
  int minDist = INFINITY;
  int i = 0;
  for(i = 0 ; i < nnodes; i++){
      if(!visited[i] && dist[i] < minDist){
          minDist = dist[i];
          minIndex = i;
      }
  }
  return minIndex;
}

int isConnected(int graph[], int x, int y, int nnodes){
  int xToy = (graph[INDEX(x, y, nnodes)] != INFINITY);
  int yTox = (graph[INDEX(y, x, nnodes)] != INFINITY);
  return xToy && yTox;
}

/* Dijkstra's algorith.  graph[INDEX(x, y, nnodes)] contains the
 * distance of node x to node y.  nnodes is the number of nodes.  src
 * is that starting node.  Output dist[x] gives the distance from src
 * to x.  Output prev[x] gives the last hop from src to x.
 */
void dijkstra(int graph[], int nnodes, int src, int dist[], int prev[]) {
  int visited[nnodes];

  int i = 0;
  for(i = 0; i < nnodes; i++){
      visited[i] = 0;
  }
  for(i = 0; i < nnodes; i++){
      dist[i] = INFINITY;
      prev[i] = UNDEFINED;
  }

  dist[src] = 0;

  for(i = 0; i < nnodes; i++){
      int minIndex = getMinIndex(visited, dist, nnodes);
      int minDist = dist[minIndex];

      if(minIndex == UNDEFINED){
          break;
      }

      //Visited this node
      visited[minIndex] = 1;

      //Update the distance;
      int j = 0;
      for(j = 0; j < nnodes; j++){
          if(visited[j]){
              continue;
          }
          if(isConnected(graph, minIndex, j, nnodes)){
              if(dist[j] > graph[INDEX(minIndex, j, nnodes)] + minDist){
                  dist[j] = graph[INDEX(minIndex, j, nnodes)] + minDist;
                  prev[j] = minIndex;
              }
          }
      }
  }
}

char* next_hop(char *src, char *des) {
    char *next;
    int nnodes = nl_nsites(nl);
    int dist[nnodes], prev[nnodes];
    int src_idx = nl_index(nl, src), des_idx = nl_index(nl, des);

    if (src_idx < 0) {
        perror("next_hop: cannnot find the source in node list\n");
        return next;
    }
    if (des_idx < 0) {
        perror("next_hop: cannnot find the destination in node list\n");
        return next;
    }

    dijkstra(graph, nnodes, src_idx, dist, prev);
    next = nl_name(nl, prev[des_idx]);
    printf("!!!!next: %s\n", next);

    if (strlen(next)-1 == strlen(src)) {
      int i = 0;
      for (;i < strlen(src); i++) {
          if (next[i] != src[i]) {
              break;
          }
      }
      if (i == strlen(src)) {
          next = des;
      }
    }

    if (strcmp(next, src) == 0) {
        next = des;
    }

    return next;
}

struct graph_struct {
    struct node_list *nl;
};

void update_nl_and_graph(char* src, char* payload) {
    if (nl == NULL) {
         nl = nl_create();
    }

    // get new nodes number and peers
    int new_node_count = nl_index(nl, src) < 0 ? 1:0;
    size_t peer_num = 0;

    size_t i = 0, j = 0;
    for (; i < payload[i] != '\0'; i++) {
        if (payload[i] == ';') {
            peer_num++;
        }
    }


    char **peers = malloc(peer_num);
    size_t len = strlen(payload);

    int peer_count = peer_num;
    for (i = 1; i < len;) {
        j = i + 1;
        while (j < len && payload[j] != ';') {
            j++;
        }

        peers[--peer_count] = malloc(j-i+1);
        memcpy(peers[peer_count], payload+i, j-i);
        peers[peer_count][j-i] = '\0';
        new_node_count += nl_index(nl, peers[peer_count]) < 0 ? 1:0;
        i = j + 1;
    }

    if (new_node_count > 0) {
        // create a new node_list
        struct node_list *nl_old = nl;
        nl = nl_create();
        nl_add(nl, src);

        for (i = 0; i < nl_old->nnodes; i++) {
            nl_add(nl, nl_old->nodes[i]);
        }

        for (i = 0; i < peer_num; i++) {
            nl_add(nl, peers[i]);
        }

        // map for indexes of the old node_list and new node_list
        int* map = calloc(nl_old->nnodes, sizeof(int));
        for (i = 0; i < nl_old->nnodes; i++) {
            map[i] = nl_index(nl, nl_old->nodes[i]);
        }

        // create a new graph based on the old one
        int *graph_old = graph;
        graph = calloc(nl->nnodes * nl->nnodes, sizeof(int));
        for (i = 0; i < nl_old->nnodes; i++) {
            for (; j < nl_old->nnodes; j++) {
                graph[INDEX(i, j, nl->nnodes)] = graph[INDEX(j, i, nl->nnodes)] = graph_old[INDEX(i, j, nl_old->nnodes)];
            }
        }
    }


    // update the graph based on the src and payloads
    int src_idx = nl_index(nl, src);
    for (i = 0; i < nl->nnodes; i++) {
        graph[INDEX(i, src_idx, nl->nnodes)] = graph[INDEX(src_idx, i, nl->nnodes)] = INFINITY;
    }

    for (i = 0; i < peer_num; i++) {
        int peer_idx = nl_index(nl, peers[i]);
        graph[INDEX(peer_idx, src_idx, nl->nnodes)] = graph[INDEX(src_idx, peer_idx, nl->nnodes)] = 1;
    }

    // display information of node_list and graph
    // printf("\n[Node info]\n");
    // for (i = 0; i < nl->nnodes; i++) {
    //     printf("%s!!!\n", nl->nodes[i]);
    // }

    printf("[Graph info]\n");
    for (i = 0; i < nl->nnodes; i++) {
        for (j = i + 1; j < nl->nnodes; j++) {
            if (graph[INDEX(i, j, nl->nnodes)] == 1) {
                printf("<%s, %s>\n", nl->nodes[i], nl->nodes[j]);
            }
        }
    }

}
