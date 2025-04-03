/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

ABTU_ret_err static int timer_alloc(ABTI_timer **pp_newtimer);

/** @defgroup TIMER  Timer
 * This group is for Timer.
 */

/**
 * @ingroup TIMER
 * @brief   Get elapsed wall clock time.
 *
 * \c ABT_get_wtime() returns the elapsed wall clock time in seconds since an
 * arbitrary time in the past.
 *
 * \DOC_DESC_TIMER_RESOLUTION
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @return Elapsed wall clock time in seconds
 */
double ABT_get_wtime(void)
{
    return ABTI_get_wtime();
}

/**
 * @ingroup TIMER
 * @brief   Create a new timer.
 *
 * \c ABT_timer_create() creates a new timer and returns its handle through
 * \c newtimer.  The initial start time and stop time of \c newtimer are
 * undefined.
 *
 * The created timer must be freed by \c ABT_timer_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newtimer, \c ABT_TIMER_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c newtimer}
 *
 * @param[out] newtimer  timer handle
 * @return Error code
 */
int ABT_timer_create(ABT_timer *newtimer)
{
    ABTI_UB_ASSERT(newtimer);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *newtimer = ABT_TIMER_NULL;
#endif
    ABTI_timer *p_newtimer;
    int abt_errno = timer_alloc(&p_newtimer);
    ABTI_CHECK_ERROR(abt_errno);

    *newtimer = ABTI_timer_get_handle(p_newtimer);
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Duplicate a timer.
 *
 * \c ABT_timer_dup() creates a new timer and copies the start and stop time
 * of the timer \c timer to the new timer.  The handle of the new timer is
 * returned through \c newtimer.
 *
 * The created timer must be freed by \c ABT_timer_free() after its use.
 *
 * @changev20
 * \DOC_DESC_V1X_SET_VALUE_ON_ERROR{\c newtimer, \c ABT_TIMER_NULL}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 * \DOC_ERROR_INV_TIMER_HANDLE{\c timer}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c newtimer}
 *
 * @param[in]  timer     handle to a timer to be duplicated
 * @param[out] newtimer  handle to a new timer
 * @return Error code
 */
int ABT_timer_dup(ABT_timer timer, ABT_timer *newtimer)
{
    ABTI_UB_ASSERT(newtimer);

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    *newtimer = ABT_TIMER_NULL;
#endif
    ABTI_timer *p_timer = ABTI_timer_get_ptr(timer);
    ABTI_CHECK_NULL_TIMER_PTR(p_timer);

    ABTI_timer *p_newtimer;
    int abt_errno = timer_alloc(&p_newtimer);
    ABTI_CHECK_ERROR(abt_errno);

    memcpy(p_newtimer, p_timer, sizeof(ABTI_timer));
    *newtimer = ABTI_timer_get_handle(p_newtimer);
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Free a timer.
 *
 * \c ABT_timer_free() deallocates the resource used for the timer \c timer and
 * sets \c timer to \c ABT_TIMER_NULL.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_TIMER_PTR{\c timer}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c timer}
 * \DOC_UNDEFINED_THREAD_UNSAFE_FREE{\c timer}
 *
 * @param[in,out] timer  timer handle
 * @return Error code
 */
int ABT_timer_free(ABT_timer *timer)
{
    ABTI_UB_ASSERT(timer);

    ABTI_timer *p_timer = ABTI_timer_get_ptr(*timer);
    ABTI_CHECK_NULL_TIMER_PTR(p_timer);

    /* We use libc malloc/free for ABT_timer because ABTU_malloc/free might
     * need the initialization of Argobots if they are not the same as libc
     * malloc/free.  This is to allow ABT_timer to be used irrespective of
     * Argobots initialization. */
    free(p_timer);
    *timer = ABT_TIMER_NULL;
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Start a timer.
 *
 * \c ABT_timer_start() sets the start time of the timer \c timer to the current
 * time.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_TIMER_HANDLE{\c timer}
 *
 * @undefined
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c timer}
 *
 * @param[in] timer  timer handle
 * @return Error code
 */
int ABT_timer_start(ABT_timer timer)
{
    ABTI_timer *p_timer = ABTI_timer_get_ptr(timer);
    ABTI_CHECK_NULL_TIMER_PTR(p_timer);

    ABTD_time_get(&p_timer->start);
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Stop a timer.
 *
 * \c ABT_timer_stop() sets the stop time of the timer \c timer to the current
 * time.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_TIMER_HANDLE{\c timer}
 *
 * @undefined
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c timer}
 *
 * @param[in] timer  timer handle
 * @return Error code
 */
int ABT_timer_stop(ABT_timer timer)
{
    ABTI_timer *p_timer = ABTI_timer_get_ptr(timer);
    ABTI_CHECK_NULL_TIMER_PTR(p_timer);

    ABTD_time_get(&p_timer->end);
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Read the elapsed time of the timer.
 *
 * \c ABT_timer_read() returns the time difference in seconds between the start
 * time and the stop time of the timer \c timer through \c secs.  If either the
 * start time or the stop time of \c timer has not been set, \c secs is set to
 * an undefined value.
 *
 * \DOC_DESC_TIMER_RESOLUTION
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_TIMER_HANDLE{\c timer}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c secs}
 *
 * @param[in]  timer  timer handle
 * @param[out] secs   elapsed time in seconds
 * @return Error code
 */
