/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 199309L

#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include "abt.h"
#include "abttest.h"

#define DUMMY_SIZE ((int)(1024 / sizeof(double)))

int g_mprotect_signal = 0;
volatile int g_sig_err = 0;
volatile int g_is_segv = 0;
volatile char *gp_stack = NULL;
size_t g_sys_page_size = 0;

void segv_handler(int sig, siginfo_t *si, void *unused)
{
    if (sig != SIGSEGV) {
        g_sig_err = 1; /* We cannot call assert(). */
        signal(sig, SIG_DFL);
    } else if (si->si_addr != gp_stack) {
        g_sig_err = 2;
        signal(SIGSEGV, SIG_DFL);
    } else {
        /* Since POSIX does not mark mprotect() as async-signal safe, we need to
         * ask another thread to call mprotect() instead of this thread even if
         * we control where the signal happens; calling an async signal-unsafe
         * function can cause any unexpected issues. */
        ATS_atomic_store(&g_mprotect_signal, 1);
        while (ATS_atomic_load(&g_mprotect_signal) == 1) {
            ; /* Waiting for the helper thread. */
        }
        /* mprotect() finished. */
        g_is_segv = 1;
    }
}

void *helper_func(void *arg)
{
    /* Waiting for g_mprotect_signal from a signal handler. */
    while (ATS_atomic_load(&g_mprotect_signal) == 0)
        ;
    /* Call mprotect() to temporarily allow an access. */
    int ret =
        mprotect((void *)gp_stack, g_sys_page_size, PROT_READ | PROT_WRITE);
    assert(ret == 0);
    /* Tell the signal handler that mprotect has finished. */
    ATS_atomic_store(&g_mprotect_signal, 0);
    return NULL;
}

void thread_func(void *arg)
{
    int ret;
    void *p_stack;
    size_t stacksize;
    /* Get the stack information. */
    {
        ABT_thread self_thread;
        ABT_thread_attr self_thread_attr;
        ret = ABT_self_get_thread(&self_thread);
        ATS_ERROR(ret, "ABT_self_get_thread");
        ret = ABT_thread_get_attr(self_thread, &self_thread_attr);
        ATS_ERROR(ret, "ABT_thread_get_attr");
        ret = ABT_thread_attr_get_stack(self_thread_attr, &p_stack, &stacksize);
        ATS_ERROR(ret, "ABT_thread_attr_get_stack");
        ret = ABT_thread_attr_free(&self_thread_attr);
        ATS_ERROR(ret, "ABT_thread_attr_free");
    }

    /* We can reasonably assume that we do not corrupt the function stack of
     * thread_func().  Let's assume that the protected page is within a few
     * pages from the bottom of the stack.
     * gp_stack should be aligned with the page size. */
    gp_stack = (char *)(((((uintptr_t)p_stack) + g_sys_page_size - 1) /
                         g_sys_page_size) *
                            g_sys_page_size +
                        g_sys_page_size * 2);
    while (1) {
        /* Using this stack variable to see if we can observe SEGV. */
        gp_stack -= g_sys_page_size;
        assert(((char *)p_stack) <= gp_stack);
        volatile char val = gp_stack[0];
        /* Though we use "volatile", we'd like to put a compiler barrier just in
         * case. */
        __asm__ __volatile__("" ::: "memory");
        /* The following should cause SEGV.  If SEGV happens, the signal handler
         * will allow this ULT to temporarily access this. */
        gp_stack[0] = val;
        __asm__ __volatile__("" ::: "memory");
        /* Signal might have happened. */
        if (g_is_segv) {
            assert(g_sig_err == 0);
            /* Succeeded!  Undo the mprotect setting.  Originally it should be
             * read-protected. */
            g_is_segv = 0;
            ret = mprotect((void *)gp_stack, g_sys_page_size, PROT_READ);
            assert(ret == 0);
            return;
        }
        /* We must catch SEGV until we touch stacksize */
    }
}

