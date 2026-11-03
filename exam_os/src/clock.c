#include "clock.h"
#include <errno.h>
#include <stdio.h>
#include "shared.h"

long long clock_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

long long clock_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void sim_sleep_ticks(int n) {
    if (n <= 0) return;

    long long milliseconds = (long long)n * g_config.tick_ms;
    struct timespec remaining = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L
    };
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            fprintf(stderr, "ERROR: nanosleep failed\n");
            return;
        }
    }
}
