#include "threadpool.h"
#include "time.h"
#include <unistd.h>
#define MIN_THREADS 3 // 最低要求运行的线程数
#define DEFAULT_ADD_NUM 5


threadpool* threadpool_init(int corePoolSize,int max_threads,int max_queue)
{
    threadpool * pool = (threadpool*)malloc(sizeof(threadpool));
    if(pool == NULL)
    {
        printf("Failed to initialzie a threadpool!\n"); // due to lack of memory
        return NULL;
    }
    // 分配成功
    pool->min_threads = MIN_THREADS;
    pool->core_pool_size = corePoolSize;
    pool->max_threads = max_threads;
    pool->busy = 0;
    pool->live = MIN_THREADS;
    pool->state = running;
    pthread_mutex_init(&pool->mutex,NULL);
    pthread_mutex_init(&pool->lock,NULL);
    pthread_cond_init(&pool->not_empty,NULL);
    pthread_cond_init(&pool->not_full,NULL);
    do{        
        if(task_queue_init(&(pool->tq),max_queue) != 0)
        {
            printf("Failed to initialzie a taskqueue!\n");
            break;
        }
        pool->workers = (worker_t*)malloc(sizeof(worker_t) * max_threads);
        if(pool->workers == NULL)
        {
            printf("Failed to malloc memory for Tids!\n"); // due to lack of memory
            break;         
        }
        worker_t *threads = pool->workers;
        for(int i = 0; i < MIN_THREADS; ++i)
        {
            pthread_create(&(threads[i].tid),NULL,Work,(void*)pool);
            threads[i].state = 1;
        }
        pthread_create(&(pool->admin),NULL,Admin,(void*)pool);


        return pool;
    }while(0);
    threadpool_free(pool);
    return NULL;
}

void threadpool_free(threadpool* pool)
{
    task_queue_destroy(pool->tq); // destroy a taskqueue 
    free(pool->workers);
    free(pool);
    return;
}

void threadpool_destroy(threadpool* pool)
{
    pool->state = shutdown;
    pthread_cond_broadcast(&(pool->not_empty));// 广播所有工作线程醒过来
    pthread_join(pool->admin, NULL);
    worker_t *threads = pool->workers;
    for (int i = 0; i < pool->max_threads; i++)
    {
        if(threads[i].state == 1)
        {
            pthread_join(threads[i].tid, NULL);
            threads[i].state = 0;
        }
    }
    pthread_mutex_destroy(&pool->mutex);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    threadpool_free(pool);
    return;
}
void* Admin(void* arg)
{
    srand(time(NULL));
    threadpool *pool = (threadpool*)arg;
    int live,busy;
    double busy_ratio; // 忙线程比例
    double queue_usage; //队列使用率
    double avg_time; // 任务平均等待时间
    while(pool->state)
    {
        sleep(rand()%10); // 随机抽时间检查运行状况
        pthread_mutex_lock(&(pool->lock));     
        queue_usage = (double)pool->tq.len / pool->tq.size;
        avg_time = pool->task_wait_time;   
        pthread_mutex_unlock(&(pool->lock));   
        pthread_mutex_lock(&(pool->mutex));
        busy = pool->busy; 
        pthread_mutex_unlock(&(pool->mutex));
        live = pool->live; 
        busy_ratio = (double)busy / live;
        printf("Admin监测到当前线程池状态：\n队列使用率：%f-队列平均等待时间：%f(ms)-线程使用率：%d/%d\n",queue_usage,avg_time,busy,live);
        if(busy_ratio < 0.5&&live > MIN_THREADS) // 删除线程
        {
            int exit_num;
            if(MIN_THREADS > live * 0.5)
                exit_num = live - MIN_THREADS;
            else
                exit_num = live * 0.5;
            worker_t *threads = pool->workers;
            for(int i = 0,j = 0; i< exit_num && j < pool->max_threads;j++)
            {
                if(threads[j].state == 1)
                {
                    pthread_cancel(threads[i].tid);
                    --pool->live;
                    threads[j].state = 0;
                    i++;
                }
            }
        }
        if(busy_ratio >= 0.8 && queue_usage > 0.8)
        {
            int add_num;
            if(live < pool->core_pool_size)
                add_num = pool->core_pool_size - live;
            else
                add_num = DEFAULT_ADD_NUM;
            worker_t *threads = pool->workers;
            for(int i = 0,j = 0;j < pool->max_threads && i < add_num;j++)
            {
                if(threads[j].state == 0)
                {
                    pthread_create(&(threads[j].tid),NULL,Work,(void*)pool);
                    ++pool->live;
                    threads[j].state = 1;
                    i++;
                }
            }
        }
    }
    pthread_exit(NULL);
}

