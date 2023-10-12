#ifndef TASK_H
#define TASK_H
#include <time.h>
typedef struct task{
    void* (*function)(void*); // function used for task processing
    void* arg; // input data for task 
    clock_t ti; // the clock_t when the task enter the queue
}task;

// 循环队列
typedef struct task_queue 
{
    task* queue;
    int front; // 指向队头的第一个任务
    int rear; // 指向任务队列队尾的可以存放任务的下一个空位置
    int len; // 任务队列实时长度
    int size; // 任务队列大小
}task_queue;

int task_queue_init(task_queue q,int size);
void task_queue_put(task_queue q,task t);
task task_queue_get(task_queue q);
void task_queue_destroy(task_queue q);
void execute(void*(*function)(void*),void* arg);
int task_queue_empty(task_queue q);
int task_queue_full(task_queue q);

#endif