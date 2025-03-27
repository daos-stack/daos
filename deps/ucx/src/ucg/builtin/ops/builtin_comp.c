/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "builtin_ops.h"
#include <ucs/profile/profile.h>

#include "builtin_comp_step.inl"

/*
 * Below is a list of possible callback/helper functions for an incoming message.
 * Upon arrival, a message is typically copied or reduced to its collective's
 * final recieve buffer, though there are some complex collectives which are
 * handled otherwise (using intermediate buffers).
 */

mpi_reduce_f ucg_builtin_mpi_reduce_cb;
static void UCS_F_ALWAYS_INLINE
ucg_builtin_mpi_reduce(void *mpi_op, void *src, void *dst,
        unsigned dcount, void* mpi_datatype)
{
    UCS_PROFILE_CALL_VOID(ucg_builtin_mpi_reduce_cb, mpi_op, (char*)src,
            (char*)dst, dcount, mpi_datatype);
}

#define ucg_builtin_mpi_reduce_req(_req, _offset, _data, _length, _params) \
{ \
    ucg_collective_params_t *params = _params; \
    ucs_assert(length == (params->recv.count * params->recv.dt_len)); \
    ucg_builtin_mpi_reduce(params->recv.op_ext,  _data, \
                           (_req)->step->recv_buffer + offset, \
                           params->recv.count, params->recv.dt_ext); \
}

static void UCS_F_ALWAYS_INLINE ucg_builtin_memcpy(void *src, void *dst, size_t length)
{
    UCS_PROFILE_CALL_VOID(memcpy, (char*)src, (char*)dst, length);
}

#define UCG_IF_PENDING_REACHED(req, num) \
    ucs_assert(req->pending > (num)); if (--req->pending == (num))

int static UCS_F_ALWAYS_INLINE
ucg_builtin_comp_step_check_cb(ucg_builtin_request_t *req)
{
    UCG_IF_PENDING_REACHED(req, 0) {
        (void) ucg_builtin_comp_step_cb(req, NULL);
        return 1;
    }

    return 0;
}

int static UCS_F_ALWAYS_INLINE
ucg_builtin_comp_send_check_cb(ucg_builtin_request_t *req, uint32_t pending)
{
    UCG_IF_PENDING_REACHED(req, pending) {
        (void) ucg_builtin_step_execute(req, NULL);
        return 1;
    }

    return 0;
}

int static UCS_F_ALWAYS_INLINE
ucg_builtin_comp_send_check_frag_cb(ucg_builtin_request_t *req, uint64_t offset)
{
    ucg_builtin_op_step_t *step = req->step;
    unsigned frag_idx = offset / step->fragment_length;
    ucs_assert(step->fragment_pending[frag_idx] > 0);
    if (--step->fragment_pending[frag_idx] == 0) {
        if (ucs_unlikely(step->iter_offset == UCG_BUILTIN_OFFSET_PIPELINE_PENDING)) {
            step->fragment_pending[frag_idx] = UCG_BUILTIN_FRAG_PENDING;
        } else {
            step->iter_offset = offset;
            (void) ucg_builtin_step_execute(req, NULL);
            return 1;
        }
    }

    return step->iter_offset != UCG_BUILTIN_OFFSET_PIPELINE_READY;
}

static int ucg_builtin_comp_recv_one_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucg_builtin_memcpy(req->step->recv_buffer, data, length);
    (void) ucg_builtin_comp_step_cb(req, NULL);
    return 1;
}

static int ucg_builtin_comp_recv_one_then_send_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucg_builtin_memcpy(req->step->recv_buffer, data, length);
    (void) ucg_builtin_step_execute(req, NULL);
    return 1;
}

static int ucg_builtin_comp_barrier_recv_one_then_send_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == 0);
    ucg_collective_release_barrier(req->op->super.plan->group);
    (void) ucg_builtin_step_execute(req, NULL);
    return 1;
}

static int ucg_builtin_comp_recv_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_memcpy(req->step->recv_buffer + offset, data, length);
    return ucg_builtin_comp_step_check_cb(req);
}

static int ucg_builtin_comp_recv_many_then_send_pipe_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_memcpy(req->step->recv_buffer + offset, data, length);
    /* if num_fragments arrived - start sending! */
    return ucg_builtin_comp_send_check_frag_cb(req, offset);
}

