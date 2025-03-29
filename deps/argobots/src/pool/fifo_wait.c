/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"
#include "thread_queue.h"

/* FIFO_WAIT pool implementation */

static int pool_init(ABT_pool pool, ABT_pool_config config);
static void pool_free(ABT_pool pool);
static ABT_bool pool_is_empty(ABT_pool pool);
static size_t pool_get_size(ABT_pool pool);
static void pool_push(ABT_pool pool, ABT_unit unit, ABT_pool_context context);
static ABT_thread pool_pop(ABT_pool pool, ABT_pool_context context);
static ABT_thread pool_pop_wait(ABT_pool pool, double time_secs,
                                ABT_pool_context context);
static void pool_push_many(ABT_pool pool, const ABT_unit *units,
                           size_t num_units, ABT_pool_context context);
static void pool_pop_many(ABT_pool pool, ABT_thread *threads,
                          size_t max_threads, size_t *num_popped,
                          ABT_pool_context context);
static void pool_print_all(ABT_pool pool, void *arg,
                           void (*print_fn)(void *, ABT_thread));
static ABT_unit pool_create_unit(ABT_pool pool, ABT_thread thread);
static void pool_free_unit(ABT_pool pool, ABT_unit unit);

/* For backward compatibility */
static int pool_remove(ABT_pool pool, ABT_unit unit);
static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs);
static ABT_bool pool_unit_is_in_pool(ABT_unit unit);

struct data {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    thread_queue_t queue;
};
typedef struct data data_t;

static inline data_t *pool_get_data_ptr(void *p_data)
{
    return (data_t *)p_data;
}

ABTU_ret_err int
ABTI_pool_get_fifo_wait_def(ABT_pool_access access,
                            ABTI_pool_required_def *p_required_def,
                            ABTI_pool_optional_def *p_optional_def,
                            ABTI_pool_deprecated_def *p_deprecated_def)
{
    p_optional_def->p_init = pool_init;
    p_optional_def->p_free = pool_free;
    p_required_def->p_is_empty = pool_is_empty;
    p_optional_def->p_get_size = pool_get_size;
    p_required_def->p_push = pool_push;
    p_required_def->p_pop = pool_pop;
    p_optional_def->p_pop_wait = pool_pop_wait;
    p_optional_def->p_push_many = pool_push_many;
    p_optional_def->p_pop_many = pool_pop_many;
    p_optional_def->p_print_all = pool_print_all;
    p_required_def->p_create_unit = pool_create_unit;
    p_required_def->p_free_unit = pool_free_unit;

    p_deprecated_def->p_pop_timedwait = pool_pop_timedwait;
    p_deprecated_def->u_is_in_pool = pool_unit_is_in_pool;
    p_deprecated_def->p_remove = pool_remove;
    return ABT_SUCCESS;
}

/* Pool functions */

static int pool_init(ABT_pool pool, ABT_pool_config config)
{
    ABTI_UNUSED(config);
    int ret, abt_errno = ABT_SUCCESS;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);

    data_t *p_data;
    abt_errno = ABTU_malloc(sizeof(data_t), (void **)&p_data);
    ABTI_CHECK_ERROR(abt_errno);

    ret = pthread_mutex_init(&p_data->mutex, NULL);
    if (ret != 0) {
        ABTU_free(p_data);
        return ABT_ERR_SYS;
    }
    ret = pthread_cond_init(&p_data->cond, NULL);
    if (ret != 0) {
        pthread_mutex_destroy(&p_data->mutex);
        ABTU_free(p_data);
        return ABT_ERR_SYS;
    }
    thread_queue_init(&p_data->queue);

    p_pool->data = p_data;
    return abt_errno;
}

static void pool_free(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    thread_queue_free(&p_data->queue);
    pthread_mutex_destroy(&p_data->mutex);
    pthread_cond_destroy(&p_data->cond);
    ABTU_free(p_data);
}

static ABT_bool pool_is_empty(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    return thread_queue_is_empty(&p_data->queue);
}

static size_t pool_get_size(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    return thread_queue_get_size(&p_data->queue);
}

static void pool_push(ABT_pool pool, ABT_unit unit, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);

    pthread_mutex_lock(&p_data->mutex);
    thread_queue_push_tail(&p_data->queue, p_thread);
    pthread_cond_signal(&p_data->cond);
    pthread_mutex_unlock(&p_data->mutex);
}

static void pool_push_many(ABT_pool pool, const ABT_unit *units,
                           size_t num_units, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);

    if (num_units > 0) {
        pthread_mutex_lock(&p_data->mutex);
        size_t i;
        for (i = 0; i < num_units; i++) {
            ABTI_thread *p_thread =
                ABTI_unit_get_thread_from_builtin_unit(units[i]);
            thread_queue_push_tail(&p_data->queue, p_thread);
        }
        if (num_units == 1) {
            /* Wake up a single waiter. */
            pthread_cond_signal(&p_data->cond);
        } else {
            /* Wake up all the waiters. */
            pthread_cond_broadcast(&p_data->cond);
        }
        pthread_mutex_unlock(&p_data->mutex);
    }
}

