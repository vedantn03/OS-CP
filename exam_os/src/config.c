#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "config.h"

void config_load_defaults(Config *cfg) {
    cfg->num_students    = 50;
    cfg->memory_frames   = 64;
    cfg->page_size       = 4;
    cfg->time_quantum    = 5;
    cfg->exam_duration   = 100;
    cfg->sched_algo      = PRIORITY;
    cfg->page_algo       = LRU;
    cfg->buffer_capacity = DEFAULT_BUFFER_CAPACITY;
    cfg->demo_mode       = 0;
    cfg->headless_mode   = 0;
    cfg->random_seed     = 0;
    cfg->session_limit   = 0;
    cfg->arrival_batch   = 5;
    cfg->arrival_interval = 10;
    cfg->strict          = 0;
    cfg->tick_ms         = TIME_TICK_MS;
    cfg->trace            = 0;
    cfg->workload        = WORKLOAD_LEGACY;
}

int config_parse_file(Config *cfg, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;  // no config file is fine, defaults are used

    char line[256], key[64], val[64];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " %63[^ =\t] %*[= \t] %63s", key, val) != 2)
            continue;

        char *end = NULL;
        long number = strtol(val, &end, 10);
        int numeric = end != val && *end == '\0' && number >= INT_MIN && number <= INT_MAX;
        if      (strcmp(key, "NUM_STUDENTS")     == 0 && numeric) cfg->num_students    = (int)number;
        else if (strcmp(key, "MEMORY_FRAMES")    == 0 && numeric) cfg->memory_frames   = (int)number;
        else if (strcmp(key, "PAGE_SIZE")        == 0 && numeric) cfg->page_size       = (int)number;
        else if (strcmp(key, "TIME_QUANTUM")     == 0 && numeric) cfg->time_quantum    = (int)number;
        else if (strcmp(key, "EXAM_DURATION")    == 0 && numeric) cfg->exam_duration   = (int)number;
        else if (strcmp(key, "BUFFER_CAPACITY")  == 0 && numeric) cfg->buffer_capacity = (int)number;
        else if (strcmp(key, "SESSION_LIMIT")    == 0 && numeric) cfg->session_limit   = (int)number;
        else if (strcmp(key, "ARRIVAL_BATCH")    == 0 && numeric) cfg->arrival_batch   = (int)number;
        else if (strcmp(key, "ARRIVAL_INTERVAL") == 0 && numeric) cfg->arrival_interval = (int)number;
        else if (strcmp(key, "TICK_MS")          == 0 && numeric) cfg->tick_ms = (int)number;
        else if (strcmp(key, "WORKLOAD")         == 0)
            cfg->workload = (strcmp(val, "INTERACTIVE") == 0)
                ? WORKLOAD_INTERACTIVE : WORKLOAD_LEGACY;
        else if (strcmp(key, "SCHEDULING_ALGO")  == 0)
            cfg->sched_algo = (strcmp(val, "ROUND_ROBIN") == 0) ? ROUND_ROBIN : PRIORITY;
        else if (strcmp(key, "PAGE_REPLACE")     == 0)
            cfg->page_algo  = (strcmp(val, "FIFO") == 0) ? FIFO : LRU;
    }

    fclose(f);
    return 1;
}

int config_parse_args(Config *cfg, int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--students") == 0 && i+1 < argc) cfg->num_students  = atoi(argv[++i]);
        else if (strcmp(argv[i], "--frames")   == 0 && i+1 < argc) cfg->memory_frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--quantum")  == 0 && i+1 < argc) cfg->time_quantum  = atoi(argv[++i]);
        else if (strcmp(argv[i], "--duration") == 0 && i+1 < argc) cfg->exam_duration = atoi(argv[++i]);
        else if (strcmp(argv[i], "--buffer")  == 0 && i+1 < argc) cfg->buffer_capacity = atoi(argv[++i]);
        else if (strcmp(argv[i], "--limit")   == 0 && i+1 < argc) cfg->session_limit   = atoi(argv[++i]);
        else if (strcmp(argv[i], "--arrival-batch") == 0 && i+1 < argc) cfg->arrival_batch = atoi(argv[++i]);
        else if (strcmp(argv[i], "--arrival-interval") == 0 && i+1 < argc) cfg->arrival_interval = atoi(argv[++i]);
        else if (strcmp(argv[i], "--algo")     == 0 && i+1 < argc)
            cfg->sched_algo = (strcmp(argv[++i], "RR") == 0) ? ROUND_ROBIN : PRIORITY;
        else if (strcmp(argv[i], "--page")     == 0 && i+1 < argc)
            cfg->page_algo  = (strcmp(argv[++i], "FIFO") == 0) ? FIFO : LRU;
        else if (strcmp(argv[i], "--demo")     == 0) cfg->demo_mode = 1;
        else if (strcmp(argv[i], "--headless") == 0) cfg->headless_mode = 1;
        else if (strcmp(argv[i], "--strict")   == 0) cfg->strict = 1;
        else if (strcmp(argv[i], "--tick-ms") == 0 && i+1 < argc) cfg->tick_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--trace")    == 0) cfg->trace = 1;
        else if (strcmp(argv[i], "--workload") == 0 && i+1 < argc)
            cfg->workload = (strcmp(argv[++i], "INTERACTIVE") == 0)
                ? WORKLOAD_INTERACTIVE : WORKLOAD_LEGACY;
        else if (strcmp(argv[i], "--seed")    == 0 && i+1 < argc) cfg->random_seed = (unsigned int)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--help")   == 0) {
            printf("Usage: ./exam_os [options]\n");
            printf("  --students N   Number of students\n");
            printf("  --frames N     Number of memory frames\n");
            printf("  --quantum N    Scheduler quantum\n");
            printf("  --duration N   Simulation duration in ticks\n");
            printf("  --limit N      Per-session time limit in ticks (0 uses initial remaining time)\n");
            printf("  --arrival-batch N    Processes added per arrival batch\n");
            printf("  --arrival-interval N Ticks between arrival batches\n");
            printf("  --buffer N     I/O buffer capacity\n");
            printf("  --algo RR|PRIORITY  Scheduling algorithm\n");
            printf("  --page FIFO|LRU     Page replacement algorithm\n");
            printf("  --headless      Disable dashboard\n");
            printf("  --demo          Enable demo mode\n");
            printf("  --seed N        Random seed\n");
            printf("  --strict        Exit with status 3 if invariants fail\n");
            printf("  --tick-ms N     Tick duration in milliseconds\n");
            printf("  --trace         Write scheduler trace to output/trace.csv\n");
            printf("  --workload LEGACY|INTERACTIVE  Workload behavior\n");
            exit(EXIT_SUCCESS);
        }
        else {
            fprintf(stderr, "ERROR: Unknown or incomplete argument: %s\n", argv[i]);
            return 0;
        }
    }
    return 1;
}

