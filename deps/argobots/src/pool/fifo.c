/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"
#include "thread_queue.h"
#include <time.h>

/* FIFO pool implementation */

static int pool_init(ABT_pool pool, ABT_pool_config config);
static void pool_free(ABT_pool pool);
static ABT_bool pool_is_empty(ABT_pool pool);
static size_t pool_get_size(ABT_pool pool);
static void pool_push_shared(ABT_pool pool, ABT_unit unit,
                             ABT_pool_context context);
static void pool_push_private(ABT_pool pool, ABT_unit unit,
                              ABT_pool_context context);
static ABT_thread pool_pop_shared(ABT_pool pool, ABT_pool_context context);
static ABT_thread pool_pop_private(ABT_pool pool, ABT_pool_context context);
static ABT_thread pool_pop_wait(ABT_pool pool, double time_secs,
                                ABT_pool_context context);
static void pool_push_many_shared(ABT_pool pool, const ABT_unit *units,
                                  size_t num_units, ABT_pool_context context);
static void pool_push_many_private(ABT_pool pool, const ABT_unit *units,
                                   size_t num_units, ABT_pool_context context);
static void pool_pop_many_shared(ABT_pool pool, ABT_thread *threads,
                                 size_t max_threads, size_t *num_popped,
                                 ABT_pool_context context);
static void pool_pop_many_private(ABT_pool pool, ABT_thread *threads,
                                  size_t max_threads, size_t *num_popped,
                                  ABT_pool_context context);
static void pool_print_all(ABT_pool pool, void *arg,
                           void (*print_fn)(void *, ABT_thread));
static ABT_unit pool_create_unit(ABT_pool pool, ABT_thread thread);
static void pool_free_unit(ABT_pool pool, ABT_unit unit);

/* For backward compatibility */
static int pool_remove_shared(ABT_pool pool, ABT_unit unit);
static int pool_remove_private(ABT_pool pool, ABT_unit unit);
static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs);
static ABT_bool pool_unit_is_in_pool(ABT_unit unit);

struct data {
    ABTD_spinlock mutex;
    thread_queue_t queue;
};
typedef struct data data_t;

static inline data_t *pool_get_data_ptr(void *p_data)
{
    return (data_t *)p_data;
}

/* Obtain the FIFO pool definition according to the access type */
ABTU_ret_err int
ABTI_pool_get_fifo_def(ABT_pool_access access,
                       ABTI_pool_required_def *p_required_def,
                       ABTI_pool_optional_def *p_optional_def,
                       ABTI_pool_deprecated_def *p_deprecated_def)
{
    /* Definitions according to the access type */
    /* FIXME: need better implementation, e.g., lock-free one */
    switch (access) {
        case ABT_POOL_ACCESS_PRIV:
            p_required_def->p_push = pool_push_private;
            p_required_def->p_pop = pool_pop_private;
            p_optional_def->p_push_many = pool_push_many_private;
            p_optional_def->p_pop_many = pool_pop_many_private;
            p_deprecated_def->p_remove = pool_remove_private;
            break;

        case ABT_POOL_ACCESS_SPSC:
        case ABT_POOL_ACCESS_MPSC:
        case ABT_POOL_ACCESS_SPMC:
        case ABT_POOL_ACCESS_MPMC:
            p_required_def->p_push = pool_push_shared;
            p_required_def->p_pop = pool_pop_shared;
            p_optional_def->p_push_many = pool_push_many_shared;
            p_optional_def->p_pop_many = pool_pop_many_shared;
            p_deprecated_def->p_remove = pool_remove_shared;
            break;

        default:
            ABTI_HANDLE_ERROR(ABT_ERR_INV_POOL_ACCESS);
    }

    /* Common definitions regardless of the access type */
    p_optional_def->p_init = pool_init;
    p_optional_def->p_free = pool_free;
    p_required_def->p_is_empty = pool_is_empty;
    p_optional_def->p_get_size = pool_get_size;
    p_optional_def->p_pop_wait = pool_pop_wait;
    p_optional_def->p_print_all = pool_print_all;
    p_required_def->p_create_unit = pool_create_unit;
    p_required_def->p_free_unit = pool_free_unit;

    p_deprecated_def->p_pop_timedwait = pool_pop_timedwait;
    p_deprecated_def->u_is_in_pool = pool_unit_is_in_pool;
    return ABT_SUCCESS;
}

/* Pool functions */

static int pool_init(ABT_pool pool, ABT_pool_config config)
{
    ABTI_UNUSED(config);
    int abt_errno = ABT_SUCCESS;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    ABT_pool_access access;

    data_t *p_data;
    abt_errno = ABTU_malloc(sizeof(data_t), (void **)&p_data);
    ABTI_CHECK_ERROR(abt_errno);

    access = p_pool->access;
    if (access != ABT_POOL_ACCESS_PRIV) {
        /* Initialize the mutex */
        ABTD_spinlock_clear(&p_data->mutex);
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

static void pool_push_shared(ABT_pool pool, ABT_unit unit,
                             ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);
    ABTD_spinlock_acquire(&p_data->mutex);
    thread_queue_push_tail(&p_data->queue, p_thread);
    ABTD_spinlock_release(&p_data->mutex);
}

static void pool_push_private(ABT_pool pool, ABT_unit unit,
                              ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);
    thread_queue_push_tail(&p_data->queue, p_thread);
}

