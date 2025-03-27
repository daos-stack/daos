/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

/*
 * Simple resource tracer.
 *
 * This tracer assumes that each Pthread calls resource allocation functions
 * in deterministic order.  If the execution order of resource allocation calls
 * are not deterministic, this tracer does not work well.  This trace also
 * assumes that the original resource allocation functions do not occasionally
 * fail during the execution.
 */

#undef ABT_RT_ENABLED

/* The following checks the following:
 *
 * 1. dlvsym() is available (this is basically Linux only).
 * 2. symbol versions are correctly extracted.
 * 3. Address sanitizers are not used.
 *
 * Symbol versions are necessary for correct execution (e.g., "GLIBC_2.3.2" in
 * pthread_cond_init@@GLIBC_2.3.2).  dlsym(), which does not take a symbol
 * version, often finds an old symbol and mysteriously crashes a program that
 * assumes a newer symbol.  Since this rtrace is fairly optional and does not
 * need to be run on every architecture, this library is disabled if the
 * environment is doubtful.  Otherwise this rtace may crash a program for
 * unknown reasons.
 */
#ifdef ABT_RT_USE_DLVSYM
#define _GNU_SOURCE
#include <pthread.h>
#include <unistd.h>

#undef USE_PTHREAD_BARRIER
#if defined(_POSIX_BARRIERS) && _POSIX_BARRIERS > 0
#define USE_PTHREAD_BARRIER 1
#else
#define USE_PTHREAD_BARRIER 0
#endif

#undef ADDRESS_SANITIZER_ENABLED
#if defined(__GNUC__) && defined(__SANITIZE_ADDRESS__)
#define ADDRESS_SANITIZER_ENABLED 1
#elif __clang__
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ADDRESS_SANITIZER_ENABLED 1
#endif /* __has_feature(address_sanitizer) */
#endif /* defined(__has_feature) */
#endif /* __clang__ */

/* All symbol versions must be set */
#if defined(ABT_RT_MALLOC_VER) && defined(ABT_RT_CALLOC_VER) &&                \
    defined(ABT_RT_REALLOC_VER) && defined(ABT_RT_POSIX_MEMALIGN_VER) &&       \
    defined(ABT_RT_FREE_VER) && defined(ABT_RT_MMAP_VER) &&                    \
    defined(ABT_RT_MUNMAP_VER) && defined(ABT_RT_PTHREAD_CREATE_VER) &&        \
    defined(ABT_RT_PTHREAD_JOIN_VER) &&                                        \
    defined(ABT_RT_PTHREAD_MUTEX_INIT_VER) &&                                  \
    defined(ABT_RT_PTHREAD_MUTEX_DESTROY_VER) &&                               \
    defined(ABT_RT_PTHREAD_COND_INIT_VER) &&                                   \
    defined(ABT_RT_PTHREAD_COND_DESTROY_VER) &&                                \
    (!defined(USE_PTHREAD_BARRIER) ||                                          \
     (defined(ABT_RT_PTHREAD_BARRIER_INIT_VER) &&                              \
      defined(ABT_RT_PTHREAD_BARRIER_DESTROY_VER)))
/* Address sanitizers are not supported. */
#ifndef ADDRESS_SANITIZER_ENABLED
#define ABT_RT_ENABLED 1
#endif
#endif

#endif /* ABT_RT_USE_DLVSYM */

#ifdef ABT_RT_ENABLED
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#undef USE_PTHREAD_BARRIER
#if defined(_POSIX_BARRIERS) && _POSIX_BARRIERS > 0
#define USE_PTHREAD_BARRIER 1
#else
#define USE_PTHREAD_BARRIER 0
#endif

typedef enum RTRACE_OP_KIND {
    RTRACE_OP_KIND_MALLOC = 0,
    RTRACE_OP_KIND_CALLOC,
    RTRACE_OP_KIND_REALLOC,
    RTRACE_OP_KIND_POSIX_MEMALIGN,
    RTRACE_OP_KIND_MMAP,
    RTRACE_OP_KIND_PTHREAD_CREATE,
    RTRACE_OP_KIND_PTHREAD_MUTEX_INIT,
    RTRACE_OP_KIND_PTHREAD_COND_INIT,
    RTRACE_OP_KIND_PTHREAD_BARRIER_INIT,
} RTRACE_OP_KIND;

typedef enum RTRACE_RES_KIND {
    RTRACE_RES_KIND_NORMAL_MEM = 0, /* memory released by free() */
    RTRACE_RES_KIND_MMAP_MEM,
    RTRACE_RES_KIND_PTHREAD_T,
    RTRACE_RES_KIND_PTHREAD_MUTEX_T,
    RTRACE_RES_KIND_PTHREAD_COND_T,
    RTRACE_RES_KIND_PTHREAD_BARRIER_T,
} RTRACE_RES_KIND;

#define RTRACE_SUCCESS 0
#define RTRACE_SUCCESS_FIXED 1
#define RTRACE_FAILURE 2
#define RTRACE_REAL_FAILURE 3 /* Error by nature (e.g., mmap()). */

