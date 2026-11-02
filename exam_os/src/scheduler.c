#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "scheduler.h"
#include "logger.h"
#include "rng.h"
#include "process.h"
#include "io_buffer.h"

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
        if (g_state.processes[slot].state == READY)
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

/* Caller holds rq_lock and g_state.lock. */
static void enqueue_ready(int slot) {
    if (g_config.sched_algo == PRIORITY)
        heap_push(slot);
    else
        ready_queue[rq_size++] = slot;
}

static void scheduler_submission(int pid) {
    char answer[64];
    snprintf(answer, sizeof(answer), "SCHED_PID%d_%u", pid, rng_next() % 1000u);
    if (io_buffer_submit(pid, rng_uniform(10) + 1, answer, 0) == 0) {
        pthread_mutex_lock(&g_state.lock);
        g_state.scheduler_submissions++;
        pthread_mutex_unlock(&g_state.lock);
    }
}

static void wake_waiting_processes(int tick) {
    int woke = 0;
    pthread_mutex_lock(&rq_lock);
    pthread_mutex_lock(&g_state.lock);
    for (int i = 0; i < g_state.process_count; i++) {
        PCB *p = &g_state.processes[i];
        if (p->state == WAITING && p->io_wait_until_tick <= tick) {
            if (process_set_state(p, READY) == 0) {
                p->last_ready_tick = tick;
                p->quantum_remaining = 0;
                enqueue_ready(i);
                g_state.wakeups++;
                woke++;
            }
        }
    }
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&rq_lock);

    if (woke > 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Wakeups processed: %d", woke);
        log_event("INFO", "SCHEDULER", msg);
    }
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
    process.quantum_remaining = 0;
    process.cpu_burst_remaining = 0;
    process.io_wait_until_tick = -1;
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
    enqueue_ready(slot);
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

static void update_cpu_metrics(int busy_tick) {
    pthread_mutex_lock(&g_state.lock);
    if (busy_tick) g_state.cpu_busy_ticks++;
    else g_state.cpu_idle_ticks++;
    int total = g_state.cpu_busy_ticks + g_state.cpu_idle_ticks;
    g_state.cpu_utilization = total > 0
        ? (100.0f * (float)g_state.cpu_busy_ticks / (float)total) : 0.0f;
    pthread_mutex_unlock(&g_state.lock);
}

static void dispatch_one_tick(int tick) {
    enum { ACTION_NONE, ACTION_REQUEUE, ACTION_BLOCK, ACTION_DONE } action = ACTION_NONE;
    int slot = -1;
    int selected_index = -1;
    int pid = -1;

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
        update_cpu_metrics(0);
        return;
    }

    PCB *p = &g_state.processes[slot];
    int previous_pid = g_state.running_pid;
    if (process_set_state(p, RUNNING) != 0) {
        if (g_config.sched_algo == ROUND_ROBIN && selected_index >= 0)
            rr_remove(selected_index);
        g_state.running_pid = -1;
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&rq_lock);
        update_cpu_metrics(0);
        return;
    }

    if (p->first_run_tick < 0) p->first_run_tick = tick;
    p->waiting_time += tick - p->last_ready_tick;
    if (p->quantum_remaining <= 0) p->quantum_remaining = g_config.time_quantum;
    if (g_config.workload_mode == WORKLOAD_INTERACTIVE && p->cpu_burst_remaining <= 0)
        p->cpu_burst_remaining = 1 + rng_uniform(g_config.time_quantum);
    g_state.dispatches++;
    if (previous_pid > 0 && previous_pid != p->pid)
        g_state.context_switches++;
    g_state.running_pid = p->pid;

    p->remaining_time -= 1;
    p->cpu_ticks += 1;
    p->quantum_remaining -= 1;
    if (g_config.workload_mode == WORKLOAD_INTERACTIVE)
        p->cpu_burst_remaining -= 1;

    pid = p->pid;
    if (p->remaining_time <= 0) {
        action = ACTION_DONE;
        g_state.running_pid = -1;
    } else if (g_config.workload_mode == WORKLOAD_INTERACTIVE &&
               p->cpu_burst_remaining <= 0) {
        if (process_set_state(p, WAITING) == 0) {
            p->io_wait_until_tick = tick + 1 + rng_uniform(3);
            p->quantum_remaining = 0;
            action = ACTION_BLOCK;
        } else {
            action = ACTION_REQUEUE;
        }
        g_state.running_pid = -1;
    } else {
        if (process_set_state(p, READY) == 0) {
            p->last_ready_tick = tick + 1;
            action = ACTION_REQUEUE;
        }
        g_state.running_pid = -1;
    }

    if (g_config.sched_algo == ROUND_ROBIN && selected_index >= 0) {
        if (action == ACTION_DONE || action == ACTION_BLOCK) {
            rr_remove(selected_index);
        } else {
            if (p->quantum_remaining <= 0) {
                p->quantum_remaining = 0;
                rr_index = (selected_index + 1) % rq_size;
            } else {
                rr_index = selected_index;
            }
        }
    } else if (g_config.sched_algo == PRIORITY && action == ACTION_REQUEUE) {
        enqueue_ready(slot);
    }

    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&rq_lock);

    update_cpu_metrics(1);

    if (action == ACTION_BLOCK) {
        scheduler_submission(pid);
    } else if (action == ACTION_DONE) {
        if (process_terminate(pid, 0)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "PID %d completed exam", pid);
            log_event("INFO", "SCHEDULER", msg);
        }
    }
}

static void schedule_arrivals(int tick, int *next_arrival_tick) {
    if (tick < *next_arrival_tick) return;

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
    *next_arrival_tick = tick - (tick % g_config.arrival_interval)
                       + g_config.arrival_interval;
}

void *scheduler_thread(void *arg) {
    (void)arg;
    rng_seed_thread(1u ^ g_config.random_seed);
    log_event("INFO", "SCHEDULER", "Scheduler thread started");

    int last_tick = -1;
    int next_arrival_tick = 0;
    while (1) {
        pthread_mutex_lock(&g_state.lock);
        int running = g_state.simulation_running;
        int tick = g_state.current_tick;
        pthread_mutex_unlock(&g_state.lock);
        if (!running) break;

        if (tick <= last_tick) {
            usleep(1000);
            continue;
        }

        for (int t = last_tick + 1; t <= tick; t++) {
            wake_waiting_processes(t);
            schedule_arrivals(t, &next_arrival_tick);
            dispatch_one_tick(t);
        }
        last_tick = tick;
    }

    log_event("INFO", "SCHEDULER", "Scheduler thread exiting");
    return NULL;
}
