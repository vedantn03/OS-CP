#ifndef PROCESS_H
#define PROCESS_H

#include "shared.h"

int process_slot(int pid);
int process_set_state(PCB *p, ProcessState to);
int process_terminate(int pid, int timed_out);

#endif // PROCESS_H