typedef struct rtrace_op_chain_t {
    RTRACE_OP_KIND op_kind;
    size_t val; /* Any operation-related value that helps identify the order.
                 * This is just a hint, so if that operation does not have a
                 * good value, zero should be substituted. */
    int success;
    struct rtrace_op_chain_t *p_next;
} rtrace_op_chain_t;

#define RTRACE_RES_HTABLE_SIZE 64

typedef struct rtrace_res_elem {
    RTRACE_RES_KIND res_kind;
    void *ptr;
    size_t val;
    int id; /* -1 if it is allocated by a non-target thread. */
    struct rtrace_res_elem *p_next;
} rtrace_res_elem;

typedef struct rtrace_res_table {
    rtrace_res_elem *elems[RTRACE_RES_HTABLE_SIZE];
    pthread_spinlock_t spinlock;
} rtrace_res_table;

/* free-able memory functions */
typedef void *(*mallocf_t)(size_t);
typedef void *(*callocf_t)(size_t, size_t);
typedef void *(*reallocf_t)(void *, size_t);
typedef int (*posix_memalignf_t)(void **, size_t, size_t);
typedef void (*freef_t)(void *);
/* mmap()/munmap() */
typedef void *(*mmapf_t)(void *, size_t, int, int, int, off_t);
typedef int (*munmapf_t)(void *, size_t);
/* pthread_t */
typedef int (*pthread_createf_t)(pthread_t *, const pthread_attr_t *,
                                 void *(*)(void *), void *);
typedef int (*pthread_joinf_t)(pthread_t, void **);
/* pthread_mutex_t */
typedef int (*pthread_mutex_initf_t)(pthread_mutex_t *,
                                     const pthread_mutexattr_t *);
typedef int (*pthread_mutex_destroyf_t)(pthread_mutex_t *);
/* pthread_cond_t */
typedef int (*pthread_cond_initf_t)(pthread_cond_t *,
                                    const pthread_condattr_t *);
typedef int (*pthread_cond_destroyf_t)(pthread_cond_t *);
#ifdef USE_PTHREAD_BARRIER
/* pthread_barrier_t */
typedef int (*pthread_barrier_initf_t)(pthread_barrier_t *,
                                       const pthread_barrierattr_t *, unsigned);
typedef int (*pthread_barrier_destroyf_t)(pthread_barrier_t *);
#endif

typedef struct {
    int enabled, check_failure, is_retrying;
    int verbose;
    int allocid,
        break_allocid; /* The program stops when allocid = break_allocid */
    pthread_t trace_thread;
    rtrace_res_table res_table;
    /* If not NULL, trace_thread will follow p_path. */
    rtrace_op_chain_t *p_path;
    rtrace_op_chain_t *p_path_cur;
    /* Trace_thread's path will be saved in p_history. */
    rtrace_op_chain_t *p_history;
    rtrace_op_chain_t *p_history_cur;
    /* Functions. */
    mallocf_t real_malloc;
    callocf_t real_calloc;
    reallocf_t real_realloc;
    posix_memalignf_t real_posix_memalign;
    freef_t real_free;
    mmapf_t real_mmap;
    munmapf_t real_munmap;

    pthread_createf_t real_pthread_create;
    pthread_joinf_t real_pthread_join;
    pthread_mutex_initf_t real_pthread_mutex_init;
    pthread_mutex_destroyf_t real_pthread_mutex_destroy;
    pthread_cond_initf_t real_pthread_cond_init;
    pthread_cond_destroyf_t real_pthread_cond_destroy;
#ifdef USE_PTHREAD_BARRIER
    pthread_barrier_initf_t real_pthread_barrier_init;
    pthread_barrier_destroyf_t real_pthread_barrier_destroy;
#endif
} rtrace_global_t;
static rtrace_global_t g_rtrace_global;
/**/
static __thread int
    l_rtrace_disabled; /* Some functions (e.g., pthread_create()) free their
                          memory resources on terminating a process, so it is
                          detected as a memory leak.  This flag temporarily
                          disables resource tracing. */

typedef struct {
    rtrace_op_chain_t *p_chain;
    rtrace_op_chain_t *p_history;
} rtrace_local_t;

#define STRINGIFY2(macro) #macro
#define STRINGIFY(macro) STRINGIFY2(macro)

static const char *dlvsym_ver_malloc = STRINGIFY(ABT_RT_MALLOC_VER);
static const char *dlvsym_ver_calloc = STRINGIFY(ABT_RT_CALLOC_VER);
static const char *dlvsym_ver_realloc = STRINGIFY(ABT_RT_REALLOC_VER);
static const char *dlvsym_ver_posix_memalign =
    STRINGIFY(ABT_RT_POSIX_MEMALIGN_VER);
static const char *dlvsym_ver_free = STRINGIFY(ABT_RT_FREE_VER);
static const char *dlvsym_ver_mmap = STRINGIFY(ABT_RT_MMAP_VER);
static const char *dlvsym_ver_munmap = STRINGIFY(ABT_RT_MUNMAP_VER);
static const char *dlvsym_ver_pthread_create =
    STRINGIFY(ABT_RT_PTHREAD_CREATE_VER);
