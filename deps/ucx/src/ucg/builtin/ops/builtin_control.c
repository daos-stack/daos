/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <stddef.h>
#include <ucs/sys/compiler_def.h>

#include "builtin_ops.h"

#ifndef MPI_IN_PLACE
#define MPI_IN_PLACE ((void*)0x1)
#endif

/*
 * Below is a list of possible callback functions for operation initialization.
 */
void ucg_builtin_init_dummy(ucg_builtin_op_t *op, ucg_coll_id_t coll_id) {}

void ucg_builtin_init_gather(ucg_builtin_op_t *op, ucg_coll_id_t coll_id)
{
    ucg_builtin_op_step_t *step = &op->steps[0];
    size_t len = step->buffer_length;
    memcpy(step->recv_buffer + (op->super.plan->group_id * len),
            step->send_buffer, len);
}

void ucg_builtin_init_reduce(ucg_builtin_op_t *op, ucg_coll_id_t coll_id)
{
    /* Skip unless root */
    if (op->super.params.type.root != op->super.plan->my_index) {
        return;
    }

    ucg_builtin_op_step_t *step = &op->steps[0];
    memcpy(step->recv_buffer, step->send_buffer, step->buffer_length);
}

/* Alltoall Bruck phase 1/3: shuffle the data */
void ucg_builtin_init_alltoall(ucg_builtin_op_t *op, ucg_coll_id_t coll_id)
{
    ucg_builtin_op_step_t *step = &op->steps[0];
    int bsize                   = step->buffer_length;
    int my_idx                  = op->super.plan->my_index;
    int nProcs                  = op->super.plan->group_size;
    int ii;

    /* Shuffle data: rank i displaces all data blocks "i blocks" upwards */
    for(ii=0; ii < nProcs; ii++){
        memcpy(step->send_buffer + bsize * ii,
               step->recv_buffer + bsize * ((ii + my_idx) % nProcs),
               bsize);
    }
}

/* Alltoall Bruck phase 2/3: send data */
void ucg_builtin_calc_alltoall(ucg_builtin_request_t *req, uint8_t *send_count,
                               size_t *base_offset, size_t *item_interval)
{
    int kk, nProcs = req->op->super.plan->group_size;

    // k = ceil( log(nProcs) / log(2) ) communication steps
    //      - For each step k, rank (i+2^k) sends all the data blocks whose k^{th} bits are 1
    for(kk = 0; kk < ceil( log(nProcs) / log(2) ); kk++){
        unsigned bit_k    = UCS_BIT(kk);
        send_count   [kk] = bit_k;
        base_offset  [kk] = bit_k;
        item_interval[kk] = bit_k;
    }
}

/* Alltoall Bruck phase 3/3: shuffle the data */
void ucg_builtin_fini_alltoall(ucg_builtin_op_t *op, ucg_coll_id_t coll_id)
{
    ucg_builtin_op_step_t *step = &op->steps[0];
    int bsize                   = step->buffer_length;
    int nProcs                  = op->super.plan->group_size;
    int ii;

    /* Shuffle data: rank i displaces all data blocks up by i+1 blocks and inverts vector */
    for(ii = 0; ii < nProcs; ii++){
        memcpy(step->send_buffer + bsize * ii,
               step->recv_buffer + bsize * (nProcs - 1 - ii),
               bsize);
    }
}

void ucg_builtin_init_scatter(ucg_builtin_op_t *op, ucg_coll_id_t coll_id)
{
    ucg_builtin_plan_t *plan    = ucs_derived_of(op->super.plan, ucg_builtin_plan_t);
    void *dst                   = op->steps[plan->phs_cnt - 1].recv_buffer;
    ucg_builtin_op_step_t *step = &op->steps[0];
    void *src                   = step->send_buffer;
    size_t length               = step->buffer_length;
    size_t offset               = length * plan->super.my_index;

    if (dst != src) {
        memcpy(dst + offset, src + offset, length);
    }
}

void ucg_builtin_calc_scatter(ucg_builtin_request_t *req, uint8_t *send_count,
                              size_t *base_offset, size_t *item_interval)
{
#if 0 /* Unused variables */
    ucg_builtin_plan_t *plan    = ucs_derived_of(req->op->super.plan, ucg_builtin_plan_t);
    ucg_builtin_op_step_t *step = req->step;
    *send_count                 = step->phase->ep_cnt;
    *base_offset                = step->buffer_length * step->phase->plan->super.my_index;
    *item_interval              = step->buffer_length;
#endif
}

