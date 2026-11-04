#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "memory.h"
#include "disk.h"
#include "logger.h"
#include "clock.h"
#include "rng.h"
#include "process.h"
#include "config.h"

typedef struct {
    int pid;
    int virtual_page;
    int load_order;
    long last_accessed;
    int ref;
} Frame;

typedef struct {
    int pid;
    int page;
} PageTraceEntry;

static Frame frame_pool[MAX_FRAMES];
static int total_frames;
static int fifo_counter;
static int clock_hand;
static pthread_mutex_t mem_lock = PTHREAD_MUTEX_INITIALIZER;
static PageTableEntry page_tables[MAX_STUDENTS][MAX_PAGES];
static PageTraceEntry *page_trace_entries;
static size_t page_trace_count;
static int page_trace_warned;
static int thrash_window[THRASH_WINDOW];
static int thrash_count;
static int thrash_position;
static int last_thrash_pause_tick;

void memory_init(void) {
    total_frames = g_config.memory_frames;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;
    fifo_counter = 0;
    clock_hand = 0;
    thrash_count = 0;
    thrash_position = 0;
    last_thrash_pause_tick = -1;
    page_trace_count = 0;
    page_trace_warned = 0;
    page_trace_entries = NULL;
    if (g_config.page_trace) {
        page_trace_entries = malloc(PAGE_TRACE_MAX * sizeof(*page_trace_entries));
        if (!page_trace_entries)
            log_event("ERROR", "MEMORY", "Could not allocate page reference trace");
    }

    for (int i = 0; i < total_frames; i++) {
        frame_pool[i].pid = -1;
        frame_pool[i].virtual_page = -1;
        frame_pool[i].load_order = 0;
        frame_pool[i].last_accessed = 0;
        frame_pool[i].ref = 0;
    }
    for (int i = 0; i < MAX_STUDENTS; i++)
        for (int j = 0; j < MAX_PAGES; j++) {
            page_tables[i][j].virtual_page = j;
            page_tables[i][j].frame_number = -1;
            page_tables[i][j].valid = 0;
            page_tables[i][j].dirty = 0;
            page_tables[i][j].last_accessed = 0;
            page_tables[i][j].load_order = 0;
        }
    log_event("INFO", "MEMORY", "Memory subsystem initialized");
}

static int find_free_frame(void) {
    for (int i = 0; i < total_frames; i++)
        if (frame_pool[i].pid == -1) return i;
    return -1;
}

static int evict_fifo(void) {
    int selected = 0;
    for (int i = 1; i < total_frames; i++)
        if (frame_pool[i].load_order < frame_pool[selected].load_order)
            selected = i;
    return selected;
}

static int evict_lru(void) {
    int selected = 0;
    for (int i = 1; i < total_frames; i++)
        if (frame_pool[i].last_accessed < frame_pool[selected].last_accessed)
            selected = i;
    return selected;
}

static int evict_clock(void) {
    int steps = 0;
    while (steps++ < 2 * total_frames) {
        if (frame_pool[clock_hand].ref) {
            frame_pool[clock_hand].ref = 0;
            clock_hand = (clock_hand + 1) % total_frames;
        } else {
            int selected = clock_hand;
            clock_hand = (clock_hand + 1) % total_frames;
            return selected;
        }
    }
    return clock_hand;
}

static void record_outcome(int fault) {
    thrash_window[thrash_position] = fault;
    thrash_position = (thrash_position + 1) % THRASH_WINDOW;
    if (thrash_count < THRASH_WINDOW) thrash_count++;
}

static int thrash_update(void) {
    int faults = 0;
    if (thrash_count < THRASH_WINDOW) return 0;
    for (int i = 0; i < THRASH_WINDOW; i++) faults += thrash_window[i];
    int entered = 0;
    if (!g_state.thrashing &&
        faults * 100 >= g_config.thrash_high * THRASH_WINDOW) {
        g_state.thrashing = 1;
        g_state.thrash_episodes++;
        entered = 1;
    }
    if (g_state.thrashing &&
        faults * 100 < (g_config.thrash_high / 2) * THRASH_WINDOW)
        g_state.thrashing = 0;
    if (g_config.thrash_control && g_state.thrashing &&
        (entered || last_thrash_pause_tick != g_state.current_tick)) {
        int until = g_state.current_tick + g_config.thrash_pause;
        if (until > g_state.admission_paused_until_tick)
            g_state.admission_paused_until_tick = until;
        last_thrash_pause_tick = g_state.current_tick;
    }
    return entered;
}

