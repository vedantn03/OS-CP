#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "logger.h"
#include "clock.h"
#include "config.h"

// ─── Internal log queue ──────────────────────────────────
static LogEntry   log_queue[MAX_LOG_QUEUE];
static int        q_head = 0, q_tail = 0, q_count = 0;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static sem_t           q_ready;
static FILE           *log_file = NULL;
static int             logger_running = 1;
static int             logger_io_warned = 0;

static void warn_logger_io_failure(void) {
    if (!logger_io_warned) {
        fprintf(stderr, "WARNING: Logger output failed\n");
        logger_io_warned = 1;
    }
}

// ─── Get nanosecond timestamp ────────────────────────────
void logger_init() {
    sem_init(&q_ready, 0, 0);
    log_file = fopen("output/system_log.txt", "w");
    if (!log_file) {
        fprintf(stderr, "WARNING: Could not open log file. Logging to stderr.\n");
        log_file = stderr;
    }
    if (fprintf(log_file, "=== EXAM OS SIMULATION LOG ===\n\n") < 0 ||
        fflush(log_file) != 0)
        warn_logger_io_failure();
}

void logger_shutdown() {
    pthread_mutex_lock(&q_lock);
    logger_running = 0;
    pthread_mutex_unlock(&q_lock);
    sem_post(&q_ready);  // wake thread so it can exit
}

// Called by any module — thread safe
void log_event(const char *level, const char *subsystem, const char *message) {
    pthread_mutex_lock(&q_lock);

    if (q_count < MAX_LOG_QUEUE) {
        LogEntry *e = &log_queue[q_tail];
        e->timestamp_ns = clock_now_ns();
        strncpy(e->level,     level,     sizeof(e->level)     - 1);
        strncpy(e->subsystem, subsystem, sizeof(e->subsystem) - 1);
        strncpy(e->message,   message,   sizeof(e->message)   - 1);

        q_tail = (q_tail + 1) % MAX_LOG_QUEUE;
        q_count++;
        sem_post(&q_ready);
    } else {
        pthread_mutex_lock(&g_state.lock);
        g_state.dropped_logs++;
        pthread_mutex_unlock(&g_state.lock);
    }
    // if queue full, silently drop (never block the caller)

    pthread_mutex_unlock(&q_lock);

    // Also update dashboard recent logs
    pthread_mutex_lock(&g_state.lock);
    int idx = g_state.log_index % 3;
    snprintf(g_state.recent_logs[idx], 255, "[%-9s] %-11s %s", level, subsystem, message);
    g_state.log_index++;
    pthread_mutex_unlock(&g_state.lock);
}

// Runs in its own thread — drains queue and writes to file
void *logger_thread(void *arg) {
    (void)arg;

    while (1) {
        sem_wait(&q_ready);
        while (1) {
            pthread_mutex_lock(&q_lock);
            if (q_count == 0) {
                int done = !logger_running;
                pthread_mutex_unlock(&q_lock);
                if (done) goto logger_done;
                break;
            }
            LogEntry e = log_queue[q_head];
            q_head = (q_head + 1) % MAX_LOG_QUEUE;
            q_count--;
            pthread_mutex_unlock(&q_lock);

            long ms = e.timestamp_ns / 1000000;
            if (fprintf(log_file, "[%8ld ms] [%-5s] [%-10s] %s\n",
                        ms, e.level, e.subsystem, e.message) < 0 ||
                fflush(log_file) != 0)
                warn_logger_io_failure();
        }
    }

logger_done:
    if (log_file && log_file != stderr) fclose(log_file);
    return NULL;
}

