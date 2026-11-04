#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROCESSES 32
#define MAX_RESOURCES 16
#define MAX_TOKENS 1024
#define MAX_TOKEN_LEN 127

typedef struct {
    int processes;
    int resources;
    long long available[MAX_RESOURCES];
    int maximum[MAX_PROCESSES][MAX_RESOURCES];
    int allocation[MAX_PROCESSES][MAX_RESOURCES];
    int request[MAX_PROCESSES][MAX_RESOURCES];
    int have_processes;
    int have_resources;
    int have_available;
    int have_maximum;
    int have_allocation;
    int have_request;
} State;

static void error(const char *message) { fprintf(stderr, "deadsim: %s\n", message); }

static int parse_long(const char *text, long min, long max, long *out) {
    char *end;
    long value;
    if (text == NULL || *text == '\0') return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < min || value > max) return 0;
    *out = value;
    return 1;
}

static int tokenize(char *line, char tokens[][MAX_TOKEN_LEN + 1], int max_tokens) {
    int count = 0;
    char *p = line;
    while (*p) {
        char *start;
        size_t len;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') break;
        start = p;
        while (*p && !isspace((unsigned char)*p) && *p != '#') p++;
        len = (size_t)(p - start);
        if (len > MAX_TOKEN_LEN || count >= max_tokens) return -1;
        memcpy(tokens[count], start, len);
        tokens[count][len] = '\0';
        count++;
        if (*p == '#') break;
    }
    return count;
}

static int read_line(FILE *file, char *line, size_t capacity) {
    size_t len;
    if (fgets(line, (int)capacity, file) == NULL) return 0;
    len = strlen(line);
    if (len > 0 && line[len - 1] != '\n' && !feof(file)) return -1;
    return 1;
}

static int parse_vector(char tokens[][MAX_TOKEN_LEN + 1], int count, int expected, int *out) {
    int i;
    long value;
    if (count != expected) return 0;
    for (i = 0; i < expected; i++) {
        if (!parse_long(tokens[i], 0, INT_MAX, &value)) return 0;
        out[i] = (int)value;
    }
    return 1;
}

static int read_state(const char *path, State *s) {
    FILE *file = fopen(path, "r");
    char line[4096];
    char tokens[MAX_TOKENS][MAX_TOKEN_LEN + 1];
    int section = 0, row = 0, line_status;
    long value;
    if (file == NULL) { error("cannot open state file"); return 0; }
    memset(s, 0, sizeof(*s));
    while ((line_status = read_line(file, line, sizeof(line))) != 0) {
        int count;
        if (line_status < 0) { error("input line too long"); fclose(file); return 0; }
        count = tokenize(line, tokens, MAX_TOKENS);
        if (count < 0) { error("too many or overlong tokens"); fclose(file); return 0; }
        if (count == 0) continue;
        if (section != 0 && section != 4 && section != 5 && section != 6) {
            int *target = section == 1 ? s->maximum[row] : section == 2 ? s->allocation[row] : s->request[row];
            if (!parse_vector(tokens, count, s->resources, target)) { error("invalid matrix row"); fclose(file); return 0; }
            row++;
            if (row == s->processes) section = 0;
            continue;
        }
        if (strcmp(tokens[0], "NAMES") == 0) {
            if (count != s->resources + 1 || s->resources == 0) { error("invalid NAMES line"); fclose(file); return 0; }
            continue;
        }
        if (strcmp(tokens[0], "PROCESSES") == 0) {
            if (s->have_processes || count != 2 || !parse_long(tokens[1], 1, MAX_PROCESSES, &value)) { error("invalid PROCESSES line"); fclose(file); return 0; }
            s->processes = (int)value; s->have_processes = 1;
        } else if (strcmp(tokens[0], "RESOURCES") == 0) {
            if (s->have_resources || count != 2 || !parse_long(tokens[1], 1, MAX_RESOURCES, &value)) { error("invalid RESOURCES line"); fclose(file); return 0; }
            s->resources = (int)value; s->have_resources = 1;
        } else if (strcmp(tokens[0], "AVAILABLE") == 0) {
            int values[MAX_RESOURCES], j;
            if (s->have_available || !s->have_resources || !parse_vector(tokens + 1, count - 1, s->resources, values)) { error("invalid AVAILABLE line"); fclose(file); return 0; }
            for (j = 0; j < s->resources; j++) s->available[j] = values[j];
            s->have_available = 1;
        } else if (strcmp(tokens[0], "MAX") == 0 || strcmp(tokens[0], "ALLOC") == 0 || strcmp(tokens[0], "REQUEST") == 0) {
            int *flag, kind;
            if (count != 1 || !s->have_processes || !s->have_resources || !s->have_available) { error("matrix section before header or malformed"); fclose(file); return 0; }
            if (strcmp(tokens[0], "MAX") == 0) { flag = &s->have_maximum; kind = 1; }
            else if (strcmp(tokens[0], "ALLOC") == 0) { flag = &s->have_allocation; kind = 2; }
            else { flag = &s->have_request; kind = 3; }
            if (*flag) { error("duplicate matrix section"); fclose(file); return 0; }
            *flag = 1; section = kind; row = 0;
        } else {
            error("unknown state token"); fclose(file); return 0;
        }
    }
    fclose(file);
    if (section != 0 || !s->have_processes || !s->have_resources || !s->have_available) { error("incomplete state file"); return 0; }
    return 1;
}

