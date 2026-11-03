#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include "config.h"

int config_parse_sched_algo(const char *value, SchedAlgo *algo) {
    char normalized[32];
    size_t length = strlen(value);
    if (length >= sizeof(normalized)) return 0;
    for (size_t i = 0; i <= length; i++)
        normalized[i] = (char)toupper((unsigned char)value[i]);
    if (strcmp(normalized, "RR") == 0 || strcmp(normalized, "ROUND_ROBIN") == 0)
        *algo = ROUND_ROBIN;
    else if (strcmp(normalized, "PRIORITY") == 0)
        *algo = PRIORITY;
    else if (strcmp(normalized, "FCFS") == 0)
        *algo = FCFS;
    else if (strcmp(normalized, "SJF") == 0)
        *algo = SJF;
    else if (strcmp(normalized, "SRTF") == 0)
        *algo = SRTF;
    else
        return 0;
    return 1;
}

const char *sched_algo_name(SchedAlgo algo) {
    switch (algo) {
    case ROUND_ROBIN: return "ROUND_ROBIN";
    case PRIORITY: return "PRIORITY";
    case FCFS: return "FCFS";
    case SJF: return "SJF";
    case SRTF: return "SRTF";
    default: return "UNKNOWN";
    }
}

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
    cfg->workload        = WORKLOAD_INTERACTIVE;
    cfg->demand_min      = 10;
    cfg->demand_max      = 25;
    cfg->burst_min       = 2;
    cfg->burst_max       = 6;
    cfg->think_min       = 4;
    cfg->think_max       = 12;
    cfg->switch_cost     = 0;
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
        else if (strcmp(key, "DEMAND_MIN")       == 0 && numeric) cfg->demand_min = (int)number;
        else if (strcmp(key, "DEMAND_MAX")       == 0 && numeric) cfg->demand_max = (int)number;
        else if (strcmp(key, "BURST_MIN")        == 0 && numeric) cfg->burst_min = (int)number;
        else if (strcmp(key, "BURST_MAX")        == 0 && numeric) cfg->burst_max = (int)number;
        else if (strcmp(key, "THINK_MIN")        == 0 && numeric) cfg->think_min = (int)number;
        else if (strcmp(key, "THINK_MAX")        == 0 && numeric) cfg->think_max = (int)number;
        else if (strcmp(key, "SWITCH_COST")     == 0 && numeric) cfg->switch_cost     = (int)number;
        else if (strcmp(key, "WORKLOAD")         == 0) {
            for (char *p = val; *p; p++) *p = (char)toupper((unsigned char)*p);
            if (strcmp(val, "INTERACTIVE") == 0)
                cfg->workload = WORKLOAD_INTERACTIVE;
            else if (strcmp(val, "LEGACY") == 0)
                cfg->workload = WORKLOAD_LEGACY;
            else {
                fprintf(stderr, "ERROR: WORKLOAD must be LEGACY or INTERACTIVE\n");
                return 0;
            }
        }
        else if (strcmp(key, "SCHEDULING_ALGO")  == 0) {
            if (!config_parse_sched_algo(val, &cfg->sched_algo)) {
                fprintf(stderr, "ERROR: SCHEDULING_ALGO must be RR, ROUND_ROBIN, PRIORITY, FCFS, SJF, or SRTF\n");
                fclose(f);
                return 0;
            }
        }
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
        else if (strcmp(argv[i], "--algo")     == 0 && i+1 < argc) {
            if (!config_parse_sched_algo(argv[++i], &cfg->sched_algo)) {
                fprintf(stderr, "ERROR: --algo must be RR, ROUND_ROBIN, PRIORITY, FCFS, SJF, or SRTF\n");
                return 0;
            }
        }
        else if (strcmp(argv[i], "--switch-cost") == 0 && i+1 < argc)
            cfg->switch_cost = atoi(argv[++i]);
        else if (strcmp(argv[i], "--page")     == 0 && i+1 < argc)
            cfg->page_algo  = (strcmp(argv[++i], "FIFO") == 0) ? FIFO : LRU;
        else if (strcmp(argv[i], "--demo")     == 0) cfg->demo_mode = 1;
        else if (strcmp(argv[i], "--headless") == 0) cfg->headless_mode = 1;
        else if (strcmp(argv[i], "--strict")   == 0) cfg->strict = 1;
        else if (strcmp(argv[i], "--tick-ms") == 0 && i+1 < argc) cfg->tick_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "--trace")    == 0) cfg->trace = 1;
        else if (strcmp(argv[i], "--workload") == 0 && i+1 < argc) {
            char workload[32];
            snprintf(workload, sizeof(workload), "%s", argv[++i]);
            for (char *p = workload; *p; p++) *p = (char)toupper((unsigned char)*p);
            if (strcmp(workload, "INTERACTIVE") == 0)
                cfg->workload = WORKLOAD_INTERACTIVE;
            else if (strcmp(workload, "LEGACY") == 0)
                cfg->workload = WORKLOAD_LEGACY;
            else {
                fprintf(stderr, "ERROR: --workload must be LEGACY or INTERACTIVE\n");
                return 0;
            }
        }
        else if (strcmp(argv[i], "--seed")    == 0 && i+1 < argc) cfg->random_seed = (unsigned int)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--demand-min") == 0 && i+1 < argc) cfg->demand_min = atoi(argv[++i]);
        else if (strcmp(argv[i], "--demand-max") == 0 && i+1 < argc) cfg->demand_max = atoi(argv[++i]);
        else if (strcmp(argv[i], "--burst-min") == 0 && i+1 < argc) cfg->burst_min = atoi(argv[++i]);
        else if (strcmp(argv[i], "--burst-max") == 0 && i+1 < argc) cfg->burst_max = atoi(argv[++i]);
        else if (strcmp(argv[i], "--think-min") == 0 && i+1 < argc) cfg->think_min = atoi(argv[++i]);
        else if (strcmp(argv[i], "--think-max") == 0 && i+1 < argc) cfg->think_max = atoi(argv[++i]);
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
            printf("  --algo RR|ROUND_ROBIN|PRIORITY|FCFS|SJF|SRTF  Scheduling algorithm\n");
            printf("  --switch-cost N  Context-switch overhead in ticks (0..50)\n");
            printf("  --page FIFO|LRU     Page replacement algorithm\n");
            printf("  --headless      Disable dashboard\n");
            printf("  --demo          Enable demo mode\n");
            printf("  --seed N        Random seed\n");
            printf("  --strict        Exit with status 3 if invariants fail\n");
            printf("  --tick-ms N     Tick duration in milliseconds\n");
            printf("  --trace         Write scheduler trace to output/trace.csv\n");
            printf("  --workload LEGACY|INTERACTIVE  Workload behavior\n");
            printf("  --demand-min N  Minimum CPU demand per student\n");
            printf("  --demand-max N  Maximum CPU demand per student\n");
            printf("  --burst-min N   Minimum CPU burst length\n");
            printf("  --burst-max N   Maximum CPU burst length\n");
            printf("  --think-min N   Minimum think time\n");
            printf("  --think-max N   Maximum think time\n");
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
    else if (cfg->switch_cost < 0 || cfg->switch_cost > 50)
        snprintf(error, error_size, "SWITCH_COST must be 0..50");
    else if (cfg->demand_min < 1)
        snprintf(error, error_size, "DEMAND_MIN must be at least 1");
    else if (cfg->demand_max < cfg->demand_min)
        snprintf(error, error_size, "DEMAND_MAX must be at least DEMAND_MIN");
    else if (cfg->burst_min < 1)
        snprintf(error, error_size, "BURST_MIN must be at least 1");
    else if (cfg->burst_max < cfg->burst_min)
        snprintf(error, error_size, "BURST_MAX must be at least BURST_MIN");
    else if (cfg->think_min < 1)
        snprintf(error, error_size, "THINK_MIN must be at least 1");
    else if (cfg->think_max < cfg->think_min)
        snprintf(error, error_size, "THINK_MAX must be at least THINK_MIN");
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
    printf("│ Demand Min   : %-26d │\n", cfg->demand_min);
    printf("│ Demand Max   : %-26d │\n", cfg->demand_max);
    printf("│ Burst Min    : %-26d │\n", cfg->burst_min);
    printf("│ Burst Max    : %-26d │\n", cfg->burst_max);
    printf("│ Think Min    : %-26d │\n", cfg->think_min);
    printf("│ Think Max    : %-26d │\n", cfg->think_max);
    printf("│ Buffer       : %-26d │\n", cfg->buffer_capacity);
    printf("│ Scheduling   : %-26s │\n", sched_algo_name(cfg->sched_algo));
    printf("│ Switch Cost  : %-26d │\n", cfg->switch_cost);
    printf("│ Page Replace : %-26s │\n", cfg->page_algo  == LRU      ? "LRU"      : "FIFO");
    printf("│ Demo Mode    : %-26s │\n", cfg->demo_mode  ? "ON" : "OFF");
    printf("│ Headless     : %-26s │\n", cfg->headless_mode ? "ON" : "OFF");
    printf("└─────────────────────────────────────────┘\n");
}
