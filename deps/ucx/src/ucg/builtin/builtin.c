/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>
#include <ucs/arch/atomic.h>
#include <ucs/profile/profile.h>
#include <ucp/core/ucp_request.inl>
#include <ucg/api/ucg_plan_component.h>

#include "ops/builtin_ops.h"
#include "plan/builtin_plan.h"

#define UCG_BUILTIN_SUPPORT_MASK (UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE |\
                                  UCG_GROUP_COLLECTIVE_MODIFIER_BROADCAST)

#define UCG_BUILTIN_PARAM_MASK   (UCG_GROUP_PARAM_FIELD_MEMBER_COUNT |\
                                  UCG_GROUP_PARAM_FIELD_MEMBER_INDEX |\
                                  UCG_GROUP_PARAM_FIELD_DISTANCES    |\
                                  UCG_GROUP_PARAM_FIELD_REDUCE_CB    |\
                                  UCG_GROUP_PARAM_FIELD_RESOLVER_CB)

static ucs_config_field_t ucg_builtin_config_table[] = {
    {"PLAN_", "", NULL, ucs_offsetof(ucg_builtin_config_t, super),
     UCS_CONFIG_TYPE_TABLE(ucg_plan_config_table)},

    {"TREE_", "", NULL, ucs_offsetof(ucg_builtin_config_t, tree),
     UCS_CONFIG_TYPE_TABLE(ucg_builtin_tree_config_table)},

    {"RECURSIVE_", "", NULL, ucs_offsetof(ucg_builtin_config_t, recursive),
     UCS_CONFIG_TYPE_TABLE(ucg_builtin_recursive_config_table)},

    {"NEIGHBOR_", "", NULL, ucs_offsetof(ucg_builtin_config_t, neighbor),
     UCS_CONFIG_TYPE_TABLE(ucg_builtin_neighbor_config_table)},

    {"CACHE_SIZE", "1000", "Number of cached collective operations",
     ucs_offsetof(ucg_builtin_config_t, cache_size), UCS_CONFIG_TYPE_UINT},

    {"SHORT_MAX_TX_SIZE", "256", "Largest send operation to use short messages",
     ucs_offsetof(ucg_builtin_config_t, short_max_tx), UCS_CONFIG_TYPE_MEMUNITS},

    {"BCOPY_MAX_TX_SIZE", "32768", "Largest send operation to use buffer copy",
     ucs_offsetof(ucg_builtin_config_t, bcopy_max_tx), UCS_CONFIG_TYPE_MEMUNITS},

    {"MEM_REG_OPT_CNT", "10", "Operation counter before registering the memory",
     ucs_offsetof(ucg_builtin_config_t, mem_reg_opt_cnt), UCS_CONFIG_TYPE_ULUNITS},

    {NULL}
};

extern ucg_plan_component_t ucg_builtin_component;

typedef struct ucg_builtin_ctx {
    ucs_ptr_array_t group_by_id;
} ucg_builtin_ctx_t;

struct ucg_builtin_group_ctx {
    /*
     * The following is the key structure of a group - an array of outstanding
     * collective operations, one slot per operation. Messages for future ops
     * may be stored in a slot before the operation actually starts.
     *
     * TODO: support more than this amount of concurrent operations...
     */
    ucg_builtin_comp_slot_t   slots[UCG_BUILTIN_MAX_CONCURRENT_OPS];

    /*
     * Resend slots is a bit-field indicating which slots require re-sending,
     * typically due to insufficient buffers on the reciever side (indicated by
     * UCS_ERR_NO_RESOURCES during the UCT call). On progress calls, all these
     * steps will be resumed by calling @ref ucg_builtin_step_execute on each.
     */
    uint64_t                  resend_slots;

