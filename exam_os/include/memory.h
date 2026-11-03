#ifndef MEMORY_H
#define MEMORY_H

#include "shared.h"
#include <stddef.h>

void  memory_init();
void *memory_thread(void *arg);
int   memory_access(int pid, int virtual_page);
int   memory_access_rw(int pid, int virtual_page, int is_write);
void  memory_free_process(int pid);
int   memory_audit(char *msg, size_t msglen);
void  memory_write_trace(void);

#endif // MEMORY_H