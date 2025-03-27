/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_GLOBAL_H_INCLUDED
#define ABTI_GLOBAL_H_INCLUDED

static inline ABTI_global *ABTI_global_get_global(void)
{
    ABTI_ASSERT(gp_ABTI_global);
    return gp_ABTI_global;
}

static inline ABTI_global *ABTI_global_get_global_or_null(void)
{
    return gp_ABTI_global;
}

static inline void ABTI_global_set_global(ABTI_global *p_global)
{
    gp_ABTI_global = p_global;
}

#endif /* ABTI_GLOBAL_H_INCLUDED */