int config_validate(const Config *cfg, char *error, size_t error_size) {
    if (cfg->num_students < 1 || cfg->num_students > MAX_STUDENTS)
        snprintf(error, error_size, "NUM_STUDENTS must be 1..%d", MAX_STUDENTS);
    else if (cfg->memory_frames < 1 || cfg->memory_frames > MAX_FRAMES)
        snprintf(error, error_size, "MEMORY_FRAMES must be 1..%d", MAX_FRAMES);
    else if (cfg->page_size < 1 || cfg->page_size > MAX_PAGES)
        snprintf(error, error_size, "PAGE_SIZE must be 1..%d", MAX_PAGES);
    else if (cfg->time_quantum < 1)
        snprintf(error, error_size, "TIME_QUANTUM must be positive");
    else if (cfg->exam_duration < 1)
        snprintf(error, error_size, "EXAM_DURATION must be positive");
    else if (cfg->buffer_capacity < 1 || cfg->buffer_capacity > MAX_BUFFER_CAPACITY)
        snprintf(error, error_size, "BUFFER_CAPACITY must be 1..%d", MAX_BUFFER_CAPACITY);
    else if (cfg->session_limit < 0 || cfg->session_limit > 100000)
        snprintf(error, error_size, "SESSION_LIMIT must be 0..100000");
    else if (cfg->arrival_batch < 1)
        snprintf(error, error_size, "ARRIVAL_BATCH must be at least 1");
    else if (cfg->arrival_interval < 1)
        snprintf(error, error_size, "ARRIVAL_INTERVAL must be at least 1");
    else if (cfg->tick_ms < 1 || cfg->tick_ms > 60000)
        snprintf(error, error_size, "TICK_MS must be 1..60000");
    else
        return 1;
    return 0;
}

void config_print(Config *cfg) {
    printf("┌─── Configuration ───────────────────────┐\n");
    printf("│ Students     : %-26d │\n", cfg->num_students);
    printf("│ Memory Frames: %-26d │\n", cfg->memory_frames);
    printf("│ Time Quantum : %-26d │\n", cfg->time_quantum);
    printf("│ Exam Duration: %-26d │\n", cfg->exam_duration);
    printf("│ Session Limit: %-26d │\n", cfg->session_limit);
    printf("│ Arrival Batch: %-26d │\n", cfg->arrival_batch);
    printf("│ Arrival Every: %-26d │\n", cfg->arrival_interval);
    printf("│ Tick (ms)    : %-26d │\n", cfg->tick_ms);
    printf("│ Trace        : %-26s │\n", cfg->trace ? "ON" : "OFF");
    printf("│ Workload     : %-26s │\n",
           cfg->workload == WORKLOAD_INTERACTIVE ? "INTERACTIVE" : "LEGACY");
    printf("│ Buffer       : %-26d │\n", cfg->buffer_capacity);
    printf("│ Scheduling   : %-26s │\n", cfg->sched_algo == PRIORITY ? "PRIORITY" : "ROUND_ROBIN");
    printf("│ Page Replace : %-26s │\n", cfg->page_algo  == LRU      ? "LRU"      : "FIFO");
    printf("│ Demo Mode    : %-26s │\n", cfg->demo_mode  ? "ON" : "OFF");
    printf("│ Headless     : %-26s │\n", cfg->headless_mode ? "ON" : "OFF");
    printf("└─────────────────────────────────────────┘\n");
}
