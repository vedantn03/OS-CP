#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_MAX 128
#define VALUE_MAX 100000000L
#define BUDDY_MAX_ORDER 30

static void error_message(const char *message) { fprintf(stderr, "memlab: %s\n", message); }

static int parse_long_value(const char *text, long minimum, long maximum, long *out) {
    char *end;
    long value;
    if (text == NULL || *text == '\0') return 0;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < minimum || value > maximum) return 0;
    *out = value;
    return 1;
}

static int parse_list(const char *text, long values[LIST_MAX], size_t *count,
                      long minimum, long maximum) {
    char *copy, *save = NULL, *token;
    *count = 0;
    if (text == NULL || *text == '\0') return 0;
    copy = malloc(strlen(text) + 1);
    if (copy == NULL) return -1;
    strcpy(copy, text);
    for (token = strtok_r(copy, " ,\t\r\n", &save); token != NULL;
         token = strtok_r(NULL, " ,\t\r\n", &save)) {
        long value;
        if (*count == LIST_MAX || !parse_long_value(token, minimum, maximum, &value)) {
            free(copy);
            return 0;
        }
        values[(*count)++] = value;
    }
    free(copy);
    return *count == 0 ? 0 : 1;
}

static int parse_policy(const char *text) {
    if (strcmp(text, "first") == 0) return 0;
    if (strcmp(text, "best") == 0) return 1;
    if (strcmp(text, "worst") == 0) return 2;
    return -1;
}

static int contiguous_command(int argc, char **argv) {
    const char *policy_text = NULL, *holes_text = NULL, *requests_text = NULL;
    long holes[LIST_MAX], requests[LIST_MAX];
    long long total_free, largest_free = 0;
    size_t hole_count = 0, request_count = 0, i;
    unsigned seen = 0;
    int policy, allocated = 0, failed = 0, external = 0, j, parsed;
    for (i = 0; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--policy") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 1U)) return 2;
            policy_text = argv[++i]; seen |= 1U;
        } else if (strcmp(argv[i], "--holes") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 2U)) return 2;
            holes_text = argv[++i]; seen |= 2U;
        } else if (strcmp(argv[i], "--requests") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 4U)) return 2;
            requests_text = argv[++i]; seen |= 4U;
        } else return 2;
    }
    if (seen != 7U) return 2;
    policy = parse_policy(policy_text);
    if (policy < 0) return 2;
    parsed = parse_list(holes_text, holes, &hole_count, 1, VALUE_MAX);
    if (parsed < 0) { error_message("out of memory"); return 1; }
    if (parsed == 0) return 2;
    parsed = parse_list(requests_text, requests, &request_count, 1, VALUE_MAX);
    if (parsed < 0) { error_message("out of memory"); return 1; }
    if (parsed == 0) return 2;
    total_free = 0;
    for (i = 0; i < hole_count; i++) total_free += holes[i];
    for (i = 0; i < request_count; i++) {
        int selected = -1;
        for (j = 0; j < (int)hole_count; j++) {
            if (holes[j] < requests[i]) continue;
            if (selected < 0 || (policy == 1 && holes[j] < holes[selected]) ||
                (policy == 2 && holes[j] > holes[selected])) selected = j;
            if (policy == 0) break;
        }
        if (selected < 0) {
            failed++;
            if (total_free >= requests[i]) external = 1;
        } else {
            holes[selected] -= requests[i];
            total_free -= requests[i];
            allocated++;
        }
    }
    for (i = 0; i < hole_count; i++) if (holes[i] > largest_free) largest_free = holes[i];
    printf("mode: contiguous\npolicy: %s\nrequests: %zu\nallocated: %d\nfailed: %d\ntotal_free: %lld\nlargest_free: %lld\nexternal_fragmentation: %s\nfragmented_free: %lld\n",
           policy_text, request_count, allocated, failed, total_free, largest_free,
           external ? "yes" : "no", total_free - largest_free);
    return 0;
}

static int is_power_of_two(long value) { return value > 0 && (value & (value - 1)) == 0; }

