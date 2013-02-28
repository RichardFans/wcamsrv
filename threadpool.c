/*
 *	threadpool.c
 *	Copyright (C) 2010, 2011 Richard Fan
 */ 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include <cam/threadpool.h>

#if defined(DBG_TPOOL)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif
 
struct worker { 
    void *(*proc) (void *arg); 
    void *arg;        
    struct worker *next; 
}; 

struct thread_pool { 
    pthread_mutex_t queue_lock; 
    pthread_cond_t queue_ready; 
    struct worker *queue_head; 
    bool need_destroy; 
    pthread_t *threadid; 
    int thread_num; 
    int cur_queue_size; 
}; 
 
void *thread_routine (void *arg); 

thread_pool_t pool_create(int thread_num) 
{ 
    int i = 0; 
	pthread_attr_t attr;
	struct thread_pool *pool;
	
    pool = (struct thread_pool*)malloc(sizeof(*pool)); 
    if (!pool) {
		perror("pool_create");
		return NULL;
	}
    pthread_mutex_init(&(pool->queue_lock), NULL); 
    pthread_cond_init(&(pool->queue_ready), NULL); 

    pool->queue_head = NULL; 

    pool->thread_num = thread_num; 
    pool->cur_queue_size = 0; 

    pool->need_destroy = false; 
	
    pool->threadid = 
        (pthread_t *)malloc(thread_num * sizeof (pthread_t)); 
	
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 512*1024);
    for (i = 0; i < thread_num; i++) 
        pthread_create(&(pool->threadid[i]), &attr, 
		               thread_routine, (void*)pool); 
	return pool;
} 

/*
 * 向线程池中加入任务
 */ 
int pool_add_worker(thread_pool_t tpool, void *(*proc)(void *), void *arg) 
{ 
	struct thread_pool *pool = tpool;
    struct worker *newworker = malloc(sizeof(struct worker)); 

    newworker->proc = proc; 
    newworker->arg = arg; 
    newworker->next = NULL;
	
    pthread_mutex_lock(&(pool->queue_lock)); 
	
    struct worker *memb = pool->queue_head;  /*将任务加入到等待队列中*/ 
    if (memb != NULL) { 
        while (memb->next != NULL) 
            memb = memb->next; 
        memb->next = newworker; 
    } else { 
        pool->queue_head = newworker; 
    } 
    assert(pool->queue_head != NULL); 
    pool->cur_queue_size++; 
    pr_debug("cur_queue_size = %d\n", pool->cur_queue_size);

    pthread_cond_signal(&(pool->queue_ready)); 
    pthread_mutex_unlock(&(pool->queue_lock));	
    return 0; 
} 

/* 
 * 销毁线程池
 *
 * 等待队列中的任务不会再被执行，但是正在运行的线程会一直 
 * 把任务运行完后再退出
 */ 
int pool_free(thread_pool_t tpool) 
{ 
	struct thread_pool *pool = tpool;
    int i; 

    if (pool->need_destroy) 
        return -1;   
    pool->need_destroy = true;
	
	pthread_mutex_lock(&(pool->queue_lock)); 
    /*唤醒所有等待线程，线程池要销毁了*/ 
    pthread_cond_broadcast(&(pool->queue_ready)); 
	pthread_mutex_unlock(&(pool->queue_lock));	

    for (i = 0; i < pool->thread_num; i++) 
        pthread_join(pool->threadid[i], NULL); 
    free(pool->threadid); 

    /*销毁等待队列*/ 
    struct worker *head = NULL; 
    while (pool->queue_head != NULL) { 
        head = pool->queue_head; 
        pool->queue_head = pool->queue_head->next; 
        free (head); 
    } 
    /*条件变量和互斥量也别忘了销毁*/ 
    pthread_mutex_destroy(&(pool->queue_lock)); 
    pthread_cond_destroy(&(pool->queue_ready)); 
     
    free(pool);  
    pool = NULL; 
    return 0; 
} 

void *thread_routine(void *arg) 
{ 
	struct thread_pool *pool = arg; 
    pr_debug("starting thread 0x%lx\n", pthread_self()); 
    while (true) { 
        pthread_mutex_lock(&(pool->queue_lock)); 
 
        while (pool->cur_queue_size == 0 && !pool->need_destroy) { 
            pr_debug("thread 0x%lx is waiting\n", pthread_self()); 
            pthread_cond_wait (&(pool->queue_ready), &(pool->queue_lock)); 
        } 

        if (pool->need_destroy) { 
            pthread_mutex_unlock(&(pool->queue_lock)); 
            pr_debug("thread 0x%lx will exit\n", pthread_self()); 
            break; 
        } 
        pr_debug("thread 0x%lx is starting to work\n", pthread_self()); 

        assert(pool->cur_queue_size != 0); 
        assert(pool->queue_head != NULL); 
         
        /*等待队列长度减去1，并取出链表中的头元素*/ 
        pool->cur_queue_size--; 
        struct worker *worker = pool->queue_head; 
        pool->queue_head = worker->next; 
        pthread_mutex_unlock(&(pool->queue_lock)); 

        /*调用回调函数，执行任务*/ 
        worker->proc(worker->arg); 
        free(worker); 
        worker = NULL; 
    }  
    pthread_exit(NULL); 
}

