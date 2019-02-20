/*
 * threadpool.h
 *
 *  Created on: Feb 17, 2019
 *      Author: alexey.lapshin
 */

#ifndef THREADPOOL_H_
#define THREADPOOL_H_

#include <semaphore.h>
#include <stdatomic.h>
#include <sys/queue.h>

typedef struct QueuedEvent_s {
    void* (*event) (void*);
    void *args;
    TAILQ_ENTRY(QueuedEvent_s) next;
} QueuedEvent;

typedef struct ThreadPool_s {
    sem_t queue_sem;
    pthread_mutex_t lock;
    pthread_t *processors;
    _Atomic int work;
    size_t size;
    int *fds_at_work;
    TAILQ_HEAD(, QueuedEvent_s) queue;
} ThreadPool;


ThreadPool *threadPoolCreate(size_t size);
int threadPoolAddEvent(ThreadPool *pool, void*(*fn)(void*), void* arg);
int threadPoolDestroy(ThreadPool *pool);


#endif /* THREADPOOL_H_ */