    /* Mostly control-path, from here on */
    ucg_group_h               group;         /**< group handle */
    ucp_worker_h              worker;        /**< worker handle, for descriptors */
    const ucg_group_params_t *group_params;  /**< the original group parameters */
    ucg_group_member_index_t  host_proc_cnt; /**< Number of intra-node processes */
    ucg_group_id_t            group_id;      /**< Group identifier */
    uint16_t                  am_id;         /**< Active-Message identifier */
    ucg_builtin_config_t     *config;        /**< configured/default settings */
    ucs_list_link_t           plan_head;     /**< list of plans (for cleanup) */
    ucg_builtin_ctx_t        *bctx;          /**< per-group context (for cleanup) */
};

static ucs_status_t
ucg_builtin_choose_topology(enum ucg_collective_modifiers flags,
                            ucg_group_member_index_t group_size,
                            ucg_builtin_plan_topology_t *topology)
{
    if (flags & UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_SOURCE) {
        /* MPI_Bcast / MPI_Scatter */
        topology->type = UCG_PLAN_TREE_FANOUT;
        return UCS_OK;
    }

    if (flags & UCG_GROUP_COLLECTIVE_MODIFIER_SINGLE_DESTINATION) {
        /* MPI_Reduce / MPI_Gather */
        // TODO: Alex - test operand/operator support
        topology->type = UCG_PLAN_TREE_FANIN;
        return UCS_OK;
    }

    if (flags & UCG_GROUP_COLLECTIVE_MODIFIER_AGGREGATE) {
        /* MPI_Allreduce */
        if (ucs_popcount(group_size) > 1) {
            /* Not a power of two */
            topology->type = UCG_PLAN_TREE_FANIN_FANOUT;
        } else {
            topology->type = UCG_PLAN_RECURSIVE;
        }
        return UCS_OK;
    }

    /* MPI_Alltoall */
    ucs_assert(flags == 0);
    if (ucs_popcount(group_size) == 1) {
        topology->type = UCG_PLAN_ALLTOALL_BRUCK;
    } else {
        topology->type = UCG_PLAN_PAIRWISE;
    }
    return UCS_OK;
}

UCS_PROFILE_FUNC(ucs_status_t, ucg_builtin_am_handler,
                 (worker, data, length, am_flags),
                 void *worker, void *data, size_t length, unsigned am_flags)
{
    ucg_builtin_ctx_t *bctx = UCG_GLOBAL_COMPONENT_CTX(ucg_builtin_component, worker);
    ucg_builtin_header_t* header = data;
    ucs_assert(length >= sizeof(header));

    /* Find the Group context, based on the ID received in the header */
    ucg_group_id_t group_id = header->group_id;
    ucs_assert(group_id < bctx->group_by_id.size);
    ucg_builtin_group_ctx_t *gctx;
    ucs_ptr_array_lookup(&bctx->group_by_id, group_id, gctx);
    ucs_assert(gctx != NULL);

    /* Find the slot to be used, based on the ID received in the header */
    ucg_coll_id_t coll_id = header->msg.coll_id;
    ucg_builtin_comp_slot_t *slot = &gctx->slots[coll_id % UCG_BUILTIN_MAX_CONCURRENT_OPS];
    ucs_assert((slot->req.latest.coll_id != coll_id) ||
               (slot->req.latest.step_idx <= header->msg.step_idx));

    /* Consume the message if it fits the current collective and step index */
    if (ucs_likely(slot->cb && (header->msg.local_id == slot->req.latest.local_id))) {
        /* Make sure the packet indeed belongs to the collective currently on */
        data         = header + 1;
        length      -= sizeof(ucg_builtin_header_t);

        ucs_assert((header->remote_offset + length) <=
                   slot->req.step->buffer_length);
        ucs_assert((length == 0) ||
                   (length == slot->req.step->buffer_length) ||
                   ((length <= slot->req.step->fragment_length) &&
                    (slot->req.step->fragments > 1)));

        ucs_trace_req("ucg_builtin_am_handler CB: coll_id %u step_idx %u cb %p pending %u",
                header->msg.coll_id, header->msg.step_idx, slot->cb, slot->req.pending);

        UCS_PROFILE_CODE("ucg_builtin_am_handler_cb") {
            (void) slot->cb(&slot->req, header->remote_offset, data, length);
        }

        return UCS_OK;
    }

    ucs_trace_req("ucg_builtin_am_handler STORE: group_id %u "
                  "coll_id %u(%u) step_idx %u slot_step_idx %u",
                  header->group_id, header->msg.coll_id, slot->req.latest.coll_id,
                  header->msg.step_idx, slot->req.latest.step_idx);

    /* Store the message (if the relevant step has not been reached) */
    ucp_recv_desc_t *rdesc;
    ucs_status_t ret = ucp_recv_desc_init(worker, data, length,
            0, am_flags, 0, 0, 0, &rdesc);
    if (ucs_likely(ret != UCS_ERR_NO_MEMORY)) {
        uint32_t placeholder;
        (void) ucs_ptr_array_insert(&slot->messages, rdesc, &placeholder);
    }
    return ret;
}

