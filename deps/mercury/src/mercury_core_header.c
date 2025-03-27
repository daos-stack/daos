/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_core_header.h"
#include "mercury_error.h"

#ifdef HG_HAS_CHECKSUMS
#    include <mchecksum.h>
#endif

#include "mercury_inet.h"

#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

#define HG_CORE_HEADER_CHECKSUM "crc16"

/* Convert values between host and network byte order */
#define hg_core_header_proc_uint8_t_enc(x)  (x & 0xff)
#define hg_core_header_proc_uint8_t_dec(x)  (x & 0xff)
#define hg_core_header_proc_uint16_t_enc(x) htons(x & 0xffff)
#define hg_core_header_proc_uint16_t_dec(x) ntohs(x & 0xffff)
#define hg_core_header_proc_uint32_t_enc(x) htonl(x & 0xffffffff)
#define hg_core_header_proc_uint32_t_dec(x) ntohl(x & 0xffffffff)
#define hg_core_header_proc_uint64_t_enc(x)                                    \
    (((uint64_t) htonl(x & 0xffffffff) << 32) | htonl((uint32_t) (x >> 32)))
#define hg_core_header_proc_uint64_t_dec(x)                                    \
    (((uint64_t) ntohl(x & 0xffffffff) << 32) | ntohl((uint32_t) (x >> 32)))

/* Signed values */
#define hg_core_header_proc_int8_t_enc(x)                                      \
    (int8_t) hg_core_header_proc_uint8_t_enc((uint8_t) x)
#define hg_core_header_proc_int8_t_dec(x)                                      \
    (int8_t) hg_core_header_proc_uint8_t_dec((uint8_t) x)

/* Update checksum */
#ifdef HG_HAS_CHECKSUMS
#    define HG_CORE_HEADER_CHECKSUM_UPDATE(hg_header, data, type)              \
        do {                                                                   \
            if (hg_header->checksum != MCHECKSUM_OBJECT_NULL)                  \
                mchecksum_update(hg_header->checksum, &data, sizeof(type));    \
        } while (0)
#else
#    define HG_CORE_HEADER_CHECKSUM_UPDATE(hg_header, data, type)
#endif

/* Proc type */
#define HG_CORE_HEADER_PROC_TYPE(buf_ptr, data, type, op)                      \
    do {                                                                       \
        type __tmp;                                                            \
        if (op == HG_ENCODE) {                                                 \
            __tmp = hg_core_header_proc_##type##_enc(data);                    \
            memcpy(buf_ptr, &__tmp, sizeof(type));                             \
        } else {                                                               \
            memcpy(&__tmp, buf_ptr, sizeof(type));                             \
            data = hg_core_header_proc_##type##_dec(__tmp);                    \
        }                                                                      \
        buf_ptr = (char *) buf_ptr + sizeof(type);                             \
    } while (0)

/* Proc */
#define HG_CORE_HEADER_PROC(hg_header, buf_ptr, data, type, op)                \
    do {                                                                       \
        HG_CORE_HEADER_PROC_TYPE(buf_ptr, data, type, op);                     \
        HG_CORE_HEADER_CHECKSUM_UPDATE(hg_header, data, type);                 \
    } while (0)

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/
extern const char *
HG_Error_to_string(hg_return_t errnum);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
void
hg_core_header_request_init(
    struct hg_core_header *hg_core_header, bool use_checksum)
{
#ifdef HG_HAS_CHECKSUMS
    /* Create a new checksum (CRC16) */
    if (use_checksum)
        mchecksum_init(HG_CORE_HEADER_CHECKSUM, &hg_core_header->checksum);
#else
    (void) use_checksum;
#endif

    hg_core_header_request_reset(hg_core_header);
}

/*---------------------------------------------------------------------------*/
void
hg_core_header_response_init(
    struct hg_core_header *hg_core_header, bool use_checksum)
{
#ifdef HG_HAS_CHECKSUMS
    /* Create a new checksum (CRC16) */
    if (use_checksum)
        mchecksum_init(HG_CORE_HEADER_CHECKSUM, &hg_core_header->checksum);
#else
    (void) use_checksum;
#endif

    hg_core_header_response_reset(hg_core_header);
}

/*---------------------------------------------------------------------------*/
void
hg_core_header_request_finalize(struct hg_core_header *hg_core_header)
{
#ifdef HG_HAS_CHECKSUMS
    mchecksum_destroy(hg_core_header->checksum);
    hg_core_header->checksum = MCHECKSUM_OBJECT_NULL;
#else
    (void) hg_core_header;
#endif
}

/*---------------------------------------------------------------------------*/
void
hg_core_header_response_finalize(struct hg_core_header *hg_core_header)
{
#ifdef HG_HAS_CHECKSUMS
    mchecksum_destroy(hg_core_header->checksum);
    hg_core_header->checksum = MCHECKSUM_OBJECT_NULL;
#else
    (void) hg_core_header;
#endif
}

/*---------------------------------------------------------------------------*/
void
hg_core_header_request_reset(struct hg_core_header *hg_core_header)
{
    memset(
        &hg_core_header->msg.request, 0, sizeof(struct hg_core_header_request));
    hg_core_header->msg.request.hg = HG_CORE_IDENTIFIER;
    hg_core_header->msg.request.protocol = HG_CORE_PROTOCOL_VERSION;

#ifdef HG_HAS_CHECKSUMS
    if (hg_core_header->checksum != MCHECKSUM_OBJECT_NULL)
        mchecksum_reset(hg_core_header->checksum);
#endif
}

