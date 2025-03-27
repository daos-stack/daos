/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2019.  ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#include "builtin_ops.h"

int ucg_builtin_atomic_reduce_full(ucg_builtin_request_t *req,
        uint64_t offset, void *src, void *dst, size_t length, ucs_spinlock_pure_t *lock);
int ucg_builtin_atomic_reduce_partial(ucg_builtin_request_t *req,
        uint64_t offset, void *src, void *dst, size_t length, ucs_spinlock_pure_t *lock);

#define UCG_BUILTIN_BCOPY_PACK_CB(source, offset, length) { \
    ucg_builtin_op_step_t *step      = (ucg_builtin_op_step_t*)arg; \
    ucg_builtin_header_t *header_ptr = (ucg_builtin_header_t*)dest; \
    size_t buffer_length             = length; \
    header_ptr->header               = step->am_header.header; \
    memcpy(header_ptr + 1, source + offset, buffer_length); \
    return sizeof(*header_ptr) + buffer_length; \
}

UCG_BUILTIN_PACKER_DECLARE(_, _single, _sbuf)
UCG_BUILTIN_BCOPY_PACK_CB(step->send_buffer, 0,                 step->buffer_length)

UCG_BUILTIN_PACKER_DECLARE(_, _full, _sbuf)
UCG_BUILTIN_BCOPY_PACK_CB(step->send_buffer, step->iter_offset, step->fragment_length)

UCG_BUILTIN_PACKER_DECLARE(_, _partial, _sbuf)
UCG_BUILTIN_BCOPY_PACK_CB(step->send_buffer, step->iter_offset, step->buffer_length - step->iter_offset)

UCG_BUILTIN_PACKER_DECLARE(_, _single, _rbuf)
UCG_BUILTIN_BCOPY_PACK_CB(step->recv_buffer, 0,                 step->buffer_length)

UCG_BUILTIN_PACKER_DECLARE(_, _full, _rbuf)
UCG_BUILTIN_BCOPY_PACK_CB(step->recv_buffer, step->iter_offset, step->fragment_length)

UCG_BUILTIN_PACKER_DECLARE(_, _partial, _rbuf)
UCG_BUILTIN_BCOPY_PACK_CB(step->recv_buffer, step->iter_offset, step->buffer_length - step->iter_offset)

#ifndef HAVE_UCP_EXTENSIONS
#define LOCK_HACK ucs_spinlock_t *lock = NULL;
#else
#define LOCK_HACK
#endif

#define UCG_BUILTIN_COLL_PACK_CB(source, offset, length, part) { \
    /* First writer to this buffer - overwrite the existing data */ \
    ucg_builtin_request_t *req = (ucg_builtin_request_t*)arg; \
    LOCK_HACK /* until we get HAVE_UCP_EXTENSIONS... */ \
    if (ucs_unlikely(!lock)) { \
        arg = (ucg_builtin_op_step_t*)(req->step); \
        UCG_BUILTIN_BCOPY_PACK_CB(source, offset, length) \
    } else { \
        /* Otherwise - reduce onto existing data */ \
        ucg_builtin_op_step_t *step = (ucg_builtin_op_step_t*)arg; \
        ucg_builtin_header_t *header_ptr = (ucg_builtin_header_t*)dest; \
        return sizeof(*header_ptr) + ucg_builtin_atomic_reduce_ ## part \
                (req, offset, source, header_ptr + 1, length, lock); \
    } \
}

UCG_BUILTIN_PACKER_DECLARE(_locked, _single, _sbuf)
UCG_BUILTIN_COLL_PACK_CB(step->send_buffer, 0,                 step->buffer_length, partial)

UCG_BUILTIN_PACKER_DECLARE(_locked, _full, _sbuf)
UCG_BUILTIN_COLL_PACK_CB(step->send_buffer, step->iter_offset, step->fragment_length, full)

UCG_BUILTIN_PACKER_DECLARE(_locked, _partial, _sbuf)
UCG_BUILTIN_COLL_PACK_CB(step->send_buffer, step->iter_offset, step->buffer_length - step->iter_offset, partial)

UCG_BUILTIN_PACKER_DECLARE(_locked, _single, _rbuf)
UCG_BUILTIN_COLL_PACK_CB(step->recv_buffer, 0,                 step->buffer_length, partial)

UCG_BUILTIN_PACKER_DECLARE(_locked, _full, _rbuf)
UCG_BUILTIN_COLL_PACK_CB(step->recv_buffer, step->iter_offset, step->fragment_length, full)

UCG_BUILTIN_PACKER_DECLARE(_locked, _partial, _rbuf)
UCG_BUILTIN_COLL_PACK_CB(step->recv_buffer, step->iter_offset, step->buffer_length - step->iter_offset, partial)
