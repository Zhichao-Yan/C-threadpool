#include "task.h"
#include <stdlib.h>
#include <stdio.h>

int task_queue_init(task_queue *q,int size)
{
    q->queue = (task*)malloc(sizeof(task) * size);
    if(q->queue == NULL)
    {
        printf("Failed to malloc a task_queue\n");
        return -1;
    }
    q->front = 0;
    q->rear = 0;
    q->len = 0;
    q->size = size;
    return 0;
}
void task_queue_put(task_queue *q,task t)
{   
    t.ti = clock();
    q->queue[q->rear] = t;
    ++q->len;
    q->rear = (q->rear + 1) % q->size;
    return;
}
task task_queue_get(task_queue *q)
{
    task re = q->queue[q->front]; // 任务数据被拷贝走，可以置空了
    q->queue[q->front].arg = NULL; // 把取走的任务数据指针置空
    q->queue[q->front].function = NULL; // 把取走的任务函数指针置空
    q->front = (q->front + 1) % q->size;
    --q->len;
    return re;
}

void task_queue_destroy(task_queue q)
{
    if(q.queue)  // free it if q.queue is not NULL
    {
        free(q.queue);
        q.queue = NULL;
        q.size = 0;
        q.len = 0;
        q.front = q.rear = 0;
    }
    // do nothing if the q.queue is NULL
    return;
}
void execute(void*(*function)(void*),void* arg)
{
    (*(function))(arg);
    return;
}
int task_queue_empty(task_queue q)
{
    if(q.len  == 0)
        return 1;
    return 0;
}
int task_queue_full(task_queue q)
{
    if((q.rear + 1)%(q.size) == q.front)
        return 1;
    return 0;
}