static const char *dlvsym_ver_pthread_join = STRINGIFY(ABT_RT_PTHREAD_JOIN_VER);
static const char *dlvsym_ver_pthread_mutex_init =
    STRINGIFY(ABT_RT_PTHREAD_MUTEX_INIT_VER);
static const char *dlvsym_ver_pthread_mutex_destroy =
    STRINGIFY(ABT_RT_PTHREAD_MUTEX_DESTROY_VER);
static const char *dlvsym_ver_pthread_cond_init =
    STRINGIFY(ABT_RT_PTHREAD_COND_INIT_VER);
static const char *dlvsym_ver_pthread_cond_destroy =
    STRINGIFY(ABT_RT_PTHREAD_COND_DESTROY_VER);
#ifdef USE_PTHREAD_BARRIER
static const char *dlvsym_ver_pthread_barrier_init =
    STRINGIFY(ABT_RT_PTHREAD_BARRIER_INIT_VER);
static const char *dlvsym_ver_pthread_barrier_destroy =
    STRINGIFY(ABT_RT_PTHREAD_BARRIER_DESTROY_VER);
#endif

/* Overwritten functions. */
#define PREP_REAL_FUNC(func_name)                                              \
    func_name##f_t real_##func_name =                                          \
        __atomic_load_n(&g_rtrace_global.real_##func_name, __ATOMIC_RELAXED);  \
    if (!real_##func_name) {                                                   \
        /* We use dlvsym since dlsym may load an old symbol, which causes an   \
         * error because of version mismatch (e.g., combining old              \
         * pthread_cond_init() and new pthread_cond_wait() cause an error      \
         *  since their struct usages are different. */                        \
        typedef union {                                                        \
            void *ptr;                                                         \
            func_name##f_t fptr;                                               \
        } func_conv_t;                                                         \
        func_conv_t func_conv;                                                 \
        if (dlvsym_ver_##func_name[0] == '\0') {                               \
            func_conv.ptr = dlsym(RTLD_NEXT, #func_name);                      \
        } else {                                                               \
            func_conv.ptr =                                                    \
                dlvsym(RTLD_NEXT, #func_name, dlvsym_ver_##func_name);         \
        }                                                                      \
        real_##func_name = func_conv.fptr;                                     \
        __atomic_store_n(&g_rtrace_global.real_##func_name, real_##func_name,  \
                         __ATOMIC_RELAXED);                                    \
    }

static inline uint32_t ptr_hash(void *ptr)
{
    /* Based on Xorshift:
     * George Marsaglia, "Xorshift RNGs", Journal of Statistical Software,
     * Articles, 2003 */
    uint32_t seed = (uint32_t)((uintptr_t)ptr);
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static const char *get_op_kind_str(RTRACE_OP_KIND op_kind)
{
    if (op_kind == RTRACE_OP_KIND_MALLOC) {
        return "malloc";
    } else if (op_kind == RTRACE_OP_KIND_CALLOC) {
        return "calloc";
    } else if (op_kind == RTRACE_OP_KIND_REALLOC) {
        return "realloc";
    } else if (op_kind == RTRACE_OP_KIND_POSIX_MEMALIGN) {
        return "posix_memalign";
    } else if (op_kind == RTRACE_OP_KIND_MMAP) {
        return "mmap";
    } else if (op_kind == RTRACE_OP_KIND_PTHREAD_CREATE) {
        return "pthread_create";
    } else if (op_kind == RTRACE_OP_KIND_PTHREAD_MUTEX_INIT) {
        return "pthread_mutex_init";
    } else if (op_kind == RTRACE_OP_KIND_PTHREAD_COND_INIT) {
        return "pthread_cond_init";
    } else if (op_kind == RTRACE_OP_KIND_PTHREAD_BARRIER_INIT) {
        return "pthread_barrier_init";
    } else {
        return "UNKNOWN_OP_KIND";
    }
}

static const char *get_success_str(int success)
{
    if (success == RTRACE_SUCCESS) {
        return "success";
    } else if (success == RTRACE_SUCCESS_FIXED) {
        return "success (fixed)";
    } else if (success == RTRACE_FAILURE) {
        return "failure";
    } else if (success == RTRACE_REAL_FAILURE) {
        return "real failure";
    } else {
        return "unknown";
    }
}

static void rtrace_res_init(void)
{
    memset(&g_rtrace_global.res_table, 0, sizeof(rtrace_res_table));
    int ret = pthread_spin_init(&g_rtrace_global.res_table.spinlock, 0);
    assert(ret == 0);
}

static void rtrace_res_finalize(int *p_retry)
{
    /* Check if all the resources are released. */
    int i, leak_flag = 0;
    for (i = 0; i < RTRACE_RES_HTABLE_SIZE; i++) {
        rtrace_res_elem *p_elem = g_rtrace_global.res_table.elems[i];
        while (p_elem) {
            if (g_rtrace_global.is_retrying || g_rtrace_global.verbose) {
                printf("%p [id = %d, val = %zu] is not released\n",
                       (void *)p_elem, p_elem->id, p_elem->val);
            }
            leak_flag = 1;
            p_elem = p_elem->p_next;
        }
    }
    if (leak_flag != 0) {
        if (g_rtrace_global.is_retrying) {
            /* Resource is really leaked. */
            assert(0);
        } else {
            /* Maybe some global functions (e.g., fprintf) internally caches
             * resources.  Let's run it again to see if this happens again.  If
             * someone caches resources, new resource allocation should not
             * occur the next run. */
            if (g_rtrace_global.verbose) {
                printf("Memory leak is detected.  Run this configuration "
                       "again.\n");
            }
            g_rtrace_global.is_retrying = 1;
            *p_retry = 1;
        }
    } else {
        *p_retry = 0;
        g_rtrace_global.is_retrying = 0;
        if (g_rtrace_global.verbose) {
            printf("No memory is leaked [# of allocations: %d]\n",
                   g_rtrace_global.allocid);
        }
    }
    int ret = pthread_spin_destroy(&g_rtrace_global.res_table.spinlock);
    assert(ret == 0);
}

static void rtrace_res_add(RTRACE_RES_KIND res_kind, void *ptr, size_t val)
{
    int ret;
    ret = pthread_spin_lock(&g_rtrace_global.res_table.spinlock);
    assert(ret == 0);

    uint32_t hash_idx = ptr_hash(ptr) % RTRACE_RES_HTABLE_SIZE;
    rtrace_res_elem **pp_elem = &g_rtrace_global.res_table.elems[hash_idx];
    while (*pp_elem) {
        pp_elem = &((*pp_elem)->p_next);
    }

    l_rtrace_disabled++;
    PREP_REAL_FUNC(calloc);
    rtrace_res_elem *p_new_elem = real_calloc(1, sizeof(rtrace_res_elem));
    l_rtrace_disabled--;
    if (!p_new_elem) {
        pthread_spin_unlock(&g_rtrace_global.res_table.spinlock);
        assert(p_new_elem);
    }
    p_new_elem->res_kind = res_kind;
    p_new_elem->ptr = ptr;
    p_new_elem->val = val;
    pthread_t self_thread = pthread_self();
    if (!pthread_equal(self_thread, g_rtrace_global.trace_thread)) {
        p_new_elem->id = -1;
    } else {
        p_new_elem->id = g_rtrace_global.allocid++;
        if (p_new_elem->id == g_rtrace_global.break_allocid) {
            pthread_spin_unlock(&g_rtrace_global.res_table.spinlock);
            assert(0);
        }
    }
    *pp_elem = p_new_elem;

    pthread_spin_unlock(&g_rtrace_global.res_table.spinlock);
}

static void rtrace_res_remove(RTRACE_RES_KIND res_kind, void *ptr, size_t val)
{
    int ret;
    ret = pthread_spin_lock(&g_rtrace_global.res_table.spinlock);
    assert(ret == 0);

    uint32_t hash_idx = ptr_hash(ptr) % RTRACE_RES_HTABLE_SIZE;
    rtrace_res_elem **pp_elem = &g_rtrace_global.res_table.elems[hash_idx];
    while (*pp_elem) {
        rtrace_res_elem *p_elem = *pp_elem;
        if (p_elem->ptr == ptr && p_elem->res_kind == res_kind &&
            (val == 0 || p_elem->val == 0 || p_elem->val == val)) {
            /* This element must be removed. */
            *pp_elem = p_elem->p_next;
            l_rtrace_disabled++;
            PREP_REAL_FUNC(free);
            real_free(p_elem);
            l_rtrace_disabled--;
            pthread_spin_unlock(&g_rtrace_global.res_table.spinlock);
            return;
        }
        pp_elem = &((*pp_elem)->p_next);
    }
    /* Removal failed: maybe memory that has been already allocated before
     * rtrace_init() was released.  Let's ignore. */
    pthread_spin_unlock(&g_rtrace_global.res_table.spinlock);
}

static void rtrace_res_replace(RTRACE_RES_KIND res_kind, void *old_ptr,
                               size_t old_val, void *new_ptr, size_t new_val)
{
    assert(old_ptr != NULL);

    int ret;
    ret = pthread_spin_lock(&g_rtrace_global.res_table.spinlock);
    assert(ret == 0);

    uint32_t hash_idx = ptr_hash(old_ptr) % RTRACE_RES_HTABLE_SIZE;
    rtrace_res_elem *p_elem = g_rtrace_global.res_table.elems[hash_idx];
    while (p_elem) {
        if (p_elem->ptr == old_ptr && p_elem->res_kind == res_kind &&
            (old_val == 0 || p_elem->val == 0 || p_elem->val == old_val)) {
            /* This element must be replaced. */
            p_elem->ptr = new_ptr;
            p_elem->val = new_val;
            ret = pthread_spin_unlock(&g_rtrace_global.res_table.spinlock);
            assert(ret == 0);
            return;
        }
        p_elem = p_elem->p_next;
    }
    /* Replace failed: maybe memory that has been already allocated before
     * rtrace_init() was released.  Let's ignore. */
    pthread_spin_unlock(&g_rtrace_global.res_table.spinlock);
}

static int rtrace_log_success(RTRACE_OP_KIND op_kind, size_t val)
{
    if (!g_rtrace_global.p_path_cur) {
        /* If p_path is not set, ignore. */
        return 1;
    }
    pthread_t self_thread = pthread_self();
    if (!pthread_equal(self_thread, g_rtrace_global.trace_thread)) {
        /* It always succeeds if self_thread is not the target. */
        return 1;
    }
    /* Check its path */
    if (g_rtrace_global.p_path_cur->op_kind == op_kind &&
        g_rtrace_global.p_path_cur->val == val) {
        int success = g_rtrace_global.p_path_cur->success;
        g_rtrace_global.p_path_cur = g_rtrace_global.p_path_cur->p_next;
        return success == RTRACE_SUCCESS || success == RTRACE_SUCCESS_FIXED;
    } else {
        /* Maybe diverged. Let's make it succeed. */
        return 1;
    }
}

static void rtrace_op_add(RTRACE_OP_KIND op_kind, size_t val, int success)
{
    pthread_t self_thread = pthread_self();
    if (!pthread_equal(self_thread, g_rtrace_global.trace_thread)) {
        /* op is not added if self_thread is not the target. */
        return;
    }
    l_rtrace_disabled++;
    PREP_REAL_FUNC(malloc);
    rtrace_op_chain_t *p_op =
        (rtrace_op_chain_t *)real_malloc(sizeof(rtrace_op_chain_t));
    l_rtrace_disabled--;
    assert(p_op);
    p_op->op_kind = op_kind;
    p_op->val = val;
    if (!g_rtrace_global.check_failure && success == RTRACE_SUCCESS) {
        /* This operation won't fail in this testing. */
        p_op->success = RTRACE_SUCCESS_FIXED;
    } else {
        p_op->success = success;
    }
    p_op->p_next = NULL;
    if (g_rtrace_global.p_history_cur) {
        g_rtrace_global.p_history_cur->p_next = p_op;
        g_rtrace_global.p_history_cur = p_op;
    } else {
        g_rtrace_global.p_history = p_op;
        g_rtrace_global.p_history_cur = p_op;
    }
}

static void rtrace_free_chain(rtrace_op_chain_t *p_chain)
{
    l_rtrace_disabled++;
    while (p_chain) {
        rtrace_op_chain_t *p_next = p_chain->p_next;
        PREP_REAL_FUNC(free);
        real_free(p_next);
        p_chain = p_next;
    }
    l_rtrace_disabled--;
}

void rtrace_init(void)
{
    assert(g_rtrace_global.enabled == 0);
    char *env;
    /* Let's set up all the function pointers here. */
    {
        PREP_REAL_FUNC(malloc);
        PREP_REAL_FUNC(calloc);
        PREP_REAL_FUNC(realloc);
        PREP_REAL_FUNC(posix_memalign);
        PREP_REAL_FUNC(free);
        PREP_REAL_FUNC(mmap);
        PREP_REAL_FUNC(munmap);
        PREP_REAL_FUNC(pthread_create);
        PREP_REAL_FUNC(pthread_join);
        PREP_REAL_FUNC(pthread_mutex_init);
        PREP_REAL_FUNC(pthread_mutex_destroy);
        PREP_REAL_FUNC(pthread_cond_init);
        PREP_REAL_FUNC(pthread_cond_destroy);
#ifdef USE_PTHREAD_BARRIER
        PREP_REAL_FUNC(pthread_barrier_init);
        PREP_REAL_FUNC(pthread_barrier_destroy);
#endif
    }
    env = getenv("RTRACE_VERBOSE");
    if (env) {
        g_rtrace_global.verbose = atoi(env);
    } else {
        g_rtrace_global.verbose = 0;
    }
    env = getenv("RTRACE_BREAK_ALLOCID");
    if (env) {
        g_rtrace_global.break_allocid = atoi(env);
    } else {
        g_rtrace_global.break_allocid = -1;
    }
    g_rtrace_global.p_path = NULL;
    g_rtrace_global.p_path_cur = g_rtrace_global.p_path;
    g_rtrace_global.p_history = NULL;
    g_rtrace_global.p_history_cur = g_rtrace_global.p_history;
    g_rtrace_global.is_retrying = 0;
}

void rtrace_finalize(void)
{
    assert(g_rtrace_global.enabled == 0);
    rtrace_free_chain(g_rtrace_global.p_path);
    g_rtrace_global.p_path = NULL;
    g_rtrace_global.p_path_cur = NULL;
    printf("No error\n");
}

void rtrace_start(void)
{
    assert(g_rtrace_global.enabled == 0);
    g_rtrace_global.trace_thread = pthread_self();
    rtrace_res_init();
    if (g_rtrace_global.verbose >= 2) {
        printf("[rtrace_start] execution chain:\n");
        rtrace_op_chain_t *p_cur = g_rtrace_global.p_path;
        int index = 0;
        while (p_cur) {
            printf("  [%3d] %-20s (val = %8zu): %s\n", index,
                   get_op_kind_str(p_cur->op_kind), p_cur->val,
                   get_success_str(p_cur->success));
            index++;
            p_cur = p_cur->p_next;
        }
        printf("  [%3s] %-20s (val = %8s): %s\n", "*", "*", "*",
               get_success_str(RTRACE_SUCCESS));
    }
    g_rtrace_global.allocid = 0;
    g_rtrace_global.enabled = 1;
    g_rtrace_global.check_failure = 1;
}

int rtrace_stop(void)
{
    assert(g_rtrace_global.enabled == 1);
    g_rtrace_global.enabled = 0;
    if (g_rtrace_global.verbose >= 2) {
        printf("[rtrace_stop] execution history:\n");
        rtrace_op_chain_t *p_cur = g_rtrace_global.p_history;
        int index = 0;
        while (p_cur) {
            printf("  [%3d] %-20s (val = %8zu): %s\n", index,
                   get_op_kind_str(p_cur->op_kind), p_cur->val,
                   get_success_str(p_cur->success));
            index++;
            p_cur = p_cur->p_next;
        }
    }
    int retry = 0;
    rtrace_res_finalize(&retry);

    if (retry) {
        /* Use the same configuration again. */
        g_rtrace_global.p_path_cur = g_rtrace_global.p_path;
        rtrace_free_chain(g_rtrace_global.p_history);
        g_rtrace_global.p_history = NULL;
        g_rtrace_global.p_history_cur = NULL;
        return 0;
    } else {
        /* Try the next configuration. */
        /* Free memory. */
        rtrace_free_chain(g_rtrace_global.p_path);
        g_rtrace_global.p_path = NULL;
        g_rtrace_global.p_path_cur = NULL;

        /* Check the history and see if there's success.  If there's no success,
         * all the tests have been finished. */
        rtrace_op_chain_t *p_cur = g_rtrace_global.p_history;
        rtrace_op_chain_t *p_last_success = NULL;
        while (p_cur) {
            /* We do not count RTRACE_SUCCESS_FIXED */
            if (p_cur->success == RTRACE_SUCCESS) {
                p_last_success = p_cur;
            }
            p_cur = p_cur->p_next;
        }
        if (!p_last_success) {
            /* All the tests have finished.  Let's free all the history. */
            rtrace_free_chain(g_rtrace_global.p_history);
            g_rtrace_global.p_history = NULL;
            g_rtrace_global.p_history_cur = NULL;
            return 1;
        } else {
            /* Let's create a new path by changing the last success to failure.
             */
            p_cur = g_rtrace_global.p_history;
            while (p_cur) {
                if (p_cur == p_last_success) {
                    /* Intentional failure. */
                    p_cur->success = RTRACE_FAILURE;
                    /* Following operations should be successful, so we do not
                     * need its chain. */
                    rtrace_free_chain(p_cur->p_next);
                    p_cur->p_next = NULL;
                    break;
                }
                p_cur = p_cur->p_next;
            }
            g_rtrace_global.p_path = g_rtrace_global.p_history;
            g_rtrace_global.p_path_cur = g_rtrace_global.p_path;
            g_rtrace_global.p_history = NULL;
            g_rtrace_global.p_history_cur = NULL;
            return 0;
        }
    }
}

void rtrace_set_enabled(int enabled)
{
    g_rtrace_global.check_failure = enabled;
}

#define ALLOC_BUFFER_SIZE (32 * 1024)
static size_t alloc_buffer[ALLOC_BUFFER_SIZE / sizeof(size_t)];
static size_t alloc_buffer_index = 0;

void *malloc(size_t size)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_CALLOC;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, size)) {
        /* Artificial failure. */
        rtrace_op_add(op, size, RTRACE_FAILURE);
        return NULL;
    }
    /* Success */
    void *ret = NULL;
    if (size <= ALLOC_BUFFER_SIZE) {
        size_t aligned_size = ((size + 16 + 15) / 16) * 16;
        if (aligned_size <=
            ALLOC_BUFFER_SIZE -
                __atomic_load_n(&alloc_buffer_index, __ATOMIC_ACQUIRE)) {
            size_t new_index =
                __atomic_fetch_add(&alloc_buffer_index, aligned_size,
                                   __ATOMIC_ACQ_REL);
            if (new_index + aligned_size <= ALLOC_BUFFER_SIZE) {
                /* Allocation succeeded. */
                ret =
                    (void *)(&alloc_buffer[(new_index + 16) / sizeof(size_t)]);
                /* Write the size of this memory in the first 4 * size_t bytes.
                 */
                alloc_buffer[new_index / sizeof(size_t)] = size;
            }
        }
    }
    if (ret == NULL) {
        l_rtrace_disabled++;
        PREP_REAL_FUNC(malloc);
        ret = real_malloc(size);
        l_rtrace_disabled--;
    }
    assert(ret);
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_add(RTRACE_RES_KIND_NORMAL_MEM, ret, size);
        rtrace_op_add(op, size, RTRACE_SUCCESS);
    }
    return ret;
}

