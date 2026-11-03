#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef enum { ALGO_FIFO, ALGO_LRU, ALGO_CLOCK, ALGO_OPT } Algorithm;

static int parse_positive(const char *text, int *value) {
    char *end;
    long parsed = strtol(text, &end, 10);
    if (text[0] == '\0' || *end != '\0' || parsed < 1 || parsed > 100000000)
        return 0;
    *value = (int)parsed;
    return 1;
}

static int parse_algo(const char *text, Algorithm *algo) {
    if (strcmp(text, "FIFO") == 0) *algo = ALGO_FIFO;
    else if (strcmp(text, "LRU") == 0) *algo = ALGO_LRU;
    else if (strcmp(text, "CLOCK") == 0) *algo = ALGO_CLOCK;
    else if (strcmp(text, "OPT") == 0) *algo = ALGO_OPT;
    else return 0;
    return 1;
}

static int token_id(char ***names, size_t *count, size_t *capacity, const char *token) {
    for (size_t i = 0; i < *count; i++)
        if (strcmp((*names)[i], token) == 0) return (int)i;
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 16 : *capacity * 2;
        char **grown = realloc(*names, next * sizeof(**names));
        if (!grown) return -1;
        *names = grown;
        *capacity = next;
    }
    (*names)[*count] = malloc(strlen(token) + 1);
    if (!(*names)[*count]) return -1;
    strcpy((*names)[*count], token);
    return (int)(*count)++;
}

static int load_tokens(const char *input, int **refs, size_t *length) {
    char *copy = malloc(strlen(input) + 1);
    if (!copy) return 0;
    strcpy(copy, input);
    char **names = NULL;
    size_t names_count = 0, names_capacity = 0, refs_capacity = 0;
    char *save = NULL;
    for (char *token = strtok_r(copy, " ,\t\r\n", &save);
         token; token = strtok_r(NULL, " ,\t\r\n", &save)) {
        int id = token_id(&names, &names_count, &names_capacity, token);
        if (id < 0) {
            for (size_t i = 0; i < names_count; i++) free(names[i]);
            free(names);
            free(copy);
            return 0;
        }
        if (*length == refs_capacity) {
            size_t next = refs_capacity == 0 ? 64 : refs_capacity * 2;
            int *grown = realloc(*refs, next * sizeof(**refs));
            if (!grown) {
                for (size_t i = 0; i < names_count; i++) free(names[i]);
                free(names);
                free(copy);
                return 0;
            }
            *refs = grown;
            refs_capacity = next;
        }
        (*refs)[(*length)++] = id;
    }
    for (size_t i = 0; i < names_count; i++) free(names[i]);
    free(names);
    free(copy);
    return *length > 0;
}

static int read_file(const char *path, char **data) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    size_t length = 0, capacity = 4096;
    *data = malloc(capacity);
    if (!*data) {
        fclose(file);
        return 0;
    }
    while (1) {
        size_t got = fread(*data + length, 1, capacity - length - 1, file);
        length += got;
        if (ferror(file)) {
            free(*data);
            *data = NULL;
            fclose(file);
            return 0;
        }
        if (got == 0) break;
        if (length + 1 == capacity) {
            size_t next = capacity * 2;
            char *grown = realloc(*data, next);
            if (!grown) {
                free(*data);
                *data = NULL;
                fclose(file);
                return 0;
            }
            *data = grown;
            capacity = next;
        }
    }
    (*data)[length] = '\0';
    fclose(file);
    return length > 0;
}

