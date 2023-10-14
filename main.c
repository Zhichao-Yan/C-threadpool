#include "threadpool.h"
#include "task.h"
#include <stdlib.h>
#include <stdio.h>

void* factory(void* arg) // 生产产品的厂家
{
    threadpool *pool = (threadpool*)arg;
    int i = 0;
    while(1)
    {
        task t;
        int *data = (int*)malloc(sizeof(int));  // arg通常指向堆内存
        *data = ++i;
        t.arg = data; 
        t.function = func;
        Produce(pool,t);
    }
}
int main(int argc,char **argv)
{
    threadpool *pool = threadpool_init(10,100,100);
    if(pool == NULL)
        exit(-1);
    if(argc != 2)
    {
        printf("pool [factory thread numbers]\n");
        return -1;
    }
    int num = atoi(argv[1]);
    pthread_t *tid = (pthread_t*)malloc(sizeof(pthread_t) * num);
    for(int i = 0; i < num; ++i )
    {
        pthread_create(&tid[i],NULL,factory,pool);
    }
    for(int i = 0; i < num; ++i)
    {
        pthread_join(tid[i],NULL);
    }
    free(tid);
    return 0;
}


