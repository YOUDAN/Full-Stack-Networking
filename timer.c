#include <sys/time.h>
#include <stdlib.h>
#include "global.h"

struct timer {
    struct timer *next;     // next in sorted linked list
    double when;            // expiry time
    void (*handler)(void *arg);
    void *arg;
};
static struct timer *timers;

/* Return current time.
 */
double timer_now(void){
    struct timeval now;
    double time;

    gettimeofday(&now, 0);
    return now.tv_sec + (double) now.tv_usec / 1000000;
}

/* Set a timer.
 */
void timer_start(double when, void (*handler)(void *arg), void *arg){
    struct timer **tp, *t, *n;

    /* Allocate a new record.
     */
    n = calloc(1, sizeof(*n));
    n->when = when;
    n->handler = handler;
    n->arg = arg;

    /* Insert it in the right place.
     */
    for (tp = &timers; (t = *tp) != 0; tp = &t->next) {
        if (t->when > when) {
            n->next = t;
            *tp = n;
            return;
        }
    }
    *tp = n;
}

/* Check if any timers expired.  Return the amount of time until the
 * next timer, or -1 if there are no timers.
 */
int timer_check(void){
    if (timers == 0) {
        return -1;
    }
    else {
        double now = timer_now();
        while (timers != 0 && timers->when <= now) {
            struct timer *t = timers;

            timers = t->next;
            (*t->handler)(t->arg);
            free(t);
        }
        return timers == 0 ? -1 : (int) ((timers->when - now) * 1000);
    }
}
