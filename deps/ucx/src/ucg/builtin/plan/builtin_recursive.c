/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack.h>
#include <uct/api/uct_def.h>

#include "builtin_plan.h"

ucs_config_field_t ucg_builtin_recursive_config_table[] = {
    {"FACTOR", "2", "Recursive factor",
     ucs_offsetof(ucg_builtin_recursive_config_t, factor), UCS_CONFIG_TYPE_UINT},

    {NULL}
};

ucs_status_t ucg_builtin_recursive_create(ucg_builtin_group_ctx_t *ctx,
        const ucg_builtin_plan_topology_t *topology,
        const ucg_builtin_config_t *config,
        const ucg_group_params_t *group_params,
        const ucg_collective_type_t *coll_type,
        ucg_builtin_plan_t **plan_p)
{
    unsigned ppn = 0;
    unsigned host_up_cnt = 0;
    unsigned host_down_cnt = 0;
    ucg_group_member_index_t my_index = 0;
    ucg_group_member_index_t host_up[UCG_BUILTIN_TREE_MAX_RADIX] = {0};
    ucg_group_member_index_t host_down[UCG_BUILTIN_TREE_MAX_RADIX] = {0};
    enum ucg_group_member_distance master_phase = UCG_GROUP_MEMBER_DISTANCE_NET;
    ucg_builtin_plan_topology_t temp_topology = *topology;
    temp_topology.type = UCG_PLAN_TREE_FANIN;
    ucg_builtin_tree_params_t tree_params = {
        .group_params = group_params,
        .coll_type = coll_type,
        .topology = &temp_topology,
        .config = &config->tree,
        .root = 0,
        .ctx = ctx
    };

    /* Find who's above and who's below */
    ucs_status_t status = ucg_builtin_tree_add_intra(&tree_params, &my_index,
            &ppn, host_up, &host_up_cnt, host_down, &host_down_cnt, &master_phase);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }
    if (master_phase == UCG_GROUP_MEMBER_DISTANCE_HOST) {
        /* For host-master, the "up" direction is internode, so it's excluded */
        host_up_cnt = 0;
    }

    unsigned factor = config->recursive.factor;
    if (factor < 2) {
        ucs_error("Recursive K-ing factor must be at least 2 (given %u)", factor);
        return UCS_ERR_INVALID_PARAM;
    }

    /* Calculate the number of recursive steps */
    unsigned step_size      = 1;
    ucg_step_idx_t step_idx = 0;
    unsigned proc_count     = (group_params->member_count == ppn) ? ppn:
                               (group_params->member_count / ppn) +
                               ((group_params->member_count % ppn) > 0);
    while (step_size < proc_count) {
        step_size *= factor;
        step_idx++;
    }

    /* is proc_count a power of factor (e.g. 2 for recursive doubling) ? */
    if (step_size != proc_count) {
        if (group_params->member_count == ppn) {
            /* Fallback to tree-based intra-node reduction */
            ucs_assert((host_down_cnt + host_up_cnt) > 0);
            step_idx = 0;
        } else {
            ucs_error("Recursive K-ing must have proc# a power of the factor (factor %u procs %u)", factor, proc_count);
            /* Currently only an exact power of the recursive factor is supported */
            return UCS_ERR_UNSUPPORTED;
        }
    }

    /* If intra-node is recursive - discard the tree topology calculation */
    if ((group_params->member_count == ppn) && (step_idx)) {
        host_down_cnt = host_up_cnt = 0;
        ppn = 1;
    }

    /* Calculate how many steps are needed */
    unsigned alloc_phases, alloc_eps = 0;
    if (host_up_cnt) {
        /* This is a "leaf node", so it only does fanin+fanout */
        ucs_assert(host_up_cnt == 1);
        alloc_phases = 2;
    } else {
        /* Calculate the space for the recursive phases */
        alloc_phases = step_idx;
        if (factor != 2) {
            /* Allocate extra space for the map's multiple endpoints */
            alloc_eps += step_idx * (factor - 1);
        }

        if (host_down_cnt) {
            /* Also support in-host fanin+fanout (as master) */
            alloc_phases += 2;
            if (host_down_cnt > 1) {
                /* Require extra slots for endpoints */
                alloc_eps += 2 * host_down_cnt;
            }
        }
    }

    /* Allocate memory resources */
    size_t alloc_size               = sizeof(ucg_builtin_plan_t) +
                                      (alloc_phases * sizeof(ucg_builtin_plan_phase_t)) +
                                      (alloc_eps    * sizeof(uct_ep_h));
    ucg_builtin_plan_t *recursive   = (ucg_builtin_plan_t*)UCS_ALLOC_CHECK(alloc_size,
                                                                           "recursive topology");
    ucg_builtin_plan_phase_t *phase = &recursive->phss[0];
    uct_ep_h *next_ep               = (uct_ep_h*)(phase + alloc_phases);
    int is_mock                     = coll_type->modifiers &
                                      UCG_GROUP_COLLECTIVE_MODIFIER_MOCK_EPS;
    recursive->super.my_index       = my_index;
    recursive->phs_cnt              = 0;

    if (host_down_cnt || host_up_cnt) {
        /* Fill the first steps with the fanin stage */
        status = ucg_builtin_tree_connect(recursive, NULL, &tree_params, 0,
                next_ep, host_up, host_up_cnt, NULL, 0, NULL, 0,
                host_down, host_down_cnt);
        if (status != UCS_OK) {
            goto recursive_fail;
        }

        /* Prepare for the recursive loop */
        if (host_down_cnt) {
            next_ep += host_down_cnt;
        }
        alloc_phases--;
        phase++;
    }

    if (host_up_cnt) {
        alloc_phases += step_idx;
    } else {
        /* Calculate the peers for each step */
        for (step_idx = phase - &recursive->phss[0], step_size = ppn;
             ((step_idx < alloc_phases) && (status == UCS_OK));
             step_idx++, phase++, recursive->phs_cnt++, step_size *= factor) {

            unsigned step_base = my_index - (my_index % (step_size * factor));

            /* Recursive doubling is simpler */
            if (factor == 2) {
                ucg_group_member_index_t peer_index = step_base +
                        ((my_index - step_base + step_size) % (step_size << 1));
                status = ucg_builtin_single_connection_phase(ctx, peer_index,
                        step_idx, UCG_PLAN_METHOD_REDUCE_RECURSIVE, 0, phase, is_mock);
            } else {
                phase->method      = UCG_PLAN_METHOD_REDUCE_RECURSIVE;
                phase->ep_cnt      = factor - 1;
                phase->step_index  = step_idx;

#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
                phase->indexes     = UCS_ALLOC_CHECK((factor - 1) * sizeof(my_index),
                                                     "recursive topology indexes");
#endif

                /* In each step, there are one or more peers */
                unsigned step_peer_idx;
                for (step_peer_idx = 1;
                     ((step_peer_idx < factor) && (status == UCS_OK));
                     step_peer_idx++, phase->multi_eps = next_ep++) {

                    ucg_group_member_index_t peer_index = step_base +
                            ((my_index - step_base + step_size * step_peer_idx) %
                                    (step_size * factor));

                    ucs_info("%lu's peer #%u/%u (step #%u/%u): %lu ", my_index,
                            step_peer_idx, factor - 1, step_idx + 1,
                            recursive->phs_cnt, peer_index);

                    status = ucg_builtin_connect(ctx, peer_index, phase,
                                                 step_peer_idx - 1, 0, is_mock);
                }
            }
        }
        if (status != UCS_OK) {
            goto recursive_fail;
        }
    }

    if (host_down_cnt || host_up_cnt) {
        /* Fill the first steps with the fanout stage */
        temp_topology.type = UCG_PLAN_TREE_FANOUT;
        status = ucg_builtin_tree_connect(recursive, NULL, &tree_params,
                alloc_phases, next_ep, host_up, host_up_cnt, NULL, 0, NULL,
                0, host_down, host_down_cnt);
        if (status != UCS_OK) {
            goto recursive_fail;
        }

        if (host_down_cnt) {
            next_ep += host_down_cnt;
        }
        phase++;
    }

    recursive->ep_cnt = next_ep - (uct_ep_h*)phase;
    *plan_p = recursive;
    return UCS_OK;

recursive_fail:
    ucs_free(recursive);
    return status;
}
