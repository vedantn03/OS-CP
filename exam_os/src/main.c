#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include "shared.h"
#include "config.h"
#include "logger.h"
#include "scheduler.h"
#include "memory.h"
#include "io_buffer.h"
#include "interrupt.h"
#include "dashboard.h"
#include "clock.h"
#include "rng.h"
#include "invariants.h"

// ─── Global instances ─────────────────────────────────────
SystemState g_state;
IOBuffer    g_io_buffer;
Config      g_config;
static volatile sig_atomic_t shutdown_signal = 0;

static void handle_shutdown_signal(int signal_number) {
    (void)signal_number;
    shutdown_signal = 1;
}

// ─── Init global state ────────────────────────────────────
static int state_init() {
    memset(&g_state, 0, sizeof(SystemState));
    g_state.running_pid        = -1;
    g_state.simulation_running =  1;
    g_state.current_tick       =  0;
    for (int i = 0; i < 3; i++)
        strncpy(g_state.recent_logs[i], "--- no events yet ---", 255);
    int result = pthread_mutex_init(&g_state.lock, NULL);
    if (result != 0) {
        fprintf(stderr, "ERROR: pthread_mutex_init failed: %s\n", strerror(result));
        return 0;
    }

    pthread_condattr_t cond_attr;
    result = pthread_condattr_init(&cond_attr);
    if (result != 0) {
        fprintf(stderr, "ERROR: pthread_condattr_init failed: %s\n", strerror(result));
        pthread_mutex_destroy(&g_state.lock);
        return 0;
    }
    result = pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    if (result != 0) {
        fprintf(stderr, "ERROR: pthread_condattr_setclock failed: %s\n", strerror(result));
        pthread_condattr_destroy(&cond_attr);
        pthread_mutex_destroy(&g_state.lock);
        return 0;
    }
    result = pthread_cond_init(&g_state.tick_cv, &cond_attr);
    if (result != 0)
        fprintf(stderr, "ERROR: pthread_cond_init failed: %s\n", strerror(result));
    pthread_condattr_destroy(&cond_attr);
    if (result != 0) {
        pthread_mutex_destroy(&g_state.lock);
        return 0;
    }
    return 1;
}

// ─── Simulation tick thread ───────────────────────────────
// Central clock — increments tick every TIME_TICK_MS
static void *tick_thread(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&g_state.lock);
        int running = g_state.simulation_running;
        if (running) g_state.current_tick++;
        pthread_mutex_unlock(&g_state.lock);
        int result = pthread_cond_broadcast(&g_state.tick_cv);
        if (result != 0)
            fprintf(stderr, "ERROR: pthread_cond_broadcast failed: %s\n", strerror(result));

        if (!running) break;
        sim_sleep_ticks(1);
    }
    return NULL;
}

// ─── Print startup banner ─────────────────────────────────
static void print_banner() {
    printf("\n");
    printf("  ╔═══════════════════════════════════════════╗\n");
    printf("  ║      EXAM OS SIMULATION  v1.0             ║\n");
    printf("  ║  CPU Scheduling | Paging | I/O | Signals  ║\n");
    printf("  ╚═══════════════════════════════════════════╝\n\n");
}

