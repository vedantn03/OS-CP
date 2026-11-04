#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void error_message(const char *message) { fprintf(stderr, "synclab: %s\n", message); }

static int parse_number(const char *text, long minimum, long maximum, long *out) {
    char *end;
    long value;
    if (text == NULL || *text == '\0') return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < minimum || value > maximum) return 0;
    *out = value;
    return 1;
}

static int sem_wait_checked(sem_t *sem) {
    int result;
    do { result = sem_wait(sem); } while (result != 0 && errno == EINTR);
    return result;
}

static void sleep_us(long usec) {
    struct timespec delay;
    delay.tv_sec = usec / 1000000;
    delay.tv_nsec = (usec % 1000000) * 1000;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) { }
}

typedef struct {
    volatile long counter;
    long iterations;
    int mode;
    pthread_barrier_t start;
    pthread_mutex_t mutex;
    sem_t semaphore;
    _Atomic int flag[2];
    _Atomic int turn;
    _Atomic int runtime_error;
} CounterState;

typedef struct { CounterState *state; int id; } CounterArg;

static void peterson_enter(CounterState *state, int me) {
    int other = 1 - me;
    atomic_store(&state->flag[me], 1);
    atomic_store(&state->turn, other);
    while (atomic_load(&state->flag[other]) && atomic_load(&state->turn) == other) sched_yield();
}

static void peterson_exit(CounterState *state, int me) { atomic_store(&state->flag[me], 0); }

static void *counter_worker(void *opaque) {
    CounterArg *arg = opaque;
    CounterState *state = arg->state;
    long i;
    int barrier_result = pthread_barrier_wait(&state->start);
    if (barrier_result != 0 && barrier_result != PTHREAD_BARRIER_SERIAL_THREAD) return NULL;
    for (i = 0; i < state->iterations; i++) {
        if (state->mode == 1) {
            if (pthread_mutex_lock(&state->mutex) != 0) { atomic_store(&state->runtime_error, 1); return NULL; }
        } else if (state->mode == 2 && sem_wait_checked(&state->semaphore) != 0) {
            atomic_store(&state->runtime_error, 1); return NULL;
        }
        else if (state->mode == 3) peterson_enter(state, arg->id);
        state->counter = state->counter + 1;
        if (state->mode == 1) {
            if (pthread_mutex_unlock(&state->mutex) != 0) { atomic_store(&state->runtime_error, 1); return NULL; }
        } else if (state->mode == 2 && sem_post(&state->semaphore) != 0) {
            atomic_store(&state->runtime_error, 1); return NULL;
        }
        else if (state->mode == 3) peterson_exit(state, arg->id);
    }
    return NULL;
}

