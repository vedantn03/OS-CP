#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESOURCES 16
#define MAX_SESSIONS 64
#define NAME_SIZE 32

typedef struct {
    char name[NAME_SIZE];
    int allocation[MAX_RESOURCES];
    int maximum[MAX_RESOURCES];
} Session;

typedef struct {
    int resources;
    int available[MAX_RESOURCES];
    Session sessions[MAX_SESSIONS];
    int count;
} Scenario;

static void trim(char *text) {
    char *start = text;
    size_t length;
    while (isspace((unsigned char)*start))
        start++;
    if (start != text)
        memmove(text, start, strlen(start) + 1);
    length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        text[--length] = '\0';
}

static int parse_vector(char **cursor, int count, int *values) {
    for (int i = 0; i < count; i++) {
        char *end;
        long value;
        while (isspace((unsigned char)**cursor))
            (*cursor)++;
        errno = 0;
        value = strtol(*cursor, &end, 10);
        if (end == *cursor || errno == ERANGE || value < 0 || value > 1000000)
            return 0;
        values[i] = (int)value;
        *cursor = end;
    }
    return 1;
}

static int parse_line(char *line, Scenario *scenario) {
    char *cursor;
    char *token;

    trim(line);
    if (*line == '\0' || *line == '#')
        return 1;
    cursor = line;
    token = strtok_r(cursor, " \t,", &cursor);
    if (token == NULL)
        return 1;
    if (strcmp(token, "resources") == 0) {
        char *end;
        long count;
        token = strtok_r(NULL, " \t,", &cursor);
        if (token == NULL)
            return 0;
        errno = 0;
        count = strtol(token, &end, 10);
        if (errno == ERANGE || *end != '\0' || count < 1 ||
            count > MAX_RESOURCES || scenario->resources != 0)
            return 0;
        scenario->resources = (int)count;
        return 1;
    }
    if (scenario->resources == 0 ||
        (strcmp(token, "available") != 0 &&
         strcmp(token, "session") != 0))
        return 0;
    if (strcmp(token, "available") == 0) {
        if (scenario->count != 0)
            return 0;
        return parse_vector(&cursor, scenario->resources, scenario->available);
    }
    if (scenario->count == MAX_SESSIONS)
        return 0;
    token = strtok_r(NULL, " \t,", &cursor);
    if (token == NULL || strlen(token) >= NAME_SIZE)
        return 0;
    strcpy(scenario->sessions[scenario->count].name, token);
    token = strtok_r(NULL, " \t,", &cursor);
    if (token == NULL || strcmp(token, "allocation") != 0)
        return 0;
    if (!parse_vector(&cursor, scenario->resources,
                      scenario->sessions[scenario->count].allocation))
        return 0;
    token = strtok_r(NULL, " \t,", &cursor);
    if (token == NULL || strcmp(token, "maximum") != 0)
        return 0;
    if (!parse_vector(&cursor, scenario->resources,
                      scenario->sessions[scenario->count].maximum))
        return 0;
    for (int i = 0; i < scenario->resources; i++)
        if (scenario->sessions[scenario->count].maximum[i] <
            scenario->sessions[scenario->count].allocation[i])
            return 0;
    scenario->count++;
    return 1;
}

static int load_text(const char *text, Scenario *scenario) {
    char *copy = malloc(strlen(text) + 1);
    char *line;
    char *save = NULL;
    int valid = 1;
    if (copy == NULL)
        return 0;
    strcpy(copy, text);
    for (line = strtok_r(copy, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        if (!parse_line(line, scenario)) {
            valid = 0;
            break;
        }
    }
    free(copy);
    return valid && scenario->resources > 0 && scenario->count > 0;
}

static int read_file(const char *path, char **text) {
    FILE *file = fopen(path, "rb");
    long length;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL)
            fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    *text = malloc((size_t)length + 1);
    if (*text == NULL || fread(*text, 1, (size_t)length, file) != (size_t)length) {
        free(*text);
        *text = NULL;
        fclose(file);
        return 0;
    }
    (*text)[length] = '\0';
    fclose(file);
    return 1;
}

static int banker(const Scenario *scenario, int *sequence, int *finished,
                  int *sequence_count) {
    int work[MAX_RESOURCES];
    int done[MAX_SESSIONS] = {0};
    int complete = 0;
    for (int r = 0; r < scenario->resources; r++)
        work[r] = scenario->available[r];
    while (complete < scenario->count) {
        int progress = 0;
        for (int s = 0; s < scenario->count; s++) {
            if (done[s])
                continue;
            int possible = 1;
            for (int r = 0; r < scenario->resources; r++) {
                int need = scenario->sessions[s].maximum[r] -
                           scenario->sessions[s].allocation[r];
                if (need > work[r])
                    possible = 0;
            }
            if (!possible)
                continue;
            for (int r = 0; r < scenario->resources; r++)
                work[r] += scenario->sessions[s].allocation[r];
            done[s] = 1;
            sequence[complete++] = s;
            progress = 1;
        }
        if (!progress)
            break;
    }
    for (int s = 0; s < scenario->count; s++)
        finished[s] = done[s];
    *sequence_count = complete;
    return complete == scenario->count;
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *path = NULL;
    char *file_text = NULL;
    Scenario scenario = {0};
    int sequence[MAX_SESSIONS];
    int finished[MAX_SESSIONS];
    int sequence_count;
    int safe;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--string") == 0 && i + 1 < argc)
            input = argv[++i];
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
            path = argv[++i];
        else
            return fprintf(stderr, "usage: deadsim --string TEXT | --file PATH\n"), 2;
    }
    if ((input == NULL) == (path == NULL))
        return fprintf(stderr, "provide exactly one input\n"), 2;
    if (path != NULL && !read_file(path, &file_text))
        return fprintf(stderr, "could not read input\n"), 1;
    if (!load_text(input != NULL ? input : file_text, &scenario)) {
        free(file_text);
        return fprintf(stderr, "invalid scenario\n"), 1;
    }
    free(file_text);
    safe = banker(&scenario, sequence, finished, &sequence_count);
    printf("resources: %d\nsessions: %d\nsafe: %s\nsequence:", scenario.resources,
           scenario.count, safe ? "yes" : "no");
    for (int i = 0; i < sequence_count; i++)
        if (finished[sequence[i]])
            printf(" %s", scenario.sessions[sequence[i]].name);
    printf("\ndeadlocked:");
    for (int i = 0; i < scenario.count; i++)
        if (!finished[i])
            printf(" %s", scenario.sessions[i].name);
    printf("\n");
    return safe ? 0 : 1;
}