int main(int argc, char *argv[]) {
    print_banner();

    // ─── Load config ──────────────────────────────────────
    config_load_defaults(&g_config);
    config_parse_file(&g_config, "config.conf");
    if (!config_parse_args(&g_config, argc, argv)) return EXIT_FAILURE;
    char config_error[128];
    if (!config_validate(&g_config, config_error, sizeof(config_error))) {
        fprintf(stderr, "ERROR: Invalid configuration: %s\n", config_error);
        return EXIT_FAILURE;
    }
    if (g_config.random_seed == 0) {
        g_config.random_seed = (unsigned int)time(NULL);
        printf("  Random seed: %u\n", g_config.random_seed);
    }
    rng_init_thread(0);
    signal(SIGINT, handle_shutdown_signal);
    signal(SIGTERM, handle_shutdown_signal);
    config_print(&g_config);

    if (g_config.demo_mode)
        printf("\n  [DEMO MODE] Submission storm at tick 30\n");

    if (!g_config.headless_mode) {
        printf("\n  Starting simulation in 2 seconds...\n\n");
        sleep(2);
    }

    // ─── Init all subsystems ──────────────────────────────
    if (!state_init()) return EXIT_FAILURE;
    logger_init();
    scheduler_init();
    memory_init();
    io_buffer_init();
    interrupt_init();
    if (!g_config.headless_mode) dashboard_init();

    // ─── Spawn all threads ────────────────────────────────
    pthread_t t_tick, t_logger, t_scheduler,
              t_memory, t_io, t_interrupt, t_dashboard;

    pthread_create(&t_tick,      NULL, tick_thread,       NULL);
    pthread_create(&t_logger,    NULL, logger_thread,     NULL);
    pthread_create(&t_scheduler, NULL, scheduler_thread,  NULL);
    pthread_create(&t_memory,    NULL, memory_thread,     NULL);
    pthread_create(&t_io,        NULL, io_buffer_thread,  NULL);
    pthread_create(&t_interrupt, NULL, interrupt_thread,  NULL);
    if (!g_config.headless_mode)
        pthread_create(&t_dashboard, NULL, dashboard_thread, NULL);

    // ─── Run until exam_duration ticks or 'q' pressed ────
    while (1) {
        pthread_mutex_lock(&g_state.lock);
        int tick    = g_state.current_tick;
        int running = g_state.simulation_running;
        int done    = g_state.completed_processes;
        if (shutdown_signal) g_state.shutdown_requested = 1;
        if (shutdown_signal) g_state.simulation_running = 0;
        pthread_mutex_unlock(&g_state.lock);

        // End conditions
        if (!running) break;
        if (tick >= g_config.exam_duration) {
            pthread_mutex_lock(&g_state.lock);
            g_state.simulation_running = 0;
            pthread_mutex_unlock(&g_state.lock);
            break;
        }
        if (done >= g_config.num_students) {
            pthread_mutex_lock(&g_state.lock);
            g_state.simulation_running = 0;
            pthread_mutex_unlock(&g_state.lock);
            break;
        }

        sim_sleep_ticks(1);
    }

    // ─── Shutdown sequence ────────────────────────────────
    // Signal all threads to stop
    pthread_mutex_lock(&g_state.lock);
    g_state.simulation_running = 0;
    pthread_mutex_unlock(&g_state.lock);

    io_buffer_shutdown();
    logger_shutdown();
    if (!g_config.headless_mode) dashboard_shutdown();

    // Wait for all threads
    if (!g_config.headless_mode) pthread_join(t_dashboard, NULL);
    pthread_join(t_interrupt, NULL);
    pthread_join(t_io,        NULL);
    pthread_join(t_memory,    NULL);
    pthread_join(t_scheduler, NULL);
    pthread_join(t_logger,    NULL);
    pthread_join(t_tick,      NULL);

    // ─── Write final report ───────────────────────────────
    memory_write_trace();
    printf("\n  Simulation complete. Writing report...\n");
    logger_write_report();
    int invariant_failures = invariants_run();
    if (invariant_failures > 0)
        fprintf(stderr, "INVARIANT VIOLATIONS: %d\n", invariant_failures);

    printf("\n  Output files:\n");
    printf("    output/system_log.txt   — full event log\n");
    printf("    output/submissions.txt  — all submissions\n");
    printf("    output/summary.txt      — final statistics\n\n");

    // ─── Cleanup ──────────────────────────────────────────
    pthread_cond_destroy(&g_state.tick_cv);
    pthread_mutex_destroy(&g_state.lock);
    pthread_mutex_destroy(&g_io_buffer.lock);
    sem_destroy(&g_io_buffer.empty_slots);
    sem_destroy(&g_io_buffer.filled_slots);
    free(g_io_buffer.buffer);

    return (g_config.strict && invariant_failures > 0) ? 3 : 0;
}