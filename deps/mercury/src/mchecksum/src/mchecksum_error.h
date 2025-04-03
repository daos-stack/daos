/**
 * Copyright (c) 2013-2021 UChicago Argonne, LLC and The HDF Group.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MCHECKSUM_ERROR_H
#define MCHECKSUM_ERROR_H

#include "mchecksum_config.h"

/*****************/
/* Public Macros */
/*****************/

#include <stdio.h>
#define MCHECKSUM_LOG_WRITE(_stream, ...)                                      \
    do {                                                                       \
        fprintf(_stream, __VA_ARGS__);                                         \
        fprintf(_stream, "\n");                                                \
    } while (0)
#ifdef MCHECKSUM_HAS_DEBUG
#    define MCHECKSUM_LOG_ERROR(...)   MCHECKSUM_LOG_WRITE(stderr, __VA_ARGS__)
#    define MCHECKSUM_LOG_WARNING(...) MCHECKSUM_LOG_WRITE(stdout, __VA_ARGS__)
#    define MCHECKSUM_LOG_DEBUG(...)   MCHECKSUM_LOG_WRITE(stdout, __VA_ARGS__)
#else
#    define MCHECKSUM_LOG_ERROR(...)   (void) 0
#    define MCHECKSUM_LOG_WARNING(...) (void) 0
#    define MCHECKSUM_LOG_DEBUG(...)   (void) 0
#endif

/* Branch predictor hints */
#ifndef _WIN32
#    define likely(x)   __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#else
#    define likely(x)   (x)
#    define unlikely(x) (x)
#endif

/* Error macros */
#define MCHECKSUM_GOTO_DONE(label, ret, ret_val)                               \
    do {                                                                       \
        ret = ret_val;                                                         \
        goto label;                                                            \
    } while (0)

#define MCHECKSUM_GOTO_ERROR(label, ret, err_val, ...)                         \
    do {                                                                       \
        MCHECKSUM_LOG_ERROR(__VA_ARGS__);                                      \
        ret = err_val;                                                         \
        goto label;                                                            \
    } while (0)

/* Check for rc value and goto label */
#define MCHECKSUM_CHECK_RC_ERROR(label, rc, ...)                               \
    do {                                                                       \
        if (unlikely(rc != 0)) {                                               \
            MCHECKSUM_LOG_ERROR(__VA_ARGS__);                                  \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* Check for cond, set ret to err_val and goto label */
#define MCHECKSUM_CHECK_ERROR(cond, label, ret, err_val, ...)                  \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            MCHECKSUM_LOG_ERROR(__VA_ARGS__);                                  \
            ret = err_val;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define MCHECKSUM_CHECK_ERROR_NORET(cond, label, ...)                          \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            MCHECKSUM_LOG_ERROR(__VA_ARGS__);                                  \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define MCHECKSUM_CHECK_ERROR_DONE(cond, ...)                                  \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            MCHECKSUM_LOG_ERROR(__VA_ARGS__);                                  \
        }                                                                      \
    } while (0)

/* Check for cond and print warning */
#define MCHECKSUM_CHECK_WARNING(cond, ...)                                     \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            MCHECKSUM_LOG_WARNING(__VA_ARGS__);                                \
        }                                                                      \
    } while (0)

#endif /* MCHECKSUM_ERROR_H */
