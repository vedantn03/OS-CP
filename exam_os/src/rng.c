#include <pthread.h>
#include <stdint.h>
#include "shared.h"
#include "rng.h"

static _Thread_local unsigned int thread_seed;

void rng_init_thread(unsigned stream) {
    uint64_t value = (uint64_t)g_config.random_seed + stream;
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    thread_seed = (unsigned int)value;
    if (thread_seed == 0) thread_seed = 1;
}

unsigned int rng_next(void) {
    if (thread_seed == 0) rng_init_thread(0);
    thread_seed = thread_seed * 1664525u + 1013904223u;
    return thread_seed;
}

int rng_uniform(int upper_bound) {
    return upper_bound > 0 ? (int)(rng_next() % (unsigned int)upper_bound) : 0;
}
