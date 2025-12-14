/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;
static struct queue_t running_list;

#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
    unsigned long prio;
    for (prio = 0; prio < MAX_PRIO; prio++)
        if (!empty(&mlq_ready_queue[prio]))
            return -1;
#endif
    return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i;
    for (i = 0; i < MAX_PRIO; i++) {
        mlq_ready_queue[i].size = 0;
        slot[i] = MAX_PRIO - i;
    }
#endif
    ready_queue.size = 0;
    run_queue.size = 0;
    running_list.size = 0;
    pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
struct pcb_t* get_mlq_proc(void) {
    struct pcb_t* proc = NULL;

    pthread_mutex_lock(&queue_lock);

    /* Duyệt từ priority cao nhất (0) đến thấp nhất */
    for (unsigned long prio = 0; prio < MAX_PRIO; prio++) {
        if (!empty(&mlq_ready_queue[prio]) && slot[prio] > 0) {
            proc = dequeue(&mlq_ready_queue[prio]);
            slot[prio]--;
            break;
        }
    }

    /* Nếu không lấy được process (hết slot) nhưng vẫn còn process */
    if (proc == NULL) {
        int has_process = 0;
        for (unsigned long prio = 0; prio < MAX_PRIO; prio++) {
            if (!empty(&mlq_ready_queue[prio])) {
                has_process = 1;
                break;
            }
        }

        if (has_process) {
            /* Reset tất cả slot */
            for (unsigned long prio = 0; prio < MAX_PRIO; prio++) {
                slot[prio] = MAX_PRIO - prio;
            }

            /* Lấy process sau khi reset */
            for (unsigned long prio = 0; prio < MAX_PRIO; prio++) {
                if (!empty(&mlq_ready_queue[prio])) {
                    proc = dequeue(&mlq_ready_queue[prio]);
                    slot[prio]--;
                    break;
                }
            }
        }
    }

    /* Add to running_list để tracking */
    if (proc != NULL) {
        enqueue(&running_list, proc);
    }

    pthread_mutex_unlock(&queue_lock);
    return proc;
}

void put_mlq_proc(struct pcb_t* proc) {
    proc->krnl->ready_queue = &ready_queue;
    proc->krnl->mlq_ready_queue = mlq_ready_queue;
    proc->krnl->running_list = &running_list;

    pthread_mutex_lock(&queue_lock);

    /* Xóa khỏi running_list - PHẢI DÙNG ĐÚNG CÁCH */
    for (int i = 0; i < running_list.size; i++) {
        if (running_list.proc[i] == proc) {
            /* Dịch các phần tử phía sau lên */
            for (int j = i; j < running_list.size - 1; j++) {
                running_list.proc[j] = running_list.proc[j + 1];
            }
            running_list.size--;
            break;
        }
    }

    /* Đưa lại vào ready queue theo priority */
   // enqueue(&mlq_ready_queue[proc->prio], proc);
    int q = proc->prio;
    if (q >= MAX_PRIO) q = MAX_PRIO - 1;
    enqueue(&mlq_ready_queue[q], proc);

    pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t* proc) {
    proc->krnl->ready_queue = &ready_queue;
    proc->krnl->mlq_ready_queue = mlq_ready_queue;
    proc->krnl->running_list = &running_list;

    pthread_mutex_lock(&queue_lock);
   // enqueue(&mlq_ready_queue[proc->prio], proc);
    int q = proc->prio;
    if (q >= MAX_PRIO) q = MAX_PRIO - 1;
    enqueue(&mlq_ready_queue[q], proc);
    pthread_mutex_unlock(&queue_lock);
}

struct pcb_t* get_proc(void) {
    return get_mlq_proc();
}

void put_proc(struct pcb_t* proc) {
    return put_mlq_proc(proc);
}

void add_proc(struct pcb_t* proc) {
    return add_mlq_proc(proc);
}

#else
struct pcb_t* get_proc(void) {
    struct pcb_t* proc = NULL;

    pthread_mutex_lock(&queue_lock);

    if (!empty(&ready_queue)) {
        proc = dequeue(&ready_queue);
        if (proc != NULL) {
            enqueue(&running_list, proc);
        }
    }

    pthread_mutex_unlock(&queue_lock);
    return proc;
}

void put_proc(struct pcb_t* proc) {
    proc->krnl->ready_queue = &ready_queue;
    proc->krnl->running_list = &running_list;

    pthread_mutex_lock(&queue_lock);

    /* Xóa khỏi running_list */
    for (int i = 0; i < running_list.size; i++) {
        if (running_list.proc[i] == proc) {
            for (int j = i; j < running_list.size - 1; j++) {
                running_list.proc[j] = running_list.proc[j + 1];
            }
            running_list.size--;
            break;
        }
    }

    enqueue(&ready_queue, proc);

    pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t* proc) {
    proc->krnl->ready_queue = &ready_queue;
    proc->krnl->running_list = &running_list;

    pthread_mutex_lock(&queue_lock);
    enqueue(&ready_queue, proc);
    pthread_mutex_unlock(&queue_lock);
}
#endif