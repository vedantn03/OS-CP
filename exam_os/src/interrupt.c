#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "interrupt.h"
#include "logger.h"
#include "scheduler.h"
#include "memory.h"
#include "io_buffer.h"
#include "clock.h"
#include "process.h"

// ─── Interrupt Vector Table ───────────────────────────────
static IVTEntry ivt[MAX_INTERRUPTS];
static int      ivt_size = 0;
static pthread_mutex_t ivt_lock = PTHREAD_MUTEX_INITIALIZER;

// ─── Interrupt queue (raised but not yet handled) ─────────
typedef struct {
    int interrupt_id;
    int pid;
    long timestamp;
} PendingInterrupt;

static PendingInterrupt int_queue[64];
static int int_q_head = 0, int_q_tail = 0, int_q_count = 0;
static pthread_mutex_t int_q_lock = PTHREAD_MUTEX_INITIALIZER;
static sem_t           int_ready;

// ─── Timestamp ────────────────────────────────────────────
// ════════════════════════════════════════════════════════
//  INTERRUPT HANDLERS
// ════════════════════════════════════════════════════════

// Handler 0: Exam timeout — save partial, terminate process
static void handle_exam_timeout(int pid, SystemState *state) {
    if (!process_terminate(pid, 1)) {
        char ignored[96];
        snprintf(ignored, sizeof(ignored),
                 "Timeout ignored: PID %d already terminated", pid);
        log_event("INFO", "INTERRUPT", ignored);
        return;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "TIMEOUT: PID %d exam expired — saving partial submission", pid);
    log_event("WARN", "INTERRUPT", msg);

    // Save whatever the student had to I/O buffer as partial
    char answer[64];
    snprintf(answer, sizeof(answer), "PARTIAL_PID%d", pid);
    io_buffer_submit(pid, 0, answer, 1);

    pthread_mutex_lock(&state->lock);
    state->timeouts_fired++;
    pthread_mutex_unlock(&state->lock);
}

// Handler 1: System overload — pause new logins
static void handle_overload(int pid, SystemState *state) {
    (void)pid;
    pthread_mutex_lock(&state->lock);
    state->overload_signals++;
    state->backpressure_until_tick = state->current_tick + BACKPRESSURE_TICKS;
    pthread_mutex_unlock(&state->lock);

    log_event("WARN", "INTERRUPT", "OVERLOAD: Buffer critical — pausing new submissions");
}

// Handler 2: Page fault notification
static void handle_page_fault(int pid, SystemState *state) {
    (void)state;
    char msg[64];
    snprintf(msg, sizeof(msg), "PAGE FAULT raised for PID %d", pid);
    log_event("INFO", "INTERRUPT", msg);
    // Actual handling done in memory.c — this just logs it centrally
}

// Handler 3: Submission complete
static void handle_submit_complete(int pid, SystemState *state) {
    (void)state;
    char msg[64];
    snprintf(msg, sizeof(msg), "Submission complete for PID %d", pid);
    log_event("INFO", "INTERRUPT", msg);
}

// ─── Register handler in IVT ──────────────────────────────
static void ivt_register(int id, const char *name, handler_fn handler) {
    pthread_mutex_lock(&ivt_lock);
    if (ivt_size < MAX_INTERRUPTS) {
        ivt[ivt_size].interrupt_id = id;
        strncpy(ivt[ivt_size].name, name, sizeof(ivt[ivt_size].name) - 1);
        ivt[ivt_size].handler = handler;
        ivt_size++;
    }
    pthread_mutex_unlock(&ivt_lock);
}

// ─── Init: register all handlers ─────────────────────────
void interrupt_init() {
    sem_init(&int_ready, 0, 0);

    ivt_register(INT_EXAM_TIMEOUT,    "EXAM_TIMEOUT",    handle_exam_timeout);
    ivt_register(INT_OVERLOAD,        "OVERLOAD",        handle_overload);
    ivt_register(INT_PAGE_FAULT,      "PAGE_FAULT",      handle_page_fault);
    ivt_register(INT_SUBMIT_COMPLETE, "SUBMIT_COMPLETE", handle_submit_complete);

    log_event("INFO", "INTERRUPT", "Interrupt vector table initialized (4 handlers)");
}