ucs_status_t ucg_builtin_op_select_callbacks(ucg_builtin_plan_t *plan,
        ucg_builtin_op_init_cb_t *init_cb, ucg_builtin_op_init_cb_t *fini_cb)
{
    switch (plan->phss[0].method) {
    case UCG_PLAN_METHOD_REDUCE_WAYPOINT:
    case UCG_PLAN_METHOD_REDUCE_TERMINAL:
    case UCG_PLAN_METHOD_REDUCE_RECURSIVE:
        *init_cb = ucg_builtin_init_reduce;
        break;

    case UCG_PLAN_METHOD_GATHER_WAYPOINT:
        *init_cb = ucg_builtin_init_gather;
        break;

    case UCG_PLAN_METHOD_ALLTOALL_BRUCK:
        *init_cb = ucg_builtin_init_alltoall;
        *fini_cb = ucg_builtin_fini_alltoall;
        break;

    case UCG_PLAN_METHOD_PAIRWISE:
    case UCG_PLAN_METHOD_SCATTER_TERMINAL:
        *init_cb = ucg_builtin_init_scatter;
        break;

    default:
        *init_cb = ucg_builtin_init_dummy;
        break;
    }

    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_builtin_step_send_flags(ucg_builtin_op_step_t *step,
                            ucg_builtin_plan_phase_t *phase,
                            const ucg_collective_params_t *params,
                            enum ucg_builtin_op_step_flags *send_flag)
{
    size_t length    = step->buffer_length;
    size_t dt_len    = params->send.dt_len;

    /*
     * Short messages (e.g. RDMA "inline")
     */
    if (ucs_likely(length <= phase->max_short_one)) {
        /* Short send - single message */
        *send_flag            = UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT;
        step->fragments       = 1;
    } else if (ucs_likely(length <= phase->max_short_max)) {
        /* Short send - multiple messages */
        *send_flag            = (enum ucg_builtin_op_step_flags)
                                (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                                 UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);

        step->fragment_length = phase->max_short_one -
                               (phase->max_short_one % dt_len);
        step->fragments       = length / step->fragment_length +
                              ((length % step->fragment_length) > 0);

    /*
     * Large messages, if supported (e.g. RDMA "zero-copy")
     */
    } else if (ucs_unlikely((length >  phase->max_bcopy_max) &&
                            (phase->md_attr->cap.max_reg))) {
        if (ucs_likely(length < phase->max_zcopy_one)) {
            /* ZCopy send - single message */
            *send_flag            = UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;
            step->fragments       = 1;
        } else {
            /* ZCopy send - single message */
            *send_flag            = (enum ucg_builtin_op_step_flags)
                                    (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY |
                                     UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
            step->fragment_length = phase->max_zcopy_one -
                                   (phase->max_zcopy_one % dt_len);
            step->fragments       = length / step->fragment_length +
                                  ((length % step->fragment_length) > 0);
        }
    /*
     * Medium messages
     */
    } else if (ucs_likely(length <= phase->max_bcopy_one)) {
        /* BCopy send - single message */
        *send_flag            = UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY;
        step->fragment_length = step->buffer_length;
        step->fragments       = 1;
    } else {
        /* BCopy send - multiple messages */
        *send_flag            = (enum ucg_builtin_op_step_flags)
                                (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                                 UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
        step->fragment_length = phase->max_bcopy_one -
                               (phase->max_bcopy_one % dt_len);
        step->fragments       = length / step->fragment_length +
                              ((length % step->fragment_length) > 0);
    }

    return UCS_OK;
}

ucs_status_t ucg_builtin_step_create(ucg_builtin_plan_phase_t *phase,
                                     unsigned extra_flags,
                                     unsigned base_am_id,
                                     ucg_group_id_t group_id,
                                     const ucg_collective_params_t *params,
                                     int8_t **current_data_buffer,
                                     ucg_builtin_op_step_t *step)
{
    /* Set the parameters determining the send-flags later on */
    step->buffer_length      = params->send.dt_len * params->send.count;
    step->uct_md             = phase->md;
    if (phase->md) {
        step->uct_iface      = (phase->ep_cnt == 1) ? phase->single_ep->iface :
                                                      phase->multi_eps[0]->iface;
    }
    /* Note: we assume all the UCT endpoints have the same interface */
    step->phase                  = phase;
    step->am_id                  = base_am_id;
    step->batch_cnt              = phase->host_proc_cnt - 1;
    step->am_header.group_id     = group_id;
    step->am_header.msg.step_idx = phase->step_index;
    step->iter_ep                = 0;
    step->iter_offset            = 0;
    step->fragment_pending       = NULL;
    step->recv_buffer            = (int8_t*)params->recv.buf;
    step->send_buffer            = ((params->send.buf == MPI_IN_PLACE) ||
            !(extra_flags & UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP)) ?
                    (int8_t*)params->recv.buf : (int8_t*)params->send.buf;
    if (*current_data_buffer) {
        step->send_buffer = *current_data_buffer;
    } else {
        *current_data_buffer = step->recv_buffer;
    }
    ucs_assert(base_am_id >= UCP_AM_ID_LAST);

    /* Decide how the messages are sent (regardless of my role) */
    enum ucg_builtin_op_step_flags send_flag;
    ucs_status_t status = ucg_builtin_step_send_flags(step, phase, params, &send_flag);
    extra_flags |= (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    /* Set the actual step-related parameters */
    switch (phase->method) {
    /* Send-all, Recv-all */
    case UCG_PLAN_METHOD_PAIRWISE:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND;
        /* no break */

    /* Send-only */
    case UCG_PLAN_METHOD_SCATTER_TERMINAL:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_CALC_SENT_BUFFERS;
        step->calc_cb     = ucg_builtin_calc_scatter;
        /* no break */
    case UCG_PLAN_METHOD_SEND_TERMINAL:
        step->flags       = send_flag | extra_flags;
        break;

    /* Recv-only */
    case UCG_PLAN_METHOD_RECV_TERMINAL:
    case UCG_PLAN_METHOD_REDUCE_TERMINAL:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND;
        step->flags       = extra_flags;
        break;

    /* Recv-all, Send-one */
    case UCG_PLAN_METHOD_GATHER_WAYPOINT:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_CALC_SENT_BUFFERS;
        /* no break */
    case UCG_PLAN_METHOD_REDUCE_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_BEFORE_SEND1;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        step->recv_buffer = *current_data_buffer =
                (int8_t*)ucs_calloc(1, step->buffer_length, "ucg_fanin_waypoint_buffer");
        if (!step->recv_buffer) return UCS_ERR_NO_MEMORY;
        // TODO: memory registration, and de-registration at some point...
        break;

    /* Recv-one, Send-all */
    case UCG_PLAN_METHOD_BCAST_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        break;

    case UCG_PLAN_METHOD_SCATTER_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_CALC_SENT_BUFFERS;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        step->recv_buffer = *current_data_buffer =
                (int8_t*)ucs_calloc(1, step->buffer_length, "ucg_fanout_waypoint_buffer");
        if (!step->recv_buffer) return UCS_ERR_NO_MEMORY;
        // TODO: memory registration, and de-registration at some point...
        break;

    /* Recursive patterns */
    case UCG_PLAN_METHOD_REDUCE_RECURSIVE:
    case UCG_PLAN_METHOD_NEIGHBOR:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        step->flags       = send_flag | extra_flags;
        break;

    case UCG_PLAN_METHOD_ALLTOALL_BRUCK:
    case UCG_PLAN_METHOD_ALLGATHER_BRUCK: // TODO: fix for MPI_Allgather()
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_CALC_SENT_BUFFERS;
        step->flags       = send_flag | extra_flags;
        step->calc_cb     = ucg_builtin_calc_alltoall;
        break;
    }

    /* fill in additional data before finishing this step */
    if (phase->ep_cnt == 1) {
        step->flags |= UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT;
    }
    if (step->flags & send_flag) {
        step->am_header.remote_offset = 0;
    }

    /* memory registration (using the memory registration cache) */
    if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY) {
        ucs_status_t status = ucg_builtin_step_zcopy_prep(step);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }
    }

    /* Pipelining preparation */
    if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
        step->fragment_pending = (uint8_t*)UCS_ALLOC_CHECK(sizeof(phase->ep_cnt),
                                                           "ucg_builtin_step_pipelining");
    }

    /* Select the right completion callback */
    return ucg_builtin_step_select_callbacks(phase, &step->recv_cb, step->flags,
#ifdef HAVE_UCP_EXTENSIONS
            phase->ep_attr->cap.align_incast,
#else
            0,
#endif
            params->send.count > 0);
}

ucs_status_t ucg_builtin_op_create(ucg_plan_t *plan,
                                   const ucg_collective_params_t *params,
                                   ucg_op_t **new_op)
{
    ucs_status_t status;
    ucg_builtin_plan_t *builtin_plan     = (ucg_builtin_plan_t*)plan;
    ucg_builtin_plan_phase_t *next_phase = &builtin_plan->phss[0];
    unsigned phase_count                 = builtin_plan->phs_cnt;

    /* Check for non-zero-root trees */
    if (ucs_unlikely(params->type.root != 0)) {
        /* Assume the plan is tree-based, since Recursive K-ing has no root */
        status = ucg_builtin_topo_tree_set_root(params->type.root,
                plan->my_index, builtin_plan, &next_phase, &phase_count);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }
    }

    ucg_builtin_op_t *op                 = (ucg_builtin_op_t*)
            ucs_mpool_get_inline(&builtin_plan->op_mp);
    ucg_builtin_op_step_t *next_step     = &op->steps[0];
    unsigned am_id                       = builtin_plan->am_id;
    int8_t *current_data_buffer          = NULL;

    /* Select the right initialization callback */
    status = ucg_builtin_op_select_callbacks(builtin_plan, &op->init_cb, &op->fini_cb);
    if (status != UCS_OK) {
        goto op_cleanup;
    }

    /* Create a step in the op for each phase in the topology */
    if (phase_count == 1) {
        /* The only step in the plan */
        status = ucg_builtin_step_create(next_phase,
                UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP |
                UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP,
                am_id, plan->group_id, params,
                &current_data_buffer, next_step);
    } else {
        /* First step of many */
        status = ucg_builtin_step_create(next_phase,
                UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP, am_id, plan->group_id,
                params, &current_data_buffer, next_step);
        if (ucs_unlikely(status != UCS_OK)) {
            goto op_cleanup;
        }

        ucg_step_idx_t step_cnt;
        for (step_cnt = 1; step_cnt < phase_count - 1; step_cnt++) {
            status = ucg_builtin_step_create(++next_phase, 0, am_id,
                    plan->group_id, params, &current_data_buffer, ++next_step);
            if (ucs_unlikely(status != UCS_OK)) {
                goto op_cleanup;
            }
        }

        /* Last step gets a special flag */
        status = ucg_builtin_step_create(++next_phase,
                UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP, am_id, plan->group_id,
                params, &current_data_buffer, ++next_step);
    }
    if (ucs_unlikely(status != UCS_OK)) {
        goto op_cleanup;
    }

    /* Select the right optimization callback */
    status = ucg_builtin_op_consider_optimization(op,
            (ucg_builtin_config_t*)plan->planner->plan_config);
    if (status != UCS_OK) {
        goto op_cleanup;
    }

    UCS_STATIC_ASSERT(sizeof(ucg_builtin_header_t) <= UCP_WORKER_HEADROOM_PRIV_SIZE);
    UCS_STATIC_ASSERT(sizeof(ucg_builtin_header_t) == sizeof(uint64_t));

    op->slots  = (ucg_builtin_comp_slot_t*)builtin_plan->slots;
    *new_op    = &op->super;
    return UCS_OK;

op_cleanup:
    ucs_mpool_put_inline(op);
    return status;
}

