/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NA_TEST_H
#define NA_TEST_H

#include "na_test_mpi.h"

#ifdef HG_TEST_HAS_CXI
#    include <libcxi/libcxi.h>
#endif

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

#ifdef HG_TEST_HAS_CXI
struct na_test_cxi_info {
    struct cxil_dev *dev;
    struct cxi_svc_desc svc_desc;
};
#endif

struct na_test_info {
    na_class_t *na_class;    /* Default NA class */
    na_class_t **na_classes; /* Array of NA classes */
    char *target_name;       /* Default target name */
    char **target_names;     /* Array of targets */
    char *comm;              /* Comm/Plugin name */
    char *domain;            /* Domain name */
    char *protocol;          /* Protocol name */
    char *hostfile;          /* Hostfile */
    char *hostname;          /* Hostname */
    int port;                /* Port */
    bool listen;             /* Listen */
    bool mpi_static;         /* MPI static comm */
    bool self_send;          /* Self send */
    char *key;               /* Auth key */
    char *tclass;            /* Traffic class */
    int loop;                /* Number of loops */
    bool busy_wait;          /* Busy wait */
    size_t max_classes;      /* Max classes */
    uint8_t max_contexts;    /* Max contexts */
    uint32_t max_targets;    /* Max targets */
    size_t max_msg_size;     /* Max msg size */
    size_t buf_size_min;     /* Min buffer size */
    size_t buf_size_max;     /* Max buffer size */
    size_t buf_count;        /* Buffer count */
    bool verbose;            /* Verbose mode */
    struct na_test_mpi_info mpi_info;
#ifdef HG_TEST_HAS_CXI
    struct na_test_cxi_info cxi_info;
#endif
    bool extern_init;    /* Extern init */
    bool use_threads;    /* Use threads */
    bool force_register; /* Force registration each iteration */
    bool verify;         /* Verify data */
    bool mbps;           /* OSU-style of output in MB/s */
    bool no_multi_recv;  /* Disable multi-recv */
};

/*****************/
/* Public Macros */
/*****************/

/* Default error macro */
#include <mercury_log.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

extern NA_PRIVATE HG_LOG_OUTLET_DECL(na_test);
#define NA_TEST_LOG_ERROR(...)                                                 \
    HG_LOG_WRITE(na_test, HG_LOG_LEVEL_ERROR, __VA_ARGS__)
#define NA_TEST_LOG_WARNING(...)                                               \
    HG_LOG_WRITE(na_test, HG_LOG_LEVEL_WARNING, __VA_ARGS__)
#ifdef NA_HAS_DEBUG
#    define NA_TEST_LOG_DEBUG(...)                                             \
        HG_LOG_WRITE(na_test, HG_LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#    define NA_TEST_LOG_DEBUG(...) (void) 0
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
#define NA_TEST_GOTO_DONE(label, ret, ret_val)                                 \
    do {                                                                       \
        ret = ret_val;                                                         \
        goto label;                                                            \
    } while (0)

#define NA_TEST_GOTO_ERROR(label, ret, err_val, ...)                           \
    do {                                                                       \
        HG_LOG_ERROR(__VA_ARGS__);                                             \
        ret = err_val;                                                         \
        goto label;                                                            \
    } while (0)

/* Check for na_ret value and goto label */
#define NA_TEST_CHECK_NA_ERROR(label, na_ret, ...)                             \
    do {                                                                       \
        if (unlikely(na_ret != NA_SUCCESS)) {                                  \
            NA_TEST_LOG_ERROR(__VA_ARGS__);                                    \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* Check for cond, set ret to err_val and goto label */
#define NA_TEST_CHECK_ERROR(cond, label, ret, err_val, ...)                    \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_TEST_LOG_ERROR(__VA_ARGS__);                                    \
            ret = err_val;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define NA_TEST_CHECK_ERROR_NORET(cond, label, ...)                            \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_TEST_LOG_ERROR(__VA_ARGS__);                                    \
            goto label;                                                        \
        }                                                                      \
    } while (0)

#define NA_TEST_CHECK_ERROR_DONE(cond, ...)                                    \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_TEST_LOG_ERROR(__VA_ARGS__);                                    \
        }                                                                      \
    } while (0)

/* Check for cond and print warning */
#define NA_TEST_CHECK_WARNING(cond, ...)                                       \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_TEST_LOG_WARNING(__VA_ARGS__);                                  \
        }                                                                      \
    } while (0)

#define NA_TEST(x)                                                             \
    do {                                                                       \
        printf("Testing %-62s", x);                                            \
        fflush(stdout);                                                        \
    } while (0)

#define NA_PASSED()                                                            \
    do {                                                                       \
        puts(" PASSED");                                                       \
        fflush(stdout);                                                        \
    } while (0)

#define NA_FAILED()                                                            \
    do {                                                                       \
        puts("*FAILED*");                                                      \
        fflush(stdout);                                                        \
    } while (0)

/* Max addr name */
#define NA_TEST_MAX_ADDR_NAME 2048

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Usage
 */
void
na_test_usage(const char *execname);

/**
 * Set config file
 */
na_return_t
na_test_set_config(const char *hostfile, const char *addr_name, bool append);

/**
 * Get config file
 */
na_return_t
na_test_get_config(const char *hostfile, char *addrs[], size_t *count_p);

/**
 * Initialize
 */
na_return_t
NA_Test_init(int argc, char *argv[], struct na_test_info *na_test_info);

/**
 * Finalize
 */
na_return_t
NA_Test_finalize(struct na_test_info *na_test_info);

/**
 * Call MPI_Barrier if available
 */
void
NA_Test_barrier(const struct na_test_info *na_test_info);

/**
 * Call MPI_Bcast if available
 */
void
NA_Test_bcast(
    void *buf, size_t size, int root, const struct na_test_info *na_test_info);

#ifdef __cplusplus
}
#endif

#endif /* NA_TEST_H */