// ─── Raise an interrupt (thread-safe, non-blocking) ──────
void interrupt_raise(int interrupt_id, int pid) {
    pthread_mutex_lock(&int_q_lock);

    if (int_q_count < 64) {
        PendingInterrupt *pi = &int_queue[int_q_tail];
        pi->interrupt_id     = interrupt_id;
        pi->pid              = pid;
        pi->timestamp        = (long)clock_now_ms();

        int_q_tail  = (int_q_tail + 1) % 64;
        int_q_count++;
        sem_post(&int_ready);
    } else {
        pthread_mutex_lock(&g_state.lock);
        g_state.dropped_interrupts++;
        pthread_mutex_unlock(&g_state.lock);
    }

    pthread_mutex_unlock(&int_q_lock);
}

// ─── Dispatch: look up IVT and call handler ───────────────
static void dispatch(PendingInterrupt *pi) {
    pthread_mutex_lock(&ivt_lock);

    for (int i = 0; i < ivt_size; i++) {
        if (ivt[i].interrupt_id == pi->interrupt_id) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Dispatching INT_%d (%s) for PID %d at %ldms",
                     pi->interrupt_id, ivt[i].name, pi->pid, pi->timestamp);
            log_event("INFO", "INTERRUPT", msg);

            pthread_mutex_unlock(&ivt_lock);
            ivt[i].handler(pi->pid, &g_state);
            return;
        }
    }

    pthread_mutex_unlock(&ivt_lock);
    log_event("WARN", "INTERRUPT", "Unknown interrupt ID received");
}

// ─── Check for overload condition ─────────────────────────
static void check_overload() {
    pthread_mutex_lock(&g_io_buffer.lock);
    float fill = (float)g_io_buffer.count / g_io_buffer.capacity;
    pthread_mutex_unlock(&g_io_buffer.lock);

    pthread_mutex_lock(&g_state.lock);
    int tick = g_state.current_tick;
    int backpressure_until = g_state.backpressure_until_tick;
    pthread_mutex_unlock(&g_state.lock);

    if (fill >= 0.95f && tick >= backpressure_until) {
        interrupt_raise(INT_OVERLOAD, -1);
    }
}

// ─── Check for process timeouts ───────────────────────────
static void check_timeouts() {
    int expired[MAX_STUDENTS];
    int expired_count = 0;

    pthread_mutex_lock(&g_state.lock);

    for (int i = 0; i < g_state.process_count; i++) {
        PCB *p = &g_state.processes[i];
        if ((p->state == READY || p->state == RUNNING || p->state == WAITING) &&
            g_state.current_tick >= p->deadline_tick &&
            p->timeout_pending == 0) {
            p->timeout_pending = 1;
            expired[expired_count++] = p->pid;
        }
    }

    pthread_mutex_unlock(&g_state.lock);

    for (int i = 0; i < expired_count; i++)
        interrupt_raise(INT_EXAM_TIMEOUT, expired[i]);
}

// ─── Interrupt thread: monitors system + dispatches ───────
void *interrupt_thread(void *arg) {
    (void)arg;
    log_event("INFO", "INTERRUPT", "Interrupt handler thread started");

    int tick_counter = 0;

    while (1) {
        pthread_mutex_lock(&g_state.lock);
        int running = g_state.simulation_running;
        pthread_mutex_unlock(&g_state.lock);

        if (!running) break;

        // Check system conditions every tick
        check_timeouts();
        check_overload();

        // Dispatch any pending interrupts
        while (1) {
            if (sem_trywait(&int_ready) != 0) break;

            pthread_mutex_lock(&int_q_lock);
            if (int_q_count == 0) {
                pthread_mutex_unlock(&int_q_lock);
                break;
            }
            PendingInterrupt pi = int_queue[int_q_head];
            int_q_head  = (int_q_head + 1) % 64;
            int_q_count--;
            pthread_mutex_unlock(&int_q_lock);

            dispatch(&pi);
        }

        tick_counter++;
        sim_sleep_ticks(1);
    }

    log_event("INFO", "INTERRUPT", "Interrupt thread exiting");
    return NULL;
}