void ucg_builtin_op_discard(ucg_op_t *op)
{
    ucg_builtin_op_t *builtin_op = (ucg_builtin_op_t*)op;
    ucg_builtin_op_step_t *step = &builtin_op->steps[0];
    do {
        if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY) {
            uct_md_mem_dereg(step->uct_md, step->zcopy.memh);
            ucs_free(step->zcopy.zcomp);
        }

        if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
            ucs_free((void*)step->fragment_pending);
        }
    } while (!((step++)->flags & UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP));

    ucs_mpool_put_inline(op);
}

ucs_status_t ucg_builtin_op_trigger(ucg_op_t *op, ucg_coll_id_t coll_id, ucg_request_t **request)
{
    /* Allocate a "slot" for this operation, from a per-group array of slots */
    ucg_builtin_op_t *builtin_op  = (ucg_builtin_op_t*)op;
    ucg_builtin_comp_slot_t *slot = &builtin_op->slots[coll_id % UCG_BUILTIN_MAX_CONCURRENT_OPS];
    slot->req.latest.coll_id      = coll_id;
    if (ucs_unlikely(slot->cb != NULL)) {
        ucs_error("UCG Builtin planner exceeded the max concurrent collectives.");
        return UCS_ERR_NO_RESOURCE;
    }

    /* Initialize the request structure, located inside the selected slot s*/
    ucg_builtin_request_t *builtin_req = &slot->req;
    builtin_req->op                    = builtin_op;
    ucg_builtin_op_step_t *first_step  = builtin_op->steps;
    builtin_req->step                  = first_step;
    builtin_req->pending               = first_step->fragments *
                                         first_step->phase->ep_cnt;
    slot->req.latest.step_idx          = first_step->am_header.msg.step_idx;

    /* Sanity checks */
    ucs_assert(first_step->iter_offset == 0);
    ucs_assert(first_step->iter_ep == 0);
    ucs_assert(request != NULL);

    /*
     * For some operations, like MPI_Reduce, MPI_Allreduce or MPI_Gather, the
     * local data has to be aggregated along with the incoming data. In others,
     * some shuffle is required once before starting (e.g. Bruck algorithms).
     */
    builtin_op->init_cb(builtin_op, coll_id);

    /* Consider optimization, if this operation is used often enough */
    if (ucs_unlikely(--builtin_op->opt_cnt == 0)) {
        ucs_status_t optm_status = builtin_op->optm_cb(builtin_op);
        if (ucs_unlikely(UCS_STATUS_IS_ERR(optm_status))) {
            return optm_status;
        }
        /* Need to return original status, becuase it can be OK or INPROGRESS */
    }

    /* Start the first step, which may actually complete the entire operation */
    return ucg_builtin_step_execute(builtin_req, request);
}
