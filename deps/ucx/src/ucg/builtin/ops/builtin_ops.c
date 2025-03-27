/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <string.h>

#include <ucs/datastruct/queue.h>
#include <ucs/datastruct/list.h>
#include <ucs/profile/profile.h>
#include <ucs/debug/memtrack.h>
#include <ucs/debug/assert.h>
#include <ucp/dt/dt_contig.h>

#include "builtin_cb.inl"

#ifndef MPI_IN_PLACE
#define MPI_IN_PLACE ((void*)0x1)
#endif

/******************************************************************************
 *                                                                            *
 *                            Operation Execution                             *
 *                                                                            *
 ******************************************************************************/

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_dummy_send(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) == 0);
    return UCS_OK;
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_builtin_step_am_short_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT);
    return step->uct_iface->ops.ep_am_short(ep, step->am_id,
            step->am_header.header, step->send_buffer, step->buffer_length);
}

static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_builtin_step_am_short_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send) {
    ucs_status_t status;
    unsigned am_id               = step->am_id;
    ucg_offset_t frag_size       = step->fragment_length;
    ucg_offset_t iter_offset     = step->iter_offset;
    int8_t *buffer_iter          = step->send_buffer + iter_offset;
    ucg_offset_t length_left     = step->buffer_length - iter_offset;
    ucg_builtin_header_t am_iter = { .header = step->am_header.header };
    am_iter.remote_offset       += iter_offset;
    ucs_status_t (*ep_am_short)(uct_ep_h, uint8_t, uint64_t, const void*, unsigned) =
            step->uct_iface->ops.ep_am_short;

    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT);

    /* send every fragment but the last */
    if (ucs_likely(length_left > frag_size)) {
        do {
            status = ep_am_short(ep, am_id, am_iter.header, buffer_iter, frag_size);

            if (is_single_send) {
                return status;
            }

            buffer_iter           += frag_size;
            am_iter.remote_offset += frag_size;
            length_left           -= frag_size;
        } while ((status == UCS_OK) && (length_left > frag_size));

        /* send last fragment of the message */
        if (ucs_unlikely(status != UCS_OK)) {
            /* assuming UCS_ERR_NO_RESOURCE, restore the state for re-entry */
            step->iter_offset = buffer_iter - frag_size - step->send_buffer;
            return status;
        }
    }

    status = ep_am_short(ep, am_id, am_iter.header, buffer_iter, length_left);
    step->iter_offset = (status == UCS_OK) ? 0 : buffer_iter - step->send_buffer;
    return status;
}

static size_t ucg_builtin_step_am_bcopy_single_frag_packer(void *dest, void *arg)
{
    ucg_builtin_op_step_t *step      = (ucg_builtin_op_step_t*)arg;
    ucg_builtin_header_t *header_ptr = (ucg_builtin_header_t*)dest;
    header_ptr->header               = step->am_header.header;

    memcpy(header_ptr + 1, step->send_buffer, step->buffer_length);
    return sizeof(*header_ptr) + step->buffer_length;
}

static size_t ucg_builtin_step_am_bcopy_full_frag_packer(void *dest, void *arg)
{
    ucg_builtin_op_step_t *step      = (ucg_builtin_op_step_t*)arg;
    ucg_builtin_header_t *header_ptr = (ucg_builtin_header_t*)dest;
    header_ptr->header               = step->am_header.header;

    memcpy(header_ptr + 1, step->send_buffer + step->iter_offset, step->fragment_length);
    return sizeof(*header_ptr) + step->fragment_length;
}

