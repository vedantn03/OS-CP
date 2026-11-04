#ifndef DISK_H
#define DISK_H

#include "disk_sched.h"

#define DISK_QUEUE_MAX 4096

typedef enum {
    DISK_REQ_SUBMIT,
    DISK_REQ_WRITEBACK
} DiskReqKind;

void disk_init(void);
int disk_enqueue(int cylinder, DiskReqKind kind);
void disk_service(void);
void disk_finish(void);
int disk_cyl_for_submission(int pid, int question_id);
int disk_cyl_for_page(int slot, int page);

#endif
