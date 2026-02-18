#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

#define THREAD_POOL_SIZE 10
#define QUEUE_SIZE 100

typedef struct {
    int client_sockets[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int shutdown;
} ThreadPool;

void pool_init(ThreadPool *pool);
void pool_submit(ThreadPool *pool, int client_socket);
int pool_fetch(ThreadPool *pool);
void pool_shutdown(ThreadPool *pool);

#endif