static int ucg_builtin_comp_recv1_many_then_send_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_memcpy(req->step->recv_buffer + offset, data, length);
    return ucg_builtin_comp_send_check_cb(req,
            (req->step->phase->ep_cnt - 1) * req->step->fragments);
}

static int ucg_builtin_comp_recv_many_then_send1_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_memcpy(req->step->recv_buffer + offset, data, length);
    return ucg_builtin_comp_send_check_cb(req, req->step->fragments);
}

static int ucg_builtin_comp_recv1_many_then_send_non_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_memcpy(req->step->recv_buffer + offset, data, length);
    return ucg_builtin_comp_send_check_cb(req, 0);
}

static int ucg_builtin_comp_recv_many_then_send1_non_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_memcpy(req->step->recv_buffer + offset, data, length);
    return ucg_builtin_comp_send_check_cb(req, 0);
}

UCS_PROFILE_FUNC(int, ucg_builtin_comp_reduce_one_cb, (req, offset, data, length),
                 ucg_builtin_request_t *req, uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == req->step->buffer_length);
    ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    (void) ucg_builtin_comp_step_cb(req, NULL);
    return 1;
}

static int ucg_builtin_comp_reduce_one_then_send_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == req->step->buffer_length);
    ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    (void) ucg_builtin_step_execute(req, NULL);
    return 1;
}

static int ucg_builtin_comp_reduce_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    return ucg_builtin_comp_step_check_cb(req);
}

static int ucg_builtin_comp_reduce_padded_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    size_t stride = ucs_align_up(length, UCS_SYS_CACHE_LINE_SIZE);
    data = (void*)ucs_align_up((uintptr_t)data, UCS_SYS_CACHE_LINE_SIZE);
    for (index = 0; index < count; index++, data = (uint8_t*)data + stride) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_step_check_cb(req);
}

static int ucg_builtin_comp_reduce_packed_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    for (index = 0; index < count; index++, data = (uint8_t*)data + length) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_step_check_cb(req);
}

static int ucg_builtin_comp_reduce_many_then_send_pipe_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    return ucg_builtin_comp_send_check_frag_cb(req, offset);
}

static int ucg_builtin_comp_reduce_padded_many_then_send_pipe_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    size_t stride = ucs_align_up(length, UCS_SYS_CACHE_LINE_SIZE);
    data = (void*)ucs_align_up((uintptr_t)data, UCS_SYS_CACHE_LINE_SIZE);
    for (index = 0; index < count; index++, data = (uint8_t*)data + stride) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_send_check_frag_cb(req, offset);
}

static int ucg_builtin_comp_reduce_packed_many_then_send_pipe_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    for (index = 0; index < count; index++, data = (uint8_t*)data + length) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_send_check_frag_cb(req, offset);
}

static int ucg_builtin_comp_reduce_many_then_send_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    return ucg_builtin_comp_send_check_cb(req, req->step->fragments);
}

static int ucg_builtin_comp_reduce_padded_many_then_send_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    size_t stride = ucs_align_up(length, UCS_SYS_CACHE_LINE_SIZE);
    data = (void*)ucs_align_up((uintptr_t)data, UCS_SYS_CACHE_LINE_SIZE);
    for (index = 0; index < count; index++, data = (uint8_t*)data + stride) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_send_check_cb(req, req->step->fragments);
}

static int ucg_builtin_comp_reduce_packed_many_then_send_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    for (index = 0; index < count; index++, data = (uint8_t*)data + length) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_send_check_cb(req, req->step->fragments);
}

static int ucg_builtin_comp_reduce_many_then_send_non_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    return ucg_builtin_comp_send_check_cb(req, 0);
}

static int ucg_builtin_comp_reduce_padded_many_then_send_non_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    size_t stride = ucs_align_up(length, UCS_SYS_CACHE_LINE_SIZE);
    data = (void*)ucs_align_up((uintptr_t)data, UCS_SYS_CACHE_LINE_SIZE);
    for (index = 0; index < count; index++, data = (uint8_t*)data + stride) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_send_check_cb(req, 0);
}

static int ucg_builtin_comp_reduce_packed_many_then_send_non_zcopy_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    unsigned index, count = req->step->batch_cnt;
    for (index = 0; index < count; index++, data = (uint8_t*)data + length) {
        ucg_builtin_mpi_reduce_req(req, offset, data, length, &req->op->super.params);
    }
    return ucg_builtin_comp_send_check_cb(req, 0);
}

