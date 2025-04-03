/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"
#include <sys/mman.h>

ABTU_ret_err int ABTU_mprotect(void *addr, size_t size, ABT_bool protect)
{
#ifdef HAVE_MPROTECT
    int ret;
    if (protect) {
        ret = mprotect(addr, size, PROT_READ);
    } else {
        ret = mprotect(addr, size, PROT_READ | PROT_WRITE);
    }
    return ret == 0 ? ABT_SUCCESS : ABT_ERR_SYS;
#else
    return ABT_ERR_SYS;
#endif
}