// Called at simulation end
void logger_write_report() {
    FILE *f = fopen("output/summary.txt", "w");
    if (!f) return;

    pthread_mutex_lock(&g_state.lock);

    int total = g_state.page_faults + g_state.page_hits;
    float hit_rate = total > 0
        ? (float)g_state.page_hits / total * 100.0f
        : 0.0f;
    double waiting = 0.0;
    double turnaround = 0.0;
    double response = 0.0;
    int terminated = 0;
    int responded = 0;
    for (int i = 0; i < g_state.process_count; i++) {
        PCB *p = &g_state.processes[i];
        if (p->state == TERMINATED) {
            waiting += p->waiting_time;
            turnaround += p->turnaround_time;
            terminated++;
            if (p->first_run_tick >= 0) {
                response += p->first_run_tick - p->arrival_tick;
                responded++;
            }
        }
    }

    fprintf(f, "╔══════════════════════════════════════════╗\n");
    fprintf(f, "║       EXAM OS SIMULATION REPORT          ║\n");
    fprintf(f, "╠══════════════════════════════════════════╣\n");
    fprintf(f, "║ CPU                                      ║\n");
    fprintf(f, "║   Workload           : %-18s ║\n",
            g_config.workload == WORKLOAD_INTERACTIVE ? "INTERACTIVE" : "LEGACY");
    fprintf(f, "║   Scheduler          : %-18s ║\n", sched_algo_name(g_config.sched_algo));
    fprintf(f, "║   Context Switches  : %-18d ║\n", g_state.context_switches);
    fprintf(f, "║   Dispatches        : %-18d ║\n", g_state.dispatches);
    fprintf(f, "║   Busy Ticks        : %-18d ║\n", g_state.busy_ticks);
    fprintf(f, "║   Idle Ticks        : %-18d ║\n", g_state.idle_ticks);
    fprintf(f, "║   Overhead Ticks    : %-18d ║\n", g_state.overhead_ticks);
    fprintf(f, "║   Preemptions       : %-18d ║\n", g_state.preemptions);
    fprintf(f, "║   MLFQ Demotions    : %-18d ║\n",
            g_config.sched_algo == MLFQ ? g_state.demotions : 0);
    fprintf(f, "║   MLFQ Boosts       : %-18d ║\n",
            g_config.sched_algo == MLFQ ? g_state.boosts : 0);
    fprintf(f, "║   CPU Utilization   : %-17.1f%% ║\n", g_state.cpu_utilization);
    fprintf(f, "║   Throughput (per 100 ticks): %-8.1f ║\n",
            g_state.current_tick > 0
                ? (double)g_state.finished_ok * 100.0 / g_state.current_tick : 0.0);
    fprintf(f, "║   Unfinished At End : %-18d ║\n",
            g_state.process_count - g_state.completed_processes);
    fprintf(f, "║   Blocked Waits     : %-18d ║\n", g_state.block_events);
    fprintf(f, "║   Wakeups           : %-18d ║\n", g_state.wakeups);
    fprintf(f, "║   Avg Waiting Time  : %-18.1f ║\n",
            terminated > 0 ? waiting / terminated : 0.0);
    fprintf(f, "║   Avg Turnaround    : %-18.1f ║\n",
            terminated > 0 ? turnaround / terminated : 0.0);
    fprintf(f, "║   Avg Response      : %-18.1f ║\n",
            responded > 0 ? response / responded : 0.0);
    fprintf(f, "║   Completed Exams   : %-18d ║\n", g_state.finished_ok);
    fprintf(f, "║   Timeouts Fired    : %-18d ║\n", g_state.timeouts_fired);
    fprintf(f, "║   Dropped Log Records: %-17d ║\n", g_state.dropped_logs);
    fprintf(f, "╠══════════════════════════════════════════╣\n");
    fprintf(f, "║ MEMORY                                   ║\n");
    fprintf(f, "║   Page Replacement  : %-18s ║\n", page_algo_name(g_config.page_algo));
    fprintf(f, "║   Page Faults       : %-18d ║\n", g_state.page_faults);
    fprintf(f, "║   Page Hits         : %-18d ║\n", g_state.page_hits);
    fprintf(f, "║   Evictions         : %-18d ║\n", g_state.evictions);
    fprintf(f, "║   Dirty Writebacks  : %-18d ║\n", g_state.dirty_writebacks);
    fprintf(f, "║   Page Fault Rate   : %-17.1f%% ║\n",
            total > 0 ? (double)g_state.page_faults * 100.0 / total : 0.0);
    fprintf(f, "║   Thrash Episodes   : %-18d ║\n", g_state.thrash_episodes);
    fprintf(f, "║   Deferred Arrival Ticks: %-13d ║\n",
            g_state.deferred_arrival_ticks);
    fprintf(f, "║   Hit Rate          : %-17.1f%% ║\n", hit_rate);
    fprintf(f, "╠══════════════════════════════════════════╣\n");
    fprintf(f, "║ I/O BUFFER                               ║\n");
    fprintf(f, "║   Total Submissions : %-18d ║\n", g_state.total_submissions);
    fprintf(f, "║   Dropped           : %-18d ║\n", g_state.dropped_submissions);
    fprintf(f, "║   Flush Count       : %-18d ║\n", g_state.flush_count);
    fprintf(f, "║   Throttled Submissions: %-14d ║\n", g_state.throttled_submissions);
    fprintf(f, "╠══════════════════════════════════════════╣\n");
    fprintf(f, "║ INTERRUPTS                               ║\n");
    fprintf(f, "║   Overload Signals  : %-18d ║\n", g_state.overload_signals);
    fprintf(f, "║   Dropped Interrupts: %-18d ║\n", g_state.dropped_interrupts);
    fprintf(f, "╚══════════════════════════════════════════╝\n");

    pthread_mutex_unlock(&g_state.lock);
    fclose(f);

    // Print to terminal too
    printf("\n");
    FILE *summary = fopen("output/summary.txt", "r");
    if (summary) {
        char line[256];
        while (fgets(line, sizeof(line), summary)) fputs(line, stdout);
        fclose(summary);
    }
}