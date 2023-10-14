#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "task.h"
#define shutdown 0
#define running 1

typedef struct worker
{
    pthread_t tid;
    int state;
}worker_t;

typedef struct threadpool
{
    int state; // shutdown state = 0 or running state = 1
    pthread_mutex_t mutex; // mutex for task_t queue
    pthread_mutex_t lock; // 

    task_queue tq; // task queue
    pthread_cond_t not_empty; // condition variable means queue is not empty/tasks available
    pthread_cond_t not_full; // condition variable means queue is not full/spaces for tasks
    pthread_t admin; // manager
    worker_t *workers; // threads array

    int min_threads;
    int core_pool_size;
    int max_threads;
    int live;
    int busy;
    double task_wait_time; // the time task stayed in queue
}threadpool;


threadpool* threadpool_init(int corePoolSize,int max_threads,int max_queue);
void threadpool_free(threadpool* pool);
void* Admin(void* arg);
void* Work(void* arg);
void Produce(threadpool *pool,task t);
void* func(void* arg);
double get_avg_time(double ck);
#endif