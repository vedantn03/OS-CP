#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "rng.h"

static _Thread_local unsigned int thread_seed;

void rng_seed_thread(unsigned int salt) {
    unsigned long id = (unsigned long)pthread_self();
    thread_seed = (unsigned int)time(NULL) ^ (unsigned int)id ^ salt;
    if (thread_seed == 0) thread_seed = 1;
}

unsigned int rng_next(void) {
    if (thread_seed == 0) rng_seed_thread(0);
    thread_seed = thread_seed * 1664525u + 1013904223u;
    return thread_seed;
}

int rng_uniform(int upper_bound) {
    return upper_bound > 0 ? (int)(rng_next() % (unsigned int)upper_bound) : 0;
}