void *calloc(size_t nmemb, size_t size)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_CALLOC;
    const size_t val = nmemb * size;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, val)) {
        /* Artificial failure. */
        rtrace_op_add(op, val, RTRACE_FAILURE);
        return NULL;
    }
    /* Success */
    /* dlsym() and dlvsym() uses calloc() (potentially malloc() too) internally,
     * so it can cause an infinite recursion.  Let's use a statically allocated
     * buffer to avoid such. */
    void *ret = NULL;
    if (nmemb * size <= ALLOC_BUFFER_SIZE) {
        size_t aligned_size = ((nmemb * size + 16 + 15) / 16) * 16;
        if (aligned_size <=
            ALLOC_BUFFER_SIZE -
                __atomic_load_n(&alloc_buffer_index, __ATOMIC_ACQUIRE)) {
            size_t new_index =
                __atomic_fetch_add(&alloc_buffer_index, aligned_size,
                                   __ATOMIC_ACQ_REL);
            if (new_index + aligned_size <= ALLOC_BUFFER_SIZE) {
                /* Allocation succeeded. */
                ret =
                    (void *)(&alloc_buffer[(new_index + 16) / sizeof(size_t)]);
                /* Write the size of this memory in the first 16 bytes. */
                alloc_buffer[new_index / sizeof(size_t)] = nmemb * size;
                /* Global variables are initialized with zero, so no need to
                 * call memset() */
            }
        }
    }
    if (ret == NULL) {
        l_rtrace_disabled++;
        PREP_REAL_FUNC(calloc);
        ret = real_calloc(nmemb, size);
        l_rtrace_disabled--;
    }
    assert(ret);
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_add(RTRACE_RES_KIND_NORMAL_MEM, ret, val);
        rtrace_op_add(op, val, RTRACE_SUCCESS);
    }
    return ret;
}