int ABT_timer_read(ABT_timer timer, double *secs)
{
    ABTI_UB_ASSERT(secs);

    ABTI_timer *p_timer = ABTI_timer_get_ptr(timer);
    ABTI_CHECK_NULL_TIMER_PTR(p_timer);

    double start = ABTD_time_read_sec(&p_timer->start);
    double end = ABTD_time_read_sec(&p_timer->end);

    *secs = end - start;
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Stop a timer and read an elapsed time of a timer.
 *
 * \c ABT_timer_stop_and_read() sets the stop time of the timer \c timer to the
 * current time and returns the time difference in seconds between the start
 * time and the stop time of \c timer through \c secs.  If the start time of
 * \c timer has not been set, \c secs is set to an undefined value.
 *
 * \DOC_DESC_TIMER_RESOLUTION
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_TIMER_HANDLE{\c timer}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c secs}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c timer}
 *
 * @param[in]  timer  timer handle
 * @param[out] secs   elapsed time in seconds
 * @return Error code
 */
int ABT_timer_stop_and_read(ABT_timer timer, double *secs)
{
    ABTI_UB_ASSERT(secs);

    ABTI_timer *p_timer = ABTI_timer_get_ptr(timer);
    ABTI_CHECK_NULL_TIMER_PTR(p_timer);

    ABTD_time_get(&p_timer->end);
    double start = ABTD_time_read_sec(&p_timer->start);
    double end = ABTD_time_read_sec(&p_timer->end);

    *secs = end - start;
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Stop a timer and add an elapsed time of a timer.
 *
 * \c ABT_timer_stop_and_add() sets the stop time of the timer \c timer to the
 * current time and adds the time difference in seconds between the start time
 * and the stop time of \c timer to \c secs.  If the start time has not been
 * set, the returned value is undefined.
 *
 * \DOC_DESC_TIMER_RESOLUTION
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_INV_TIMER_HANDLE{\c timer}
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c secs}
 * \DOC_UNDEFINED_THREAD_UNSAFE{\c timer}
 *
 * @param[in]     timer  timer handle
 * @param[in,out] secs   accumulated elapsed time in seconds
 * @return Error code
 */
int ABT_timer_stop_and_add(ABT_timer timer, double *secs)
{
    ABTI_UB_ASSERT(secs);

    ABTI_timer *p_timer = ABTI_timer_get_ptr(timer);
    ABTI_CHECK_NULL_TIMER_PTR(p_timer);

    ABTD_time_get(&p_timer->end);
    double start = ABTD_time_read_sec(&p_timer->start);
    double end = ABTD_time_read_sec(&p_timer->end);

    *secs += (end - start);
    return ABT_SUCCESS;
}

/**
 * @ingroup TIMER
 * @brief   Obtain an overhead time of using ABT_timer.
 *
 * \c ABT_timer_get_overhead() returns the overhead time when measuring the
 * elapsed time with \c ABT_timer and returns the overhead time in seconds
 * through \c overhead.  It measures the time difference in consecutive calls
 * of \c ABT_timer_start() and \c ABT_timer_stop().
 *
 * \DOC_DESC_TIMER_RESOLUTION
 *
 * This function is deprecated because the returned overhead is not a reliable
 * value.  The users are recommended to write their own benchmarks to measure
 * the performance.
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_ERROR_SUCCESS
 * \DOC_ERROR_RESOURCE
 *
 * @undefined
 * \DOC_UNDEFINED_NULL_PTR{\c overhead}
 *
 * @param[out] overhead  overhead time of \c ABT_timer
 * @return Error code
 */
int ABT_timer_get_overhead(double *overhead)
{
    ABTI_UB_ASSERT(overhead);

    int abt_errno;
    ABT_timer h_timer;
    int i;
    const int iter = 5000;
    double secs, sum = 0.0;

    abt_errno = ABT_timer_create(&h_timer);
    ABTI_CHECK_ERROR(abt_errno);

    for (i = 0; i < iter; i++) {
        ABT_timer_start(h_timer);
        ABT_timer_stop(h_timer);
        ABT_timer_read(h_timer, &secs);
        sum += secs;
    }

    abt_errno = ABT_timer_free(&h_timer);
    ABTI_CHECK_ERROR(abt_errno);

    *overhead = sum / iter;
    return ABT_SUCCESS;
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

ABTU_ret_err static int timer_alloc(ABTI_timer **pp_newtimer)
{
    /* We use libc malloc/free for ABT_timer because ABTU_malloc/free might
     * need the initialization of Argobots if they are not the same as libc
     * malloc/free.  This is to allow ABT_timer to be used irrespective of
     * Argobots initialization. */
    ABTI_timer *p_newtimer = (ABTI_timer *)malloc(sizeof(ABTI_timer));
    ABTI_CHECK_TRUE(p_newtimer != NULL, ABT_ERR_MEM);

    *pp_newtimer = p_newtimer;
    return ABT_SUCCESS;
}