static int counter_command(int argc, char **argv) {
    const char *mode_text = NULL;
    long threads = 2, iterations = 100000, expected;
    int seen_mode = 0, seen_threads = 0, seen_iters = 0, mode, i, created = 0;
    pthread_t ids[16];
    CounterArg args[16];
    CounterState state;
    memset(&state, 0, sizeof(state));
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0) { if (++i >= argc || seen_mode++) return 2; mode_text = argv[i]; }
        else if (strcmp(argv[i], "--threads") == 0) { if (++i >= argc || seen_threads++ || !parse_number(argv[i], 1, 16, &threads)) return 2; }
        else if (strcmp(argv[i], "--iters") == 0) { if (++i >= argc || seen_iters++ || !parse_number(argv[i], 1, 50000000, &iterations)) return 2; }
        else return 2;
    }
    if (!seen_mode || mode_text == NULL) return 2;
    if (strcmp(mode_text, "none") == 0) mode = 0;
    else if (strcmp(mode_text, "mutex") == 0) mode = 1;
    else if (strcmp(mode_text, "sem") == 0) mode = 2;
    else if (strcmp(mode_text, "peterson") == 0) mode = 3;
    else return 2;
    if (mode == 3 && threads != 2) return 2;
    state.mode = mode;
    state.iterations = iterations;
    atomic_init(&state.flag[0], 0);
    atomic_init(&state.flag[1], 0);
    atomic_init(&state.turn, 0);
    atomic_init(&state.runtime_error, 0);
    if (pthread_barrier_init(&state.start, NULL, (unsigned)threads + 1U) != 0) { error_message("barrier initialization failed"); return 1; }
    if (mode == 1 && pthread_mutex_init(&state.mutex, NULL) != 0) { pthread_barrier_destroy(&state.start); error_message("mutex initialization failed"); return 1; }
    if (mode == 2 && sem_init(&state.semaphore, 0, 1) != 0) { pthread_barrier_destroy(&state.start); error_message("semaphore initialization failed"); return 1; }
    for (i = 0; i < (int)threads; i++) {
        args[i].state = &state; args[i].id = i;
        if (pthread_create(&ids[i], NULL, counter_worker, &args[i]) != 0) break;
        created++;
    }
    if (created != (int)threads) {
        error_message("thread creation failed"); return 1;
    }
    { int br = pthread_barrier_wait(&state.start); if (br != 0 && br != PTHREAD_BARRIER_SERIAL_THREAD) { error_message("barrier wait failed"); return 1; } }
    for (i = 0; i < (int)threads; i++) if (pthread_join(ids[i], NULL) != 0) { error_message("thread join failed"); return 1; }
    if (atomic_load(&state.runtime_error)) { error_message("counter synchronization failed"); return 1; }
    expected = threads * iterations;
    printf("mode: %s\nthreads: %ld\niters: %ld\nexpected: %ld\nfinal: %ld\ncorrect: %s\nlost_updates: %ld\n", mode_text, threads, iterations, expected, state.counter, state.counter == expected ? "yes" : "no", expected - state.counter);
    pthread_barrier_destroy(&state.start);
    if (mode == 1) pthread_mutex_destroy(&state.mutex);
    if (mode == 2) sem_destroy(&state.semaphore);
    return 0;
}

typedef struct {
    int policy;
    long readers, writers, ops, hold_us;
    volatile long a, b, version;
    pthread_barrier_t start;
    pthread_mutex_t reader_mutex, writer_count_mutex;
    sem_t resource, read_try, turnstile;
    long reader_count, waiting_writers;
    _Atomic long active_readers, active_writers, violations, max_readers, max_writers;
    _Atomic long torn_reads, reads_done, writes_done;
    _Atomic int runtime_error;
} RwState;

typedef struct { RwState *state; int writer; } RwArg;

static void update_max(_Atomic long *target, long value) {
    long old = atomic_load(target);
    while (old < value && !atomic_compare_exchange_weak(target, &old, value)) { }
}

static void rw_read_lock(RwState *s) {
    if (s->policy == 0) return;
    if (s->policy == 1) {
        pthread_mutex_lock(&s->reader_mutex);
        s->reader_count++;
        if (s->reader_count == 1 && sem_wait_checked(&s->resource) != 0) atomic_store(&s->runtime_error, 1);
        pthread_mutex_unlock(&s->reader_mutex);
    } else if (s->policy == 2) {
        if (sem_wait_checked(&s->read_try) != 0) atomic_store(&s->runtime_error, 1);
        pthread_mutex_lock(&s->reader_mutex);
        s->reader_count++;
        if (s->reader_count == 1 && sem_wait_checked(&s->resource) != 0) atomic_store(&s->runtime_error, 1);
        pthread_mutex_unlock(&s->reader_mutex);
        sem_post(&s->read_try);
    } else {
        if (sem_wait_checked(&s->turnstile) != 0) atomic_store(&s->runtime_error, 1);
        sem_post(&s->turnstile);
        pthread_mutex_lock(&s->reader_mutex);
        s->reader_count++;
        if (s->reader_count == 1 && sem_wait_checked(&s->resource) != 0) atomic_store(&s->runtime_error, 1);
        pthread_mutex_unlock(&s->reader_mutex);
    }
}

