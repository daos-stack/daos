/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_TEST_H
#define MERCURY_TEST_H

#include "na_test.h"

#include "mercury.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

struct hg_test_info {
    struct na_test_info na_test_info; /* NA test info */
    hg_class_t *hg_class;             /* Default HG class */
    hg_class_t **hg_classes;          /* Array of HG classes */
    unsigned int handle_max;          /* Max number of handles in-flight */
    unsigned int thread_count;        /* Max number of threads */
    unsigned int multi_recv_op_max;   /* Max number of multi-recv ops */
    unsigned int request_post_init;   /* Init number of posted handles */
    hg_bool_t auto_sm;                /* Use shared-memory */
    hg_bool_t bidirectional;          /* Bidirectional tests */
};

/*****************/
/* Public Macros */
/*****************/

/* Default error macro */
#include "mercury_log.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

extern HG_PRIVATE HG_LOG_OUTLET_DECL(hg_test);
#define HG_TEST_LOG_ERROR(...)                                                 \
    HG_LOG_WRITE(hg_test, HG_LOG_LEVEL_ERROR, __VA_ARGS__)
#define HG_TEST_LOG_WARNING(...)                                               \
    HG_LOG_WRITE(hg_test, HG_LOG_LEVEL_WARNING, __VA_ARGS__)
#ifdef HG_HAS_DEBUG
#    define HG_TEST_LOG_DEBUG(...)                                             \
        HG_LOG_WRITE(hg_test, HG_LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#    define HG_TEST_LOG_DEBUG(...) (void) 0
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
#define HG_TEST_GOTO_DONE(label, ret, ret_val)                                 \
    do {                                                                       \
        ret = ret_val;                                                         \
        goto label;                                                            \
    } while (0)

#define HG_TEST_GOTO_ERROR(label, ret, err_val, ...)                           \
    do {                                                                       \
        HG_LOG_ERROR(__VA_ARGS__);                                             \
        ret = err_val;                                                         \
        goto label;                                                            \
    } while (0)

/* Check for hg_ret value and goto label */
#define HG_TEST_CHECK_HG_ERROR(label, hg_ret, ...)                             \
    do {                                                                       \
        if (unlikely(hg_ret != HG_SUCCESS)) {                                  \
            HG_TEST_LOG_ERROR(__VA_ARGS__);                                    \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* Check for cond, set ret to err_val and goto label */
#define HG_TEST_CHECK_ERROR(cond, label, ret, err_val, ...)                    \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_TEST_LOG_ERROR(__VA_ARGS__);                                    \
            ret = err_val;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define HG_TEST_CHECK_ERROR_NORET(cond, label, ...)                            \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_TEST_LOG_ERROR(__VA_ARGS__);                                    \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define HG_TEST_CHECK_ERROR_DONE(cond, ...)                                    \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_TEST_LOG_ERROR(__VA_ARGS__);                                    \
        }                                                                      \
    } while (0)

/* Check for cond and print warning */
#define HG_TEST_CHECK_WARNING(cond, ...)                                       \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_TEST_LOG_WARNING(__VA_ARGS__);                                  \
        }                                                                      \
    } while (0)

#define HG_TEST(x)                                                             \
    do {                                                                       \
        printf("Testing %-62s", x);                                            \
        fflush(stdout);                                                        \
    } while (0)

#define HG_PASSED()                                                            \
    do {                                                                       \
        puts(" PASSED");                                                       \
        fflush(stdout);                                                        \
    } while (0)

#define HG_FAILED()                                                            \
    do {                                                                       \
        puts("*FAILED*");                                                      \
        fflush(stdout);                                                        \
    } while (0)

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize client/server
 */
hg_return_t
HG_Test_init(int argc, char *argv[], struct hg_test_info *hg_test_info);

/**
 * Finalize client/server
 */
hg_return_t
HG_Test_finalize(struct hg_test_info *hg_test_info);

/**
 * Disable log (e.g., for tests that produce errors)
 */
void
HG_Test_log_disable(void);

/**
 * Re-enable log (e.g., for tests that produce errors)
 */
void
HG_Test_log_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_TEST_H */