static void load_page(int pid, int virtual_page, int frame, int is_write) {
    int prev_pid = frame_pool[frame].pid;
    int prev_page = frame_pool[frame].virtual_page;
    if (prev_pid >= 0 && prev_page >= 0) {
        page_tables[prev_pid][prev_page].valid = 0;
        page_tables[prev_pid][prev_page].frame_number = -1;
        if (page_tables[prev_pid][prev_page].dirty) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Dirty eviction: PID %d page %d → disk write",
                     prev_pid + 1, prev_page);
            pthread_mutex_lock(&g_state.lock);
            g_state.dirty_writebacks++;
            pthread_mutex_unlock(&g_state.lock);
            disk_enqueue(disk_cyl_for_page(prev_pid, prev_page),
                         DISK_REQ_WRITEBACK);
            log_event("WARN", "MEMORY", msg);
            page_tables[prev_pid][prev_page].dirty = 0;
        }
    }
    frame_pool[frame].pid = pid;
    frame_pool[frame].virtual_page = virtual_page;
    frame_pool[frame].load_order = fifo_counter++;
    frame_pool[frame].last_accessed = (long)clock_now_ms();
    frame_pool[frame].ref = 1;
    page_tables[pid][virtual_page].frame_number = frame;
    page_tables[pid][virtual_page].valid = 1;
    page_tables[pid][virtual_page].dirty = is_write;
    page_tables[pid][virtual_page].last_accessed = (long)clock_now_ms();
    page_tables[pid][virtual_page].load_order = frame_pool[frame].load_order;
    usleep(500);
}

int memory_access_rw(int pid, int virtual_page, int is_write) {
    int slot = process_slot(pid);
    if (slot < 0 || virtual_page < 0 || virtual_page >= MAX_PAGES) return -1;
    pthread_mutex_lock(&mem_lock);
    pthread_mutex_lock(&g_state.lock);
    int terminated = g_state.processes[slot].state == TERMINATED;
    pthread_mutex_unlock(&g_state.lock);
    if (terminated) {
        pthread_mutex_unlock(&mem_lock);
        return -1;
    }

    PageTableEntry *entry = &page_tables[slot][virtual_page];
    int fault = !entry->valid;
    int frame_number;
    char msg[128];
    if (!fault) {
        frame_number = entry->frame_number;
        entry->last_accessed = (long)clock_now_ms();
        frame_pool[frame_number].last_accessed = (long)clock_now_ms();
        frame_pool[frame_number].ref = 1;
        if (is_write) entry->dirty = 1;
    } else {
        frame_number = find_free_frame();
        if (frame_number < 0) {
            frame_number = g_config.page_algo == LRU ? evict_lru()
                : g_config.page_algo == FIFO ? evict_fifo() : evict_clock();
            pthread_mutex_lock(&g_state.lock);
            g_state.evictions++;
            pthread_mutex_unlock(&g_state.lock);
            snprintf(msg, sizeof(msg), "Evicting frame %d (%s)",
                     frame_number, page_algo_name(g_config.page_algo));
            log_event("INFO", "MEMORY", msg);
        }
        load_page(slot, virtual_page, frame_number, is_write);
    }

    int warn_thrash = 0;
    if (page_trace_entries) {
        if (page_trace_count < PAGE_TRACE_MAX) {
            page_trace_entries[page_trace_count++] = (PageTraceEntry){pid, virtual_page};
        } else if (!page_trace_warned) {
            page_trace_warned = 1;
            log_event("WARN", "MEMORY", "Page reference trace capacity exceeded");
        }
    }
    record_outcome(fault);
    pthread_mutex_lock(&g_state.lock);
    if (fault) g_state.page_faults++;
    else g_state.page_hits++;
    warn_thrash = thrash_update();
    int used = 0;
    for (int i = 0; i < total_frames; i++)
        if (frame_pool[i].pid != -1) used++;
    g_state.frames_used = used;
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&mem_lock);

    if (fault) {
        snprintf(msg, sizeof(msg), "Page fault: PID %d page %d", pid, virtual_page);
        log_event("WARN", "MEMORY", msg);
    }
    if (warn_thrash)
        log_event("WARN", "MEMORY", "Thrashing detected");
    return frame_number;
}

int memory_access(int pid, int virtual_page) {
    return memory_access_rw(pid, virtual_page, 0);
}

void memory_write_trace(void) {
    if (!g_config.page_trace) return;
    FILE *f = fopen("output/pagetrace.txt", "w");
    if (!f) {
        fprintf(stderr, "ERROR: Could not open output/pagetrace.txt\n");
    } else {
        for (size_t i = 0; i < page_trace_count; i++)
            fprintf(f, "%d:%d\n", page_trace_entries[i].pid,
                    page_trace_entries[i].page);
        if (fclose(f) != 0)
            fprintf(stderr, "ERROR: Could not close output/pagetrace.txt\n");
    }
    free(page_trace_entries);
    page_trace_entries = NULL;
}