void *realloc(void *ptr, size_t size)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_REALLOC;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, size)) {
        /* Artificial failure. */
        rtrace_op_add(op, size, RTRACE_FAILURE);
        return NULL;
    }
    /* Success */
    void *ret;
    if ((void *)alloc_buffer <= ptr &&
        ptr < (void *)&alloc_buffer[ALLOC_BUFFER_SIZE / sizeof(size_t)]) {
        /* Newly allocate the data since we cannot reallocate it. */
        l_rtrace_disabled++;
        PREP_REAL_FUNC(malloc);
        ret = real_malloc(size);
        l_rtrace_disabled--;
        assert(ret);
        size_t i, old_size = *(((size_t *)ptr) - 16 / sizeof(size_t));
        size_t copy_size = old_size > size ? size : old_size;
        for (i = 0; i < copy_size; i++)
            ((char *)ret)[i] = ((char *)ptr)[i];
    } else {
        l_rtrace_disabled++;
        PREP_REAL_FUNC(realloc);
        ret = real_realloc(ptr, size);
        l_rtrace_disabled--;
        assert(ret);
    }
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        if (ptr == NULL) {
            rtrace_res_add(RTRACE_RES_KIND_NORMAL_MEM, ret, size);
        } else {
            rtrace_res_replace(RTRACE_RES_KIND_NORMAL_MEM, ptr, 0, ret, size);
        }
        rtrace_op_add(op, size, RTRACE_SUCCESS);
    }
    return ret;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_POSIX_MEMALIGN;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, size)) {
        /* Artificial failure. */
        rtrace_op_add(op, size, RTRACE_FAILURE);
        return ENOMEM;
    }
    /* Success */
    l_rtrace_disabled++;
    PREP_REAL_FUNC(posix_memalign);
    int ret = real_posix_memalign(memptr, alignment, size);
    l_rtrace_disabled--;
    assert(ret == 0);
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_add(RTRACE_RES_KIND_NORMAL_MEM, *memptr, size);
        rtrace_op_add(op, size, RTRACE_SUCCESS);
    }
    return ret;
}

