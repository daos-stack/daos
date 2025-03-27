/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_POOL_CONFIG_H_INCLUDED
#define ABTI_POOL_CONFIG_H_INCLUDED

/* Inlined functions for pool config */

static inline ABTI_pool_config *ABTI_pool_config_get_ptr(ABT_pool_config config)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_pool_config *p_config;
    if (config == ABT_POOL_CONFIG_NULL) {
        p_config = NULL;
    } else {
        p_config = (ABTI_pool_config *)config;
    }
    return p_config;
#else
    return (ABTI_pool_config *)config;
#endif
}

static inline ABT_pool_config
ABTI_pool_config_get_handle(ABTI_pool_config *p_config)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_pool_config h_config;
    if (p_config == NULL) {
        h_config = ABT_POOL_CONFIG_NULL;
    } else {
        h_config = (ABT_pool_config)p_config;
    }
    return h_config;
#else
    return (ABT_pool_config)p_config;
#endif
}

#endif /* ABTI_POOL_CONFIG_H_INCLUDED */