int main(int argc, char *argv[])
{
    int ret, i;
    /* Get the system page size. */
    g_sys_page_size = getpagesize();
    if (g_sys_page_size > 16 * 1024 * 1024) {
        /* The system page size is too large.  Let's skip this test. */
        return 77;
    }
    /* Catch SEGV. */
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = segv_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        /* Unsupported. */
        return 77;
    }
    /* Set memory protection */
    putenv("ABT_STACK_OVERFLOW_CHECK=mprotect_strict");
    /* Initialize */
    ATS_read_args(argc, argv);
    size_t stacksizes[] = { g_sys_page_size * 2 + 1024 * 64,
                            g_sys_page_size * 2 + 1024 * 64 + 64,
                            g_sys_page_size * 2 + 1024 * 64 + 128,
                            g_sys_page_size * 2 + 1024 * 64 - 64,
                            g_sys_page_size * 2 + 1024 * 64 - 128,
                            g_sys_page_size * 2 + 1024 * 1024,
                            g_sys_page_size * 2 + 4 * 1024 * 1024 };

    int stack_i, num_stacksizes = sizeof(stacksizes) / sizeof(stacksizes[0]);
    for (stack_i = 0; stack_i < num_stacksizes; stack_i++) {
        /* Set the default stack size. */
        const size_t stacksize = stacksizes[stack_i];
        unsetenv("ABT_THREAD_STACKSIZE");
        char stacksize_str[256];
        sprintf(stacksize_str, "ABT_THREAD_STACKSIZE=%zu", stacksize);
        putenv(stacksize_str);
        /* Use ATS_init for the last run. */
        if (stack_i == num_stacksizes - 1) {
            ATS_init(argc, argv, 2);
        } else {
            ret = ABT_init(argc, argv);
            ATS_ERROR(ret, "ABT_finalize");
        }
        /* Check if the mprotect-based stack guard is enabled. */
        int stack_overflow_check_mode = 0;
        ret = ABT_info_query_config(
            ABT_INFO_QUERY_KIND_ENABLED_STACK_OVERFLOW_CHECK,
            &stack_overflow_check_mode);
        ATS_ERROR(ret, "ABT_info_query_config");
        if (stack_overflow_check_mode != 3) {
            /* Unsupported. */
            return 77;
        }

        ABT_xstream xstream;
        ABT_pool main_pool;
        ret = ABT_self_get_xstream(&xstream);
        ATS_ERROR(ret, "ABT_self_get_xstream");
        ret = ABT_xstream_get_main_pools(xstream, 1, &main_pool);
        ATS_ERROR(ret, "ABT_xstream_get_main_pools");

        for (i = 0; i < 3; i++) {
            pthread_t helper_thread;
            ret = pthread_create(&helper_thread, NULL, helper_func, NULL);
            assert(ret == 0);
            ABT_thread thread;
            void *stack = NULL;
            if (i == 0) {
                /* 1. ULT + default parameters. */
                ret = ABT_thread_create(main_pool, thread_func, NULL,
                                        ABT_THREAD_ATTR_NULL, &thread);
                ATS_ERROR(ret, "ABT_thread_create");
            } else if (i == 1) {
                /* 2. ULT + user-given stack size. */
                ABT_thread_attr thread_attr;
                ret = ABT_thread_attr_create(&thread_attr);
                ATS_ERROR(ret, "ABT_thread_attr_create");
                ret =
                    ABT_thread_attr_set_stacksize(thread_attr, stacksize + 128);
                ATS_ERROR(ret, "ABT_thread_attr_set_stacksize");
                ret = ABT_thread_create(main_pool, thread_func, NULL,
                                        thread_attr, &thread);
                ATS_ERROR(ret, "ABT_thread_create");
                ret = ABT_thread_attr_free(&thread_attr);
                ATS_ERROR(ret, "ABT_thread_attr_free");
            } else if (i == 2) {
                /* 3. ULT + user-given stack. */
                ABT_thread_attr thread_attr;
                ret = ABT_thread_attr_create(&thread_attr);
                ATS_ERROR(ret, "ABT_thread_attr_create");
                stack = calloc(1, stacksize);
                ret = ABT_thread_attr_set_stack(thread_attr, stack, stacksize);
                ATS_ERROR(ret, "ABT_thread_attr_set_stack");
                ret = ABT_thread_create(main_pool, thread_func, NULL,
                                        thread_attr, &thread);
                ATS_ERROR(ret, "ABT_thread_create");
                ret = ABT_thread_attr_free(&thread_attr);
                ATS_ERROR(ret, "ABT_thread_attr_free");
            }
            ret = ABT_thread_free(&thread);
            ATS_ERROR(ret, "ABT_thread_free");
            if (stack)
                free(stack);
            ret = pthread_join(helper_thread, NULL);
            assert(ret == 0);
        }
        /* Finalize */
        if (stack_i == num_stacksizes - 1) {
            ret = ATS_finalize(0);
            ATS_ERROR(ret, "ATS_finalize");
        } else {
            ret = ABT_finalize();
            ATS_ERROR(ret, "ABT_thread_free");
        }
    }
    return ret;
}

#else /* _POSIX_C_SOURCE */

int main()
{
    /* Unsupported. */
    return 77;
}

#endif