static int buddy_command(int argc, char **argv) {
    const char *requests_text = NULL;
    long total = 0, minimum = 0, requests[LIST_MAX];
    size_t request_count = 0, i;
    unsigned seen = 0;
    unsigned long long free_blocks[BUDDY_MAX_ORDER + 1] = {0};
    long long allocated_bytes = 0, requested_bytes = 0, total_free, largest_free = 0;
    int max_order, allocated = 0, failed = 0, parsed;
    for (i = 0; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--total") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 1U) || !parse_long_value(argv[i + 1], 1, VALUE_MAX, &total)) return 2;
            i++; seen |= 1U;
        } else if (strcmp(argv[i], "--min-block") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 2U) || !parse_long_value(argv[i + 1], 1, VALUE_MAX, &minimum)) return 2;
            i++; seen |= 2U;
        } else if (strcmp(argv[i], "--requests") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 4U)) return 2;
            requests_text = argv[++i]; seen |= 4U;
        } else return 2;
    }
    if (seen != 7U || !is_power_of_two(total) || !is_power_of_two(minimum) || minimum > total) return 2;
    parsed = parse_list(requests_text, requests, &request_count, 1, VALUE_MAX);
    if (parsed < 0) { error_message("out of memory"); return 1; }
    if (parsed == 0) return 2;
    max_order = 0;
    { long block = minimum; while (block < total) { block *= 2; max_order++; } }
    if (max_order > BUDDY_MAX_ORDER) return 2;
    free_blocks[max_order] = 1;
    for (i = 0; i < request_count; i++) {
        long rounded = minimum;
        int wanted = 0, found;
        while (rounded < requests[i] && rounded <= total / 2) { rounded *= 2; wanted++; }
        if (rounded < requests[i]) { failed++; continue; }
        found = wanted;
        while (found <= max_order && free_blocks[found] == 0) found++;
        if (found > max_order) { failed++; continue; }
        free_blocks[found]--;
        while (found > wanted) { found--; free_blocks[found]++; }
        allocated++;
        requested_bytes += requests[i];
        allocated_bytes += rounded;
    }
    total_free = (long long)total - allocated_bytes;
    for (i = 0; i <= (size_t)max_order; i++) {
        long long size = (long long)minimum << i;
        if (free_blocks[i] != 0 && size > largest_free) largest_free = size;
    }
    printf("mode: buddy\ntotal: %ld\nmin_block: %ld\nrequests: %zu\nallocated: %d\nfailed: %d\nallocated_bytes: %lld\nrequested_bytes: %lld\ninternal_fragmentation: %lld\ntotal_free: %lld\nlargest_free: %lld\n",
           total, minimum, request_count, allocated, failed, allocated_bytes,
           requested_bytes, allocated_bytes - requested_bytes, total_free, largest_free);
    return 0;
}

static int slab_command(int argc, char **argv) {
    const char *classes_text = NULL, *objects_text = NULL;
    long page_size = 0, classes[LIST_MAX], objects[LIST_MAX];
    unsigned long long slabs[LIST_MAX] = {0}, used[LIST_MAX] = {0};
    size_t class_count = 0, object_count = 0, i, j;
    unsigned seen = 0;
    long long slab_bytes = 0, requested_bytes = 0, allocated_bytes = 0;
    int allocated = 0, failed = 0, parsed;
    for (i = 0; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--page-size") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 1U) || !parse_long_value(argv[i + 1], 1, VALUE_MAX, &page_size)) return 2;
            i++; seen |= 1U;
        } else if (strcmp(argv[i], "--classes") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 2U)) return 2;
            classes_text = argv[++i]; seen |= 2U;
        } else if (strcmp(argv[i], "--objects") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 4U)) return 2;
            objects_text = argv[++i]; seen |= 4U;
        } else return 2;
    }
    if (seen != 7U) return 2;
    parsed = parse_list(classes_text, classes, &class_count, 1, VALUE_MAX);
    if (parsed < 0) { error_message("out of memory"); return 1; }
    if (parsed == 0) return 2;
    parsed = parse_list(objects_text, objects, &object_count, 1, VALUE_MAX);
    if (parsed < 0) { error_message("out of memory"); return 1; }
    if (parsed == 0) return 2;
    for (i = 0; i < class_count; i++) {
        if (classes[i] > page_size || (i > 0 && classes[i] <= classes[i - 1])) return 2;
    }
    for (i = 0; i < object_count; i++) {
        for (j = 0; j < class_count && classes[j] < objects[i]; j++) { }
        if (j == class_count) { failed++; continue; }
        if (used[j] == 0 || used[j] == (unsigned long long)(page_size / classes[j])) {
            slabs[j]++;
            used[j] = 0;
            slab_bytes += page_size;
        }
        used[j]++;
        allocated++;
        requested_bytes += objects[i];
        allocated_bytes += classes[j];
    }
    {
        unsigned long long slab_count = 0;
        for (i = 0; i < class_count; i++) slab_count += slabs[i];
        printf("mode: slab\npage_size: %ld\nclasses: %zu\nobjects: %zu\nallocated: %d\nfailed: %d\nslabs: %llu\nrequested_bytes: %lld\nallocated_bytes: %lld\ninternal_fragmentation: %lld\nslab_waste: %lld\n",
               page_size, class_count, object_count, allocated, failed, slab_count,
               requested_bytes, allocated_bytes, allocated_bytes - requested_bytes,
               slab_bytes - allocated_bytes);
    }
    return 0;
}

int main(int argc, char **argv) {
    int result;
    if (argc < 2) { error_message("usage: memlab contiguous|buddy|slab ..."); return 2; }
    if (strcmp(argv[1], "contiguous") == 0) result = contiguous_command(argc - 2, argv + 2);
    else if (strcmp(argv[1], "buddy") == 0) result = buddy_command(argc - 2, argv + 2);
    else if (strcmp(argv[1], "slab") == 0) result = slab_command(argc - 2, argv + 2);
    else result = 2;
    if (result == 2) error_message("invalid command, option, or value");
    else if (result == 1) error_message("runtime failure");
    return result;
}
