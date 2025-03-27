/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup MUTEX Mutex
 * This group is for Mutex.
 */

/**
 * @ingroup MUTEX
 * @brief   Create a new mutex.
 *
 * \c ABT_mutex_create() creates a new mutex with default attributes and returns
 * its handle through \c newmutex.
 *
 * \c newmutex must be freed by \c ABT_mutex_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newmutex, \c ABT_MUTEX_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c newmutex}
 *
 * @param[out] newmutex  mutex handle
 * @return Error code
 */
int ABT_mutex_create(ABT_mutex *newmutex)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newmutex);

    /* Check if the size of ABT_mutex_memory is okay. */
    ABTI_STATIC_ASSERT(sizeof(ABTI_mutex) <= sizeof(ABT_mutex_memory));

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newmutex to NULL on error. */
    *newmutex = ABT_MUTEX_NULL;
#endif
    ABTI_mutex *p_newmutex;

    int abt_errno = ABTU_malloc(sizeof(ABTI_mutex), (void **)&p_newmutex);
    ABTI_CHECK_ERROR(abt_errno);
    ABTI_mutex_init(p_newmutex);

    /* Return value */
    *newmutex = ABTI_mutex_get_handle(p_newmutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Create a new mutex with mutex attributes.
 *
 * \c ABT_mutex_create_with_attr() creates a new mutex configured with the mutex
 * attribute \c attr and returns its handle through \c newmutex.  If \c attr is
 * \c ABT_MUTEX_ATTR_NULL, \c newmutex has default attributes.
 *
 * @note
 * \DOC_NOTE_DEFAULT_MUTEX_ATTRIBUTE
 *
 * This routine does not take the ownership of \c attr, so it is the user's
 * responsibility to free \c attr after its use.
 *
 * \c newmutex must be freed by \c ABT_mutex_free() after its use.
 *
 * @changev11
 * \DOC_DESC_V10_REJECT_MUTEX_ATTR_NULL{\c attr}
 * @endchangev11
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newmutex, \c ABT_MUTEX_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH

 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c newmutex}
 *
 * @param[in]  attr      mutex attribute handle
 * @param[out] newmutex  mutex handle
 * @return Error code
 */
int ABT_mutex_create_with_attr(ABT_mutex_attr attr, ABT_mutex *newmutex)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(newmutex);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    /* Argobots 1.x sets newmutex to NULL on error. */
    *newmutex = ABT_MUTEX_NULL;