static int ucg_builtin_comp_wait_one_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == 0);
    (void) ucg_builtin_comp_step_cb(req, NULL);
    return 1;
}

static int ucg_builtin_comp_wait_one_then_send_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == 0);
    (void) ucg_builtin_step_execute(req, NULL);
    return 1;
}

static int ucg_builtin_comp_wait_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == 0);
    return ucg_builtin_comp_step_check_cb(req);
}

static int ucg_builtin_comp_wait_many_then_send_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == 0);
    return ucg_builtin_comp_send_check_cb(req, 1);
}

static int ucg_builtin_comp_barrier_one_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == 0);
    (void) ucg_builtin_comp_step_cb(req, NULL);
    ucg_collective_release_barrier(req->op->super.plan->group);
    return 1;
}

static inline int ucg_builtin_comp_barrier_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    ucs_assert(offset == 0);
    ucs_assert(length == 0);
    UCG_IF_PENDING_REACHED(req, 0) {
        (void) ucg_builtin_comp_step_cb(req, NULL);
        ucg_collective_release_barrier(req->op->super.plan->group);
        return 1;
    }
    return 0;
}

static int ucg_builtin_comp_barrier_padded_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    req->pending -= req->step->batch_cnt;
    return ucg_builtin_comp_barrier_many_cb(req, offset, data, length);
}

static int ucg_builtin_comp_barrier_packed_many_cb(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length)
{
    req->pending -= req->step->batch_cnt;
    return ucg_builtin_comp_barrier_many_cb(req, offset, data, length);
}

