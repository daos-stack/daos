/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTD_H_INCLUDED
#define ABTD_H_INCLUDED

#define __USE_GNU 1
#include <pthread.h>
#include "abtd_atomic.h"
#include "abtd_context.h"
#include "abtd_spinlock.h"
#include "abtd_futex.h"

/* Data Types */
typedef enum {
    ABTD_XSTREAM_CONTEXT_STATE_RUNNING,
    ABTD_XSTREAM_CONTEXT_STATE_WAITING,
    ABTD_XSTREAM_CONTEXT_STATE_REQ_JOIN,
    ABTD_XSTREAM_CONTEXT_STATE_REQ_TERMINATE,
    ABTD_XSTREAM_CONTEXT_STATE_UNINIT,
} ABTD_xstream_context_state;
typedef struct ABTD_xstream_context {
    pthread_t native_thread;
    void *(*thread_f)(void *);
    void *p_arg;
    ABTD_xstream_context_state state;
    pthread_mutex_t state_lock;
    pthread_cond_t state_cond;
} ABTD_xstream_context;
typedef pthread_mutex_t ABTD_xstream_mutex;
#ifdef HAVE_PTHREAD_BARRIER_INIT
typedef pthread_barrier_t ABTD_xstream_barrier;
#else
typedef void *ABTD_xstream_barrier;
#endif
typedef struct ABTD_affinity_cpuset {
    size_t num_cpuids;
    int *cpuids;
} ABTD_affinity_cpuset;

/* ES Storage Qualifier */
#define ABTD_XSTREAM_LOCAL __thread

/* Environment */
void ABTD_env_init(ABTI_global *p_global);
/* Following does not need p_global. */
ABT_bool ABTD_env_get_use_debug(void);
ABT_bool ABTD_env_get_use_logging(void);
ABT_bool ABTD_env_get_print_config(void);
int ABTD_env_get_max_xstreams(void);
uint32_t ABTD_env_key_table_size(void);
size_t ABTD_env_get_sys_pagesize(void);
size_t ABTD_env_get_thread_stacksize(void);
size_t ABTD_env_get_sched_stacksize(void);
uint32_t ABTD_env_get_sched_event_freq(void);
uint64_t ABTD_env_get_sched_sleep_nsec(void);
ABT_bool ABTD_env_get_stack_guard_mprotect(ABT_bool *is_strict);

/* ES Context */
ABTU_ret_err int ABTD_xstream_context_create(void *(*f_xstream)(void *),
                                             void *p_arg,
                                             ABTD_xstream_context *p_ctx);
void ABTD_xstream_context_free(ABTD_xstream_context *p_ctx);
void ABTD_xstream_context_join(ABTD_xstream_context *p_ctx);
void ABTD_xstream_context_revive(ABTD_xstream_context *p_ctx);
void ABTD_xstream_context_set_self(ABTD_xstream_context *p_ctx);
void ABTD_xstream_context_print(ABTD_xstream_context *p_ctx, FILE *p_os,
                                int indent);

/* ES Affinity */
void ABTD_affinity_init(ABTI_global *p_global, const char *affinity_str);
void ABTD_affinity_finalize(ABTI_global *p_global);
ABTU_ret_err int ABTD_affinity_cpuset_read(ABTD_xstream_context *p_ctx,
                                           int max_cpuids, int *cpuids,
                                           int *p_num_cpuids);
ABTU_ret_err int
ABTD_affinity_cpuset_apply(ABTD_xstream_context *p_ctx,
                           const ABTD_affinity_cpuset *p_cpuset);
int ABTD_affinity_cpuset_apply_default(ABTD_xstream_context *p_ctx, int rank);
void ABTD_affinity_cpuset_destroy(ABTD_affinity_cpuset *p_cpuset);

/* ES Affinity Parser */
typedef struct ABTD_affinity_id_list {
    uint32_t num;
    int *ids; /* id here can be negative. */
} ABTD_affinity_id_list;
typedef struct ABTD_affinity_list {
    uint32_t num;
    ABTD_affinity_id_list **p_id_lists;
    void *p_mem_head; /* List to free all the allocated memory easily */
} ABTD_affinity_list;
ABTU_ret_err int
ABTD_affinity_list_create(const char *affinity_str,
                          ABTD_affinity_list **pp_affinity_list);
void ABTD_affinity_list_free(ABTD_affinity_list *p_list);

#include "abtd_stream.h"

#if defined(ABT_CONFIG_USE_CLOCK_GETTIME)
#include <time.h>
typedef struct timespec ABTD_time;

#elif defined(ABT_CONFIG_USE_MACH_ABSOLUTE_TIME)
#include <mach/mach_time.h>
typedef uint64_t ABTD_time;

#elif defined(ABT_CONFIG_USE_GETTIMEOFDAY)
#include <sys/time.h>
typedef struct timeval ABTD_time;

#endif

void ABTD_time_init(void);
void ABTD_time_get(ABTD_time *p_time);
double ABTD_time_read_sec(ABTD_time *p_time);

#endif /* ABTD_H_INCLUDED */
