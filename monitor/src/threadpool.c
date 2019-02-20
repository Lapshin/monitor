/*
 * threadpool.c
 *
 *  Created on: Feb 17, 2019
 *      Author: alexey.lapshin
 */

#include <malloc.h>
#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include "threadpool.h"

static void *processor(void *pool);

ThreadPool *threadPoolCreate(size_t size)
{
    int ret;
    size_t i;
    pthread_mutexattr_t attr;
    ThreadPool *pool = malloc(sizeof(ThreadPool));
    if(pool == NULL) {
        syslog(LOG_ERR, "<%s> Can't alloc memory!", __func__);
        goto error;
    }
    pool->processors = malloc(sizeof(pthread_t)*size);
    if(pool->processors == NULL)
    {
        syslog(LOG_ERR, "<%s> Can't alloc memory for processors!", __func__);
        goto error;
    }

    pool->size = size;

    ret = sem_init(&pool->queue_sem, 0, 0);
    if (ret) {
        syslog(LOG_ERR, "Failed on sem_init. ret = %d\n", ret);
        goto error;
    }
    pool->work = 1;
    pthread_mutexattr_init(&attr);
    pthread_mutex_init(&pool->lock, &attr);
    TAILQ_INIT(&pool->queue);

    for (i = 0; i < size; i++)
    {
        pthread_create(&pool->processors[i], NULL, processor, pool);
    }

    goto exit;

error:
    free(pool->processors);
    free(pool);
    pool = NULL;
exit:
    return pool;
}

int threadPoolAddEvent(ThreadPool *pool, void*(*fn)(void*), void* arg)
{
    QueuedEvent *event;

    if(pool->work == 0)
    {
        return 0;
    }

    event = malloc(sizeof(QueuedEvent));
    if (event == NULL)
    {
        syslog(LOG_ERR, "<%s> Can't alloc memory!", __func__);
        return -1;
    }
    event->event = fn;
    event->args = arg;

    pthread_mutex_lock(&pool->lock);
    TAILQ_INSERT_TAIL(&pool->queue, event, next);
    pthread_mutex_unlock(&pool->lock);
    sem_post(&pool->queue_sem);
    return 0;
}

static void *processor(void *arg) {
    QueuedEvent *event;
    ThreadPool *pool = (ThreadPool*) arg;
    while(pool->work) {
        sem_wait(&pool->queue_sem);
        pthread_mutex_lock(&pool->lock);
        event = pool->queue.tqh_first;
        if (!event) {
            pthread_mutex_unlock(&pool->lock);
            continue;
        }
        TAILQ_REMOVE(&pool->queue, event, next);
        pthread_mutex_unlock(&pool->lock);

        event->event(event->args);

//        free(event->args);
        free(event);
    }
    syslog(LOG_DEBUG, "Pool worker finished.");
    return NULL;
}

int threadPoolDestroy(ThreadPool *pool)
{
    size_t i;
    /*
     * Disable workers loop.
     */
    pool->work = 0;

    /*
     * Clear queue.
     */
    pthread_mutex_lock(&pool->lock);
    QueuedEvent *event;
    while ((event = pool->queue.tqh_first)) {
        TAILQ_REMOVE(&pool->queue, event, next);
        free(event->args);
        free(event);
    }
    pthread_mutex_unlock(&pool->lock);

    /*
     * Finalize all workers.
     */

    for (i = 0; i < pool->size; i++) {
        sem_post(&pool->queue_sem);
    }

    for (i = 0; i < pool->size; i++) {
        pthread_join(pool->processors[i], NULL);
    }

    free(pool->processors);
    sem_destroy(&pool->queue_sem);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
    return 0;
}