void free(void *ptr)
{
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_remove(RTRACE_RES_KIND_NORMAL_MEM, ptr, 0);
    }
    if ((void *)alloc_buffer <= ptr &&
        ptr < (void *)&alloc_buffer[ALLOC_BUFFER_SIZE / sizeof(size_t)]) {
        /* Skip since this ptr is statically allocated. */
    } else {
        l_rtrace_disabled++;
        PREP_REAL_FUNC(free);
        l_rtrace_disabled--;
        real_free(ptr);
    }
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_MMAP;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, length)) {
        /* Artificial failure. */
        rtrace_op_add(op, length, RTRACE_FAILURE);
        return NULL;
    }
    /* Success */
    l_rtrace_disabled++;
    PREP_REAL_FUNC(mmap);
    void *ret = real_mmap(addr, length, prot, flags, fd, offset);
    l_rtrace_disabled--;
    /* mmap can actually fail. */
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        if (ret == MAP_FAILED) {
            rtrace_op_add(op, length, RTRACE_REAL_FAILURE);
        } else {
            rtrace_res_add(RTRACE_RES_KIND_MMAP_MEM, ret, length);
            rtrace_op_add(op, length, RTRACE_SUCCESS);
        }
    }
    return ret;
}

int munmap(void *addr, size_t length)
{
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_remove(RTRACE_RES_KIND_MMAP_MEM, addr, length);
    }
    l_rtrace_disabled++;
    PREP_REAL_FUNC(munmap);
    l_rtrace_disabled--;
    int ret = real_munmap(addr, length);
    assert(ret == 0);
    return ret;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_PTHREAD_CREATE;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, 0)) {
        /* Artificial failure. */
        rtrace_op_add(op, 0, RTRACE_FAILURE);
        return EAGAIN;
    }
    /* Success */
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_create);
    /* pthread_create leaks memory, so let's disable its resource tracing. */
    int ret = real_pthread_create(thread, attr, start_routine, arg);
    l_rtrace_disabled--;
    assert(ret == 0);
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        void *thread_val = (void *)((uintptr_t)*thread);
        rtrace_res_add(RTRACE_RES_KIND_PTHREAD_T, thread_val, 0);
        rtrace_op_add(op, 0, RTRACE_SUCCESS);
    }
    return ret;
}

