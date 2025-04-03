/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_LOG_H_INCLUDED
#define ABTI_LOG_H_INCLUDED

#include "abt_config.h"

#ifdef ABT_CONFIG_USE_DEBUG_LOG

void ABTI_log_debug(const char *format, ...);
void ABTI_log_debug_thread(const char *msg, ABTI_thread *p_thread);
void ABTI_log_pool_push(ABTI_pool *p_pool, ABT_unit unit);
void ABTI_log_pool_remove(ABTI_pool *p_pool, ABT_unit unit);
void ABTI_log_pool_pop(ABTI_pool *p_pool, ABT_thread thread);
void ABTI_log_pool_pop_many(ABTI_pool *p_pool, const ABT_thread *threads,
                            size_t num);
void ABTI_log_pool_push_many(ABTI_pool *p_pool, const ABT_unit *units,
                             size_t num);

#define LOG_DEBUG_POOL_PUSH(p_pool, unit) ABTI_log_pool_push(p_pool, unit)
#define LOG_DEBUG_POOL_REMOVE(p_pool, unit) ABTI_log_pool_remove(p_pool, unit)
#define LOG_DEBUG_POOL_POP(p_pool, thread) ABTI_log_pool_pop(p_pool, thread)
#define LOG_DEBUG_POOL_POP_MANY(p_pool, threads, num)                          \
    ABTI_log_pool_pop_many(p_pool, threads, num)
#define LOG_DEBUG_POOL_PUSH_MANY(p_pool, units, num)                           \
    ABTI_log_pool_push_many(p_pool, units, num)

#else

#define LOG_DEBUG_POOL_PUSH(p_pool, unit)                                      \
    do {                                                                       \
    } while (0)
#define LOG_DEBUG_POOL_REMOVE(p_pool, unit)                                    \
    do {                                                                       \
    } while (0)
#define LOG_DEBUG_POOL_POP(p_pool, thread)                                     \
    do {                                                                       \
    } while (0)
#define LOG_DEBUG_POOL_POP_MANY(p_pool, threads, num)                          \
    do {                                                                       \
    } while (0)
#define LOG_DEBUG_POOL_PUSH_MANY(p_pool, units, num)                           \
    do {                                                                       \
    } while (0)

#endif /* ABT_CONFIG_USE_DEBUG_LOG */

#endif /* ABTI_LOG_H_INCLUDED */
