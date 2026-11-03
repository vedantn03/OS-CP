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
    else if (strcmp(normalized, "PRIO") == 0)
        *algo = PRIO;
    else if (strcmp(normalized, "MLFQ") == 0)
        *algo = MLFQ;
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
    case PRIO: return "PRIO";
    case MLFQ: return "MLFQ";
    default: return "UNKNOWN";
    }
}

int config_parse_page_algo(const char *value, PageAlgo *algo) {
    char normalized[16];
    size_t length = strlen(value);
    if (length >= sizeof(normalized)) return 0;
    for (size_t i = 0; i <= length; i++)
        normalized[i] = (char)toupper((unsigned char)value[i]);
    if (strcmp(normalized, "LRU") == 0) *algo = LRU;
    else if (strcmp(normalized, "FIFO") == 0) *algo = FIFO;
    else if (strcmp(normalized, "CLOCK") == 0) *algo = CLOCK;
    else return 0;
    return 1;
}

const char *page_algo_name(PageAlgo algo) {
    switch (algo) {
    case LRU: return "LRU";
    case FIFO: return "FIFO";
    case CLOCK: return "CLOCK";
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
    cfg->priority_levels = 3;
    cfg->aging_interval  = 25;
    cfg->preempt         = 1;
    cfg->mlfq_levels     = 3;
    cfg->mlfq_boost      = 100;
    cfg->pages_per_proc  = 16;
    cfg->ws_size         = 4;
    cfg->locality        = 90;
    cfg->phase_ticks     = 40;
    cfg->write_pct       = 30;
    cfg->thrash_high     = 45;
    cfg->page_trace      = 0;
    cfg->thrash_control  = 0;
    cfg->thrash_pause    = 40;
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
        else if (strcmp(key, "PRIORITY_LEVELS") == 0 && numeric) cfg->priority_levels = (int)number;
        else if (strcmp(key, "AGING_INTERVAL")  == 0 && numeric) cfg->aging_interval  = (int)number;
        else if (strcmp(key, "PREEMPT")         == 0 && numeric) cfg->preempt         = (int)number;
        else if (strcmp(key, "MLFQ_LEVELS")     == 0 && numeric) cfg->mlfq_levels     = (int)number;
        else if (strcmp(key, "MLFQ_BOOST")      == 0 && numeric) cfg->mlfq_boost      = (int)number;
        else if (strcmp(key, "PAGES_PER_PROC")  == 0 && numeric) cfg->pages_per_proc  = (int)number;
        else if (strcmp(key, "WS_SIZE")         == 0 && numeric) cfg->ws_size         = (int)number;
        else if (strcmp(key, "LOCALITY")        == 0 && numeric) cfg->locality        = (int)number;
        else if (strcmp(key, "PHASE_TICKS")     == 0 && numeric) cfg->phase_ticks     = (int)number;
        else if (strcmp(key, "WRITE_PCT")       == 0 && numeric) cfg->write_pct       = (int)number;
        else if (strcmp(key, "THRASH_HIGH")     == 0 && numeric) cfg->thrash_high     = (int)number;
        else if (strcmp(key, "PAGE_TRACE")      == 0 && numeric) cfg->page_trace      = (int)number;
        else if (strcmp(key, "THRASH_CONTROL")  == 0 && numeric) cfg->thrash_control  = (int)number;
        else if (strcmp(key, "THRASH_PAUSE")    == 0 && numeric) cfg->thrash_pause    = (int)number;
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
                fprintf(stderr, "ERROR: SCHEDULING_ALGO must be RR, ROUND_ROBIN, PRIORITY, FCFS, SJF, SRTF, PRIO, or MLFQ\n");
                fclose(f);
                return 0;
            }
        }
        else if (strcmp(key, "PAGE_REPLACE") == 0) {
            if (!config_parse_page_algo(val, &cfg->page_algo)) {
                fprintf(stderr, "ERROR: PAGE_REPLACE must be LRU, FIFO, or CLOCK\n");
                fclose(f);
                return 0;
            }
        }
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
                fprintf(stderr, "ERROR: --algo must be RR, ROUND_ROBIN, PRIORITY, FCFS, SJF, SRTF, PRIO, or MLFQ\n");
                return 0;
            }
        }
        else if (strcmp(argv[i], "--switch-cost") == 0 && i+1 < argc)
            cfg->switch_cost = atoi(argv[++i]);
        else if (strcmp(argv[i], "--priority-levels") == 0 && i+1 < argc)
            cfg->priority_levels = atoi(argv[++i]);
        else if (strcmp(argv[i], "--aging") == 0 && i+1 < argc)
            cfg->aging_interval = atoi(argv[++i]);
        else if (strcmp(argv[i], "--preempt") == 0 && i+1 < argc)
            cfg->preempt = atoi(argv[++i]);
        else if (strcmp(argv[i], "--mlfq-levels") == 0 && i+1 < argc)
            cfg->mlfq_levels = atoi(argv[++i]);
        else if (strcmp(argv[i], "--mlfq-boost") == 0 && i+1 < argc)
            cfg->mlfq_boost = atoi(argv[++i]);
        else if (strcmp(argv[i], "--page") == 0 && i+1 < argc) {
            if (!config_parse_page_algo(argv[++i], &cfg->page_algo)) {
                fprintf(stderr, "ERROR: --page must be LRU, FIFO, or CLOCK\n");
                return 0;
            }
        }
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
        else if (strcmp(argv[i], "--pages") == 0 && i+1 < argc) cfg->pages_per_proc = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ws") == 0 && i+1 < argc) cfg->ws_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "--locality") == 0 && i+1 < argc) cfg->locality = atoi(argv[++i]);
        else if (strcmp(argv[i], "--phase") == 0 && i+1 < argc) cfg->phase_ticks = atoi(argv[++i]);
        else if (strcmp(argv[i], "--write-pct") == 0 && i+1 < argc) cfg->write_pct = atoi(argv[++i]);
        else if (strcmp(argv[i], "--thrash-high") == 0 && i+1 < argc) cfg->thrash_high = atoi(argv[++i]);
        else if (strcmp(argv[i], "--page-trace") == 0) cfg->page_trace = 1;
        else if (strcmp(argv[i], "--thrash-control") == 0 && i+1 < argc) cfg->thrash_control = atoi(argv[++i]);
        else if (strcmp(argv[i], "--thrash-pause") == 0 && i+1 < argc) cfg->thrash_pause = atoi(argv[++i]);
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
            printf("  --algo RR|ROUND_ROBIN|PRIORITY|FCFS|SJF|SRTF|PRIO|MLFQ  Scheduling algorithm\n");
            printf("  --switch-cost N  Context-switch overhead in ticks (0..50)\n");
            printf("  --priority-levels N  Static priority levels (1..9)\n");
            printf("  --aging N       Priority aging interval in ticks (0 disables aging)\n");
            printf("  --preempt 0|1   Enable PRIO preemption\n");
            printf("  --mlfq-levels N  MLFQ levels (2..6)\n");
            printf("  --mlfq-boost N  MLFQ boost interval in ticks (0 disables boosts)\n");
            printf("  --page LRU|FIFO|CLOCK  Page replacement algorithm\n");
            printf("  --pages N         Pages per process (4..%d)\n", MAX_PAGES);
            printf("  --ws N            Working-set size\n");
            printf("  --locality N      Locality percentage (0..100)\n");
            printf("  --phase N         Phase duration in ticks\n");
            printf("  --write-pct N     Write percentage (0..100)\n");
            printf("  --thrash-high N   Thrash threshold percentage (1..100)\n");
            printf("  --page-trace      Write page reference trace\n");
            printf("  --thrash-control N  Pause arrivals during thrashing (0 or 1)\n");
            printf("  --thrash-pause N  Arrival pause duration in ticks (1..1000)\n");
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
    else if (cfg->priority_levels < 1 || cfg->priority_levels > 9)
        snprintf(error, error_size, "PRIORITY_LEVELS must be 1..9");
    else if (cfg->aging_interval < 0)
        snprintf(error, error_size, "AGING_INTERVAL must be non-negative");
    else if (cfg->preempt < 0 || cfg->preempt > 1)
        snprintf(error, error_size, "PREEMPT must be 0 or 1");
    else if (cfg->mlfq_levels < 2 || cfg->mlfq_levels > 6)
        snprintf(error, error_size, "MLFQ_LEVELS must be 2..6");
    else if (cfg->mlfq_boost < 0)
        snprintf(error, error_size, "MLFQ_BOOST must be non-negative");
    else if (cfg->pages_per_proc < 4 || cfg->pages_per_proc > MAX_PAGES)
        snprintf(error, error_size, "PAGES_PER_PROC must be 4..%d", MAX_PAGES);
    else if (cfg->ws_size < 1 || cfg->ws_size > cfg->pages_per_proc)
        snprintf(error, error_size, "WS_SIZE must be 1..PAGES_PER_PROC");
    else if (cfg->locality < 0 || cfg->locality > 100)
        snprintf(error, error_size, "LOCALITY must be 0..100");
    else if (cfg->phase_ticks < 1)
        snprintf(error, error_size, "PHASE_TICKS must be at least 1");
    else if (cfg->write_pct < 0 || cfg->write_pct > 100)
        snprintf(error, error_size, "WRITE_PCT must be 0..100");
    else if (cfg->thrash_high < 1 || cfg->thrash_high > 100)
        snprintf(error, error_size, "THRASH_HIGH must be 1..100");
    else if (cfg->page_trace < 0 || cfg->page_trace > 1)
        snprintf(error, error_size, "PAGE_TRACE must be 0 or 1");
    else if (cfg->thrash_control < 0 || cfg->thrash_control > 1)
        snprintf(error, error_size, "THRASH_CONTROL must be 0 or 1");
    else if (cfg->thrash_pause < 1 || cfg->thrash_pause > 1000)
        snprintf(error, error_size, "THRASH_PAUSE must be 1..1000");
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
    printf("│ Priority Lvls: %-26d │\n", cfg->priority_levels);
    printf("│ Aging        : %-26d │\n", cfg->aging_interval);
    printf("│ Preempt      : %-26d │\n", cfg->preempt);
    printf("│ MLFQ Levels  : %-26d │\n", cfg->mlfq_levels);
    printf("│ MLFQ Boost   : %-26d │\n", cfg->mlfq_boost);
    printf("│ Page Replace : %-26s │\n", page_algo_name(cfg->page_algo));
    printf("│ Pages/Proc   : %-26d │\n", cfg->pages_per_proc);
    printf("│ Working Set  : %-26d │\n", cfg->ws_size);
    printf("│ Locality     : %-26d │\n", cfg->locality);
    printf("│ Phase Ticks  : %-26d │\n", cfg->phase_ticks);
    printf("│ Write %%      : %-26d │\n", cfg->write_pct);
    printf("│ Thrash High  : %-26d │\n", cfg->thrash_high);
    printf("│ Page Trace   : %-26s │\n", cfg->page_trace ? "ON" : "OFF");
    printf("│ Thrash Ctrl  : %-26d │\n", cfg->thrash_control);
    printf("│ Thrash Pause : %-26d │\n", cfg->thrash_pause);
    printf("│ Demo Mode    : %-26s │\n", cfg->demo_mode  ? "ON" : "OFF");
    printf("│ Headless     : %-26s │\n", cfg->headless_mode ? "ON" : "OFF");
    printf("└─────────────────────────────────────────┘\n");
}
