#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "scheduler.h"
#include "logger.h"
#include "rng.h"
#include "process.h"
#include "io_buffer.h"

static int ready_queue[MAX_STUDENTS];
static int rq_size = 0;
static pthread_mutex_t rq_lock = PTHREAD_MUTEX_INITIALIZER;
static int rr_index = 0;

typedef struct {
    int tick_start;
    int tick_end;
    int pid;
    const char *reason;
} TraceRow;

#define TRACE_CAPACITY 20000
static TraceRow trace_rows[TRACE_CAPACITY];
static int trace_count;
static int trace_overflows;

static void trace_record(int tick_start, int tick_end, int pid, const char *reason) {
    if (!g_config.trace) return;
    if (trace_count >= TRACE_CAPACITY) {
        trace_overflows++;
        return;
    }
    trace_rows[trace_count++] = (TraceRow){tick_start, tick_end, pid, reason};
}

static void trace_write(void) {
    if (!g_config.trace) return;
    FILE *f = fopen("output/trace.csv", "w");
    if (!f) {
        fprintf(stderr, "ERROR: Could not open output/trace.csv\n");
        return;
    }
    fprintf(f, "tick_start,tick_end,pid,reason\n");
    for (int i = 0; i < trace_count; i++)
        fprintf(f, "%d,%d,%d,%s\n", trace_rows[i].tick_start,
                trace_rows[i].tick_end, trace_rows[i].pid, trace_rows[i].reason);
    if (fclose(f) != 0)
        fprintf(stderr, "ERROR: Could not close output/trace.csv\n");
    if (trace_overflows > 0)
        fprintf(stderr, "WARNING: Scheduler trace capacity exceeded (%d rows dropped)\n",
                trace_overflows);
}

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

static int draw_burst(int remaining_time) {
    int burst = g_config.burst_min +
        rng_uniform(g_config.burst_max - g_config.burst_min + 1);
    return remaining_time < burst ? remaining_time : burst;
}

static void enqueue_woken(int *slots, int count) {
    pthread_mutex_lock(&rq_lock);
    pthread_mutex_lock(&g_state.lock);
    for (int i = 0; i < count; i++) {
        int slot = slots[i];
        if (slot < 0 || slot >= g_state.process_count ||
            g_state.processes[slot].state != READY)
            continue;
        if (g_config.sched_algo == PRIORITY)
            heap_push(slot);
        else
            ready_queue[rq_size++] = slot;
    }
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&rq_lock);
}

