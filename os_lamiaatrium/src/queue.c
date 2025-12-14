#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t* q)
{
    if (q == NULL)
        return 1;
    return (q->size == 0);
}

void enqueue(struct queue_t* q, struct pcb_t* proc)
{
    /* TODO: put a new process to queue [q] */
    if (q == NULL || proc == NULL)
        return;

    if (q->size >= MAX_QUEUE_SIZE) {
        // Hàng đợi đầy – có thể in cảnh báo nếu muốn
        // printf("Queue is full, cannot enqueue pid=%d\n", proc->pid);
        return;
    }

    q->proc[q->size] = proc;
    q->size++;
}

struct pcb_t* dequeue(struct queue_t* q)
{
    /* TODO: return a pcb whose prioprity is the highest
     * in the queue [q] and remember to remove it from q
     */
    if (q == NULL || q->size == 0)
        return NULL;

    int best = 0;
    // Chọn process có prio nhỏ nhất (ưu tiên cao nhất)
    for (int i = 1; i < q->size; i++) {
        if (q->proc[i]->prio < q->proc[best]->prio) {
            best = i;
        }
    }

    struct pcb_t* res = q->proc[best];

    // Dồn các phần tử phía sau về trước
    for (int i = best + 1; i < q->size; i++) {
        q->proc[i - 1] = q->proc[i];
    }
    q->size--;
    q->proc[q->size] = NULL;

    return res;
}

struct pcb_t* purgequeue(struct queue_t* q, struct pcb_t* proc)
{
    /* TODO: remove a specific item from queue
     * */
    if (q == NULL || q->size == 0 || proc == NULL)
        return NULL;

    int idx = -1;
    for (int i = 0; i < q->size; i++) {
        if (q->proc[i] == proc) {
            idx = i;
            break;
        }
    }

    if (idx == -1)
        return NULL; // không tìm thấy

    struct pcb_t* res = q->proc[idx];

    // Dồn lại
    for (int i = idx + 1; i < q->size; i++) {
        q->proc[i - 1] = q->proc[i];
    }
    q->size--;
    q->proc[q->size] = NULL;

    return res;
}
