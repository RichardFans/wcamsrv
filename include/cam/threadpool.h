#ifndef	__THREADPOOL_H__
#define __THREADPOOL_H__
	
/* Opaque struct pointer to thread_pool_t */	
typedef struct thread_pool *thread_pool_t;

#define DEF_THREAD_IN_POOL   8

thread_pool_t pool_create(int thread_nr);
int pool_add_worker(thread_pool_t pool, 
                           void *(*process)(void *arg), 
						   void *arg); 
int pool_free(thread_pool_t pool);

#endif	//__THREADPOOL_H__