int pthread_join(pthread_t thread, void **value_ptr)
{
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        void *thread_val = (void *)((uintptr_t)thread);
        rtrace_res_remove(RTRACE_RES_KIND_PTHREAD_T, thread_val, 0);
    }
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_join);
    l_rtrace_disabled--;
    int ret = real_pthread_join(thread, value_ptr);
    assert(ret == 0);
    return ret;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_PTHREAD_MUTEX_INIT;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, 0)) {
        /* Artificial failure. */
        rtrace_op_add(op, 0, RTRACE_FAILURE);
        return EAGAIN;
    }
    /* Success */
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_mutex_init);
    int ret = real_pthread_mutex_init(mutex, attr);
    l_rtrace_disabled--;
    assert(ret == 0);
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_add(RTRACE_RES_KIND_PTHREAD_MUTEX_T, mutex, 0);
        rtrace_op_add(op, 0, RTRACE_SUCCESS);
    }
    return ret;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_remove(RTRACE_RES_KIND_PTHREAD_MUTEX_T, mutex, 0);
    }
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_mutex_destroy);
    l_rtrace_disabled--;
    int ret = real_pthread_mutex_destroy(mutex);
    assert(ret == 0);
    return ret;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_PTHREAD_COND_INIT;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, 0)) {
        /* Artificial failure. */
        rtrace_op_add(op, 0, RTRACE_FAILURE);
        return EAGAIN;
    }
    /* Success */
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_cond_init);
    int ret = real_pthread_cond_init(cond, attr);
    l_rtrace_disabled--;
    assert(ret == 0);
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_add(RTRACE_RES_KIND_PTHREAD_COND_T, cond, 0);
        rtrace_op_add(op, 0, RTRACE_SUCCESS);
    }
    return ret;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_remove(RTRACE_RES_KIND_PTHREAD_COND_T, cond, 0);
    }
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_cond_destroy);
    l_rtrace_disabled--;
    int ret = real_pthread_cond_destroy(cond);
    assert(ret == 0);
    return ret;
}

