#include "disk_sched.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int text_equal(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int disk_algo_parse(const char *s, DiskAlgo *out) {
    if (s == NULL || out == NULL)
        return -1;
    if (text_equal(s, "FIFO"))
        *out = DISK_FIFO;
    else if (text_equal(s, "LIFO"))
        *out = DISK_LIFO;
    else if (text_equal(s, "SSTF"))
        *out = DISK_SSTF;
    else if (text_equal(s, "SCAN"))
        *out = DISK_SCAN;
    else if (text_equal(s, "CSCAN") || text_equal(s, "C-SCAN"))
        *out = DISK_CSCAN;
    else
        return -1;
    return 0;
}

const char *disk_algo_name(DiskAlgo a) {
    switch (a) {
    case DISK_FIFO: return "FIFO";
    case DISK_LIFO: return "LIFO";
    case DISK_SSTF: return "SSTF";
    case DISK_SCAN: return "SCAN";
    case DISK_CSCAN: return "CSCAN";
    }
    return "UNKNOWN";
}

static int valid_algo(DiskAlgo algo) {
    return algo >= DISK_FIFO && algo <= DISK_CSCAN;
}

static int choose_pending(const int *req, const unsigned char *served, int n,
                          int position, int mode) {
    int chosen = -1;
    for (int i = 0; i < n; i++) {
        if (served[i] || (mode == 1 && req[i] < position) ||
            (mode == 2 && req[i] > position))
            continue;
        if (chosen < 0 ||
            ((mode == 1 || mode == 3) &&
             (req[i] < req[chosen] ||
              (req[i] == req[chosen] && i < chosen))) ||
            ((mode == 2 || mode == 4) &&
             (req[i] > req[chosen] ||
              (req[i] == req[chosen] && i < chosen))))
            chosen = i;
    }
    return chosen;
}

static long serve(int index, int *position, const int *req,
                  unsigned char *served, int *order_out, int *served_count) {
    long distance = (long)req[index] - *position;
    if (distance < 0)
        distance = -distance;
    *position = req[index];
    served[index] = 1;
    order_out[*served_count] = index;
    (*served_count)++;
    return distance;
}

long disk_sched_run(DiskAlgo algo, int ncyl, int head, DiskDir dir,
                    const int *req, int n, int *order_out,
                    int *final_head, DiskDir *final_dir) {
    unsigned char *served;
    long movement = 0;
    int position = head;
    int count = 0;
    DiskDir ending_dir = dir;

    if (!valid_algo(algo) || !((dir == DISK_DIR_UP) || (dir == DISK_DIR_DOWN)) ||
        ncyl < 1 || head < 0 || head >= ncyl || n < 0 ||
        (n > 0 && (req == NULL || order_out == NULL)))
        return -1;
    for (int i = 0; i < n; i++)
        if (req[i] < 0 || req[i] >= ncyl)
            return -1;
    if (n == 0) {
        if (final_head != NULL)
            *final_head = head;
        if (final_dir != NULL)
            *final_dir = dir;
        return 0;
    }

    served = calloc((size_t)n, sizeof(*served));
    if (served == NULL)
        return -1;

    if (algo == DISK_FIFO || algo == DISK_LIFO) {
        int step = algo == DISK_FIFO ? 1 : -1;
        int index = algo == DISK_FIFO ? 0 : n - 1;
        for (int i = 0; i < n; i++, index += step)
            movement += serve(index, &position, req, served, order_out, &count);
    } else if (algo == DISK_SSTF) {
        while (count < n) {
            int chosen = -1;
            for (int i = 0; i < n; i++) {
                long distance;
                long chosen_distance;
                if (served[i])
                    continue;
                distance = (long)req[i] - position;
                if (distance < 0)
                    distance = -distance;
                chosen_distance = chosen < 0 ? 0 :
                    (long)req[chosen] - position;
                if (chosen >= 0 && chosen_distance < 0)
                    chosen_distance = -chosen_distance;
                if (chosen < 0 || distance < chosen_distance)
                    chosen = i;
            }
            movement += serve(chosen, &position, req, served,
                              order_out, &count);
        }
    } else {
        int first_mode = dir == DISK_DIR_UP ? 1 : 2;
        int second_mode;
        if (algo == DISK_CSCAN)
            second_mode = dir == DISK_DIR_UP ? 3 : 4;
        else
            second_mode = dir == DISK_DIR_UP ? 4 : 3;
        while (count < n) {
            int chosen = choose_pending(req, served, n, position, first_mode);
            if (chosen < 0)
                break;
            movement += serve(chosen, &position, req, served,
                              order_out, &count);
        }
        if (count < n) {
            int endpoint = dir == DISK_DIR_UP ? ncyl - 1 : 0;
            movement += (long)endpoint - position > 0 ?
                (long)endpoint - position : (long)position - endpoint;
            position = endpoint;
            if (algo == DISK_CSCAN) {
                int jump = dir == DISK_DIR_UP ? 0 : ncyl - 1;
                movement += (long)ncyl - 1;
                position = jump;
            } else {
                ending_dir = dir == DISK_DIR_UP ? DISK_DIR_DOWN : DISK_DIR_UP;
            }
            while (count < n) {
                int chosen = choose_pending(req, served, n, position,
                                            second_mode);
                if (chosen < 0)
                    break;
                movement += serve(chosen, &position, req, served,
                                  order_out, &count);
            }
        }
    }
    if (final_head != NULL)
        *final_head = position;
    if (final_dir != NULL)
        *final_dir = ending_dir;
    free(served);
    return movement;
}
