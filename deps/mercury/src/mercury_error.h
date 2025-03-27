/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_ERROR_H
#define MERCURY_ERROR_H

#include "mercury_config.h"
#include "mercury_log.h"

#include <inttypes.h>

/* Default log outlet */
extern HG_PRIVATE HG_LOG_OUTLET_DECL(hg);

/* Fatal log outlet always 'on' by default */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(fatal, hg);

/* Specific outlets */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(cls, hg);       /* Class */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(ctx, hg);       /* Context */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(addr, hg);      /* Addresses */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(rpc, hg);       /* RPC */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(bulk, hg);      /* Bulk */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(proc, hg);      /* Proc */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(poll, hg);      /* Poll */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(rpc_ref, hg);   /* RPC ref */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(poll_loop, hg); /* Progress loop */
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(perf, hg); /* Perf related log */
#ifndef _WIN32
extern HG_PRIVATE HG_LOG_OUTLET_SUBSYS_DECL(diag, hg); /* Diagnostics */
#endif

/* Base log macros */
#define HG_LOG_ERROR(...) HG_LOG_WRITE(hg, HG_LOG_LEVEL_ERROR, __VA_ARGS__)
#define HG_LOG_SUBSYS_ERROR(subsys, ...)                                       \
    HG_LOG_SUBSYS_WRITE(subsys, hg, HG_LOG_LEVEL_ERROR, __VA_ARGS__)
#define HG_LOG_WARNING(...) HG_LOG_WRITE(hg, HG_LOG_LEVEL_WARNING, __VA_ARGS__)
#define HG_LOG_SUBSYS_WARNING(subsys, ...)                                     \
    HG_LOG_SUBSYS_WRITE(subsys, hg, HG_LOG_LEVEL_WARNING, __VA_ARGS__)
#ifdef HG_HAS_DEBUG
#    define HG_LOG_DEBUG(...) HG_LOG_WRITE(hg, HG_LOG_LEVEL_DEBUG, __VA_ARGS__)
#    define HG_LOG_SUBSYS_DEBUG(subsys, ...)                                   \
        HG_LOG_SUBSYS_WRITE(subsys, hg, HG_LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
#    define HG_LOG_DEBUG(...)        (void) 0
#    define HG_LOG_SUBSYS_DEBUG(...) (void) 0
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

/* HG_GOTO_DONE: goto label wrapper and set return value */
#define HG_GOTO_DONE(label, ret, ret_val)                                      \
    do {                                                                       \
        ret = ret_val;                                                         \
        goto label;                                                            \
    } while (0)

/* HG_GOTO_ERROR: goto label wrapper and set return value / log error */
#define HG_GOTO_ERROR(label, ret, err_val, ...)                                \
    do {                                                                       \
        HG_LOG_ERROR(__VA_ARGS__);                                             \
        ret = err_val;                                                         \
        goto label;                                                            \
    } while (0)

/* HG_GOTO_SUBSYS_ERROR: goto label wrapper and set return value / log subsys
 * error */
#define HG_GOTO_SUBSYS_ERROR(subsys, label, ret, err_val, ...)                 \
    do {                                                                       \
        HG_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                              \
        ret = err_val;                                                         \
        goto label;                                                            \
    } while (0)

/* HG_GOTO_SUBSYS_ERROR_NORET: goto label wrapper and log subsys error */
#define HG_GOTO_SUBSYS_ERROR_NORET(subsys, label, ...)                         \
    do {                                                                       \
        HG_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                              \
        goto label;                                                            \
    } while (0)

/* HG_CHECK_HG_ERROR: HG type error check */
#define HG_CHECK_HG_ERROR(label, hg_ret, ...)                                  \
    do {                                                                       \
        if (unlikely(hg_ret != HG_SUCCESS)) {                                  \
            HG_LOG_ERROR(__VA_ARGS__);                                         \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* HG_CHECK_SUBSYS_HG_ERROR: subsys HG type error check */
#define HG_CHECK_SUBSYS_HG_ERROR(subsys, label, hg_ret, ...)                   \
    do {                                                                       \
        if (unlikely(hg_ret != HG_SUCCESS)) {                                  \
            HG_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* HG_CHECK_ERROR: error check on cond */
#define HG_CHECK_ERROR(cond, label, ret, err_val, ...)                         \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_ERROR(__VA_ARGS__);                                         \
            ret = err_val;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* HG_CHECK_SUBSYS_ERROR: subsys error check on cond */
#define HG_CHECK_SUBSYS_ERROR(subsys, cond, label, ret, err_val, ...)          \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
            ret = err_val;                                                     \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* HG_CHECK_ERROR_NORET: error check / no return values */
#define HG_CHECK_ERROR_NORET(cond, label, ...)                                 \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_ERROR(__VA_ARGS__);                                         \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* HG_CHECK_SUBSYS_ERROR_NORET: subsys error check / no return values */
#define HG_CHECK_SUBSYS_ERROR_NORET(subsys, cond, label, ...)                  \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
            goto label;                                                        \
        }                                                                      \
    } while (0)

/* HG_CHECK_ERROR_DONE: error check after clean up / done labels */
#define HG_CHECK_ERROR_DONE(cond, ...)                                         \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_ERROR(__VA_ARGS__);                                         \
        }                                                                      \
    } while (0)

/* HG_CHECK_SUBSYS_ERROR_DONE: subsys error check after clean up labels */
#define HG_CHECK_SUBSYS_ERROR_DONE(subsys, cond, ...)                          \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_SUBSYS_ERROR(subsys, __VA_ARGS__);                          \
        }                                                                      \
    } while (0)

/* HG_CHECK_WARNING: warning check on cond */
#define HG_CHECK_WARNING(cond, ...)                                            \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_WARNING(__VA_ARGS__);                                       \
        }                                                                      \
    } while (0)

/* HG_CHECK_SUBSYS_WARNING: subsys warning check on cond */
#define HG_CHECK_SUBSYS_WARNING(subsys, cond, ...)                             \
    do {                                                                       \
        if (unlikely(cond)) {                                                  \
            HG_LOG_SUBSYS_WARNING(subsys, __VA_ARGS__);                        \
        }                                                                      \
    } while (0)

#endif /* MERCURY_ERROR_H */