void memory_free_process(int pid) {
    int slot = process_slot(pid);
    if (slot < 0) return;
    pthread_mutex_lock(&mem_lock);
    for (int i = 0; i < total_frames; i++) {
        if (frame_pool[i].pid == slot) {
            int vp = frame_pool[i].virtual_page;
            page_tables[slot][vp].valid = 0;
            page_tables[slot][vp].frame_number = -1;
            page_tables[slot][vp].dirty = 0;
            frame_pool[i].pid = -1;
            frame_pool[i].virtual_page = -1;
            frame_pool[i].ref = 0;
        }
    }
    pthread_mutex_lock(&g_state.lock);
    int used = 0;
    for (int i = 0; i < total_frames; i++)
        if (frame_pool[i].pid != -1) used++;
    g_state.frames_used = used;
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&mem_lock);
    char msg[64];
    snprintf(msg, sizeof(msg), "Freed all frames for PID %d", pid);
    log_event("INFO", "MEMORY", msg);
}

int memory_audit(char *msg, size_t msglen) {
    int violations = 0;
    int owned_frames = 0;
    if (msglen > 0) msg[0] = '\0';
    pthread_mutex_lock(&mem_lock);
    pthread_mutex_lock(&g_state.lock);
    for (int frame = 0; frame < total_frames; frame++) {
        int slot = frame_pool[frame].pid;
        if (slot < 0) continue;
        owned_frames++;
        if (slot >= MAX_STUDENTS || frame_pool[frame].virtual_page < 0 ||
            frame_pool[frame].virtual_page >= MAX_PAGES) {
            violations++;
            if (msg[0] == '\0') snprintf(msg, msglen, "frame %d has invalid owner/page", frame);
            continue;
        }
        PageTableEntry *entry = &page_tables[slot][frame_pool[frame].virtual_page];
        if (!entry->valid || entry->frame_number != frame) {
            violations++;
            if (msg[0] == '\0') snprintf(msg, msglen, "frame %d owner entry is inconsistent", frame);
        }
        if (g_state.processes[slot].state == TERMINATED) {
            violations++;
            if (msg[0] == '\0') snprintf(msg, msglen, "frame %d is owned by terminated slot %d", frame, slot);
        }
    }
    for (int slot = 0; slot < MAX_STUDENTS; slot++)
        for (int page = 0; page < MAX_PAGES; page++) {
            PageTableEntry *entry = &page_tables[slot][page];
            if (!entry->valid) continue;
            int frame = entry->frame_number;
            if (frame < 0 || frame >= total_frames ||
                frame_pool[frame].pid != slot ||
                frame_pool[frame].virtual_page != page) {
                violations++;
                if (msg[0] == '\0') snprintf(msg, msglen, "slot %d page %d has inconsistent frame", slot, page);
            }
        }
    if (g_state.frames_used != owned_frames) {
        violations++;
        if (msg[0] == '\0') snprintf(msg, msglen, "frames_used=%d owned=%d", g_state.frames_used, owned_frames);
    }
    pthread_mutex_unlock(&g_state.lock);
    pthread_mutex_unlock(&mem_lock);
    return violations;
}

void *memory_thread(void *arg) {
    (void)arg;
    rng_init_thread(2);
    log_event("INFO", "MEMORY", "Memory thread started");
    while (1) {
        pthread_mutex_lock(&g_state.lock);
        int running = g_state.simulation_running;
        int curr_pid = g_state.running_pid;
        int tick = g_state.current_tick;
        pthread_mutex_unlock(&g_state.lock);
        if (!running) break;
        if (curr_pid > 0) {
            int slot = process_slot(curr_pid);
            int phase = tick / g_config.phase_ticks;
            int base = (slot * 3 + phase * (g_config.ws_size / 2 + 1)) %
                g_config.pages_per_proc;
            int accesses = 1 + rng_uniform(3);
            for (int i = 0; i < accesses; i++) {
                int page = rng_uniform(100) < g_config.locality
                    ? (base + rng_uniform(g_config.ws_size)) % g_config.pages_per_proc
                    : rng_uniform(g_config.pages_per_proc);
                int is_write = rng_uniform(100) < g_config.write_pct;
                memory_access_rw(curr_pid, page, is_write);
            }
        }
        sim_sleep_ticks(1);
    }
    log_event("INFO", "MEMORY", "Memory thread exiting");
    return NULL;
}