static int run_simulation(const int *refs, size_t count, int frames,
                          Algorithm algo, int *faults, int *hits) {
    int *resident = malloc((size_t)frames * sizeof(*resident));
    int *last_use = malloc((size_t)frames * sizeof(*last_use));
    int *ref_bits = calloc((size_t)frames, sizeof(*ref_bits));
    int *next_use = malloc(count * sizeof(*next_use));
    int *latest = malloc(count * sizeof(*latest));
    int hand = 0, loaded = 0, fifo = 0;
    *faults = 0;
    *hits = 0;
    if (!resident || !last_use || !ref_bits || !next_use || !latest) {
        free(resident);
        free(last_use);
        free(ref_bits);
        free(next_use);
        free(latest);
        return 0;
    }
    for (int i = 0; i < frames; i++) resident[i] = -1;
    for (size_t i = 0; i < count; i++) latest[i] = -1;
    for (size_t i = count; i-- > 0;) {
        next_use[i] = latest[refs[i]];
        latest[refs[i]] = (int)i;
    }
    for (size_t i = 0; i < count; i++) latest[i] = -1;
    for (size_t i = 0; i < count; i++) {
        int frame = -1;
        for (int j = 0; j < frames; j++)
            if (resident[j] == refs[i]) frame = j;
        if (frame >= 0) {
            (*hits)++;
            last_use[frame] = (int)i;
            ref_bits[frame] = 1;
            latest[refs[i]] = next_use[i];
            continue;
        }
        (*faults)++;
        if (loaded < frames) frame = loaded++;
        else if (algo == ALGO_FIFO) frame = fifo++ % frames;
        else if (algo == ALGO_LRU) {
            frame = 0;
            for (int j = 1; j < frames; j++)
                if (last_use[j] < last_use[frame]) frame = j;
        } else if (algo == ALGO_CLOCK) {
            while (ref_bits[hand]) {
                ref_bits[hand] = 0;
                hand = (hand + 1) % frames;
            }
            frame = hand;
            hand = (hand + 1) % frames;
        } else {
            frame = 0;
            int farthest = -1;
            for (int j = 0; j < frames; j++) {
                int next = latest[resident[j]];
                if (next < 0) {
                    frame = j;
                    break;
                }
                if (next > farthest) {
                    farthest = next;
                    frame = j;
                }
            }
        }
        resident[frame] = refs[i];
        last_use[frame] = (int)i;
        ref_bits[frame] = 1;
        latest[refs[i]] = next_use[i];
    }
    free(resident);
    free(last_use);
    free(ref_bits);
    free(next_use);
    free(latest);
    return 1;
}

int main(int argc, char **argv) {
    Algorithm algo = ALGO_FIFO;
    int frames = 0, sweep_start = 0, sweep_end = 0;
    const char *string_input = NULL, *file_input = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--algo") == 0 && i + 1 < argc) {
            if (!parse_algo(argv[++i], &algo)) {
                fprintf(stderr, "unknown algorithm\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            if (!parse_positive(argv[++i], &frames)) return 2;
        } else if (strcmp(argv[i], "--string") == 0 && i + 1 < argc) {
            string_input = argv[++i];
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            file_input = argv[++i];
        } else if (strcmp(argv[i], "--sweep") == 0 && i + 1 < argc) {
            char *dash;
            char *value = argv[++i];
            dash = strchr(value, '-');
            if (!dash || dash == value || dash[1] == '\0') return 2;
            *dash = '\0';
            if (!parse_positive(value, &sweep_start) ||
                !parse_positive(dash + 1, &sweep_end) ||
                sweep_start > sweep_end) return 2;
        } else {
            return 2;
        }
    }
    if ((sweep_start == 0 && frames < 1) ||
        (string_input && file_input) || (!string_input && !file_input))
        return 2;
    char *input = NULL;
    if (string_input) {
        input = malloc(strlen(string_input) + 1);
        if (!input) return 1;
        strcpy(input, string_input);
    } else if (!read_file(file_input, &input)) {
        fprintf(stderr, "could not read input\n");
        return 1;
    }
    int *refs = NULL;
    size_t count = 0;
    if (!load_tokens(input, &refs, &count)) {
        free(input);
        fprintf(stderr, "empty or invalid input\n");
        return 1;
    }
    free(input);
    if (sweep_start > 0) {
        for (int f = sweep_start; f <= sweep_end; f++) {
            int faults, hits;
            if (!run_simulation(refs, count, f, algo, &faults, &hits)) {
                free(refs);
                fprintf(stderr, "simulation allocation failed\n");
                return 1;
            }
            printf("frames %d faults %d\n", f, faults);
        }
    } else {
        int faults, hits;
        if (!run_simulation(refs, count, frames, algo, &faults, &hits)) {
            free(refs);
            fprintf(stderr, "simulation allocation failed\n");
            return 1;
        }
        printf("algo: %s\nframes: %d\nrefs: %zu\nfaults: %d\nhits: %d\n",
               algo == ALGO_FIFO ? "FIFO" : algo == ALGO_LRU ? "LRU" :
               algo == ALGO_CLOCK ? "CLOCK" : "OPT",
               frames, count, faults, hits);
    }
    free(refs);
    return 0;
}
