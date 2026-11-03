#include "disk_sched.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int min, int max, int *value) {
    char *end;
    long parsed;

    if (text == NULL || *text == '\0')
        return 0;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed < min || parsed > max)
        return 0;
    *value = (int)parsed;
    return 1;
}

static int parse_dir(const char *text, DiskDir *dir) {
    if (text == NULL)
        return 0;
    if (strcasecmp(text, "up") == 0)
        *dir = DISK_DIR_UP;
    else if (strcasecmp(text, "down") == 0)
        *dir = DISK_DIR_DOWN;
    else
        return 0;
    return 1;
}

static int append_request(int **requests, size_t *count, size_t *capacity,
                          int value) {
    if (*count == *capacity) {
        size_t next = *capacity == 0 ? 16 : *capacity * 2;
        int *grown = realloc(*requests, next * sizeof(*grown));
        if (grown == NULL)
            return 0;
        *requests = grown;
        *capacity = next;
    }
    (*requests)[(*count)++] = value;
    return 1;
}

static int parse_requests(const char *text, int cylinders, int **requests,
                          size_t *count) {
    const char *cursor = text;
    size_t capacity = 0;

    while (*cursor != '\0') {
        char *end;
        long value;
        while (*cursor != '\0' &&
               (isspace((unsigned char)*cursor) || *cursor == ','))
            cursor++;
        if (*cursor == '\0')
            break;
        errno = 0;
        value = strtol(cursor, &end, 10);
        if (end == cursor || errno == ERANGE || value < 0 ||
            value >= cylinders) {
            free(*requests);
            *requests = NULL;
            *count = 0;
            return 0;
        }
        if (*end != '\0' && !isspace((unsigned char)*end) && *end != ',') {
            free(*requests);
            *requests = NULL;
            *count = 0;
            return 0;
        }
        if (!append_request(requests, count, &capacity, (int)value)) {
            free(*requests);
            *requests = NULL;
            *count = 0;
            return 0;
        }
        cursor = end;
    }
    return 1;
}

static int read_file(const char *path, char **contents) {
    FILE *file = fopen(path, "rb");
    size_t length = 0;
    size_t capacity = 4096;

    if (file == NULL)
        return 0;
    *contents = malloc(capacity);
    if (*contents == NULL) {
        fclose(file);
        return 0;
    }
    for (;;) {
        size_t got = fread(*contents + length, 1, capacity - length - 1,
                           file);
        length += got;
        if (ferror(file)) {
            free(*contents);
            *contents = NULL;
            fclose(file);
            return 0;
        }
        if (got == 0)
            break;
        if (length + 1 == capacity) {
            char *grown;
            capacity *= 2;
            grown = realloc(*contents, capacity);
            if (grown == NULL) {
                free(*contents);
                *contents = NULL;
                fclose(file);
                return 0;
            }
            *contents = grown;
        }
    }
    (*contents)[length] = '\0';
    fclose(file);
    return 1;
}

int main(int argc, char **argv) {
    DiskAlgo algo;
    DiskDir dir = DISK_DIR_UP;
    const char *input_string = NULL;
    const char *input_file = NULL;
    char *file_contents = NULL;
    int cylinders = 0;
    int head = 0;
    int have_algo = 0;
    int have_head = 0;
    int have_cylinders = 0;
    int *requests = NULL;
    size_t count = 0;
    int *order = NULL;
    int final_head;
    DiskDir final_dir;
    long movement;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--algo") == 0 && i + 1 < argc) {
            if (disk_algo_parse(argv[++i], &algo) != 0)
                return fprintf(stderr, "unknown algorithm\n"), 2;
            have_algo = 1;
        } else if (strcmp(argv[i], "--head") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], 0, 2147483647, &head))
                return fprintf(stderr, "invalid head\n"), 2;
            have_head = 1;
        } else if (strcmp(argv[i], "--cylinders") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], 1, 100000, &cylinders))
                return fprintf(stderr, "invalid cylinders\n"), 2;
            have_cylinders = 1;
        } else if (strcmp(argv[i], "--string") == 0 && i + 1 < argc) {
            input_string = argv[++i];
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            input_file = argv[++i];
        } else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            if (!parse_dir(argv[++i], &dir))
                return fprintf(stderr, "invalid direction\n"), 2;
        } else {
            return fprintf(stderr, "usage error\n"), 2;
        }
    }
    if (!have_algo || !have_head || !have_cylinders ||
        (input_string == NULL && input_file == NULL) ||
        (input_string != NULL && input_file != NULL) || head >= cylinders)
        return fprintf(stderr, "usage error\n"), 2;
    if (input_file != NULL) {
        if (!read_file(input_file, &file_contents)) {
            fprintf(stderr, "could not read input\n");
            return 1;
        }
        input_string = file_contents;
    }
    if (!parse_requests(input_string, cylinders, &requests, &count)) {
        free(file_contents);
        fprintf(stderr, "invalid request\n");
        return 1;
    }
    free(file_contents);
    if (count == 0) {
        free(requests);
        fprintf(stderr, "empty request list\n");
        return 1;
    }
    order = malloc(count * sizeof(*order));
    if (order == NULL) {
        free(requests);
        fprintf(stderr, "allocation failure\n");
        return 1;
    }
    movement = disk_sched_run(algo, cylinders, head, dir, requests,
                              (int)count, order, &final_head, &final_dir);
    if (movement < 0) {
        free(order);
        free(requests);
        fprintf(stderr, "scheduling failure\n");
        return 1;
    }
    printf("algo: %s\n", disk_algo_name(algo));
    printf("head: %d\n", head);
    printf("cylinders: %d\n", cylinders);
    printf("requests: %zu\n", count);
    printf("movement: %ld\n", movement);
    printf("final_head: %d\n", final_head);
    printf("final_dir: %s\n", final_dir == DISK_DIR_UP ? "up" : "down");
    printf("order:");
    for (size_t i = 0; i < count; i++)
        printf(" %d", requests[order[i]]);
    printf("\n");
    free(order);
    free(requests);
    return 0;
}
