#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_MAX 128
#define VALUE_MAX 100000000L
#define BUDDY_MAX_ORDER 30
#define BUDDY_FREE_MAX (LIST_MAX * (BUDDY_MAX_ORDER + 1) + 1)

typedef struct { long offset; int order; int active; } BuddyFreeBlock;
typedef struct { long requested; long block_size; long offset; int active; } BuddyAllocation;

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

typedef struct {
    long requested;
    int class_index;
    unsigned long long slab_index;
    unsigned long long slot_index;
    int active;
} SlabObject;

static int slab_allocate_object(long requested, long page_size, const long classes[LIST_MAX],
                                size_t class_count, unsigned long long slabs[LIST_MAX],
                                SlabObject objects[LIST_MAX], size_t *object_count,
                                int *class_result) {
    size_t class_index;
    for (class_index = 0; class_index < class_count && classes[class_index] < requested; class_index++) { }
    if (class_index == class_count || *object_count >= LIST_MAX) return 0;
    {
        unsigned long long slab_index, capacity = (unsigned long long)(page_size / classes[class_index]);
        for (slab_index = 0; slab_index < slabs[class_index]; slab_index++) {
            unsigned long long slot_index;
            for (slot_index = 0; slot_index < capacity; slot_index++) {
                size_t k;
                int occupied = 0;
                for (k = 0; k < *object_count; k++) if (objects[k].active &&
                    objects[k].class_index == (int)class_index && objects[k].slab_index == slab_index &&
                    objects[k].slot_index == slot_index) { occupied = 1; break; }
                if (!occupied) {
                    SlabObject *object = &objects[(*object_count)++];
                    object->requested = requested;
                    object->class_index = (int)class_index;
                    object->slab_index = slab_index;
                    object->slot_index = slot_index;
                    object->active = 1;
                    *class_result = (int)class_index;
                    return 1;
                }
            }
        }
        {
            SlabObject *object = &objects[(*object_count)++];
            object->requested = requested;
            object->class_index = (int)class_index;
            object->slab_index = slabs[class_index]++;
            object->slot_index = 0;
            object->active = 1;
            *class_result = (int)class_index;
            return 1;
        }
    }
}