static void rw_read_unlock(RwState *s) {
    if (s->policy == 0) return;
    pthread_mutex_lock(&s->reader_mutex);
    s->reader_count--;
    if (s->reader_count == 0) sem_post(&s->resource);
    pthread_mutex_unlock(&s->reader_mutex);
}

static void rw_write_lock(RwState *s) {
    if (s->policy == 0) return;
    if (s->policy == 1) { if (sem_wait_checked(&s->resource) != 0) atomic_store(&s->runtime_error, 1); }
    else if (s->policy == 2) {
        pthread_mutex_lock(&s->writer_count_mutex);
        s->waiting_writers++;
        if (s->waiting_writers == 1 && sem_wait_checked(&s->read_try) != 0) atomic_store(&s->runtime_error, 1);
        pthread_mutex_unlock(&s->writer_count_mutex);
        if (sem_wait_checked(&s->resource) != 0) atomic_store(&s->runtime_error, 1);
    } else {
        if (sem_wait_checked(&s->turnstile) != 0) atomic_store(&s->runtime_error, 1);
        if (sem_wait_checked(&s->resource) != 0) atomic_store(&s->runtime_error, 1);
    }
}

static void rw_write_unlock(RwState *s) {
    if (s->policy == 0) return;
    if (s->policy == 2) {
        sem_post(&s->resource);
        pthread_mutex_lock(&s->writer_count_mutex);
        s->waiting_writers--;
        if (s->waiting_writers == 0) sem_post(&s->read_try);
        pthread_mutex_unlock(&s->writer_count_mutex);
    } else if (s->policy == 3) { sem_post(&s->resource); sem_post(&s->turnstile); }
    else sem_post(&s->resource);
}

static void *rw_worker(void *opaque) {
    RwArg *arg = opaque;
    RwState *s = arg->state;
    long i;
    int br = pthread_barrier_wait(&s->start);
    if (br != 0 && br != PTHREAD_BARRIER_SERIAL_THREAD) { atomic_store(&s->runtime_error, 1); return NULL; }
    for (i = 0; i < s->ops; i++) {
        if (!arg->writer) {
            long x, y, active;
            rw_read_lock(s);
            active = atomic_fetch_add(&s->active_readers, 1) + 1;
            if (atomic_load(&s->active_writers) != 0) atomic_fetch_add(&s->violations, 1);
            update_max(&s->max_readers, active);
            x = s->a; sleep_us(s->hold_us); y = s->b;
            if (x != y) atomic_fetch_add(&s->torn_reads, 1);
            atomic_fetch_add(&s->reads_done, 1);
            atomic_fetch_sub(&s->active_readers, 1);
            rw_read_unlock(s);
        } else {
            long active;
            rw_write_lock(s);
            active = atomic_fetch_add(&s->active_writers, 1) + 1;
            if (active > 1 || atomic_load(&s->active_readers) != 0) atomic_fetch_add(&s->violations, 1);
            update_max(&s->max_writers, active);
            s->version++; s->a = s->version; sleep_us(s->hold_us); s->b = s->version;
            atomic_fetch_add(&s->writes_done, 1);
            atomic_fetch_sub(&s->active_writers, 1);
            rw_write_unlock(s);
        }
    }
    return NULL;
}

