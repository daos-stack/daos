/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_CORE_TYPES_H
#define MERCURY_CORE_TYPES_H

#include "mercury_config.h"
#include "na_types.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

typedef uint64_t hg_size_t; /* Size */
typedef uint64_t hg_id_t;   /* RPC ID */

/* Checksum levels */
typedef enum hg_checksum_level {
    HG_CHECKSUM_NONE,        /*!< no checksum */
    HG_CHECKSUM_RPC_HEADERS, /*!< only RPC headers are checksummed */
    HG_CHECKSUM_RPC_PAYLOAD  /*!< entire RPC payload is checksummed (inc.
                                headers) */
} hg_checksum_level_t;

/**
 * HG init info struct
 * NB. should be initialized using HG_INIT_INFO_INITIALIZER
 */
struct hg_init_info {
    /* NA init info struct, see na_types.h for documentation */
    struct na_init_info_4_0 na_init_info;

    /* Optional NA class that can be used for initializing an HG class. Using
     * that option makes the init string passed to HG_Init() ignored.
     * Default is: NULL */
    na_class_t *na_class;

    /* Controls the initial number of requests that are posted on context
     * creation when the HG class is initialized with listen set to true.
     * A value of zero is equivalent to using the internal default value.
     * Default value is: 512 */
    uint32_t request_post_init;

    /* Controls the number of requests that are incrementally posted when the
     * initial number of requests is exhausted, a value of 0 means that only the
     * initial number of requests will be re-used after they complete. Note that
     * if the number of requests that are posted reaches 0, the underlying
     * NA transport is responsible for queueing incoming requests.
     * A value of -1 indicates no increment.
     * Default value is: 512 */
    int32_t request_post_incr;

    /* Controls whether the NA shared-memory interface should be automatically
     * used if/when the RPC target address shares the same node as its origin.
     * Default is: false */
    uint8_t auto_sm;

    /* Overrides the default info string used to initialize the NA shared-memory
     * interface when auto_sm is set to true (e.g., "foo-bar" will create
     * shared-memory objects and directories using "foo-bar" as a suffix).
     * Default is: null */
    const char *sm_info_string;

    /* Control checksum level on RPC (Note this does not include bulk data,
     * which is never checksummed).
     * Default is: HG_CHECKSUM_DEFAULT */
    hg_checksum_level_t checksum_level;

    /* Controls whether mercury should _NOT_ attempt to transfer small bulk data
     * along with the RPC request.
     * Default is: false */
    uint8_t no_bulk_eager;

    /* Disable internal loopback interface that enables forwarding of RPC
     * requests to self addresses. Doing so will force traffic to be routed
     * through NA. For performance reasons, users should be cautious when using
     * that option.
     * Default is: false */
    uint8_t no_loopback;

    /* (Debug) Print stats at exit.
     * Default is: false */
    uint8_t stats;

    /* Disable use of multi_recv when available and post separate buffers.
     * Default is: false */
    uint8_t no_multi_recv;

    /* Release input buffers as early as possible (usually after HG_Get_input())
     * as opposed to releasing them after a call to handle destroy. This may be
     * beneficial in cases where the RPC execution time is longer than usual.
     * Default is: false */
    uint8_t release_input_early;

    /* Preferred traffic class. Default is NA_TC_UNSPEC */
    enum na_traffic_class traffic_class;

    /* Disable use of overflow buffers when RPC message size is above the eager
     * message size threshold.
     * Default is: false */
    bool no_overflow;

    /* Controls the number of multi-recv buffers that are posted. Incrementing
     * this value may be beneficial in cases where RPC handles remain in use for
     * longer periods of time and release_input_early is not set, preventing
     * existing buffers from being reposted.
     * Default value is: 4 */
    unsigned int multi_recv_op_max;

    /* Controls when we should start copying data in an effort to release
     * multi-recv buffers. Copy will occur when at most
     * multi_recv_copy_threshold buffers remain. Value should not exceed
     * multi_recv_op_max.
     * Default value is: 0 (never copy) */
    unsigned int multi_recv_copy_threshold;
};

/* Error return codes:
 * Functions return 0 for success or corresponding return code */