/*---------------------------------------------------------------------------*/
void
hg_core_header_response_reset(struct hg_core_header *hg_core_header)
{
    memset(&hg_core_header->msg.response, 0,
        sizeof(struct hg_core_header_response));

#ifdef HG_HAS_CHECKSUMS
    if (hg_core_header->checksum != MCHECKSUM_OBJECT_NULL)
        mchecksum_reset(hg_core_header->checksum);
#endif
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_core_header_request_proc(hg_proc_op_t op, void *buf, size_t buf_size,
    struct hg_core_header *hg_core_header)
{
    void *buf_ptr = buf;
    struct hg_core_header_request *header = &hg_core_header->msg.request;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc, buf_size < sizeof(struct hg_core_header_request),
        error, ret, HG_INVALID_ARG, "Invalid buffer size");

#ifdef HG_HAS_CHECKSUMS
    /* Reset header checksum first */
    if (hg_core_header->checksum != MCHECKSUM_OBJECT_NULL)
        mchecksum_reset(hg_core_header->checksum);
#endif

    /* HG byte */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->hg, uint8_t, op);

    /* Protocol */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->protocol, uint8_t, op);

    /* RPC ID */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->id, uint64_t, op);

    /* Flags */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->flags, uint8_t, op);

    /* Cookie */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->cookie, uint8_t, op);

#ifdef HG_HAS_CHECKSUMS
    if (hg_core_header->checksum != MCHECKSUM_OBJECT_NULL) {
        /* Checksum of header */
        mchecksum_get(hg_core_header->checksum, &header->hash.header,
            sizeof(uint16_t), MCHECKSUM_FINALIZE);

        if (op == HG_ENCODE) {
            HG_CORE_HEADER_PROC_TYPE(
                buf_ptr, header->hash.header, uint16_t, op);
        } else { /* HG_DECODE */
            uint16_t h_hash_header = 0;

            HG_CORE_HEADER_PROC_TYPE(buf_ptr, h_hash_header, uint16_t, op);
            HG_CHECK_SUBSYS_ERROR(rpc, header->hash.header != h_hash_header,
                error, ret, HG_CHECKSUM_ERROR,
                "checksum 0x%04" PRIx16 " does not match (expected 0x%04" PRIx16
                "!)",
                header->hash.header, h_hash_header);
        }
    }
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_core_header_response_proc(hg_proc_op_t op, void *buf, size_t buf_size,
    struct hg_core_header *hg_core_header)
{
    void *buf_ptr = buf;
    struct hg_core_header_response *header = &hg_core_header->msg.response;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(rpc,
        buf_size < sizeof(struct hg_core_header_response), error, ret,
        HG_OVERFLOW, "Invalid buffer size");

#ifdef HG_HAS_CHECKSUMS
    /* Reset header checksum first */
    if (hg_core_header->checksum != MCHECKSUM_OBJECT_NULL)
        mchecksum_reset(hg_core_header->checksum);
#endif

    /* Return code */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->ret_code, int8_t, op);

    /* Flags */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->flags, uint8_t, op);

    /* Cookie */
    HG_CORE_HEADER_PROC(hg_core_header, buf_ptr, header->cookie, uint16_t, op);

#ifdef HG_HAS_CHECKSUMS
    if (hg_core_header->checksum != MCHECKSUM_OBJECT_NULL) {
        /* Checksum of header */
        mchecksum_get(hg_core_header->checksum, &header->hash.header,
            sizeof(uint16_t), MCHECKSUM_FINALIZE);

        if (op == HG_ENCODE) {
            HG_CORE_HEADER_PROC_TYPE(
                buf_ptr, header->hash.header, uint16_t, op);
        } else { /* HG_DECODE */
            uint16_t h_hash_header = 0;

            HG_CORE_HEADER_PROC_TYPE(buf_ptr, h_hash_header, uint16_t, op);
            HG_CHECK_SUBSYS_ERROR(rpc, header->hash.header != h_hash_header,
                error, ret, HG_CHECKSUM_ERROR,
                "checksum 0x%04" PRIx16 " does not match (expected 0x%04" PRIx16
                "!)",
                header->hash.header, h_hash_header);
        }
    }
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_core_header_request_verify(const struct hg_core_header *hg_core_header)
{
    const struct hg_core_header_request *header = &hg_core_header->msg.request;
    hg_return_t ret;

    /* Must match HG */
    HG_CHECK_SUBSYS_ERROR(rpc,
        (((header->hg >> 1) & 'H') != 'H') || (((header->hg) & 'G') != 'G'),
        error, ret, HG_PROTOCOL_ERROR, "Invalid HG byte");

    HG_CHECK_SUBSYS_ERROR(rpc, header->protocol != HG_CORE_PROTOCOL_VERSION,
        error, ret, HG_PROTONOSUPPORT,
        "Invalid protocol version, using %" PRIx8 ", expected %x",
        header->protocol, HG_CORE_PROTOCOL_VERSION);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_core_header_response_verify(const struct hg_core_header *hg_core_header)
{
    const struct hg_core_header_response *header =
        &hg_core_header->msg.response;

    HG_CHECK_SUBSYS_WARNING(rpc, header->ret_code, "Response return code: %s",
        HG_Error_to_string((hg_return_t) header->ret_code));

    return HG_SUCCESS;
}