#ifndef HAVE_UCP_EXTENSIONS
static unsigned ucg_am_id;
#else
static void ucg_builtin_msg_dump(ucp_worker_h worker, uct_am_trace_type_t type,
                                 uint8_t id, const void *data, size_t length,
                                 char *buffer, size_t max)
{
    const ucg_builtin_header_t *header = (const ucg_builtin_header_t*)data;
    snprintf(buffer, max, "COLLECTIVE [coll_id %u step_idx %u offset %lu length %lu]",
             (unsigned)header->msg.coll_id, (unsigned)header->msg.step_idx,
             (uint64_t)header->remote_offset, length - sizeof(*header));
}
#endif

static ucs_status_t ucg_builtin_query(unsigned ucg_api_version,
        unsigned available_am_id, ucg_plan_desc_t **desc_p, unsigned *num_descs_p)
{
#ifdef HAVE_UCP_EXTENSIONS
    /* Set the Active Message handler (before creating the UCT interfaces) */
    ucp_am_handler_t* am_handler     = ucp_am_handlers + available_am_id;
    am_handler->features             = UCP_FEATURE_GROUPS;
    am_handler->cb                   = ucg_builtin_am_handler;
    am_handler->tracer               = ucg_builtin_msg_dump;
    am_handler->flags                = 0;
#else
    ucg_am_id = available_am_id;
#endif

    /* Return a simple description of the "Builtin" module */
    ucs_status_t status              = ucg_plan_single(&ucg_builtin_component,
                                                       desc_p, num_descs_p);
    (*desc_p)[0].modifiers_supported = UCG_BUILTIN_SUPPORT_MASK;
    (*desc_p)[0].flags = 0;
    return status;
}

static ucg_group_member_index_t
ucg_builtin_calc_host_proc_cnt(const ucg_group_params_t *group_params)
{
    ucg_group_member_index_t index, count = 0;
    for (index = 0; index < group_params->member_count; index++) {
        if (group_params->distance[index] < UCG_GROUP_MEMBER_DISTANCE_NET) {
            count++;
        }
    }
    return count;
}