static void wake_sleepers(void) {
    int slots[MAX_STUDENTS];
    int count = 0;

    pthread_mutex_lock(&g_state.lock);
    int tick = g_state.current_tick;
    for (int slot = 0; slot < g_state.process_count; slot++) {
        PCB *p = &g_state.processes[slot];
        if (p->state == WAITING && p->wake_tick <= tick &&
            process_set_state(p, READY) == 0) {
            p->last_ready_tick = tick;
            p->burst_remaining = draw_burst(p->remaining_time);
            g_state.wakeups++;
            slots[count++] = slot;
        }
    }
    pthread_mutex_unlock(&g_state.lock);
    enqueue_woken(slots, count);
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
    if (g_config.workload == WORKLOAD_INTERACTIVE &&
        process.burst_remaining <= 0)
        process.burst_remaining = draw_burst(process.remaining_time);
    else if (g_config.workload == WORKLOAD_LEGACY)
        process.burst_remaining = 0;
    process.wake_tick = -1;
    process.bursts_done = 0;
    process.deadline_tick = now +
        (g_config.session_limit > 0 ? g_config.session_limit
         : g_config.workload == WORKLOAD_INTERACTIVE
             ? g_config.exam_duration : process.remaining_time);
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

static void update_cpu_utilization(void) {
    int total = g_state.busy_ticks + g_state.idle_ticks;
    g_state.cpu_utilization = total > 0
        ? 100.0f * g_state.busy_ticks / total : 0.0f;
}

static int wait_for_tick(int *last_seen) {
    pthread_mutex_lock(&g_state.lock);
    while (g_state.simulation_running && g_state.current_tick <= *last_seen) {
        struct timespec abs;
        if (clock_gettime(CLOCK_MONOTONIC, &abs) != 0) {
            fprintf(stderr, "ERROR: clock_gettime failed\n");
            pthread_mutex_unlock(&g_state.lock);
            return -1;
        }
        long timeout_ms = 2L * g_config.tick_ms;
        if (timeout_ms < 20) timeout_ms = 20;
        abs.tv_sec += timeout_ms / 1000;
        abs.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (abs.tv_nsec >= 1000000000L) {
            abs.tv_sec++;
            abs.tv_nsec -= 1000000000L;
        }
        int result = pthread_cond_timedwait(&g_state.tick_cv, &g_state.lock, &abs);
        if (result != 0 && result != ETIMEDOUT) {
            fprintf(stderr, "ERROR: pthread_cond_timedwait failed: %s\n",
                    strerror(result));
            pthread_mutex_unlock(&g_state.lock);
            return -1;
        }
    }
    if (!g_state.simulation_running) {
        pthread_mutex_unlock(&g_state.lock);
        return -1;
    }
    int elapsed = g_state.current_tick - *last_seen;
    *last_seen = g_state.current_tick;
    pthread_mutex_unlock(&g_state.lock);
    return elapsed;
}

static void admit_arrivals(int *next_arrival_tick) {
    pthread_mutex_lock(&g_state.lock);
    int tick = g_state.current_tick;
    pthread_mutex_unlock(&g_state.lock);

    wake_sleepers();
    if (tick < *next_arrival_tick) return;

    pthread_mutex_lock(&g_state.lock);
    int added = g_state.process_count;
    pthread_mutex_unlock(&g_state.lock);
    if (added < g_config.num_students) {
        int batch = (g_config.num_students - added < g_config.arrival_batch)
            ? g_config.num_students - added : g_config.arrival_batch;
        for (int i = 0; i < batch; i++) {
            int demand = g_config.workload == WORKLOAD_INTERACTIVE
                ? g_config.demand_min +
                  rng_uniform(g_config.demand_max - g_config.demand_min + 1)
                : g_config.exam_duration - rng_uniform(10);
            PCB p = {
                .pid = added + i + 1,
                .state = NEW,
                .priority = 1,
                .total_time = g_config.workload == WORKLOAD_INTERACTIVE
                    ? demand : g_config.exam_duration,
                .remaining_time = demand,
                .burst_remaining = g_config.workload == WORKLOAD_INTERACTIVE
                    ? draw_burst(demand) : 0,
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

static int dispatch_one(const char *algorithm, int *last_seen,
                        int *next_arrival_tick) {
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
        return 0;
    }

    PCB *p = &g_state.processes[slot];
    previous_pid = g_state.running_pid;
    if (process_set_state(p, RUNNING) != 0) {
        if (g_config.sched_algo == ROUND_ROBIN) rr_remove(selected_index);
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&rq_lock);
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

    int pid = p->pid;
    int tick_start;
    int work_left;
    int interrupted = 0;
    pthread_mutex_lock(&g_state.lock);
    tick_start = g_state.current_tick;
    work_left = g_config.time_quantum < p->remaining_time
        ? g_config.time_quantum : p->remaining_time;
    if (g_config.workload == WORKLOAD_INTERACTIVE &&
        work_left > p->burst_remaining)
        work_left = p->burst_remaining;
    pthread_mutex_unlock(&g_state.lock);

    while (work_left > 0) {
        int elapsed = wait_for_tick(last_seen);
        if (elapsed < 0) return -1;
        int step = elapsed < work_left ? elapsed : work_left;
        pthread_mutex_lock(&g_state.lock);
        p = &g_state.processes[slot];
        if (p->state != RUNNING) {
            interrupted = 1;
            g_state.idle_ticks += elapsed;
        } else {
            p->remaining_time -= step;
            p->cpu_ticks += step;
            if (g_config.workload == WORKLOAD_INTERACTIVE)
                p->burst_remaining -= step;
            work_left -= step;
            g_state.busy_ticks += step;
            g_state.idle_ticks += elapsed - step;
        }
        update_cpu_utilization();
        pthread_mutex_unlock(&g_state.lock);
        admit_arrivals(next_arrival_tick);
        if (interrupted) break;
    }

    int done = 0;
    int blocked = 0;
    int burst_number = 0;
    int submit_burst = 0;
    int backpressure = 0;
    pthread_mutex_lock(&g_state.lock);
    p = &g_state.processes[slot];
    if (!interrupted && p->state == RUNNING && p->remaining_time <= 0)
        done = 1;
    else if (!interrupted && p->state == RUNNING) {
        if (g_config.workload == WORKLOAD_INTERACTIVE &&
            p->burst_remaining <= 0) {
            process_set_state(p, WAITING);
            p->wake_tick = g_state.current_tick + g_config.think_min +
                rng_uniform(g_config.think_max - g_config.think_min + 1);
            p->bursts_done++;
            burst_number = p->bursts_done;
            g_state.block_events++;
            g_state.running_pid = -1;
            blocked = 1;
            backpressure = g_state.current_tick <
                g_state.backpressure_until_tick;
            if (backpressure)
                g_state.throttled_submissions++;
            else
                submit_burst = 1;
        } else {
            process_set_state(p, READY);
            p->last_ready_tick = g_state.current_tick;
        }
    }
    pthread_mutex_unlock(&g_state.lock);

    if (done) {
        if (g_config.workload == WORKLOAD_INTERACTIVE)
            io_buffer_submit(pid, (pid % 10) + 1, "FINAL", 0);
        if (process_terminate(pid, 0)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "PID %d completed exam (%s)", pid, algorithm);
            log_event("INFO", "SCHEDULER", msg);
        }
    } else if (blocked && submit_burst) {
        io_buffer_submit(pid, (burst_number % 10) + 1, "BURST", 0);
    }

    pthread_mutex_lock(&rq_lock);
    pthread_mutex_lock(&g_state.lock);
    if (g_config.sched_algo == ROUND_ROBIN) {
        if (done || g_state.processes[slot].state == TERMINATED) {
            rr_remove(selected_index);
        } else if (g_state.processes[slot].state == READY) {
            rr_index = (selected_index + 1) % rq_size;
        } else if (g_state.processes[slot].state == WAITING) {
            rr_remove(selected_index);
        }
    } else if (!done && g_state.processes[slot].state == READY) {
        heap_push(slot);
    }
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&rq_lock);

    const char *reason = interrupted ? "TIMED_OUT" : done ? "DONE" : "QUANTUM";
    pthread_mutex_lock(&g_state.lock);
    int tick_end = g_state.current_tick;
    pthread_mutex_unlock(&g_state.lock);
    trace_record(tick_start, tick_end, pid, reason);
    return 1;
}

void *scheduler_thread(void *arg) {
    (void)arg;
    rng_init_thread(1);
    log_event("INFO", "SCHEDULER", "Scheduler thread started");

    trace_count = 0;
    trace_overflows = 0;
    int next_arrival_tick = 0;
    int last_seen;
    int idle_trace_start = -1;
    pthread_mutex_lock(&g_state.lock);
    last_seen = g_state.current_tick;
    pthread_mutex_unlock(&g_state.lock);
    while (1) {
        admit_arrivals(&next_arrival_tick);
        int dispatch_result = dispatch_one(
            g_config.sched_algo == ROUND_ROBIN ? "RR" : "PRIORITY",
            &last_seen, &next_arrival_tick);
        if (dispatch_result < 0)
            break;
        if (dispatch_result == 0) {
            pthread_mutex_lock(&g_state.lock);
            g_state.running_pid = -1;
            pthread_mutex_unlock(&g_state.lock);
            if (idle_trace_start < 0) {
                pthread_mutex_lock(&g_state.lock);
                idle_trace_start = g_state.current_tick;
                pthread_mutex_unlock(&g_state.lock);
            }
            int elapsed = wait_for_tick(&last_seen);
            if (elapsed < 0) break;
            pthread_mutex_lock(&g_state.lock);
            g_state.idle_ticks += elapsed;
            update_cpu_utilization();
            pthread_mutex_unlock(&g_state.lock);
            admit_arrivals(&next_arrival_tick);
            continue;
        }
        if (idle_trace_start >= 0) {
            pthread_mutex_lock(&g_state.lock);
            int idle_trace_end = g_state.current_tick;
            pthread_mutex_unlock(&g_state.lock);
            trace_record(idle_trace_start, idle_trace_end, 0, "IDLE");
            idle_trace_start = -1;
        }
    }

    if (idle_trace_start >= 0) {
        pthread_mutex_lock(&g_state.lock);
        int idle_trace_end = g_state.current_tick;
        pthread_mutex_unlock(&g_state.lock);
        trace_record(idle_trace_start, idle_trace_end, 0, "IDLE");
    }
    trace_write();
    log_event("INFO", "SCHEDULER", "Scheduler thread exiting");
    return NULL;
}
