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
        else if (strcmp(argv[i], "--algo")     == 0 && i+1 < argc)
            cfg->sched_algo = (strcmp(argv[++i], "RR") == 0) ? ROUND_ROBIN : PRIORITY;
        else if (strcmp(argv[i], "--page")     == 0 && i+1 < argc)
            cfg->page_algo  = (strcmp(argv[++i], "FIFO") == 0) ? FIFO : LRU;
        else if (strcmp(argv[i], "--demo")     == 0) cfg->demo_mode = 1;
        else if (strcmp(argv[i], "--headless") == 0) cfg->headless_mode = 1;
        else if (strcmp(argv[i], "--seed")    == 0 && i+1 < argc) cfg->random_seed = (unsigned int)strtoul(argv[++i], NULL, 10);
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
    printf("│ Buffer       : %-26d │\n", cfg->buffer_capacity);
    printf("│ Scheduling   : %-26s │\n", cfg->sched_algo == PRIORITY ? "PRIORITY" : "ROUND_ROBIN");
    printf("│ Page Replace : %-26s │\n", cfg->page_algo  == LRU      ? "LRU"      : "FIFO");
    printf("│ Demo Mode    : %-26s │\n", cfg->demo_mode  ? "ON" : "OFF");
    printf("│ Headless     : %-26s │\n", cfg->headless_mode ? "ON" : "OFF");
    printf("└─────────────────────────────────────────┘\n");
}