static ucs_status_t ucg_builtin_create(ucg_plan_component_t *plan_component,
                                       ucg_worker_h worker,
                                       ucg_group_h group,
                                       ucg_group_id_t group_id,
                                       const ucg_group_params_t *group_params)
{
    if ((group_params->field_mask & UCG_BUILTIN_PARAM_MASK) != UCG_BUILTIN_PARAM_MASK) {
        ucs_error("UCG Planner \"Builtin\" is missing some group parameters");
        return UCS_ERR_INVALID_PARAM;
    }

    /* Fill in the information in the per-group context */
    ucg_builtin_group_ctx_t *gctx =
            UCG_GROUP_COMPONENT_CTX(ucg_builtin_component, group);
    ucg_builtin_ctx_t *bctx =
            UCG_GLOBAL_COMPONENT_CTX(ucg_builtin_component, worker);
    gctx->group                   = group;
    gctx->group_id                = group_id;
    gctx->group_params            = group_params;
    gctx->host_proc_cnt           = ucg_builtin_calc_host_proc_cnt(group_params);
    gctx->config                  = plan_component->plan_config;
    gctx->am_id                   = plan_component->allocated_am_id;
    gctx->bctx                    = bctx;
    gctx->resend_slots            = 0; /* No pending resends */
    ucs_list_head_init(&gctx->plan_head);

    unsigned i;
    for (i = 0; i < UCG_BUILTIN_MAX_CONCURRENT_OPS; i++) {
        ucg_builtin_comp_slot_t *slot = &gctx->slots[i];
        ucs_ptr_array_init(&slot->messages, 0, "builtin messages");
        slot->req.latest.step_idx = 0;
        slot->req.latest.coll_id = 0;
        slot->cb = NULL;
    }

    /* Create or expand the per-worker context - for the AM-handler's use */
    if (ucs_unlikely(group_id == 0)) {
        /* First group ever created - initialize the global context */
        ucs_ptr_array_init(&bctx->group_by_id, 0, "builtin_group_table");
        ucg_builtin_mpi_reduce_cb = group_params->mpi_reduce_f;
    }

#ifndef HAVE_UCP_EXTENSIONS
    for (i = 0; i < worker->num_ifaces; i++) {
        ucs_status_t status = uct_iface_set_am_handler(worker->ifaces[i]->iface,
                ucg_am_id, ucg_builtin_am_handler, worker, 0);
        if (status != UCS_OK) {
            return status;
        }
    }
#endif

    (void) ucs_ptr_array_replace(&bctx->group_by_id, group_id, gctx);
    return UCS_OK;
}

static void ucg_builtin_destroy(ucg_group_h group)
{
    ucg_builtin_group_ctx_t *gctx =
            UCG_GROUP_COMPONENT_CTX(ucg_builtin_component, group);

    /* Cleanup left-over messages and outstanding operations */
    unsigned i, j;
    for (i = 0; i < UCG_BUILTIN_MAX_CONCURRENT_OPS; i++) {
        ucg_builtin_comp_slot_t *slot = &gctx->slots[i];
        if (slot->cb != NULL) {
            ucs_warn("Collective operation #%u has been left incomplete (Group #%u)",
                    gctx->slots[i].req.latest.coll_id, gctx->group_id);
        }

        ucp_recv_desc_t *rdesc;
        ucs_ptr_array_for_each(rdesc, j, &slot->messages) {
            ucs_warn("Collective operation #%u still has a pending message for"
                     "step #%u (Group #%u)",
                     ((ucg_builtin_header_t*)(rdesc + 1))->msg.coll_id,
                     ((ucg_builtin_header_t*)(rdesc + 1))->msg.step_idx,
                     ((ucg_builtin_header_t*)(rdesc + 1))->group_id);
#ifdef HAVE_UCP_EXTENSIONS
            if (!(rdesc->flags & UCP_RECV_DESC_FLAG_UCT_DESC_SHARED)) {
                ucp_recv_desc_release(rdesc, NULL);
            } /* No UCT interface information, we can't release if it's shared */
#endif
            ucp_recv_desc_release(rdesc);
            ucs_ptr_array_remove(&slot->messages, j, 0);
        }
        ucs_ptr_array_cleanup(&slot->messages);
    }


    /* Cleanup plans created for this group */
    while (!ucs_list_is_empty(&gctx->plan_head)) {
        ucg_builtin_plan_t *plan = ucs_list_extract_head(&gctx->plan_head,
                ucg_builtin_plan_t, list);

        while (!ucs_list_is_empty(&plan->super.op_head)) {
            ucg_op_t *op = ucs_list_extract_head(&plan->super.op_head, ucg_op_t, list);
            ucg_builtin_op_discard(op);
        }

        ucs_mpool_cleanup(&plan->op_mp, 1);
        ucs_free(plan);
    }

    /* Remove the group from the global storage array */
    ucg_builtin_ctx_t *bctx = gctx->bctx;
    ucs_ptr_array_remove(&bctx->group_by_id, gctx->group_id, 0);
    if (ucs_unlikely(gctx->group_id == 0)) {
        ucs_ptr_array_cleanup(&bctx->group_by_id);
    }
}