#endif
    ABTI_mutex_attr *p_attr = ABTI_mutex_attr_get_ptr(attr);
    ABTI_mutex *p_newmutex;

    int abt_errno = ABTU_malloc(sizeof(ABTI_mutex), (void **)&p_newmutex);
    ABTI_CHECK_ERROR(abt_errno);

    ABTI_mutex_init(p_newmutex);
    if (p_attr)
        p_newmutex->attrs = p_attr->attrs;

    /* Return value */
    *newmutex = ABTI_mutex_get_handle(p_newmutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Free a mutex.
 *
 * \c ABT_mutex_free() deallocates the resource used for the mutex \c mutex and
 * sets \c mutex to \c ABT_MUTEX_NULL.
 *
 * @note
 * This routine frees \c mutex regardless of whether it is locked or not.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_PTR{\c mutex}
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c mutex}
 * \DOC_UNDEFINED_WAITER{\c mutex}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c mutex}
 *
 * @param[in,out] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_free(ABT_mutex *mutex)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(mutex);

    ABT_mutex h_mutex = *mutex;
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(h_mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);
    ABTU_free(p_mutex);

    /* Return value */
    *mutex = ABT_MUTEX_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Lock a mutex.
 *
 * \c ABT_mutex_lock() locks the mutex \c mutex.  If this routine successfully
 * returns, the caller acquires \c mutex.  If \c mutex has already been locked,
 * the caller is blocked on \c mutex until \c mutex becomes available.
 *
 * If \c mutex is recursive, the same caller can acquire multiple levels of
 * ownership over \c mutex.  \c mutex remains locked until \c mutex is unlocked
 * as many times as the level of ownership.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{\c mutex is locked and
 * therefore the caller fails to take a lock}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_lock(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);
    ABTI_mutex_lock(&p_local, p_mutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Lock a mutex with low priority.
 *
 * \c ABT_mutex_lock_low() locks the mutex \c mutex with low priority while
 * \c ABT_mutex_lock() and \c ABT_mutex_lock_high() do with higher priority.
 * That is, waiters that call the high-priority mutex lock functions might be
 * prioritized over the same \c mutex.  Except for priority, the semantics of
 * \c ABT_mutex_lock_low() is the same as that of \c ABT_mutex_lock().
 *
 * @note
 * A program that relies on the scheduling order regarding mutex priorities is
 * non-conforming.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{\c mutex is locked and
 * therefore the caller fails to take a lock}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_lock_low(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);
    ABTI_mutex_lock(&p_local, p_mutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Lock a mutex with high priority.
 *
 * \c ABT_mutex_lock_high() locks the mutex \c mutex with high priority while
 * \c ABT_mutex_lock() and \c ABT_mutex_lock_low() do with lower priority.  That
 * is, waiters that call the high-priority mutex lock functions might be
 * prioritized over the same \c mutex.  Except for priority, the semantics of
 * \c ABT_mutex_lock_high() is the same as that of \c ABT_mutex_lock().
 *
 * @note
 * A program that relies on the scheduling order regarding mutex priorities is
 * non-conforming.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{\c mutex is locked and
 * therefore the caller fails to take a lock}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_lock_high(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);
    ABTI_mutex_lock(&p_local, p_mutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Attempt to lock a mutex.
 *
 * \c ABT_mutex_trylock() attempts to lock the mutex \c mutex.  If this routine
 * returns \c ABT_SUCCESS, the caller acquires the mutex.  If the caller fails
 * to take a lock, \c ABT_ERR_MUTEX_LOCKED is returned.
 *
 * If \c mutex is recursive, the same caller can acquire multiple levels of
 * ownership over \c mutex.  \c mutex remains locked until \c mutex is unlocked
 * as many times as the level of ownership.
 *
 * This trylock operation is atomically strong, so lock acquisition by this
 * routine never fails if \c mutex is not locked.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS_LOCK_ACQUIRED{\c mutex}
 * \DOC_ERROR_SUCCESS_LOCK_FAILED{\c mutex}
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_trylock(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);
    int abt_errno = ABTI_mutex_trylock(p_local, p_mutex);
    /* Trylock always needs to return an error code. */
    return abt_errno;
}

/**
 * @ingroup MUTEX
 * @brief   Lock a mutex in a busy-wait form.
 *
 * \c ABT_mutex_spinlock() locks the mutex \c mutex in a busy-wait form.  If
 * this routine successfully returns, the caller acquires \c mutex.  If \c mutex
 * has already been locked, the caller is blocked on \c mutex until \c mutex
 * becomes available.
 *
 * If \c mutex is recursive, the same caller can acquire multiple levels of
 * ownership over \c mutex.  \c mutex remain locked until \c mutex is unlocked
 * as many times as the level of ownership.
 *
 * @note
 * \c ABT_mutex_spinlock() might show a slightly better performance than
 * \c ABT_mutex_lock() if \c mutex is uncontended.  This routine, however,
 * blocks the underlying execution stream when \c mutex has already been locked
 * even if the caller is a ULT.  This busy-wait behavior is deadlock-prone, so
 * the user should carefully use this routine.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_spinlock(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);
    ABTI_mutex_spinlock(p_local, p_mutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Unlock a mutex.
 *
 * \c ABT_mutex_unlock() unlocks the mutex \c mutex.
 *
 * If \c mutex is recursive and has been locked more than once, the caller must
 * be the same as that of the corresponding locking function.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{a waiter is waiting on
 *                                                     \c mutex}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @undefined
 * \DOC_UNDEFINED_NOT_LOCKED{\c mutex}
 * \DOC_UNDEFINED_MUTEX_ILLEGAL_UNLOCK{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_unlock(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);

    /* Check if a given mutex is legal. */
    /* p_mutex must be locked. */
    ABTI_UB_ASSERT(ABTI_mutex_is_locked(p_mutex));
    /* If p_mutex is recursive, the caller of this function must be an owner. */
    ABTI_UB_ASSERT(!((p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) &&
                     p_mutex->owner_id != ABTI_self_get_thread_id(p_local)));

    ABTI_mutex_unlock(p_local, p_mutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Unlock a mutex and try to hand it over a waiter associated with the
 *          same execution stream.
 *
 * \c ABT_mutex_unlock_se() unlocks the mutex \c mutex.
 *
 * If \c mutex is recursive and has been locked more than once, the caller must
 * be the same as that of the corresponding locking function.
 *
 * After unlocking the mutex, this routine tries to hand over the ownership of
 * \c mutex to a waiter that is associated with the same execution stream as an
 * execution stream running the caller if the caller is a work unit.  If this
 * attempt fails, the behavior of this routine is the same as that of
 * \c ABT_mutex_unlock().
 *
 * @note
 * A program that relies on the handover mechanism provided by this routine is
 * non-conforming.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{a waiter is waiting on
 *                                                     \c mutex}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @undefined
 * \DOC_UNDEFINED_NOT_LOCKED{\c mutex}
 * \DOC_UNDEFINED_MUTEX_ILLEGAL_UNLOCK{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_unlock_se(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);

    /* Check if a given mutex is legal. */
    /* p_mutex must be locked. */
    ABTI_UB_ASSERT(ABTI_mutex_is_locked(p_mutex));
    /* If p_mutex is recursive, the caller of this function must be an owner. */
    ABTI_UB_ASSERT(!((p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) &&
                     p_mutex->owner_id != ABTI_self_get_thread_id(p_local)));

    ABTI_mutex_unlock(p_local, p_mutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Unlock a mutex and try to hand it over a waiter associated with an
 *          execution stream that is different from that of the caller.
 *
 * \c ABT_mutex_unlock_de() unlocks the mutex \c mutex.
 *
 * If \c mutex is recursive and has been locked more than once, the caller must
 * be the same as that of the corresponding locking function.
 *
 * After unlocking the mutex, this routine tries to hand over the ownership of
 * \c mutex to a waiter that is associated with an execution stream that is
 * different from an execution stream running the caller if the caller is a work
 * unit.  If this attempt fails, the behavior of this routine is the same as
 * that of \c ABT_mutex_unlock().
 *
 * @note
 * A program that relies on the handover mechanism provided by this routine is
 * non-conforming.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_CTXSWITCH_CONDITIONAL{a waiter is waiting on
 *                                                     \c mutex}
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 *
 * @undefined
 * \DOC_UNDEFINED_NOT_LOCKED{\c mutex}
 * \DOC_UNDEFINED_MUTEX_ILLEGAL_UNLOCK{\c mutex}
 *
 * @param[in] mutex  mutex handle
 * @return Error code
 */
int ABT_mutex_unlock_de(ABT_mutex mutex)
{
    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);

    /* Check if a given mutex is legal. */
    /* p_mutex must be locked. */
    ABTI_UB_ASSERT(ABTI_mutex_is_locked(p_mutex));
    /* If p_mutex is recursive, the caller of this function must be an owner. */
    ABTI_UB_ASSERT(!((p_mutex->attrs & ABTI_MUTEX_ATTR_RECURSIVE) &&
                     p_mutex->owner_id != ABTI_self_get_thread_id(p_local)));

    ABTI_mutex_unlock(p_local, p_mutex);
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Compare two mutex handles for equality.
 *
 * \c ABT_mutex_equal() compares two mutex handles \c mutex1 and \c mutex2 for
 * equality and returns the result through \c result.
 *
 * This routine is deprecated since its behavior is the same as comparing values
 * of \c mutex1 and \c mutex2.
 * @code{.c}
 * *result = (mutex1 == mutex2) ? ABT_TRUE : ABT_FALSE;
 * @endcode
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c result}
 *
 * @param[in]  mutex1  mutex handle 1
 * @param[in]  mutex2  mutex handle 2
 * @param[out] result  result (\c ABT_TRUE: same, \c ABT_FALSE: not same)
 * @return Error code
 */
int ABT_mutex_equal(ABT_mutex mutex1, ABT_mutex mutex2, ABT_bool *result)
{
    ABTI_UB_ASSERT(result);

    ABTI_mutex *p_mutex1 = ABTI_mutex_get_ptr(mutex1);
    ABTI_mutex *p_mutex2 = ABTI_mutex_get_ptr(mutex2);
    *result = (p_mutex1 == p_mutex2) ? ABT_TRUE : ABT_FALSE;
    return ABT_SUCCESS;
}

/**
 * @ingroup MUTEX
 * @brief   Get attributes of a mutex.
 *
 * \c ABT_mutex_get_attr() returns a newly created attribute object that is
 * copied from the attributes of the mutex \c mutex through \c attr.  Since this
 * routine allocates a mutex attribute object, it is the user's responsibility
 * to free \c attr after its use.
 *
 * @contexts
 * \DOC_CONTEXT_INIT \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_MUTEX_HANDLE{\c mutex}
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_UNINIT
 * \DOC_UNDEFINED_NULL_PTR{\c attr}
 *
 * @param[in]  mutex  mutex handle
 * @param[out] attr   mutex attribute handle
 * @return Error code
 */
int ABT_mutex_get_attr(ABT_mutex mutex, ABT_mutex_attr *attr)
{
    ABTI_UB_ASSERT(ABTI_initialized());
    ABTI_UB_ASSERT(attr);

    ABTI_mutex *p_mutex = ABTI_mutex_get_ptr(mutex);
    ABTI_CHECK_NULL_MUTEX_PTR(p_mutex);

    ABTI_mutex_attr *p_newattr;
    int abt_errno = ABTU_malloc(sizeof(ABTI_mutex_attr), (void **)&p_newattr);
    ABTI_CHECK_ERROR(abt_errno);

    /* Copy values.  Nesting count must be initialized. */
    p_newattr->attrs = p_mutex->attrs;

    /* Return value */
    *attr = ABTI_mutex_attr_get_handle(p_newattr);
    return ABT_SUCCESS;
}