static int rw_command(int argc, char **argv) {
    const char *policy_text = NULL;
    long readers = 0, writers = 0, ops = 0, hold = 0;
    int seen[5] = {0}, policy, i, count, created = 0;
    pthread_t ids[32]; RwArg args[32]; RwState s;
    memset(&s, 0, sizeof(s));
    for (i = 0; i < argc; i++) {
        int slot; long *target = NULL; long min = 0, max = 0;
        if (strcmp(argv[i], "--policy") == 0) { slot = 0; if (++i >= argc || seen[slot]++) return 2; policy_text = argv[i]; continue; }
        if (strcmp(argv[i], "--readers") == 0) { slot = 1; target = &readers; min = 1; max = 16; }
        else if (strcmp(argv[i], "--writers") == 0) { slot = 2; target = &writers; min = 1; max = 16; }
        else if (strcmp(argv[i], "--ops") == 0) { slot = 3; target = &ops; min = 1; max = 100000; }
        else if (strcmp(argv[i], "--hold-us") == 0) { slot = 4; target = &hold; min = 0; max = 100000; }
        else return 2;
        if (++i >= argc || seen[slot]++ || !parse_number(argv[i], min, max, target)) return 2;
    }
    for (i = 0; i < 5; i++) if (!seen[i]) return 2;
    if (strcmp(policy_text, "none") == 0) policy = 0;
    else if (strcmp(policy_text, "reader") == 0) policy = 1;
    else if (strcmp(policy_text, "writer") == 0) policy = 2;
    else if (strcmp(policy_text, "fair") == 0) policy = 3;
    else return 2;
    s.policy = policy; s.readers = readers; s.writers = writers; s.ops = ops; s.hold_us = hold;
    atomic_init(&s.active_readers, 0); atomic_init(&s.active_writers, 0);
    atomic_init(&s.violations, 0); atomic_init(&s.max_readers, 0); atomic_init(&s.max_writers, 0);
    atomic_init(&s.torn_reads, 0); atomic_init(&s.reads_done, 0); atomic_init(&s.writes_done, 0);
    atomic_init(&s.runtime_error, 0);
    count = (int)(readers + writers);
    if (pthread_barrier_init(&s.start, NULL, (unsigned)count + 1) != 0 || pthread_mutex_init(&s.reader_mutex, NULL) != 0 || pthread_mutex_init(&s.writer_count_mutex, NULL) != 0 || sem_init(&s.resource, 0, 1) != 0 || sem_init(&s.read_try, 0, 1) != 0 || sem_init(&s.turnstile, 0, 1) != 0) { error_message("rw resource initialization failed"); return 1; }
    for (i = 0; i < count; i++) {
        args[i].state = &s; args[i].writer = i >= readers;
        if (pthread_create(&ids[i], NULL, rw_worker, &args[i]) != 0) break;
        created++;
    }
    if (created != count) { error_message("thread creation failed"); return 1; }
    { int br = pthread_barrier_wait(&s.start); if (br != 0 && br != PTHREAD_BARRIER_SERIAL_THREAD) atomic_store(&s.runtime_error, 1); }
    for (i = 0; i < count; i++) if (pthread_join(ids[i], NULL) != 0) atomic_store(&s.runtime_error, 1);
    if (atomic_load(&s.runtime_error)) { error_message("rw synchronization failed"); return 1; }
    printf("policy: %s\nreaders: %ld\nwriters: %ld\nops: %ld\nreads_done: %ld\nwrites_done: %ld\ntorn_reads: %ld\nviolations: %ld\nmax_readers: %ld\nmax_writers: %ld\n", policy_text, readers, writers, ops, atomic_load(&s.reads_done), atomic_load(&s.writes_done), atomic_load(&s.torn_reads), atomic_load(&s.violations), atomic_load(&s.max_readers), atomic_load(&s.max_writers));
    pthread_barrier_destroy(&s.start); pthread_mutex_destroy(&s.reader_mutex); pthread_mutex_destroy(&s.writer_count_mutex); sem_destroy(&s.resource); sem_destroy(&s.read_try); sem_destroy(&s.turnstile);
    return 0;
}

typedef struct {
    int n, mode;
    long meals, eat_us, timeout_ms;
    pthread_mutex_t *forks;
    _Atomic int *owners;
    pthread_barrier_t left_barrier;
    sem_t waiter;
    _Atomic long stuck, meals_eaten, violations;
    _Atomic int runtime_error;
} PhilState;

typedef struct { PhilState *state; int id; } PhilArg;

static void fork_acquired(PhilState *s, int fork) {
    if (atomic_fetch_add(&s->owners[fork], 1) != 0) atomic_fetch_add(&s->violations, 1);
}
static void fork_released(PhilState *s, int fork) { atomic_fetch_sub(&s->owners[fork], 1); }

