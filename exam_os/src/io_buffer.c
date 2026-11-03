#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "io_buffer.h"
#include "logger.h"
#include "clock.h"
#include "rng.h"
#include "process.h"

#define FLUSH_THRESHOLD 0.80  // flush when 80% full

static FILE *disk_file   = NULL;
static int   io_running  = 1;

// ─── Timestamp ────────────────────────────────────────────
// ─── Init ─────────────────────────────────────────────────
void io_buffer_init() {
    g_io_buffer.capacity = g_config.buffer_capacity;
    g_io_buffer.buffer = calloc((size_t)g_io_buffer.capacity, sizeof(*g_io_buffer.buffer));
    if (!g_io_buffer.buffer) {
        fprintf(stderr, "ERROR: Could not allocate I/O buffer (%d entries)\n",
                g_io_buffer.capacity);
        exit(EXIT_FAILURE);
    }
    g_io_buffer.head  = 0;
    g_io_buffer.tail  = 0;
    g_io_buffer.count = 0;

    sem_init(&g_io_buffer.empty_slots, 0, (unsigned int)g_io_buffer.capacity);
    sem_init(&g_io_buffer.filled_slots, 0, 0);
    pthread_mutex_init(&g_io_buffer.lock, NULL);

    disk_file = fopen("output/submissions.txt", "w");
    if (!disk_file) {
        fprintf(stderr, "WARNING: Could not open submissions file\n");
        disk_file = stderr;
    }
    fprintf(disk_file, "=== EXAM SUBMISSIONS ===\n\n");
    fflush(disk_file);

    log_event("INFO", "IO", "I/O buffer initialized");
}

void io_buffer_shutdown() {
    io_running = 0;
}

// ─── Producer: called by exam processes ──────────────────
int io_buffer_submit(int pid, int question_id,
                     const char *answer, int is_partial) {
    // Non-blocking try — if buffer full, drop submission
    int rc = sem_trywait(&g_io_buffer.empty_slots);
    if (rc != 0) {
        pthread_mutex_lock(&g_state.lock);
        g_state.dropped_submissions++;
        pthread_mutex_unlock(&g_state.lock);

        char msg[128];
        snprintf(msg, sizeof(msg),
                 "DROP: PID %d Q%d — buffer full!", pid, question_id);
        log_event("ERROR", "IO", msg);
        return -1;
    }

    pthread_mutex_lock(&g_io_buffer.lock);

    Submission *s   = &g_io_buffer.buffer[g_io_buffer.tail];
    s->pid          = pid;
    s->question_id  = question_id;
    s->timestamp    = (long)clock_now_ms();
    s->is_partial   = is_partial;
    strncpy(s->answer, answer ? answer : "EMPTY", sizeof(s->answer) - 1);

    g_io_buffer.tail  = (g_io_buffer.tail + 1) % g_io_buffer.capacity;
    g_io_buffer.count++;

    // Update shared state for dashboard
    pthread_mutex_lock(&g_state.lock);
    g_state.buffer_count       = g_io_buffer.count;
    g_state.total_submissions++;
    pthread_mutex_unlock(&g_state.lock);

    pthread_mutex_unlock(&g_io_buffer.lock);
    sem_post(&g_io_buffer.filled_slots);

    char msg[128];
    snprintf(msg, sizeof(msg), "PID %d submitted Q%d%s",
             pid, question_id, is_partial ? " (PARTIAL/timeout)" : "");
    log_event("INFO", "IO", msg);

    if (g_config.workload == WORKLOAD_INTERACTIVE && !is_partial) {
        int slot = process_slot(pid);
        if (slot >= 0) {
            pthread_mutex_lock(&g_state.lock);
            PCB *p = &g_state.processes[slot];
            if (p->state == RUNNING &&
                process_set_state(p, WAITING) == 0)
                p->wait_until_tick = g_state.current_tick + 1;
            pthread_mutex_unlock(&g_state.lock);
        }
    }

    return 0;
}

