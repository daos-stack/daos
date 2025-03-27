/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NA_ERROR_H
#define NA_ERROR_H

#include "na_config.h"

#include "mercury_log.h"

#include <inttypes.h>

/* Default log outlet */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_DECL(na);

/* Fatal log outlet always 'on' by default */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(fatal, na);

/* Specific outlets */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(cls, na); /* Class */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(ctx, na); /* Context */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(op, na);  /* Operations */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(addr, na); /* Addresses */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(msg, na);  /* Messages */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(mem, na);  /* Memory */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(rma, na);  /* RMA */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(poll, na); /* Progress */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(
    poll_loop, na); /* Progress loop */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(ip, na); /* IP res */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(
    perf, na); /* Perf related log */

/* Plugin specific log (must be declared here to prevent contructor issues) */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(
    libfabric, na); /* Libfabric log */
extern NA_PLUGIN_VISIBILITY HG_LOG_OUTLET_SUBSYS_DECL(ucx, na); /* UCX log */

/* Base log macros */
#define NA_LOG_ERROR(...) HG_LOG_WRITE(na, HG_LOG_LEVEL_ERROR, __VA_ARGS__)
#define NA_LOG_SUBSYS_ERROR(subsys, ...)                                       \
    HG_LOG_SUBSYS_WRITE(subsys, na, HG_LOG_LEVEL_ERROR, __VA_ARGS__)
#define NA_LOG_WARNING(...) HG_LOG_WRITE(na, HG_LOG_LEVEL_WARNING, __VA_ARGS__)
#define NA_LOG_SUBSYS_WARNING(subsys, ...)                                     \
    HG_LOG_SUBSYS_WRITE(subsys, na, HG_LOG_LEVEL_WARNING, __VA_ARGS__)
#ifdef NA_HAS_DEBUG
#    define NA_LOG_DEBUG(...) HG_LOG_WRITE(na, HG_LOG_LEVEL_DEBUG, __VA_ARGS__)
#    define NA_LOG_SUBSYS_DEBUG(subsys, ...)                                   \
        HG_LOG_SUBSYS_WRITE(subsys, na, HG_LOG_LEVEL_DEBUG, __VA_ARGS__)
#    define NA_LOG_SUBSYS_DEBUG_EXT(subsys, header, ...)                       \
        HG_LOG_SUBSYS_WRITE_DEBUG_EXT(subsys, na, header, __VA_ARGS__)
#else
#    define NA_LOG_DEBUG(...)            (void) 0
#    define NA_LOG_SUBSYS_DEBUG(...)     (void) 0
#    define NA_LOG_SUBSYS_DEBUG_EXT(...) (void) 0
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

/* NA_GOTO_DONE: goto label wrapper and set return value */
#define NA_GOTO_DONE(label, ret, ret_val)                                      \
    do {                                                                       \
        ret = ret_val;                                                         \
        goto label;                                                            \
    } while (0)

/* NA_GOTO_ERROR: goto label wrapper and set return value / log error */
#define NA_GOTO_ERROR(label, ret, err_val, ...)                                \
    do {                                                                       \
        NA_LOG_ERROR(__VA_ARGS__);                                             \
        ret = err_val;                                                         \
        goto label;                                                            \
    } while (0)

/* NA_GOTO_ERROR: goto label wrapper and set return value / log subsys error */
#define NA_GOTO_SUBSYS_ERROR(subsys, label, ret, err_val, ...)                 \
    do {                                                                       \
        NA_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                              \
        ret = err_val;                                                         \
        goto label;                                                            \
    } while (0)

/* NA_GOTO_SUBSYS_ERROR_NORET: goto label wrapper and log subsys error */
#define NA_GOTO_SUBSYS_ERROR_NORET(subsys, label, ...)                         \
    do {                                                                       \
        NA_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                              \
        goto label;                                                            \
    } while (0)

/* NA_CHECK_NA_ERROR: NA type error check */
#define NA_CHECK_NA_ERROR(label, na_ret, ...)                                  \
    do {                                                                       \
        if (unlikely(na_ret != NA_SUCCESS)) {                                  \
            NA_LOG_ERROR(__VA_ARGS__);                                         \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* NA_CHECK_SUBSYS_NA_ERROR: subsys NA type error check */
#define NA_CHECK_SUBSYS_NA_ERROR(subsys, label, na_ret, ...)                   \
    do {                                                                       \
        if (unlikely(na_ret != NA_SUCCESS)) {                                  \
            NA_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* NA_CHECK_ERROR: error check on cond */
#define NA_CHECK_ERROR(cond, label, ret, err_val, ...)                         \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_ERROR(__VA_ARGS__);                                         \
            ret = err_val;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* NA_CHECK_SUBSYS_ERROR: subsys error check on cond */
#define NA_CHECK_SUBSYS_ERROR(subsys, cond, label, ret, err_val, ...)          \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
            ret = err_val;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* NA_CHECK_ERROR_NORET: error check / no return values */
#define NA_CHECK_ERROR_NORET(cond, label, ...)                                 \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_ERROR(__VA_ARGS__);                                         \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* NA_CHECK_SUBSYS_ERROR_NORET: subsys error check / no return values */
#define NA_CHECK_SUBSYS_ERROR_NORET(subsys, cond, label, ...)                  \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* NA_CHECK_ERROR_DONE: error check after clean up / done labels */
#define NA_CHECK_ERROR_DONE(cond, ...)                                         \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_ERROR(__VA_ARGS__);                                         \
        }                                                                      \
    } while (0)

/* NA_CHECK_SUBSYS_ERROR_DONE: subsys error check after clean up labels */
#define NA_CHECK_SUBSYS_ERROR_DONE(subsys, cond, ...)                          \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
        }                                                                      \
    } while (0)

/* NA_CHECK_WARNING: warning check on cond */
#define NA_CHECK_WARNING(cond, ...)                                            \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_WARNING(__VA_ARGS__);                                       \
        }                                                                      \
    } while (0)

/* NA_CHECK_SUBSYS_WARNING: subsys warning check on cond */
#define NA_CHECK_SUBSYS_WARNING(subsys, cond, ...)                             \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            NA_LOG_SUBSYS_WARNING(subsys, __VA_ARGS__);                        \
        }                                                                      \
    } while (0)

#endif /* NA_ERROR_H */