static void *ordered_philosopher_worker(void *opaque) {
    PhilArg *arg = opaque; PhilState *s = arg->state;
    int left = arg->id, right = (arg->id + 1) % s->n, first, second;
    long meal;
    for (meal = 0; meal < s->meals; meal++) {
        first = left; second = right;
        if (first > second) { first = right; second = left; }
        if (pthread_mutex_lock(&s->forks[first]) != 0) { atomic_store(&s->runtime_error, 1); return NULL; }
        fork_acquired(s, first);
        if (pthread_mutex_lock(&s->forks[second]) != 0) { atomic_store(&s->runtime_error, 1); fork_released(s, first); pthread_mutex_unlock(&s->forks[first]); return NULL; }
        fork_acquired(s, second);
        sleep_us(s->eat_us);
        atomic_fetch_add(&s->meals_eaten, 1);
        fork_released(s, second); pthread_mutex_unlock(&s->forks[second]);
        fork_released(s, first); pthread_mutex_unlock(&s->forks[first]);
    }
    return NULL;
}

static void *waiter_philosopher_worker(void *opaque) {
    PhilArg *arg = opaque; PhilState *s = arg->state;
    int left = arg->id, right = (arg->id + 1) % s->n;
    long meal;
    for (meal = 0; meal < s->meals; meal++) {
        if (sem_wait_checked(&s->waiter) != 0) { atomic_store(&s->runtime_error, 1); return NULL; }
        if (pthread_mutex_lock(&s->forks[left]) != 0) { atomic_store(&s->runtime_error, 1); return NULL; }
        fork_acquired(s, left);
        if (pthread_mutex_lock(&s->forks[right]) != 0) { atomic_store(&s->runtime_error, 1); fork_released(s, left); pthread_mutex_unlock(&s->forks[left]); return NULL; }
        fork_acquired(s, right);
        sleep_us(s->eat_us);
        atomic_fetch_add(&s->meals_eaten, 1);
        fork_released(s, right); pthread_mutex_unlock(&s->forks[right]);
        fork_released(s, left); pthread_mutex_unlock(&s->forks[left]);
        sem_post(&s->waiter);
    }
    return NULL;
}

static void *phil_worker(void *opaque) {
    PhilArg *arg = opaque; PhilState *s = arg->state;
    int left = arg->id, right = (arg->id + 1) % s->n, br;
    if (s->mode == 1) return ordered_philosopher_worker(opaque);
    if (s->mode == 2) return waiter_philosopher_worker(opaque);
    {
        struct timespec deadline;
        if (pthread_mutex_lock(&s->forks[left]) != 0) { atomic_store(&s->runtime_error, 1); return NULL; }
        fork_acquired(s, left);
        br = pthread_barrier_wait(&s->left_barrier);
        if (br != 0 && br != PTHREAD_BARRIER_SERIAL_THREAD) atomic_store(&s->runtime_error, 1);
        if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) { atomic_store(&s->runtime_error, 1); }
        deadline.tv_sec += s->timeout_ms / 1000;
        deadline.tv_nsec += (s->timeout_ms % 1000) * 1000000;
        if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
        { int result = pthread_mutex_timedlock(&s->forks[right], &deadline);
          if (result == ETIMEDOUT) atomic_fetch_add(&s->stuck, 1);
          else if (result != 0) atomic_store(&s->runtime_error, 1);
          else { fork_acquired(s, right); atomic_fetch_add(&s->meals_eaten, 1); fork_released(s, right); pthread_mutex_unlock(&s->forks[right]); }
        }
        fork_released(s, left); pthread_mutex_unlock(&s->forks[left]);
        return NULL;
    }
}