static size_t ucg_builtin_step_am_bcopy_partial_frag_packer(void *dest, void *arg)
{
    ucg_builtin_op_step_t *step      = (ucg_builtin_op_step_t*)arg;
    ucg_offset_t last_frag_length    = step->buffer_length - step->iter_offset;
    ucg_builtin_header_t *header_ptr = (ucg_builtin_header_t*)dest;
    header_ptr->header               = step->am_header.header;

    memcpy(header_ptr + 1, step->send_buffer + step->iter_offset, last_frag_length);
    return sizeof(*header_ptr) + last_frag_length;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_bcopy_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY);

    /* send active message to remote endpoint */
    ssize_t len = step->uct_iface->ops.ep_am_bcopy(ep, step->am_id,
            ucg_builtin_step_am_bcopy_single_frag_packer, step, 0);
    return (ucs_unlikely(len < 0)) ? (ucs_status_t)len : UCS_OK;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_bcopy_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send) {
    ssize_t len;
    unsigned am_id           = step->am_id;
    ucg_offset_t iter_offset = step->iter_offset;
    ucg_offset_t length_left = step->buffer_length - iter_offset;
    ucg_offset_t frag_size   = step->fragment_length;
    ssize_t (*ep_am_bcopy)(uct_ep_h, uint8_t, uct_pack_callback_t, void*, unsigned) =
            step->uct_iface->ops.ep_am_bcopy;

    ucs_assert((step->flags & (UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                               UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY)) ==
                                       UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY);

    /* check if this is not, by any chance, the last fragment */
    if (ucs_likely(length_left > frag_size)) {
        /* send every fragment but the last */
        do {
            len = ep_am_bcopy(ep, am_id, ucg_builtin_step_am_bcopy_full_frag_packer, step, 0);

            if (is_single_send) {
                return ucs_unlikely(len < 0) ? (ucs_status_t)len : UCS_OK;
            }

            step->iter_offset             += frag_size;
            step->am_header.remote_offset += frag_size;
            length_left                   -= frag_size;
        } while ((len >= 0) && (length_left > frag_size));

        if (ucs_unlikely(len < 0)) {
            step->iter_offset -= frag_size;
            return (ucs_status_t)len;
        }
    }

    /* Send last fragment of the message */
    len = ep_am_bcopy(ep, am_id, ucg_builtin_step_am_bcopy_partial_frag_packer, step, 0);
    if (ucs_unlikely(len < 0)) {
        return (ucs_status_t)len;
    }

    step->am_header.remote_offset = 0;
    step->iter_offset = 0;
    return UCS_OK;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_zcopy_one(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    uct_iov_t iov = {
            .buffer = step->send_buffer,
            .length = step->buffer_length,
            .memh   = step->zcopy.memh,
            .stride = 0,
            .count  = 1
    };

    ucg_builtin_zcomp_t *zcomp = &step->zcopy.zcomp[step->iter_ep];
    zcomp->req = req;

    ucs_status_t status = step->uct_iface->ops.ep_am_zcopy(ep, step->am_id,
            &step->am_header, sizeof(step->am_header), &iov, 1, 0, &zcomp->comp);
    return ucs_unlikely(status != UCS_INPROGRESS) ? status : UCS_OK;
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_am_zcopy_max(ucg_builtin_request_t *req,
        ucg_builtin_op_step_t *step, uct_ep_h ep, int is_single_send)
{
    ucs_status_t status;
    unsigned am_id             = step->am_id;
    ucg_offset_t iter_offset   = step->iter_offset;
    ucg_offset_t length_left   = step->buffer_length - iter_offset;
    ucg_offset_t frag_size     = step->fragment_length;
    unsigned zcomp_index       = step->iter_ep * step->fragments +
                                 step->iter_offset / step->fragment_length;
    ucg_builtin_zcomp_t *zcomp = &step->zcopy.zcomp[zcomp_index];
    ucs_status_t (*ep_am_zcopy)(uct_ep_h, uint8_t, const void*, unsigned,
            const uct_iov_t*, size_t, unsigned, uct_completion_t*) =
                    step->uct_iface->ops.ep_am_zcopy;

    uct_iov_t iov = {
            .buffer = step->send_buffer + step->iter_offset,
            .length = frag_size,
            .memh   = step->zcopy.memh,
            .stride = 0,
            .count  = 1
    };

    /* check if this is not, by any chance, the last fragment */
    if (ucs_likely(length_left > frag_size)) {
        /* send every fragment but the last */
        do {
            status = ep_am_zcopy(ep, am_id, &step->am_header,
                                 sizeof(step->am_header), &iov,
                                 1, 0, &zcomp->comp);
            (zcomp++)->req = req;

            if (is_single_send) {
                return status;
            }

            length_left -= frag_size;
            iov.buffer   = (void*)((int8_t*)iov.buffer + frag_size);
            step->am_header.remote_offset += iter_offset;
        } while ((status == UCS_INPROGRESS) && (length_left > frag_size));

        if (ucs_unlikely(status != UCS_INPROGRESS)) {
            step->iter_offset = (int8_t*)iov.buffer - step->send_buffer - frag_size;
            return status;
        }
    }

    /* Send last fragment of the message */
    zcomp->req = req;
    iov.length = length_left;
    status     = ep_am_zcopy(ep, am_id, &step->am_header,
                             sizeof(step->am_header),
                             &iov, 1, 0, &zcomp->comp);
    if (ucs_unlikely(status != UCS_INPROGRESS)) {
        step->iter_offset = (int8_t*)iov.buffer - step->send_buffer;
        return status;
    }

    step->am_header.remote_offset = 0;
    step->iter_offset = 0;
    return UCS_OK;
}

/*
 * Below is a set of macros, generating most bit-field combinations of
 * step->flags in the switch-case inside @ref ucg_builtin_step_execute() .
 */

#define case_send_full(/* General parameters */                                \
                       req, ureq, step, phase,                                 \
                       /* Receive-related indicators, for non-send-only steps*/\
                       _is_recv, _is_rs1, _is_r1s, _is_pipelined,              \
                       /* Step-completion-related indicators */                \
                       _is_first, _is_last, _is_one_ep,                        \
                       /* Send-related  parameters */                          \
                       _is_scatter, _send_flag, _send_func)                    \
   case ((_is_scatter   ? UCG_BUILTIN_OP_STEP_FLAG_LENGTH_PER_REQUEST : 0) |   \
         (_is_one_ep    ? UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT    : 0) |   \
         (_is_last      ? UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP          : 0) |   \
         (_is_first     ? UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP         : 0) |   \
         (_is_pipelined ? UCG_BUILTIN_OP_STEP_FLAG_PIPELINED          : 0) |   \
         (_is_r1s       ? UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND  : 0) |   \
         (_is_rs1       ? UCG_BUILTIN_OP_STEP_FLAG_RECV_BEFORE_SEND1  : 0) |   \
         (_is_recv      ? UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND    : 0) |   \
         _send_flag):                                                          \
                                                                               \
        is_zcopy = (_send_flag) & UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;      \
        if ((_is_rs1 || _is_r1s) && (step->iter_ep == 0)) {                    \
            uint32_t new_cnt = step->iter_ep = _is_r1s ? 1 : phase->ep_cnt - 1;\
            ucs_assert(new_cnt > 1);                                           \
            if (_is_pipelined) {                                               \
                memset(step->fragment_pending, new_cnt, step->fragments);      \
            }                                                                  \
            if (!is_zcopy) {                                                   \
                req->pending = new_cnt * step->fragments;                      \
            }                                                                  \
            break; /* Beyond the switch-case we fall-back to receiving */      \
        }                                                                      \
                                                                               \
        if (_is_recv && is_zcopy) {                                            \
            /* Both zcopy callbacks and incoming messages use pending, so ...*/\
            req->pending = 2 * step->fragments * phase->ep_cnt;                \
        }                                                                      \
                                                                               \
        /* Perform one or many send operations, unless an error occurs */      \
        if (_is_one_ep) {                                                      \
            ucs_assert(!_is_pipelined); /* makes no sense in single-ep case */ \
            status = _send_func (req, step, phase->single_ep, 0);              \
            if (ucs_unlikely(UCS_STATUS_IS_ERR(status))) {                     \
                goto step_execute_error;                                       \
            }                                                                  \
        } else {                                                               \
            uct_ep_h *ep_iter, *ep_last;                                       \
            ep_iter = ep_last = phase->multi_eps;                              \
            ep_iter += step->iter_ep;                                          \
            ep_last += phase->ep_cnt;                                          \
            do {                                                               \
                status = _send_func (req, step, *ep_iter, _is_pipelined);      \
                if (ucs_unlikely(UCS_STATUS_IS_ERR(status))) {                 \
                    /* Store the pointer, e.g. for UCS_ERR_NO_RESOURCE */      \
                    step->iter_ep = ep_iter - phase->multi_eps;                \
                    goto step_execute_error;                                   \
                }                                                              \
                                                                               \
                if (_is_scatter) {                                             \
                    step->send_buffer += step->buffer_length;                  \
                }                                                              \
            } while (++ep_iter < ep_last);                                     \
                                                                               \
            if (_is_scatter) { /* restore after a temporary pointer change */  \
                step->send_buffer -= phase->ep_cnt * step->buffer_length;      \
            }                                                                  \
                                                                               \
            if (_is_pipelined) {                                               \
                /* Reset the iterator for the next pipelined incoming packet */\
                step->iter_ep = _is_r1s ? 1 : phase->ep_cnt - 1;               \
                ucs_assert(_is_r1s + _is_rs1 > 0);                             \
                                                                               \
                /* Check if this invocation is a result of a resend attempt */ \
                unsigned idx = step->iter_offset / step->fragment_length;      \
                if (ucs_unlikely(step->fragment_pending[idx] ==                \
                        UCG_BUILTIN_FRAG_PENDING)) {                           \
                    step->fragment_pending[idx] = 0;                           \
                                                                               \
                    /* Look for other packets in need of resending */          \
                    for (idx = 0; idx < step->fragments; idx++) {              \
                        if (step->fragment_pending[idx] ==                     \
                                UCG_BUILTIN_FRAG_PENDING) {                    \
                            /* Found such packets - mark for next resend */    \
                            step->iter_offset = idx * step->fragment_length;   \
                            status            = UCS_ERR_NO_RESOURCE;           \
                            goto step_execute_error;                           \
                        }                                                      \
                    }                                                          \
                } else {                                                       \
                    ucs_assert(step->fragment_pending[idx] == 0);              \
                }                                                              \
                step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_READY;         \
            } else {                                                           \
                step->iter_ep = 0; /* Reset the per-step endpoint iterator */  \
                ucs_assert(step->iter_offset == 0);                            \
            }                                                                  \
        }                                                                      \
                                                                               \
        /* Potential completions (the operation may have finished by now) */   \
        if ((!_is_recv && !is_zcopy) || (req->pending == 0)) {                 \
            /* Nothing else to do - complete this step */                      \
            if (_is_last) {                                                    \
                if (!ureq) {                                                   \
                    ucg_builtin_comp_last_step_cb(req, UCS_OK);                \
                }                                                              \
                return UCS_OK;                                                 \
            } else {                                                           \
                return ucg_builtin_comp_step_cb(req, ureq);                    \
            }                                                                  \
        }                                                                      \
        break;

#define case_send_rs1(r, u, s, p,    _is_rs1, _is_r1s, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
       case_send_full(r, u, s, p, 0, _is_rs1, _is_r1s, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
       case_send_full(r, u, s, p, 1, _is_rs1, _is_r1s, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func)

#define case_send_r1s(r, u, s, p,    _is_r1s, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
        case_send_rs1(r, u, s, p, 0, _is_r1s, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
        case_send_rs1(r, u, s, p, 1, _is_r1s, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func)

#define case_send_ppld(r, u, s, p,    _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
         case_send_r1s(r, u, s, p, 0, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
         case_send_r1s(r, u, s, p, 1, _is_ppld, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func)

#define case_send_first(r, u, s, p,    _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
         case_send_ppld(r, u, s, p, 0, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func) \
         case_send_ppld(r, u, s, p, 1, _is_first, _is_last, _is_one_ep, _is_scatter, _send_flag, _send_func)

#define  case_send_last(r, u, s, p,    _is_last,_is_one_ep, _is_scatter, _send_flag, _send_func) \
        case_send_first(r, u, s, p, 0, _is_last,_is_one_ep, _is_scatter, _send_flag, _send_func) \
        case_send_first(r, u, s, p, 1, _is_last,_is_one_ep, _is_scatter, _send_flag, _send_func) \

#define case_send_one_ep(r, u, s, p,    _is_one_ep, _is_scatter, _send_flag, _send_func) \
          case_send_last(r, u, s, p, 0, _is_one_ep, _is_scatter, _send_flag, _send_func) \
          case_send_last(r, u, s, p, 1, _is_one_ep, _is_scatter, _send_flag, _send_func)

#define case_send_scatter(r, u, s, p,    _is_scatter, _send_flag, _send_func) \
         case_send_one_ep(r, u, s, p, 0, _is_scatter, _send_flag, _send_func) \
         case_send_one_ep(r, u, s, p, 1, _is_scatter, _send_flag, _send_func)

#define         case_send(r, u, s, p,    _send_flag, _send_func) \
        case_send_scatter(r, u, s, p, 0, _send_flag, _send_func) \
        case_send_scatter(r, u, s, p, 1, _send_flag, _send_func)

#define INIT_USER_REQUEST_IF_GIVEN(user_req, req) {                            \
    if (ucs_unlikely(user_req != NULL)) {                                      \
        /* Initialize user's request part (checked for completion) */          \
        if (*user_req) {                                                       \
            req->comp_req = *user_req - 1;                                     \
        } else {                                                               \
            req->comp_req = &req->super;                                       \
            *user_req     = &req->super + 1;                                   \
        }                                                                      \
        req->comp_req->flags = 0;                                              \
        user_req = NULL;                                                       \
    }                                                                          \
}
/*
 * Executing a single step is the heart of the Builtin planner.
 * This function advances to the next step (some invocations negate that...),
 * sends and then recieves according to the instructions of this step.
 * The function returns the status, typically one of the following:
 * > UCS_OK - collective operation (not just this step) has been completed.
 * > UCS_INPROGRESS - sends complete, waiting on some messages to be recieved.
 * > otherwise - an error has occurred.
 *
 * For example, a "complex" case is when the message is fragmented, and requires
 * both recieveing and sending in a single step, like in REDUCE_WAYPOINT. The
 * first call, coming from @ref ucg_builtin_op_trigger() , will enter the first
 * branch ("step_ep" is zero when a new step is starting), will process some
 * potential incoming messages (arriving beforehand) - returning UCS_INPROGRESS.
 * Subsequent calls to "progress()" will handle the rest of the incoming
 * messages for this step, and eventually call this function again from within
 * @ref ucg_builtin_comp_step_cb() . This call will choose the second branch,
 * the swith-case, which will send the message and
 */
UCS_PROFILE_FUNC(ucs_status_t, ucg_builtin_step_execute, (req, user_req),
                 ucg_builtin_request_t *req, ucg_request_t **user_req)
{
    int is_zcopy;
    uint16_t local_id;
    ucs_status_t status;
    ucg_builtin_op_step_t *step     = req->step;
    ucg_builtin_plan_phase_t *phase = step->phase;
    ucg_builtin_comp_slot_t *slot   = ucs_container_of(req, ucg_builtin_comp_slot_t, req);
    step->am_header.coll_id         = slot->coll_id;
    ucs_assert(slot->step_idx == step->am_header.step_idx);

    /* This step either starts by sending or contains no send operations */
    switch (step->flags) {
    /* Single-send operations (only one fragment passed to UCT) */
    case_send(req, user_req, step, phase, 0, /* for recv-only steps */
              ucg_builtin_step_dummy_send);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT,
              ucg_builtin_step_am_short_one);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY,
              ucg_builtin_step_am_bcopy_one);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY,
              ucg_builtin_step_am_zcopy_one);

    /* Multi-send operations (using iter_ep and iter_offset for context) */
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED,
              ucg_builtin_step_dummy_send);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT,
              ucg_builtin_step_am_short_max);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY,
              ucg_builtin_step_am_bcopy_max);
    case_send(req, user_req, step, phase, UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED|
                                          UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY,
              ucg_builtin_step_am_zcopy_max);

    default:
        ucs_error("Invalid method for a collective operation step.");
        status = UCS_ERR_INVALID_PARAM;
        goto step_execute_error;
    }

    /* Initialize the users' request object, if applicable */
    INIT_USER_REQUEST_IF_GIVEN(user_req, req);
    slot->cb = step->recv_cb;

    /* Check pending incoming messages - invoke the callback on each one */
    if (ucs_likely(ucs_list_is_empty(&slot->msg_head))) {
        return UCS_INPROGRESS;
    }

    /* Look for matches in list of packets waiting on this slot */
    local_id = slot->local_id;
    ucg_builtin_comp_desc_t *desc, *iter;
    ucs_list_for_each_safe(desc, iter, &slot->msg_head, super.tag_list[0]) {
        /*
         * Note: stored message coll_id can be either larger or smaller than
         * the one currently handled - due to coll_id wrap-around.
         */
        ucs_assert((desc->header.coll_id  != slot->coll_id) ||
                   (desc->header.step_idx >= slot->step_idx));

        if (ucs_likely(desc->header.local_id == local_id)) {
            /* Remove the packet (next call may lead here recursively) */
            ucs_list_del(&desc->super.tag_list[0]);

            /* Handle this "waiting" packet, possibly completing the step */
            int is_step_done = step->recv_cb(&slot->req,
                    desc->header.remote_offset, &desc->data[0],
                    desc->super.length);

            /* Dispose of the packet, according to its allocation */
            ucp_recv_desc_release(desc, step->uct_iface);

            /* If the step has indeed completed - check the entire op */
            if (is_step_done) {
                return (req->comp_req->flags & UCP_REQUEST_FLAG_COMPLETED) ?
                        req->comp_req->status : UCS_INPROGRESS;
            }
        }
    }

    return UCS_INPROGRESS;

    /************************** Error flows ***********************************/