#ifdef USE_PTHREAD_BARRIER
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned count)
{
    const RTRACE_OP_KIND op = RTRACE_OP_KIND_PTHREAD_BARRIER_INIT;
    if (!l_rtrace_disabled && g_rtrace_global.enabled &&
        !rtrace_log_success(op, 0)) {
        /* Artificial failure. */
        rtrace_op_add(op, 0, RTRACE_FAILURE);
        return EAGAIN;
    }
    /* Success */
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_barrier_init);
    int ret = real_pthread_barrier_init(barrier, attr, count);
    l_rtrace_disabled--;
    assert(ret == 0);
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_add(RTRACE_RES_KIND_PTHREAD_BARRIER_T, barrier, count);
        rtrace_op_add(op, 0, RTRACE_SUCCESS);
    }
    return ret;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    if (!l_rtrace_disabled && g_rtrace_global.enabled) {
        rtrace_res_remove(RTRACE_RES_KIND_PTHREAD_BARRIER_T, barrier, 0);
    }
    l_rtrace_disabled++;
    PREP_REAL_FUNC(pthread_barrier_destroy);
    l_rtrace_disabled--;
    int ret = real_pthread_barrier_destroy(barrier);
    assert(ret == 0);
    return ret;
}
#endif /* USE_PTHREAD_BARRIER */

#else /* !ABT_RT_ENABLED */

#include <stdio.h>

void rtrace_init(void)
{
    printf("rtrace is disabled.\n");
}

void rtrace_finalize(void)
{
    printf("No error\n");
}

void rtrace_start(void)
{
    /* Do nothing */
}

int rtrace_stop(void)
{
    return 1;
}
void rtrace_set_enabled(int enabled)
{
    /* Do nothing. */
    (void)enabled;
}

#endif /* !ABT_RT_ENABLED */
