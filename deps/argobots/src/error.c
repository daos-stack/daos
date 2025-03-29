/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup ERROR_CODE Error Code
 * This group is for Error Code.
 */

/** @defgroup ERROR Error
 * This group is for Error.
 */

/**
 * @ingroup ERROR
 * @brief   Retrieve a string of an error code and its length.
 *
 * \c ABT_error_get_str() returns a zero-terminated string of the error code
 * \c err through \c str and its length in bytes through \c len.
 *
 * If \c str is not \c NULL, the user must be responsible for passing \c str
 * that has enough space to save <i>\"the length of the string \+ 1\"</i> bytes
 * of characters.  If \c str is \c NULL, \c str is not updated.
 *
 * If \c len is not \c NULL, this routine sets the length of the string to
 * \c len.  If \c len is \c NULL, \c len is not updated.
 *
 * @note
 * The length of the string does not count the terminator \c '\0'.  For example,
 * the length of \c "Hello world" is 11.
 *
 * @changev20
 * \DOC_DESC_V1X_ERROR_CODE_CHANGE{\c ABT_ERR_OTHER, \c ABT_ERR_INV_ARG,
 *                                 \c err is not a valid error code}
 * @endchangev20
 *
 * @contexts
 * \DOC_CONTEXT_ANY \DOC_CONTEXT_NOCTXSWITCH
 *
 * @errors
 * \DOC_V1X \DOC_ERROR_OTHER_ERR_CODE{\c err}
 * \DOC_V20 \DOC_ERROR_INV_ARG_ERR_CODE{\c err}
 *
 * @param[in]  err  error code
 * @param[out] str  error string
 * @param[out] len  length of string in bytes
 * @return Error code
 */
int ABT_error_get_str(int err, char *str, size_t *len)
{
    static const char *err_str[] = { "ABT_SUCCESS",
                                     "ABT_ERR_UNINITIALIZED",
                                     "ABT_ERR_MEM",
                                     "ABT_ERR_OTHER",
                                     "ABT_ERR_INV_XSTREAM",
                                     "ABT_ERR_INV_XSTREAM_RANK",
                                     "ABT_ERR_INV_XSTREAM_BARRIER",
                                     "ABT_ERR_INV_SCHED",
                                     "ABT_ERR_INV_SCHED_KIND",
                                     "ABT_ERR_INV_SCHED_PREDEF",
                                     "ABT_ERR_INV_SCHED_TYPE",
                                     "ABT_ERR_INV_SCHED_CONFIG",
                                     "ABT_ERR_INV_POOL",
                                     "ABT_ERR_INV_POOL_KIND",
                                     "ABT_ERR_INV_POOL_ACCESS",
                                     "ABT_ERR_INV_UNIT",
                                     "ABT_ERR_INV_THREAD",
                                     "ABT_ERR_INV_THREAD_ATTR",
                                     "ABT_ERR_INV_TASK",
                                     "ABT_ERR_INV_KEY",
                                     "ABT_ERR_INV_MUTEX",
                                     "ABT_ERR_INV_MUTEX_ATTR",
                                     "ABT_ERR_INV_COND",
                                     "ABT_ERR_INV_RWLOCK",
                                     "ABT_ERR_INV_EVENTUAL",
                                     "ABT_ERR_INV_FUTURE",
                                     "ABT_ERR_INV_BARRIER",
                                     "ABT_ERR_INV_TIMER",
                                     "ABT_ERR_INV_QUERY_KIND",
                                     "ABT_ERR_XSTREAM",
                                     "ABT_ERR_XSTREAM_STATE",
                                     "ABT_ERR_XSTREAM_BARRIER",
                                     "ABT_ERR_SCHED",
                                     "ABT_ERR_SCHED_CONFIG",
                                     "ABT_ERR_POOL",
                                     "ABT_ERR_UNIT",
                                     "ABT_ERR_THREAD",
                                     "ABT_ERR_TASK",
                                     "ABT_ERR_KEY",
                                     "ABT_ERR_MUTEX",
                                     "ABT_ERR_MUTEX_LOCKED",
                                     "ABT_ERR_COND",
                                     "ABT_ERR_COND_TIMEDOUT",
                                     "ABT_ERR_RWLOCK",
                                     "ABT_ERR_EVENTUAL",
                                     "ABT_ERR_FUTURE",
                                     "ABT_ERR_BARRIER",
                                     "ABT_ERR_TIMER",
                                     "ABT_ERR_MIGRATION_TARGET",
                                     "ABT_ERR_MIGRATION_NA",
                                     "ABT_ERR_MISSING_JOIN",
                                     "ABT_ERR_FEATURE_NA",
                                     "ABT_ERR_INV_TOOL_CONTEXT",
                                     "ABT_ERR_INV_ARG",
                                     "ABT_ERR_SYS",
                                     "ABT_ERR_CPUID",
                                     "ABT_ERR_INV_POOL_CONFIG",
                                     "ABT_ERR_INV_POOL_USER_DEF" };

#ifndef ABT_CONFIG_ENABLE_VER_20_API
    ABTI_CHECK_TRUE(err >= ABT_SUCCESS &&
                        err < (int)(sizeof(err_str) / sizeof(err_str[0])),
                    ABT_ERR_OTHER);
    /* This entry does not exist. */
    ABTI_CHECK_TRUE(err_str[err], ABT_ERR_OTHER);
#else
    ABTI_CHECK_TRUE(err >= ABT_SUCCESS &&
                        err < (int)(sizeof(err_str) / sizeof(err_str[0])),
                    ABT_ERR_INV_ARG);
    /* This entry does not exist. */
    ABTI_CHECK_TRUE(err_str[err], ABT_ERR_INV_ARG);
#endif

    if (str)
        strcpy(str, err_str[err]);
    if (len)
        *len = strlen(err_str[err]);
    return ABT_SUCCESS;
}
