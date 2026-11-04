#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCKS 512
#define MAX_FILES 64
#define MAX_OPS 256
#define NAME_SIZE 32

typedef enum { ALLOC_CONTIGUOUS, ALLOC_LINKED, ALLOC_INDEXED } AllocationMethod;

typedef struct {
    char name[NAME_SIZE];
    int size;
    int index_block;
    int blocks[MAX_BLOCKS];
    int block_count;
    int active;
} FileEntry;

static void error_message(const char *message) { fprintf(stderr, "fslab: %s\n", message); }

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

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
    return text;
}

static int valid_name(const char *name) {
    size_t i, length = strlen(name);
    if (length == 0 || length >= NAME_SIZE) return 0;
    for (i = 0; i < length; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-') return 0;
    return 1;
}

static int name_exists(const FileEntry files[MAX_FILES], size_t count, const char *name) {
    size_t i;
    for (i = 0; i < count; i++) if (strcmp(files[i].name, name) == 0) return 1;
    return 0;
}

static int parse_files(const char *text, FileEntry files[MAX_FILES], size_t *count) {
    char *copy, *save = NULL, *token;
    *count = 0;
    if (text == NULL || *text == '\0') return 0;
    copy = malloc(strlen(text) + 1);
    if (copy == NULL) return -1;
    strcpy(copy, text);
    for (token = strtok_r(copy, ",", &save); token != NULL; token = strtok_r(NULL, ",", &save)) {
        char *item = trim(token), *colon = strchr(item, ':');
        long size;
        if (*count >= MAX_FILES || colon == NULL || strchr(colon + 1, ':') != NULL) { free(copy); return 0; }
        *colon++ = '\0';
        item = trim(item); colon = trim(colon);
        if (!valid_name(item) || name_exists(files, *count, item) ||
            !parse_long_value(colon, 1, MAX_BLOCKS, &size)) { free(copy); return 0; }
        strcpy(files[*count].name, item);
        files[*count].size = (int)size;
        files[*count].index_block = -1;
        files[*count].active = 0;
        files[*count].block_count = 0;
        (*count)++;
    }
    free(copy);
    return *count == 0 ? 0 : 1;
}

static int parse_free_names(const char *text, char names[MAX_FILES][NAME_SIZE], size_t *count) {
    char *copy, *save = NULL, *token;
    *count = 0;
    if (text == NULL || *text == '\0') return 1;
    copy = malloc(strlen(text) + 1);
    if (copy == NULL) return -1;
    strcpy(copy, text);
    for (token = strtok_r(copy, ",", &save); token != NULL; token = strtok_r(NULL, ",", &save)) {
        char *name = trim(token);
        size_t i;
        if (*count >= MAX_FILES || !valid_name(name)) { free(copy); return 0; }
        for (i = 0; i < *count; i++) if (strcmp(names[i], name) == 0) { free(copy); return 0; }
        strcpy(names[(*count)++], name);
    }
    free(copy);
    return 1;
}

static int allocate_contiguous(unsigned char bitmap[MAX_BLOCKS], int total, FileEntry *file) {
    int start, run, i;
    for (start = 0; start + file->size <= total; start++) {
        for (run = 0; run < file->size && !bitmap[start + run]; run++) { }
        if (run == file->size) {
            for (i = 0; i < file->size; i++) { bitmap[start + i] = 1; file->blocks[i] = start + i; }
            file->block_count = file->size;
            file->active = 1;
            return 1;
        }
        start += run;
    }
    return 0;
}

static int allocate_scattered(unsigned char bitmap[MAX_BLOCKS], int total, FileEntry *file,
                              int indexed) {
    int needed = file->size + indexed, count = 0, i;
    int selected[MAX_BLOCKS];
    if (needed > total) return 0;
    for (i = 0; i < total && count < needed; i++) if (!bitmap[i]) selected[count++] = i;
    if (count != needed) return 0;
    if (indexed) file->index_block = selected[0];
    for (i = 0; i < file->size; i++) file->blocks[i] = selected[i + indexed];
    file->block_count = file->size;
    file->active = 1;
    for (i = 0; i < needed; i++) bitmap[selected[i]] = 1;
    return 1;
}

static int find_file(FileEntry files[MAX_FILES], size_t count, const char *name) {
    size_t i;
    for (i = 0; i < count; i++) if (files[i].active && strcmp(files[i].name, name) == 0) return (int)i;
    return -1;
}

static int free_space_stats(const unsigned char bitmap[MAX_BLOCKS], int total, int *largest_run) {
    int free_blocks = 0, run = 0, i;
    *largest_run = 0;
    for (i = 0; i < total; i++) {
        if (!bitmap[i]) {
            free_blocks++;
            run++;
            if (run > *largest_run) *largest_run = run;
        } else run = 0;
    }
    return free_blocks;
}

static void release_file(unsigned char bitmap[MAX_BLOCKS], FileEntry *file) {
    int i;
    for (i = 0; i < file->block_count; i++) bitmap[file->blocks[i]] = 0;
    if (file->index_block >= 0) bitmap[file->index_block] = 0;
    file->active = 0;
}

static void print_directory(const FileEntry files[MAX_FILES], size_t count,
                            AllocationMethod method) {
    size_t i;
    int first = 1, j;
    printf("directory:");
    for (i = 0; i < count; i++) if (files[i].active) {
        printf("%s%s(size=%d,", first ? " " : "; ", files[i].name, files[i].size);
        if (method == ALLOC_INDEXED) printf("index=%d)", files[i].index_block);
        else printf("start=%d)", files[i].blocks[0]);
        first = 0;
    }
    if (first) printf(" empty");
    printf("\nlayout:");
    first = 1;
    for (i = 0; i < count; i++) if (files[i].active) {
        printf("%s%s=", first ? " " : "; ", files[i].name);
        if (method == ALLOC_INDEXED) printf("index:%d[", files[i].index_block);
        for (j = 0; j < files[i].block_count; j++) {
            if (j) printf(method == ALLOC_LINKED ? "->" : ",");
            printf("%d", files[i].blocks[j]);
        }
        if (method == ALLOC_INDEXED) printf("]");
        first = 0;
    }
    if (first) printf(" empty");
    printf("\n");
}

static int run(int argc, char **argv) {
    const char *method_text = NULL, *files_text = NULL, *free_text = NULL, *ops_text = NULL;
    unsigned char bitmap[MAX_BLOCKS] = {0};
    FileEntry files[MAX_FILES];
    char free_names[MAX_FILES][NAME_SIZE];
    long blocks = 0;
    size_t file_count = 0, free_count = 0, i;
    unsigned seen = 0;
    AllocationMethod method;
    int allocated = 0, failed = 0, freed = 0, files_requested = 0, free_blocks = 0, largest_extent = 0;
    int external_fragmentation = 0, parsed;
    memset(files, 0, sizeof(files));
    for (i = 0; i < (size_t)argc; i++) {
        if (strcmp(argv[i], "--blocks") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 1U) || !parse_long_value(argv[i + 1], 1, MAX_BLOCKS, &blocks)) return 2;
            i++; seen |= 1U;
        } else if (strcmp(argv[i], "--method") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 2U)) return 2;
            method_text = argv[++i]; seen |= 2U;
        } else if (strcmp(argv[i], "--files") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 4U)) return 2;
            files_text = argv[++i]; seen |= 4U;
        } else if (strcmp(argv[i], "--free") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 8U)) return 2;
            free_text = argv[++i]; seen |= 8U;
        } else if (strcmp(argv[i], "--ops") == 0) {
            if (i + 1 >= (size_t)argc || (seen & 16U)) return 2;
            ops_text = argv[++i]; seen |= 16U;
        } else return 2;
    }
    if ((seen & 3U) != 3U || ((seen & 4U) == 0 && (seen & 16U) == 0) ||
        ((seen & 4U) != 0 && (seen & 16U) != 0) ||
        ((seen & 8U) != 0 && ((seen & 4U) == 0 || (seen & 16U) != 0))) return 2;
    if (strcmp(method_text, "contiguous") == 0) method = ALLOC_CONTIGUOUS;
    else if (strcmp(method_text, "linked") == 0) method = ALLOC_LINKED;
    else if (strcmp(method_text, "indexed") == 0) method = ALLOC_INDEXED;
    else return 2;
    if (files_text != NULL) {
        parsed = parse_files(files_text, files, &file_count);
        if (parsed < 0) { error_message("out of memory"); return 1; }
        if (parsed == 0) { error_message("invalid file list (check names, sizes, and duplicates)"); return 2; }
        files_requested = (int)file_count;
        parsed = parse_free_names(free_text, free_names, &free_count);
        if (parsed < 0) { error_message("out of memory"); return 1; }
        if (parsed == 0) { error_message("invalid free list"); return 2; }
        for (i = 0; i < file_count; i++) {
            int success;
            if (method == ALLOC_CONTIGUOUS) success = allocate_contiguous(bitmap, (int)blocks, &files[i]);
            else success = allocate_scattered(bitmap, (int)blocks, &files[i], method == ALLOC_INDEXED);
            if (success) allocated++;
            else {
                failed++;
                if (method == ALLOC_CONTIGUOUS) {
                    int largest;
                    if (free_space_stats(bitmap, (int)blocks, &largest) >= files[i].size && largest < files[i].size)
                        external_fragmentation = 1;
                }
            }
        }
        for (i = 0; i < free_count; i++) {
            int position = find_file(files, file_count, free_names[i]);
            if (position < 0) {
                error_message("file to free was not found or was already freed");
                return 2;
            }
        }
        for (i = 0; i < free_count; i++) {
            int position = find_file(files, file_count, free_names[i]);
            release_file(bitmap, &files[position]);
            freed++;
        }
    } else {
        char *copy, *save = NULL, *token;
        size_t operation_count = 0;
        size_t ops_length = strlen(ops_text);
        if (ops_length == 0 || ops_text[0] == ',' || ops_text[ops_length - 1] == ',' || strstr(ops_text, ",,") != NULL) {
            error_message("invalid file operation list"); return 2;
        }
        copy = malloc(ops_length + 1);
        if (copy == NULL) { error_message("out of memory"); return 1; }
        strcpy(copy, ops_text);
        for (token = strtok_r(copy, ",", &save); token != NULL; token = strtok_r(NULL, ",", &save)) {
            char *operation = trim(token), *first_colon = strchr(operation, ':'), *second_colon;
            if (++operation_count > MAX_OPS || first_colon == NULL) {
                free(copy); error_message("invalid file operation list"); return 2;
            }
            *first_colon++ = '\0';
            second_colon = strchr(first_colon, ':');
            if (strcmp(operation, "alloc") == 0) {
                long size;
                FileEntry *file;
                int success, file_position = -1;
                size_t slot;
                if (second_colon == NULL || strchr(second_colon + 1, ':') != NULL) {
                    free(copy); error_message("invalid allocation operation"); return 2;
                }
                *second_colon++ = '\0';
                first_colon = trim(first_colon);
                second_colon = trim(second_colon);
                if (!valid_name(first_colon) || !parse_long_value(second_colon, 1, MAX_BLOCKS, &size)) {
                    free(copy); error_message("invalid allocation name or size"); return 2;
                }
                if (find_file(files, file_count, first_colon) >= 0) {
                    free(copy); error_message("duplicate active file name"); return 2;
                }
                for (slot = 0; slot < file_count; slot++) if (!files[slot].active) { file_position = (int)slot; break; }
                if (file_position < 0 && file_count < MAX_FILES) file_position = (int)file_count++;
                if (file_position < 0) { free(copy); error_message("directory capacity exceeded"); return 1; }
                file = &files[file_position];
                memset(file, 0, sizeof(*file));
                strcpy(file->name, first_colon);
                file->size = (int)size;
                file->index_block = -1;
                files_requested++;
                if (method == ALLOC_CONTIGUOUS) success = allocate_contiguous(bitmap, (int)blocks, file);
                else success = allocate_scattered(bitmap, (int)blocks, file, method == ALLOC_INDEXED);
                if (success) allocated++;
                else {
                    failed++;
                    if (method == ALLOC_CONTIGUOUS) {
                        int largest;
                        if (free_space_stats(bitmap, (int)blocks, &largest) >= file->size && largest < file->size)
                            external_fragmentation = 1;
                    }
                }
            } else if (strcmp(operation, "free") == 0) {
                int position;
                if (second_colon != NULL || !valid_name(trim(first_colon))) {
                    free(copy); error_message("invalid free operation"); return 2;
                }
                position = find_file(files, file_count, trim(first_colon));
                if (position < 0) { free(copy); error_message("file to free was not found or was already freed"); return 2; }
                release_file(bitmap, &files[position]);
                freed++;
            } else { free(copy); error_message("file operation must be alloc or free"); return 2; }
        }
        free(copy);
        if (operation_count == 0) return 2;
    }
    free_blocks = free_space_stats(bitmap, (int)blocks, &largest_extent);
    printf("mode: fslab\nmethod: %s\nblocks: %ld\nfiles_requested: %d\nfiles_allocated: %d\nfiles_failed: %d\nfiles_freed: %d\n",
           method_text, blocks, files_requested, allocated, failed, freed);
    print_directory(files, file_count, method);
    printf("free_blocks: %d\nlargest_free_extent: %d\nexternal_fragmentation: %s\nbitmap: ", free_blocks, largest_extent,
           external_fragmentation ? "yes" : "no");
    for (i = 0; i < (size_t)blocks; i++) putchar(bitmap[i] ? '1' : '0');
    putchar('\n');
    if (failed != 0) {
        error_message("one or more files could not be allocated (capacity or external fragmentation)");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int result;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("usage:\n  fslab --blocks N --method contiguous|linked|indexed --files name:blocks,... [--free name,...]\n  fslab --blocks N --method contiguous|linked|indexed --ops \"alloc:name:blocks,free:name,...\"");
        return 0;
    }
    result = run(argc - 1, argv + 1);
    if (result == 2) error_message("invalid command or option (use --help)");
    return result;
}