static int buddy_command(int argc, char **argv) {
    const char *requests_text = NULL, *free_text = NULL;
    long total = 0, minimum = 0, requests[LIST_MAX], free_ids[LIST_MAX];
    size_t request_count = 0, free_count = 0, i;
    unsigned seen = 0;
    BuddyFreeBlock free_blocks[BUDDY_FREE_MAX];
    BuddyAllocation allocations[LIST_MAX] = {{0}};
    long long allocated_bytes = 0, requested_bytes = 0, total_free = 0, largest_free = 0;
    int free_block_count = 0, max_order, allocated = 0, failed = 0, freed = 0, parsed;
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
        } else if (strcmp(argv[i], "--free") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 8U)) return 2;
            free_text = argv[++i]; seen |= 8U;
        } else return 2;
    }
    if ((seen & 7U) != 7U || !is_power_of_two(total) || !is_power_of_two(minimum) || minimum > total) return 2;
    parsed = parse_list(requests_text, requests, &request_count, 1, VALUE_MAX);
    if (parsed < 0) { error_message("out of memory"); return 1; }
    if (parsed == 0) return 2;
    if (free_text != NULL) {
        parsed = parse_list(free_text, free_ids, &free_count, 1, LIST_MAX);
        if (parsed < 0) { error_message("out of memory"); return 1; }
        if (parsed == 0) return 2;
    }
    max_order = 0;
    { long block = minimum; while (block < total) { block *= 2; max_order++; } }
    if (max_order > BUDDY_MAX_ORDER) return 2;
    free_blocks[0].offset = 0;
    free_blocks[0].order = max_order;
    free_blocks[0].active = 1;
    free_block_count = 1;
    for (i = 0; i < request_count; i++) {
        long rounded = minimum;
        int wanted = 0, found = -1, k;
        while (rounded < requests[i] && rounded <= total / 2) { rounded *= 2; wanted++; }
        if (rounded < requests[i]) { failed++; continue; }
        for (k = 0; k < free_block_count; k++) if (free_blocks[k].active && free_blocks[k].order >= wanted &&
            (found < 0 || free_blocks[k].order < free_blocks[found].order ||
             (free_blocks[k].order == free_blocks[found].order && free_blocks[k].offset < free_blocks[found].offset))) found = k;
        if (found < 0) { failed++; continue; }
        free_blocks[found].active = 0;
        while (free_blocks[found].order > wanted) {
            long half;
            free_blocks[found].order--;
            half = minimum << free_blocks[found].order;
            if (free_block_count >= BUDDY_FREE_MAX) { error_message("free-block table capacity exceeded"); return 1; }
            free_blocks[free_block_count].offset = free_blocks[found].offset + half;
            free_blocks[free_block_count].order = free_blocks[found].order;
            free_blocks[free_block_count].active = 1;
            free_block_count++;
        }
        allocations[i].requested = requests[i];
        allocations[i].block_size = rounded;
        allocations[i].offset = free_blocks[found].offset;
        allocations[i].active = 1;
        allocated++;
    }
    for (i = 0; i < free_count; i++) {
        long id = free_ids[i];
        int index = (int)id - 1, k;
        if (index < 0 || (size_t)index >= request_count || !allocations[index].active) {
            error_message("allocation ID to free is invalid or already freed");
            return 2;
        }
        allocations[index].active = 0;
        freed++;
        {
            long block_size = allocations[index].block_size;
            long offset = allocations[index].offset;
            int order = 0;
            while ((minimum << order) < block_size) order++;
            while (order < max_order) {
                long buddy_offset = offset ^ (minimum << order);
                int buddy = -1;
                for (k = 0; k < free_block_count; k++) if (free_blocks[k].active &&
                    free_blocks[k].order == order && free_blocks[k].offset == buddy_offset) { buddy = k; break; }
                if (buddy < 0) break;
                free_blocks[buddy].active = 0;
                if (buddy_offset < offset) offset = buddy_offset;
                order++;
            }
            if (free_block_count >= BUDDY_FREE_MAX) { error_message("free-block table capacity exceeded"); return 1; }
            free_blocks[free_block_count].offset = offset;
            free_blocks[free_block_count].order = order;
            free_blocks[free_block_count].active = 1;
            free_block_count++;
        }
    }
    for (i = 0; i < request_count; i++) if (allocations[i].block_size > 0) {
        if (allocations[i].active) {
            allocated_bytes += allocations[i].block_size;
            requested_bytes += allocations[i].requested;
        }
        printf("allocation_%zu: request=%ld block=%ld offset=%ld id=%zu status=%s\n", i + 1,
               allocations[i].requested, allocations[i].block_size, allocations[i].offset, i + 1,
               allocations[i].active ? "active" : "freed");
    }
    for (i = 0; i < (size_t)free_block_count; i++) if (free_blocks[i].active) {
        long size = minimum << free_blocks[i].order;
        total_free += size;
        if (size > largest_free) largest_free = size;
    }
    printf("mode: buddy\ntotal: %ld\nmin_block: %ld\nrequests: %zu\nallocated: %d\nfailed: %d\nfreed: %d\nallocated_bytes: %lld\nrequested_bytes: %lld\ninternal_fragmentation: %lld\ntotal_free: %lld\nlargest_free: %lld\ncoalesced: %s\n",
           total, minimum, request_count, allocated, failed, freed, allocated_bytes,
           requested_bytes, allocated_bytes - requested_bytes, total_free, largest_free,
           largest_free == total ? "yes" : "no");
    return 0;
}