ucs_status_t ucg_builtin_step_select_callbacks(ucg_builtin_plan_phase_t *phase,
        ucg_builtin_comp_recv_cb_t *recv_cb, int flags, size_t align_incast,
        int nonzero_length)
{
    int is_pipelined  = flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
    int is_fragmented = flags & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;
    int is_single_ep  = flags & UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT;
    int is_last_step  = flags & UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP;
    int is_zcopy      = flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;
    int is_single_msg = ((is_single_ep) && (!is_fragmented));

    switch (phase->method) {
    case UCG_PLAN_METHOD_BCAST_WAYPOINT:
    case UCG_PLAN_METHOD_SCATTER_WAYPOINT:
        if (!is_fragmented) {
            if (ucs_likely(nonzero_length)) {
                *recv_cb = ucg_builtin_comp_recv_one_then_send_cb;
            } else {
                *recv_cb = ucg_builtin_comp_barrier_recv_one_then_send_cb;
                ucs_assert(is_last_step);
            }
        } else if (is_pipelined) {
            *recv_cb = ucg_builtin_comp_recv_many_then_send_pipe_cb;
        } else if (is_zcopy) {
            *recv_cb = ucg_builtin_comp_recv1_many_then_send_zcopy_cb;
        } else {
            *recv_cb = ucg_builtin_comp_recv1_many_then_send_non_zcopy_cb;
        }
        break;

    case UCG_PLAN_METHOD_GATHER_WAYPOINT:
        if (!is_fragmented) {
            *recv_cb = ucg_builtin_comp_recv_one_then_send_cb;
        } else if (is_pipelined) {
            *recv_cb = ucg_builtin_comp_recv_many_then_send_pipe_cb;
        } else if (is_zcopy) {
            *recv_cb = ucg_builtin_comp_recv_many_then_send1_zcopy_cb;
        } else {
            *recv_cb = ucg_builtin_comp_recv_many_then_send1_non_zcopy_cb;
        }
        break;

    case UCG_PLAN_METHOD_RECV_TERMINAL:
        /* Special case for barriers, requiring a release */
        if (ucs_unlikely((!nonzero_length) && (is_last_step))) {
            *recv_cb = is_single_ep ? ucg_builtin_comp_barrier_one_cb :
                                      ucg_builtin_comp_barrier_many_cb;
            break;
        }
        /* No break */
    case UCG_PLAN_METHOD_SEND_TERMINAL:
    case UCG_PLAN_METHOD_SCATTER_TERMINAL:
    case UCG_PLAN_METHOD_ALLTOALL_BRUCK:
    case UCG_PLAN_METHOD_ALLGATHER_BRUCK:
    case UCG_PLAN_METHOD_PAIRWISE:
    case UCG_PLAN_METHOD_NEIGHBOR:
        *recv_cb = is_single_msg ? ucg_builtin_comp_recv_one_cb :
                                   ucg_builtin_comp_recv_many_cb;
        break;

    case UCG_PLAN_METHOD_REDUCE_WAYPOINT:
        is_single_msg |= ((phase->ep_cnt == 2) && (!is_fragmented));
        if (is_single_msg) {
            *recv_cb = nonzero_length ? ucg_builtin_comp_reduce_one_then_send_cb :
                                        ucg_builtin_comp_wait_one_then_send_cb;
        } else if (!nonzero_length) {
            *recv_cb = ucg_builtin_comp_wait_many_then_send_cb;
        } else if (is_pipelined) {
            switch (align_incast) {
            case NO_INCAST_SUPPORT:
                *recv_cb = ucg_builtin_comp_reduce_many_then_send_pipe_cb;
                break;
            case UCS_SYS_CACHE_LINE_SIZE:
                *recv_cb = ucg_builtin_comp_reduce_padded_many_then_send_pipe_cb;
                break;
            case 0:
                *recv_cb = ucg_builtin_comp_reduce_packed_many_then_send_pipe_cb;
                break;
            default:
                ucs_error("Interface with an unsupported element alignment");
                return UCS_ERR_UNSUPPORTED;
            }
        } else if (is_zcopy) {
            switch (align_incast) {
            case NO_INCAST_SUPPORT:
                *recv_cb = ucg_builtin_comp_reduce_many_then_send_zcopy_cb;
                break;
            case UCS_SYS_CACHE_LINE_SIZE:
                *recv_cb = ucg_builtin_comp_reduce_padded_many_then_send_zcopy_cb;
                break;
            case 0:
                *recv_cb = ucg_builtin_comp_reduce_packed_many_then_send_zcopy_cb;
                break;
            default:
                ucs_error("Interface with an unsupported element alignment");
                return UCS_ERR_UNSUPPORTED;
            }
        } else {
            switch (align_incast) {
            case NO_INCAST_SUPPORT:
                *recv_cb = ucg_builtin_comp_reduce_many_then_send_non_zcopy_cb;
                break;
            case UCS_SYS_CACHE_LINE_SIZE:
                *recv_cb = ucg_builtin_comp_reduce_padded_many_then_send_non_zcopy_cb;
                break;
            case 0:
                *recv_cb = ucg_builtin_comp_reduce_packed_many_then_send_non_zcopy_cb;
                break;
            default:
                ucs_error("Interface with an unsupported element alignment");
                return UCS_ERR_UNSUPPORTED;
            }
        }
        break;

    case UCG_PLAN_METHOD_REDUCE_TERMINAL:
        /**
         * Special barrier case: if the root does FANIN + FANOUT
         * (instead of RD) the FANOUT will not trigger the recv_cb, but
         * actually by the time the FAININ ended - the barrier can be
         * released (since everyone has arrived).
         */
        switch (align_incast) {
        case NO_INCAST_SUPPORT:
            *recv_cb = nonzero_length ?
                    (is_single_msg ? ucg_builtin_comp_reduce_one_cb :
                                     ucg_builtin_comp_reduce_many_cb) :
                    (is_single_msg ? ucg_builtin_comp_barrier_one_cb :
                                     ucg_builtin_comp_barrier_many_cb);
            break;

        case UCS_SYS_CACHE_LINE_SIZE:
            *recv_cb = nonzero_length ?
                    (is_single_msg ? ucg_builtin_comp_reduce_one_cb :
                                     ucg_builtin_comp_reduce_padded_many_cb) :
                    (is_single_msg ? ucg_builtin_comp_barrier_one_cb :
                                     ucg_builtin_comp_barrier_padded_many_cb);
            break;

        case 0:
            *recv_cb = nonzero_length ?
                    (is_single_msg ? ucg_builtin_comp_reduce_one_cb :
                                     ucg_builtin_comp_reduce_packed_many_cb) :
                    (is_single_msg ? ucg_builtin_comp_barrier_one_cb :
                                     ucg_builtin_comp_barrier_packed_many_cb);
            break;

        default:
            ucs_error("Interface with an unsupported element alignment");
            return UCS_ERR_UNSUPPORTED;
        }
        break;

    case UCG_PLAN_METHOD_REDUCE_RECURSIVE:
        /* Special case for barriers, requiring a release */
        if (ucs_unlikely((!nonzero_length) && (is_last_step))) {
            *recv_cb = is_single_ep ? ucg_builtin_comp_barrier_one_cb :
                                      ucg_builtin_comp_barrier_many_cb;
            break;
        }

        if (is_single_msg && !is_zcopy) { /* zcopy also needs pending == 0... */
            *recv_cb = nonzero_length ? ucg_builtin_comp_reduce_one_cb :
                                        ucg_builtin_comp_wait_one_cb;
        } else {
            *recv_cb = nonzero_length ? ucg_builtin_comp_reduce_many_cb :
                                        ucg_builtin_comp_wait_many_cb;
        }
        break;

        *recv_cb = ucg_builtin_comp_recv_one_cb;
        break;
    }

    return UCS_OK;
}

