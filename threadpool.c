#include <stdlib.h>
#include <stdio.h>

#include "uthread.h"
#include "uthread_mutex_cond.h"

#include "threadpool.h"

struct tpool {
  struct Task *first_task;
  struct Task *last_task;
  int remaining_tasks;
  int num_threads;
  int end_pool;
  uthread_mutex_t mutex;
  uthread_cond_t ready_to_work;
  uthread_cond_t finished;
  uthread_t *threads;
};

struct Task {
  struct Task *next_task;
  void (*fun)(tpool_t, void *);
  void *arg;
};

struct Task *createTask(void (*fun)(tpool_t, void *), void *arg) {
  struct Task *task;
  task = malloc(sizeof(*task));
  task->fun = fun;
  task->arg = arg;
  task->next_task = NULL;
  return task;
}

/* Function executed by each pool worker thread. This function is
 * responsible for running individual tasks. The function continues
 * running as long as either the pool is not yet joined, or there are
 * unstarted tasks to run. If there are no tasks to run, and the pool
 * has not yet been joined, the worker thread must be blocked.
 * 
 * Parameter: param: The pool associated to the thread.
 * Returns: nothing.
 */
static void *worker_thread(void *param) {
  tpool_t pool = param;
  while (1) {
    uthread_mutex_lock(pool->mutex);
    while (pool->first_task == NULL && !pool->end_pool) {
      uthread_cond_wait(pool->ready_to_work);
    }
    if (pool->end_pool == 1) {
      uthread_cond_broadcast(pool->finished);
      uthread_mutex_unlock(pool->mutex);
      return NULL;
    }
    struct Task *task = pool->first_task;
    pool->first_task = task->next_task;
    uthread_mutex_unlock(pool->mutex);

    task->fun(pool, task->arg);
    free(task);

    uthread_mutex_lock(pool->mutex);
    pool->remaining_tasks--;
    if (pool->remaining_tasks == 0 && pool->first_task == NULL) {
      uthread_cond_broadcast(pool->finished);
    }
    uthread_mutex_unlock(pool->mutex);
  }
  
  return NULL;
}

/* Creates (allocates) and initializes a new thread pool. Also creates
 * `num_threads` worker threads associated to the pool, so that
 * `num_threads` tasks can run in parallel at any given time.
 *
 * Parameter: num_threads: Number of worker threads to be created.
 * Returns: a pointer to the new thread pool object.
 */
tpool_t tpool_create(unsigned int num_threads) {
  tpool_t pool = malloc(sizeof(*pool));
  pool->mutex = uthread_mutex_create();
  pool->ready_to_work = uthread_cond_create(pool->mutex);
  pool->finished = uthread_cond_create(pool->mutex);
  pool->first_task = NULL;
  pool->last_task = NULL;
  pool->end_pool = 0;
  pool->num_threads = 0;
  pool->remaining_tasks = 0;
  pool->threads = malloc(sizeof(uthread_t) * num_threads);
  for (int threadno = 0; threadno < num_threads; threadno++) {
    pool->threads[threadno] = uthread_create(worker_thread, pool);
    pool->num_threads++;
  }
  return pool;
}

/* Queues a new task, to be executed by one of the worker threads
 * associated to the pool. The task is represented by function `fun`,
 * which receives the pool and a generic pointer as parameters. If any
 * of the worker threads is available, `fun` is started immediately by
 * one of the worker threads. If all of the worker threads are busy,
 * `fun` is scheduled to be executed when a worker thread becomes
 * available. Tasks are retrieved by individual worker threads in the
 * order in which they are scheduled, though due to the nature of
 * concurrency they may not start exactly in the same order. This
 * function returns immediately, and does not wait for `fun` to
 * complete.
 *
 * Parameters: pool: the pool that is expected to run the task.
 *             fun: the function that should be executed.
 *             arg: the argument to be passed to fun.
 */
void tpool_schedule_task(tpool_t pool, void (*fun)(tpool_t, void *),
                         void *arg) {
  struct Task *task = createTask(fun, arg);
  uthread_mutex_lock(pool->mutex);
  if (pool->first_task == NULL) {
    pool->first_task = task;
    pool->last_task = task;
  } else {
    pool->last_task->next_task = task;
    pool->last_task = task;
  }
  pool->remaining_tasks++;
  uthread_cond_signal(pool->ready_to_work);
  uthread_mutex_unlock(pool->mutex);
}

/* Blocks until the thread pool has no more scheduled tasks; then,
 * joins all worker threads, and frees the pool and all related
 * resources. Once this function returns, the pool cannot be used
 * anymore.
 *
 * Parameters: pool: the pool to be joined.
 */
void tpool_join(tpool_t pool) {
  uthread_mutex_lock(pool->mutex);
  while (pool->remaining_tasks != 0) {
    uthread_cond_wait(pool->finished);
  }
  pool->end_pool = 1;
  uthread_cond_broadcast(pool->ready_to_work);
  uthread_mutex_unlock(pool->mutex);
  for (int threadno = 0; threadno < pool->num_threads; threadno++) {
    uthread_join(pool->threads[threadno], 0);
  }
  uthread_cond_destroy(pool->ready_to_work);
  uthread_cond_destroy(pool->finished);
  uthread_mutex_destroy(pool->mutex);
  free(pool);
}