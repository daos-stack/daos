/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include <ucp/core/ucp_request.inl> /* For @ref ucp_recv_desc_release */

void static UCS_F_ALWAYS_INLINE
ucg_builtin_comp_last_step_cb(ucg_builtin_request_t *req, ucs_status_t status)
{
    /* Sanity checks */
    ucs_assert(req->comp_req != NULL);
    ucs_assert(((req->comp_req->flags & UCP_REQUEST_FLAG_COMPLETED) == 0) ||
                (req->comp_req->status != UCS_OK));

    /* Mark (per-group) slot as available */
    ucs_container_of(req, ucg_builtin_comp_slot_t, req)->cb = NULL;

    /* Mark request as complete */
    req->comp_req->status = status;
    req->comp_req->flags |= UCP_REQUEST_FLAG_COMPLETED;
    UCS_PROFILE_REQUEST_EVENT(req, "complete_coll", 0);
    ucs_trace_req("collective returning completed request=%p (status: %s)",
            req->comp_req, ucs_status_string(status));
}

ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_comp_step_cb(ucg_builtin_request_t *req,
                         ucg_request_t **user_req)
{
    /* Sanity checks */
    if (req->step->flags & UCG_BUILTIN_OP_STEP_FLAG_PIPELINED) {
        unsigned frag_idx;
        ucs_assert(req->step->fragment_pending != NULL);
        for (frag_idx = 0; frag_idx < req->step->fragments; frag_idx++) {
            ucs_assert(req->step->fragment_pending[frag_idx] == 0);
        }
    }

    /* Check if this is the last step */
    if (ucs_unlikely(req->step->flags & UCG_BUILTIN_OP_STEP_FLAG_LAST_STEP)) {
        ucs_assert(user_req == NULL); /* not directly from step_execute() */
        ucg_builtin_comp_last_step_cb(req, UCS_OK);
        return UCS_OK;
    }

    /* Mark (per-group) slot as available */
    ucs_container_of(req, ucg_builtin_comp_slot_t, req)->cb = NULL;

    /* Start on the next step for this collective operation */
    ucg_builtin_op_step_t *next_step = ++req->step;
    req->pending = next_step->fragments * next_step->phase->ep_cnt;
    req->latest.step_idx = next_step->am_header.msg.step_idx;

    return ucg_builtin_step_execute(req, user_req);
}


ucs_status_t static UCS_F_ALWAYS_INLINE
ucg_builtin_step_check_pending(ucg_builtin_comp_slot_t *slot)
{
    /* Check pending incoming messages - invoke the callback on each one */
    unsigned msg_index;
    ucp_recv_desc_t *rdesc;
    ucg_builtin_header_t *header;
    uint16_t local_id = slot->req.latest.local_id;
    ucs_ptr_array_for_each(rdesc, msg_index, &slot->messages) {
        header = (ucg_builtin_header_t*)(rdesc + 1);
        ucs_assert((header->msg.coll_id  != slot->req.latest.coll_id) ||
                   (header->msg.step_idx >= slot->req.latest.step_idx));
        /*
         * Note: stored message coll_id can be either larger or smaller than
         * the one currently handled - due to coll_id wrap-around.
         */

        if (ucs_likely(header->msg.local_id == local_id)) {
            /* Remove the packet (next call may lead here recursively) */
            ucs_ptr_array_remove(&slot->messages, msg_index, 0);

            /* Handle this "waiting" packet, possibly completing the step */
            int is_step_done = slot->cb(&slot->req, header->remote_offset,
                    header + 1, rdesc->length);

            /* Dispose of the packet, according to its allocation */
            ucp_recv_desc_release(rdesc
#ifdef HAVE_UCP_EXTENSIONS
                    , slot->req.step->uct_iface
#endif
                    );

            /* If the step has indeed completed - check the entire op */
            if (is_step_done) {
                return (slot->req.comp_req->flags & UCP_REQUEST_FLAG_COMPLETED)?
                        slot->req.comp_req->status : UCS_INPROGRESS;
            }
        }
    }

    return UCS_INPROGRESS;
}