static void ucg_builtin_step_am_zcopy_comp_step_check_cb(uct_completion_t *self,
                                                         ucs_status_t status)
{

    ucg_builtin_zcomp_t *zcomp = ucs_container_of(self, ucg_builtin_zcomp_t, comp);
    ucg_builtin_request_t *req = zcomp->req;
    zcomp->comp.count          = 1;

    if (ucs_unlikely(status != UCS_OK)) {
        ucg_builtin_comp_last_step_cb(req, status);
    } else {
        (void) ucg_builtin_comp_step_check_cb(req);
    }
}

ucs_status_t ucg_builtin_step_zcopy_prep(ucg_builtin_op_step_t *step)
{
    /* Allocate callback context for zero-copy sends */
    uint32_t zcomp_cnt         = step->phase->ep_cnt * step->fragments;
    step->zcopy.memh           = NULL; /* - in case the allocation fails... */
    ucg_builtin_zcomp_t *zcomp =
             step->zcopy.zcomp = (ucg_builtin_zcomp_t*)UCS_ALLOC_CHECK(zcomp_cnt *
                     sizeof(*zcomp), "ucg_zcopy_completion");

    /* Initialize all the zero-copy send completion structures */
    while (zcomp_cnt--) {
        zcomp->comp.func  = ucg_builtin_step_am_zcopy_comp_step_check_cb;
        zcomp->comp.count = 1;
        zcomp++;
    }

    /* Register the buffer, creating a memory handle used in zero-copy sends */
    int8_t *sbuf = (step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_FROM_RECV_BUF) ?
            step->recv_buffer : step->send_buffer;
    ucs_status_t status = uct_md_mem_reg(step->uct_md, sbuf,
            step->buffer_length, UCT_MD_MEM_ACCESS_ALL, &step->zcopy.memh);
    if (status != UCS_OK) {
        ucs_free(zcomp);
        return status;
    }
    return UCS_OK;
}

static ucs_status_t ucg_builtin_optimize_bcopy_to_zcopy(ucg_builtin_op_t *op)
{
    /* This function was called because we want to "upgrade" a bcopy-send to
     * zcopy, by way of memory registration (costly, but hopefully worth it) */
    ucs_status_t status;
    ucg_builtin_op_step_t *step;
    ucg_step_idx_t step_idx = 0;
    do {
        step = &op->steps[step_idx++];
        if ((step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY) &&
            (step->phase->md_attr->cap.max_reg > step->buffer_length)) {
            status = ucg_builtin_step_zcopy_prep(step);
            if (status != UCS_OK) {
                goto bcopy_to_zcopy_cleanup;
            }

            step->flags &= ~UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY;
            step->flags |=  UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;


            if (step->recv_cb == ucg_builtin_comp_reduce_one_cb) {
                step->recv_cb = ucg_builtin_comp_reduce_many_cb;
                /* So that recursive doubling doesn't return right after sending */
            } else if (step->recv_cb == ucg_builtin_comp_recv1_many_then_send_non_zcopy_cb) {
                step->recv_cb = ucg_builtin_comp_recv1_many_then_send_zcopy_cb;
            } else if (step->recv_cb == ucg_builtin_comp_recv_many_then_send1_non_zcopy_cb) {
                step->recv_cb = ucg_builtin_comp_recv_many_then_send1_zcopy_cb;
            } else if (step->recv_cb == ucg_builtin_comp_reduce_many_then_send_non_zcopy_cb) {
                step->recv_cb = ucg_builtin_comp_reduce_many_then_send_zcopy_cb;
            }
        }
    } while (!(step->flags & UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP));

    return UCS_OK;

bcopy_to_zcopy_cleanup:
    while (step_idx) {
        if (step->zcopy.memh) {

        }
    }
    return status;
}