void clean(void *arg)
{
    threadpool *pool = (threadpool*)arg;
    pthread_mutex_unlock(&pool->lock);
    printf("thread:%ld响应取消退出\n",pthread_self());
    system("pause");// 查看退出的该线程暂停执行
    return;
}
void* Work(void* arg)
{
    pthread_cleanup_push(clean,arg);
    threadpool *pool = (threadpool*)arg;
    while(1)
    {
        pthread_mutex_lock(&pool->lock);
        while(task_queue_empty(pool->tq)&&pool->state)
        {
            pthread_cond_wait(&(pool->not_empty),&pool->lock);//本身是一个取消点
        }
        if(pool->state == shutdown)
        {
            pthread_mutex_unlock(&pool->lock);
            break;
        }
        task t = task_queue_get(&(pool->tq)); // 取出任务
        double ck = (double)(clock() - t.ti)/CLOCKS_PER_SEC * 1000; // 该任务在队列中等待的时间（毫秒）
        pool->task_wait_time = get_avg(ck);
        pthread_cond_signal(&(pool->not_full));// 通知生产线程任务队列不为满，可以继续放入任务
        pthread_mutex_unlock(&pool->lock); //解锁队列，其他线程可以取任务

        pthread_mutex_lock(&(pool->mutex)); // 锁住互斥变量，该互斥变量保护在忙线程计数变量
        ++pool->busy;
        pthread_mutex_unlock(&(pool->mutex)); // 解锁互斥变量

        execute(t.function,t.arg); // 正在处理任务

        pthread_mutex_lock(&(pool->mutex)); 
        --pool->busy;
        pthread_mutex_unlock(&(pool->mutex)); 

        pthread_testcancel(); // 取消点
    }
    pthread_cleanup_pop(0);
    return NULL;
} 

double get_avg(double ck)
{
    static double t[3] = {0.0,0.0,0.0};
    static int i = 0;
    t[i] = ck;
    i = (i + 1) % 3;
    return (t[0] + t[1] + t[2]) / 3;
}
/*
double get_mean(double ck)
{
    static double t[5] = {0.0};
    static mean;
    static i = 0;
    mean = mean + (ck - t[i]) / 5;
    t[i] = ck;
    i = (i + 1) % 5;
    return mean;
}
double time_mean(double t,int size) // 求采样平均值
{
    static int index = 1;
    static double last_mean = 0.0;
    double mean = 0.0;
    if(index < size)
    {
        mean = last_mean+(t-last_mean)/index;
        index++;
    }else{
        mean = last_mean+(t-last_mean)/size;
        index = 1;
    }
    last_mean = mean;
    return mean;
}
*/

void Produce(threadpool *pool,task t)
{
    pthread_mutex_lock(&(pool->lock));
    while (task_queue_full(pool->tq))
    {
        pthread_cond_wait(&(pool->not_full), &(pool->lock));
    }
    task_queue_put(&(pool->tq),t); // 添加任务到队列
    pthread_cond_signal(&(pool->not_empty)); // 唤醒所有工作线程
    pthread_mutex_unlock(&(pool->lock)); // 解锁
    return;
}

// 任务处理函数
void* func(void* arg)
{
    int id = *((int*)arg);
    printf("处理第%d个任务\n",id);
    free(arg); // 任务执行完成，释放堆分配的内存
    return NULL;
}
