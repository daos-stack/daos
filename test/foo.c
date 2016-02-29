#include <stdbool.h>
#include <stdlib.h>
#include <pmix.h>
#include <pthread.h>

/* This is a test that does nothing but link a couple
 * of functions from expected libraries.  The code
 * should compile.
 */
int main(int argc, char **argv)
{
    pmix_proc_t proc;
    pthread_mutex_t lock;

    pthread_mutex_init(&lock, NULL);
    PMIx_Init(&proc);
    PMIx_Finalize();

    return 0;
}
