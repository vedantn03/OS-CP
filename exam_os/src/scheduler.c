#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "scheduler.h"
#include "logger.h"
#include "rng.h"
#include "process.h"

static int ready_queue[MAX_STUDENTS];
static int rq_size = 0;
static pthread_mutex_t rq_lock = PTHREAD_MUTEX_INITIALIZER;
static int rr_index = 0;

static void swap_slots(int a, int b) {
    int tmp = ready_queue[a];
    ready_queue[a] = ready_queue[b];
    ready_queue[b] = tmp;
}

/* Caller holds rq_lock and g_state.lock. */
static int slot_before(int left, int right) {
    int left_remaining = g_state.processes[left].remaining_time;
    int right_remaining = g_state.processes[right].remaining_time;
    return left_remaining < right_remaining ||
           (left_remaining == right_remaining && left < right);
}

/* Caller holds rq_lock and g_state.lock. */
static void heap_push(int slot) {
    ready_queue[rq_size] = slot;
    int i = rq_size++;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!slot_before(ready_queue[i], ready_queue[parent])) break;
        swap_slots(parent, i);
        i = parent;
    }
}

/* Caller holds rq_lock and g_state.lock. */
static int heap_pop(void) {
    int top = ready_queue[0];
    ready_queue[0] = ready_queue[--rq_size];
    int i = 0;
    while (1) {
        int left = 2 * i + 1;
        int right = left + 1;
        int smallest = i;
        if (left < rq_size && slot_before(ready_queue[left], ready_queue[smallest]))
            smallest = left;
        if (right < rq_size && slot_before(ready_queue[right], ready_queue[smallest]))
            smallest = right;
        if (smallest == i) break;
        swap_slots(i, smallest);
        i = smallest;
    }
    return top;
}

/* Caller holds rq_lock and g_state.lock. */
static int priority_select(void) {
    while (rq_size > 0) {
        int slot = heap_pop();
        if (g_state.processes[slot].state != TERMINATED)
            return slot;
    }
    return -1;
}

/* Caller holds rq_lock and g_state.lock. */
static int rr_select(int *selected_index) {
    if (rq_size == 0) return -1;
    if (rr_index >= rq_size) rr_index = 0;

    int scanned = 0;
    while (scanned < rq_size) {
        if (rr_index >= rq_size) rr_index = 0;
        int slot = ready_queue[rr_index];
        if (g_state.processes[slot].state == TERMINATED) {
            memmove(&ready_queue[rr_index], &ready_queue[rr_index + 1],
                    (size_t)(rq_size - rr_index - 1) * sizeof(ready_queue[0]));
            rq_size--;
            if (rr_index >= rq_size) rr_index = 0;
            continue;
        }
        if (g_state.processes[slot].state == READY) {
            *selected_index = rr_index;
            return slot;
        }
        rr_index = (rr_index + 1) % rq_size;
        scanned++;
    }
    return -1;
}

/* Caller holds rq_lock and g_state.lock. */
static void rr_remove(int index) {
    memmove(&ready_queue[index], &ready_queue[index + 1],
            (size_t)(rq_size - index - 1) * sizeof(ready_queue[0]));
    rq_size--;
    if (rr_index >= rq_size) rr_index = 0;
}

static void wake_waiting_processes(int tick) {
    pthread_mutex_lock(&rq_lock);
    pthread_mutex_lock(&g_state.lock);
    for (int slot = 0; slot < g_state.process_count; slot++) {
        PCB *p = &g_state.processes[slot];
        if (p->state == WAITING && tick >= p->wait_until_tick) {
            if (process_set_state(p, READY) == 0) {
                p->last_ready_tick = tick;
                if (g_config.sched_algo == PRIORITY)
                    heap_push(slot);
            }
        }
    }
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&rq_lock);
}

void scheduler_init(void) {
    rq_size = 0;
    rr_index = 0;
    log_event("INFO", "SCHEDULER", "Scheduler initialized");
}

void scheduler_add_process(PCB process) {
    pthread_mutex_lock(&rq_lock);
    pthread_mutex_lock(&g_state.lock);

    if (g_state.process_count >= MAX_STUDENTS || rq_size >= MAX_STUDENTS) {
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&rq_lock);
        log_event("WARN", "SCHEDULER", "Cannot add process: process or ready queue full");
        return;
    }
    if (process.pid != g_state.process_count + 1) {
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&rq_lock);
        log_event("ERROR", "SCHEDULER", "Cannot add process: PID does not match slot");
        return;
    }

    int now = g_state.current_tick;
    process.arrival_tick = now;
    process.first_run_tick = -1;
    process.finish_tick = -1;
    process.last_ready_tick = now;
    process.timeout_pending = 0;
    process.timed_out = 0;
    process.cpu_ticks = 0;
    process.deadline_tick = now +
        (g_config.session_limit > 0 ? g_config.session_limit : process.remaining_time);
    if (process_set_state(&process, READY) != 0) {
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&rq_lock);
        log_event("ERROR", "SCHEDULER", "Cannot add process: invalid initial state");
        return;
    }

    int slot = g_state.process_count++;
    g_state.processes[slot] = process;
    if (g_config.sched_algo == PRIORITY)
        heap_push(slot);
    else
        ready_queue[rq_size++] = slot;
    pthread_mutex_unlock(&g_state.lock);

    char msg[128];
    snprintf(msg, sizeof(msg), "PID %d added to ready queue (remaining=%d ticks)",
             process.pid, process.remaining_time);
    pthread_mutex_unlock(&rq_lock);
    log_event("INFO", "SCHEDULER", msg);
}