step_execute_error:
    if (status == UCS_ERR_NO_RESOURCE) {
        /* Special case: send incomplete - enqueue for resend upon progress */
        INIT_USER_REQUEST_IF_GIVEN(user_req, req);

        if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
            step->fragment_pending[step->iter_offset / step->fragment_length] =
                    UCG_BUILTIN_FRAG_PENDING;
            step->iter_offset = UCG_BUILTIN_OFFSET_PIPELINE_PENDING;
        }

        ucs_list_add_tail(req->op->resend, &req->send_list);
        return UCS_INPROGRESS;
    }

    /* Generic error - reset the collective and mark the request as completed */
    ucg_builtin_comp_last_step_cb(req, status);
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
        if (step->fragment_pending) {
            ucs_free(step->fragment_pending);
        }
    } while (!((step++)->flags & UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP));

    ucs_mpool_put_inline(op);
}

ucs_status_t ucg_builtin_op_trigger(ucg_op_t *op, ucg_coll_id_t coll_id, ucg_request_t **request)
{
    /* Allocate a "slot" for this operation, from a per-group array of slots */
    ucg_builtin_op_t *builtin_op  = (ucg_builtin_op_t*)op;
    ucg_builtin_comp_slot_t *slot = &builtin_op->slots[coll_id % UCG_BUILTIN_MAX_CONCURRENT_OPS];
    slot->coll_id                 = coll_id;
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
    slot->step_idx                     = first_step->am_header.step_idx;

    /* Sanity checks */
    ucs_assert(first_step->iter_offset == 0);
    ucs_assert(first_step->iter_ep == 0);
    ucs_assert(request != NULL);

    /*
     * For some operations, like MPI_Reduce, MPI_Allreduce or MPI_Gather, the
     * local data has to be aggregated along with the incoming data. In others,
     * some shuffle is required once before starting (e.g. Bruck algorithms).
     */
    builtin_op->init_cb(builtin_op);

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

/******************************************************************************
 *                                                                            *
 *                            Operation Creation                              *
 *                                                                            *
 ******************************************************************************/
static UCS_F_ALWAYS_INLINE ucs_status_t
ucg_builtin_step_send_flags(ucg_builtin_op_step_t *step,
                            ucg_builtin_plan_phase_t *phase,
                            const ucg_collective_params_t *params,
                            enum ucg_builtin_op_step_flags *send_flag)
{
    size_t length = step->buffer_length;
    size_t dt_len = params->send.dt_len;
    /*
     * Short messages (e.g. RDMA "inline")
     */
    if (length <= phase->max_short_one) {
        /* Short send - single message */
        *send_flag            = UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT;
        step->fragments       = 1;
    } else if (length <= phase->max_short_max) {
        /* Short send - multiple messages */
        *send_flag = (enum ucg_builtin_op_step_flags)
                     UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_SHORT |
                     UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;

        step->fragment_length = phase->max_short_one -
                               (phase->max_short_one % dt_len);
        step->fragments       = length / step->fragment_length +
                              ((length % step->fragment_length) > 0);

    /*
     * Large messages, if supported (e.g. RDMA "zero-copy")
     */
    } else if ((length >  phase->max_bcopy_max) &&
               (length <= phase->md_attr->cap.max_reg)) {
        if (length < phase->max_zcopy_one) {
            /* ZCopy send - single message */
            *send_flag            = UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY;
            step->fragments       = 1;
        } else {
            /* ZCopy send - single message */
            *send_flag            = (enum ucg_builtin_op_step_flags)
                                    UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_ZCOPY |
                                    UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;
            step->fragment_length = phase->max_zcopy_one -
                                   (phase->max_zcopy_one % dt_len);
            step->fragments       = length / step->fragment_length +
                                  ((length % step->fragment_length) > 0);
        }

        /* memory registration (using the memory registration cache) */
        ucs_status_t status = ucg_builtin_step_zcopy_prep(step);
        if (ucs_unlikely(status != UCS_OK)) {
            return status;
        }

    /*
     * Medium messages
     */
    } else if (length <= phase->max_bcopy_one) {
        /* BCopy send - single message */
        *send_flag = UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY;
        step->fragment_length = step->buffer_length;
        step->fragments       = 1;
    } else {
        /* BCopy send - multiple messages */
        *send_flag            = (enum ucg_builtin_op_step_flags)
                                UCG_BUILTIN_OP_STEP_FLAG_SEND_AM_BCOPY |
                                UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED;
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
    step->phase              = phase;
    step->am_id              = base_am_id;
    step->am_header.group_id = group_id;
    step->am_header.step_idx = phase->step_index;
    step->iter_ep            = 0;
    step->iter_offset        = 0;
    step->fragment_pending   = NULL;
    step->recv_buffer        = (int8_t*)params->recv.buf;
    step->send_buffer        = ((params->send.buf == MPI_IN_PLACE) ||
            !(extra_flags & UCG_BUILTIN_OP_STEP_FLAG_FIRST_STEP)) ?
                    (int8_t*)params->recv.buf : (int8_t*)params->send.buf;
    if (*current_data_buffer) {
        step->send_buffer = *current_data_buffer;
    } else {
        *current_data_buffer = step->recv_buffer;
    }
    ucs_assert(base_am_id < UCP_AM_ID_MAX);

    /* Decide how the messages are sent (regardless of my role) */
    enum ucg_builtin_op_step_flags send_flag;
    ucs_status_t status = ucg_builtin_step_send_flags(step, phase, params, &send_flag);
    extra_flags |= (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED);
    if (ucs_unlikely(status != UCS_OK)) {
        return status;
    }

    /* Set the actual step-related parameters */
    switch (phase->method) {
    /* Send-only */
    case UCG_PLAN_METHOD_SCATTER_TERMINAL:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_LENGTH_PER_REQUEST;
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
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_LENGTH_PER_REQUEST;
        /* no break */
    case UCG_PLAN_METHOD_REDUCE_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_BEFORE_SEND1;
        step->flags       = send_flag | extra_flags;
        step->recv_buffer = step->send_buffer = *current_data_buffer =
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
        step->flags       = send_flag | extra_flags;
        break;

    case UCG_PLAN_METHOD_SCATTER_WAYPOINT:
        if (send_flag & UCG_BUILTIN_OP_STEP_FLAG_FRAGMENTED) {
            extra_flags  |= UCG_BUILTIN_OP_STEP_FLAG_PIPELINED;
        }
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV1_BEFORE_SEND;
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_LENGTH_PER_REQUEST;
        step->flags       = send_flag | extra_flags;
        step->recv_buffer = step->send_buffer = *current_data_buffer =
                (int8_t*)ucs_calloc(1, step->buffer_length, "ucg_fanout_waypoint_buffer");
        if (!step->recv_buffer) return UCS_ERR_NO_MEMORY;
        // TODO: memory registration, and de-registration at some point...
        break;

    /* Recursive patterns */
    case UCG_PLAN_METHOD_REDUCE_RECURSIVE:
        extra_flags      |= UCG_BUILTIN_OP_STEP_FLAG_RECV_AFTER_SEND;
        step->flags       = send_flag | extra_flags;
        break;

    default:
        ucs_error("Invalid method for a collective operation.");
        return UCS_ERR_INVALID_PARAM;
    }

    /* fill in additional data before finishing this step */
    if (phase->ep_cnt == 1) {
        step->flags |= UCG_BUILTIN_OP_STEP_FLAG_SINGLE_ENDPOINT;
    }
    if (step->flags & send_flag) {
        step->am_header.remote_offset = 0;
    }

    /* Pipelining preparation */
    if (step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
        step->fragment_pending = (uint8_t*)UCS_ALLOC_CHECK(sizeof(phase->ep_cnt),
                                                           "ucg_builtin_step_pipelining");
    }

    /* Select the right completion callback */
    return ucg_builtin_step_select_callbacks(phase, &step->recv_cb,
            params->send.count > 0, step->flags);
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
    status = ucg_builtin_op_select_callback(builtin_plan, &op->init_cb);
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
    status = ucg_builtin_op_consider_optimization(op);
    if (status != UCS_OK) {
        goto op_cleanup;
    }

    UCS_STATIC_ASSERT(sizeof(ucg_builtin_header_t) <= UCP_WORKER_HEADROOM_PRIV_SIZE);
    UCS_STATIC_ASSERT(sizeof(ucg_builtin_header_t) == sizeof(uint64_t));

    op->slots  = (ucg_builtin_comp_slot_t*)builtin_plan->slots;
    op->resend = builtin_plan->resend;
    *new_op    = &op->super;
    return UCS_OK;

op_cleanup:
    ucs_mpool_put_inline(op);
    return status;
}
