/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack.h>
#include <uct/api/uct_def.h>
#include <ucg/api/ucg_mpi.h>

#include "builtin_plan.h"

ucs_status_t ucg_builtin_pairwise_create(ucg_builtin_group_ctx_t *ctx,
        const ucg_builtin_plan_topology_t *topology,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p)
{
    /* Calculate the number of pairwise steps */
    ucg_group_member_index_t proc_idx, proc_count = group_params->member_count;

    /* Allocate memory resources */
    size_t alloc_size = sizeof(ucg_builtin_plan_t) +
                        sizeof(ucg_builtin_plan_phase_t) +
                        ((proc_count - 1) * sizeof(uct_ep_h));

    ucg_builtin_plan_t *pairwise    = (ucg_builtin_plan_t*)UCS_ALLOC_CHECK(alloc_size, "pairwise topology");
    ucg_builtin_plan_phase_t *phase = &pairwise->phss[0];
    pairwise->ep_cnt                = proc_count - 1;
    pairwise->phs_cnt               = 1;

    ucs_assert((ucg_group_member_index_t)((typeof(pairwise->ep_cnt))-1) > proc_count);

    /* Calculate the peers for each step */
    phase->multi_eps  = (uct_ep_h*)(phase + 1);
    phase->method     = UCG_PLAN_METHOD_PAIRWISE;
    phase->ep_cnt     = proc_count - 1;
    phase->step_index = 1;

    ucg_group_member_index_t my_index = group_params->member_index;
#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
    phase->indexes = UCS_ALLOC_CHECK(phase->ep_cnt * sizeof(my_index),
                                     "pairwise topology indexes");
#endif

    int is_mock = coll_type->modifiers & UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS;
    for(proc_idx = 1; proc_idx < proc_count; proc_idx++) {
        /* Connect to receiver for second EP */
        ucg_group_member_index_t next_peer = (my_index + proc_idx) % proc_count;
        ucs_status_t status = ucg_builtin_connect(ctx, next_peer, phase,
                                                  proc_idx - 1, 0, is_mock);
        if (status != UCS_OK) {
            return status;
        }
    }

    pairwise->super.my_index = my_index;
    *plan_p = pairwise;
    return UCS_OK;
}
