/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_POOL_USER_DEF_H_INCLUDED
#define ABTI_POOL_USER_DEF_H_INCLUDED

/* Inlined functions for pool user definitions */

static inline ABTI_pool_user_def *
ABTI_pool_user_def_get_ptr(ABT_pool_user_def def)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_pool_user_def *p_def;
    if (def == ABT_POOL_USER_DEF_NULL) {
        p_def = NULL;
    } else {
        p_def = (ABTI_pool_user_def *)def;
    }
    return p_def;
#else
    return (ABTI_pool_user_def *)def;
#endif
}

static inline ABT_pool_user_def
ABTI_pool_user_def_get_handle(ABTI_pool_user_def *p_def)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_pool_user_def h_def;
    if (p_def == NULL) {
        h_def = ABT_POOL_USER_DEF_NULL;
    } else {
        h_def = (ABT_pool_user_def)p_def;
    }
    return h_def;
#else
    return (ABT_pool_user_def)p_def;
#endif
}

#endif /* ABTI_POOL_USER_DEF_H_INCLUDED */