static unsigned ucg_builtin_progress(ucg_group_h group)
{
    ucg_builtin_group_ctx_t *gctx =
            UCG_GROUP_COMPONENT_CTX(ucg_builtin_component, group);

    /* Reset the list of active slots, then re-test (some may return to it) */
    uint64_t resend_slots = ucs_atomic_swap64(&gctx->resend_slots, 0);
    if (ucs_likely(resend_slots == 0)) {
        return 0;
    }

    unsigned index, ret = 0;
    ucs_for_each_bit(index, resend_slots) {
        ucg_builtin_request_t *req = &gctx->slots[index].req;
        ucs_status_t status = ucg_builtin_step_execute(req, NULL);
        if (status != UCS_INPROGRESS) {
            ret++;
        }
    }

    return ret;
}

ucs_mpool_ops_t ucg_builtin_plan_mpool_ops = {
    .chunk_alloc   = ucs_mpool_hugetlb_malloc,
    .chunk_release = ucs_mpool_hugetlb_free,
    .obj_init      = ucs_empty_function,
    .obj_cleanup   = ucs_empty_function
};

static ucs_status_t ucg_builtin_plan(ucg_plan_component_t *plan_component,
                                     const ucg_collective_type_t *coll_type,
                                     ucg_group_h group,
                                     ucg_plan_t **plan_p)
{
    /* Check what kind of resources are available to the group (e.g. SM) */
    ucg_builtin_plan_topology_t topology = {0};
    ucs_status_t status = ucg_plan_query_resources(group, &topology.resources);
    if (status != UCS_OK) {
        return status;
    }

    /* Choose the best topology for this collective operation type */
    ucg_builtin_group_ctx_t *builtin_ctx =
            UCG_GROUP_COMPONENT_CTX(ucg_builtin_component, group);
    status = ucg_builtin_choose_topology(coll_type->modifiers,
            builtin_ctx->group_params->member_count, &topology);
    if (status != UCS_OK) {
        return status;
    }

    /* Build the topology according to the requested */
    ucg_builtin_plan_t *plan;
    switch(topology.type) {
    case UCG_PLAN_RECURSIVE:
        status = ucg_builtin_recursive_create(builtin_ctx, &topology,
                plan_component->plan_config, builtin_ctx->group_params, coll_type, &plan);
        break;

    case UCG_PLAN_ALLTOALL_BRUCK:
        status = ucg_builtin_bruck_create(builtin_ctx, &topology,
                plan_component->plan_config, builtin_ctx->group_params, coll_type, &plan);
        break;

    case UCG_PLAN_PAIRWISE:
        status = ucg_builtin_pairwise_create(builtin_ctx, &topology,
                plan_component->plan_config, builtin_ctx->group_params, coll_type, &plan);
        break;

    case UCG_PLAN_TREE_FANIN:
    case UCG_PLAN_TREE_FANOUT:
    case UCG_PLAN_TREE_FANIN_FANOUT:
        status = ucg_builtin_tree_create(builtin_ctx, &topology,
                plan_component->plan_config, builtin_ctx->group_params, coll_type, &plan);
        break;
    }

    if (status != UCS_OK) {
        return status;
    }

    /* Create a memory-pool for operations for this plan */
    size_t op_size = sizeof(ucg_builtin_op_t) + plan->phs_cnt * sizeof(ucg_builtin_op_step_t);
    status = ucs_mpool_init(&plan->op_mp, 0, op_size, 0, UCS_SYS_CACHE_LINE_SIZE,
            1, UINT_MAX, &ucg_builtin_plan_mpool_ops, "ucg_builtin_plan_mp");
    if (status != UCS_OK) {
        return status;
    }

    ucs_list_add_head(&builtin_ctx->plan_head, &plan->list);
    plan->slots     = &builtin_ctx->slots[0];
    plan->am_id     = builtin_ctx->am_id;
    *plan_p         = (ucg_plan_t*)plan;
    return UCS_OK;
}

