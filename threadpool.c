#include "threadpool.h"
#include "time.h"
#include <unistd.h>
#define MIN_THREADS 3 // 最低要求运行的线程数
#define DEFAULT_INCREMENT1 2 // 线程不足核心线程数时，默认自增的线程数
#define DEFAULT_INCREMENT2 5 // 线程数量超过核心线程数时，默认的自增线程数目


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
    pool->core_pool_size = corePoolSize; // 核心线程数
    pool->max_threads = max_threads;
    pool->busy = 0;
    pool->live = MIN_THREADS;
    pool->state = running;
    // 因为pool是动态分配的，所以需要动态初始化互斥量和条件变量
    pthread_mutex_init(&pool->mutex,NULL); // 初始化互斥量，保护busy共享数据
    pthread_mutex_init(&pool->lock,NULL); // 初始化互斥量，保护队列互斥访问
    pthread_cond_init(&pool->not_empty,NULL); // 初始化条件变量，此时条件状态为队列非空
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
    free(pool->workers);
    pthread_mutex_destroy(&pool->mutex);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    task_queue_destroy(pool->tq); // destroy a taskqueue
    free(pool);
    return;
}

void threadpool_destroy(threadpool* pool)
{
    /* 广播所有阻塞的工作线程醒过来 
    ** 在处理任务的线程会处理完后自动跳出循环
    */
    pool->state = shutdown;
    pthread_cond_broadcast(&(pool->not_empty));
    threadpool_free(pool);
    return;
}
void* Admin(void* arg)
{
    pthread_detach(pthread_self());
    srand(time(NULL)); // 播下时间种子
    threadpool *pool = (threadpool*)arg;
    double busy_ratio; // 忙的线程占存活线程比例
    double queue_usage; //队列使用率
    double avg_time; // 任务平均等待时间
    while(pool->state)
    {
        sleep(rand()%10); // 休息随机时间后抽查运行状况
        pthread_mutex_lock(&(pool->lock));     
        queue_usage = (double)pool->tq.len / pool->tq.size;
        avg_time = pool->task_wait_time;   
        pthread_mutex_unlock(&(pool->lock));
        // 因为pooL->busy是和工作线程共享，所以需要互斥访问
        // 目前有多少工作线程pool->live只有管理线程清楚  
        pthread_mutex_lock(&(pool->mutex));
        busy_ratio = (double)pool->busy / pool->live;
        pthread_mutex_unlock(&(pool->mutex));
        
        printf("此时线程池状态：\
        \n队列使用率:%f\n队列平均等待时间:%f(ms)\n线程使用率:%f(%dthreads)\n",queue_usage,avg_time,busy_ratio,pool->live);
	    if(busy_ratio < 0.5&&pool->live > MIN_THREADS) // 取消线程
        {
            int exit_num = 0; // 打算取消的线程
            if(MIN_THREADS > pool->live * 0.5) // 如果最小线程数大于线程数的0.5，那么不应该取消太多
                exit_num = pool->live - MIN_THREADS;
            else
                exit_num = pool->live * 0.5; // 如果最小线程数小于于线程数的0.5，那么尝试直接删除一半
            worker_t *threads = pool->workers;
            for(int i = 0,j = 0; i< exit_num && j < pool->max_threads;j++)
            {
                if(threads[j].state == 1)
                {
                    pthread_cancel(threads[j].tid); // 给线程发送取消信号
                    printf("取消线程%ld\n",threads[j].tid);
                    --pool->live;
                    threads[j].state = 0;
                    i++;
                }
            }
        }
        if(busy_ratio >= 0.8 && queue_usage > 0.8)
        {
            int add_num = 0;
            if(pool->live < pool->core_pool_size) // 如果当前线程数小于核心线程数
                add_num = DEFAULT_INCREMENT1; // 以2为单位逐渐递增
            else
                add_num = DEFAULT_INCREMENT2; // 如果当前线程数大于核心线程数，则以5为单位递增
            worker_t *threads = pool->workers;
            for(int i = 0,j = 0;j < pool->max_threads && i < add_num;j++)
            {
                if(threads[j].state == 0) 
                {
                    pthread_create(&(threads[j].tid),NULL,Work,(void*)pool); // 创建新线程
                    printf("新增线程%ld\n",threads[j].tid);
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
    pthread_mutex_unlock(&pool->lock); // 清理函数，线程响应取消时，锁可能没释放，因此清理函数应当释放锁
    printf("thread:%ld响应取消退出\n",pthread_self());
    return;
}
void* Work(void* arg)
{
    pthread_detach(pthread_self());
    pthread_cleanup_push(clean,arg); // 注册清理函数，线程响应取消时执行清理函数
    threadpool *pool = (threadpool*)arg;
    while(pool->state) // 线程池在running则继续循环
    {
        pthread_mutex_lock(&pool->lock);
        while(task_queue_empty(pool->tq)&&pool->state)
        {
            pthread_cond_wait(&(pool->not_empty),&pool->lock);//本身是一个取消点
        }
        if(pool->state == shutdown)
        {
            pthread_mutex_unlock(&pool->lock);
            break; // 跳出循环不会执行清理函数
        }
        task t = task_queue_get(&(pool->tq)); // 取出任务
        double ck = (double)(clock() - t.ti)/CLOCKS_PER_SEC * 1000; // 该任务在队列中等待的时间（毫秒）
        pool->task_wait_time = get_avg_time(ck);
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
    pthread_cleanup_pop(0); // 弹出清理函数，参数为0，正常运行结束不会执行清理函数
    return NULL; 
} 

// 获得最近3个任务的队列中平均等待时间
double get_avg_time(double ck)
{
    static double t[3] = {0.0,0.0,0.0};
    static int i = 0;
    t[i] = ck;
    i = (i + 1) % 3;
    return (t[0] + t[1] + t[2]) / 3;
}

// 生产者函数，产生任务并放入队列
void Produce(threadpool *pool,task t)
{
    pthread_mutex_lock(&(pool->lock));
    while (task_queue_full(pool->tq)) // 当队列为满，进入循环
    {
        pthread_cond_wait(&(pool->not_full), &(pool->lock)); // 当条件状态变为非满，线程醒来
    }
    task_queue_put(&(pool->tq),t); // 添加任务到队列
    pthread_cond_signal(&(pool->not_empty)); // 唤醒一个工作线程
    pthread_mutex_unlock(&(pool->lock)); // 队列解锁
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