#define HG_RETURN_VALUES                                                       \
    X(HG_SUCCESS)        /*!< operation succeeded */                           \
    X(HG_PERMISSION)     /*!< operation not permitted */                       \
    X(HG_NOENTRY)        /*!< no such file or directory */                     \
    X(HG_INTERRUPT)      /*!< operation interrupted */                         \
    X(HG_AGAIN)          /*!< operation must be retried */                     \
    X(HG_NOMEM)          /*!< out of memory */                                 \
    X(HG_ACCESS)         /*!< permission denied */                             \
    X(HG_FAULT)          /*!< bad address */                                   \
    X(HG_BUSY)           /*!< device or resource busy */                       \
    X(HG_EXIST)          /*!< entry already exists */                          \
    X(HG_NODEV)          /*!< no such device */                                \
    X(HG_INVALID_ARG)    /*!< invalid argument */                              \
    X(HG_PROTOCOL_ERROR) /*!< protocol error */                                \
    X(HG_OVERFLOW)       /*!< value too large */                               \
    X(HG_MSGSIZE)        /*!< message size too long */                         \
    X(HG_PROTONOSUPPORT) /*!< protocol not supported */                        \
    X(HG_OPNOTSUPPORTED) /*!< operation not supported on endpoint */           \
    X(HG_ADDRINUSE)      /*!< address already in use */                        \
    X(HG_ADDRNOTAVAIL)   /*!< cannot assign requested address */               \
    X(HG_HOSTUNREACH)    /*!< cannot reach host during operation */            \
    X(HG_TIMEOUT)        /*!< operation reached timeout */                     \
    X(HG_CANCELED)       /*!< operation canceled */                            \
    X(HG_IO_ERROR)       /*!< I/O error */                                     \
    X(HG_CHECKSUM_ERROR) /*!< checksum error */                                \
    X(HG_NA_ERROR)       /*!< generic NA error */                              \
    X(HG_OTHER_ERROR)    /*!< generic HG error */                              \
    X(HG_RETURN_MAX)

#define X(a) a,
typedef enum hg_return { HG_RETURN_VALUES } hg_return_t;
#undef X

/* Compat return codes */
#define HG_INVALID_PARAM HG_INVALID_ARG
#define HG_SIZE_ERROR    HG_MSGSIZE
#define HG_NOMEM_ERROR   HG_NOMEM
#define HG_NO_MATCH      HG_NOENTRY

/* Callback operation type */
typedef enum hg_cb_type {
    HG_CB_LOOKUP,  /*!< lookup callback */
    HG_CB_FORWARD, /*!< forward callback */
    HG_CB_RESPOND, /*!< respond callback */
    HG_CB_BULK     /*!< bulk transfer callback */
} hg_cb_type_t;

/* Input / output operation type */
typedef enum { HG_UNDEF, HG_INPUT, HG_OUTPUT } hg_op_t;

/**
 * Encode/decode operations.
 */
typedef enum {
    HG_ENCODE, /*!< causes the type to be encoded into the stream */
    HG_DECODE, /*!< causes the type to be extracted from the stream */
    HG_FREE    /*!< can be used to release the space allocated by an HG_DECODE
                  request */
} hg_proc_op_t;

/**
 * Encode/decode operation flags.
 */
#define HG_CORE_SM (1 << 0)

/**
 * Counters.
 */
struct hg_diag_counters {
    uint64_t rpc_req_sent_count;        /* RPC requests sent */
    uint64_t rpc_req_recv_count;        /* RPC requests received */
    uint64_t rpc_resp_sent_count;       /* RPC responses sent */
    uint64_t rpc_resp_recv_count;       /* RPC responses received */
    uint64_t rpc_req_extra_count;       /* RPCs that required extra data */
    uint64_t rpc_resp_extra_count;      /* RPCs that required extra data */
    uint64_t rpc_req_recv_active_count; /* Currently active RPCs */
    uint64_t rpc_multi_recv_copy_count; /* RPCs requests received that
                                                     required a copy */
    uint64_t bulk_count;                /* Bulk transfer count */
};

/*****************/
/* Public Macros */
/*****************/

/* Versions */
#define HG_VERSION(major, minor) (((major) << 16) | (minor))
#define HG_MAJOR(version)        (version >> 16)
#define HG_MINOR(version)        (version & 0xffff)
#define HG_VERSION_GE(v1, v2)    (v1 >= v2)
#define HG_VERSION_LT(v1, v2)    (v1 < v2)

/* Max timeout */
#define HG_MAX_IDLE_TIME (3600 * 1000)

/* HG size max */
#define HG_SIZE_MAX (UINT64_MAX)

/* HG init info initializer */
#define HG_INIT_INFO_INITIALIZER                                               \
    (struct hg_init_info)                                                      \
    {                                                                          \
        .na_init_info = NA_INIT_INFO_INITIALIZER_4_0, .na_class = NULL,        \
        .request_post_init = 0, .request_post_incr = 0, .auto_sm = false,      \
        .sm_info_string = NULL, .checksum_level = HG_CHECKSUM_NONE,            \
        .no_bulk_eager = false, .no_loopback = false, .stats = false,          \
        .no_multi_recv = false, .release_input_early = false,                  \
        .no_overflow = false, .multi_recv_op_max = 0,                          \
        .multi_recv_copy_threshold = 0                                         \
    }

#endif /* MERCURY_CORE_TYPES_H */