static ABT_thread pool_pop_wait(ABT_pool pool, double time_secs,
                                ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    pthread_mutex_lock(&p_data->mutex);
    if (thread_queue_is_empty(&p_data->queue)) {
#if defined(ABT_CONFIG_USE_CLOCK_GETTIME)
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)time_secs;
        ts.tv_nsec += (long)((time_secs - (time_t)time_secs) * 1e9);
        if (ts.tv_nsec > 1e9) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1e9;
        }
        pthread_cond_timedwait(&p_data->cond, &p_data->mutex, &ts);
#else
        /* We cannot use pthread_cond_timedwait().  Let's use nanosleep()
         * instead */
        double start_time = ABTI_get_wtime();
        while (ABTI_get_wtime() - start_time < time_secs) {
            pthread_mutex_unlock(&p_data->mutex);
            const int sleep_nsecs = 100;
            struct timespec ts = { 0, sleep_nsecs };
            nanosleep(&ts, NULL);
            pthread_mutex_lock(&p_data->mutex);
            if (p_data->num_threads > 0)
                break;
        }
#endif
    }
    ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
    pthread_mutex_unlock(&p_data->mutex);
    return ABTI_thread_get_handle(p_thread);
}

static inline void convert_double_sec_to_timespec(struct timespec *ts_out,
                                                  double seconds)
{
    ts_out->tv_sec = (time_t)seconds;
    ts_out->tv_nsec = (long)((seconds - ts_out->tv_sec) * 1000000000.0);
}

static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    pthread_mutex_lock(&p_data->mutex);
    if (thread_queue_is_empty(&p_data->queue)) {
        struct timespec ts;
        convert_double_sec_to_timespec(&ts, abstime_secs);
        pthread_cond_timedwait(&p_data->cond, &p_data->mutex, &ts);
    }
    ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
    pthread_mutex_unlock(&p_data->mutex);
    if (p_thread) {
        return ABTI_unit_get_builtin_unit(p_thread);
    } else {
        return ABT_UNIT_NULL;
    }
}

static ABT_thread pool_pop(ABT_pool pool, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    if (!thread_queue_is_empty(&p_data->queue)) {
        pthread_mutex_lock(&p_data->mutex);
        ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
        pthread_mutex_unlock(&p_data->mutex);
        return ABTI_thread_get_handle(p_thread);
    } else {
        return ABT_THREAD_NULL;
    }
}

static void pool_pop_many(ABT_pool pool, ABT_thread *threads,
                          size_t max_threads, size_t *num_popped,
                          ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    if (max_threads != 0 && !thread_queue_is_empty(&p_data->queue)) {
        pthread_mutex_lock(&p_data->mutex);
        size_t i;
        for (i = 0; i < max_threads; i++) {
            ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
            if (!p_thread)
                break;
            threads[i] = ABTI_thread_get_handle(p_thread);
        }
        *num_popped = i;
        pthread_mutex_unlock(&p_data->mutex);
    } else {
        *num_popped = 0;
    }
}

static int pool_remove(ABT_pool pool, ABT_unit unit)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);

    ABTI_CHECK_TRUE(!thread_queue_is_empty(&p_data->queue), ABT_ERR_POOL);
    ABTI_CHECK_TRUE(ABTD_atomic_acquire_load_int(&p_thread->is_in_pool) == 1,
                    ABT_ERR_POOL);

    pthread_mutex_lock(&p_data->mutex);
    int abt_errno = thread_queue_remove(&p_data->queue, p_thread);
    pthread_mutex_unlock(&p_data->mutex);
    return abt_errno;
}

static void pool_print_all(ABT_pool pool, void *arg,
                           void (*print_fn)(void *, ABT_thread))
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);

    pthread_mutex_lock(&p_data->mutex);
    thread_queue_print_all(&p_data->queue, arg, print_fn);
    pthread_mutex_unlock(&p_data->mutex);
}

/* Unit functions */

static ABT_bool pool_unit_is_in_pool(ABT_unit unit)
{
    ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);
    return ABTD_atomic_acquire_load_int(&p_thread->is_in_pool) ? ABT_TRUE
                                                               : ABT_FALSE;
}

static ABT_unit pool_create_unit(ABT_pool pool, ABT_thread thread)
{
    /* Call ABTI_unit_init_builtin() instead. */
    ABTI_ASSERT(0);
    return ABT_UNIT_NULL;
}

static void pool_free_unit(ABT_pool pool, ABT_unit unit)
{
    /* A built-in unit does not need to be freed.  This function may not be
     * called. */
    ABTI_ASSERT(0);
}