static void ucg_builtin_print(ucg_plan_t *plan, const ucg_collective_params_t *coll_params)
{
    ucs_status_t status;
    ucg_builtin_plan_t *builtin_plan = (ucg_builtin_plan_t*)plan;
    printf("Planner:    %s\n", builtin_plan->super.planner->name);
    printf("Endpoints:  %i\n", builtin_plan->ep_cnt);
    printf("Phases:     %i\n", builtin_plan->phs_cnt);

    printf("Object memory size:\n");
    printf("\tPer-group context: %lu bytes\n", sizeof(ucg_builtin_group_ctx_t));
    printf("\tPlan: %lu bytes\n", sizeof(ucg_builtin_plan_t) +
            builtin_plan->phs_cnt * sizeof(ucg_builtin_plan_phase_t) +
            builtin_plan->ep_cnt * sizeof(uct_ep_h));
    printf("\tOperation: %lu bytes (%lu per step)\n", sizeof(ucg_builtin_op_t) +
            builtin_plan->phs_cnt * sizeof(ucg_builtin_op_step_t),
            sizeof(ucg_builtin_op_step_t));
    printf("\tRequest: %lu bytes\n", sizeof(ucg_builtin_request_t));
    printf("\tSlot: %lu bytes\n", sizeof(ucg_builtin_comp_slot_t));

    unsigned phase_idx;
    for (phase_idx = 0; phase_idx < builtin_plan->phs_cnt; phase_idx++) {
        printf("Phase #%i: ", phase_idx);
        printf("the method is ");
        switch (builtin_plan->phss[phase_idx].method) {
        case UCG_PLAN_METHOD_SEND_TERMINAL:
            printf("Send (T), ");
            break;
        case UCG_PLAN_METHOD_RECV_TERMINAL:
            printf("Recv (T), ");
            break;
        case UCG_PLAN_METHOD_BCAST_WAYPOINT:
            printf("Bcast (W), ");
            break;
        case UCG_PLAN_METHOD_SCATTER_TERMINAL:
            printf("Scatter (T), ");
            break;
        case UCG_PLAN_METHOD_SCATTER_WAYPOINT:
            printf("Scatter (W), ");
            break;
        case UCG_PLAN_METHOD_GATHER_WAYPOINT:
            printf("Gather (W), ");
            break;
        case UCG_PLAN_METHOD_REDUCE_TERMINAL:
            printf("Reduce (T), ");
            break;
        case UCG_PLAN_METHOD_REDUCE_WAYPOINT:
            printf("Reduce (W), ");
            break;
        case UCG_PLAN_METHOD_REDUCE_RECURSIVE:
            printf("Reduce (R), ");
            break;
        case UCG_PLAN_METHOD_ALLGATHER_BRUCK:
            printf("Allgather (G), ");
            break;
        case UCG_PLAN_METHOD_ALLTOALL_BRUCK:
            printf("Alltoall (B), ");
            break;
        case UCG_PLAN_METHOD_PAIRWISE:
            printf("Alltoall (P), ");
            break;
        case UCG_PLAN_METHOD_NEIGHBOR:
            printf("Neighbors, ");
            break;
        }

#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
        ucg_builtin_plan_phase_t *phase = &builtin_plan->phss[phase_idx];
        if ((phase->ep_cnt == 1) &&
            (phase->indexes[0] == UCG_GROUP_MEMBER_INDEX_UNSPECIFIED)) {
            printf("with all same-level peers (collective-aware transport)\n");
        } else {
            uct_ep_h *ep = (phase->ep_cnt == 1) ? &phase->single_ep :
                                                   phase->multi_eps;
            printf("with the following peers: ");

            unsigned peer_idx;
            for (peer_idx = 0;
                 peer_idx < phase->ep_cnt;
                 peer_idx++, ep++) {
                printf("%lu,", phase->indexes[peer_idx]);
            }
            printf("\n");
        }
#else
        printf("no peer info (configured without \"--enable-debug-data\")");
#endif

        if (coll_params) {
            int flags = 0;
            if (phase_idx == 0) {
                flags |= UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP;
            }
            if (phase_idx == (builtin_plan->phs_cnt - 1)) {
                flags |= UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
            }

            int8_t *temp_buffer = NULL;
            ucg_builtin_op_step_t step;
            printf("Step #%i (actual index used: %u):", phase_idx,
                    builtin_plan->phss[phase_idx].step_index);
            status = ucg_builtin_step_create(&builtin_plan->phss[phase_idx],
                    flags, 0, plan->group_id, coll_params, &temp_buffer, &step);
            if (status != UCS_OK) {
                printf("failed to create, %s", ucs_status_string(status));
            }

            printf("\n\tBuffer Length: %lu", step.buffer_length);
            if (step.flags & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
                printf("\n\tFragment Length: %lu", step.fragment_length);
                printf("\n\tFragment Count: %u", step.fragments);
            }

            int flag;
            printf("\n\tFlags:");
            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND) != 0);
            printf("\n\t\t(Pre-)RECV1:\t\t%i", flag);
            if (flag)
                printf(" (buffer: %s)", strlen((char*)step.recv_buffer) ?
                        (char*)step.recv_buffer : "temp-buffer");

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_RECV_BEFORE_SEND1) != 0);
            printf("\n\t\t(Pre-)RECVn:\t\t%i", flag);
            if (flag)
                printf(" (buffer: %s)", strlen((char*)step.recv_buffer) ?
                        (char*)step.recv_buffer : "temp-buffer");

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT) ||
                    (step.flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY) ||
                    (step.flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY));
            printf("\n\t\t      SEND:\t\t%i", flag);
            if (flag)
                printf(" (buffer: %s)", strlen((char*)step.send_buffer) ?
                        (char*)step.send_buffer : "temp-buffer");

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND) != 0);
            printf("\n\t\t(Post)RECV:\t\t%i", flag);
            if (flag)
                printf(" (buffer: %s)", strlen((char*)step.recv_buffer) ?
                        (char*)step.recv_buffer : "temp-buffer");

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT) != 0);
            printf("\n\t\tSINGLE_ENDPOINT:\t%i", flag);

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_CALC_SENT_BUFFERS) != 0);
            printf("\n\t\tCALC_SENT_BUFFERS:\t%i", flag);

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) != 0);
            printf("\n\t\tFRAGMENTED:\t\t%i", flag);

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) != 0);
            printf("\n\t\tPIPELINED:\t\t%i", flag);

            flag = ((step.flags & UCG_BUILTIN_OP_STEP_FLAG_LOCKED_PACK_CB) != 0);
            printf("\n\t\tLOCKED_PACK_CB:\t\t%i", flag);

            printf("\n\n");
        }
    }
}