static int phil_command(int argc, char **argv) {
    const char *mode_text = NULL;
    long n = 0, meals = 10, eat = 200, timeout = 200;
    int seen[5] = {0}, mode, i, created = 0;
    pthread_t ids[32]; PhilArg args[32]; PhilState s;
    memset(&s, 0, sizeof(s));
    for (i = 0; i < argc; i++) {
        int slot; long *target = NULL; long min = 0, max = 0;
        if (strcmp(argv[i], "--mode") == 0) { slot = 0; if (++i >= argc || seen[slot]++) return 2; mode_text = argv[i]; continue; }
        if (strcmp(argv[i], "--n") == 0) { slot = 1; target = &n; min = 2; max = 32; }
        else if (strcmp(argv[i], "--meals") == 0) { slot = 2; target = &meals; min = 1; max = 100000; }
        else if (strcmp(argv[i], "--eat-us") == 0) { slot = 3; target = &eat; min = 0; max = 100000; }
        else if (strcmp(argv[i], "--timeout-ms") == 0) { slot = 4; target = &timeout; min = 1; max = 60000; }
        else return 2;
        if (++i >= argc || seen[slot]++ || !parse_number(argv[i], min, max, target)) return 2;
    }
    if (!seen[0] || !seen[1]) return 2;
    if (strcmp(mode_text, "naive") == 0) mode = 0;
    else if (strcmp(mode_text, "ordered") == 0) mode = 1;
    else if (strcmp(mode_text, "waiter") == 0) mode = 2;
    else return 2;
    s.n = (int)n; s.mode = mode; s.meals = meals; s.eat_us = eat; s.timeout_ms = timeout;
    atomic_init(&s.stuck, 0); atomic_init(&s.meals_eaten, 0); atomic_init(&s.violations, 0);
    atomic_init(&s.runtime_error, 0);
    s.forks = calloc((size_t)n, sizeof(*s.forks)); s.owners = calloc((size_t)n, sizeof(*s.owners));
    if (s.forks == NULL || s.owners == NULL) { free(s.forks); free(s.owners); error_message("fork allocation failed"); return 1; }
    for (i = 0; i < (int)n; i++) {
        atomic_init(&s.owners[i], 0);
        if (pthread_mutex_init(&s.forks[i], NULL) != 0) { error_message("fork mutex initialization failed"); return 1; }
    }
    if (mode == 0 && pthread_barrier_init(&s.left_barrier, NULL, (unsigned)n) != 0) { error_message("barrier initialization failed"); return 1; }
    if (mode == 2 && sem_init(&s.waiter, 0, (unsigned)(n - 1)) != 0) { error_message("waiter semaphore initialization failed"); return 1; }
    for (i = 0; i < (int)n; i++) { args[i].state = &s; args[i].id = i; if (pthread_create(&ids[i], NULL, phil_worker, &args[i]) != 0) break; created++; }
    if (created != n) { error_message("thread creation failed"); return 1; }
    for (i = 0; i < (int)n; i++) if (pthread_join(ids[i], NULL) != 0) atomic_store(&s.runtime_error, 1);
    if (atomic_load(&s.runtime_error)) { error_message("philosopher synchronization failed"); return 1; }
    printf("mode: %s\nphilosophers: %ld\nmeals: %ld\ndeadlock: %s\nstuck: %ld\nmeals_eaten: %ld\nviolations: %ld\n", mode_text, n, meals, atomic_load(&s.stuck) > 0 ? "yes" : "no", atomic_load(&s.stuck), atomic_load(&s.meals_eaten), atomic_load(&s.violations));
    if (mode == 0) pthread_barrier_destroy(&s.left_barrier);
    if (mode == 2) sem_destroy(&s.waiter);
    for (i = 0; i < (int)n; i++) pthread_mutex_destroy(&s.forks[i]);
    free(s.forks); free(s.owners);
    return 0;
}

int main(int argc, char **argv) {
    int result;
    if (argc < 2) { error_message("usage: synclab counter|rw|phil ..."); return 2; }
    if (strcmp(argv[1], "counter") == 0) result = counter_command(argc - 2, argv + 2);
    else if (strcmp(argv[1], "rw") == 0) result = rw_command(argc - 2, argv + 2);
    else if (strcmp(argv[1], "phil") == 0) result = phil_command(argc - 2, argv + 2);
    else result = 2;
    if (result == 2) error_message("invalid command, option, or value");
    return result;
}