static void pool_push_many_shared(ABT_pool pool, const ABT_unit *units,
                                  size_t num_units, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    if (num_units > 0) {
        ABTD_spinlock_acquire(&p_data->mutex);
        size_t i;
        for (i = 0; i < num_units; i++) {
            ABTI_thread *p_thread =
                ABTI_unit_get_thread_from_builtin_unit(units[i]);
            thread_queue_push_tail(&p_data->queue, p_thread);
        }
        ABTD_spinlock_release(&p_data->mutex);
    }
}

static void pool_push_many_private(ABT_pool pool, const ABT_unit *units,
                                   size_t num_units, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    size_t i;
    for (i = 0; i < num_units; i++) {
        ABTI_thread *p_thread =
            ABTI_unit_get_thread_from_builtin_unit(units[i]);
        thread_queue_push_tail(&p_data->queue, p_thread);
    }
}

static ABT_thread pool_pop_wait(ABT_pool pool, double time_secs,
                                ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    double time_start = 0.0;
    while (1) {
        if (thread_queue_acquire_spinlock_if_not_empty(&p_data->queue,
                                                       &p_data->mutex) == 0) {
            ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
            ABTD_spinlock_release(&p_data->mutex);
            if (p_thread)
                return ABTI_thread_get_handle(p_thread);
        }
        if (time_start == 0.0) {
            time_start = ABTI_get_wtime();
        } else {
            double elapsed = ABTI_get_wtime() - time_start;
            if (elapsed > time_secs)
                return ABT_THREAD_NULL;
        }
        /* Sleep. */
        const int sleep_nsecs = 100;
        struct timespec ts = { 0, sleep_nsecs };
        nanosleep(&ts, NULL);
    }
}

static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    while (1) {
        if (thread_queue_acquire_spinlock_if_not_empty(&p_data->queue,
                                                       &p_data->mutex) == 0) {
            ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
            ABTD_spinlock_release(&p_data->mutex);
            if (p_thread) {
                return ABTI_unit_get_builtin_unit(p_thread);
            }
        }
        const int sleep_nsecs = 100;
        struct timespec ts = { 0, sleep_nsecs };
        nanosleep(&ts, NULL);

        if (ABTI_get_wtime() > abstime_secs)
            return ABT_UNIT_NULL;
    }
}

static ABT_thread pool_pop_shared(ABT_pool pool, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    if (thread_queue_acquire_spinlock_if_not_empty(&p_data->queue,
                                                   &p_data->mutex) == 0) {
        ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
        ABTD_spinlock_release(&p_data->mutex);
        return ABTI_thread_get_handle(p_thread);
    } else {
        return ABT_THREAD_NULL;
    }
}

static ABT_thread pool_pop_private(ABT_pool pool, ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
    return ABTI_thread_get_handle(p_thread);
}

static void pool_pop_many_shared(ABT_pool pool, ABT_thread *threads,
                                 size_t max_threads, size_t *num_popped,
                                 ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    if (max_threads != 0 &&
        thread_queue_acquire_spinlock_if_not_empty(&p_data->queue,
                                                   &p_data->mutex) == 0) {
        size_t i;
        for (i = 0; i < max_threads; i++) {
            ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
            if (!p_thread)
                break;
            threads[i] = ABTI_thread_get_handle(p_thread);
        }
        *num_popped = i;
        ABTD_spinlock_release(&p_data->mutex);
    } else {
        *num_popped = 0;
    }
}

static void pool_pop_many_private(ABT_pool pool, ABT_thread *threads,
                                  size_t max_threads, size_t *num_popped,
                                  ABT_pool_context context)
{
    (void)context;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    size_t i;
    for (i = 0; i < max_threads; i++) {
        ABTI_thread *p_thread = thread_queue_pop_head(&p_data->queue);
        if (!p_thread)
            break;
        threads[i] = ABTI_thread_get_handle(p_thread);
    }
    *num_popped = i;
}

static int pool_remove_shared(ABT_pool pool, ABT_unit unit)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);
    ABTD_spinlock_acquire(&p_data->mutex);
    int abt_errno = thread_queue_remove(&p_data->queue, p_thread);
    ABTD_spinlock_release(&p_data->mutex);
    return abt_errno;
}

static int pool_remove_private(ABT_pool pool, ABT_unit unit)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);
    ABTI_thread *p_thread = ABTI_unit_get_thread_from_builtin_unit(unit);
    return thread_queue_remove(&p_data->queue, p_thread);
}

static void pool_print_all(ABT_pool pool, void *arg,
                           void (*print_fn)(void *, ABT_thread))
{
    ABT_pool_access access;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    data_t *p_data = pool_get_data_ptr(p_pool->data);

    access = p_pool->access;
    if (access != ABT_POOL_ACCESS_PRIV) {
        ABTD_spinlock_acquire(&p_data->mutex);
    }
    thread_queue_print_all(&p_data->queue, arg, print_fn);
    if (access != ABT_POOL_ACCESS_PRIV) {
        ABTD_spinlock_release(&p_data->mutex);
    }
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