static ucs_status_t ucg_builtin_no_optimization(ucg_builtin_op_t *op)
{
    return UCS_OK;
}

/*
 * While some buffers are large enough to be registered (as in memory
 * registration) upon first send, others are "buffer-copied" (BCOPY) - unless
 * it is used repeatedly. If an operation is used this many times - its buffers
 * will also be registered, turning it into a zero-copy (ZCOPY) send henceforth.
 */
ucs_status_t ucg_builtin_op_consider_optimization(ucg_builtin_op_t *op,
        ucg_builtin_config_t *config)
{
    ucg_builtin_op_step_t *step;
    ucg_step_idx_t step_idx = 0;
    do {
        step = &op->steps[step_idx++];
        if ((step->flags & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY) &&
            (step->phase->md_attr->cap.max_reg > step->buffer_length)) {
            op->optm_cb = ucg_builtin_optimize_bcopy_to_zcopy;
            op->opt_cnt = config->mem_reg_opt_cnt;
            return UCS_OK;
        }
    } while (!(step->flags & UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP));

    /* Note: This function will be called... after opt_cnt wrap-around */
    op->optm_cb = ucg_builtin_no_optimization;
    op->opt_cnt = 0;
    return UCS_OK;
}

#ifdef TODO
static inline void reduce_atomic_for_packet(void *dst, const void *src, size_t len)
{
    size_t length_iter;
    switch (len) {
    case sizeof(uint8_t):
        /* Atomic addition - 8 bits */
        ucs_atomic_add8(dst, src);
    break;

    case sizeof(uint16_t):
        /* Atomic addition - 16 bits */
        ucs_atomic_add16(dst, src);
    break;

    case sizeof(uint32_t):
        /* Atomic addition - 32 bits */
        ucs_atomic_add32(dst, src);
    break;

    default:
        /* Atomic addition - multiples of 64 bits */
        ucs_assert(len % sizeof(uint64_t) == 0);
        for (length_iter = len; length_iter > 0; len -= sizeof(uint64_t)) {
            ucs_atomic_add64(dst, src);
            dst += sizeof(uint64_t);
            src += sizeof(uint64_t);
        }
    }
}

int ucg_builtin_atomic_reduce_full(ucg_builtin_request_t *req,
        uint64_t offset, void *data, size_t length, ucs_spinlock_t *lock)
{

    if (ucs_unlikely(req->op->super.params->recv.op_ext == MPI_ADD)) {
        reduce_atomic_for_packet();
        return;
    }
#else
int ucg_builtin_atomic_reduce_full(ucg_builtin_request_t *req,
        uint64_t offset, void *src, void *dst, size_t length, ucs_spinlock_t *lock)
{
#endif
    ucg_collective_params_t *params = &req->op->super.params;
    ucs_assert(lock != NULL);

    ucs_spin_lock(lock);
    ucg_builtin_mpi_reduce(params->recv.op_ext, src, dst, params->send.count,
            params->send.dt_ext);
    ucs_spin_unlock(lock);

    return length; // TODO: make ucg_builtin_mpi_reduce return the actual size
}

int ucg_builtin_atomic_reduce_partial(ucg_builtin_request_t *req,
        uint64_t offset, void *src, void *dst, size_t length, ucs_spinlock_t *lock)
{
    ucg_collective_params_t *params = &req->op->super.params;
    ucs_assert(lock != NULL);

    /* Check for barriers */
    if (ucs_unlikely(params->send.dt_len == 0)) {
        return 0;
    }

    ucs_spin_lock(lock);
    ucg_builtin_mpi_reduce(params->recv.op_ext, src, dst,
            length / params->send.dt_len, params->send.dt_ext);
    ucs_spin_unlock(lock);

    return length;
}

END_C_DECLS