void scheduler_terminate_process(int pid) {
    process_terminate(pid, 0);
}

static int dispatch_one(const char *algorithm, int *busy) {
    int slot;
    int selected_index = -1;
    int previous_pid;

    pthread_mutex_lock(&rq_lock);
    pthread_mutex_lock(&g_state.lock);
    if (g_config.sched_algo == ROUND_ROBIN)
        slot = rr_select(&selected_index);
    else
        slot = priority_select();

    if (slot < 0) {
        g_state.running_pid = -1;
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&rq_lock);
        *busy = 0;
        return 0;
    }

    PCB *p = &g_state.processes[slot];
    previous_pid = g_state.running_pid;
    if (process_set_state(p, RUNNING) != 0) {
        if (g_config.sched_algo == ROUND_ROBIN) rr_remove(selected_index);
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&rq_lock);
        *busy = 0;
        return 0;
    }
    if (p->first_run_tick < 0) p->first_run_tick = g_state.current_tick;
    p->waiting_time += g_state.current_tick - p->last_ready_tick;
    g_state.dispatches++;
    if (previous_pid > 0 && previous_pid != p->pid)
        g_state.context_switches++;
    g_state.running_pid = p->pid;
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&rq_lock);

    usleep((useconds_t)g_config.tick_ms * 500);

    int done = 0;
    int pid = p->pid;
    pthread_mutex_lock(&g_state.lock);
    p = &g_state.processes[slot];
    if (p->state == RUNNING) {
        int work = g_config.time_quantum < p->remaining_time
            ? g_config.time_quantum : p->remaining_time;
        p->remaining_time -= g_config.time_quantum;
        p->cpu_ticks += work;
        if (p->remaining_time <= 0) {
            done = 1;
        } else {
            process_set_state(p, READY);
            p->last_ready_tick = g_state.current_tick;
        }
    }
    pthread_mutex_unlock(&g_state.lock);

    if (done) {
        if (process_terminate(pid, 0)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "PID %d completed exam (%s)", pid, algorithm);
            log_event("INFO", "SCHEDULER", msg);
        }
    }

    pthread_mutex_lock(&rq_lock);
    pthread_mutex_lock(&g_state.lock);
    if (g_config.sched_algo == ROUND_ROBIN) {
        if (done || g_state.processes[slot].state == TERMINATED) {
            rr_remove(selected_index);
        } else if (g_state.processes[slot].state == READY) {
            rr_index = (selected_index + 1) % rq_size;
        }
    } else if (!done && g_state.processes[slot].state == READY) {
        heap_push(slot);
    }
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&rq_lock);

    *busy = 1;
    return 0;
}

void *scheduler_thread(void *arg) {
    (void)arg;
    rng_init_thread(1);
    log_event("INFO", "SCHEDULER", "Scheduler thread started");

    int total_iterations = 0;
    int busy_iterations = 0;
    int next_arrival_tick = 0;
    while (1) {
        pthread_mutex_lock(&g_state.lock);
        int running = g_state.simulation_running;
        int tick = g_state.current_tick;
        pthread_mutex_unlock(&g_state.lock);
        if (!running) break;

        wake_waiting_processes(tick);

        if (tick >= next_arrival_tick) {
            pthread_mutex_lock(&g_state.lock);
            int added = g_state.process_count;
            pthread_mutex_unlock(&g_state.lock);
            if (added < g_config.num_students) {
                int batch = (g_config.num_students - added < g_config.arrival_batch)
                    ? g_config.num_students - added : g_config.arrival_batch;
                for (int i = 0; i < batch; i++) {
                    PCB p = {
                        .pid = added + i + 1,
                        .state = NEW,
                        .priority = 1,
                        .total_time = g_config.exam_duration,
                        .remaining_time = g_config.exam_duration - rng_uniform(10),
                        .waiting_time = 0,
                        .turnaround_time = 0,
                        .pages_used = 0
                    };
                    scheduler_add_process(p);
                }
            }
            next_arrival_tick = tick - (tick % g_config.arrival_interval)
                              + g_config.arrival_interval;
        }

        int busy = 0;
        total_iterations++;
        if (g_config.sched_algo == ROUND_ROBIN)
            dispatch_one("RR", &busy);
        else
            dispatch_one("PRIORITY", &busy);
        busy_iterations += busy;

        pthread_mutex_lock(&g_state.lock);
        g_state.cpu_utilization = total_iterations > 0
            ? 100.0f * busy_iterations / total_iterations : 0.0f;
        pthread_mutex_unlock(&g_state.lock);
        usleep((useconds_t)g_config.tick_ms * 1000);
    }

    log_event("INFO", "SCHEDULER", "Scheduler thread exiting");
    return NULL;
}