#define UCG_BUILTIN_CONNECT_SINGLE_EP ((unsigned)-1)
static uct_iface_attr_t mock_ep_attr;

ucs_status_t ucg_builtin_connect(ucg_builtin_group_ctx_t *ctx,
        ucg_group_member_index_t idx, ucg_builtin_plan_phase_t *phase,
        unsigned phase_ep_index, enum ucg_plan_connect_flags flags, int is_mock)
{
#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
    phase->indexes[(phase_ep_index != UCG_BUILTIN_CONNECT_SINGLE_EP) ?
            phase_ep_index : 0] = flags ? UCG_GROUP_MEMBER_INDEX_UNSPECIFIED : idx;
#endif
    if (is_mock) {
        phase->max_short_one = UCS_MEMUNITS_INF;
        memset(&mock_ep_attr, 0, sizeof(mock_ep_attr));
        phase->ep_attr = &mock_ep_attr;
        phase->md = NULL;
        return UCS_OK;
    }

    uct_ep_h ep;
    ucs_status_t status = ucg_plan_connect(ctx->group, idx, flags,
            &ep, &phase->ep_attr, &phase->md, &phase->md_attr);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    phase->resends = &ctx->resend_slots;
    phase->host_proc_cnt = ctx->host_proc_cnt;
    if (phase_ep_index == UCG_BUILTIN_CONNECT_SINGLE_EP) {
        phase->single_ep = ep;
    } else {
        ucs_assert(phase_ep_index < phase->ep_cnt);
        phase->multi_eps[phase_ep_index] = ep;
    }

    /* Set the thresholds */
    if (phase->ep_attr->cap.flags & UCT_IFACE_FLAG_AM_SHORT) {
        phase->max_short_one = phase->ep_attr->cap.am.max_short - sizeof(ucg_builtin_header_t);
        phase->max_short_max = ctx->config->short_max_tx - sizeof(ucg_builtin_header_t);
        // TODO: support UCS_CONFIG_MEMUNITS_AUTO
        if (phase->max_short_one > phase->max_short_max) {
            phase->max_short_one = phase->max_short_max - sizeof(ucg_builtin_header_t);
        }
    } else {
        phase->max_short_one = phase->max_short_max = 0;
    }

    ucs_assert(phase->ep_attr->cap.flags & UCT_IFACE_FLAG_AM_BCOPY);
    phase->max_bcopy_one = phase->ep_attr->cap.am.max_bcopy - sizeof(ucg_builtin_header_t);
    if (phase->md_attr->cap.max_reg) {
        phase->max_bcopy_max = ctx->config->bcopy_max_tx - sizeof(ucg_builtin_header_t);
        // TODO: support UCS_CONFIG_MEMUNITS_AUTO
        if (phase->max_bcopy_one > phase->max_bcopy_max) {
            phase->max_bcopy_one = phase->max_bcopy_max - sizeof(ucg_builtin_header_t);
        }

        phase->max_zcopy_one = phase->ep_attr->cap.am.max_zcopy - sizeof(ucg_builtin_header_t);
        if (phase->max_zcopy_one < phase->max_bcopy_max) {
            phase->max_zcopy_one = phase->max_bcopy_max - sizeof(ucg_builtin_header_t);
        }
    } else {
        // TODO: issue a warning?
        phase->max_zcopy_one = phase->max_bcopy_max = UCS_MEMUNITS_INF;
    }
    return status;
}

