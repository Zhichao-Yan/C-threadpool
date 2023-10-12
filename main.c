#include "threadpool.h"
#include <stdlib.h>


int main()
{
    threadpool *pool = threadpool_init(10,100,100);
    if(pool == NULL)
        exit(-1);
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
    return 0;
}


