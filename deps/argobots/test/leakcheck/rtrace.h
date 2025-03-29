/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

/*
 * Resource allocation tracing library.
 *
 * This library checks resource leak especially when system resource allocation
 * fails.  This library overrides resource allocation functions to realize such
 * situations.
 *
 *
 * ## Usage
 *
 *   rtrace_init();
 *   do {
 *       rtrace_start();
 *       do_something();
 *   } while (!rtrace_stop());
 *   rtrace_finalize();
 *
 * This library checks if do_something() frees all the resources (malloc, mmap,
 * pthread_mutex_t, ... ) that are allocated between rtrace_start() and
 * rtrace_stop().  This library has a global state, so it can repeats the
 * execution while changing the resource allocation patterns.
 *
 *
 * ## Motivation drive by an example.
 *
 * Let's assume the following function.
 *
 *   void do_somthing() {
 *       int num_strs = 3;
 *       char **strs = (char **)malloc(sizeof(char *) * num_strs);
 *       if (!strs) {
 *           num_strs = 1; // Use a smaller number.
 *           strs = (char **)malloc(sizeof(char *) * num_strs);
 *       }
 *       if (strs) {
 *           for (int i = 0; i < num_strs; i++) {
 *               strs[i] = (char *)malloc(sizeof(char) * 128);
 *               if (strs[i] == NULL) {
 *                   free(strs);
 *                   return;
 *               }
 *           }
 *           [Use strs];
 *           for (int i = 0; i < num_strs; i++)
 *               free(strs[i]);
 *           free(strs);
 *       }
 *   }
 *
 * Normally, the program above successfully calls malloc() four times.  If so,
 * this program frees all the memory resources properly.  However, this program
 * leaks memory in the following case:
 *
 *   1st malloc() succeeds.
 *   2nd malloc() succeeds.
 *   3rd malloc() fails.
 *
 * In this case, the program returns without freeing the 2nd malloc()'ed memory.
 *
 * Some might fix this error as follows, which is also wrong.
 *
 *   for (int i = 0; i < num_strs; i++) {
 *       strs[i] = (char *)malloc(sizeof(char) * 128);
 *       if (strs[i] == NULL) {
 *           for (int j = 0; j <= i; j++)
 *               free(strs[j]);
 *           free(strs);
 *           return;
 *       }
 *   }
 *
 * The code above tries to free an invalid pointer (strs[i] is not allocated.
 * j < i should be a correct condition).  However, this is never checked since
 * malloc() usually succeeds in a testing environment.  Typically, any resource
 * allocation error paths are never checked.
 *
 * This librtrace tool repeats the execution to cover all the memory allocation
 * patterns.  For example, the original do_something() has the following
 * patterns.
 *
 * S - S - S - S : OK (the default case: all succeed)
 * S - S - S - F : memory leak
 * S - S - F     : memory leak
 * S - F         : OK
 * F - S - S     : OK
 * F - S - F     : OK
 * F - F         ; OK
 *
 * The wrong fix of do_something() can cause the following:
 *
 * S - S - S - S : OK
 * S - S - S - F : SEGV
 *
 * rtrace_start() - rtrace_stop() traces the memory allocation patterns and
 * changes the pattern in the next iteration.  As a result, it can simulate all
 * the possible patterns.
 *
 *
 * ## Target routines
 *
 * malloc(), calloc(), realloc(), posix_memalign(), free(), mmap(), munmap(),
 * pthread_create(), pthread_join(), pthread_mutex_init(),
 * pthread_mutex_destroy(), pthread_cond_init(), pthread_cond_destroy(),
 * pthread_barrier_init(), pthread_barrier_destroy()
 *
 * This library will support other functions (e.g., fopen(), valloc(), ...) if
 * Argobots starts to use them.
 *
 *
 * ## Assumption of this library
 *
 * Currently this library only controls success and failure of allocation
 * functions called by a thread that calls rtrace_start().  (NOTE: rtrace tracks
 * all the resources allocated by all the threads to check memory leak).
 * Since this has a global state, do not call rtrace_start() from multiple
 * threads.  This library assumes that the calling order of resource allocation
 * functions called on the target thread is deterministic.  This library does
 * not work if the code uses a singleton pattern, atexit(), or destructor of
 * pthread_key_t (this library is aware of basic Pthread functions regarding the
 * point above).
 *
 * This library is for a Linux machine.  Don't combine this library with other
 * malloc()-overriding or memory-tracing libraries (such as valgrind and address
 * sanitizers).
 *
 * Although this is developed for Argobots, this is not part of the Argobots
 * library, so don't assume any nice support for this tool.
 *
 *
 * ## Tips
 *
 * RTRACE_VERBOSE=0|1|2 would show information that would be useful for
 * debugging.  To know which memory allocation fails, RTRACE_BREAK_ALLOCID=X
 * would be helpful.
 *
 * To disable an artificial failure, rtrace_set_enabled() can be used.  For
 * example, if ABT_init() is not your interest, you can put
 * rtrace_set_enabled(0) and rtrace_set_enabled(1) to disable librtrace failure.
 *
 * Maybe you would like to use LD_PRELOAD to use this library.
 */

#ifndef RTRACE_H_INCLUDED
#define RTRACE_H_INCLUDED

void rtrace_init(void);
void rtrace_finalize(void);
void rtrace_start(void);
/* Return 1 if there is no possible trace. */
int rtrace_stop(void);
/* enabled = 0 will succeed all the operations. */
void rtrace_set_enabled(int enabled);

#endif /* RTRACE_H_INCLUDED */