static void calculate_need(const State *s, int need[MAX_PROCESSES][MAX_RESOURCES]) {
    int i, j;
    for (i = 0; i < s->processes; i++) for (j = 0; j < s->resources; j++) need[i][j] = s->maximum[i][j] - s->allocation[i][j];
}

static int less_equal(const int *a, const long long *b, int n) {
    int j;
    for (j = 0; j < n; j++) if (a[j] > b[j]) return 0;
    return 1;
}

static int safe_sequence(const State *s, int sequence[MAX_PROCESSES], int *length) {
    int need[MAX_PROCESSES][MAX_RESOURCES], done[MAX_PROCESSES] = {0};
    long long work[MAX_RESOURCES];
    int i, j, progress;
    calculate_need(s, need);
    memcpy(work, s->available, (size_t)s->resources * sizeof(work[0]));
    *length = 0;
    do {
        progress = 0;
        for (i = 0; i < s->processes; i++) if (!done[i] && less_equal(need[i], work, s->resources)) {
            sequence[(*length)++] = i; done[i] = 1; progress = 1;
            for (j = 0; j < s->resources; j++) work[j] += s->allocation[i][j];
            break;
        }
    } while (progress);
    for (i = 0; i < s->processes; i++) if (!done[i]) return 0;
    return 1;
}

static void print_indices(const int *values, int count) {
    int i;
    if (count == 0) { printf("none"); return; }
    for (i = 0; i < count; i++) printf("%s%d", i ? " " : "", values[i]);
}

static void print_vector(const int *values, int count) {
    int i;
    for (i = 0; i < count; i++) printf("%s%d", i ? " " : "", values[i]);
}

static int validate_alloc_max(const State *s) {
    int i, j;
    for (i = 0; i < s->processes; i++) for (j = 0; j < s->resources; j++) if (s->allocation[i][j] > s->maximum[i][j]) return 0;
    return 1;
}

static int detect(State *s, int finish_order[MAX_PROCESSES], int *nfinish, int dead[MAX_PROCESSES], int *ndead) {
    long long work[MAX_RESOURCES];
    int finish[MAX_PROCESSES] = {0}, i, j, progress;
    memcpy(work, s->available, (size_t)s->resources * sizeof(work[0])); *nfinish = 0;
    for (i = 0; i < s->processes; i++) {
        finish[i] = 1;
        for (j = 0; j < s->resources; j++) if (s->allocation[i][j] != 0) finish[i] = 0;
    }
    do {
        progress = 0;
        for (i = 0; i < s->processes; i++) if (!finish[i] && less_equal(s->request[i], work, s->resources)) {
            finish_order[(*nfinish)++] = i; finish[i] = 1; progress = 1;
            for (j = 0; j < s->resources; j++) work[j] += s->allocation[i][j];
            break;
        }
    } while (progress);
    *ndead = 0;
    for (i = 0; i < s->processes; i++) if (!finish[i]) dead[(*ndead)++] = i;
    return *ndead != 0;
}