static int slab_command(int argc, char **argv) {
    const char *classes_text = NULL, *objects_text = NULL, *ops_text = NULL;
    long page_size = 0, classes[LIST_MAX], objects[LIST_MAX];
    unsigned long long slabs[LIST_MAX] = {0};
    SlabObject object_records[LIST_MAX] = {{0}};
    size_t class_count = 0, input_count = 0, object_count = 0, i;
    unsigned seen = 0;
    long long slab_bytes = 0, requested_bytes = 0, allocated_bytes = 0;
    int allocated = 0, failed = 0, freed = 0, parsed;
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
        } else if (strcmp(argv[i], "--ops") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 8U)) return 2;
            ops_text = argv[++i]; seen |= 8U;
        } else return 2;
    }
    if ((seen & 3U) != 3U || ((seen & 4U) != 0 && (seen & 8U) != 0) || ((seen & 4U) == 0 && (seen & 8U) == 0)) return 2;
    parsed = parse_list(classes_text, classes, &class_count, 1, VALUE_MAX);
    if (parsed < 0) { error_message("out of memory"); return 1; }
    if (parsed == 0) return 2;
    for (i = 0; i < class_count; i++) {
        if (classes[i] > page_size || (i > 0 && classes[i] <= classes[i - 1])) return 2;
    }
    if (objects_text != NULL) {
        parsed = parse_list(objects_text, objects, &input_count, 1, VALUE_MAX);
        if (parsed < 0) { error_message("out of memory"); return 1; }
        if (parsed == 0) return 2;
        for (i = 0; i < input_count; i++) {
            int selected_class = -1;
            if (slab_allocate_object(objects[i], page_size, classes, class_count, slabs,
                                     object_records, &object_count, &selected_class)) allocated++;
            else failed++;
        }
    } else {
        char *copy, *save = NULL, *token;
        size_t operation_count = 0;
        size_t ops_length = strlen(ops_text);
        if (ops_length == 0 || ops_text[0] == ',' || ops_text[ops_length - 1] == ',' || strstr(ops_text, ",,") != NULL) {
            error_message("invalid slab operation list"); return 2;
        }
        copy = malloc(ops_length + 1);
        if (copy == NULL) { error_message("out of memory"); return 1; }
        strcpy(copy, ops_text);
        for (token = strtok_r(copy, ",", &save); token != NULL; token = strtok_r(NULL, ",", &save)) {
            char *operation = token, *colon, *end;
            long value;
            while (isspace((unsigned char)*operation)) operation++;
            end = operation + strlen(operation);
            while (end > operation && isspace((unsigned char)end[-1])) *--end = '\0';
            colon = strchr(operation, ':');
            if (++operation_count > LIST_MAX || colon == NULL || strchr(colon + 1, ':') != NULL) {
                free(copy); error_message("invalid slab operation list"); return 2;
            }
            *colon++ = '\0';
            if (strcmp(operation, "alloc") == 0) {
                int selected_class = -1;
                if (!parse_long_value(colon, 1, VALUE_MAX, &value)) { free(copy); error_message("invalid object size"); return 2; }
                input_count++;
                if (slab_allocate_object(value, page_size, classes, class_count, slabs,
                                         object_records, &object_count, &selected_class)) allocated++;
                else failed++;
            } else if (strcmp(operation, "free") == 0) {
                size_t object_index;
                int found = -1;
                if (!parse_long_value(colon, 1, LIST_MAX, &value)) { free(copy); error_message("invalid object ID"); return 2; }
                for (object_index = 0; object_index < object_count; object_index++)
                    if ((long)object_index + 1 == value && object_records[object_index].active) { found = (int)object_index; break; }
                if (found < 0) { free(copy); error_message("object ID is invalid or already freed"); return 2; }
                object_records[found].active = 0;
                freed++;
            } else { free(copy); error_message("slab operation must be alloc or free"); return 2; }
        }
        free(copy);
        if (input_count == 0) return 2;
    }
    {
        unsigned long long slab_count = 0;
        unsigned long long total_slots = 0, in_use = 0;
        for (i = 0; i < class_count; i++) {
            slab_count += slabs[i];
            total_slots += slabs[i] * (unsigned long long)(page_size / classes[i]);
            slab_bytes += (long long)slabs[i] * page_size;
        }
        for (i = 0; i < object_count; i++) {
            SlabObject *object = &object_records[i];
            printf("object_%zu: class=%ld slab=%llu slot=%llu status=%s\n", i + 1,
                   classes[object->class_index], object->slab_index, object->slot_index,
                   object->active ? "active" : "freed");
            if (object->active) {
                in_use++;
                requested_bytes += object->requested;
                allocated_bytes += classes[object->class_index];
            }
        }
        printf("mode: slab\npage_size: %ld\nclasses: %zu\nobjects: %zu\nallocated: %d\nfailed: %d\nfreed: %d\nslabs: %llu\nslots_in_use: %llu\nfree_slots: %llu\nrequested_bytes: %lld\nallocated_bytes: %lld\ninternal_fragmentation: %lld\nslab_waste: %lld\n",
               page_size, class_count, input_count, allocated, failed, freed, slab_count,
               in_use, total_slots - in_use, requested_bytes, allocated_bytes,
               allocated_bytes - requested_bytes, slab_bytes - allocated_bytes);
    }
    return 0;
}

int main(int argc, char **argv) {
    int result;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("usage:\n  memlab contiguous --policy first|best|worst --holes \"KB ...\" --requests \"KB ...\"\n  memlab buddy --total KB --min-block KB --requests \"KB ...\" [--free \"ID ...\"]\n  memlab slab --page-size BYTES --classes \"BYTES ...\" --objects \"BYTES ...\"\n  memlab slab --page-size BYTES --classes \"BYTES ...\" --ops \"alloc:SIZE,free:ID,...\"");
        return 0;
    }
    if (argc < 2) { error_message("usage: memlab contiguous|buddy|slab ... (use --help)"); return 2; }
    if (strcmp(argv[1], "contiguous") == 0) result = contiguous_command(argc - 2, argv + 2);
    else if (strcmp(argv[1], "buddy") == 0) result = buddy_command(argc - 2, argv + 2);
    else if (strcmp(argv[1], "slab") == 0) result = slab_command(argc - 2, argv + 2);
    else result = 2;
    if (result == 2) error_message("invalid command, option, or value");
    else if (result == 1) error_message("runtime failure");
    return result;
}