// ─── Flush batch to disk ──────────────────────────────────
static int flush_buffer() {
    pthread_mutex_lock(&g_io_buffer.lock);
    int count = g_io_buffer.count;
    pthread_mutex_unlock(&g_io_buffer.lock);

    if (count == 0) return 0;

    int flushed = 0;
    while (flushed < count) {
        // Wait for an item
        if (sem_trywait(&g_io_buffer.filled_slots) != 0) break;

        pthread_mutex_lock(&g_io_buffer.lock);
        Submission s = g_io_buffer.buffer[g_io_buffer.head];
        g_io_buffer.head  = (g_io_buffer.head + 1) % g_io_buffer.capacity;
        g_io_buffer.count--;

        pthread_mutex_lock(&g_state.lock);
        g_state.buffer_count = g_io_buffer.count;
        pthread_mutex_unlock(&g_state.lock);

        pthread_mutex_unlock(&g_io_buffer.lock);
        sem_post(&g_io_buffer.empty_slots);

        // Write to simulated disk
        if (fprintf(disk_file, "[%ld ms] PID=%-3d Q=%-2d %s ANSWER=%s\n",
                    s.timestamp, s.pid, s.question_id,
                    s.is_partial ? "[PARTIAL]" : "        ",
                    s.answer) >= 0) {
            flushed++;
            pthread_mutex_lock(&g_state.lock);
            g_state.flushed_records++;
            pthread_mutex_unlock(&g_state.lock);
        }
    }

    if (flushed > 0) {
        if (fflush(disk_file) != 0)
            fprintf(stderr, "WARNING: Could not flush submissions file\n");

        pthread_mutex_lock(&g_state.lock);
        g_state.flush_count++;
        pthread_mutex_unlock(&g_state.lock);

        char msg[64];
        snprintf(msg, sizeof(msg), "Flushed %d submissions to disk", flushed);
        log_event("INFO", "IO", msg);
    }

    return flushed;
}

// ─── Demo mode: submission storm ─────────────────────────
static void trigger_submission_storm() {
    pthread_mutex_lock(&g_state.lock);
    int count = g_state.process_count;
    pthread_mutex_unlock(&g_state.lock);

    int storms = count < 30 ? count : 30;
    char msg[128];
    snprintf(msg, sizeof(msg),
             "SUBMISSION STORM triggered — %d simultaneous submissions!", storms);
    log_event("WARN", "IO", msg);
    for (int i = 0; i < storms; i++) {
        char answer[64];
        snprintf(answer, sizeof(answer), "ANS_%d_%d", i, rng_uniform(100));
        io_buffer_submit(i + 1, rng_uniform(10) + 1, answer, 0);
    }
}

// ─── I/O flusher thread ───────────────────────────────────
void *io_buffer_thread(void *arg) {
    (void)arg;
    rng_init_thread(3);
    log_event("INFO", "IO", "I/O buffer thread started");

    int storm_triggered = 0;
    int last_flush_tick = 0;

    while (1) {
        pthread_mutex_lock(&g_state.lock);
        int running = g_state.simulation_running;
        int tick    = g_state.current_tick;
        int count   = g_state.process_count;
        pthread_mutex_unlock(&g_state.lock);

        if (!running) {
            // Final flush before exit
            flush_buffer();
            break;
        }

        // Demo mode: trigger submission storm at tick 30
        if (g_config.demo_mode && tick >= 30 && !storm_triggered && count >= 10) {
            trigger_submission_storm();
            storm_triggered = 1;
        }

        // Simulate random submissions from active processes
        pthread_mutex_lock(&g_state.lock);
        int pid     = g_state.running_pid;
        int prcount = g_state.process_count;
        int backpressure_until = g_state.backpressure_until_tick;
        pthread_mutex_unlock(&g_state.lock);

        if (pid > 0 && prcount > 0 && tick >= backpressure_until) {
            // 30% chance a process submits an answer each tick
            if (rng_uniform(100) < 30) {
                char answer[64];
                snprintf(answer, sizeof(answer), "ANS_%d", rng_uniform(1000));
                io_buffer_submit(pid, rng_uniform(10) + 1, answer, 0);
            }
        } else if (pid > 0 && prcount > 0 && tick < backpressure_until && !g_config.demo_mode) {
            pthread_mutex_lock(&g_state.lock);
            g_state.throttled_submissions++;
            pthread_mutex_unlock(&g_state.lock);
        }

        // Flush if above threshold or every 15 ticks
        pthread_mutex_lock(&g_io_buffer.lock);
        float fill = (float)g_io_buffer.count / g_io_buffer.capacity;
        pthread_mutex_unlock(&g_io_buffer.lock);

        if (fill >= FLUSH_THRESHOLD || tick - last_flush_tick >= 15) {
            flush_buffer();
            last_flush_tick = tick;
        }

        sim_sleep_ticks(1);
    }

    if (disk_file && disk_file != stderr) fclose(disk_file);
    log_event("INFO", "IO", "I/O buffer thread exiting");
    return NULL;
}