static int usage(void) { error("usage error"); return 2; }

int main(int argc, char **argv) {
    const char *command;
    if (argc < 2) return usage();
    command = argv[1];
    if (strcmp(command, "wfg") == 0) {
        const char *edge_text = NULL; long nodes = -1; int seen_nodes = 0, seen_edges = 0;
        unsigned char edge[64][64] = {{0}}; int on[64] = {0}, i, j, k, any_cycle = 0;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--nodes") == 0) { if (++i >= argc || seen_nodes++) return usage(); if (!parse_long(argv[i], 1, 64, &nodes)) return usage(); }
            else if (strcmp(argv[i], "--edges") == 0) { if (++i >= argc || seen_edges++) return usage(); edge_text = argv[i]; }
            else return usage();
        }
        if (!seen_nodes || !seen_edges) return usage();
        { const char *p = edge_text;
          while (*p) {
              const char *start; long from, to; char *end;
              while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
              if (!*p) break;
              start = p; errno = 0; from = strtol(p, &end, 10);
              if (end == p || errno == ERANGE || *end != '>') { error("invalid edge"); return 1; }
              p = end + 1; errno = 0; to = strtol(p, &end, 10);
              if (end == p || errno == ERANGE || (from < 0 || from >= nodes || to < 0 || to >= nodes) || (*end && !isspace((unsigned char)*end) && *end != ',')) { error("invalid edge endpoint"); return 1; }
              edge[from][to] = 1; p = end; (void)start;
          }
        }
        for (k = 0; k < nodes; k++)
            for (i = 0; i < nodes; i++)
                for (j = 0; j < nodes; j++)
                    if (edge[i][k] && edge[k][j]) edge[i][j] = 1;
        for (i = 0; i < nodes; i++) on[i] = edge[i][i];
        for (i = 0; i < nodes; i++) any_cycle |= on[i];
        printf("deadlock: %s\non_cycle:", any_cycle ? "yes" : "no");
        k = 0; for (i = 0; i < nodes; i++) if (on[i]) { printf(" %d", i); k++; }
        if (!k) printf(" none");
        printf("\n");
        return 0;
    }
    if (strcmp(command, "safe") != 0 && strcmp(command, "request") != 0 && strcmp(command, "detect") != 0 && strcmp(command, "recover") != 0) return usage();
    {
        const char *file = NULL, *policy = "min-alloc", *request_text = NULL; long pid = -1, value;
        int have_pid = 0, have_req = 0, have_policy = 0, i, sequence[MAX_PROCESSES], length, dead[MAX_PROCESSES], ndead, finish[MAX_PROCESSES], nfinish;
        State state;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--file") == 0) { if (++i >= argc || file != NULL) return usage(); file = argv[i]; }
            else if (strcmp(argv[i], "--pid") == 0) { if (++i >= argc || have_pid++) return usage(); if (!parse_long(argv[i], 0, MAX_PROCESSES - 1, &pid)) { error("invalid pid"); return 1; } }
            else if (strcmp(argv[i], "--req") == 0) { if (++i >= argc || have_req++) return usage(); request_text = argv[i]; }
            else if (strcmp(argv[i], "--policy") == 0) { if (++i >= argc || have_policy++) return usage(); policy = argv[i]; }
            else return usage();
        }
        if (!file) return usage();
        if (strcmp(command, "request") == 0 && (!have_pid || !have_req)) return usage();
        if ((strcmp(command, "safe") == 0 || strcmp(command, "detect") == 0) && (have_pid || have_req || have_policy)) return usage();
        if (strcmp(command, "request") == 0 && have_policy) return usage();
        if (strcmp(command, "recover") == 0 && (have_pid || have_req)) return usage();
        if (strcmp(command, "recover") == 0 && strcmp(policy, "min-alloc") && strcmp(policy, "max-alloc") && strcmp(policy, "lowest-index") && strcmp(policy, "highest-index")) { error("unknown policy"); return 2; }
        if (strcmp(command, "recover") != 0 && have_policy) return usage();
        if (!read_state(file, &state)) return 1;
        if (strcmp(command, "safe") == 0 || strcmp(command, "request") == 0) {
            if (!state.have_maximum || !state.have_allocation || !validate_alloc_max(&state)) { error("safe/request requires valid MAX and ALLOC"); return 1; }
            if (strcmp(command, "safe") == 0) {
                int is_safe = safe_sequence(&state, sequence, &length);
                printf("state: %s\nsequence: ", is_safe ? "safe" : "unsafe"); print_indices(sequence, is_safe ? length : 0); printf("\n"); return 0;
            } else {
                int req[MAX_RESOURCES], need[MAX_PROCESSES][MAX_RESOURCES]; const char *p = request_text; int index = 0, reason = 0;
                if (pid >= state.processes) { error("pid out of range"); return 1; }
                while (*p) { char *end; while (isspace((unsigned char)*p)) p++; if (!*p) break; errno = 0; value = strtol(p, &end, 10); if (end == p || errno == ERANGE || value < 0 || value > INT_MAX || (*end && !isspace((unsigned char)*end)) || index >= state.resources) { error("malformed request"); return 1; } req[index++] = (int)value; p = end; }
                if (index != state.resources) { error("wrong request length"); return 1; }
                calculate_need(&state, need);
                for (i = 0; i < state.resources; i++) if (req[i] > need[pid][i]) reason = 1;
                if (!reason) for (i = 0; i < state.resources; i++) if (req[i] > state.available[i]) reason = 2;
                printf("pid: %ld\nrequest: ", pid); print_vector(req, state.resources); printf("\n");
                if (!reason) {
                    for (i = 0; i < state.resources; i++) { state.available[i] -= req[i]; state.allocation[pid][i] += req[i]; }
                    if (!safe_sequence(&state, sequence, &length)) reason = 3;
                }
                printf("result: %s\nreason: %s\nsequence: ", reason ? "denied" : "granted", reason == 1 ? "exceeds_need" : reason == 2 ? "exceeds_available" : reason == 3 ? "unsafe" : "ok");
                print_indices(sequence, reason ? 0 : length); printf("\n"); return 0;
            }
        }
        if (!state.have_allocation || !state.have_request) { error("detect/recover requires ALLOC and REQUEST"); return 1; }
        if (strcmp(command, "detect") == 0) {
            int is_dead = detect(&state, finish, &nfinish, dead, &ndead);
            printf("deadlock: %s\nfinish_order: ", is_dead ? "yes" : "no"); print_indices(finish, nfinish); printf("\ndeadlocked: "); print_indices(dead, ndead); printf("\n"); return 0;
        }
        {
            int initially_dead = detect(&state, finish, &nfinish, dead, &ndead), aborted[MAX_PROCESSES], naborted = 0;
            while (ndead) {
                int victim = dead[0], candidate, j;
                long long best_total = 0;
                if (strcmp(policy, "highest-index") == 0) victim = dead[ndead - 1];
                else if (strcmp(policy, "min-alloc") == 0 || strcmp(policy, "max-alloc") == 0) {
                    for (i = 0; i < ndead; i++) {
                        candidate = dead[i]; { long long total = 0; for (j = 0; j < state.resources; j++) total += state.allocation[candidate][j];
                          if (i == 0 || (strcmp(policy, "min-alloc") == 0 ? total < best_total : total > best_total)) { victim = candidate; best_total = total; } }
                    }
                }
                aborted[naborted++] = victim;
                for (j = 0; j < state.resources; j++) { state.available[j] += state.allocation[victim][j]; state.allocation[victim][j] = 0; state.request[victim][j] = 0; }
                detect(&state, finish, &nfinish, dead, &ndead);
            }
            printf("deadlock: %s\naborted: ", initially_dead ? "yes" : "no"); print_indices(aborted, naborted); printf("\nfinal: no_deadlock\n"); return 0;
        }
    }
}
