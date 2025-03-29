/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_header.h"
#include "mercury_error.h"

#include "mercury_inet.h"

#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

/* Convert values between host and network byte order */
#define hg_header_proc_uint32_t_enc(x) htonl(x & 0xffffffff)
#define hg_header_proc_uint32_t_dec(x) ntohl(x & 0xffffffff)

/* Proc type */
#define HG_HEADER_PROC_TYPE(buf_ptr, data, type, op)                           \
    do {                                                                       \
        type __tmp;                                                            \
        if (op == HG_ENCODE) {                                                 \
            __tmp = hg_header_proc_##type##_enc(data);                         \
            memcpy(buf_ptr, &__tmp, sizeof(type));                             \
        } else {                                                               \
            memcpy(&__tmp, buf_ptr, sizeof(type));                             \
            data = hg_header_proc_##type##_dec(__tmp);                         \
        }                                                                      \
        buf_ptr = (char *) buf_ptr + sizeof(type);                             \
    } while (0)

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
void
hg_header_init(struct hg_header *hg_header, hg_op_t op)
{
    hg_header_reset(hg_header, op);
}

/*---------------------------------------------------------------------------*/
void
hg_header_finalize(struct hg_header *hg_header)
{
    (void) hg_header;
}

/*---------------------------------------------------------------------------*/
void
hg_header_reset(struct hg_header *hg_header, hg_op_t op)
{
    switch (op) {
        case HG_INPUT:
            memset(&hg_header->msg.input, 0, sizeof(struct hg_header_input));
            break;
        case HG_OUTPUT:
            memset(&hg_header->msg.output, 0, sizeof(struct hg_header_output));
            break;
        default:
            break;
    }
    hg_header->op = op;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_header_proc(
    hg_proc_op_t op, void *buf, size_t buf_size, struct hg_header *hg_header)
{
#ifdef HG_HAS_CHECKSUMS
    struct hg_header_hash *header_hash = NULL;
    void *buf_ptr = buf;
    hg_return_t ret;
#endif

#ifdef HG_HAS_CHECKSUMS
    switch (hg_header->op) {
        case HG_INPUT:
            HG_CHECK_SUBSYS_ERROR(rpc,
                buf_size < sizeof(struct hg_header_input), error, ret,
                HG_INVALID_ARG, "Invalid buffer size");
            header_hash = &hg_header->msg.input.hash;
            break;
        case HG_OUTPUT:
            HG_CHECK_SUBSYS_ERROR(rpc,
                buf_size < sizeof(struct hg_header_output), error, ret,
                HG_INVALID_ARG, "Invalid buffer size");
            header_hash = &hg_header->msg.output.hash;
            break;
        default:
            HG_GOTO_SUBSYS_ERROR(
                rpc, error, ret, HG_INVALID_ARG, "Invalid header op");
    }

    /* Checksum of user payload */
    HG_HEADER_PROC_TYPE(buf_ptr, header_hash->payload, uint32_t, op);

    return HG_SUCCESS;

error:
    return ret;
#else
    (void) hg_header;
    (void) buf;
    (void) buf_size;
    (void) op;

    return HG_SUCCESS;
#endif
}
