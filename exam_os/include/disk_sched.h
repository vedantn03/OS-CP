#ifndef DISK_SCHED_H
#define DISK_SCHED_H

#include <stddef.h>

typedef enum {
    DISK_FIFO,
    DISK_LIFO,
    DISK_SSTF,
    DISK_SCAN,
    DISK_CSCAN
} DiskAlgo;

typedef enum {
    DISK_DIR_UP,
    DISK_DIR_DOWN
} DiskDir;

int disk_algo_parse(const char *s, DiskAlgo *out);
const char *disk_algo_name(DiskAlgo a);
long disk_sched_run(DiskAlgo algo, int ncyl, int head, DiskDir dir,
                    const int *req, int n, int *order_out,
                    int *final_head, DiskDir *final_dir);

#endif
