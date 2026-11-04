#include "disk.h"

#include <stdio.h>
#include <pthread.h>

#include "shared.h"

static int pending_cylinders[DISK_QUEUE_MAX];
static DiskReqKind pending_kinds[DISK_QUEUE_MAX];
static int pending_count;
static int disk_head;
static DiskDir disk_dir;
static FILE *trace_file;
static int disk_initialized;
static pthread_mutex_t disk_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t svc_lock = PTHREAD_MUTEX_INITIALIZER;

void disk_init(void) {
    pthread_mutex_lock(&disk_lock);
    pending_count = 0;
    pthread_mutex_unlock(&disk_lock);
    pthread_mutex_lock(&svc_lock);
    disk_head = g_config.disk_start;
    disk_dir = DISK_DIR_UP;
    if (g_config.disk_trace) {
        trace_file = fopen("output/disktrace.txt", "w");
        if (trace_file == NULL) {
            fprintf(stderr, "WARNING: Could not open output/disktrace.txt\n");
            g_config.disk_trace = 0;
        } else {
            fprintf(trace_file, "# cylinders=%d start=%d algo=%s\n",
                    g_config.disk_cylinders, g_config.disk_start,
                    disk_algo_name(g_config.disk_algo));
            fflush(trace_file);
        }
    }
    disk_initialized = 1;
    pthread_mutex_unlock(&svc_lock);
}

int disk_enqueue(int cylinder, DiskReqKind kind) {
    int result = 0;

    pthread_mutex_lock(&disk_lock);
    pthread_mutex_lock(&g_state.lock);
    g_state.disk_requests++;
    if (kind == DISK_REQ_WRITEBACK)
        g_state.disk_wb_requests++;
    if (pending_count >= DISK_QUEUE_MAX) {
        g_state.disk_dropped++;
        result = -1;
    } else {
        pending_cylinders[pending_count] = cylinder;
        pending_kinds[pending_count] = kind;
        pending_count++;
        if (pending_count > g_state.disk_max_queue)
            g_state.disk_max_queue = pending_count;
    }
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&disk_lock);
    return result;
}

void disk_service(void) {
    int batch[DISK_QUEUE_MAX];
    int order[DISK_QUEUE_MAX];
    int count;
    long movement;

    pthread_mutex_lock(&svc_lock);
    pthread_mutex_lock(&disk_lock);
    count = pending_count;
    for (int i = 0; i < count; i++)
        batch[i] = pending_cylinders[i];
    pending_count = 0;
    pthread_mutex_unlock(&disk_lock);
    if (count == 0) {
        pthread_mutex_unlock(&svc_lock);
        return;
    }

    movement = disk_sched_run(g_config.disk_algo, g_config.disk_cylinders,
                              disk_head, disk_dir, batch, count, order,
                              &disk_head, &disk_dir);
    if (movement < 0) {
        fprintf(stderr, "WARNING: Disk service rejected a request batch\n");
        pthread_mutex_lock(&g_state.lock);
        g_state.disk_dropped += count;
        pthread_mutex_unlock(&g_state.lock);
        pthread_mutex_unlock(&svc_lock);
        return;
    }
    if (trace_file != NULL) {
        for (int i = 0; i < count; i++)
            fprintf(trace_file, "%s%d", i == 0 ? "" : " ", batch[i]);
        fputc('\n', trace_file);
        fflush(trace_file);
    }
    pthread_mutex_lock(&g_state.lock);
    g_state.disk_movement += movement;
    g_state.disk_served += count;
    g_state.disk_batches++;
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&svc_lock);
}

void disk_finish(void) {
    pthread_mutex_lock(&svc_lock);
    if (!disk_initialized) {
        pthread_mutex_unlock(&svc_lock);
        return;
    }
    pthread_mutex_unlock(&svc_lock);
    disk_service();
    pthread_mutex_lock(&svc_lock);
    if (trace_file != NULL) {
        fclose(trace_file);
        trace_file = NULL;
    }
    disk_initialized = 0;
    pthread_mutex_unlock(&svc_lock);
}

int disk_cyl_for_submission(int pid, int question_id) {
    int value = (pid * 131 + question_id * 17) % g_config.disk_cylinders;
    return value < 0 ? value + g_config.disk_cylinders : value;
}

int disk_cyl_for_page(int slot, int page) {
    int base = g_config.disk_cylinders * 3 / 4;
    int span = g_config.disk_cylinders - base;
    int value = (slot * g_config.pages_per_proc + page) % span;
    if (value < 0)
        value += span;
    return base + value;
}