ucs_status_t ucg_builtin_single_connection_phase(ucg_builtin_group_ctx_t *ctx,
        ucg_group_member_index_t idx, ucg_step_idx_t step_index,
        enum ucg_builtin_plan_method_type method,
        enum ucg_plan_connect_flags flags,
        ucg_builtin_plan_phase_t *phase,
        int is_mock)
{
    phase->ep_cnt     = 1;
    phase->step_index = step_index;
    phase->method     = method;

#if ENABLE_DEBUG_DATA || ENABLE_FAULT_TOLERANCE
    phase->indexes = UCS_ALLOC_CHECK(sizeof(idx), "phase indexes");
#endif

    return ucg_builtin_connect(ctx, idx, phase, UCG_BUILTIN_CONNECT_SINGLE_EP,
            flags, is_mock);
}


UCG_PLAN_COMPONENT_DEFINE(ucg_builtin_component, "builtin", sizeof(ucg_builtin_ctx_t),
                          sizeof(ucg_builtin_group_ctx_t), ucg_builtin_query,
                          ucg_builtin_create, ucg_builtin_destroy,
                          ucg_builtin_progress, ucg_builtin_plan,
                          ucg_builtin_op_create, ucg_builtin_op_trigger,
                          ucg_builtin_op_discard, ucg_builtin_print, "BUILTIN_",
                          ucg_builtin_config_table, ucg_builtin_config_t);

