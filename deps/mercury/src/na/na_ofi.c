/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE
#include "na_plugin.h"

#ifndef _WIN32
#    include "na_ip.h"
#    include "na_loc.h"
#endif

#include "mercury_hash_string.h"
#include "mercury_hash_table.h"
#include "mercury_inet.h"
#include "mercury_mem.h"
#include "mercury_mem_pool.h"
#include "mercury_thread.h"
#include "mercury_thread_mutex.h"
#include "mercury_thread_rwlock.h"
#include "mercury_thread_spin.h"
#include "mercury_time.h"

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_ext.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
#ifdef NA_OFI_HAS_EXT_GNI_H
#    include <rdma/fi_ext_gni.h>
#endif
#ifdef NA_OFI_HAS_EXT_CXI_H
#    include <rdma/fi_cxi_ext.h>
#endif

#ifdef _WIN32
#    include <WS2tcpip.h>
#    include <WinSock2.h>
#    include <Windows.h>
#else
#    include <netdb.h>
#    include <sys/socket.h>
#    include <sys/uio.h> /* for struct iovec */
#    include <unistd.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

/* Name of this class */
#define NA_OFI_CLASS_NAME "ofi"

/**
 * FI VERSION provides binary backward and forward compatibility support.
 * Specify the version of OFI is coded to, the provider will select struct
 * layouts that are compatible with this version.
 */
#ifndef FI_COMPILE_VERSION
#    define FI_COMPILE_VERSION FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION)
#endif
#if FI_VERSION_LT(FI_COMPILE_VERSION, FI_VERSION(1, 16))
#    define NA_OFI_VERSION FI_VERSION(1, 9)
#elif FI_VERSION_LT(FI_COMPILE_VERSION, FI_VERSION(1, 20))
/* Bump to 1.13 to support redefinition of OFI log callbacks */
#    define NA_OFI_VERSION FI_VERSION(1, 13)
#else
/* Bump to 1.20 to support av auth keys */
#    define NA_OFI_VERSION FI_VERSION(1, 20)
#endif

/* Fallback for undefined OPX values */
#if FI_VERSION_LT(FI_COMPILE_VERSION, FI_VERSION(1, 15))
#    define FI_ADDR_OPX  -1
#    define FI_PROTO_OPX -1
#endif

/* Fallback for undefined CXI values */
#if FI_VERSION_LT(FI_COMPILE_VERSION, FI_VERSION(1, 14)) /* < 1.14 */
#    define FI_ADDR_CXI  -2
#    define FI_PROTO_CXI -2
#elif (FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION == 14) /* = 1.14 */
#    define FI_ADDR_CXI  (FI_ADDR_PSMX3 + 1)
#    define FI_PROTO_CXI (FI_PROTO_PSMX3 + 1)
#elif (FI_MAJOR_VERSION == 1 && FI_MINOR_VERSION == 15 &&                      \
       FI_REVISION_VERSION < 2) /* 1.15 */
#    define FI_ADDR_CXI  (FI_ADDR_OPX + 1)
#    define FI_PROTO_CXI (FI_PROTO_OPX + 1)
#endif

/* Fallback for undefined XNET values */
#if FI_VERSION_LT(FI_COMPILE_VERSION, FI_VERSION(1, 17))
#    define FI_PROTO_XNET -3
#endif

/* Default basic bits */
#define NA_OFI_MR_BASIC_REQ (FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY)

/* flags that control na_ofi behavior (in the X macro below for each
 * provider)
 */
#define NA_OFI_DOM_IFACE   (1 << 0) /* domain name is interface name */
#define NA_OFI_WAIT_SET    (1 << 1) /* supports FI_WAIT_SET */
#define NA_OFI_WAIT_FD     (1 << 2) /* supports FI_WAIT_FD */
#define NA_OFI_SIGNAL      (1 << 3) /* supports fi_signal() */
#define NA_OFI_SEP         (1 << 4) /* supports SEPs */
#define NA_OFI_LOC_INFO    (1 << 5) /* supports locality info */
#define NA_OFI_CONTEXT2    (1 << 6) /* requires FI_CONTEXT2 */
#define NA_OFI_HMEM        (1 << 7) /* supports FI_HMEM */
#define NA_OFI_AV_AUTH_KEY (1 << 8) /* support FI_AV_AUTH_KEY */

/* X-macro to define the following for each supported provider:
 * - enum type
 * - name
 * - alternate (alias) names for convenience
 * - preferred address format if unspecified
 * - native address format
 * - progress mode
 * - endpoint protocol
 * - additional capabilities used (beyond the base set required by NA)
 * - misc flags to control na_ofi behavior and workarounds with this provider
 *
 * The purpose of this is to aggregate settings for all providers into a
 * single location so that it is easier to alter them.
 */
/* clang-format off */
#define NA_OFI_PROV_TYPES                                                      \
    X(NA_OFI_PROV_NULL, "", "", 0, 0, 0, 0, 0, 0)                              \
    X(NA_OFI_PROV_SHM,                                                         \
      "shm",                                                                   \
      "sm",                                                                    \
      FI_ADDR_STR,                                                             \
      FI_ADDR_STR,                                                             \
      FI_PROGRESS_MANUAL,                                                      \
      FI_PROTO_SHM,                                                            \
      0,                                                                       \
      NA_OFI_HMEM                                                              \
    )                                                                          \
    X(NA_OFI_PROV_SOCKETS,                                                     \
      "sockets",                                                               \
      "",                                                                      \
      FI_SOCKADDR_IN,                                                          \
      FI_SOCKADDR_IN,                                                          \
      FI_PROGRESS_AUTO,                                                        \
      FI_PROTO_SOCK_TCP,                                                       \
      FI_MULTI_RECV,                                                           \
      NA_OFI_DOM_IFACE | NA_OFI_WAIT_FD | NA_OFI_SEP                           \
    )                                                                          \
    X(NA_OFI_PROV_TCP,                                                         \
      "tcp",                                                                   \
      "tcp_exp",                                                               \
      FI_SOCKADDR_IN,                                                          \
      FI_SOCKADDR_IN,                                                          \
      FI_PROGRESS_MANUAL,                                                      \
      FI_PROTO_XNET,                                                           \
      FI_MULTI_RECV,                                                           \
      NA_OFI_DOM_IFACE | NA_OFI_WAIT_FD                                        \
    )                                                                          \
    X(NA_OFI_PROV_TCP_RXM,                                                     \
      "tcp;ofi_rxm",                                                           \
      "tcp_rxm",                                                               \
      FI_SOCKADDR_IN,                                                          \
      FI_SOCKADDR_IN,                                                          \
      FI_PROGRESS_MANUAL,                                                      \
      FI_PROTO_RXM,                                                            \
      FI_MULTI_RECV,                                                           \
      NA_OFI_DOM_IFACE | NA_OFI_WAIT_FD                                        \
    )                                                                          \
    X(NA_OFI_PROV_PSM2,                                                        \
      "psm2",                                                                  \
      "",                                                                      \
      FI_ADDR_PSMX2,                                                           \
      FI_ADDR_PSMX2,                                                           \
      FI_PROGRESS_MANUAL,                                                      \
      FI_PROTO_PSMX2,                                                          \
      FI_SOURCE | FI_SOURCE_ERR | FI_MULTI_RECV,                               \
      NA_OFI_SIGNAL | NA_OFI_SEP | NA_OFI_LOC_INFO                             \
    )                                                                          \
    X(NA_OFI_PROV_OPX,                                                         \
      "opx",                                                                   \
      "",                                                                      \
      FI_ADDR_OPX,                                                             \
      FI_ADDR_OPX,                                                             \
      FI_PROGRESS_MANUAL,                                                      \
      FI_PROTO_OPX,                                                            \
      0,                                                                       \
      NA_OFI_SIGNAL | NA_OFI_SEP | NA_OFI_CONTEXT2                             \
    )                                                                          \
    X(NA_OFI_PROV_VERBS_RXM,                                                   \
      "verbs;ofi_rxm",                                                         \
      "verbs",                                                                 \
      FI_SOCKADDR_IN,                                                          \
      FI_SOCKADDR_IB,                                                          \
      FI_PROGRESS_MANUAL,                                                      \
      FI_PROTO_RXM,                                                            \
      FI_MULTI_RECV,                                                           \
      NA_OFI_WAIT_FD | NA_OFI_LOC_INFO | NA_OFI_HMEM                           \
    )                                                                          \
    X(NA_OFI_PROV_GNI,                                                         \
      "gni",                                                                   \
      "",                                                                      \
      FI_ADDR_GNI,                                                             \
      FI_ADDR_GNI,                                                             \
      FI_PROGRESS_AUTO,                                                        \
      FI_PROTO_GNI,                                                            \
      FI_SOURCE | FI_SOURCE_ERR | FI_MULTI_RECV,                               \
      NA_OFI_WAIT_SET | NA_OFI_SIGNAL | NA_OFI_SEP                             \
    )                                                                          \
    X(NA_OFI_PROV_CXI,                                                         \
      "cxi",                                                                   \
      "",                                                                      \
      FI_ADDR_CXI,                                                             \
      FI_ADDR_CXI,                                                             \
      FI_PROGRESS_MANUAL,                                                      \
      FI_PROTO_CXI,                                                            \
      FI_SOURCE | FI_SOURCE_ERR | FI_MULTI_RECV,                               \
      NA_OFI_WAIT_FD | NA_OFI_LOC_INFO | NA_OFI_HMEM | NA_OFI_AV_AUTH_KEY      \
    )                                                                          \
    X(NA_OFI_PROV_MAX, "", "", 0, 0, 0, 0, 0, 0)
/* clang-format on */

#define X(a, b, c, d, e, f, g, h, i) a,
enum na_ofi_prov_type { NA_OFI_PROV_TYPES };
#undef X
#define X(a, b, c, d, e, f, g, h, i) b,
static const char *const na_ofi_prov_name[] = {NA_OFI_PROV_TYPES};
#undef X
#define X(a, b, c, d, e, f, g, h, i) c,
static const char *const na_ofi_prov_alt_name[] = {NA_OFI_PROV_TYPES};
#undef X
#define X(a, b, c, d, e, f, g, h, i) d,
static int const na_ofi_prov_addr_format_pref[] = {NA_OFI_PROV_TYPES};
#undef X
#define X(a, b, c, d, e, f, g, h, i) e,
static int const na_ofi_prov_addr_format_native[] = {NA_OFI_PROV_TYPES};
#undef X
#define X(a, b, c, d, e, f, g, h, i) f,
static enum fi_progress const na_ofi_prov_progress[] = {NA_OFI_PROV_TYPES};
#undef X
#define X(a, b, c, d, e, f, g, h, i) g,
static int const na_ofi_prov_ep_proto[] = {NA_OFI_PROV_TYPES};
#undef X
#define X(a, b, c, d, e, f, g, h, i) h,
static uint64_t const na_ofi_prov_extra_caps[] = {NA_OFI_PROV_TYPES};
#undef X
#define X(a, b, c, d, e, f, g, h, i) i,
static unsigned long const na_ofi_prov_flags[] = {NA_OFI_PROV_TYPES};
#undef X

/* Prov info array init count */
#define NA_OFI_PROV_INFO_COUNT (32)

/* Address / URI max len */
#define NA_OFI_MAX_URI_LEN (128)

/* Address key hash */
#define NA_OFI_HASH64(x)                                                       \
    ((((uint32_t) (x >> 32)) & 0xFFFF0000U) | ((x & 0xFFFFFFFFU) & 0xFFFFU))

/* Address serialization optimization */
/* Don't enable for now to prevent backward compatibility issues */
// #define NA_OFI_ADDR_OPT

/* OPX */
#define NA_OFI_OPX_NODE_PREFIX "ib"

/* IB */
#ifndef AF_IB
#    define AF_IB 27
#endif
/* Values taken from librdmacm/rdma_cma.h */
#define NA_OFI_IB_IP_PS_MASK   0xFFFFFFFFFFFF0000ULL
#define NA_OFI_IB_IP_PORT_MASK 0x000000000000FFFFULL

/* GNI */
#define NA_OFI_GNI_AV_STR_ADDR_VERSION (1)
#define NA_OFI_GNI_UDREG_REG_LIMIT     (2048)

/* CXI */
#define NA_OFI_CXI_NODE_PREFIX "cxi"
#ifndef C_DFA_PID_BITS_MAX
#    define C_DFA_PID_BITS_MAX 9
#endif
#ifndef C_DFA_NIC_BITS
#    define C_DFA_NIC_BITS 20
#endif

/* Address pool (enabled by default, comment out to disable) */
#define NA_OFI_HAS_ADDR_POOL
#define NA_OFI_ADDR_POOL_COUNT (64)

/* Memory pool (enabled by default, comment out to disable) */
#define NA_OFI_HAS_MEM_POOL
#define NA_OFI_MEM_CHUNK_COUNT (256)
#define NA_OFI_MEM_BLOCK_COUNT (2)

/* Allocation using hugepages */
#define NA_OFI_ALLOC_HUGE (NA_ALLOC_MAX)

/* Unexpected size */
#define NA_OFI_MSG_SIZE       (4096)
#define NA_OFI_TAG_MASK       ((uint64_t) 0x0FFFFFFFF)
#define NA_OFI_UNEXPECTED_TAG (NA_OFI_TAG_MASK + 1)

/* Number of CQ event provided for fi_cq_read() */
#define NA_OFI_CQ_EVENT_NUM (16)
/**
 * CQ default provider sizes:
 * - tcp: 1024
 * - verbs: 1024
 * - opx: 8192
 * - socket: 256
 * - gni: 256
 * Override default to 128k
 */
#define NA_OFI_CQ_DEPTH (131072)

/* Default OP multi CQ size */
#define NA_OFI_OP_MULTI_CQ_SIZE (NA_OFI_CQ_EVENT_NUM * 4)

/* Uncomment to register SGL regions */
// #define NA_OFI_USE_REGV

/* Maximum number of pre-allocated IOV entries */
#define NA_OFI_IOV_STATIC_MAX (8)

/* Receive context bits for SEP */
#define NA_OFI_SEP_RX_CTX_BITS (8)

/* Op ID status bits */
#define NA_OFI_OP_COMPLETED (1 << 0)
#define NA_OFI_OP_CANCELING (1 << 1)
#define NA_OFI_OP_CANCELED  (1 << 2)
#define NA_OFI_OP_QUEUED    (1 << 3)
#define NA_OFI_OP_ERRORED   (1 << 4)

/* Timeout (ms) until we give up on retry */
#define NA_OFI_OP_RETRY_TIMEOUT (120 * 1000)

/* Private data access */
#define NA_OFI_CLASS(x)   ((struct na_ofi_class *) ((x)->plugin_class))
#define NA_OFI_CONTEXT(x) ((struct na_ofi_context *) ((x)->plugin_context))

/* Init info */
#define NA_OFI_INFO_INITIALIZER                                                \
    ((struct na_ofi_info){.addr_format = FI_FORMAT_UNSPEC,                     \
        .thread_mode = FI_THREAD_UNSPEC,                                       \
        .node = NULL,                                                          \
        .service = NULL,                                                       \
        .src_addr = NULL,                                                      \
        .src_addrlen = 0,                                                      \
        .num_auth_keys = 0,                                                    \
        .use_hmem = false})

/* Get IOV */
#define NA_OFI_IOV(iov, iovcnt) (iovcnt > NA_OFI_IOV_STATIC_MAX) ? iov.d : iov.s

/* Reset op ID */
#define NA_OFI_OP_RESET(                                                       \
    _op, _context, _fi_op_flags, _cb_type, _cb, _arg, _addr)                   \
    do {                                                                       \
        *_op->completion_data =                                                \
            (struct na_cb_completion_data){                                    \
                .callback_info =                                               \
                    (struct na_cb_info){                                       \
                        .info.multi_recv_unexpected =                          \
                            (struct na_cb_info_multi_recv_unexpected){         \
                                .actual_buf_size = 0,                          \
                                .source = NULL,                                \
                                .tag = 0,                                      \
                                .actual_buf = NULL,                            \
                                .last = false},                                \
                        .arg = NULL,                                           \
                        .type = _cb_type,                                      \
                        .ret = NA_SUCCESS},                                    \
                .callback = NULL,                                              \
                .plugin_callback = NULL,                                       \
                .plugin_callback_args = NULL},                                 \
        _op->context = _context;                                               \
        _op->addr = _addr;                                                     \
        if (_addr)                                                             \
            na_ofi_addr_ref_incr(_addr);                                       \
        _op->retry_op.msg = NULL;                                              \
        _op->fi_op_flags = _fi_op_flags;                                       \
        _op->callback = _cb;                                                   \
        _op->arg = _arg;                                                       \
        _op->type = _cb_type;                                                  \
        hg_atomic_set32(&_op->status, 0);                                      \
    } while (0)

#define NA_OFI_OP_RELEASE(_op)                                                 \
    do {                                                                       \
        if (_op->addr)                                                         \
            na_ofi_addr_ref_decr(_op->addr);                                   \
        hg_atomic_set32(&_op->status, NA_OFI_OP_COMPLETED);                    \
    } while (0)

#ifdef _WIN32
#    define strtok_r strtok_s
#    undef strdup
#    define strdup _strdup
#endif

/************************************/
/* Local Type and Struct Definition */
/************************************/

#ifdef _WIN32
typedef USHORT in_port_t;
#endif

/* IB address */
struct na_ofi_sockaddr_ib {
    unsigned short int sib_family; /* AF_IB */
    uint16_t sib_pkey;
    uint32_t sib_flowinfo;
    uint8_t sib_addr[16];
    uint64_t sib_sid;
    uint64_t sib_sid_mask;
    uint64_t sib_scope_id;
};

/* PSM address */
struct na_ofi_psm_addr {
    uint64_t addr0;
};

/* PSM2 address */
struct na_ofi_psm2_addr {
    uint64_t addr0;
    uint64_t addr1;
};

/* OPX address */
typedef uint32_t na_ofi_opx_uid_t;

NA_PACKED(union na_ofi_opx_uid {
    na_ofi_opx_uid_t fi;
    NA_PACKED(struct {
        uint16_t endpoint_id;
        uint16_t lid;
    });
});

NA_PACKED(union na_ofi_opx_address {
    uint64_t raw;
    NA_PACKED(struct {
        uint8_t hfi1_rx;
        uint8_t hfi1_unit;
        uint8_t reliability_rx;
        union na_ofi_opx_uid uid;
        uint8_t rx_index;
    });
});

NA_PACKED(struct na_ofi_opx_addr {
    union na_ofi_opx_address addr;
    uint32_t key;
    uint32_t key_inst;
});

/* GNI address */
struct na_ofi_gni_addr {
    struct {
        uint32_t device_addr; /* physical NIC address     */
        uint32_t cdm_id;      /* user supplied id         */
    };
    struct {
        uint32_t name_type : 8;      /* bound, unbound           */
        uint32_t cm_nic_cdm_id : 24; /* CM nic ID                */
        uint32_t cookie;             /* CDM identifier           */
    };
    struct {
        uint32_t rx_ctx_cnt : 8;  /* number of contexts       */
        uint32_t key_offset : 12; /* auth key offset          */
        uint32_t unused1 : 12;
        uint32_t unused2;
    };
    uint64_t reserved[3];
};

/* CXI address */
#if FI_VERSION_LT(FI_COMPILE_VERSION, FI_VERSION(1, 20))
struct na_ofi_cxi_addr {
    uint32_t pid : C_DFA_PID_BITS_MAX;
    uint32_t nic : C_DFA_NIC_BITS;
    /* ignore other bits */
};
#else
struct na_ofi_cxi_addr {
    uint32_t pid : C_DFA_PID_BITS_MAX;
    uint32_t nic : C_DFA_NIC_BITS;
    uint16_t vni;
};
#endif

/* String address */
struct na_ofi_str_addr {
    char buf[NA_OFI_MAX_URI_LEN];
};

/* Raw address */
union na_ofi_raw_addr {
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
    struct na_ofi_sockaddr_ib sib;
    struct na_ofi_psm_addr psm;
    struct na_ofi_psm2_addr psm2;
    struct na_ofi_opx_addr opx;
    struct na_ofi_gni_addr gni;
    struct na_ofi_cxi_addr cxi;
    struct na_ofi_str_addr str;
};

/* Address key */
struct na_ofi_addr_key {
    union na_ofi_raw_addr addr;
    uint64_t val; /* Keep a 64-bit value for now to simplify hashing */
};

/* Address */
struct na_ofi_addr {
    struct na_ofi_addr_key addr_key; /* Address key               */
    STAILQ_ENTRY(na_ofi_addr) entry; /* Entry in addr pool        */
    struct na_ofi_class *class;      /* Class                     */
    fi_addr_t fi_addr;               /* FI address                */
    fi_addr_t fi_auth_key;           /* FI auth key               */
    hg_atomic_int32_t refcount;      /* Reference counter         */
};

/* Error address info */
struct na_ofi_src_err {
    union na_ofi_raw_addr addr; /* Source address */
    size_t addrlen;             /* Address size */
    fi_addr_t fi_auth_key;      /* Auth key if configured with FI_AV_AUTH_KEY */
};

/* Message buffer info */
struct na_ofi_msg_buf_handle {
    size_t alloc_size;    /* Buf alloc size */
    unsigned long flags;  /* Buf alloc flags */
    struct fid_mr *fi_mr; /* FI MR handle    */
};

/* Memory descriptor info */
struct na_ofi_mem_desc_info {
    uint64_t fi_mr_key; /* FI MR key                   */
    uint64_t len;       /* Size of region              */
    uint64_t iovcnt;    /* Segment count               */
    uint8_t flags;      /* Flag of operation access    */
};

/* Memory descriptor */
struct na_ofi_mem_desc {
    struct na_ofi_mem_desc_info info; /* Segment info */
    union {
        struct iovec s[NA_OFI_IOV_STATIC_MAX]; /* Single segment */
        struct iovec *d;                       /* Multiple segments */
    } iov;                                     /* Remain last */
};

/* Memory handle */
struct na_ofi_mem_handle {
    struct na_ofi_mem_desc desc; /* Memory descriptor        */
    struct fid_mr *fi_mr;        /* FI MR handle             */
};

/* Msg info */
struct na_ofi_msg_info {
    union {
        const void *const_ptr;
        void *ptr;
    } buf;
    void *desc;
    size_t buf_size;
    fi_addr_t fi_addr;
    uint64_t tag;
    uint64_t tag_mask;
};

/* OFI RMA op (put/get) */
typedef ssize_t (*na_ofi_rma_op_t)(
    struct fid_ep *ep, const struct fi_msg_rma *msg, uint64_t flags);

/* RMA info */
struct na_ofi_rma_info {
    na_ofi_rma_op_t fi_rma_op;
    const char *fi_rma_op_string;
    uint64_t fi_rma_flags;
    union {
        struct iovec s[NA_OFI_IOV_STATIC_MAX]; /* Single segment */
        struct iovec *d;                       /* Multiple segments */
    } local_iov_storage;
    struct iovec *local_iov;
    union {
        void *s[NA_OFI_IOV_STATIC_MAX]; /* Single segment */
        void **d;                       /* Multiple segments */
    } local_desc_storage;
    void **local_desc;
    size_t local_iovcnt;
    fi_addr_t fi_addr;
    union {
        struct fi_rma_iov s[NA_OFI_IOV_STATIC_MAX]; /* Single segment */
        struct fi_rma_iov *d;                       /* Multiple segments */
    } remote_iov_storage;
    struct fi_rma_iov *remote_iov;
    size_t remote_iovcnt;
};

struct na_ofi_completion_multi {
    struct na_cb_completion_data *data;
    hg_atomic_int32_t head;
    hg_atomic_int32_t tail;
    int32_t mask;
    uint32_t size;
    uint32_t completion_count; /* Total completion count */
};

/* Operation ID */
struct na_ofi_op_id {
    union {
        struct na_cb_completion_data single;  /* Single completion      */
        struct na_ofi_completion_multi multi; /* Multiple completions   */
    } completion_data_storage;                /* Completion data storage */
    union {
        struct na_ofi_msg_info msg;    /* Msg info (tagged and non-tagged) */
        struct na_ofi_rma_info rma;    /* RMA info */
    } info;                            /* Op info                  */
    TAILQ_ENTRY(na_ofi_op_id) multi;   /* Entry in multi queue     */
    TAILQ_ENTRY(na_ofi_op_id) retry;   /* Entry in retry queue     */
    struct fi_context fi_ctx[2];       /* Context handle           */
    hg_time_t retry_deadline;          /* Retry deadline           */
    hg_time_t retry_last;              /* Last retry time          */
    struct na_ofi_class *na_ofi_class; /* NA class associated      */
    na_context_t *context;             /* NA context associated    */
    struct na_ofi_addr *addr;          /* Address associated       */
    union {
        na_return_t (*msg)(
            struct fid_ep *, const struct na_ofi_msg_info *, void *);
        na_return_t (*rma)(
            struct fid_ep *, const struct na_ofi_rma_info *, void *);
    } retry_op; /* Operation used for retries */
    void (*complete)(struct na_ofi_op_id *, bool, na_return_t); /* Complete */
    struct na_cb_completion_data *completion_data; /* Completion data */
    uint64_t fi_op_flags;     /* Operation flags          */
    na_cb_t callback;         /* Operation callback       */
    void *arg;                /* Callback args            */
    na_cb_type_t type;        /* Operation type           */
    hg_atomic_int32_t status; /* Operation status         */
    bool multi_event;         /* Triggers multiple events */
};

/* Op ID queue */
struct na_ofi_op_queue {
    TAILQ_HEAD(, na_ofi_op_id) queue;
    hg_thread_spin_t lock;
};

/* Event queue */
struct na_ofi_eq {
    struct fid_cq *fi_cq;                   /* CQ handle                */
    struct na_ofi_op_queue *retry_op_queue; /* Retry op queue           */
    struct fid_wait *fi_wait;               /* Optional wait set handle */
};

/* Context */
struct na_ofi_context {
    struct na_ofi_op_queue multi_op_queue; /* To keep track of multi-events */
    struct fid_ep *fi_tx;                  /* Transmit context handle       */
    struct fid_ep *fi_rx;                  /* Receive context handle        */
    struct na_ofi_eq *eq;                  /* Event queues                  */
    hg_atomic_int32_t multi_op_count;      /* Number of multi-events ops    */
    uint8_t idx;                           /* Context index                 */
};

/* Endpoint */
struct na_ofi_endpoint {
    struct fid_ep *fi_ep;           /* Endpoint handle  */
    struct na_ofi_eq *eq;           /* Event queues     */
    struct na_ofi_addr *src_addr;   /* Endpoint address */
    size_t unexpected_msg_size_max; /* Max unexpected msg size */
    size_t expected_msg_size_max;   /* Max expected msg size */
};

/* Map (used to cache addresses) */
struct na_ofi_map {
    hg_thread_rwlock_t lock;
    hg_hash_table_t *key_map; /* Primary */
    hg_hash_table_t *fi_map;  /* Secondary */
};

#ifndef NA_OFI_HAS_EXT_GNI_H
/* GNI Authorization Key */
struct fi_gni_raw_auth_key {
    uint32_t protection_key;
};

struct fi_gni_auth_key {
    uint32_t type;
    union {
        struct fi_gni_raw_auth_key raw;
    };
};
#endif

#ifndef NA_OFI_HAS_EXT_CXI_H
/*
 * TODO: The following should be integrated into the include/rdma/fi_ext.h
 * and are use for provider specific fi_control() operations.
 */
#    define FI_PROV_SPECIFIC_CXI (0xccc << 16)

enum {
    FI_OPT_CXI_SET_TCLASS = -FI_PROV_SPECIFIC_CXI, /* uint32_t */
    FI_OPT_CXI_SET_MSG_ORDER,                      /* uint64_t */

    /* fid_nic control operation to refresh NIC attributes. */
    FI_OPT_CXI_NIC_REFRESH_ATTR,

    FI_OPT_CXI_SET_MR_MATCH_EVENTS, /* bool */
    FI_OPT_CXI_GET_MR_MATCH_EVENTS, /* bool */
    FI_OPT_CXI_SET_OPTIMIZED_MRS,   /* bool */
    FI_OPT_CXI_GET_OPTIMIZED_MRS,   /* bool */
    FI_OPT_CXI_SET_PROV_KEY_CACHE,  /* bool */
    FI_OPT_CXI_GET_PROV_KEY_CACHE,  /* bool */
};

/* CXI Authorization Key */
struct cxi_auth_key {
    uint32_t svc_id;
    uint16_t vni;
};
#endif

/* Authorization Key */
union na_ofi_auth_key {
    struct fi_gni_auth_key gni_auth_key; /* GNI auth key */
    struct cxi_auth_key cxi_auth_key;    /* CXI auth key */
};

/* Domain */
struct na_ofi_domain {
    const struct na_ofi_fabric *fabric; /* Associated fabric */
    struct na_ofi_map addr_map;         /* Address map */
    hg_hash_table_t *auth_key_map;      /* Auth key map (FI_AV_AUTH_KEY) */
    struct fid_domain *fi_domain;       /* Domain handle */
    struct fid_av *fi_av;               /* Address vector handle */
    char *name;                         /* Domain name */
    size_t context_max;                 /* Max contexts available */
    hg_atomic_int64_t requested_key; /* Requested key if not FI_MR_PROV_KEY */
    int64_t max_key;                 /* Max key if not FI_MR_PROV_KEY */
    uint64_t max_tag;                /* Max tag from CQ data size */
    hg_atomic_int32_t *mr_reg_count; /* Number of MR registered */
    bool no_wait;                    /* Wait disabled on domain */
    bool av_auth_key;                /* Use FI_AV_AUTH_KEY */
    bool av_user_id;                 /* Use FI_AV_USER_ID */
} HG_LOCK_CAPABILITY("domain");

/* Addr pool */
struct na_ofi_addr_pool {
    STAILQ_HEAD(, na_ofi_addr) queue;
    hg_thread_spin_t lock;
};

/* Fabric */
struct na_ofi_fabric {
    SLIST_ENTRY(na_ofi_fabric) entry; /* Entry in fabric list */
    struct fid_fabric *fi_fabric;     /* Fabric handle */
    char *name;                       /* Fabric name */
    char *prov_name;                  /* Provider name */
    enum na_ofi_prov_type prov_type;  /* Provider type */
    int32_t refcount;                 /* Refcount of this fabric */
} HG_LOCK_CAPABILITY("fabric");

/* Get info */
struct na_ofi_info {
    char *node;                    /* Node/host IP info */
    char *service;                 /* Service/port info */
    enum fi_threading thread_mode; /* Thread mode */
    int addr_format;               /* Address format */
    void *src_addr;                /* Native src addr */
    size_t src_addrlen;            /* Native src addr len */
    size_t num_auth_keys;          /* Requested number of auth keys */
    bool use_hmem;                 /* Use FI_HMEM */
};

/* Verify info */
struct na_ofi_verify_info {
    const struct na_loc_info *loc_info; /* Loc info */
    const char *domain_name;            /* Domain name */
    int addr_format;                    /* Addr format */
    enum na_ofi_prov_type prov_type;    /* Provider type */
};

/* OFI class */
struct na_ofi_class {
    struct na_ofi_addr_pool addr_pool; /* Addr pool                */
    struct fi_info *fi_info;           /* OFI info                 */
    struct na_ofi_fabric *fabric;      /* Fabric pointer           */
    struct na_ofi_domain *domain;      /* Domain pointer           */
    struct na_ofi_endpoint *endpoint;  /* Endpoint pointer         */
    struct hg_mem_pool *send_pool;     /* Msg send buf pool        */
    struct hg_mem_pool *recv_pool;     /* Msg recv buf pool        */
    na_return_t (*msg_send_unexpected)(
        struct fid_ep *, const struct na_ofi_msg_info *, void *);
    na_return_t (*msg_recv_unexpected)(
        struct fid_ep *, const struct na_ofi_msg_info *, void *);
    na_return_t (*cq_poll)(
        struct na_ofi_class *, struct na_ofi_context *, unsigned int *);
    unsigned long opt_features;    /* Optional feature flags   */
    hg_atomic_int32_t n_contexts;  /* Number of context        */
    unsigned int op_retry_timeout; /* Retry timeout            */
    unsigned int op_retry_period;  /* Time elapsed until next retry */
    uint8_t context_max;           /* Max number of contexts   */
    bool no_wait;                  /* Ignore wait object       */
    bool use_sep;                  /* Use scalable endpoints */
    bool finalizing;               /* Class being destroyed    */
};

/********************/
/* Local Prototypes */
/********************/

#if !defined(_WIN32) && FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 16))
/**
 * Check if OFI log is enabled.
 */
static int
na_ofi_log_enabled(const struct fi_provider *prov, enum fi_log_level level,
    enum fi_log_subsys subsys, uint64_t flags);

/**
 * Print OFI log.
 */
static void
na_ofi_log(const struct fi_provider *prov, enum fi_log_level level,
    enum fi_log_subsys subsys, const char *func, int line, const char *msg);

/**
 * Check if OFI log is ready.
 */
static int
na_ofi_log_ready(const struct fi_provider *prov, enum fi_log_level level,
    enum fi_log_subsys subsys, uint64_t flags, uint64_t *showtime);

/**
 * Convert FI log level to HG log level.
 */
static enum hg_log_level
na_ofi_log_level_to_hg(enum fi_log_level level);
#endif

/**
 * Convert FI errno to NA return values.
 */
static na_return_t
na_ofi_errno_to_na(int rc);

/**
 * Convert provider name to enum type.
 */
static NA_INLINE enum na_ofi_prov_type
na_ofi_prov_name_to_type(const char *prov_name);

/**
 * Convert NA traffic class to tclass.
 */
static NA_INLINE uint32_t
na_ofi_tclass(enum na_traffic_class traffic_class);

/**
 * Determine addr format to use based on preferences.
 */
static NA_INLINE int
na_ofi_prov_addr_format(
    enum na_ofi_prov_type prov_type, enum na_addr_format na_init_format);

/**
 * Get provider encoded address size.
 */
static NA_INLINE size_t
na_ofi_prov_addr_size(int addr_format);

/**
 * Get provider type encoded in string.
 */
static NA_INLINE enum na_ofi_prov_type
na_ofi_addr_prov(const char *str);

/**
 * Get native address from string.
 */
static NA_INLINE na_return_t
na_ofi_str_to_raw_addr(
    const char *str, int addr_format, union na_ofi_raw_addr *addr);
static na_return_t
na_ofi_str_to_sin(const char *str, struct sockaddr_in *sin_addr);
static na_return_t
na_ofi_str_to_sin6(const char *str, struct sockaddr_in6 *sin6_addr);
static na_return_t
na_ofi_str_to_sib(const char *str, struct na_ofi_sockaddr_ib *sib_addr);
static na_return_t
na_ofi_str_to_psm(const char *str, struct na_ofi_psm_addr *psm_addr);
static na_return_t
na_ofi_str_to_psm2(const char *str, struct na_ofi_psm2_addr *psm2_addr);
static na_return_t
na_ofi_str_to_opx(const char *str, struct na_ofi_opx_addr *opx_addr);
static na_return_t
na_ofi_str_to_gni(const char *str, struct na_ofi_gni_addr *gni_addr);
static na_return_t
na_ofi_str_to_cxi(const char *str, struct na_ofi_cxi_addr *cxi_addr);
static na_return_t
na_ofi_str_to_str(const char *str, struct na_ofi_str_addr *str_addr);

/**
 * Convert the address to a 64-bit key to search corresponding FI addr.
 */
static NA_INLINE uint64_t
na_ofi_raw_addr_to_key(int addr_format, const union na_ofi_raw_addr *addr);
static NA_INLINE uint64_t
na_ofi_sin_to_key(const struct sockaddr_in *addr);
static NA_INLINE uint64_t
na_ofi_sin6_to_key(const struct sockaddr_in6 *addr);
static NA_INLINE uint64_t
na_ofi_sib_to_key(const struct na_ofi_sockaddr_ib *addr);
static NA_INLINE uint64_t
na_ofi_psm_to_key(const struct na_ofi_psm_addr *addr);
static NA_INLINE uint64_t
na_ofi_psm2_to_key(const struct na_ofi_psm2_addr *addr);
static NA_INLINE uint64_t
na_ofi_opx_to_key(const struct na_ofi_opx_addr *addr);
static NA_INLINE uint64_t
na_ofi_gni_to_key(const struct na_ofi_gni_addr *addr);
static NA_INLINE uint64_t
na_ofi_cxi_to_key(const struct na_ofi_cxi_addr *addr);
static NA_INLINE uint64_t
na_ofi_str_to_key(const struct na_ofi_str_addr *addr);

/**
 * Convert a key back to an address. (only for sin serialization)
 */
#ifdef NA_OFI_ADDR_OPT
static NA_INLINE void
na_ofi_key_to_sin(struct sockaddr_in *addr, uint64_t key);
#endif

/**
 * Size required to serialize raw addr.
 */
static NA_INLINE size_t
na_ofi_raw_addr_serialize_size(int addr_format);

/**
 * Serialize addr key.
 */
static na_return_t
na_ofi_raw_addr_serialize(int addr_format, void *buf, size_t buf_size,
    const union na_ofi_raw_addr *addr);

/**
 * Deserialize addr key.
 */
static na_return_t
na_ofi_raw_addr_deserialize(int addr_format, union na_ofi_raw_addr *addr,
    union na_ofi_auth_key *auth_key, const void *buf, size_t buf_size);

/**
 * Lookup addr and insert key if not present.
 */
static na_return_t
na_ofi_addr_key_lookup(struct na_ofi_class *na_ofi_class,
    struct na_ofi_addr_key *addr_key, fi_addr_t fi_auth_key,
    struct na_ofi_addr **na_ofi_addr_p);

/**
 * Key hash for hash table.
 */
static NA_INLINE unsigned int
na_ofi_addr_key_hash(hg_hash_table_key_t key);

/**
 * Compare key.
 */
static NA_INLINE int
na_ofi_addr_key_equal_default(
    hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Compare IPv6 address keys.
 */
static NA_INLINE int
na_ofi_addr_key_equal_sin6(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Compare IB address keys.
 */
static NA_INLINE int
na_ofi_addr_key_equal_sib(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Lookup addr key from map.
 */
static NA_INLINE struct na_ofi_addr *
na_ofi_addr_map_lookup(
    struct na_ofi_map *na_ofi_map, struct na_ofi_addr_key *addr_key);

/**
 * Insert new addr key into map and return addr.
 */
static na_return_t
na_ofi_addr_map_insert(struct na_ofi_class *na_ofi_class,
    struct na_ofi_map *na_ofi_map, struct na_ofi_addr_key *addr_key,
    fi_addr_t fi_auth_key, struct na_ofi_addr **na_ofi_addr_p);

/**
 * Remove addr key from map.
 */
static na_return_t
na_ofi_addr_map_remove(
    struct na_ofi_map *na_ofi_map, struct na_ofi_addr_key *addr_key);

/**
 * Key hash for hash table.
 */
static NA_INLINE unsigned int
na_ofi_fi_addr_hash(hg_hash_table_key_t key);

/**
 * Compare key.
 */
static NA_INLINE int
na_ofi_fi_addr_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Lookup addr key from map.
 */
static NA_INLINE struct na_ofi_addr *
na_ofi_fi_addr_map_lookup(struct na_ofi_map *na_ofi_map, fi_addr_t *fi_addr);

/**
 * Get info caps from providers and return matching providers.
 */
static na_return_t
na_ofi_getinfo(enum na_ofi_prov_type prov_type, const struct na_ofi_info *info,
    struct fi_info **fi_info_p);

/**
 * Match provider name with domain.
 */
static bool
na_ofi_match_provider(const struct na_ofi_verify_info *verify_info,
    const struct fi_info *fi_info);

/**
 * Parse hostname info.
 */
static na_return_t
na_ofi_parse_hostname_info(enum na_ofi_prov_type prov_type,
    const char *hostname_info, int addr_format, char **domain_name_p,
    char **node_p, char **service_p, void **src_addr_p, size_t *src_addrlen_p);

/**
 * Free hostname info.
 */
static void
na_ofi_free_hostname_info(
    char *domain_name, char *node, char *service, void *src_addr);

/**
 * Parse IPv4 info.
 */
static na_return_t
na_ofi_parse_sin_info(
    const char *str, char **domain_name_p, char **hostname_p, uint16_t *port_p);

/**
 * Parse CXI info.
 */
static na_return_t
na_ofi_parse_cxi_info(const char *str, char **node_p, char **service_p);

/**
 * Parse OPX info.
 */
static na_return_t
na_ofi_parse_opx_info(const char *str, struct na_ofi_opx_addr **src_addr_p);

/**
 * Allocate new OFI class.
 */
static struct na_ofi_class *
na_ofi_class_alloc(void);

/**
 * Free OFI class.
 */
static na_return_t
na_ofi_class_free(struct na_ofi_class *na_ofi_class);

/**
 * Configure class parameters from environment variables.
 */
static na_return_t
na_ofi_class_env_config(struct na_ofi_class *na_ofi_class);

/**
 * Open fabric.
 */
na_return_t
na_ofi_fabric_open(enum na_ofi_prov_type prov_type, struct fi_fabric_attr *attr,
    struct na_ofi_fabric **na_ofi_fabric_p);

/**
 * Close fabric.
 */
na_return_t
na_ofi_fabric_close(struct na_ofi_fabric *na_ofi_fabric);

/**
 * Set optional domain ops.
 */
static na_return_t
na_ofi_set_domain_ops(
    enum na_ofi_prov_type prov_type, struct na_ofi_domain *na_ofi_domain);

#ifdef NA_OFI_HAS_EXT_GNI_H
/**
 * Optional domain set op value for GNI provider.
 */
static na_return_t
na_ofi_gni_set_domain_op_value(
    struct na_ofi_domain *na_ofi_domain, int op, void *value);

/**
 * Optional domain get op value for GNI provider.
 */
static na_return_t
na_ofi_gni_get_domain_op_value(
    struct na_ofi_domain *na_ofi_domain, int op, void *value);

/**
 * Set GNI specific domain ops.
 */
static na_return_t
na_ofi_gni_set_domain_ops(struct na_ofi_domain *na_ofi_domain);
#endif

/**
 * Set CXI specific domain ops.
 */
static void
na_ofi_cxi_set_domain_ops(struct na_ofi_domain *na_ofi_domain);

/**
 * Parse auth key.
 */
static na_return_t
na_ofi_parse_auth_key(const char *str, enum na_ofi_prov_type prov_type,
    const char *domain_name, union na_ofi_auth_key *auth_key,
    size_t *auth_key_size_p);

/**
 * Parse GNI auth key.
 */
static na_return_t
na_ofi_parse_gni_auth_key(
    const char *str, struct fi_gni_auth_key *auth_key, size_t *auth_key_size_p);

/**
 * Parse CXI auth key.
 */
static na_return_t
na_ofi_parse_cxi_auth_key(const char *str, const char *domain_name,
    struct cxi_auth_key *auth_key, size_t *auth_key_size_p);

/**
 * Find CXI svc_id.
 */
static na_return_t
na_ofi_cxi_find_svc_id(const char *domain_name, uint32_t *svc_id_p);

/**
 * Find CXI vni.
 */
static na_return_t
na_ofi_cxi_find_vni(int idx, uint16_t *vni_p);

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
/**
 * Parse auth key range.
 */
static na_return_t
na_ofi_parse_auth_key_range(const char *str, enum na_ofi_prov_type prov_type,
    union na_ofi_auth_key *base_key, size_t *auth_key_num_p);

/**
 * Parse CXI auth key range.
 */
static na_return_t
na_ofi_parse_cxi_auth_key_range(
    const char *str, struct cxi_auth_key *base_key, size_t *auth_key_num_p);

/**
 * Generate auth key using base range key.
 */
static na_return_t
na_ofi_gen_auth_key(const union na_ofi_auth_key *base_key, int index,
    enum na_ofi_prov_type prov_type, union na_ofi_auth_key *auth_key,
    size_t *auth_key_size_p);

/**
 * Generate CXI auth key using base range key.
 */
static void
na_ofi_gen_cxi_auth_key(const struct cxi_auth_key *base_key, int index,
    struct cxi_auth_key *auth_key, size_t *auth_key_size_p);

/**
 * Insert auth key.
 */
static na_return_t
na_ofi_auth_key_insert(struct na_ofi_domain *na_ofi_domain,
    const union na_ofi_auth_key *auth_key, size_t auth_key_size);

/**
 * Lookup auth key from map.
 */
static NA_INLINE fi_addr_t
na_ofi_auth_key_lookup(hg_hash_table_t *table, union na_ofi_auth_key *auth_key);

/**
 * Key hash for hash table.
 */
static NA_INLINE unsigned int
na_ofi_auth_key_hash(hg_hash_table_key_t key);

/**
 * Hash CXI key for hash table.
 */
static NA_INLINE unsigned int
na_ofi_cxi_auth_key_hash(const struct cxi_auth_key *key);

/**
 * Compare key.
 */
static NA_INLINE int
na_ofi_auth_key_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2);

/**
 * Compare CXI keys.
 */
static NA_INLINE int
na_ofi_cxi_auth_key_equal(
    const struct cxi_auth_key *key1, const struct cxi_auth_key *key2);
#endif

/**
 * Open domain.
 */
static na_return_t
na_ofi_domain_open(const struct na_ofi_fabric *na_ofi_fabric,
    const void *auth_key, size_t num_auth_keys,
    enum na_traffic_class traffic_class, bool no_wait, struct fi_info *fi_info,
    struct na_ofi_domain **na_ofi_domain_p);

/**
 * Close domain.
 */
static na_return_t
na_ofi_domain_close(struct na_ofi_domain *na_ofi_domain);

/**
 * Open AV.
 */
static na_return_t
na_ofi_av_open(struct na_ofi_domain *na_ofi_domain,
    enum na_ofi_prov_type prov_type, const struct fi_info *fi_info,
    int num_auth_keys, const union na_ofi_auth_key *base_auth_key);

/**
 * Close AV.
 */
na_return_t
na_ofi_av_close(struct na_ofi_domain *na_ofi_domain);

/**
 * Open endpoint.
 */
static na_return_t
na_ofi_endpoint_open(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, bool no_wait, bool sep,
    uint8_t max_contexts, size_t unexpected_msg_size_max,
    size_t expected_msg_size_max, struct fi_info *fi_info,
    struct na_ofi_endpoint **na_ofi_endpoint_p);

/**
 * Open basic endpoint.
 */
static na_return_t
na_ofi_basic_ep_open(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, struct fi_info *fi_info,
    bool no_wait, struct na_ofi_endpoint *na_ofi_endpoint);

/**
 * Open scalable endpoint.
 */
static na_return_t
na_ofi_sep_open(const struct na_ofi_domain *na_ofi_domain,
    struct fi_info *fi_info, uint8_t max_contexts,
    struct na_ofi_endpoint *na_ofi_endpoint);

/**
 * Close endpoint.
 */
static na_return_t
na_ofi_endpoint_close(struct na_ofi_endpoint *na_ofi_endpoint);

/**
 * Open event queues.
 */
static na_return_t
na_ofi_eq_open(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, bool no_wait,
    struct na_ofi_eq **na_ofi_eq_p);

/**
 * Close event queues.
 */
static na_return_t
na_ofi_eq_close(struct na_ofi_eq *na_ofi_eq);

/**
 * Get EP src address.
 */
static na_return_t
na_ofi_endpoint_get_src_addr(struct na_ofi_class *na_ofi_class);

/**
 * Get EP URI.
 */
static na_return_t
na_ofi_get_uri(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, char *buf, size_t *buf_size_p,
    const struct na_ofi_addr_key *addr_key);

/**
 * Allocate empty address.
 */
static struct na_ofi_addr *
na_ofi_addr_alloc(struct na_ofi_class *na_ofi_class);

/**
 * Destroy address.
 */
static void
na_ofi_addr_destroy(struct na_ofi_addr *na_ofi_addr);

#ifdef NA_OFI_HAS_ADDR_POOL
/**
 * Retrieve address from pool.
 */
static struct na_ofi_addr *
na_ofi_addr_pool_get(struct na_ofi_class *na_ofi_class);
#endif

/**
 * Release address without destroying it.
 */
static void
na_ofi_addr_release(struct na_ofi_addr *na_ofi_addr);

/**
 * Reset address.
 */
static void
na_ofi_addr_reset(
    struct na_ofi_addr *na_ofi_addr, struct na_ofi_addr_key *addr_key);

/**
 * Create address.
 */
static na_return_t
na_ofi_addr_create(struct na_ofi_class *na_ofi_class,
    struct na_ofi_addr_key *addr_key, struct na_ofi_addr **na_ofi_addr_p);

/**
 * Increment address refcount.
 */
static NA_INLINE void
na_ofi_addr_ref_incr(struct na_ofi_addr *na_ofi_addr);

/**
 * Decrement address refcount.
 */
static void
na_ofi_addr_ref_decr(struct na_ofi_addr *na_ofi_addr);

/**
 * Allocate memory for transfers.
 */
static void *
na_ofi_mem_alloc(struct na_ofi_class *na_ofi_class, size_t size,
    unsigned long *flags_p, size_t *alloc_size_p, struct fid_mr **mr_hdl_p);

/**
 * Free memory.
 */
static void
na_ofi_mem_free(struct na_ofi_class *na_ofi_class, void *mem_ptr,
    size_t alloc_size, unsigned long flags, struct fid_mr *mr_hdl);

/**
 * Register memory buffer.
 */
static int
na_ofi_mem_buf_register(const void *buf, size_t len, unsigned long flags,
    void **handle_p, void *arg);

/**
 * Deregister memory buffer.
 */
static int
na_ofi_mem_buf_deregister(void *handle, void *arg);

/**
 * Generate key for memory registration.
 */
static uint64_t
na_ofi_mem_key_gen(struct na_ofi_domain *na_ofi_domain);

/**
 * Msg send.
 */
static na_return_t
na_ofi_msg_send(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context);

/**
 * Msg recv.
 */
static na_return_t
na_ofi_msg_recv(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context);

/**
 * Msg multi recv.
 */
static na_return_t
na_ofi_msg_multi_recv(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context);

/**
 * Tagged msg send.
 */
static na_return_t
na_ofi_tag_send(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context);

/**
 * Tagged msg recv.
 */
static na_return_t
na_ofi_tag_recv(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context);

/**
 * Get IOV index and offset pair from an absolute offset.
 */
static NA_INLINE void
na_ofi_iov_get_index_offset(const struct iovec *iov, size_t iovcnt,
    na_offset_t offset, size_t *iov_start_index, na_offset_t *iov_start_offset);

/**
 * Get IOV count for a given length.
 */
static NA_INLINE size_t
na_ofi_iov_get_count(const struct iovec *iov, size_t iovcnt,
    size_t iov_start_index, na_offset_t iov_start_offset, size_t len);

/**
 * Create new IOV for transferring length data.
 */
static NA_INLINE void
na_ofi_iov_translate(const struct iovec *iov, void *desc, size_t iovcnt,
    size_t iov_start_index, na_offset_t iov_start_offset, size_t len,
    struct iovec *new_iov, void **new_desc, size_t new_iovcnt);

/**
 * Create new RMA IOV for transferring length data.
 */
static NA_INLINE void
na_ofi_rma_iov_translate(const struct fi_info *fi_info, const struct iovec *iov,
    size_t iovcnt, uint64_t key, size_t iov_start_index,
    na_offset_t iov_start_offset, size_t len, struct fi_rma_iov *new_iov,
    size_t new_iovcnt);

/**
 * Prepare and post RMA operation (put/get).
 */
static na_return_t
na_ofi_rma_common(struct na_ofi_class *na_ofi_class, na_context_t *context,
    na_cb_type_t op, na_cb_t callback, void *arg, na_ofi_rma_op_t fi_rma_op,
    const char *fi_rma_op_string, uint64_t fi_rma_flags,
    struct na_ofi_mem_handle *na_ofi_mem_handle_local, na_offset_t local_offset,
    struct na_ofi_mem_handle *na_ofi_mem_handle_remote,
    na_offset_t remote_offset, size_t length, struct na_ofi_addr *na_ofi_addr,
    uint8_t remote_id, struct na_ofi_op_id *na_ofi_op_id);

/**
 * Post RMA operation.
 */
static na_return_t
na_ofi_rma_post(
    struct fid_ep *ep, const struct na_ofi_rma_info *rma_info, void *context);

/**
 * Release resources allocated for RMA operation.
 */
static NA_INLINE void
na_ofi_rma_release(struct na_ofi_rma_info *rma_info);

/**
 * Check if we can process and store incoming multi-recv events.
 */
static bool
na_ofi_cq_can_poll_multi(
    struct na_ofi_op_queue *multi_op_queue, unsigned int *count_p);

/**
 * Poll from CQ (FI_SOURCE not supported).
 */
static na_return_t
na_ofi_cq_poll_no_source(struct na_ofi_class *na_ofi_class,
    struct na_ofi_context *na_ofi_context, unsigned int *count_p);

/**
 * Poll from CQ (FI_SOURCE supported).
 */
static na_return_t
na_ofi_cq_poll_fi_source(struct na_ofi_class *na_ofi_class,
    struct na_ofi_context *na_ofi_context, unsigned int *count_p);

/**
 * Read from CQ (FI_SOURCE not supported).
 */
static na_return_t
na_ofi_cq_read(struct fid_cq *cq, struct fi_cq_tagged_entry cq_events[],
    unsigned int max_count, unsigned int *count_p, bool *err_avail_p);

/**
 * Read from CQ (FI_SOURCE supported).
 */
static na_return_t
na_ofi_cq_readfrom(struct fid_cq *cq, struct fi_cq_tagged_entry cq_events[],
    unsigned int max_count, fi_addr_t *src_addrs, unsigned int *count_p,
    bool *err_avail_p);

/**
 * Read from error CQ.
 */
static na_return_t
na_ofi_cq_readerr(struct fid_cq *cq, struct fi_cq_tagged_entry *cq_event,
    struct na_ofi_src_err *src_err, unsigned int *count_p);

/**
 * Retrieve source address of unexpected messages.
 */
static na_return_t
na_ofi_cq_process_src_addr(struct na_ofi_class *na_ofi_class,
    fi_addr_t src_addr, struct na_ofi_src_err *src_err,
    struct na_ofi_addr **na_ofi_addr_p);

/**
 * Retrieve source address of unexpected messages (FI_SOURCE supported).
 */
static na_return_t
na_ofi_cq_process_fi_src_addr(struct na_ofi_class *na_ofi_class,
    fi_addr_t src_addr, struct na_ofi_addr **na_ofi_addr_p);

/**
 * Retrieve source address of unexpected messages (FI_SOURCE_ERR supported).
 */
static na_return_t
na_ofi_cq_process_fi_src_err(struct na_ofi_class *na_ofi_class,
    struct na_ofi_src_err *src_err, struct na_ofi_addr **na_ofi_addr_p);

/**
 * Retrieve source address of unexpected messages (FI_SOURCE not supported).
 */
static na_return_t
na_ofi_cq_process_raw_src_addr(struct na_ofi_class *na_ofi_class,
    const void *buf, size_t len, struct na_ofi_addr **na_ofi_addr_p);

/**
 * Process event from CQ.
 */
static na_return_t
na_ofi_cq_process_event(struct na_ofi_class *na_ofi_class,
    const struct fi_cq_tagged_entry *cq_event, struct na_ofi_addr *na_ofi_addr);

/**
 * Recv unexpected operation events.
 */
static NA_INLINE na_return_t
na_ofi_cq_process_recv_unexpected(struct na_ofi_class *na_ofi_class,
    const struct na_ofi_msg_info *msg_info,
    struct na_cb_info_recv_unexpected *recv_unexpected_info, void *buf,
    size_t len, struct na_ofi_addr *na_ofi_addr, uint64_t tag);

/**
 * Multi-recv unexpected operation events.
 */
static NA_INLINE na_return_t
na_ofi_cq_process_multi_recv_unexpected(struct na_ofi_class *na_ofi_class,
    const struct na_ofi_msg_info *msg_info,
    struct na_cb_info_multi_recv_unexpected *multi_recv_unexpected_info,
    void *buf, size_t len, struct na_ofi_addr *na_ofi_addr, uint64_t tag,
    bool last);

/**
 * Recv expected operation events.
 */
static NA_INLINE na_return_t
na_ofi_cq_process_recv_expected(const struct na_ofi_msg_info *msg_info,
    struct na_cb_info_recv_expected *recv_expected_info, void *buf, size_t len,
    uint64_t tag);

/**
 * Process retries.
 */
static na_return_t
na_ofi_cq_process_retries(
    struct na_ofi_context *na_ofi_context, unsigned retry_period_ms);

/**
 * Push op for retry.
 */
static void
na_ofi_op_retry(struct na_ofi_context *na_ofi_context, unsigned int timeout_ms,
    struct na_ofi_op_id *na_ofi_op_id);

/**
 * Abort all operations targeted at fi_addr.
 */
static void
na_ofi_op_retry_abort_addr(
    struct na_ofi_context *na_ofi_context, fi_addr_t fi_addr, na_return_t ret);

/**
 * Complete operation ID.
 */
static NA_INLINE void
na_ofi_op_complete_single(
    struct na_ofi_op_id *na_ofi_op_id, bool complete, na_return_t cb_ret);

/**
 * Release OP ID resources.
 */
static NA_INLINE void
na_ofi_op_release_single(void *arg);

/**
 * Complete operation ID.
 */
static void
na_ofi_op_complete_multi(
    struct na_ofi_op_id *na_ofi_op_id, bool complete, na_return_t cb_ret);

/**
 * Release OP ID resources.
 */
static NA_INLINE void
na_ofi_op_release_multi(void *arg);

/**
 * Cancel OP ID.
 */
static na_return_t
na_ofi_op_cancel(struct na_ofi_op_id *na_ofi_op_id);

/**
 * Init ring-buffer to hold multi CQ events.
 */
static na_return_t
na_ofi_completion_multi_init(
    struct na_ofi_completion_multi *completion_multi, uint32_t size);

/**
 * Destroy ring-buffer to hold multi CQ events.
 */
static void
na_ofi_completion_multi_destroy(
    struct na_ofi_completion_multi *completion_multi);

/**
 * Reserve entry to hold CQ data.
 */
static struct na_cb_completion_data *
na_ofi_completion_multi_push(struct na_ofi_completion_multi *completion_multi);

/**
 * Release last entry from ring-buffer.
 */
static void
na_ofi_completion_multi_pop(struct na_ofi_completion_multi *completion_multi);

/**
 * Determine whether ring-buffer is empty.
 */
/*
static NA_INLINE bool
na_ofi_completion_multi_empty(struct na_ofi_completion_multi *completion_multi);
*/

/**
 * Determine number of entries in the ring-buffer.
 */
static NA_INLINE unsigned int
na_ofi_completion_multi_count(struct na_ofi_completion_multi *completion_multi);

/********************/
/* Plugin callbacks */
/********************/

/* get_protocol_info */
static na_return_t
na_ofi_get_protocol_info(const struct na_info *na_info,
    struct na_protocol_info **na_protocol_info_p);

/* check_protocol */
static bool
na_ofi_check_protocol(const char *protocol_name);

/* initialize */
static na_return_t
na_ofi_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool listen);

/* finalize */
static na_return_t
na_ofi_finalize(na_class_t *na_class);

/* has_opt_feature */
static bool
na_ofi_has_opt_feature(na_class_t *na_class, unsigned long flags);

/* context_create */
static na_return_t
na_ofi_context_create(na_class_t *na_class, void **context_p, uint8_t id);

/* context_destroy */
static na_return_t
na_ofi_context_destroy(na_class_t *na_class, void *context);

/* op_create */
static na_op_id_t *
na_ofi_op_create(na_class_t *na_class, unsigned long flags);

/* op_destroy */
static void
na_ofi_op_destroy(na_class_t *na_class, na_op_id_t *op_id);

/* addr_lookup */
static na_return_t
na_ofi_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr_p);

/* addr_free */
static NA_INLINE void
na_ofi_addr_free(na_class_t *na_class, na_addr_t *addr);

/* addr_set_remove */
static NA_INLINE na_return_t
na_ofi_addr_set_remove(na_class_t *na_class, na_addr_t *addr);

/* addr_self */
static NA_INLINE na_return_t
na_ofi_addr_self(na_class_t *na_class, na_addr_t **addr_p);

/* addr_dup */
static NA_INLINE na_return_t
na_ofi_addr_dup(na_class_t *na_class, na_addr_t *addr, na_addr_t **new_addr_p);

/* addr_dup */
static bool
na_ofi_addr_cmp(na_class_t *na_class, na_addr_t *addr1, na_addr_t *addr2);

/* addr_is_self */
static NA_INLINE bool
na_ofi_addr_is_self(na_class_t *na_class, na_addr_t *addr);

/* addr_to_string */
static na_return_t
na_ofi_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr);

/* addr_get_serialize_size */
static NA_INLINE size_t
na_ofi_addr_get_serialize_size(na_class_t *na_class, na_addr_t *addr);

/* addr_serialize */
static na_return_t
na_ofi_addr_serialize(
    na_class_t *na_class, void *buf, size_t buf_size, na_addr_t *addr);

/* addr_deserialize */
static na_return_t
na_ofi_addr_deserialize(
    na_class_t *na_class, na_addr_t **addr_p, const void *buf, size_t buf_size);

/* msg_get_max_unexpected_size */
static NA_INLINE size_t
na_ofi_msg_get_max_unexpected_size(const na_class_t *na_class);

/* msg_get_max_expected_size */
static NA_INLINE size_t
na_ofi_msg_get_max_expected_size(const na_class_t *na_class);

/* msg_get_unexpected_header_size */
static NA_INLINE size_t
na_ofi_msg_get_unexpected_header_size(const na_class_t *na_class);

/* msg_get_max_tag */
static NA_INLINE na_tag_t
na_ofi_msg_get_max_tag(const na_class_t *na_class);

/* msg_buf_alloc */
static void *
na_ofi_msg_buf_alloc(na_class_t *na_class, size_t size, unsigned long flags,
    void **plugin_data_p);

/* msg_buf_free */
static void
na_ofi_msg_buf_free(na_class_t *na_class, void *buf, void *plugin_data);

/* msg_init_unexpected */
static na_return_t
na_ofi_msg_init_unexpected(na_class_t *na_class, void *buf, size_t buf_size);

/* msg_send_unexpected */
static na_return_t
na_ofi_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_unexpected */
static na_return_t
na_ofi_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id);

/* msg_multi_recv_unexpected */
static na_return_t
na_ofi_msg_multi_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id);

/* msg_send_expected */
static na_return_t
na_ofi_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id);

/* msg_recv_expected */
static na_return_t
na_ofi_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_addr_t *source_addr, uint8_t source_id, na_tag_t tag, na_op_id_t *op_id);

/* mem_handle */
static na_return_t
na_ofi_mem_handle_create(na_class_t *na_class, void *buf, size_t buf_size,
    unsigned long flags, na_mem_handle_t **mem_handle_p);

static na_return_t
na_ofi_mem_handle_create_segments(na_class_t *na_class,
    struct na_segment *segments, size_t segment_count, unsigned long flags,
    na_mem_handle_t **mem_handle_p);

static void
na_ofi_mem_handle_free(na_class_t *na_class, na_mem_handle_t *mem_handle);

static NA_INLINE size_t
na_ofi_mem_handle_get_max_segments(const na_class_t *na_class);

static na_return_t
na_ofi_mem_register(na_class_t *na_class, na_mem_handle_t *mem_handle,
    enum na_mem_type mem_type, uint64_t device);

static na_return_t
na_ofi_mem_deregister(na_class_t *na_class, na_mem_handle_t *mem_handle);

/* mem_handle serialization */
static NA_INLINE size_t
na_ofi_mem_handle_get_serialize_size(
    na_class_t *na_class, na_mem_handle_t *mem_handle);

static na_return_t
na_ofi_mem_handle_serialize(na_class_t *na_class, void *buf, size_t buf_size,
    na_mem_handle_t *mem_handle);

static na_return_t
na_ofi_mem_handle_deserialize(na_class_t *na_class,
    na_mem_handle_t **mem_handle_p, const void *buf, size_t buf_size);

/* put */
static na_return_t
na_ofi_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* get */
static na_return_t
na_ofi_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id,
    na_op_id_t *op_id);

/* poll_get_fd */
static NA_INLINE int
na_ofi_poll_get_fd(na_class_t *na_class, na_context_t *context);

/* poll_try_wait */
static NA_INLINE bool
na_ofi_poll_try_wait(na_class_t *na_class, na_context_t *context);

/* poll */
static na_return_t
na_ofi_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p);

/* poll_wait */
static na_return_t
na_ofi_poll_wait(na_class_t *na_class, na_context_t *context,
    unsigned int timeout, unsigned int *count_p);

/* cancel */
static na_return_t
na_ofi_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t *op_id);

/*******************/
/* Local Variables */
/*******************/

NA_PLUGIN const struct na_class_ops NA_PLUGIN_OPS(ofi) = {
    NA_OFI_CLASS_NAME,                     /* name */
    na_ofi_get_protocol_info,              /* get_protocol_info */
    na_ofi_check_protocol,                 /* check_protocol */
    na_ofi_initialize,                     /* initialize */
    na_ofi_finalize,                       /* finalize */
    NULL,                                  /* cleanup */
    na_ofi_has_opt_feature,                /* has_opt_feature */
    na_ofi_context_create,                 /* context_create */
    na_ofi_context_destroy,                /* context_destroy */
    na_ofi_op_create,                      /* op_create */
    na_ofi_op_destroy,                     /* op_destroy */
    na_ofi_addr_lookup,                    /* addr_lookup */
    na_ofi_addr_free,                      /* addr_free */
    na_ofi_addr_set_remove,                /* addr_set_remove */
    na_ofi_addr_self,                      /* addr_self */
    na_ofi_addr_dup,                       /* addr_dup */
    na_ofi_addr_cmp,                       /* addr_cmp */
    na_ofi_addr_is_self,                   /* addr_is_self */
    na_ofi_addr_to_string,                 /* addr_to_string */
    na_ofi_addr_get_serialize_size,        /* addr_get_serialize_size */
    na_ofi_addr_serialize,                 /* addr_serialize */
    na_ofi_addr_deserialize,               /* addr_deserialize */
    na_ofi_msg_get_max_unexpected_size,    /* msg_get_max_unexpected_size */
    na_ofi_msg_get_max_expected_size,      /* msg_get_max_expected_size */
    na_ofi_msg_get_unexpected_header_size, /* msg_get_unexpected_header_size */
    NULL,                                  /* msg_get_expected_header_size */
    na_ofi_msg_get_max_tag,                /* msg_get_max_tag */
    na_ofi_msg_buf_alloc,                  /* msg_buf_alloc */
    na_ofi_msg_buf_free,                   /* msg_buf_free */
    na_ofi_msg_init_unexpected,            /* msg_init_unexpected */
    na_ofi_msg_send_unexpected,            /* msg_send_unexpected */
    na_ofi_msg_recv_unexpected,            /* msg_recv_unexpected */
    na_ofi_msg_multi_recv_unexpected,      /* msg_multi_recv_unexpected */
    NULL,                                  /* msg_init_expected */
    na_ofi_msg_send_expected,              /* msg_send_expected */
    na_ofi_msg_recv_expected,              /* msg_recv_expected */
    na_ofi_mem_handle_create,              /* mem_handle_create */
    na_ofi_mem_handle_create_segments,     /* mem_handle_create_segment */
    na_ofi_mem_handle_free,                /* mem_handle_free */
    na_ofi_mem_handle_get_max_segments,    /* mem_handle_get_max_segments */
    na_ofi_mem_register,                   /* mem_register */
    na_ofi_mem_deregister,                 /* mem_deregister */
    na_ofi_mem_handle_get_serialize_size,  /* mem_handle_get_serialize_size */
    na_ofi_mem_handle_serialize,           /* mem_handle_serialize */
    na_ofi_mem_handle_deserialize,         /* mem_handle_deserialize */
    na_ofi_put,                            /* put */
    na_ofi_get,                            /* get */
    na_ofi_poll_get_fd,                    /* poll_get_fd */
    na_ofi_poll_try_wait,                  /* poll_try_wait */
    na_ofi_poll,                           /* poll */
    na_ofi_poll_wait,                      /* poll_wait */
    na_ofi_cancel                          /* cancel */
};

/* Fabric list */
static SLIST_HEAD(,
    na_ofi_fabric) na_ofi_fabric_list_g = SLIST_HEAD_INITIALIZER(na_ofi_fabric);

/* Fabric list lock */
static hg_thread_mutex_t na_ofi_fabric_list_mutex_g =
    HG_THREAD_MUTEX_INITIALIZER;

#if !defined(_WIN32) && FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 16))
/* Custom log */
static struct fid_logging na_ofi_log_fid_g = {.ops = NULL};
static struct fi_ops_log na_ofi_import_log_ops_g = {
    .size = sizeof(struct fi_ops),
    .enabled = na_ofi_log_enabled,
    .ready = na_ofi_log_ready,
    .log = na_ofi_log,
};

/* OFI log subsys names */
static const char *const na_ofi_log_subsys_g[] = {[FI_LOG_CORE] = "core",
    [FI_LOG_FABRIC] = "fabric",
    [FI_LOG_DOMAIN] = "domain",
    [FI_LOG_EP_CTRL] = "ep_ctrl",
    [FI_LOG_EP_DATA] = "ep_data",
    [FI_LOG_AV] = "av",
    [FI_LOG_CQ] = "cq",
    [FI_LOG_EQ] = "eq",
    [FI_LOG_MR] = "mr",
    [FI_LOG_CNTR] = "cntr",
    [FI_LOG_SUBSYS_MAX] = NULL};

/* Log interval in ms */
static int na_ofi_log_interval_g = 2000;

/* Initialize list of plugins etc */
static void
na_ofi_log_import(void) NA_CONSTRUCTOR;
static void
na_ofi_log_close(void) NA_DESTRUCTOR;

/*---------------------------------------------------------------------------*/
static void
na_ofi_log_import(void)
{
    /* Only import log if FI_LOG_LEVEL is not set */
    if (getenv("FI_LOG_LEVEL") == NULL) {
        int rc;

        if (FI_VERSION_LT(fi_version(), FI_VERSION(1, 16))) {
            NA_LOG_SUBSYS_WARNING(
                cls, "libfabric version < 1.16, log redirection not supported");
            return;
        }

        na_ofi_log_fid_g.ops = &na_ofi_import_log_ops_g;
        rc = fi_import_log(NA_OFI_VERSION, 0, &na_ofi_log_fid_g);
        NA_CHECK_SUBSYS_ERROR_NORET(cls, rc != 0, error,
            "fi_import_log() failed, rc: %d (%s)", rc, fi_strerror(-rc));
    }

    return;

error:
    na_ofi_log_fid_g.ops = NULL;
    return;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_log_close(void)
{
    if (na_ofi_log_fid_g.ops != NULL) {
        int rc = fi_close(&na_ofi_log_fid_g.fid);
        na_ofi_log_fid_g.ops = NULL;
        NA_CHECK_SUBSYS_ERROR_NORET(cls, rc != 0, error,
            "fi_close() logging failed, rc: %d (%s)", rc, fi_strerror(-rc));
    }

    return;

error:
    return;
}

/*---------------------------------------------------------------------------*/
static int
na_ofi_log_enabled(const struct fi_provider NA_UNUSED *prov,
    enum fi_log_level level, enum fi_log_subsys NA_UNUSED subsys,
    uint64_t NA_UNUSED flags)
{
    /* We do not filter on libfabric subsystems at the moment */
    return HG_LOG_OUTLET(na_libfabric).level >= na_ofi_log_level_to_hg(level);
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_log(const struct fi_provider *prov, enum fi_log_level level,
    enum fi_log_subsys subsys, const char *func, int line, const char *msg)
{
    HG_LOG_WRITE_FUNC(na_libfabric, na_ofi_log_level_to_hg(level), prov->name,
        na_ofi_log_subsys_g[subsys], (unsigned int) line, func, true, "%s",
        msg);
}

/*---------------------------------------------------------------------------*/
static int
na_ofi_log_ready(const struct fi_provider *prov, enum fi_log_level level,
    enum fi_log_subsys subsys, uint64_t flags, uint64_t *showtime)
{
    if (na_ofi_log_enabled(prov, level, subsys, flags)) {
        hg_time_t tv;
        uint64_t cur;

        hg_time_get_current_ms(&tv);
        cur = (uint64_t) hg_time_to_ms(tv);
        if (cur >= *showtime) {
            *showtime = cur + (uint64_t) na_ofi_log_interval_g;
            return true;
        }
    }

    return false;
}

/*---------------------------------------------------------------------------*/
static enum hg_log_level
na_ofi_log_level_to_hg(enum fi_log_level level)
{
    switch (level) {
        case FI_LOG_WARN:
            return HG_LOG_LEVEL_WARNING;
        case FI_LOG_TRACE:
        case FI_LOG_INFO:
        case FI_LOG_DEBUG:
            return HG_LOG_LEVEL_DEBUG;
        case FI_LOG_MAX:
        default:
            return HG_LOG_LEVEL_MAX;
    }
}

#endif

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_errno_to_na(int rc)
{
    na_return_t ret;

    switch (rc) {
        case FI_EPERM:
            ret = NA_PERMISSION;
            break;
        case FI_ENOENT:
            ret = NA_NOENTRY;
            break;
        case FI_EINTR:
            ret = NA_INTERRUPT;
            break;
        case FI_EIO:
#if !defined(__APPLE__)
        case FI_EREMOTEIO:
#endif
            ret = NA_IO_ERROR;
            break;
        case FI_EAGAIN:
#ifdef _WIN32
        case FI_EWOULDBLOCK:
#endif
            ret = NA_AGAIN;
            break;
        case FI_ENOMEM:
        case FI_EMFILE:
        case FI_ENOSPC:
        case FI_ENOBUFS:
            ret = NA_NOMEM;
            break;
        case FI_EACCES:
#if !defined(_WIN32) && !defined(__APPLE__)
        case FI_EKEYREJECTED:
#endif
            ret = NA_ACCESS;
            break;
        case FI_EFAULT:
            ret = NA_FAULT;
            break;
        case FI_EBUSY:
            ret = NA_BUSY;
            break;
        case FI_ENODEV:
            ret = NA_NODEV;
            break;
        case FI_E2BIG:
        case FI_EBADF:
        case FI_EINVAL:
            ret = NA_INVALID_ARG;
            break;
        case FI_EOVERFLOW:
            ret = NA_OVERFLOW;
            break;
        case FI_EMSGSIZE:
            ret = NA_MSGSIZE;
            break;
        case FI_ENOPROTOOPT:
        case FI_ENOSYS:
            ret = NA_PROTONOSUPPORT;
            break;
        case FI_EOPNOTSUPP:
            ret = NA_OPNOTSUPPORTED;
            break;
        case FI_EADDRINUSE:
            ret = NA_ADDRINUSE;
            break;
        case FI_EADDRNOTAVAIL:
            ret = NA_ADDRNOTAVAIL;
            break;
        case FI_ENETDOWN:
        case FI_ENETUNREACH:
        case FI_ECONNABORTED:
        case FI_ECONNRESET:
        case FI_ENOTCONN:
        case FI_ESHUTDOWN:
        case FI_ECONNREFUSED:
        case FI_EHOSTDOWN:
        case FI_EHOSTUNREACH:
            ret = NA_HOSTUNREACH;
            break;
        case FI_ETIMEDOUT:
            ret = NA_TIMEOUT;
            break;
        case FI_ECANCELED:
            ret = NA_CANCELED;
            break;
        case FI_ENOMSG:
        case FI_ENODATA:
        /* In practice the following codes are not errors but treat them as is
         * in this routine. */
        case FI_EISCONN:
        case FI_EALREADY:
        case FI_EINPROGRESS:
        default:
            ret = NA_PROTOCOL_ERROR;
            break;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE enum na_ofi_prov_type
na_ofi_prov_name_to_type(const char *prov_name)
{
    enum na_ofi_prov_type i = 0;

    if (FI_VERSION_LT(fi_version(), FI_VERSION(1, 18)) &&
        (strcmp(prov_name, "tcp") == 0)) {
        prov_name = "tcp;ofi_rxm";
        NA_LOG_SUBSYS_WARNING(
            cls, "Requested \"tcp\" provider, defaulting to \"%s\"", prov_name);
    }

    while (strcmp(na_ofi_prov_name[i], prov_name) &&
           strcmp(na_ofi_prov_alt_name[i], prov_name) && i != NA_OFI_PROV_MAX) {
        i++;
    }

    return ((i == NA_OFI_PROV_MAX) ? NA_OFI_PROV_NULL : i);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint32_t
na_ofi_tclass(enum na_traffic_class traffic_class)
{
    switch (traffic_class) {
        case NA_TC_BEST_EFFORT:
            return FI_TC_BEST_EFFORT;
        case NA_TC_LOW_LATENCY:
            return FI_TC_LOW_LATENCY;
        case NA_TC_BULK_DATA:
            return FI_TC_BULK_DATA;
        case NA_TC_DEDICATED_ACCESS:
            return FI_TC_DEDICATED_ACCESS;
        case NA_TC_SCAVENGER:
            return FI_TC_SCAVENGER;
        case NA_TC_NETWORK_CTRL:
            return FI_TC_NETWORK_CTRL;
        case NA_TC_UNSPEC:
            return FI_TC_UNSPEC;
        default:
            NA_LOG_SUBSYS_ERROR(fatal, "Unsupported traffic class");
            return FI_TC_UNSPEC;
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_prov_addr_format(
    enum na_ofi_prov_type prov_type, enum na_addr_format na_init_format)
{
    switch (na_init_format) {
        case NA_ADDR_IPV4:
            return FI_SOCKADDR_IN;
        case NA_ADDR_IPV6:
            return FI_SOCKADDR_IN6;
        case NA_ADDR_NATIVE:
            return na_ofi_prov_addr_format_native[prov_type];
        case NA_ADDR_UNSPEC:
            return na_ofi_prov_addr_format_pref[prov_type];
        default:
            NA_LOG_SUBSYS_ERROR(fatal, "Unsupported address format");
            return FI_FORMAT_UNSPEC;
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_prov_addr_size(int addr_format)
{
    switch (addr_format) {
        case FI_SOCKADDR_IN:
            return sizeof(struct sockaddr_in);
        case FI_SOCKADDR_IN6:
            return sizeof(struct sockaddr_in6);
        case FI_SOCKADDR_IB:
            return sizeof(struct na_ofi_sockaddr_ib);
        case FI_ADDR_PSMX:
            return sizeof(struct na_ofi_psm_addr);
        case FI_ADDR_PSMX2:
            return sizeof(struct na_ofi_psm2_addr);
        case FI_ADDR_OPX:
            return sizeof(struct na_ofi_opx_addr);
        case FI_ADDR_GNI:
            return sizeof(struct na_ofi_gni_addr);
        case FI_ADDR_CXI:
            return sizeof(struct na_ofi_cxi_addr);
        case FI_ADDR_STR:
            return sizeof(struct na_ofi_str_addr);
        default:
            NA_LOG_SUBSYS_ERROR(fatal, "Unsupported address format");
            return 0;
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE enum na_ofi_prov_type
na_ofi_addr_prov(const char *str)
{
    char fmt[19];
    int ret;

    ret = sscanf(str, "%16[^:]://", fmt);
    if (ret != 1)
        return NA_OFI_PROV_NULL;

    fmt[sizeof(fmt) - 1] = '\0';

    return na_ofi_prov_name_to_type(fmt);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ofi_str_to_raw_addr(
    const char *str, int addr_format, union na_ofi_raw_addr *addr)
{
    switch (addr_format) {
        case FI_SOCKADDR_IN:
            return na_ofi_str_to_sin(str, &addr->sin);
        case FI_SOCKADDR_IN6:
            return na_ofi_str_to_sin6(str, &addr->sin6);
        case FI_SOCKADDR_IB:
            return na_ofi_str_to_sib(str, &addr->sib);
        case FI_ADDR_PSMX:
            return na_ofi_str_to_psm(str, &addr->psm);
        case FI_ADDR_PSMX2:
            return na_ofi_str_to_psm2(str, &addr->psm2);
        case FI_ADDR_OPX:
            return na_ofi_str_to_opx(str, &addr->opx);
        case FI_ADDR_GNI:
            return na_ofi_str_to_gni(str, &addr->gni);
        case FI_ADDR_CXI:
            return na_ofi_str_to_cxi(str, &addr->cxi);
        case FI_ADDR_STR:
            return na_ofi_str_to_str(str, &addr->str);
        default:
            NA_LOG_SUBSYS_ERROR(
                fatal, "Unsupported address format: %d", addr_format);
            return NA_PROTONOSUPPORT;
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_sin(const char *str, struct sockaddr_in *sin_addr)
{
    char ip[17];
    na_return_t ret;

    sin_addr->sin_family = AF_INET;
    if (sscanf(str, "%*[^:]://:%" SCNu16, &sin_addr->sin_port) == 1) {
        NA_LOG_SUBSYS_DEBUG(addr, "port=%" PRIu16, sin_addr->sin_port);
    } else if (sscanf(str, "%*[^:]://%16[^:]:%" SCNu16, ip,
                   &sin_addr->sin_port) > 0) {
        int rc;

        ip[sizeof(ip) - 1] = '\0';
        rc = inet_pton(AF_INET, ip, &sin_addr->sin_addr);
        NA_CHECK_SUBSYS_ERROR(addr, rc != 1, error, ret, NA_PROTONOSUPPORT,
            "Unable to convert IPv4 address: %s", ip);
        NA_LOG_SUBSYS_DEBUG(
            addr, "ip=%s, port=%" PRIu16, ip, sin_addr->sin_port);
    } else
        NA_GOTO_SUBSYS_ERROR(addr, error, ret, NA_PROTONOSUPPORT,
            "Malformed FI_ADDR_STR: %s", str);

    sin_addr->sin_port = htons(sin_addr->sin_port);
    /* Make sure `sin_zero` is set to 0 */
    memset(&sin_addr->sin_zero, 0, sizeof(sin_addr->sin_zero));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_sin6(const char *str, struct sockaddr_in6 *sin6_addr)
{
    char ip[65];
    na_return_t ret;

    /* Make sure unused fields are set to 0 */
    memset(sin6_addr, 0, sizeof(*sin6_addr));

    sin6_addr->sin6_family = AF_INET6;
    if (sscanf(str, "%*[^:]://:%" SCNu16, &sin6_addr->sin6_port) == 1) {
        NA_LOG_SUBSYS_DEBUG(addr, "port=%" PRIu16, sin6_addr->sin6_port);
    } else if (sscanf(str, "%*[^:]://[%64[^]]]:%" SCNu16, ip,
                   &sin6_addr->sin6_port) > 0) {
        int rc;

        ip[sizeof(ip) - 1] = '\0';
        rc = inet_pton(AF_INET6, ip, &sin6_addr->sin6_addr);
        NA_CHECK_SUBSYS_ERROR(addr, rc != 1, error, ret, NA_PROTONOSUPPORT,
            "Unable to convert IPv6 address: %s", ip);
        NA_LOG_SUBSYS_DEBUG(
            addr, "ip=%s, port=%" PRIu16, ip, sin6_addr->sin6_port);
    } else
        NA_GOTO_SUBSYS_ERROR(addr, error, ret, NA_PROTONOSUPPORT,
            "Malformed FI_ADDR_STR: %s", str);

    sin6_addr->sin6_port = htons(sin6_addr->sin6_port);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_sib(const char *str, struct na_ofi_sockaddr_ib *sib_addr)
{
    char *tok, *endptr, *saveptr;
    uint16_t pkey, ps, port;
    uint64_t scope_id;
    char gid[64 + 1];
    char extra_str[64 + 1];
    na_return_t ret;
    int rc;

    memset(gid, 0, sizeof(gid));

    rc = sscanf(str,
        "%*[^:]://[%64[^]]]" /* GID */
        ":%64s",             /* P_Key : port_space : Scope ID : port */
        gid, extra_str);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 2, error, ret, NA_PROTONOSUPPORT,
        "Invalid GID in address: %s", str);

    tok = strtok_r(extra_str, ":", &saveptr);
    NA_CHECK_SUBSYS_ERROR(addr, tok == NULL, error, ret, NA_PROTONOSUPPORT,
        "Invalid pkey in address: %s", str);

    pkey = strtoul(tok, &endptr, 0) & 0xffff;
    NA_CHECK_SUBSYS_ERROR(addr, !pkey, error, ret, NA_PROTONOSUPPORT,
        "Invalid pkey in address: %s", str);

    tok = strtok_r(NULL, ":", &saveptr);
    NA_CHECK_SUBSYS_ERROR(addr, tok == NULL, error, ret, NA_PROTONOSUPPORT,
        "Invalid port space in address: %s", str);

    ps = strtoul(tok, &endptr, 0) & 0xffff;
    NA_CHECK_SUBSYS_ERROR(addr, *endptr, error, ret, NA_PROTONOSUPPORT,
        "Invalid port space in address: %s", str);

    tok = strtok_r(NULL, ":", &saveptr);
    NA_CHECK_SUBSYS_ERROR(addr, tok == NULL, error, ret, NA_PROTONOSUPPORT,
        "Invalid scope id in address: %s", str);

    scope_id = strtoul(tok, &endptr, 0);
    NA_CHECK_SUBSYS_ERROR(addr, *endptr, error, ret, NA_PROTONOSUPPORT,
        "Invalid scope id in address: %s", str);

    /* Port is optional */
    tok = strtok_r(NULL, ":", &saveptr);
    if (tok)
        port = strtoul(tok, &endptr, 0) & 0xffff;
    else
        port = 0;

    /* Make sure unused fields are set to 0 */
    memset(sib_addr, 0, sizeof(*sib_addr));

    rc = inet_pton(AF_INET6, gid, &sib_addr->sib_addr);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, error, ret, NA_PROTONOSUPPORT,
        "Unable to convert GID: %s", gid);

    sib_addr->sib_family = AF_IB;
    sib_addr->sib_pkey = htons(pkey);
    if (ps && port) {
        sib_addr->sib_sid = htonll(((uint64_t) ps << 16) + port);
        sib_addr->sib_sid_mask =
            htonll(NA_OFI_IB_IP_PS_MASK | NA_OFI_IB_IP_PORT_MASK);
    }
    sib_addr->sib_scope_id = htonll(scope_id);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_psm(const char *str, struct na_ofi_psm_addr *psm_addr)
{
    na_return_t ret;
    int rc;

    rc = sscanf(str, "%*[^:]://%" SCNx64, (uint64_t *) &psm_addr->addr0);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, error, ret, NA_PROTONOSUPPORT,
        "Could not convert addr string to PSM addr format");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_psm2(const char *str, struct na_ofi_psm2_addr *psm2_addr)
{
    na_return_t ret;
    int rc;

    rc = sscanf(str, "%*[^:]://%" SCNx64 ":%" SCNx64,
        (uint64_t *) &psm2_addr->addr0, (uint64_t *) &psm2_addr->addr1);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 2, error, ret, NA_PROTONOSUPPORT,
        "Could not convert addr string to PSM2 addr format");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_opx(const char *str, struct na_ofi_opx_addr *opx_addr)
{
    uint8_t rx_index, hfi1_rx, hfi1_unit, reliability_rx;
    uint16_t lid, endpoint_id;
    uint32_t key = 0, key_inst = 0;
    na_return_t ret;
    int rc;

    /* Format - FI_ADDRESS.inst.:key(a.k.a - OFI_PORT) */
    rc = sscanf(str,
        "%*[^:]://%04hx.%04hx.%02hhx.%02hhx.%02hhx.%02hhx.%04x:%" SCNu32, &lid,
        &endpoint_id, &rx_index, &hfi1_rx, &hfi1_unit, &reliability_rx,
        &key_inst, &key);
    NA_CHECK_SUBSYS_ERROR(addr, (rc != 6 && rc != 8), error, ret,
        NA_PROTONOSUPPORT, "Could not convert addr string to OPX addr format");

    *opx_addr = (struct na_ofi_opx_addr){.addr.uid.lid = lid,
        .addr.uid.endpoint_id = endpoint_id,
        .addr.rx_index = rx_index,
        .addr.hfi1_rx = hfi1_rx,
        .addr.hfi1_unit = hfi1_unit,
        .addr.reliability_rx = reliability_rx,
        .key = key,
        .key_inst = key_inst};

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_gni(const char *str, struct na_ofi_gni_addr *gni_addr)
{
    unsigned int version, name_type, rx_ctx_cnt;
    uint32_t device_addr, cdm_id, cm_nic_cdm_id, cookie;
    na_return_t ret;
    int rc;

    /* Make sure unused fields are set to 0 */
    memset(gni_addr, 0, sizeof(*gni_addr));

    rc = sscanf(str,
        "%*[^:]://%04u:0x%08" SCNx32 ":0x%08" SCNx32 ":%02u:0x%06" SCNx32
        ":0x%08" SCNx32 ":%02u",
        &version, &device_addr, &cdm_id, &name_type, &cm_nic_cdm_id, &cookie,
        &rx_ctx_cnt);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 7, error, ret, NA_PROTONOSUPPORT,
        "Could not convert addr string to GNI addr format");
    NA_CHECK_SUBSYS_ERROR(addr, version != NA_OFI_GNI_AV_STR_ADDR_VERSION,
        error, ret, NA_PROTONOSUPPORT, "Unsupported GNI string addr format");

    gni_addr->device_addr = device_addr;
    gni_addr->cdm_id = cdm_id;
    gni_addr->name_type = name_type & 0xff;
    gni_addr->cm_nic_cdm_id = cm_nic_cdm_id & 0xffffff;
    gni_addr->cookie = cookie;
    gni_addr->rx_ctx_cnt = rx_ctx_cnt & 0xff;
    NA_LOG_SUBSYS_DEBUG(addr,
        "GNI addr is: device_addr=%x, cdm_id=%x, name_type=%x, "
        "cm_nic_cdm_id=%x, cookie=%x, rx_ctx_cnt=%u",
        gni_addr->device_addr, gni_addr->cdm_id, gni_addr->name_type,
        gni_addr->cm_nic_cdm_id, gni_addr->cookie, gni_addr->rx_ctx_cnt);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_cxi(const char *str, struct na_ofi_cxi_addr *cxi_addr)
{
    na_return_t ret;
    int rc;

    /* Make sure unused fields are set to 0 */
    memset(cxi_addr, 0, sizeof(*cxi_addr));

    rc = sscanf(str, "%*[^:]://%" SCNx32, (uint32_t *) cxi_addr);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, error, ret, NA_PROTONOSUPPORT,
        "Could not convert addr string to CXI addr format");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_str_to_str(const char *str, struct na_ofi_str_addr *str_addr)
{
    na_return_t ret;
    int rc;

    rc = snprintf(str_addr->buf, sizeof(str_addr->buf), "fi_%s", str);
    NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > (int) sizeof(str_addr->buf),
        error, ret, NA_OVERFLOW,
        "snprintf() failed or name truncated, rc: %d (expected %zu)", rc,
        sizeof(str_addr->buf));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_raw_addr_to_key(int addr_format, const union na_ofi_raw_addr *addr)
{
    switch (addr_format) {
        case FI_SOCKADDR_IN:
            return na_ofi_sin_to_key(&addr->sin);
        case FI_SOCKADDR_IN6:
            return na_ofi_sin6_to_key(&addr->sin6);
        case FI_SOCKADDR_IB:
            return na_ofi_sib_to_key(&addr->sib);
        case FI_ADDR_PSMX:
            return na_ofi_psm_to_key(&addr->psm);
        case FI_ADDR_PSMX2:
            return na_ofi_psm2_to_key(&addr->psm2);
        case FI_ADDR_OPX:
            return na_ofi_opx_to_key(&addr->opx);
        case FI_ADDR_GNI:
            return na_ofi_gni_to_key(&addr->gni);
        case FI_ADDR_CXI:
            return na_ofi_cxi_to_key(&addr->cxi);
        case FI_ADDR_STR:
            return na_ofi_str_to_key(&addr->str);
        default:
            NA_LOG_SUBSYS_ERROR(fatal, "Unsupported address format");
            return 0;
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_sin_to_key(const struct sockaddr_in *addr)
{
    return ((uint64_t) addr->sin_addr.s_addr) << 32 | addr->sin_port;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_sin6_to_key(const struct sockaddr_in6 *addr)
{
    return *((const uint64_t *) &addr->sin6_addr.s6_addr);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_sib_to_key(const struct na_ofi_sockaddr_ib *addr)
{
    return *((const uint64_t *) &addr->sib_addr);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_psm_to_key(const struct na_ofi_psm_addr *addr)
{
    return addr->addr0;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_psm2_to_key(const struct na_ofi_psm2_addr *addr)
{
    /* Only need the psm2_epid, i.e. the first 64 bits */
    return addr->addr0;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_opx_to_key(const struct na_ofi_opx_addr *addr)
{
    uint64_t key;
    char opx_addr_str[NA_OFI_MAX_URI_LEN] = {'\0'};

    /* Format - FI_ADDRESS.inst:key(a.k.a - OFI_PORT) */
    snprintf(opx_addr_str, sizeof(opx_addr_str),
        "%04x.%04x.%02x.%02x.%02x.%02x.%04x:%" PRIu32, addr->addr.uid.lid,
        addr->addr.uid.endpoint_id, addr->addr.rx_index, addr->addr.hfi1_rx,
        addr->addr.hfi1_unit, addr->addr.reliability_rx, addr->key_inst,
        addr->key);
    key = (uint64_t) hg_hash_string(opx_addr_str);

    return key;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_gni_to_key(const struct na_ofi_gni_addr *addr)
{
    return ((uint64_t) addr->device_addr) << 32 | addr->cdm_id;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_cxi_to_key(const struct na_ofi_cxi_addr *addr)
{
    return (uint64_t) (*(const uint32_t *) addr);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE uint64_t
na_ofi_str_to_key(const struct na_ofi_str_addr *addr)
{
    return (uint64_t) hg_hash_string(addr->buf);
}

/*---------------------------------------------------------------------------*/
#ifdef NA_OFI_ADDR_OPT
static NA_INLINE void
na_ofi_key_to_sin(struct sockaddr_in *addr, uint64_t key)
{
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = (in_addr_t) (key >> 32);
    addr->sin_port = (in_port_t) (key & 0xffffffff);
    memset(&addr->sin_zero, 0, sizeof(addr->sin_zero));
}
#endif

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_raw_addr_serialize_size(int addr_format)
{
    switch (addr_format) {
        case FI_SOCKADDR_IN:
#ifdef NA_OFI_ADDR_OPT
            return sizeof(uint64_t);
#else
            return sizeof(struct sockaddr_in);
#endif
        case FI_SOCKADDR_IN6:
            return sizeof(struct in6_addr) + sizeof(in_port_t);
        case FI_SOCKADDR_IB:
            return sizeof(struct na_ofi_sockaddr_ib);
        case FI_ADDR_PSMX:
            return sizeof(struct na_ofi_psm_addr);
        case FI_ADDR_PSMX2:
            return sizeof(struct na_ofi_psm2_addr);
        case FI_ADDR_OPX:
            return sizeof(struct na_ofi_opx_addr);
        case FI_ADDR_GNI:
            return sizeof(struct na_ofi_gni_addr);
        case FI_ADDR_CXI:
            return sizeof(struct na_ofi_cxi_addr);
        case FI_ADDR_STR:
            return sizeof(struct na_ofi_str_addr);
        default:
            NA_LOG_SUBSYS_ERROR(fatal, "Unsupported address format");
            return 0;
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_raw_addr_serialize(int addr_format, void *buf, size_t buf_size,
    const union na_ofi_raw_addr *addr)
{
    na_return_t ret;

    switch (addr_format) {
        case FI_SOCKADDR_IN: {
#ifdef NA_OFI_ADDR_OPT
            uint64_t val = na_ofi_sin_to_key(&addr->sin);

            NA_CHECK_SUBSYS_ERROR(addr, buf_size < sizeof(val), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(buf, &val, sizeof(val));
#else
            NA_CHECK_SUBSYS_ERROR(addr, buf_size < sizeof(struct sockaddr_in),
                error, ret, NA_OVERFLOW,
                "Buffer size (%zu) too small to copy addr", buf_size);
            memcpy(buf, &addr->sin, sizeof(addr->sin));
#endif
            break;
        }
        case FI_SOCKADDR_IN6: {
            char *buf_ptr = (char *) buf;
            size_t buf_size_left = (size_t) buf_size;

            NA_ENCODE(error, ret, buf_ptr, buf_size_left, &addr->sin6.sin6_addr,
                struct in6_addr);
            NA_ENCODE(error, ret, buf_ptr, buf_size_left, &addr->sin6.sin6_port,
                in_port_t);
            break;
        }
        case FI_SOCKADDR_IB:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_sockaddr_ib), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(buf, &addr->sib, sizeof(addr->sib));
            break;
        case FI_ADDR_PSMX:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_psm_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(buf, &addr->psm, sizeof(addr->psm));
            break;
        case FI_ADDR_PSMX2:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_psm2_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(buf, &addr->psm2, sizeof(addr->psm2));
            break;
        case FI_ADDR_OPX:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_opx_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(buf, &addr->opx, sizeof(addr->opx));
            break;
        case FI_ADDR_GNI:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_gni_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(buf, &addr->gni, sizeof(addr->gni));
            break;
        case FI_ADDR_CXI:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_cxi_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(buf, &addr->cxi, sizeof(addr->cxi));
            break;
        case FI_ADDR_STR:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_str_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            strncpy(buf, addr->str.buf, sizeof(addr->str));
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(addr, error, ret, NA_PROTONOSUPPORT,
                "Unsupported address format");
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_raw_addr_deserialize(int addr_format, union na_ofi_raw_addr *addr,
    union na_ofi_auth_key *auth_key, const void *buf, size_t buf_size)
{
    na_return_t ret;

    switch (addr_format) {
        case FI_SOCKADDR_IN: {
#ifdef NA_OFI_ADDR_OPT
            uint64_t val;

            NA_CHECK_SUBSYS_ERROR(addr, buf_size < sizeof(val), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(&val, buf, sizeof(val));
            na_ofi_key_to_sin(&addr->sin, val);
#else
            NA_CHECK_SUBSYS_ERROR(addr, buf_size < sizeof(struct sockaddr_in),
                error, ret, NA_OVERFLOW,
                "Buffer size (%zu) too small to copy addr", buf_size);
            memcpy(&addr->sin, buf, sizeof(addr->sin));
#endif
            break;
        }
        case FI_SOCKADDR_IN6: {
            const char *buf_ptr = (const char *) buf;
            size_t buf_size_left = (size_t) buf_size;

            memset(&addr->sin6, 0, sizeof(addr->sin6));
            NA_DECODE(error, ret, buf_ptr, buf_size_left, &addr->sin6.sin6_addr,
                struct in6_addr);
            NA_DECODE(error, ret, buf_ptr, buf_size_left, &addr->sin6.sin6_port,
                in_port_t);
            break;
        }
        case FI_SOCKADDR_IB:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_sockaddr_ib), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(&addr->sib, buf, sizeof(addr->sib));
            break;
        case FI_ADDR_PSMX:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_psm_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(&addr->psm, buf, sizeof(addr->psm));
            break;
        case FI_ADDR_PSMX2:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_psm2_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(&addr->psm2, buf, sizeof(addr->psm2));
            break;
        case FI_ADDR_OPX:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_opx_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(&addr->opx, buf, sizeof(addr->opx));
            break;
        case FI_ADDR_GNI:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_gni_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(&addr->gni, buf, sizeof(addr->gni));
            break;
        case FI_ADDR_CXI:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_cxi_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            memcpy(&addr->cxi, buf, sizeof(addr->cxi));
#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
            auth_key->cxi_auth_key.vni = addr->cxi.vni;
#else
            (void) auth_key;
#endif
            break;
        case FI_ADDR_STR:
            NA_CHECK_SUBSYS_ERROR(addr,
                buf_size < sizeof(struct na_ofi_str_addr), error, ret,
                NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
                buf_size);
            strncpy(addr->str.buf, buf, sizeof(addr->str) - 1);
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(addr, error, ret, NA_PROTONOSUPPORT,
                "Unsupported address format");
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_key_lookup(struct na_ofi_class *na_ofi_class,
    struct na_ofi_addr_key *addr_key, fi_addr_t fi_auth_key,
    struct na_ofi_addr **na_ofi_addr_p)
{
    struct na_ofi_addr *na_ofi_addr = NULL;
    na_return_t ret;

    /* Lookup address */
    na_ofi_addr =
        na_ofi_addr_map_lookup(&na_ofi_class->domain->addr_map, addr_key);
    if (na_ofi_addr == NULL || ((fi_auth_key != FI_ADDR_NOTAVAIL) &&
                                   (na_ofi_addr->fi_auth_key != fi_auth_key))) {
        na_return_t na_ret;

        NA_LOG_SUBSYS_DEBUG(
            addr, "Address was not found, attempting to insert it");

        /* Insert new entry and create new address if needed */
        na_ret = na_ofi_addr_map_insert(na_ofi_class,
            &na_ofi_class->domain->addr_map, addr_key, fi_auth_key,
            &na_ofi_addr);
        NA_CHECK_SUBSYS_ERROR(addr, na_ret != NA_SUCCESS && na_ret != NA_EXIST,
            error, ret, na_ret, "Could not insert new address");
    }

    na_ofi_addr_ref_incr(na_ofi_addr);

    *na_ofi_addr_p = na_ofi_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_ofi_addr_key_hash(hg_hash_table_key_t key)
{
    return NA_OFI_HASH64(((struct na_ofi_addr_key *) key)->val);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_addr_key_equal_default(
    hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    /* Only when 64-bit unique keys can be generated */
    return ((struct na_ofi_addr_key *) key1)->val ==
           ((struct na_ofi_addr_key *) key2)->val;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_addr_key_equal_sin6(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    struct na_ofi_addr_key *addr_key1 = (struct na_ofi_addr_key *) key1,
                           *addr_key2 = (struct na_ofi_addr_key *) key2;

    if (addr_key1->addr.sin6.sin6_port != addr_key2->addr.sin6.sin6_port)
        return 0;

    return (
        memcmp(&addr_key1->addr.sin6.sin6_addr, &addr_key2->addr.sin6.sin6_addr,
            sizeof(addr_key1->addr.sin6.sin6_addr)) == 0);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_addr_key_equal_sib(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    struct na_ofi_addr_key *addr_key1 = (struct na_ofi_addr_key *) key1,
                           *addr_key2 = (struct na_ofi_addr_key *) key2;

    if (addr_key1->addr.sib.sib_pkey != addr_key2->addr.sib.sib_pkey ||
        addr_key1->addr.sib.sib_scope_id != addr_key2->addr.sib.sib_scope_id ||
        addr_key1->addr.sib.sib_sid != addr_key2->addr.sib.sib_sid)
        return 0;

    return (memcmp(&addr_key1->addr.sib.sib_addr, &addr_key2->addr.sib.sib_addr,
                sizeof(addr_key1->addr.sib.sib_addr)) == 0);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE struct na_ofi_addr *
na_ofi_addr_map_lookup(
    struct na_ofi_map *na_ofi_map, struct na_ofi_addr_key *addr_key)
{
    hg_hash_table_value_t value = NULL;

    /* Lookup key */
    hg_thread_rwlock_rdlock(&na_ofi_map->lock);
    value = hg_hash_table_lookup(
        na_ofi_map->key_map, (hg_hash_table_key_t) addr_key);
    hg_thread_rwlock_release_rdlock(&na_ofi_map->lock);

    return (value == HG_HASH_TABLE_NULL) ? NULL : (struct na_ofi_addr *) value;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_map_insert(struct na_ofi_class *na_ofi_class,
    struct na_ofi_map *na_ofi_map, struct na_ofi_addr_key *addr_key,
    fi_addr_t fi_auth_key, struct na_ofi_addr **na_ofi_addr_p)
{
    struct na_ofi_addr *na_ofi_addr = NULL;
    uint64_t flags = 0;
    na_return_t ret = NA_SUCCESS;
    bool addr_map_exist = false;
    int rc;

    hg_thread_rwlock_wrlock(&na_ofi_map->lock);

    /* Look up again to prevent race between lock release/acquire */
    na_ofi_addr = (struct na_ofi_addr *) hg_hash_table_lookup(
        na_ofi_map->key_map, (hg_hash_table_key_t) addr_key);
    if (na_ofi_addr != NULL) {
        if ((fi_auth_key == FI_ADDR_NOTAVAIL) ||
            (na_ofi_addr->fi_auth_key == fi_auth_key)) {
            ret = NA_EXIST; /* Entry already exists */
            goto out;
        } else {
            NA_LOG_SUBSYS_DEBUG(addr,
                "auth key for FI addr %" PRIu64
                " has changed, updating it to %" PRIu64,
                na_ofi_addr->fi_addr, fi_auth_key);

            /* If keys have changed, remove previously inserted address */
            rc = fi_av_remove(
                na_ofi_class->domain->fi_av, &na_ofi_addr->fi_addr, 1, 0);
            NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret,
                na_ofi_errno_to_na(-rc), "fi_av_remove() failed, rc: %d (%s)",
                rc, fi_strerror(-rc));
            addr_map_exist = true;
        }
    } else {
        /* Allocate address */
        ret = na_ofi_addr_create(na_ofi_class, addr_key, &na_ofi_addr);
        NA_CHECK_SUBSYS_NA_ERROR(
            addr, error, ret, "Could not allocate address");
    }

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    if (na_ofi_class->domain->av_auth_key) {
        /* Inserted address will be bound to previously inserted auth key,
         * if FI_ADDR_NOTAVAIL was passed, attempt to use base key
         * TODO we may want to properly retrieve the base key */
        if (fi_auth_key == FI_ADDR_NOTAVAIL) {
            NA_LOG_SUBSYS_DEBUG(addr, "Using default auth key for addr");
            na_ofi_addr->fi_auth_key = 0;
        } else
            na_ofi_addr->fi_auth_key = fi_auth_key;

        flags |= FI_AUTH_KEY;
        /* Input of fi_av_insert(), output will be actual fi_addr_t */
        na_ofi_addr->fi_addr = na_ofi_addr->fi_auth_key;
    } else if (na_ofi_class->domain->av_user_id) {
        flags |= FI_AV_USER_ID;
        /* Input of fi_av_insert(), output will be actual fi_addr_t */
        na_ofi_addr->fi_addr = (fi_addr_t) na_ofi_addr;
    }
#endif

    /* Insert addr into AV if key not found */
    rc = fi_av_insert(na_ofi_class->domain->fi_av, &na_ofi_addr->addr_key.addr,
        1, &na_ofi_addr->fi_addr, flags, NULL);
    NA_CHECK_SUBSYS_ERROR(addr, rc < 1, error, ret, na_ofi_errno_to_na(-rc),
        "fi_av_insert() failed, inserted: %d", rc);

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    if (na_ofi_class->domain->av_auth_key) {
        size_t addrlen = sizeof(na_ofi_addr->addr_key.addr);

        if (na_ofi_class->domain->av_user_id) {
            /* With FI_AV_USER_ID and auth_keys, set user id as struct addr
             * pointer. Addr pointer will be returned in
             * fi_cq_readfrom::src_addr. */
            rc = fi_av_set_user_id(na_ofi_class->domain->fi_av,
                na_ofi_addr->fi_addr, (fi_addr_t) na_ofi_addr, 0);
            NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret,
                na_ofi_errno_to_na(-rc),
                "fi_av_set_user_id() failed, rc: %d (%s)", rc,
                fi_strerror(-rc));
        }

        /* For providers w/ auth keys, do a reverse lookup to ensure addr is
         * fully populated */
        rc = fi_av_lookup(na_ofi_class->domain->fi_av, na_ofi_addr->fi_addr,
            &na_ofi_addr->addr_key.addr, &addrlen);
        NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret,
            na_ofi_errno_to_na(-rc),
            "fi_av_lookup() failed, rc: %d (%s), addrlen: %zu", rc,
            fi_strerror(-rc), addrlen);
    }
#endif

    NA_LOG_SUBSYS_DEBUG(
        addr, "Inserted new addr, FI addr is %" PRIu64, na_ofi_addr->fi_addr);

    /* Hash table entries should usually not exist unless we are just updating
     * the corresponding auth_key for the address */
    if (!addr_map_exist) {
        /* Insert new value to secondary map to look up by FI addr when
         * FI_SOURCE is used and prevent fi_av_lookup() followed by map lookup
         * call */
        if (na_ofi_map->fi_map != NULL) {
            rc = hg_hash_table_insert(na_ofi_map->fi_map,
                (hg_hash_table_key_t) &na_ofi_addr->fi_addr,
                (hg_hash_table_value_t) na_ofi_addr);
            NA_CHECK_SUBSYS_ERROR(addr, rc == 0, out, ret, NA_NOMEM,
                "hg_hash_table_insert() failed");
        }

        /* Insert new value to primary map */
        rc = hg_hash_table_insert(na_ofi_map->key_map,
            (hg_hash_table_key_t) &na_ofi_addr->addr_key,
            (hg_hash_table_value_t) na_ofi_addr);
        NA_CHECK_SUBSYS_ERROR(addr, rc == 0, error, ret, NA_NOMEM,
            "hg_hash_table_insert() failed");
    }

out:
    hg_thread_rwlock_release_wrlock(&na_ofi_map->lock);

    *na_ofi_addr_p = na_ofi_addr;

    return ret;

error:
    hg_thread_rwlock_release_wrlock(&na_ofi_map->lock);
    if (na_ofi_addr)
        na_ofi_addr_destroy(na_ofi_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_map_remove(
    struct na_ofi_map *na_ofi_map, struct na_ofi_addr_key *addr_key)
{
    struct na_ofi_addr *na_ofi_addr = NULL;
    na_return_t ret = NA_SUCCESS;
    int rc;

    hg_thread_rwlock_wrlock(&na_ofi_map->lock);

    na_ofi_addr = (struct na_ofi_addr *) hg_hash_table_lookup(
        na_ofi_map->key_map, (hg_hash_table_key_t) addr_key);
    if (na_ofi_addr == NULL)
        goto unlock;

    /* Remove addr key from primary map */
    rc = hg_hash_table_remove(
        na_ofi_map->key_map, (hg_hash_table_key_t) addr_key);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 1, unlock, ret, NA_NOENTRY,
        "hg_hash_table_remove() failed");

    /* Remove FI addr from secondary map */
    if (na_ofi_map->fi_map != NULL) {
        rc = hg_hash_table_remove(
            na_ofi_map->fi_map, (hg_hash_table_key_t) &na_ofi_addr->fi_addr);
        NA_CHECK_SUBSYS_ERROR(addr, rc != 1, unlock, ret, NA_NOENTRY,
            "hg_hash_table_remove() failed");
    }

    /* Remove address from AV */
    rc = fi_av_remove(na_ofi_addr->class->domain->fi_av, &na_ofi_addr->fi_addr,
        1, 0 /* flags */);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 0, unlock, ret, na_ofi_errno_to_na(-rc),
        "fi_av_remove() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    NA_LOG_SUBSYS_DEBUG(
        addr, "Removed addr for FI addr %" PRIu64, na_ofi_addr->fi_addr);

    na_ofi_addr->fi_addr = 0;

unlock:
    hg_thread_rwlock_release_wrlock(&na_ofi_map->lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_ofi_fi_addr_hash(hg_hash_table_key_t key)
{
    return NA_OFI_HASH64(*((fi_addr_t *) key));
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_fi_addr_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    return *((fi_addr_t *) key1) == *((fi_addr_t *) key2);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE struct na_ofi_addr *
na_ofi_fi_addr_map_lookup(struct na_ofi_map *na_ofi_map, fi_addr_t *fi_addr)
{
    hg_hash_table_value_t value = NULL;

    /* Lookup key */
    hg_thread_rwlock_rdlock(&na_ofi_map->lock);
    value =
        hg_hash_table_lookup(na_ofi_map->fi_map, (hg_hash_table_key_t) fi_addr);
    hg_thread_rwlock_release_rdlock(&na_ofi_map->lock);

    return (value == HG_HASH_TABLE_NULL) ? NULL : (struct na_ofi_addr *) value;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_provider_check(
    enum na_ofi_prov_type prov_type, const char *user_requested_protocol)
{
    int rc;
    struct fi_info *cur;
    struct fi_info *prev = NULL;
    size_t avail_len = 0;
    char *avail;
    struct fi_info *providers = NULL;

    /* Query OFI without hints to determine which providers are present. */
    rc = fi_getinfo(NA_OFI_VERSION, NULL, NULL, 0, NULL, &providers);
    if (rc != 0)
        return;

    /* look for match */
    for (cur = providers; cur; cur = cur->next) {
        if (!strcmp(cur->fabric_attr->prov_name, na_ofi_prov_name[prov_type])) {
            /* The provider is there at least; follow normal error
             * handling path rather than printing a special message.
             */
            fi_freeinfo(providers);
            return;
        }
        if (!prev ||
            strcmp(prev->fabric_attr->prov_name, cur->fabric_attr->prov_name)) {
            /* calculate how large of a string we need to hold
             * provider list for potential error message */
            avail_len += strlen(cur->fabric_attr->prov_name) + 1;
        }
        prev = cur;
    }

    prev = NULL;
    avail = calloc(avail_len + 1, 1);
    if (!avail) {
        /* This function is best effort; don't further obfuscate root error
         * with a memory allocation problem.  Just return.
         */
        fi_freeinfo(providers);
        return;
    }

    /* generate list of available providers */
    for (cur = providers; cur; cur = cur->next) {
        if (!prev ||
            strcmp(prev->fabric_attr->prov_name, cur->fabric_attr->prov_name)) {
            /* construct comma delimited list */
            strcat(avail, cur->fabric_attr->prov_name);
            strcat(avail, " ");
        }
        prev = cur;
    }
    /* truncate final comma */
    avail[strlen(avail) - 1] = '\0';

    /* display error message */
    NA_LOG_SUBSYS_ERROR(fatal,
        "Requested OFI provider \"%s\" (derived from \"%s\"\n"
        "   protocol) is not available. Please re-compile libfabric with "
        "support for\n"
        "   \"%s\" or use one of the following available providers:\n"
        "   %s",
        na_ofi_prov_name[prov_type], user_requested_protocol,
        na_ofi_prov_name[prov_type], avail);

    free(avail);
    fi_freeinfo(providers);

    return;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_getinfo(enum na_ofi_prov_type prov_type, const struct na_ofi_info *info,
    struct fi_info **fi_info_p)
{
    struct fi_info *hints = NULL;
    const char *node = NULL, *service = NULL;
    uint64_t flags = 0;
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* Hints to query and filter providers */
    hints = fi_allocinfo();
    NA_CHECK_SUBSYS_ERROR(
        cls, hints == NULL, out, ret, NA_NOMEM, "fi_allocinfo() failed");

    /**
     * FI_ASYNC_IOV mode indicates  that  the  application  must  provide  the
     * buffering needed for the IO vectors. When set, an application must not
     * modify an IO vector  of  length  >  1, including  any  related  memory
     * descriptor array, until the associated operation has completed.
     */
    hints->mode = FI_ASYNC_IOV; // FI_RX_CQ_DATA

    /* ep_type: reliable datagram (connection-less) */
    hints->ep_attr->type = FI_EP_RDM;

    /* caps: capabilities required for all providers */
    hints->caps = FI_MSG | FI_TAGGED | FI_RMA | FI_DIRECTED_RECV;

    /* msg_order, comp_order */
    hints->tx_attr->msg_order = FI_ORDER_NONE;
    hints->tx_attr->comp_order = FI_ORDER_NONE;

    /* Generate completion event when it is safe to re-use buffer */
    hints->tx_attr->op_flags = FI_INJECT_COMPLETE;

    /* Resource management will be enabled for this provider domain. */
    hints->domain_attr->resource_mgmt = FI_RM_ENABLED;

    /**
     * this is the requested MR mode (i.e., what we currently support),
     * cleared MR mode bits (depending on provider) are later checked at the
     * appropriate time.
     */
    hints->domain_attr->mr_mode =
        NA_OFI_MR_BASIC_REQ | FI_MR_LOCAL | FI_MR_ENDPOINT;

    if (prov_type != NA_OFI_PROV_NULL) {
        /* Filter out providers within libfabric using provider name */
        hints->fabric_attr->prov_name = strdup(na_ofi_prov_name[prov_type]);
        NA_CHECK_SUBSYS_ERROR(cls, hints->fabric_attr->prov_name == NULL,
            cleanup, ret, NA_NOMEM, "Could not duplicate name");

        if (na_ofi_prov_flags[prov_type] & NA_OFI_CONTEXT2)
            hints->mode |= FI_CONTEXT2;
        else
            hints->mode |= FI_CONTEXT;

        /* set endpoint protocol */
        NA_CHECK_SUBSYS_ERROR(cls,
            na_ofi_prov_ep_proto[prov_type] <= FI_PROTO_UNSPEC, cleanup, ret,
            NA_PROTONOSUPPORT, "Unsupported endpoint protocol (%d)",
            na_ofi_prov_ep_proto[prov_type]);

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 21))
        /* The FI_PROTO_CXI_RNR endpoint protocol is an optional protocol that
         * targets client/server environments where send-after-send ordering
         * is not required and messaging is generally to pre-posted buffers;
         * FI_MULTI_RECV is recommended. It utilizes a receiver-not-ready
         * implementation where FI_CXI_RNR_MAX_TIMEOUT_US can be tuned to
         * control the maximum retry duration.*/
        if (FI_VERSION_GE(fi_version(), FI_VERSION(1, 21)) &&
            (prov_type == NA_OFI_PROV_CXI)) {
            char *env = getenv("NA_OFI_CXI_PROTO_RNR");
            if (env == NULL || atoi(env) != 0) /* Enabled by default */
                hints->ep_attr->protocol = (uint32_t) FI_PROTO_CXI_RNR;
        } else
#endif
            hints->ep_attr->protocol =
                (uint32_t) na_ofi_prov_ep_proto[prov_type];

        /* add any additional caps that are particular to this provider */
        hints->caps |= na_ofi_prov_extra_caps[prov_type];
#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
        /* Starting with libfabric 1.20, the cxi provider enhanced scalability
         * of FI_SOURCE and supports FI_AV_USER_ID. */
        if (prov_type == NA_OFI_PROV_CXI)
            hints->caps |= FI_AV_USER_ID;
#else
        /* With older versions of Slingshot, disable FI_SOURCE. */
        if (prov_type == NA_OFI_PROV_CXI)
            hints->caps &= ~FI_SOURCE & ~FI_SOURCE_ERR;
#endif

        /* set default progress mode */
        hints->domain_attr->control_progress = na_ofi_prov_progress[prov_type];
        hints->domain_attr->data_progress = na_ofi_prov_progress[prov_type];
    }

    if (info) {
        /* Use addr format if not FI_FORMAT_UNSPEC */
        NA_CHECK_SUBSYS_ERROR(cls,
            (prov_type != NA_OFI_PROV_NULL) &&
                (info->addr_format <= FI_FORMAT_UNSPEC),
            cleanup, ret, NA_PROTONOSUPPORT, "Unsupported address format (%d)",
            info->addr_format);
        hints->addr_format = (uint32_t) info->addr_format;

        /* Set requested thread mode */
        hints->domain_attr->threading = info->thread_mode;

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
        /* Ask for auth keys */
        if ((na_ofi_prov_flags[prov_type] & NA_OFI_AV_AUTH_KEY) &&
            (info->num_auth_keys > 0)) {
            /* The CXI provider does not support FI_DIRECTED_RECV if
             * max_ep_auth_key > 1 */
            if (info->num_auth_keys > 1)
                hints->caps &= ~FI_DIRECTED_RECV;
            hints->domain_attr->max_ep_auth_key = info->num_auth_keys;
            hints->domain_attr->auth_key_size = FI_AV_AUTH_KEY;
        }
#endif

        /* Ask for HMEM support */
        if (info->use_hmem && (na_ofi_prov_flags[prov_type] & NA_OFI_HMEM)) {
            hints->caps |= FI_HMEM;
            hints->domain_attr->mr_mode |= FI_MR_HMEM;
        }

        /* Set src addr hints (FI_SOURCE must not be set in that case) */
        if (info->src_addr) {
            hints->src_addr = info->src_addr;
            hints->src_addrlen = info->src_addrlen;
        } else if (info->node && info->service) {
            /* For provider node resolution (always pass a numeric address) */
            flags = FI_SOURCE | FI_NUMERICHOST;
            node = info->node;
            service = info->service;
            NA_LOG_SUBSYS_DEBUG(cls,
                "Passing node/service (%s,%s) to fi_getinfo()", node, service);
        }
    }

    /* Retrieve list of all providers supported with above requirement hints */
    rc = fi_getinfo(NA_OFI_VERSION, node, service, flags, hints, fi_info_p);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, cleanup, ret, na_ofi_errno_to_na(-rc),
        "fi_getinfo(%s) failed, rc: %d (%s)", hints->fabric_attr->prov_name, rc,
        fi_strerror(-rc));

cleanup:
    free(hints->fabric_attr->prov_name);
    hints->fabric_attr->prov_name = NULL;
    hints->src_addr = NULL;
    fi_freeinfo(hints);

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_freeinfo(struct fi_info *fi_info)
{
    /* Prevent fi_freeinfo from attempting to free the key */
    if (fi_info->domain_attr->auth_key)
        fi_info->domain_attr->auth_key = NULL;
    if (fi_info->domain_attr->auth_key_size)
        fi_info->domain_attr->auth_key_size = 0;

    fi_freeinfo(fi_info);
}

/*---------------------------------------------------------------------------*/
static bool
na_ofi_match_provider(
    const struct na_ofi_verify_info *verify_info, const struct fi_info *fi_info)
{
    /* Domain must match expected address format (keep this check as OFI does
     * not seem to filter providers on addr_format) */
    if ((uint32_t) verify_info->addr_format != fi_info->addr_format)
        return false;

    /* Does not match provider name */
    if (strcmp(na_ofi_prov_name[verify_info->prov_type],
            fi_info->fabric_attr->prov_name) != 0)
        return false;

    /* Does not match domain name (if provided) */
    if (verify_info->domain_name && verify_info->domain_name[0] != '\0' &&
        strcmp(verify_info->domain_name, fi_info->domain_attr->name) != 0)
        return false;

        /* Match loc info as a last resort if nothing else was provided */
#ifdef NA_HAS_HWLOC
    if (verify_info->loc_info && fi_info->nic && fi_info->nic->bus_attr &&
        fi_info->nic->bus_attr->bus_type == FI_BUS_PCI) {
        const struct fi_pci_attr *pci = &fi_info->nic->bus_attr->attr.pci;
        return na_loc_check_pcidev(verify_info->loc_info, pci->domain_id,
            pci->bus_id, pci->device_id, pci->function_id);
    }
#endif

    /* Nothing prevents us from not picking that provider */
    return true;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_verify_info(enum na_ofi_prov_type prov_type, struct na_ofi_info *info,
    const char *domain_name, const struct na_loc_info *loc_info,
    struct fi_info **fi_info_p)
{
    struct fi_info *prov, *providers = NULL, **prov_array = NULL;
    size_t prov_count = 0, prov_max_count = NA_OFI_PROV_INFO_COUNT;
    struct na_ofi_verify_info verify_info =
        (struct na_ofi_verify_info){.prov_type = prov_type,
            .addr_format = info->addr_format,
            .domain_name = domain_name,
            .loc_info = loc_info};
    int cpu = 0;
    na_return_t ret;
#if !defined(_WIN32) && !defined(__APPLE__)
    hg_cpu_set_t cpu_set;
    int rc;
#endif

    ret = na_ofi_getinfo(prov_type, info, &providers);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "na_ofi_getinfo() failed");

#if !defined(_WIN32) && !defined(__APPLE__)
    /**
     * If threads are bound to a particular CPU ID, use that ID to select NIC
     * on system with multiple NICs (if/when hwloc returns multiple close NICs).
     */
    CPU_ZERO(&cpu_set);
    rc = hg_thread_getaffinity(hg_thread_self(), &cpu_set);
    NA_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
        NA_PROTOCOL_ERROR, "Could not retrieve CPU affinity");
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
        if (CPU_ISSET((size_t) cpu, &cpu_set))
            break;
#endif

    /* Create separate array to sort/filter prov infos */
    prov_array =
        (struct fi_info **) malloc(prov_max_count * sizeof(*prov_array));
    NA_CHECK_SUBSYS_ERROR(cls, prov_array == NULL, error, ret, NA_NOMEM,
        "Could not allocate prov_array");

    for (prov = providers; prov != NULL; prov = prov->next) {
        size_t i;

        /* Try to find provider that matches protocol and domain/host name */
        if (!na_ofi_match_provider(&verify_info, prov))
            continue;

        /* Keep only prov_infos that have different domains */
        for (i = 0; i < prov_count; i++)
            if (strcmp(prov_array[i]->domain_attr->name,
                    prov->domain_attr->name) == 0)
                break;
        if (i < prov_count) /* duplicate */
            continue;

        NA_LOG_SUBSYS_DEBUG_EXT(cls, "Verbose FI info for provider", "#%zu %s",
            prov_count, fi_tostr(prov, FI_TYPE_INFO));

        if (prov_count == prov_max_count) {
            prov_max_count *= 2;
            prov_array = (struct fi_info **) realloc(
                prov_array, prov_max_count * sizeof(*prov_array));
            NA_CHECK_SUBSYS_ERROR(cls, prov_array == NULL, error, ret, NA_NOMEM,
                "Could not reallocate prov_array");
        }
        prov_array[prov_count] = prov;
        prov_count++;
    }
    NA_CHECK_SUBSYS_ERROR(fatal, prov_count == 0, error, ret, NA_NOENTRY,
        "No provider found for \"%s\" provider on domain \"%s\"",
        na_ofi_prov_name[prov_type], domain_name);

    NA_LOG_SUBSYS_DEBUG(
        cls, "na_ofi_getinfo() returned %zu candidate(s)", prov_count);

    /* Round-robin on domains based on selected CPU */
    prov = prov_array[(prov_count > 1) ? (size_t) cpu % prov_count : 0];
    NA_LOG_SUBSYS_DEBUG_EXT(cls, "FI info for selected provider", "%s",
        fi_tostr(prov, FI_TYPE_INFO));

    /* Keep fi_info */
    *fi_info_p = fi_dupinfo(prov);
    NA_CHECK_SUBSYS_ERROR(cls, *fi_info_p == NULL, error, ret, NA_NOMEM,
        "Could not duplicate fi_info");

    fi_freeinfo(providers);
    free(prov_array);

    return NA_SUCCESS;

error:
    if (providers)
        fi_freeinfo(providers);
    free(prov_array);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_hostname_info(enum na_ofi_prov_type prov_type,
    const char *hostname_info, int addr_format, char **domain_name_p,
    char **node_p, char **service_p, void **src_addr_p, size_t *src_addrlen_p)
{
    char *hostname = NULL;
    char *domain_name = NULL;
    na_return_t ret = NA_SUCCESS;

    /* Parse hostname info */
    switch (addr_format) {
        case FI_SOCKADDR_IN:
        case FI_SOCKADDR_IN6: {
            uint16_t port = 0;
            struct sockaddr *sa = NULL;
            char **ifa_name_p = NULL;
            socklen_t salen = 0;
#ifndef _WIN32
            na_return_t na_ret;
#endif

            ret = na_ofi_parse_sin_info(
                hostname_info, &domain_name, &hostname, &port);
            NA_CHECK_SUBSYS_NA_ERROR(cls, out, ret, "Could not parse sin info");

            if (hostname == NULL) {
                char host[NA_OFI_MAX_URI_LEN];
                int rc;

                if (!port)
                    break; /* nothing to do */

                rc = gethostname(host, sizeof(host));
                NA_CHECK_SUBSYS_ERROR(cls, rc != 0, out, ret, NA_PROTOCOL_ERROR,
                    "gethostname() failed (%s)", strerror(errno));

                hostname = strdup(host);
                NA_CHECK_SUBSYS_ERROR(cls, hostname == NULL, out, ret, NA_NOMEM,
                    "strdup() of host failed");
            }

            /* Attempt to resolve hostname / iface */
            NA_LOG_SUBSYS_DEBUG(
                cls, "Resolving name %s with port %" PRIu16, hostname, port);

            /* Only query interface name if domain name was not provided */
            if (domain_name == NULL &&
                (na_ofi_prov_flags[prov_type] & NA_OFI_DOM_IFACE))
                ifa_name_p = &domain_name;

#ifndef _WIN32
            na_ret = na_ip_check_interface(hostname, port,
                (addr_format == FI_SOCKADDR_IN6) ? AF_INET6 : AF_INET,
                ifa_name_p, &sa, &salen);
            if (na_ret != NA_SUCCESS && domain_name == NULL) {
                NA_LOG_SUBSYS_WARNING(cls,
                    "Could not find matching interface for %s, "
                    "attempting to use it as domain name",
                    hostname);

                /* Pass domain name as hostname if not set */
                domain_name = strdup(hostname);
                NA_CHECK_SUBSYS_ERROR(cls, domain_name == NULL, out, ret,
                    NA_NOMEM, "strdup() of hostname failed");
            }
#endif

            /* Pass src addr information to avoid name resolution */
            *src_addr_p = (void *) sa;
            *src_addrlen_p = (size_t) salen;
            break;
        }
        case FI_ADDR_PSMX:
        case FI_ADDR_PSMX2:
        case FI_ADDR_GNI:
        case FI_ADDR_STR:
            /* Nothing to do */
            break;
        case FI_SOCKADDR_IB:
            /* TODO we could potentially add support for native addresses */
            /* Simply dup info */
            domain_name = strdup(hostname_info);
            NA_CHECK_SUBSYS_ERROR(cls, domain_name == NULL, out, ret, NA_NOMEM,
                "strdup() of hostname_info failed");
            break;

        case FI_ADDR_CXI:
            ret = na_ofi_parse_cxi_info(hostname_info, node_p, service_p);
            NA_CHECK_SUBSYS_NA_ERROR(cls, out, ret, "Could not parse cxi info");

            /* Manually set domain name and use that for matching info if no
             * specific port was passed. */
            if ((*node_p != NULL) && (*service_p == NULL)) {
                domain_name = strdup(*node_p);
                NA_CHECK_SUBSYS_ERROR(cls, domain_name == NULL, out, ret,
                    NA_NOMEM, "strdup() of %s failed", *node_p);
            }
            break;

        case FI_ADDR_OPX:
            ret = na_ofi_parse_opx_info(
                hostname_info, (struct na_ofi_opx_addr **) src_addr_p);
            NA_CHECK_SUBSYS_NA_ERROR(cls, out, ret, "Could not parse opx info");
            *src_addrlen_p = sizeof(struct na_ofi_opx_addr);
            break;
        default:
            NA_LOG_SUBSYS_ERROR(
                fatal, "Unsupported address format: %d", addr_format);
            return NA_PROTONOSUPPORT;
    }

    *domain_name_p = domain_name;

out:
    free(hostname);
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_free_hostname_info(
    char *domain_name, char *node, char *service, void *src_addr)
{
    free(domain_name);
    free(node);
    free(service);
    free(src_addr);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_sin_info(
    const char *str, char **domain_name_p, char **hostname_p, uint16_t *port_p)
{
    char hostname[65], domain_name[65];
    na_return_t ret;

    if (sscanf(str, ":%" SCNu16, port_p) == 1) {
        /* Only port, e.g. ":12345" */
        NA_LOG_SUBSYS_DEBUG(cls, "port: %" PRIu16, *port_p);
    } else if (sscanf(str, "%64[^/]/:%" SCNu16, domain_name, port_p) == 2) {
        /* Domain and port, e.g. "lo/:12345" */
        NA_LOG_SUBSYS_DEBUG(
            cls, "domain: %s, port: %" PRIu16, domain_name, *port_p);

        *domain_name_p = strdup(domain_name);
        NA_CHECK_SUBSYS_ERROR(cls, *domain_name_p == NULL, error, ret, NA_NOMEM,
            "strdup() of domain_name failed");

        *hostname_p = strdup(domain_name);
        NA_CHECK_SUBSYS_ERROR(cls, *hostname_p == NULL, error, ret, NA_NOMEM,
            "strdup() of domain_name failed");
    } else if (sscanf(str, "%64[^/]/%64[^/:]:%" SCNu16, domain_name, hostname,
                   port_p) > 1) {
        /* Domain / hostname and port, e.g. "lo/lo:12345" */
        NA_LOG_SUBSYS_DEBUG(cls, "domain: %s, hostname: %s, port: %" PRIu16,
            domain_name, hostname, *port_p);

        *domain_name_p = strdup(domain_name);
        NA_CHECK_SUBSYS_ERROR(cls, *domain_name_p == NULL, error, ret, NA_NOMEM,
            "strdup() of domain_name failed");

        *hostname_p = strdup(hostname);
        NA_CHECK_SUBSYS_ERROR(cls, *hostname_p == NULL, error, ret, NA_NOMEM,
            "strdup() of hostname failed");
    } else if (sscanf(str, "%64[^/:]:%" SCNu16, hostname, port_p) > 0) {
        /* Hostname and port, e.g. "lo:12345" */
        NA_LOG_SUBSYS_DEBUG(
            cls, "hostname: %s, port: %" PRIu16, hostname, *port_p);

        *hostname_p = strdup(hostname);
        NA_CHECK_SUBSYS_ERROR(cls, *hostname_p == NULL, error, ret, NA_NOMEM,
            "strdup() of hostname failed");
    } else
        NA_GOTO_SUBSYS_ERROR(cls, error, ret, NA_PROTONOSUPPORT,
            "Malformed SIN info, format is: <domain>/<node>:<service>");

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_cxi_info(
    const char *hostname_info, char **node_p, char **service_p)
{
    char nic_name[5] = {NA_OFI_CXI_NODE_PREFIX "X"}; /* cxi[0-9] */
    char pid_name[4];
    uint16_t pid = 0; /* [0-510] */
    uint16_t pid_mask = 0x1ff;
    char *node = NULL;
    na_return_t ret;

    if (sscanf(hostname_info, ":%" SCNu16, &pid) == 1) {
        /* Only port, e.g. ":510" */
        NA_CHECK_SUBSYS_ERROR(cls, pid >= pid_mask, error, ret,
            NA_PROTONOSUPPORT, "CXI PID is %" PRIu16 ", must be [0-510]", pid);
        NA_LOG_SUBSYS_DEBUG(cls, "PID: %" PRIu16, pid);
    } else if (sscanf(hostname_info, NA_OFI_CXI_NODE_PREFIX "%1[0-9]:%" SCNu16,
                   &nic_name[3], &pid) > 0) {
        /* cxi[0-9]:port or cxi[0-9] */
        NA_CHECK_SUBSYS_ERROR(cls, pid >= pid_mask, error, ret,
            NA_PROTONOSUPPORT, "CXI PID is %" PRIu16 ", must be [0-510]", pid);
        NA_LOG_SUBSYS_DEBUG(cls, "NIC name: %s, PID: %" PRIu16, nic_name, pid);

        node = strdup(nic_name);
        NA_CHECK_SUBSYS_ERROR(cls, node == NULL, error, ret, NA_NOMEM,
            "strdup() of nic_name failed");
    } else
        NA_GOTO_SUBSYS_ERROR(cls, error, ret, NA_PROTONOSUPPORT,
            "Malformed CXI info, format is: " NA_OFI_CXI_NODE_PREFIX
            "[0-9]:[0-510]");

    /* Let the service string be NULL if PID is 0 to prevent CXI failure on
     * endpoint open when same PID is used */
    if (pid > 0) {
        snprintf(pid_name, sizeof(pid_name), "%" PRIu16,
            (uint16_t) (pid & pid_mask));

        *service_p = strdup(pid_name);
        NA_CHECK_SUBSYS_ERROR(cls, *service_p == NULL, error, ret, NA_NOMEM,
            "strdup() of pid_name failed");
    }

    *node_p = node;

    return NA_SUCCESS;

error:
    free(node);
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_opx_info(
    const char *hostname_info, struct na_ofi_opx_addr **src_addr_p)
{
    struct na_ofi_opx_addr *src_addr = NULL;
    uint8_t hfi_unit = 0;
    na_ofi_opx_uid_t uid = 0;
    na_return_t ret;

    if (sscanf(hostname_info, ":%" SCNu32, &uid) == 1) {
        /* Only port */
        NA_LOG_SUBSYS_DEBUG(cls, ":%" PRIu32, uid);
    } else if (sscanf(hostname_info,
                   NA_OFI_OPX_NODE_PREFIX "%" SCNu8 ":%" SCNu32, &hfi_unit,
                   &uid) > 0) {
        /* Interface and port */
        NA_LOG_SUBSYS_DEBUG(
            cls, NA_OFI_OPX_NODE_PREFIX "%" PRIu8 ":%" PRIu32, hfi_unit, uid);
    } else
        NA_GOTO_SUBSYS_ERROR(cls, error, ret, NA_PROTONOSUPPORT,
            "Malformed OPX info, format is: <" NA_OFI_OPX_NODE_PREFIX
            "[0-9]>:<uid>");

    src_addr = calloc(1, sizeof(*src_addr));
    NA_CHECK_SUBSYS_ERROR(addr, src_addr == NULL, error, ret, NA_NOMEM,
        "Could not allocate na ofi opx address");

    src_addr->addr.hfi1_unit = hfi_unit;
    /* Need to use the uuid field of the address because key requires 4 bytes */
    src_addr->addr.uid.fi = uid;

    *src_addr_p = src_addr;

    return NA_SUCCESS;

error:
    free(src_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static struct na_ofi_class *
na_ofi_class_alloc(void)
{
    struct na_ofi_class *na_ofi_class = NULL;
    int rc;

    /* Create private data */
    na_ofi_class = (struct na_ofi_class *) calloc(1, sizeof(*na_ofi_class));
    NA_CHECK_SUBSYS_ERROR_NORET(cls, na_ofi_class == NULL, error,
        "Could not allocate NA private data class");
    hg_atomic_init32(&na_ofi_class->n_contexts, 0);

    /* Initialize addr pool */
    rc = hg_thread_spin_init(&na_ofi_class->addr_pool.lock);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, rc != HG_UTIL_SUCCESS, error, "hg_thread_spin_init() failed");
    STAILQ_INIT(&na_ofi_class->addr_pool.queue);

    return na_ofi_class;

error:
    if (na_ofi_class)
        na_ofi_class_free(na_ofi_class);

    return NULL;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_class_free(struct na_ofi_class *na_ofi_class)
{
    na_return_t ret = NA_SUCCESS;

#ifdef NA_OFI_HAS_ADDR_POOL
    /* Free addresses */
    while (!STAILQ_EMPTY(&na_ofi_class->addr_pool.queue)) {
        struct na_ofi_addr *na_ofi_addr =
            STAILQ_FIRST(&na_ofi_class->addr_pool.queue);
        STAILQ_REMOVE_HEAD(&na_ofi_class->addr_pool.queue, entry);

        na_ofi_addr_destroy(na_ofi_addr);
    }
#endif

    /* Close endpoint */
    if (na_ofi_class->endpoint) {
        ret = na_ofi_endpoint_close(na_ofi_class->endpoint);
        NA_CHECK_SUBSYS_NA_ERROR(cls, out, ret, "Could not close endpoint");
        na_ofi_class->endpoint = NULL;
    }

#ifdef NA_OFI_HAS_MEM_POOL
    if (na_ofi_class->send_pool) {
        hg_mem_pool_destroy(na_ofi_class->send_pool);
        na_ofi_class->send_pool = NULL;
    }
    if (na_ofi_class->recv_pool) {
        hg_mem_pool_destroy(na_ofi_class->recv_pool);
        na_ofi_class->recv_pool = NULL;
    }
#endif

    /* Close domain */
    if (na_ofi_class->domain) {
        ret = na_ofi_domain_close(na_ofi_class->domain);
        NA_CHECK_SUBSYS_NA_ERROR(cls, out, ret, "Could not close domain");
        na_ofi_class->domain = NULL;
    }

    /* Close fabric */
    if (na_ofi_class->fabric) {
        ret = na_ofi_fabric_close(na_ofi_class->fabric);
        NA_CHECK_SUBSYS_NA_ERROR(cls, out, ret, "Could not close fabric");
        na_ofi_class->fabric = NULL;
    }

    /* Free info */
    if (na_ofi_class->fi_info)
        na_ofi_freeinfo(na_ofi_class->fi_info);

    (void) hg_thread_spin_destroy(&na_ofi_class->addr_pool.lock);

    free(na_ofi_class);

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_class_env_config(struct na_ofi_class *na_ofi_class)
{
    char *env;
    na_return_t ret;

    /* Set unexpected msg callbacks */
    env = getenv("NA_OFI_UNEXPECTED_TAG_MSG");
    if (env == NULL || env[0] == '0' || tolower(env[0]) == 'n') {
        na_ofi_class->msg_send_unexpected = na_ofi_msg_send;
        na_ofi_class->msg_recv_unexpected = na_ofi_msg_recv;
    } else {
        NA_LOG_SUBSYS_DEBUG(cls,
            "NA_OFI_UNEXPECTED_TAG_MSG set to %s, forcing unexpected messages "
            "to use tagged recvs",
            env);
        na_ofi_class->msg_send_unexpected = na_ofi_tag_send;
        na_ofi_class->msg_recv_unexpected = na_ofi_tag_recv;
    }

    /* Default retry timeouts in ms */
    if ((env = getenv("NA_OFI_OP_RETRY_TIMEOUT")) != NULL) {
        na_ofi_class->op_retry_timeout = (unsigned int) atoi(env);
    } else
        na_ofi_class->op_retry_timeout = NA_OFI_OP_RETRY_TIMEOUT;

    if ((env = getenv("NA_OFI_OP_RETRY_PERIOD")) != NULL) {
        na_ofi_class->op_retry_period = (unsigned int) atoi(env);
    } else
        na_ofi_class->op_retry_period = 0;
    NA_CHECK_SUBSYS_ERROR(cls,
        na_ofi_class->op_retry_period > na_ofi_class->op_retry_timeout, error,
        ret, NA_INVALID_ARG,
        "NA_OFI_OP_RETRY_PERIOD (%u) > NA_OFI_OP_RETRY_TIMEOUT(%u)",
        na_ofi_class->op_retry_period, na_ofi_class->op_retry_timeout);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_ofi_fabric_open(enum na_ofi_prov_type prov_type, struct fi_fabric_attr *attr,
    struct na_ofi_fabric **na_ofi_fabric_p)
{
    struct na_ofi_fabric *na_ofi_fabric = NULL;
    na_return_t ret;
    int rc;

#ifndef _WIN32
    /**
     * Look for existing fabrics. A fabric domain represents a collection of
     * hardware and software resources that access a single physical or virtual
     * network.
     */
    hg_thread_mutex_lock(&na_ofi_fabric_list_mutex_g);
    SLIST_FOREACH (na_ofi_fabric, &na_ofi_fabric_list_g, entry)
        if ((strcmp(attr->name, na_ofi_fabric->name) == 0) &&
            (strcmp(attr->prov_name, na_ofi_fabric->prov_name) == 0))
            break;

    if (na_ofi_fabric != NULL) {
        NA_LOG_SUBSYS_DEBUG_EXT(cls, "using existing fi_fabric", "%s",
            fi_tostr(attr, FI_TYPE_FABRIC_ATTR));
        na_ofi_fabric->refcount++;
        *na_ofi_fabric_p = na_ofi_fabric;
        hg_thread_mutex_unlock(&na_ofi_fabric_list_mutex_g);
        return NA_SUCCESS;
    }
    hg_thread_mutex_unlock(&na_ofi_fabric_list_mutex_g);
#endif

    na_ofi_fabric = (struct na_ofi_fabric *) calloc(1, sizeof(*na_ofi_fabric));
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_fabric == NULL, error, ret, NA_NOMEM,
        "Could not allocate na_ofi_fabric");
    na_ofi_fabric->prov_type = prov_type;
    na_ofi_fabric->refcount = 1;

    /* Dup name */
    na_ofi_fabric->name = strdup(attr->name);
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_fabric->name == NULL, error, ret,
        NA_NOMEM, "Could not duplicate fabric name");

    /* Dup provider name */
    na_ofi_fabric->prov_name = strdup(attr->prov_name);
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_fabric->prov_name == NULL, error, ret,
        NA_NOMEM, "Could not duplicate prov_name");

    /* Open fi fabric */
    rc = fi_fabric(attr, &na_ofi_fabric->fi_fabric, NULL);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_fabric() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    NA_LOG_SUBSYS_DEBUG_EXT(
        cls, "fi_fabric opened", "%s", fi_tostr(attr, FI_TYPE_FABRIC_ATTR));

#ifndef _WIN32
    /* Insert to global fabric list */
    hg_thread_mutex_lock(&na_ofi_fabric_list_mutex_g);
    SLIST_INSERT_HEAD(&na_ofi_fabric_list_g, na_ofi_fabric, entry);
    hg_thread_mutex_unlock(&na_ofi_fabric_list_mutex_g);
#endif

    *na_ofi_fabric_p = na_ofi_fabric;

    return NA_SUCCESS;

error:
    if (na_ofi_fabric) {
        free(na_ofi_fabric->name);
        free(na_ofi_fabric->prov_name);
        free(na_ofi_fabric);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_ofi_fabric_close(struct na_ofi_fabric *na_ofi_fabric)
{
    na_return_t ret;
    int rc;

    if (!na_ofi_fabric)
        return NA_SUCCESS;

    NA_LOG_SUBSYS_DEBUG(cls, "Closing fabric");

#ifndef _WIN32
    /* Remove from fabric list */
    hg_thread_mutex_lock(&na_ofi_fabric_list_mutex_g);
    if (--na_ofi_fabric->refcount > 0) {
        hg_thread_mutex_unlock(&na_ofi_fabric_list_mutex_g);
        return NA_SUCCESS;
    }
#endif

    NA_LOG_SUBSYS_DEBUG(cls, "Freeing fabric");

    /* Close fabric */
    if (na_ofi_fabric->fi_fabric) {
        rc = fi_close(&na_ofi_fabric->fi_fabric->fid);
        NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_close() fabric failed, rc: %d (%s)", rc, fi_strerror(-rc));
        na_ofi_fabric->fi_fabric = NULL;
    }
#ifndef _WIN32
    SLIST_REMOVE(&na_ofi_fabric_list_g, na_ofi_fabric, na_ofi_fabric, entry);
    hg_thread_mutex_unlock(&na_ofi_fabric_list_mutex_g);
#endif

    free(na_ofi_fabric->name);
    free(na_ofi_fabric->prov_name);
    free(na_ofi_fabric);

    return NA_SUCCESS;

error:
#ifndef _WIN32
    na_ofi_fabric->refcount++;
    hg_thread_mutex_unlock(&na_ofi_fabric_list_mutex_g);
#endif

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_set_domain_ops(
    enum na_ofi_prov_type prov_type, struct na_ofi_domain *na_ofi_domain)
{
    switch (prov_type) {
        case NA_OFI_PROV_GNI:
#ifdef NA_OFI_HAS_EXT_GNI_H
            return na_ofi_gni_set_domain_ops(na_ofi_domain);
#else
            return NA_PROTONOSUPPORT;
#endif
        case NA_OFI_PROV_CXI:
            na_ofi_cxi_set_domain_ops(na_ofi_domain);
            break;
        case NA_OFI_PROV_SHM:
        case NA_OFI_PROV_SOCKETS:
        case NA_OFI_PROV_TCP:
        case NA_OFI_PROV_TCP_RXM:
        case NA_OFI_PROV_PSM2:
        case NA_OFI_PROV_OPX:
        case NA_OFI_PROV_VERBS_RXM:
            break;
        case NA_OFI_PROV_NULL:
        default:
            NA_LOG_SUBSYS_ERROR(fatal,
                "auth_key not supported for this provider: %s",
                na_ofi_prov_name[prov_type]);
            return NA_PROTONOSUPPORT;
    }

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
#ifdef NA_OFI_HAS_EXT_GNI_H
static na_return_t
na_ofi_gni_set_domain_op_value(
    struct na_ofi_domain *na_ofi_domain, int op, void *value)
{
    struct fi_gni_ops_domain *gni_domain_ops;
    na_return_t ret = NA_SUCCESS;
    int rc;

    rc = fi_open_ops(&na_ofi_domain->fi_domain->fid, FI_GNI_DOMAIN_OPS_1, 0,
        (void **) &gni_domain_ops, NULL);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, out, ret, na_ofi_errno_to_na(-rc),
        "fi_open_ops() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    rc = gni_domain_ops->set_val(&na_ofi_domain->fi_domain->fid, op, value);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, out, ret, na_ofi_errno_to_na(-rc),
        "gni_domain_ops->set_val() failed, rc: %d (%s)", rc, fi_strerror(-rc));

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_gni_get_domain_op_value(
    struct na_ofi_domain *na_ofi_domain, int op, void *value)
{
    struct fi_gni_ops_domain *gni_domain_ops;
    na_return_t ret = NA_SUCCESS;
    int rc;

    rc = fi_open_ops(&na_ofi_domain->fi_domain->fid, FI_GNI_DOMAIN_OPS_1, 0,
        (void **) &gni_domain_ops, NULL);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, out, ret, na_ofi_errno_to_na(-rc),
        "fi_open_ops() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    rc = gni_domain_ops->get_val(&na_ofi_domain->fi_domain->fid, op, value);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, out, ret, na_ofi_errno_to_na(-rc),
        "gni_domain_ops->get_val() failed, rc: %d (%s)", rc, fi_strerror(-rc));

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_gni_set_domain_ops(struct na_ofi_domain *na_ofi_domain)
{
    int32_t enable = 1;
    na_return_t ret;

#    ifdef NA_OFI_GNI_HAS_UDREG
    char *other_reg_type = "udreg";
    int32_t udreg_limit = NA_OFI_GNI_UDREG_REG_LIMIT;

    /* Enable use of udreg instead of internal MR cache */
    ret = na_ofi_gni_set_domain_op_value(
        na_ofi_domain, GNI_MR_CACHE, &other_reg_type);
    NA_CHECK_SUBSYS_NA_ERROR(
        cls, error, ret, "Could not set domain op value for GNI_MR_CACHE");

    /* Experiments on Theta showed default value of 2048 too high if
     * launching multiple clients on one node */
    ret = na_ofi_gni_set_domain_op_value(
        na_ofi_domain, GNI_MR_UDREG_REG_LIMIT, &udreg_limit);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret,
        "Could not set domain op value for GNI_MR_UDREG_REG_LIMIT");
#    endif

    /* Enable lazy deregistration in MR cache */
    ret = na_ofi_gni_set_domain_op_value(
        na_ofi_domain, GNI_MR_CACHE_LAZY_DEREG, &enable);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret,
        "Could not set domain op value for GNI_MR_CACHE_LAZY_DEREG");

    return NA_SUCCESS;

error:
    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
static void
na_ofi_cxi_set_domain_ops(struct na_ofi_domain *na_ofi_domain)
{
    bool val = false;
    int rc;

    /* PROV_KEY_CACHE: The provider key cache is a performance optimization for
     * FI_MR_PROV_KEY. The performance gain is fi_mr_close() becomes a no-op but
     * at the cost of the corresponding MR being left exposed to the network.
     * This is intended to be used for applications where fi_mr_close() is on
     * the critical path. For storage use-cases, leaving MRs exposed is an
     * issue. This could result in MR operations unexpectedly completing and
     * reading/writing to unknown memory. */
    rc = fi_control(
        &na_ofi_domain->fi_domain->fid, FI_OPT_CXI_SET_PROV_KEY_CACHE, &val);
    NA_CHECK_SUBSYS_WARNING(cls, rc != 0,
        "could not set CXI PROV_KEY_CACHE property (%s)", fi_strerror(-rc));

    /* OPTIMIZED_MRS: Optimized MRs offer a higher operation rate over
     * standard/unoptimized MRs. Because optimized MR allocation/deallocation is
     * expensive (i.e., it always requires calls into the kernel), optimized MRs
     * should only be used for persistent MRs. This typically maps to MPI/SHMEM
     * RMA windows which are persistent. For Mercury, since MRs are ephemeral
     * and allocation/deallocation may be on the critical path, optimized MRs
     * should be disabled. Optimized MRs also present a risk for the
     * recycling of MR keys (when using FI_MR_PROV_KEY) where multiple regions
     * could end up using the same key by allocating/deallocating the MR,
     * leading to potential memory corruptions. */
    rc = fi_control(
        &na_ofi_domain->fi_domain->fid, FI_OPT_CXI_SET_OPTIMIZED_MRS, &val);
    NA_CHECK_SUBSYS_WARNING(cls, rc != 0,
        "could not set CXI OPTIMIZED_MRS property (%s)", fi_strerror(-rc));

    /* MR_MATCH_EVENTS: While standard/unoptimized MRs do not have a call into
     * the kernel for MR allocation, there is still a call into the kernel for
     * MR deallocation. To avoid this kernel call, MR_MATCH_EVENTS needs to be
     * enabled. The cost MR_MATCH_EVENTS introduces is where the target of an
     * RMA operation was previously passive (i.e., no events), this will enable
     * MR events. This requires the owner of the MR to process event queues in a
     * timely manner or have large event queue buffers. */
    val = true;
    rc = fi_control(
        &na_ofi_domain->fi_domain->fid, FI_OPT_CXI_SET_MR_MATCH_EVENTS, &val);
    NA_CHECK_SUBSYS_WARNING(cls, rc != 0,
        "could not set CXI MR_MATCH_EVENTS property (%s)", fi_strerror(-rc));
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_auth_key(const char *str, enum na_ofi_prov_type prov_type,
    const char *domain_name, union na_ofi_auth_key *auth_key,
    size_t *auth_key_size_p)
{
    switch (prov_type) {
        case NA_OFI_PROV_GNI:
            return na_ofi_parse_gni_auth_key(
                str, &auth_key->gni_auth_key, auth_key_size_p);
        case NA_OFI_PROV_CXI:
            return na_ofi_parse_cxi_auth_key(
                str, domain_name, &auth_key->cxi_auth_key, auth_key_size_p);
        case NA_OFI_PROV_NULL:
        case NA_OFI_PROV_SHM:
        case NA_OFI_PROV_SOCKETS:
        case NA_OFI_PROV_TCP:
        case NA_OFI_PROV_TCP_RXM:
        case NA_OFI_PROV_PSM2:
        case NA_OFI_PROV_OPX:
        case NA_OFI_PROV_VERBS_RXM:
        default:
            NA_LOG_SUBSYS_ERROR(
                fatal, "unsupported provider: %s", na_ofi_prov_name[prov_type]);
            return NA_PROTONOSUPPORT;
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_gni_auth_key(
    const char *str, struct fi_gni_auth_key *auth_key, size_t *auth_key_size_p)
{
    na_return_t ret;
    int rc;

    /* GNIX_AKT_RAW is 0 */
    memset(auth_key, 0, sizeof(*auth_key));

    rc = sscanf(str, "%" SCNu32, &auth_key->raw.protection_key);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 1, error, ret, NA_PROTONOSUPPORT,
        "Invalid GNI auth_key string (%s)", str);

    *auth_key_size_p = sizeof(*auth_key);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_cxi_auth_key(const char *str, const char *domain_name,
    struct cxi_auth_key *auth_key, size_t *auth_key_size_p)
{
    na_return_t ret;
    int rc, idx = 1; /* default VNI index is 1 if not specified */

    memset(auth_key, 0, sizeof(*auth_key));

    /* Keep CXI auth key using the following format svc_id:vni */
    rc = sscanf(str, "%" SCNu32 ":%" SCNu16 ":%d", &auth_key->svc_id,
        &auth_key->vni, &idx);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 2 && rc != 3, error, ret,
        NA_PROTONOSUPPORT,
        "Invalid CXI auth key string (%s), format is \"svc_id:vni<:idx>\"",
        str);

    /* If zeros are passed for auth_key, try to find the missing bits */
    if (auth_key->svc_id == 0) {
        ret = na_ofi_cxi_find_svc_id(domain_name, &auth_key->svc_id);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not find CXI svc_id");
    }
    if (auth_key->vni == 0) {
        ret = na_ofi_cxi_find_vni(idx, &auth_key->vni);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not find CXI vni");
    }
    NA_LOG_SUBSYS_DEBUG(
        cls, "auth_key=%" PRIu32 ":%" PRIu16, auth_key->svc_id, auth_key->vni);

    *auth_key_size_p = sizeof(*auth_key);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cxi_find_svc_id(const char *domain_name, uint32_t *svc_id_p)
{
    char *device_name = getenv("SLINGSHOT_DEVICES"), *device_next;
    char *svc_id = getenv("SLINGSHOT_SVC_IDS"), *svc_id_next;
    int device_idx = 0, svc_id_idx = 0, rc;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(cls, device_name == NULL, error, ret, NA_NOENTRY,
        "SLINGSHOT_DEVICES is not set");
    while (strtok_r(device_name, ",", &device_next) != NULL) {
        if (strcmp(device_name, domain_name) == 0)
            break;
        device_name = device_next;
        device_idx++;
    }
    NA_CHECK_SUBSYS_ERROR(cls, device_name[0] == '\0', error, ret,
        NA_PROTOCOL_ERROR, "No device found for domain name %s", domain_name);
    NA_LOG_SUBSYS_DEBUG(
        cls, "Found device name %s, idx=%d", device_name, device_idx);

    NA_CHECK_SUBSYS_ERROR(cls, svc_id == NULL, error, ret, NA_NOENTRY,
        "SLINGSHOT_SVC_IDS is not set");
    while (strtok_r(svc_id, ",", &svc_id_next) != NULL) {
        if (svc_id_idx == device_idx)
            break;
        svc_id = svc_id_next;
        svc_id_idx++;
    }
    NA_CHECK_SUBSYS_ERROR(cls, svc_id_idx != device_idx, error, ret,
        NA_PROTOCOL_ERROR, "No svc_id found for domain name %s", domain_name);
    NA_LOG_SUBSYS_DEBUG(cls, "Found svc_id %s, idx=%d", svc_id, svc_id_idx);

    rc = sscanf(svc_id, "%" SCNu32, svc_id_p);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 1, error, ret, NA_PROTONOSUPPORT,
        "Invalid CXI svc_id (%s)", svc_id);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cxi_find_vni(int idx, uint16_t *vni_p)
{
    char *vni = getenv("SLINGSHOT_VNIS"), *vni_next;
    int vni_idx = 0, rc;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        cls, vni == NULL, error, ret, NA_NOENTRY, "SLINGSHOT_VNIS is not set");
    while (strtok_r(vni, ",", &vni_next) != NULL) {
        if (vni_idx == idx)
            break;
        vni = vni_next;
        vni_idx++;
    }
    NA_CHECK_SUBSYS_ERROR(cls, vni_idx != idx, error, ret, NA_PROTOCOL_ERROR,
        "No VNI found for idx %d", idx);
    NA_LOG_SUBSYS_DEBUG(cls, "Found vni %s, idx=%d", vni, vni_idx);

    rc = sscanf(vni, "%" SCNu16, vni_p);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 1, error, ret, NA_PROTONOSUPPORT,
        "Invalid CXI vni (%s)", vni);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
static na_return_t
na_ofi_parse_auth_key_range(const char *str, enum na_ofi_prov_type prov_type,
    union na_ofi_auth_key *base_key, size_t *auth_key_num_p)
{
    switch (prov_type) {
        case NA_OFI_PROV_CXI:
            return na_ofi_parse_cxi_auth_key_range(
                str, &base_key->cxi_auth_key, auth_key_num_p);
        case NA_OFI_PROV_NULL:
        case NA_OFI_PROV_SHM:
        case NA_OFI_PROV_SOCKETS:
        case NA_OFI_PROV_TCP:
        case NA_OFI_PROV_TCP_RXM:
        case NA_OFI_PROV_GNI:
        case NA_OFI_PROV_PSM2:
        case NA_OFI_PROV_OPX:
        case NA_OFI_PROV_VERBS_RXM:
        default:
            NA_LOG_SUBSYS_ERROR(
                fatal, "unsupported provider: %s", na_ofi_prov_name[prov_type]);
            return NA_PROTONOSUPPORT;
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_parse_cxi_auth_key_range(
    const char *str, struct cxi_auth_key *base_key, size_t *auth_key_num_p)
{
    uint16_t vni_max = 0;
    na_return_t ret;
    int rc;

    memset(base_key, 0, sizeof(*base_key));

    /* Keep CXI auth key using the following format svc_id:vni_min:vni_max */
    rc = sscanf(str, "%" SCNu32 ":%" SCNu16 ":%" SCNu16, &base_key->svc_id,
        &base_key->vni, &vni_max);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 2 && rc != 3, error, ret,
        NA_PROTONOSUPPORT,
        "Invalid CXI auth key range string (%s), format is "
        "\"svc_id:vni_min<:vni_max>\"",
        str);

    if (base_key->svc_id == 0)
        *auth_key_num_p = 1; /* Assume a single auth key */
    else
        *auth_key_num_p = (vni_max > base_key->vni)
                              ? (size_t) (vni_max - base_key->vni + 1)
                              : 1;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_gen_auth_key(const union na_ofi_auth_key *base_key, int index,
    enum na_ofi_prov_type prov_type, union na_ofi_auth_key *auth_key,
    size_t *auth_key_size_p)
{
    switch (prov_type) {
        case NA_OFI_PROV_CXI:
            na_ofi_gen_cxi_auth_key(&base_key->cxi_auth_key, index,
                &auth_key->cxi_auth_key, auth_key_size_p);
            return NA_SUCCESS;
        case NA_OFI_PROV_NULL:
        case NA_OFI_PROV_SHM:
        case NA_OFI_PROV_SOCKETS:
        case NA_OFI_PROV_TCP:
        case NA_OFI_PROV_TCP_RXM:
        case NA_OFI_PROV_GNI:
        case NA_OFI_PROV_PSM2:
        case NA_OFI_PROV_OPX:
        case NA_OFI_PROV_VERBS_RXM:
        default:
            NA_LOG_SUBSYS_ERROR(
                fatal, "unsupported provider: %s", na_ofi_prov_name[prov_type]);
            return NA_PROTONOSUPPORT;
    }
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_gen_cxi_auth_key(const struct cxi_auth_key *base_key, int index,
    struct cxi_auth_key *auth_key, size_t *auth_key_size_p)
{
    auth_key->svc_id = base_key->svc_id;
    auth_key->vni = base_key->vni + (uint16_t) index;
    *auth_key_size_p = sizeof(*auth_key);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_auth_key_insert(struct na_ofi_domain *na_ofi_domain,
    const union na_ofi_auth_key *auth_key, size_t auth_key_size)
{
    union na_ofi_auth_key *auth_key_p = NULL;
    fi_addr_t fi_auth_key;
    na_return_t ret;
    int rc;

    auth_key_p = calloc(1, sizeof(*auth_key_p));
    NA_CHECK_SUBSYS_ERROR(cls, auth_key_p == NULL, error, ret, NA_NOMEM,
        "Could not allocate auth_key");
    memcpy(auth_key_p, auth_key, auth_key_size);

    rc = fi_av_insert_auth_key(
        na_ofi_domain->fi_av, auth_key_p, auth_key_size, &fi_auth_key, 0);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_av_insert_auth_key() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    if (na_ofi_domain->av_user_id) {
        /* With FI_AV_USER_ID and auth_keys, set user id as auth key index. Auth
         * key index will be returned in fi_cq_err_entry::src_addr. */
        rc = fi_av_set_user_id(
            na_ofi_domain->fi_av, fi_auth_key, fi_auth_key, FI_AUTH_KEY);
        NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret,
            na_ofi_errno_to_na(-rc), "fi_av_set_user_id() failed, rc: %d (%s)",
            rc, fi_strerror(-rc));
    }

    NA_LOG_SUBSYS_DEBUG(
        addr, "Inserted new auth key, FI addr is %" PRIu64, fi_auth_key);

    rc = hg_hash_table_insert(na_ofi_domain->auth_key_map,
        (hg_hash_table_key_t) auth_key_p, (hg_hash_table_value_t) fi_auth_key);
    NA_CHECK_SUBSYS_ERROR(
        addr, rc == 0, error, ret, NA_NOMEM, "hg_hash_table_insert() failed");

    return NA_SUCCESS;

error:
    free(auth_key_p);

    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE fi_addr_t
na_ofi_auth_key_lookup(hg_hash_table_t *table, union na_ofi_auth_key *auth_key)
{
    hg_hash_table_value_t value = NULL;

    /* Lookup key */
    value = hg_hash_table_lookup(table, (hg_hash_table_key_t) auth_key);

    return (value == HG_HASH_TABLE_NULL) ? FI_ADDR_NOTAVAIL : (fi_addr_t) value;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_ofi_auth_key_hash(hg_hash_table_key_t key)
{
    /* TODO if we have more providers we'll need to pass prov_type */
    return na_ofi_cxi_auth_key_hash(
        &((const union na_ofi_auth_key *) key)->cxi_auth_key);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_ofi_cxi_auth_key_hash(const struct cxi_auth_key *key)
{
    return key->vni;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_auth_key_equal(hg_hash_table_key_t key1, hg_hash_table_key_t key2)
{
    /* TODO if we have more providers we'll need to pass prov_type */
    return na_ofi_cxi_auth_key_equal(
        &((const union na_ofi_auth_key *) key1)->cxi_auth_key,
        &((const union na_ofi_auth_key *) key2)->cxi_auth_key);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_cxi_auth_key_equal(
    const struct cxi_auth_key *key1, const struct cxi_auth_key *key2)
{
    /* ignore svc_id field */
    return key1->vni == key2->vni;
}

#endif

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_domain_open(const struct na_ofi_fabric *na_ofi_fabric,
    const void *auth_key, size_t num_auth_keys,
    enum na_traffic_class traffic_class, bool no_wait, struct fi_info *fi_info,
    struct na_ofi_domain **na_ofi_domain_p)
{
    struct na_ofi_domain *na_ofi_domain = NULL;
    struct fi_domain_attr *domain_attr = fi_info->domain_attr;
    union na_ofi_auth_key base_auth_key;
    const union na_ofi_auth_key *base_auth_key_p = NULL;
    char *env;
    bool skip_domain_ops = false;
    na_return_t ret;
    int rc;

    na_ofi_domain = (struct na_ofi_domain *) calloc(1, sizeof(*na_ofi_domain));
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_domain == NULL, error, ret, NA_NOMEM,
        "Could not allocate na_ofi_domain");
    hg_atomic_init64(&na_ofi_domain->requested_key, 0);
    /* No need to take a refcount on fabric */
    na_ofi_domain->fabric = na_ofi_fabric;

#ifndef _WIN32
    HG_LOG_ADD_COUNTER32(
        na, &na_ofi_domain->mr_reg_count, "mr_reg_count", "MR reg count");
#endif

    /* Dup name */
    na_ofi_domain->name = strdup(domain_attr->name);
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_domain->name == NULL, error, ret,
        NA_NOMEM, "Could not dup domain name");

    /* Auth key */
    if (num_auth_keys > 1) {
        na_ofi_domain->av_auth_key = true;
        base_auth_key_p = (const union na_ofi_auth_key *) auth_key;
    } else if (auth_key && ((const char *) auth_key)[0] != '\0') {
        size_t auth_key_size;

        ret = na_ofi_parse_auth_key((const char *) auth_key,
            na_ofi_fabric->prov_type, na_ofi_domain->name, &base_auth_key,
            &auth_key_size);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not parse auth key");

        /* If we're using FI_AV_AUTH_KEY, use same mechanism to handle single
         * auth key in order to keep addr fields populated */
#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
        if (na_ofi_prov_flags[na_ofi_fabric->prov_type] & NA_OFI_AV_AUTH_KEY) {
            na_ofi_domain->av_auth_key = true;
            base_auth_key_p = &base_auth_key;
        } else {
#endif
            domain_attr->auth_key = (void *) &base_auth_key;
            domain_attr->auth_key_size = auth_key_size;
#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
        }
#endif
    }

    /* Traffic class */
    if (traffic_class != NA_TC_UNSPEC)
        domain_attr->tclass = na_ofi_tclass(traffic_class);

    /* Force manual progress if no wait set or do not support
     * FI_WAIT_FD/FI_WAIT_SET. */
    if (no_wait || !(na_ofi_prov_flags[na_ofi_fabric->prov_type] &
                       (NA_OFI_WAIT_SET | NA_OFI_WAIT_FD))) {
        na_ofi_domain->no_wait = true;

        domain_attr->control_progress = FI_PROGRESS_MANUAL;
        domain_attr->data_progress = FI_PROGRESS_MANUAL;
    }

    /* Create the fi access domain */
    rc = fi_domain(
        na_ofi_fabric->fi_fabric, fi_info, &na_ofi_domain->fi_domain, NULL);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_domain() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    /* Cache max number of contexts */
    na_ofi_domain->context_max =
        MIN(domain_attr->tx_ctx_cnt, domain_attr->rx_ctx_cnt);

    /* Cache max key */
    NA_CHECK_SUBSYS_ERROR(cls, domain_attr->mr_key_size > 8, error, ret,
        NA_OVERFLOW, "MR key size (%zu) is not supported",
        domain_attr->mr_key_size);

    na_ofi_domain->max_key =
        (domain_attr->mr_key_size == 8)
            ? INT64_MAX
            : (int64_t) (1UL << (domain_attr->mr_key_size * 8)) - 1;
    NA_LOG_SUBSYS_DEBUG(cls, "MR max key is %" PRId64, na_ofi_domain->max_key);

    /* Cache max tag (TODO may want to increase) */
    NA_CHECK_SUBSYS_ERROR(cls, domain_attr->cq_data_size < 4, error, ret,
        NA_OVERFLOW, "CQ data size (%zu) is not supported",
        domain_attr->cq_data_size);
    na_ofi_domain->max_tag = UINT32_MAX;
    NA_LOG_SUBSYS_DEBUG(cls, "Msg max tag is %" PRIu64, na_ofi_domain->max_tag);

    NA_LOG_SUBSYS_DEBUG_EXT(cls, "fi_domain opened", "%s",
        fi_tostr(domain_attr, FI_TYPE_DOMAIN_ATTR));

    /* Set optional domain ops */
    env = getenv("NA_OFI_SKIP_DOMAIN_OPS");
    if (env != NULL)
        skip_domain_ops = (atoi(env) != 0);
    if (!skip_domain_ops) {
        ret = na_ofi_set_domain_ops(na_ofi_fabric->prov_type, na_ofi_domain);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not set domain ops");
    }

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    /* Check if we can use FI_AV_USER_ID */
    na_ofi_domain->av_user_id = (fi_info->caps & FI_AV_USER_ID);
#endif

    /* Open AV */
    ret = na_ofi_av_open(na_ofi_domain, na_ofi_fabric->prov_type, fi_info,
        (int) num_auth_keys, base_auth_key_p);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not open AV");

    *na_ofi_domain_p = na_ofi_domain;

    return NA_SUCCESS;

error:
    if (na_ofi_domain) {
        (void) na_ofi_av_close(na_ofi_domain);
        if (na_ofi_domain->fi_domain)
            (void) fi_close(&na_ofi_domain->fi_domain->fid);

        free(na_ofi_domain->name);
        free(na_ofi_domain);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_domain_close(
    struct na_ofi_domain *na_ofi_domain) HG_LOCK_NO_THREAD_SAFETY_ANALYSIS
{
    na_return_t ret;
    int rc;

    if (!na_ofi_domain)
        return NA_SUCCESS;

    NA_LOG_SUBSYS_DEBUG(cls, "Closing domain");

    /* Close AV */
    ret = na_ofi_av_close(na_ofi_domain);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not close AV");

    /* Close domain */
    if (na_ofi_domain->fi_domain) {
        rc = fi_close(&na_ofi_domain->fi_domain->fid);
        NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_close() domain failed, rc: %d (%s)", rc, fi_strerror(-rc));
        na_ofi_domain->fi_domain = NULL;
    }

    free(na_ofi_domain->name);
    free(na_ofi_domain);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_av_open(struct na_ofi_domain *na_ofi_domain,
    enum na_ofi_prov_type prov_type, const struct fi_info *fi_info,
    int num_auth_keys, const union na_ofi_auth_key *base_auth_key)
{
    struct fi_av_attr av_attr = {.type = FI_AV_UNSPEC};
    hg_hash_table_equal_func_t map_key_equal_func;
    na_return_t ret;
    int rc;

    /* Open fi address vector */
    if (na_ofi_prov_flags[prov_type] & NA_OFI_SEP)
        av_attr.rx_ctx_bits = NA_OFI_SEP_RX_CTX_BITS;
#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    if (na_ofi_domain->av_auth_key && na_ofi_domain->av_user_id) {
        NA_LOG_SUBSYS_DEBUG(cls, "Opening AV with FI_AV_USER_ID set");
        av_attr.flags = FI_AV_USER_ID;
    }
#endif
    rc = fi_av_open(
        na_ofi_domain->fi_domain, &av_attr, &na_ofi_domain->fi_av, NULL);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_av_open() failed, rc: %d (%s)", rc, fi_strerror(-rc));

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    if (na_ofi_domain->av_auth_key) {
        int i;

        na_ofi_domain->auth_key_map =
            hg_hash_table_new(na_ofi_auth_key_hash, na_ofi_auth_key_equal);
        NA_CHECK_SUBSYS_ERROR(addr, na_ofi_domain->auth_key_map == NULL, error,
            ret, NA_NOMEM, "Could not allocate auth key map");
        hg_hash_table_register_free_functions(
            na_ofi_domain->auth_key_map, free, NULL);

        /* Insert a block of allowed auth keys, this must be done before the
         * endpoint is enabled */
        for (i = 0; i < num_auth_keys; i++) {
            union na_ofi_auth_key auth_key;
            size_t auth_key_size;

            ret = na_ofi_gen_auth_key(
                base_auth_key, i, prov_type, &auth_key, &auth_key_size);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, error, ret, "Could not generate auth key");

            ret =
                na_ofi_auth_key_insert(na_ofi_domain, &auth_key, auth_key_size);
            NA_CHECK_SUBSYS_NA_ERROR(
                cls, error, ret, "Could not insert auth key");
        }
    }
#else
    (void) num_auth_keys;
    (void) base_auth_key;
#endif

    /* Create primary addr hash-table */
    switch ((int) fi_info->addr_format) {
        case FI_SOCKADDR_IN6:
            map_key_equal_func = na_ofi_addr_key_equal_sin6;
            break;
        case FI_SOCKADDR_IB:
            map_key_equal_func = na_ofi_addr_key_equal_sib;
            break;
        case FI_SOCKADDR_IN:
        case FI_ADDR_PSMX:
        case FI_ADDR_PSMX2:
        case FI_ADDR_OPX:
        case FI_ADDR_GNI:
        case FI_ADDR_CXI:
        case FI_ADDR_STR:
        default:
            map_key_equal_func = na_ofi_addr_key_equal_default;
            break;
    }

    /* Init rw lock */
    rc = hg_thread_rwlock_init(&na_ofi_domain->addr_map.lock);
    NA_CHECK_SUBSYS_ERROR(cls, rc != HG_UTIL_SUCCESS, error, ret, NA_NOMEM,
        "hg_thread_rwlock_init() failed");

    na_ofi_domain->addr_map.key_map =
        hg_hash_table_new(na_ofi_addr_key_hash, map_key_equal_func);
    NA_CHECK_SUBSYS_ERROR(addr, na_ofi_domain->addr_map.key_map == NULL, error,
        ret, NA_NOMEM, "Could not allocate key map");

    /* Create secondary hash-table to lookup by fi_addr if using FI_SOURCE and
     * FI_AV_USER_ID is not available */
    if ((fi_info->caps & FI_SOURCE_ERR) && !na_ofi_domain->av_user_id) {
        na_ofi_domain->addr_map.fi_map =
            hg_hash_table_new(na_ofi_fi_addr_hash, na_ofi_fi_addr_equal);
        NA_CHECK_SUBSYS_ERROR(addr, na_ofi_domain->addr_map.fi_map == NULL,
            error, ret, NA_NOMEM, "Could not allocate FI addr map");
    }

    return NA_SUCCESS;

error:
    (void) na_ofi_av_close(na_ofi_domain);
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_ofi_av_close(struct na_ofi_domain *na_ofi_domain)
{
    na_return_t ret;

    /* Close AV */
    if (na_ofi_domain->fi_av) {
        int rc = fi_close(&na_ofi_domain->fi_av->fid);
        NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret,
            na_ofi_errno_to_na(-rc), "fi_close() AV failed, rc: %d (%s)", rc,
            fi_strerror(-rc));
        na_ofi_domain->fi_av = NULL;
    }

    if (na_ofi_domain->auth_key_map != NULL) {
        hg_hash_table_free(na_ofi_domain->auth_key_map);
        na_ofi_domain->auth_key_map = NULL;
    }
    if (na_ofi_domain->addr_map.fi_map != NULL) {
        hg_hash_table_free(na_ofi_domain->addr_map.fi_map);
        na_ofi_domain->addr_map.fi_map = NULL;
    }
    if (na_ofi_domain->addr_map.key_map != NULL) {
        hg_hash_table_free(na_ofi_domain->addr_map.key_map);
        na_ofi_domain->addr_map.key_map = NULL;
        hg_thread_rwlock_destroy(&na_ofi_domain->addr_map.lock);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_endpoint_open(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, bool no_wait, bool sep,
    uint8_t max_contexts, size_t unexpected_msg_size_max,
    size_t expected_msg_size_max, struct fi_info *fi_info,
    struct na_ofi_endpoint **na_ofi_endpoint_p)
{
    struct na_ofi_endpoint *na_ofi_endpoint = NULL;
    size_t msg_size_max = NA_OFI_MSG_SIZE;
    na_return_t ret;

    na_ofi_endpoint =
        (struct na_ofi_endpoint *) calloc(1, sizeof(*na_ofi_endpoint));
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_endpoint == NULL, error, ret, NA_NOMEM,
        "Could not allocate na_ofi_endpoint");

    /* Define default msg size */
#ifdef NA_OFI_HAS_EXT_GNI_H
    if (na_ofi_fabric->prov_type == NA_OFI_PROV_GNI) {
        /* Get mbox max msg size */
        ret = na_ofi_gni_get_domain_op_value(
            na_ofi_domain, GNI_MBOX_MSG_MAX_SIZE, &msg_size_max);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret,
            "Could not get domain op value for GNI_MBOX_MSG_MAX_SIZE");
    }
#endif

    /* Set msg size limits */
    na_ofi_endpoint->unexpected_msg_size_max =
        (unexpected_msg_size_max > 0) ? unexpected_msg_size_max : msg_size_max;
    NA_CHECK_SUBSYS_ERROR(cls,
        na_ofi_endpoint->unexpected_msg_size_max >
            fi_info->ep_attr->max_msg_size,
        error, ret, NA_OVERFLOW,
        "Msg size max (%zu) larger than provider max (%zu)",
        na_ofi_endpoint->unexpected_msg_size_max,
        fi_info->ep_attr->max_msg_size);

    na_ofi_endpoint->expected_msg_size_max =
        (expected_msg_size_max > 0) ? expected_msg_size_max : msg_size_max;
    NA_CHECK_SUBSYS_ERROR(cls,
        na_ofi_endpoint->expected_msg_size_max > fi_info->ep_attr->max_msg_size,
        error, ret, NA_OVERFLOW,
        "Msg size max (%zu) larger than provider max (%zu)",
        na_ofi_endpoint->expected_msg_size_max, fi_info->ep_attr->max_msg_size);

    if (sep) {
        ret = na_ofi_sep_open(
            na_ofi_domain, fi_info, max_contexts, na_ofi_endpoint);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "na_ofi_sep_open() failed");
    } else {
        ret = na_ofi_basic_ep_open(
            na_ofi_fabric, na_ofi_domain, fi_info, no_wait, na_ofi_endpoint);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "na_ofi_basic_ep_open() failed");
    }

    *na_ofi_endpoint_p = na_ofi_endpoint;

    return NA_SUCCESS;

error:
    free(na_ofi_endpoint);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_basic_ep_open(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, struct fi_info *fi_info,
    bool no_wait, struct na_ofi_endpoint *na_ofi_endpoint)
{
    na_return_t ret;
    int rc;

    NA_LOG_SUBSYS_DEBUG(cls, "Opening standard endpoint");

    /* Create a transport level communication endpoint */
    rc = fi_endpoint(
        na_ofi_domain->fi_domain, fi_info, &na_ofi_endpoint->fi_ep, NULL);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_endpoint() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    /* Create event queues (CQ, wait sets) */
    ret = na_ofi_eq_open(
        na_ofi_fabric, na_ofi_domain, no_wait, &na_ofi_endpoint->eq);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not open event queues");

    /* Bind the CQ and AV to the endpoint */
    rc = fi_ep_bind(na_ofi_endpoint->fi_ep, &na_ofi_endpoint->eq->fi_cq->fid,
        FI_TRANSMIT | FI_RECV);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_ep_bind() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    rc = fi_ep_bind(na_ofi_endpoint->fi_ep, &na_ofi_domain->fi_av->fid, 0);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_ep_bind() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    /* When using FI_MULTI_RECV, make sure the recv buffer remains sufficiently
     * large until it is released */
    if (na_ofi_prov_extra_caps[na_ofi_fabric->prov_type] & FI_MULTI_RECV) {
        size_t old_min, old_min_len = sizeof(old_min);

        rc = fi_getopt(&na_ofi_endpoint->fi_ep->fid, FI_OPT_ENDPOINT,
            FI_OPT_MIN_MULTI_RECV, &old_min, &old_min_len);
        NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_getopt() failed, rc: %d (%s)", rc, fi_strerror(-rc));

        NA_LOG_SUBSYS_DEBUG(cls,
            "Default FI_OPT_MIN_MULTI_RECV is %zu, setting it to %zu", old_min,
            na_ofi_endpoint->unexpected_msg_size_max);

        rc = fi_setopt(&na_ofi_endpoint->fi_ep->fid, FI_OPT_ENDPOINT,
            FI_OPT_MIN_MULTI_RECV, &na_ofi_endpoint->unexpected_msg_size_max,
            sizeof(na_ofi_endpoint->unexpected_msg_size_max));
        NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_setopt() failed, rc: %d (%s)", rc, fi_strerror(-rc));
    }

    /* Enable the endpoint for communication, and commits the bind operations */
    rc = fi_enable(na_ofi_endpoint->fi_ep);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_enable() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    NA_LOG_SUBSYS_DEBUG_EXT(cls, "fi_endpoint opened", "%s",
        fi_tostr(fi_info->ep_attr, FI_TYPE_EP_ATTR));

    return NA_SUCCESS;

error:
    if (na_ofi_endpoint->fi_ep != NULL) {
        (void) fi_close(&na_ofi_endpoint->fi_ep->fid);
        na_ofi_endpoint->fi_ep = NULL;
    }
    if (na_ofi_endpoint->eq != NULL) {
        (void) na_ofi_eq_close(na_ofi_endpoint->eq);
        na_ofi_endpoint->eq = NULL;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_sep_open(const struct na_ofi_domain *na_ofi_domain,
    struct fi_info *fi_info, uint8_t max_contexts,
    struct na_ofi_endpoint *na_ofi_endpoint)
{
    na_return_t ret;
    int rc;

    NA_LOG_SUBSYS_DEBUG(cls, "Opening SEP endpoint");

    /* Set max contexts */
    fi_info->ep_attr->tx_ctx_cnt = max_contexts;
    fi_info->ep_attr->rx_ctx_cnt = max_contexts;

    /* Create a transport level communication endpoint (sep) */
    rc = fi_scalable_ep(
        na_ofi_domain->fi_domain, fi_info, &na_ofi_endpoint->fi_ep, NULL);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_scalable_ep() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    rc = fi_scalable_ep_bind(
        na_ofi_endpoint->fi_ep, &na_ofi_domain->fi_av->fid, 0);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_ep_bind() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    /* Enable the endpoint for communication, and commits the bind operations */
    rc = fi_enable(na_ofi_endpoint->fi_ep);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_enable() failed, rc: %d (%s)", rc, fi_strerror(-rc));

    return NA_SUCCESS;

error:
    if (na_ofi_endpoint->fi_ep != NULL) {
        (void) fi_close(&na_ofi_endpoint->fi_ep->fid);
        na_ofi_endpoint->fi_ep = NULL;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_endpoint_close(struct na_ofi_endpoint *na_ofi_endpoint)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    if (!na_ofi_endpoint)
        goto out;

    NA_LOG_SUBSYS_DEBUG(ctx, "Closing endpoint");

    /* Valid only when not using SEP */
    if (na_ofi_endpoint->eq && na_ofi_endpoint->eq->retry_op_queue) {
        /* Check that unexpected op queue is empty */
        bool empty = TAILQ_EMPTY(&na_ofi_endpoint->eq->retry_op_queue->queue);
        NA_CHECK_SUBSYS_ERROR(ctx, empty == false, out, ret, NA_BUSY,
            "Retry op queue should be empty");
    }

    /* Close endpoint */
    if (na_ofi_endpoint->fi_ep) {
        rc = fi_close(&na_ofi_endpoint->fi_ep->fid);
        NA_CHECK_SUBSYS_ERROR(ctx, rc != 0, out, ret, na_ofi_errno_to_na(-rc),
            "fi_close() endpoint failed, rc: %d (%s)", rc, fi_strerror(-rc));
        na_ofi_endpoint->fi_ep = NULL;
    }

    /* Close event queues */
    if (na_ofi_endpoint->eq) {
        ret = na_ofi_eq_close(na_ofi_endpoint->eq);
        NA_CHECK_SUBSYS_NA_ERROR(ctx, out, ret, "Could not close event queues");
    }

    /* Destroy source address */
    if (na_ofi_endpoint->src_addr)
        na_ofi_addr_destroy(na_ofi_endpoint->src_addr);

    free(na_ofi_endpoint);

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_eq_open(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, bool no_wait,
    struct na_ofi_eq **na_ofi_eq_p)
{
    struct na_ofi_eq *na_ofi_eq = NULL;
    struct fi_cq_attr cq_attr = {0};
    hg_cpu_set_t cpu_set;
    int cpu = -1;
    na_return_t ret;
    int rc;

#if !defined(_WIN32) && !defined(__APPLE__)
    /* If threads are bound to a particular CPU ID, use that ID as the signaling
     * vector CPU ID for that CQ. */
    CPU_ZERO(&cpu_set);
    rc = hg_thread_getaffinity(hg_thread_self(), &cpu_set);
    NA_CHECK_SUBSYS_ERROR(ctx, rc != HG_UTIL_SUCCESS, error, ret,
        NA_PROTOCOL_ERROR, "Could not retrieve CPU affinity");
    if (CPU_COUNT(&cpu_set) == 1) /* Only one CPU set */
        for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
            if (CPU_ISSET((size_t) cpu, &cpu_set))
                break;
#else
    (void) cpu_set;
#endif

    na_ofi_eq = (struct na_ofi_eq *) calloc(1, sizeof(*na_ofi_eq));
    NA_CHECK_SUBSYS_ERROR(ctx, na_ofi_eq == NULL, error, ret, NA_NOMEM,
        "Could not allocate na_ofi_eq");

    /* Initialize queue / mutex */
    na_ofi_eq->retry_op_queue = malloc(sizeof(*na_ofi_eq->retry_op_queue));
    NA_CHECK_SUBSYS_ERROR(ctx, na_ofi_eq->retry_op_queue == NULL, error, ret,
        NA_NOMEM, "Could not allocate retry_op_queue");
    TAILQ_INIT(&na_ofi_eq->retry_op_queue->queue);
    hg_thread_spin_init(&na_ofi_eq->retry_op_queue->lock);

    if (!no_wait) {
        if (na_ofi_prov_flags[na_ofi_fabric->prov_type] & NA_OFI_WAIT_FD)
            cq_attr.wait_obj = FI_WAIT_FD; /* Wait on fd */
        else {
            struct fi_wait_attr wait_attr = {0};

            /* Open wait set for other providers. */
            wait_attr.wait_obj = FI_WAIT_UNSPEC;
            rc = fi_wait_open(
                na_ofi_fabric->fi_fabric, &wait_attr, &na_ofi_eq->fi_wait);
            NA_CHECK_SUBSYS_ERROR(ctx, rc != 0, error, ret,
                na_ofi_errno_to_na(-rc), "fi_wait_open() failed, rc: %d (%s)",
                rc, fi_strerror(-rc));
            cq_attr.wait_obj = FI_WAIT_SET; /* Wait on wait set */
            cq_attr.wait_set = na_ofi_eq->fi_wait;
        }
    }
    cq_attr.wait_cond = FI_CQ_COND_NONE;
    cq_attr.format = FI_CQ_FORMAT_TAGGED;
    cq_attr.size = NA_OFI_CQ_DEPTH;
    if (cpu >= 0) {
        NA_LOG_SUBSYS_DEBUG(ctx, "Setting CQ signaling_vector to cpu %d", cpu);
        cq_attr.flags = FI_AFFINITY;
        cq_attr.signaling_vector = cpu;
    }
    rc =
        fi_cq_open(na_ofi_domain->fi_domain, &cq_attr, &na_ofi_eq->fi_cq, NULL);
    NA_CHECK_SUBSYS_ERROR(ctx, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_cq_open failed, rc: %d (%s)", rc, fi_strerror(-rc));

    *na_ofi_eq_p = na_ofi_eq;

    return NA_SUCCESS;

error:
    if (na_ofi_eq != NULL) {
        if (na_ofi_eq->fi_cq != NULL) {
            (void) fi_close(&na_ofi_eq->fi_cq->fid);
            na_ofi_eq->fi_cq = NULL;
        }
        if (na_ofi_eq->fi_wait != NULL) {
            (void) fi_close(&na_ofi_eq->fi_wait->fid);
            na_ofi_eq->fi_wait = NULL;
        }
        if (na_ofi_eq->retry_op_queue) {
            hg_thread_spin_destroy(&na_ofi_eq->retry_op_queue->lock);
            free(na_ofi_eq->retry_op_queue);
            na_ofi_eq->retry_op_queue = NULL;
        }
        free(na_ofi_eq);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_eq_close(struct na_ofi_eq *na_ofi_eq)
{
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* Close completion queue */
    if (na_ofi_eq->fi_cq) {
        rc = fi_close(&na_ofi_eq->fi_cq->fid);
        NA_CHECK_SUBSYS_ERROR(ctx, rc != 0, out, ret, na_ofi_errno_to_na(-rc),
            "fi_close() CQ failed, rc: %d (%s)", rc, fi_strerror(-rc));
        na_ofi_eq->fi_cq = NULL;
    }

    /* Close wait set */
    if (na_ofi_eq->fi_wait) {
        rc = fi_close(&na_ofi_eq->fi_wait->fid);
        NA_CHECK_SUBSYS_ERROR(ctx, rc != 0, out, ret, na_ofi_errno_to_na(-rc),
            "fi_close() wait failed, rc: %d (%s)", rc, fi_strerror(-rc));
        na_ofi_eq->fi_wait = NULL;
    }

    if (na_ofi_eq->retry_op_queue) {
        hg_thread_spin_destroy(&na_ofi_eq->retry_op_queue->lock);
        free(na_ofi_eq->retry_op_queue);
    }

    free(na_ofi_eq);

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_endpoint_get_src_addr(struct na_ofi_class *na_ofi_class)
{
    struct na_ofi_addr_key addr_key;
    int addr_format = (int) na_ofi_class->fi_info->addr_format;
    size_t addrlen = na_ofi_prov_addr_size(addr_format);
    na_return_t ret;
    int rc;

    /* Make sure expected addr format len is same as OFI addr len. In the case
     * of FI_ADDR_STR, just make sure we do not exceed the max string length */
    if (addr_format == FI_ADDR_STR) {
        NA_CHECK_SUBSYS_ERROR(addr,
            addrlen < na_ofi_class->fi_info->src_addrlen, error, ret,
            NA_PROTONOSUPPORT,
            "Address lengths do not match (expected %zu, got %zu)", addrlen,
            na_ofi_class->fi_info->src_addrlen);
    } else if (addr_format == FI_ADDR_OPX) {
        NA_CHECK_SUBSYS_ERROR(addr,
            (addrlen != sizeof(struct na_ofi_opx_addr) &&
                addrlen != sizeof(union na_ofi_opx_address)),
            error, ret, NA_PROTONOSUPPORT,
            "Address lengths do not match (expected %zu/%zu, got %zu)",
            sizeof(union na_ofi_opx_address), sizeof(struct na_ofi_opx_addr),
            na_ofi_class->fi_info->src_addrlen);
    } else {
        NA_CHECK_SUBSYS_ERROR(addr,
            addrlen != na_ofi_class->fi_info->src_addrlen, error, ret,
            NA_PROTONOSUPPORT,
            "Address lengths do not match (expected %zu, got %zu)", addrlen,
            na_ofi_class->fi_info->src_addrlen);
    }
    /* Retrieve endpoint addr */
    rc = fi_getname(
        &na_ofi_class->endpoint->fi_ep->fid, &addr_key.addr, &addrlen);
    NA_CHECK_SUBSYS_ERROR(addr, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_getname() failed, rc: %d (%s), addrlen: %zu", rc, fi_strerror(-rc),
        addrlen);

    /* Create key from addr for faster lookups */
    addr_key.val = na_ofi_raw_addr_to_key(addr_format, &addr_key.addr);
    NA_CHECK_SUBSYS_ERROR(addr, addr_key.val == 0, error, ret,
        NA_PROTONOSUPPORT, "Could not generate key from addr");

    /* Lookup/insert self address so that we can use it to send to ourself */
    ret = na_ofi_addr_map_insert(na_ofi_class, &na_ofi_class->domain->addr_map,
        &addr_key, FI_ADDR_NOTAVAIL, &na_ofi_class->endpoint->src_addr);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret, "Could not insert src address");

    na_ofi_addr_ref_incr(na_ofi_class->endpoint->src_addr);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_get_uri(const struct na_ofi_fabric *na_ofi_fabric,
    const struct na_ofi_domain *na_ofi_domain, char *buf, size_t *buf_size_p,
    const struct na_ofi_addr_key *addr_key)
{
    char fi_addr_str[NA_OFI_MAX_URI_LEN] = {'\0'}, *fi_addr_str_ptr;
    size_t fi_addr_strlen = NA_OFI_MAX_URI_LEN;
    size_t addr_strlen;
    na_return_t ret = NA_SUCCESS;
    int rc;

    /* Convert FI address to a printable string */
    fi_av_straddr(
        na_ofi_domain->fi_av, &addr_key->addr, fi_addr_str, &fi_addr_strlen);
    NA_CHECK_SUBSYS_ERROR(addr, fi_addr_strlen > NA_OFI_MAX_URI_LEN, out, ret,
        NA_OVERFLOW, "fi_av_straddr() address truncated, addrlen: %zu",
        fi_addr_strlen);

    NA_LOG_SUBSYS_DEBUG(addr, "fi_av_straddr() returned %s", fi_addr_str);

    /* Remove unnecessary "://" prefix from string if present */
    fi_addr_str_ptr = strstr(fi_addr_str, "://");
    if (fi_addr_str_ptr != NULL)
        fi_addr_str_ptr += 3;
    else
        fi_addr_str_ptr = fi_addr_str;

    addr_strlen =
        strlen(fi_addr_str_ptr) + strlen(na_ofi_fabric->prov_name) + 3;
    if (buf) {
        NA_CHECK_SUBSYS_ERROR(addr, addr_strlen >= *buf_size_p, out, ret,
            NA_OVERFLOW, "Buffer size (%zu) too small to copy addr",
            *buf_size_p);

        /* Generate URI */
        rc = snprintf(buf, *buf_size_p, "%s://%s", na_ofi_fabric->prov_name,
            fi_addr_str_ptr);
        NA_CHECK_SUBSYS_ERROR(addr, rc < 0 || rc > (int) *buf_size_p, out, ret,
            NA_OVERFLOW,
            "snprintf() failed or name truncated, rc: %d (expected %zu)", rc,
            (size_t) *buf_size_p);
    }
    *buf_size_p = addr_strlen + 1;

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static struct na_ofi_addr *
na_ofi_addr_alloc(struct na_ofi_class *na_ofi_class)
{
    struct na_ofi_addr *na_ofi_addr;

    na_ofi_addr = calloc(1, sizeof(*na_ofi_addr));
    if (na_ofi_addr)
        na_ofi_addr->class = na_ofi_class;

    return na_ofi_addr;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_addr_destroy(struct na_ofi_addr *na_ofi_addr)
{
    NA_LOG_SUBSYS_DEBUG(addr, "Destroying address %p", (void *) na_ofi_addr);

    na_ofi_addr_release(na_ofi_addr);
    free(na_ofi_addr);
}

/*---------------------------------------------------------------------------*/
#ifdef NA_OFI_HAS_ADDR_POOL
static struct na_ofi_addr *
na_ofi_addr_pool_get(struct na_ofi_class *na_ofi_class)
{
    struct na_ofi_addr *na_ofi_addr = NULL;

    hg_thread_spin_lock(&na_ofi_class->addr_pool.lock);
    na_ofi_addr = STAILQ_FIRST(&na_ofi_class->addr_pool.queue);
    if (na_ofi_addr) {
        STAILQ_REMOVE_HEAD(&na_ofi_class->addr_pool.queue, entry);
        hg_thread_spin_unlock(&na_ofi_class->addr_pool.lock);
    } else {
        hg_thread_spin_unlock(&na_ofi_class->addr_pool.lock);
        /* Fallback to allocation if pool is empty */
        na_ofi_addr = na_ofi_addr_alloc(na_ofi_class);
    }

    return na_ofi_addr;
}
#endif

/*---------------------------------------------------------------------------*/
static void
na_ofi_addr_release(struct na_ofi_addr *na_ofi_addr)
{
    if (na_ofi_addr->addr_key.val) {
        /* Removal is not needed when finalizing unless domain is shared */
        if (!na_ofi_addr->class->finalizing)
            na_ofi_addr_map_remove(
                &na_ofi_addr->class->domain->addr_map, &na_ofi_addr->addr_key);
        na_ofi_addr->addr_key.val = 0;
    }
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_addr_reset(
    struct na_ofi_addr *na_ofi_addr, struct na_ofi_addr_key *addr_key)
{
    /* One refcount for the caller to hold until addr_free */
    hg_atomic_init32(&na_ofi_addr->refcount, 1);

    /* Set FI addrs to invalid values */
    na_ofi_addr->fi_addr = FI_ADDR_NOTAVAIL;
    na_ofi_addr->fi_auth_key = FI_ADDR_NOTAVAIL;

    /* Keep copy of the key */
    na_ofi_addr->addr_key = *addr_key;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_create(struct na_ofi_class *na_ofi_class,
    struct na_ofi_addr_key *addr_key, struct na_ofi_addr **na_ofi_addr_p)
{
    struct na_ofi_addr *na_ofi_addr;
    na_return_t ret;

#ifdef NA_OFI_HAS_ADDR_POOL
    na_ofi_addr = na_ofi_addr_pool_get(na_ofi_class);
#else
    na_ofi_addr = na_ofi_addr_alloc(na_ofi_class);
#endif
    NA_CHECK_SUBSYS_ERROR(addr, na_ofi_addr == NULL, error, ret, NA_NOMEM,
        "Could not allocate addr");

    na_ofi_addr_reset(na_ofi_addr, addr_key);

    NA_LOG_SUBSYS_DEBUG(addr, "Created address %p", (void *) na_ofi_addr);

    *na_ofi_addr_p = na_ofi_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_addr_ref_incr(struct na_ofi_addr *na_ofi_addr)
{
    hg_atomic_incr32(&na_ofi_addr->refcount);
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_addr_ref_decr(struct na_ofi_addr *na_ofi_addr)
{
    /* If there are more references, return */
    if (hg_atomic_decr32(&na_ofi_addr->refcount) == 0) {
#ifdef NA_OFI_HAS_ADDR_POOL
        struct na_ofi_addr_pool *addr_pool = &na_ofi_addr->class->addr_pool;

        NA_LOG_SUBSYS_DEBUG(addr, "Releasing address %p", (void *) na_ofi_addr);
        na_ofi_addr_release(na_ofi_addr);

        /* Push address back to addr pool */
        hg_thread_spin_lock(&addr_pool->lock);
        STAILQ_INSERT_TAIL(&addr_pool->queue, na_ofi_addr, entry);
        hg_thread_spin_unlock(&addr_pool->lock);
#else
        na_ofi_addr_destroy(na_ofi_addr);
#endif
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void *
na_ofi_mem_alloc(struct na_ofi_class *na_ofi_class, size_t size,
    unsigned long *flags_p, size_t *alloc_size_p, struct fid_mr **mr_hdl_p)
{
    size_t page_size;
    void *mem_ptr = NULL;
    size_t alloc_size = 0;
    int rc;

#if !defined(_WIN32) && !defined(__APPLE__)
    page_size = (size_t) hg_mem_get_hugepage_size();
    if (page_size > 0 && size >= page_size) {
        /* Allocate a multiple of page size (TODO use extra space) */
        alloc_size = ((size % page_size) == 0)
                         ? size
                         : ((size / page_size) + 1) * page_size;

        mem_ptr = hg_mem_huge_alloc(alloc_size);
    }

    /* Allocate backend buffer */
    if (mem_ptr != NULL) {
        NA_LOG_SUBSYS_DEBUG(mem,
            "Allocated %zu bytes using hugepages at address %p", alloc_size,
            mem_ptr);
        *flags_p |= NA_OFI_ALLOC_HUGE;
    } else {
#endif
        page_size = (size_t) hg_mem_get_page_size();
        alloc_size = size;

        mem_ptr = hg_mem_aligned_alloc(page_size, size);
        NA_CHECK_SUBSYS_ERROR_NORET(mem, mem_ptr == NULL, error,
            "Could not allocate %d bytes", (int) size);

        NA_LOG_SUBSYS_DEBUG(mem,
            "Allocated %zu bytes using aligned alloc at address %p", alloc_size,
            mem_ptr);
#if !defined(_WIN32) && !defined(__APPLE__)
    }
#endif
    memset(mem_ptr, 0, alloc_size);

    /* Register buffer */
    rc = na_ofi_mem_buf_register(mem_ptr, alloc_size, *flags_p,
        (void **) mr_hdl_p, (void *) na_ofi_class);
    NA_CHECK_SUBSYS_ERROR_NORET(
        mem, rc != 0, error_free, "Could not register buffer");

    *alloc_size_p = alloc_size;

    return mem_ptr;

error_free:
    NA_LOG_SUBSYS_DEBUG(mem, "Freeing memory at address %p", mem_ptr);
    if (*flags_p & NA_OFI_ALLOC_HUGE) {
        (void) hg_mem_huge_free(mem_ptr, alloc_size);
    } else
        hg_mem_aligned_free(mem_ptr);
error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_mem_free(struct na_ofi_class *na_ofi_class, void *mem_ptr,
    size_t alloc_size, unsigned long flags, struct fid_mr *mr_hdl)
{
    int rc;

    NA_LOG_SUBSYS_DEBUG(mem, "Freeing memory at address %p", mem_ptr);

    /* Release MR handle is there was any */
    rc = na_ofi_mem_buf_deregister((void *) mr_hdl, (void *) na_ofi_class);
    NA_CHECK_SUBSYS_ERROR_NORET(
        mem, rc != 0, out, "Could not deregister buffer");

out:
    if (flags & NA_OFI_ALLOC_HUGE) {
        (void) hg_mem_huge_free(mem_ptr, alloc_size);
    } else
        hg_mem_aligned_free(mem_ptr);
    return;
}

/*---------------------------------------------------------------------------*/
static int
na_ofi_mem_buf_register(const void *buf, size_t len, unsigned long flags,
    void **handle_p, void *arg)
{
    struct na_ofi_class *na_ofi_class = (struct na_ofi_class *) arg;
    int ret = HG_UTIL_SUCCESS;

    /* Register memory if FI_MR_LOCAL is set and provider uses it */
    if (na_ofi_class->fi_info->domain_attr->mr_mode & FI_MR_LOCAL) {
        struct fid_mr *mr_hdl = NULL;
        uint64_t access = 0;
        int rc;

        if (!flags || (flags & NA_SEND))
            access |= FI_SEND;

        if (!flags || (flags & NA_RECV) || (flags & NA_MULTI_RECV))
            access |= FI_RECV;

        rc = fi_mr_reg(na_ofi_class->domain->fi_domain, buf, len, access,
            0 /* offset */, 0 /* requested key */, 0 /* flags */, &mr_hdl,
            NULL /* context */);
        NA_CHECK_SUBSYS_ERROR(mem, rc != 0, out, ret, HG_UTIL_FAIL,
            "fi_mr_reg() failed, rc: %d (%s), mr_reg_count: %d", rc,
            fi_strerror(-rc),
            hg_atomic_get32(na_ofi_class->domain->mr_reg_count));

        hg_atomic_incr32(na_ofi_class->domain->mr_reg_count);
        *handle_p = (void *) mr_hdl;
    } else
        *handle_p = NULL;

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static int
na_ofi_mem_buf_deregister(void *handle, void *arg)
{
    int ret = HG_UTIL_SUCCESS;

    /* Release MR handle is there was any */
    if (handle) {
        struct fid_mr *mr_hdl = (struct fid_mr *) handle;
        struct na_ofi_class *na_ofi_class = (struct na_ofi_class *) arg;
        int rc = fi_close(&mr_hdl->fid);
        NA_CHECK_SUBSYS_ERROR(mem, rc != 0, out, ret, HG_UTIL_FAIL,
            "fi_close() mr_hdl failed, rc: %d (%s)", rc, fi_strerror(-rc));
        hg_atomic_decr32(na_ofi_class->domain->mr_reg_count);
    }

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static uint64_t
na_ofi_mem_key_gen(struct na_ofi_domain *na_ofi_domain)
{
    return (hg_atomic_cas64(
               &na_ofi_domain->requested_key, na_ofi_domain->max_key, 0))
               ? 1 /* Incremented value */
               : (uint64_t) hg_atomic_incr64(&na_ofi_domain->requested_key);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_send(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context)
{
    ssize_t rc;

    NA_LOG_SUBSYS_DEBUG(msg,
        "Posting fi_senddata() (buf=%p, len=%zu, desc=%p, data=%" PRIu64
        ", dest_addr=%" PRIu64 ", context=%p)",
        msg_info->buf.const_ptr, msg_info->buf_size, msg_info->desc,
        msg_info->tag & NA_OFI_TAG_MASK, msg_info->fi_addr, context);

    rc = fi_senddata(ep, msg_info->buf.const_ptr, msg_info->buf_size,
        msg_info->desc, msg_info->tag & NA_OFI_TAG_MASK, msg_info->fi_addr,
        context);
    if (rc == 0)
        return NA_SUCCESS;
    else if (rc == -FI_EAGAIN)
        return NA_AGAIN;
    else {
        NA_LOG_SUBSYS_ERROR(msg,
            "fi_senddata() failed, rc: %zd (%s), buf=%p, len=%zu, desc=%p, "
            "data=%" PRIu64 ", dest_addr=%" PRIu64 ", context=%p",
            rc, fi_strerror((int) -rc), msg_info->buf.const_ptr,
            msg_info->buf_size, msg_info->desc, msg_info->tag & NA_OFI_TAG_MASK,
            msg_info->fi_addr, context);
        return na_ofi_errno_to_na((int) -rc);
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_recv(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context)
{
    ssize_t rc;

    NA_LOG_SUBSYS_DEBUG(msg,
        "Posting fi_recv() (buf=%p, len=%zu, desc=%p, src_addr=%" PRIu64
        ", context=%p)",
        msg_info->buf.ptr, msg_info->buf_size, msg_info->desc,
        msg_info->fi_addr, context);

    rc = fi_recv(ep, msg_info->buf.ptr, msg_info->buf_size, msg_info->desc,
        msg_info->fi_addr, context);
    if (rc == 0)
        return NA_SUCCESS;
    else if (rc == -FI_EAGAIN)
        return NA_AGAIN;
    else {
        NA_LOG_SUBSYS_ERROR(msg,
            "fi_recv() failed, rc: %zd (%s), buf=%p, len=%zu, desc=%p, "
            "src_addr=%" PRIu64 ", context=%p",
            rc, fi_strerror((int) -rc), msg_info->buf.ptr, msg_info->buf_size,
            msg_info->desc, msg_info->fi_addr, context);
        return na_ofi_errno_to_na((int) -rc);
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_multi_recv(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context)
{
    struct iovec msg_iov = {
        .iov_base = msg_info->buf.ptr, .iov_len = msg_info->buf_size};
    void *descs[1] = {msg_info->desc};
    struct fi_msg msg = {.msg_iov = &msg_iov,
        .desc = descs,
        .iov_count = 1,
        .addr = msg_info->fi_addr,
        .context = context,
        .data = 0};
    ssize_t rc;

    NA_LOG_SUBSYS_DEBUG(msg,
        "Posting fi_recvmsg() (iov_base=%p, iov_len=%zu, desc=%p, addr=%" PRIu64
        ", context=%p)",
        msg.msg_iov[0].iov_base, msg.msg_iov[0].iov_len,
        msg.desc ? msg.desc[0] : NULL, msg.addr, context);

    rc = fi_recvmsg(ep, &msg, FI_MULTI_RECV);
    if (rc == 0)
        return NA_SUCCESS;
    else if (rc == -FI_EAGAIN)
        return NA_AGAIN;
    else {
        NA_LOG_SUBSYS_ERROR(msg,
            "fi_recvmsg() failed, rc: %zd (%s), iov_base=%p, iov_len=%zu, "
            "desc=%p, addr=%" PRIu64 ", context=%p",
            rc, fi_strerror((int) -rc), msg.msg_iov[0].iov_base,
            msg.msg_iov[0].iov_len, msg.desc ? msg.desc[0] : NULL, msg.addr,
            context);
        return na_ofi_errno_to_na((int) -rc);
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_tag_send(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context)
{
    ssize_t rc;

    NA_LOG_SUBSYS_DEBUG(msg,
        "Posting fi_tsend() (buf=%p, len=%zu, desc=%p, dest_addr=%" PRIu64
        ", tag=%" PRIu64 ", context=%p)",
        msg_info->buf.const_ptr, msg_info->buf_size, msg_info->desc,
        msg_info->fi_addr, msg_info->tag, context);

    rc = fi_tsend(ep, msg_info->buf.const_ptr, msg_info->buf_size,
        msg_info->desc, msg_info->fi_addr, msg_info->tag, context);
    if (rc == 0)
        return NA_SUCCESS;
    else if (rc == -FI_EAGAIN)
        return NA_AGAIN;
    else {
        NA_LOG_SUBSYS_ERROR(msg,
            "fi_tsend() failed, rc: %zd (%s), buf=%p, len=%zu, desc=%p, "
            "dest_addr=%" PRIu64 ", tag=%" PRIu64 ", context=%p",
            rc, fi_strerror((int) -rc), msg_info->buf.const_ptr,
            msg_info->buf_size, msg_info->desc, msg_info->fi_addr,
            msg_info->tag, context);
        return na_ofi_errno_to_na((int) -rc);
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_tag_recv(
    struct fid_ep *ep, const struct na_ofi_msg_info *msg_info, void *context)
{
    ssize_t rc;

    NA_LOG_SUBSYS_DEBUG(msg,
        "Posting fi_trecv() (buf=%p, len=%zu, desc=%p, src_addr=%" PRIu64
        ", tag=%" PRIu64 ", tag_mask=%" PRIu64 ", context=%p)",
        msg_info->buf.ptr, msg_info->buf_size, msg_info->desc,
        msg_info->fi_addr, msg_info->tag, msg_info->tag_mask, context);

    rc = fi_trecv(ep, msg_info->buf.ptr, msg_info->buf_size, msg_info->desc,
        msg_info->fi_addr, msg_info->tag, msg_info->tag_mask, context);
    if (rc == 0)
        return NA_SUCCESS;
    else if (rc == -FI_EAGAIN)
        return NA_AGAIN;
    else {
        NA_LOG_SUBSYS_ERROR(msg,
            "fi_trecv() failed, rc: %zd (%s), buf=%p, len=%zu, desc=%p, "
            "src_addr=%" PRIu64 ", tag=%" PRIu64 ", tag_mask=%" PRIu64
            ", context=%p",
            rc, fi_strerror((int) -rc), msg_info->buf.ptr, msg_info->buf_size,
            msg_info->desc, msg_info->fi_addr, msg_info->tag,
            msg_info->tag_mask, context);
        return na_ofi_errno_to_na((int) -rc);
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_iov_get_index_offset(const struct iovec *iov, size_t iovcnt,
    na_offset_t offset, size_t *iov_start_index, na_offset_t *iov_start_offset)
{
    na_offset_t new_iov_offset = offset, next_offset = 0;
    size_t i, new_iov_start_index = 0;

    /* Get start index and handle offset */
    for (i = 0; i < iovcnt; i++) {
        next_offset += iov[i].iov_len;

        if (offset < next_offset) {
            new_iov_start_index = i;
            break;
        }
        new_iov_offset -= iov[i].iov_len;
    }

    *iov_start_index = new_iov_start_index;
    *iov_start_offset = new_iov_offset;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_iov_get_count(const struct iovec *iov, size_t iovcnt,
    size_t iov_start_index, na_offset_t iov_start_offset, size_t len)
{
    size_t remaining_len =
        len - MIN(len, iov[iov_start_index].iov_len - iov_start_offset);
    size_t i, iov_index;

    for (i = 1, iov_index = iov_start_index + 1;
         remaining_len > 0 && iov_index < iovcnt; i++, iov_index++) {
        /* Decrease remaining len from the len of data */
        remaining_len -= MIN(remaining_len, iov[iov_index].iov_len);
    }

    return i;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_iov_translate(const struct iovec *iov, void *desc, size_t iovcnt,
    size_t iov_start_index, na_offset_t iov_start_offset, size_t len,
    struct iovec *new_iov, void **new_desc, size_t new_iovcnt)
{
    size_t remaining_len = len;
    size_t i, iov_index;

    /* Offset is only within first segment */
    new_iov[0].iov_base =
        (char *) iov[iov_start_index].iov_base + iov_start_offset;
    new_desc[0] = desc;
    new_iov[0].iov_len =
        MIN(remaining_len, iov[iov_start_index].iov_len - iov_start_offset);
    remaining_len -= new_iov[0].iov_len;

    for (i = 1, iov_index = iov_start_index + 1;
         remaining_len > 0 && i < new_iovcnt && iov_index < iovcnt;
         i++, iov_index++) {
        new_iov[i].iov_base = iov[iov_index].iov_base;
        new_desc[i] = desc;
        new_iov[i].iov_len = MIN(remaining_len, iov[iov_index].iov_len);

        /* Decrease remaining len from the len of data */
        remaining_len -= new_iov[i].iov_len;
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_rma_iov_translate(const struct fi_info *fi_info, const struct iovec *iov,
    size_t iovcnt, uint64_t key, size_t iov_start_index,
    na_offset_t iov_start_offset, size_t len, struct fi_rma_iov *new_iov,
    size_t new_iovcnt)
{
    uint64_t addr;
    size_t remaining_len = len;
    size_t i, iov_index;

    /* Reference by virtual address, rather than a 0-based offset */
    addr = (fi_info->domain_attr->mr_mode & FI_MR_VIRT_ADDR)
               ? (uint64_t) iov[iov_start_index].iov_base
               : (uint64_t) iov[iov_start_index].iov_base -
                     (uint64_t) iov[0].iov_base;

    /* Offset is only within first segment */
    new_iov[0].addr = addr + iov_start_offset;
    new_iov[0].len =
        MIN(remaining_len, iov[iov_start_index].iov_len - iov_start_offset);
    new_iov[0].key = key;
    remaining_len -= new_iov[0].len;

    for (i = 1, iov_index = iov_start_index + 1;
         remaining_len > 0 && i < new_iovcnt && iov_index < iovcnt;
         i++, iov_index++) {
        addr = (fi_info->domain_attr->mr_mode & FI_MR_VIRT_ADDR)
                   ? (uint64_t) iov[iov_index].iov_base
                   : (uint64_t) iov[iov_index].iov_base -
                         (uint64_t) iov[0].iov_base;
        new_iov[i].addr = addr;
        new_iov[i].len = MIN(remaining_len, iov[iov_index].iov_len);
        new_iov[i].key = key;

        /* Decrease remaining len from the len of data */
        remaining_len -= new_iov[i].len;
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_rma_common(struct na_ofi_class *na_ofi_class, na_context_t *context,
    na_cb_type_t cb_type, na_cb_t callback, void *arg,
    na_ofi_rma_op_t fi_rma_op, const char *fi_rma_op_string,
    uint64_t fi_rma_flags, struct na_ofi_mem_handle *na_ofi_mem_handle_local,
    na_offset_t local_offset,
    struct na_ofi_mem_handle *na_ofi_mem_handle_remote,
    na_offset_t remote_offset, size_t length, struct na_ofi_addr *na_ofi_addr,
    uint8_t remote_id, struct na_ofi_op_id *na_ofi_op_id)
{
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    size_t local_iovcnt = (size_t) na_ofi_mem_handle_local->desc.info.iovcnt,
           remote_iovcnt = (size_t) na_ofi_mem_handle_remote->desc.info.iovcnt;
    struct iovec *local_iov = NA_OFI_IOV(
                     na_ofi_mem_handle_local->desc.iov, local_iovcnt),
                 *remote_iov = NA_OFI_IOV(
                     na_ofi_mem_handle_remote->desc.iov, remote_iovcnt);
    void *local_desc = fi_mr_desc(na_ofi_mem_handle_local->fi_mr);
    uint64_t remote_key = na_ofi_mem_handle_remote->desc.info.fi_mr_key;
    size_t local_iov_start_index = 0, remote_iov_start_index = 0;
    na_offset_t local_iov_start_offset = 0, remote_iov_start_offset = 0;
    struct na_ofi_rma_info *rma_info;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ofi_op_id->type));

    NA_OFI_OP_RESET(
        na_ofi_op_id, context, FI_RMA, cb_type, callback, arg, na_ofi_addr);

    /* Set RMA info */
    rma_info = &na_ofi_op_id->info.rma;
    rma_info->fi_rma_op = fi_rma_op;
    rma_info->fi_rma_op_string = fi_rma_op_string;
    rma_info->fi_rma_flags = fi_rma_flags;

    /* Translate local offset */
    if (local_offset > 0)
        na_ofi_iov_get_index_offset(local_iov, local_iovcnt, local_offset,
            &local_iov_start_index, &local_iov_start_offset);

    rma_info->local_iovcnt =
        (length == na_ofi_mem_handle_local->desc.info.len)
            ? local_iovcnt
            : na_ofi_iov_get_count(local_iov, local_iovcnt,
                  local_iov_start_index, local_iov_start_offset, length);

    if (rma_info->local_iovcnt > NA_OFI_IOV_STATIC_MAX) {
        rma_info->local_iov_storage.d = (struct iovec *) malloc(
            rma_info->local_iovcnt * sizeof(struct iovec));
        NA_CHECK_SUBSYS_ERROR(rma, rma_info->local_iov_storage.d == NULL,
            release, ret, NA_NOMEM,
            "Could not allocate iovec array (local_iovcnt=%zu)",
            rma_info->local_iovcnt);
        rma_info->local_iov = rma_info->local_iov_storage.d;

        rma_info->local_desc_storage.d =
            (void **) malloc(rma_info->local_iovcnt * sizeof(void *));
        NA_CHECK_SUBSYS_ERROR(rma, rma_info->local_desc_storage.d == NULL,
            release, ret, NA_NOMEM,
            "Could not allocate desc array (local_iovcnt=%zu)",
            rma_info->local_iovcnt);
        rma_info->local_desc = rma_info->local_desc_storage.d;
    } else {
        rma_info->local_iov = rma_info->local_iov_storage.s;
        rma_info->local_desc = rma_info->local_desc_storage.s;
    }

    /* TODO: support multiple local descs for each iov */
    na_ofi_iov_translate(local_iov, local_desc, local_iovcnt,
        local_iov_start_index, local_iov_start_offset, length,
        rma_info->local_iov, rma_info->local_desc, rma_info->local_iovcnt);

    /* Translate remote offset */
    if (remote_offset > 0)
        na_ofi_iov_get_index_offset(remote_iov, remote_iovcnt, remote_offset,
            &remote_iov_start_index, &remote_iov_start_offset);

    rma_info->remote_iovcnt =
        (length == na_ofi_mem_handle_remote->desc.info.len)
            ? remote_iovcnt
            : na_ofi_iov_get_count(remote_iov, remote_iovcnt,
                  remote_iov_start_index, remote_iov_start_offset, length);

    if (rma_info->remote_iovcnt > NA_OFI_IOV_STATIC_MAX) {
        rma_info->remote_iov_storage.d = (struct fi_rma_iov *) malloc(
            rma_info->remote_iovcnt * sizeof(struct fi_rma_iov));
        NA_CHECK_SUBSYS_ERROR(rma, rma_info->remote_iov_storage.d == NULL,
            release, ret, NA_NOMEM, "Could not allocate rma iovec");
        rma_info->remote_iov = rma_info->remote_iov_storage.d;
    } else
        rma_info->remote_iov = rma_info->remote_iov_storage.s;

    na_ofi_rma_iov_translate(na_ofi_class->fi_info, remote_iov, remote_iovcnt,
        remote_key, remote_iov_start_index, remote_iov_start_offset, length,
        rma_info->remote_iov, rma_info->remote_iovcnt);

    rma_info->fi_addr = na_ofi_class->use_sep
                            ? fi_rx_addr(na_ofi_addr->fi_addr, remote_id,
                                  NA_OFI_SEP_RX_CTX_BITS)
                            : na_ofi_addr->fi_addr;

    /* Post the OFI RMA operation */
    ret =
        na_ofi_rma_post(na_ofi_context->fi_tx, rma_info, &na_ofi_op_id->fi_ctx);
    if (ret != NA_SUCCESS) {
        if (ret == NA_AGAIN) {
            na_ofi_op_id->retry_op.rma = na_ofi_rma_post;
            na_ofi_op_retry(
                na_ofi_context, na_ofi_class->op_retry_timeout, na_ofi_op_id);
        } else
            NA_GOTO_SUBSYS_ERROR_NORET(rma, release, "Could not post RMA op");
    }

    return NA_SUCCESS;

release:
    na_ofi_rma_release(rma_info);

    NA_OFI_OP_RELEASE(na_ofi_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_rma_post(
    struct fid_ep *ep, const struct na_ofi_rma_info *rma_info, void *context)
{
    struct fi_msg_rma fi_msg_rma = {.msg_iov = rma_info->local_iov,
        .desc = rma_info->local_desc,
        .iov_count = rma_info->local_iovcnt,
        .addr = rma_info->fi_addr,
        .rma_iov = rma_info->remote_iov,
        .rma_iov_count = rma_info->remote_iovcnt,
        .context = context,
        .data = 0};
    ssize_t rc;

    NA_LOG_SUBSYS_DEBUG(rma,
        "Posting RMA op (%s, context=%p), iov_count=%zu, desc[0]=%p, "
        "msg_iov[0].iov_base=%p, msg_iov[0].iov_len=%zu, addr=%" PRIu64
        ", rma_iov_count=%zu, rma_iov[0].addr=%" PRIu64
        ", rma_iov[0].len=%zu, rma_iov[0].key=%" PRIu64 ", data=%" PRIu64,
        rma_info->fi_rma_op_string, context, fi_msg_rma.iov_count,
        fi_msg_rma.desc[0], fi_msg_rma.msg_iov[0].iov_base,
        fi_msg_rma.msg_iov[0].iov_len, fi_msg_rma.addr,
        fi_msg_rma.rma_iov_count, fi_msg_rma.rma_iov[0].addr,
        fi_msg_rma.rma_iov[0].len, fi_msg_rma.rma_iov[0].key, fi_msg_rma.data);

    /* Post the OFI RMA operation */
    rc = rma_info->fi_rma_op(ep, &fi_msg_rma, rma_info->fi_rma_flags);
    if (rc == 0)
        return NA_SUCCESS;
    else if (rc == -FI_EAGAIN)
        return NA_AGAIN;
    else {
        NA_LOG_SUBSYS_ERROR(rma,
            "%s() failed, rc: %zd (%s), iov_count=%zu, desc[0]=%p, "
            "msg_iov[0].iov_base=%p, msg_iov[0].iov_len=%zu, addr=%" PRIu64
            ", rma_iov_count=%zu, rma_iov[0].addr=%" PRIu64
            ", rma_iov[0].len=%zu, rma_iov[0].key=%" PRIu64
            ", context=%p, data=%" PRIu64,
            rma_info->fi_rma_op_string, rc, fi_strerror((int) -rc),
            fi_msg_rma.iov_count, fi_msg_rma.desc[0],
            fi_msg_rma.msg_iov[0].iov_base, fi_msg_rma.msg_iov[0].iov_len,
            fi_msg_rma.addr, fi_msg_rma.rma_iov_count,
            fi_msg_rma.rma_iov[0].addr, fi_msg_rma.rma_iov[0].len,
            fi_msg_rma.rma_iov[0].key, fi_msg_rma.context, fi_msg_rma.data);
        return na_ofi_errno_to_na((int) -rc);
    }
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_rma_release(struct na_ofi_rma_info *rma_info)
{
    /* Can free extra IOVs here */
    if (rma_info->local_iovcnt > NA_OFI_IOV_STATIC_MAX) {
        free(rma_info->local_iov_storage.d);
        rma_info->local_iov_storage.d = NULL;
    }
    if (rma_info->remote_iovcnt > NA_OFI_IOV_STATIC_MAX) {
        free(rma_info->remote_iov_storage.d);
        rma_info->remote_iov_storage.d = NULL;
    }
}

/*---------------------------------------------------------------------------*/
static bool
na_ofi_cq_can_poll_multi(
    struct na_ofi_op_queue *multi_op_queue, unsigned int *count_p)
{
    unsigned int count = 0;
    struct na_ofi_op_id *na_ofi_op_id;
    bool ret = true;

    hg_thread_spin_lock(&multi_op_queue->lock);
    TAILQ_FOREACH (na_ofi_op_id, &multi_op_queue->queue, multi) {
        struct na_ofi_completion_multi *completion_multi =
            &na_ofi_op_id->completion_data_storage.multi;
        unsigned int multi_count =
            na_ofi_completion_multi_count(completion_multi);

        count += multi_count;
        if ((completion_multi->size - multi_count) < NA_OFI_CQ_EVENT_NUM) {
            if (count_p != NULL)
                *count_p = count;
            ret = false; /* not enough space left in queue */
            break;
        }
    }
    hg_thread_spin_unlock(&multi_op_queue->lock);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_poll_no_source(struct na_ofi_class *na_ofi_class,
    struct na_ofi_context *na_ofi_context, unsigned int *count_p)
{
    struct fi_cq_tagged_entry cq_events[NA_OFI_CQ_EVENT_NUM];
    unsigned int i, count = 0;
    bool err_avail = false;
    na_return_t ret;

    ret = na_ofi_cq_read(na_ofi_context->eq->fi_cq, cq_events,
        NA_OFI_CQ_EVENT_NUM, &count, &err_avail);
    NA_CHECK_SUBSYS_NA_ERROR(
        poll, error, ret, "Could not read events from context CQ");

    if (unlikely(err_avail)) {
        ret = na_ofi_cq_readerr(na_ofi_context->eq->fi_cq, NULL, NULL, NULL);
        NA_CHECK_SUBSYS_NA_ERROR(
            poll, error, ret, "Could not read error events from context CQ");
    }

    for (i = 0; i < count; i++) {
        struct na_ofi_op_id *na_ofi_op_id =
            container_of(cq_events[i].op_context, struct na_ofi_op_id, fi_ctx);
        struct na_ofi_addr *na_ofi_addr = NULL;

        NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret,
            NA_INVALID_ARG, "Invalid operation ID");

        if (na_ofi_op_id->type == NA_CB_RECV_UNEXPECTED ||
            na_ofi_op_id->type == NA_CB_MULTI_RECV_UNEXPECTED) {
            ret = na_ofi_cq_process_raw_src_addr(na_ofi_class,
                (na_ofi_op_id->type == NA_CB_MULTI_RECV_UNEXPECTED)
                    ? cq_events[i].buf
                    : na_ofi_op_id->info.msg.buf.ptr,
                cq_events[i].len, &na_ofi_addr);
            NA_CHECK_SUBSYS_NA_ERROR(
                msg, error, ret, "Could not process raw src addr");
        }

        ret = na_ofi_cq_process_event(na_ofi_class, &cq_events[i], na_ofi_addr);
        NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret, "Could not process event");
    }

    *count_p = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_poll_fi_source(struct na_ofi_class *na_ofi_class,
    struct na_ofi_context *na_ofi_context, unsigned int *count_p)
{
    struct fi_cq_tagged_entry cq_events[NA_OFI_CQ_EVENT_NUM];
    fi_addr_t src_addrs[NA_OFI_CQ_EVENT_NUM];
    struct na_ofi_src_err src_err;
    struct na_ofi_src_err *src_err_p = NULL;
    unsigned int i, count = 0;
    bool err_avail = false;
    na_return_t ret;

    ret = na_ofi_cq_readfrom(na_ofi_context->eq->fi_cq, cq_events,
        NA_OFI_CQ_EVENT_NUM, src_addrs, &count, &err_avail);
    NA_CHECK_SUBSYS_NA_ERROR(
        poll, error, ret, "Could not read events from context CQ");

    if (unlikely(err_avail)) {
        ret = na_ofi_cq_readerr(
            na_ofi_context->eq->fi_cq, &cq_events[0], &src_err, &count);
        NA_CHECK_SUBSYS_NA_ERROR(
            poll, error, ret, "Could not read error events from context CQ");
        src_err_p = &src_err;
    }

    for (i = 0; i < count; i++) {
        struct na_ofi_op_id *na_ofi_op_id =
            container_of(cq_events[i].op_context, struct na_ofi_op_id, fi_ctx);
        struct na_ofi_addr *na_ofi_addr = NULL;

        NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret,
            NA_INVALID_ARG, "Invalid operation ID");

        if (na_ofi_op_id->type == NA_CB_RECV_UNEXPECTED ||
            na_ofi_op_id->type == NA_CB_MULTI_RECV_UNEXPECTED) {
            ret = na_ofi_cq_process_src_addr(
                na_ofi_class, src_addrs[i], src_err_p, &na_ofi_addr);
            NA_CHECK_SUBSYS_NA_ERROR(
                poll, error, ret, "Could not process src addr");
        }

        ret = na_ofi_cq_process_event(na_ofi_class, &cq_events[i], na_ofi_addr);
        NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret, "Could not process event");
    }

    *count_p = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_read(struct fid_cq *cq, struct fi_cq_tagged_entry cq_events[],
    unsigned int max_count, unsigned int *count_p, bool *err_avail_p)
{
    na_return_t ret;
    ssize_t rc;

    rc = fi_cq_read(cq, cq_events, max_count);
    if (rc > 0) { /* events available */
        *count_p = (unsigned int) rc;
        *err_avail_p = false;
    } else if (rc == -FI_EAGAIN) { /* no event available */
        *count_p = 0;
        *err_avail_p = false;
    } else if (rc == -FI_EAVAIL) {
        *count_p = 0;
        *err_avail_p = true;
    } else
        NA_GOTO_SUBSYS_ERROR(poll, error, ret, na_ofi_errno_to_na((int) -rc),
            "fi_cq_read() failed, rc: %zd (%s)", rc, fi_strerror((int) -rc));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_readfrom(struct fid_cq *cq, struct fi_cq_tagged_entry cq_events[],
    unsigned int max_count, fi_addr_t *src_addrs, unsigned int *count_p,
    bool *err_avail_p)
{
    na_return_t ret;
    ssize_t rc;

    rc = fi_cq_readfrom(cq, cq_events, max_count, src_addrs);
    if (rc > 0) { /* events available */
        *count_p = (unsigned int) rc;
        *err_avail_p = false;
    } else if (rc == -FI_EAGAIN) { /* no event available */
        *count_p = 0;
        *err_avail_p = false;
    } else if (rc == -FI_EAVAIL) {
        *count_p = 0;
        *err_avail_p = true;
    } else
        NA_GOTO_SUBSYS_ERROR(poll, error, ret, na_ofi_errno_to_na((int) -rc),
            "fi_cq_readfrom() failed, rc: %zd (%s)", rc,
            fi_strerror((int) -rc));

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_readerr(struct fid_cq *cq, struct fi_cq_tagged_entry *cq_event,
    struct na_ofi_src_err *src_err, unsigned int *count_p)
{
    struct fi_cq_err_entry cq_err;
    na_return_t ret = NA_SUCCESS;
    ssize_t rc;

    memset(&cq_err, 0, sizeof(cq_err));

    /* Prevent provider from internally allocating resources */
    if (src_err != NULL) {
        cq_err.err_data = &src_err->addr;
        cq_err.err_data_size = sizeof(src_err->addr);
    }

    /* Read error entry */
    rc = fi_cq_readerr(cq, &cq_err, 0 /* flags */);
    NA_CHECK_SUBSYS_ERROR(poll, rc != 1, out, ret,
        na_ofi_errno_to_na((int) -rc), "fi_cq_readerr() failed, rc: %zd (%s)",
        rc, fi_strerror((int) -rc));

    switch (cq_err.err) {
        case FI_ECANCELED: {
            struct na_ofi_op_id *na_ofi_op_id = NULL;

            NA_CHECK_SUBSYS_ERROR(op, cq_err.op_context == NULL, out, ret,
                NA_INVALID_ARG, "Invalid operation context");
            na_ofi_op_id =
                container_of(cq_err.op_context, struct na_ofi_op_id, fi_ctx);
            NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, out, ret,
                NA_INVALID_ARG, "Invalid operation ID");

            NA_CHECK_SUBSYS_ERROR(op,
                hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED,
                out, ret, NA_FAULT, "Operation ID was completed");
            NA_LOG_SUBSYS_DEBUG(op, "FI_ECANCELED event on operation ID %p",
                (void *) na_ofi_op_id);

            /* When tearing down connections, it is possible that operations
            will be canceled by libfabric itself.

            NA_CHECK_SUBSYS_WARNING(op,
                !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_CANCELED),
                "Operation ID was not canceled by user");
            */

            /* Complete operation in canceled state */
            na_ofi_op_id->complete(na_ofi_op_id, true, NA_CANCELED);
        } break;

        case FI_EADDRNOTAVAIL:
            NA_CHECK_SUBSYS_ERROR(op, src_err == NULL || cq_event == NULL, out,
                ret, NA_PROTONOSUPPORT,
                "FI_EADDRNOTAVAIL reported, not supported");
            NA_CHECK_SUBSYS_ERROR(op,
                cq_err.err_data_size > sizeof(src_err->addr), out, ret,
                NA_PROTONOSUPPORT, "err_data_size too large (%zu > %zu)",
                cq_err.err_data_size, sizeof(src_err->addr));

            memcpy(cq_event, &cq_err, sizeof(struct fi_cq_tagged_entry));
            /* provider should have copied err_data, emit warning if not */
            if (cq_err.err_data != &src_err->addr) {
                NA_LOG_SUBSYS_WARNING(op, "err_data was not copied");
                memcpy(&src_err->addr, cq_err.err_data, cq_err.err_data_size);
            }
            src_err->addrlen = cq_err.err_data_size;
#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
            src_err->fi_auth_key = cq_err.src_addr;
#else
            src_err->fi_auth_key = FI_ADDR_NOTAVAIL;
#endif
            *count_p = 1;
            break;

        default:
            NA_LOG_SUBSYS_WARNING(op,
                "fi_cq_readerr() got err: %d (%s), "
                "prov_errno: %d (%s)",
                cq_err.err, fi_strerror(cq_err.err), cq_err.prov_errno,
                fi_cq_strerror(
                    cq, cq_err.prov_errno, cq_err.err_data, NULL, 0));

            if (cq_err.op_context == NULL)
                break;
            else {
                struct na_ofi_op_id *na_ofi_op_id = container_of(
                    cq_err.op_context, struct na_ofi_op_id, fi_ctx);
                na_return_t na_ret = na_ofi_errno_to_na(cq_err.err);

                NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, out, ret,
                    NA_INVALID_ARG, "Invalid operation ID");
                NA_LOG_SUBSYS_DEBUG(op, "error event on operation ID %p",
                    (void *) na_ofi_op_id);

                NA_CHECK_SUBSYS_ERROR(op,
                    hg_atomic_get32(&na_ofi_op_id->status) &
                        NA_OFI_OP_COMPLETED,
                    out, ret, NA_FAULT, "Operation ID was completed");

                if (hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_ERRORED) &
                    NA_OFI_OP_CANCELED)
                    break;

                /* Abort other retries if peer is unreachable */
                if (na_ret == NA_HOSTUNREACH && na_ofi_op_id->addr)
                    na_ofi_op_retry_abort_addr(
                        NA_OFI_CONTEXT(na_ofi_op_id->context),
                        na_ofi_op_id->addr->fi_addr, NA_HOSTUNREACH);

                /* Complete operation in error state */
                na_ofi_op_id->complete(na_ofi_op_id, true, na_ret);
            }
            break;
    }

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_process_src_addr(struct na_ofi_class *na_ofi_class,
    fi_addr_t src_addr, struct na_ofi_src_err *src_err,
    struct na_ofi_addr **na_ofi_addr_p)
{
    struct na_ofi_addr *na_ofi_addr = NULL;
    na_return_t ret;

    if (unlikely(src_err != NULL)) {
        ret = na_ofi_cq_process_fi_src_err(na_ofi_class, src_err, &na_ofi_addr);
        NA_CHECK_SUBSYS_NA_ERROR(
            msg, error, ret, "Could not process FI src error addr");
    } else {
        ret =
            na_ofi_cq_process_fi_src_addr(na_ofi_class, src_addr, &na_ofi_addr);
        NA_CHECK_SUBSYS_NA_ERROR(
            msg, error, ret, "Could not process FI src addr");
    }

    *na_ofi_addr_p = na_ofi_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_process_fi_src_addr(struct na_ofi_class *na_ofi_class,
    fi_addr_t src_addr, struct na_ofi_addr **na_ofi_addr_p)
{
    struct na_ofi_addr *na_ofi_addr = NULL;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(addr, src_addr == FI_ADDR_NOTAVAIL, error, ret,
        NA_INVALID_ARG, "Invalid FI addr (%" PRIu64 ")", src_addr);
    NA_CHECK_SUBSYS_ERROR(addr, !(na_ofi_class->fi_info->caps & FI_SOURCE),
        error, ret, NA_PROTOCOL_ERROR,
        "Provider should not be using FI_SOURCE");

    NA_LOG_SUBSYS_DEBUG(
        addr, "Retrieving address for FI addr %" PRIu64, src_addr);

    /* Bypass lookup if FI_AV_USER_ID is used */
    na_ofi_addr = (na_ofi_class->domain->av_user_id)
                      ? (struct na_ofi_addr *) src_addr
                      : na_ofi_fi_addr_map_lookup(
                            &na_ofi_class->domain->addr_map, &src_addr);
    NA_CHECK_SUBSYS_ERROR(addr, na_ofi_addr == NULL, error, ret, NA_NOENTRY,
        "No entry found for previously inserted src addr");

    na_ofi_addr_ref_incr(na_ofi_addr);

    *na_ofi_addr_p = na_ofi_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_process_fi_src_err(struct na_ofi_class *na_ofi_class,
    struct na_ofi_src_err *src_err, struct na_ofi_addr **na_ofi_addr_p)
{
    struct na_ofi_addr_key addr_key = {.addr = src_err->addr};
    int addr_format = (int) na_ofi_class->fi_info->addr_format;
    na_return_t ret;

    /* Create key from addr for faster lookups */
    addr_key.val = na_ofi_raw_addr_to_key(addr_format, &addr_key.addr);
    NA_CHECK_SUBSYS_ERROR(addr, addr_key.val == 0, error, ret,
        NA_PROTONOSUPPORT, "Could not generate key from addr");

    /* Lookup key and create new addr if it does not exist */
    ret = na_ofi_addr_key_lookup(
        na_ofi_class, &addr_key, src_err->fi_auth_key, na_ofi_addr_p);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret, "Could not lookup address");

    NA_LOG_SUBSYS_DEBUG(addr, "Retrieved address for FI addr %" PRIu64,
        (*na_ofi_addr_p)->fi_addr);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_process_raw_src_addr(struct na_ofi_class *na_ofi_class,
    const void *buf, size_t len, struct na_ofi_addr **na_ofi_addr_p)
{
    struct na_ofi_addr_key addr_key;
    int addr_format = (int) na_ofi_class->fi_info->addr_format;
    union na_ofi_auth_key auth_key;
    fi_addr_t fi_auth_key = FI_ADDR_NOTAVAIL;
    na_return_t ret;

    ret = na_ofi_raw_addr_deserialize(
        addr_format, &addr_key.addr, &auth_key, buf, len);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not deserialize address key");

    /* Create key from addr for faster lookups */
    addr_key.val = na_ofi_raw_addr_to_key(addr_format, &addr_key.addr);
    NA_CHECK_SUBSYS_ERROR(addr, addr_key.val == 0, error, ret,
        NA_PROTONOSUPPORT, "Could not generate key from addr");

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    if (na_ofi_class->domain->av_auth_key) {
        fi_auth_key = na_ofi_auth_key_lookup(
            na_ofi_class->domain->auth_key_map, &auth_key);
        NA_CHECK_SUBSYS_ERROR(addr, fi_auth_key == FI_ADDR_NOTAVAIL, error, ret,
            NA_NOENTRY, "Could not find auth key");
    }
#endif

    /* Lookup key and create new addr if it does not exist */
    ret = na_ofi_addr_key_lookup(
        na_ofi_class, &addr_key, fi_auth_key, na_ofi_addr_p);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret, "Could not lookup address");

    NA_LOG_SUBSYS_DEBUG(addr, "Retrieved address for FI addr %" PRIu64,
        (*na_ofi_addr_p)->fi_addr);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_process_event(struct na_ofi_class *na_ofi_class,
    const struct fi_cq_tagged_entry *cq_event, struct na_ofi_addr *na_ofi_addr)
{
    struct na_ofi_op_id *na_ofi_op_id =
        container_of(cq_event->op_context, struct na_ofi_op_id, fi_ctx);
    bool complete = true;
    na_return_t ret;

    /* Cannot have an already completed operation ID, sanity check */
    NA_CHECK_SUBSYS_ERROR(op,
        hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED, error,
        ret, NA_FAULT, "Operation ID was completed");
    NA_CHECK_SUBSYS_ERROR(op, !(cq_event->flags & na_ofi_op_id->fi_op_flags),
        error, ret, NA_PROTONOSUPPORT,
        "Unsupported CQ event flags: 0x%" PRIx64 ", expected 0x%" PRIx64,
        cq_event->flags, na_ofi_op_id->fi_op_flags);

    NA_LOG_SUBSYS_DEBUG(op,
        "CQ event (%s, op id=%p, context=%p, flags=0x%" PRIx64
        ", len=%zu, buf=%p, data=%" PRIu64 ", tag=%" PRIu64 ")",
        na_cb_type_to_string(na_ofi_op_id->type), (void *) na_ofi_op_id,
        cq_event->op_context, cq_event->flags, cq_event->len, cq_event->buf,
        cq_event->data, cq_event->tag);

    switch (na_ofi_op_id->type) {
        case NA_CB_RECV_UNEXPECTED:
            /* Default to cq_event->tag for backward compatibility */
            ret = na_ofi_cq_process_recv_unexpected(na_ofi_class,
                &na_ofi_op_id->info.msg,
                &na_ofi_op_id->completion_data->callback_info.info
                     .recv_unexpected,
                na_ofi_op_id->info.msg.buf.ptr, cq_event->len, na_ofi_addr,
                (cq_event->data > 0) ? cq_event->data : cq_event->tag);
            NA_CHECK_SUBSYS_NA_ERROR(
                msg, error, ret, "Could not process unexpected recv event");
            break;
        case NA_CB_MULTI_RECV_UNEXPECTED:
            complete = cq_event->flags & FI_MULTI_RECV;

            ret = na_ofi_cq_process_multi_recv_unexpected(na_ofi_class,
                &na_ofi_op_id->info.msg,
                &na_ofi_op_id->completion_data->callback_info.info
                     .multi_recv_unexpected,
                cq_event->buf, cq_event->len, na_ofi_addr, cq_event->data,
                complete);
            NA_CHECK_SUBSYS_NA_ERROR(msg, error, ret,
                "Could not process unexpected multi recv event");
            break;
        case NA_CB_RECV_EXPECTED:
            ret = na_ofi_cq_process_recv_expected(&na_ofi_op_id->info.msg,
                &na_ofi_op_id->completion_data->callback_info.info
                     .recv_expected,
                na_ofi_op_id->info.msg.buf.ptr, cq_event->len, cq_event->tag);
            NA_CHECK_SUBSYS_NA_ERROR(
                msg, error, ret, "Could not process expected recv event");
            break;
        case NA_CB_PUT:
        case NA_CB_GET:
            na_ofi_rma_release(&na_ofi_op_id->info.rma);
            break;
        case NA_CB_SEND_UNEXPECTED:
        case NA_CB_SEND_EXPECTED:
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(op, error, ret, NA_INVALID_ARG,
                "Operation type %d not supported", na_ofi_op_id->type);
    }

    na_ofi_op_id->complete(na_ofi_op_id, complete, NA_SUCCESS);

    return NA_SUCCESS;

error:
    if (na_ofi_addr != NULL)
        na_ofi_addr_ref_decr(na_ofi_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ofi_cq_process_recv_unexpected(struct na_ofi_class *na_ofi_class,
    const struct na_ofi_msg_info *msg_info,
    struct na_cb_info_recv_unexpected *recv_unexpected_info, void *buf,
    size_t len, struct na_ofi_addr *na_ofi_addr, uint64_t tag)
{
    na_return_t ret;

    /* Sanity checks */
    NA_CHECK_SUBSYS_ERROR(msg, msg_info->buf.ptr != buf, error, ret, NA_FAULT,
        "Invalid buffer access (Expected %p, got %p)", msg_info->buf.ptr, buf);
    NA_CHECK_SUBSYS_ERROR(msg, len > msg_info->buf_size, error, ret, NA_MSGSIZE,
        "Unexpected recv msg size too large for buffer (expected %zu, got %zu)",
        msg_info->buf_size, len);
    NA_CHECK_SUBSYS_ERROR(msg,
        (tag & NA_OFI_TAG_MASK) > na_ofi_class->domain->max_tag, error, ret,
        NA_OVERFLOW, "Invalid tag value %" PRIu64, tag);

    /* Fill unexpected info */
    recv_unexpected_info->actual_buf_size = (size_t) len;
    recv_unexpected_info->source = (na_addr_t *) na_ofi_addr;
    recv_unexpected_info->tag = (na_tag_t) (tag & NA_OFI_TAG_MASK);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ofi_cq_process_multi_recv_unexpected(struct na_ofi_class *na_ofi_class,
    const struct na_ofi_msg_info *msg_info,
    struct na_cb_info_multi_recv_unexpected *multi_recv_unexpected_info,
    void *buf, size_t len, struct na_ofi_addr *na_ofi_addr, uint64_t tag,
    bool last)
{
    na_return_t ret;

    /* Sanity checks */
    NA_CHECK_SUBSYS_ERROR(msg, len > msg_info->buf_size, error, ret, NA_MSGSIZE,
        "Unexpected recv msg size too large for buffer (expected %zu, got %zu)",
        msg_info->buf_size, len);
    NA_CHECK_SUBSYS_ERROR(msg, tag > na_ofi_class->domain->max_tag, error, ret,
        NA_OVERFLOW, "Invalid tag value %" PRIu64, tag);
    NA_LOG_SUBSYS_DEBUG(msg, "Multi-recv completion set to: %d", last);

    /* Fill unexpected info */
    multi_recv_unexpected_info->actual_buf = buf;
    multi_recv_unexpected_info->actual_buf_size = (size_t) len;
    multi_recv_unexpected_info->source = (na_addr_t *) na_ofi_addr;
    multi_recv_unexpected_info->tag = (na_tag_t) (tag & NA_OFI_TAG_MASK);
    multi_recv_unexpected_info->last = last;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ofi_cq_process_recv_expected(const struct na_ofi_msg_info *msg_info,
    struct na_cb_info_recv_expected *recv_expected_info, void *buf, size_t len,
    uint64_t tag)
{
    na_return_t ret;

    /* Sanity checks */
    NA_CHECK_SUBSYS_ERROR(msg, msg_info->buf.ptr != buf, error, ret, NA_FAULT,
        "Invalid buffer access (Expected %p, got %p)", msg_info->buf.ptr, buf);
    NA_CHECK_SUBSYS_ERROR(msg, len > msg_info->buf_size, error, ret, NA_MSGSIZE,
        "Expected recv msg size too large for buffer (expected %zu, got %zu)",
        msg_info->buf_size, len);
    NA_CHECK_SUBSYS_ERROR(msg, msg_info->tag != tag, error, ret, NA_OVERFLOW,
        "Invalid tag value (expected %" PRIu64 ", got %" PRIu64 ")",
        msg_info->tag, tag);

    recv_expected_info->actual_buf_size = (size_t) len;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cq_process_retries(
    struct na_ofi_context *na_ofi_context, unsigned retry_period_ms)
{
    struct na_ofi_op_queue *op_queue = na_ofi_context->eq->retry_op_queue;
    struct na_ofi_op_id *na_ofi_op_id = NULL;
    na_return_t ret;

    do {
        bool canceled = false, skip_retry = false;
        na_cb_type_t cb_type;
        hg_time_t now;

        if (retry_period_ms > 0)
            hg_time_get_current_ms(&now);

        hg_thread_spin_lock(&op_queue->lock);
        na_ofi_op_id = TAILQ_FIRST(&op_queue->queue);
        if (!na_ofi_op_id) {
            hg_thread_spin_unlock(&op_queue->lock);
            /* Queue is empty */
            break;
        }

        /* Op in tail is always the most recent OP ID to be retried, if op in
         * head has already been retried less than the retry period, no need to
         * check the next ones. */
        if (retry_period_ms > 0) {
            hg_time_t retry_period_deadline = hg_time_add(
                na_ofi_op_id->retry_last, hg_time_from_ms(retry_period_ms));
            if (hg_time_less(retry_period_deadline, now))
                na_ofi_op_id->retry_last = now;
            else
                skip_retry = true;
        }

        /* Check if OP ID was canceled */
        if (hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_CANCELING) {
            hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_CANCELED);
            canceled = true;
        }

        if (!skip_retry || canceled) {
            /* Dequeue OP ID */
            TAILQ_REMOVE(&op_queue->queue, na_ofi_op_id, retry);
            hg_atomic_and32(&na_ofi_op_id->status, ~NA_OFI_OP_QUEUED);
        } else {
            hg_thread_spin_unlock(&op_queue->lock);
            /* Cannot retry yet */
            break;
        }

        hg_thread_spin_unlock(&op_queue->lock);

        if (canceled) {
            na_ofi_op_id->complete(na_ofi_op_id, true, NA_CANCELED);
            /* Try again */
            continue;
        }

        cb_type = na_ofi_op_id->type;
        NA_LOG_SUBSYS_DEBUG(op, "Attempting to retry operation %p (%s)",
            (void *) na_ofi_op_id, na_cb_type_to_string(cb_type));

        /* Retry operation */
        switch (na_ofi_op_id->fi_op_flags) {
            case FI_SEND:
                ret = na_ofi_op_id->retry_op.msg(na_ofi_context->fi_tx,
                    &na_ofi_op_id->info.msg, &na_ofi_op_id->fi_ctx);
                break;
            case FI_RECV:
                ret = na_ofi_op_id->retry_op.msg(na_ofi_context->fi_rx,
                    &na_ofi_op_id->info.msg, &na_ofi_op_id->fi_ctx);
                break;
            case FI_RMA:
                ret = na_ofi_op_id->retry_op.rma(na_ofi_context->fi_tx,
                    &na_ofi_op_id->info.rma, &na_ofi_op_id->fi_ctx);
                break;
            default:
                NA_GOTO_SUBSYS_ERROR(op, error, ret, NA_INVALID_ARG,
                    "Operation type %" PRIu64 " not supported",
                    na_ofi_op_id->fi_op_flags);
        }

        if (ret == NA_SUCCESS) {
            /* If the operation got canceled while we retried it, attempt to
             * cancel it */
            if (hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_CANCELING) {
                ret = na_ofi_op_cancel(na_ofi_op_id);
                NA_CHECK_SUBSYS_NA_ERROR(
                    op, error, ret, "Could not cancel operation");
            }
            continue;
        } else if (ret == NA_AGAIN) {
            /* Do not retry past deadline */
            hg_time_get_current_ms(&now);
            if (hg_time_less(na_ofi_op_id->retry_deadline, now)) {
                NA_LOG_SUBSYS_WARNING(op,
                    "Retry time elapsed, aborting operation %p (%s)",
                    (void *) na_ofi_op_id, na_cb_type_to_string(cb_type));
                hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_ERRORED);
                na_ofi_op_id->complete(na_ofi_op_id, true, NA_TIMEOUT);
                continue;
            }

            hg_thread_spin_lock(&op_queue->lock);
            /* Do not repush OP ID if it was canceled in the meantime */
            if (hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_CANCELING) {
                hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_CANCELED);
                canceled = true;
            } else {
                NA_LOG_SUBSYS_DEBUG(
                    op, "Re-pushing %p for retry", (void *) na_ofi_op_id);
                /* Re-push op ID to retry queue */
                TAILQ_INSERT_TAIL(&op_queue->queue, na_ofi_op_id, retry);
                hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_QUEUED);
            }
            hg_thread_spin_unlock(&op_queue->lock);

            if (canceled) {
                na_ofi_op_id->complete(na_ofi_op_id, true, NA_CANCELED);
                /* Try again */
                continue;
            } else
                /* Do not attempt to retry again and continue making progress,
                 * otherwise we could loop indefinitely */
                break;
        } else {
            NA_LOG_SUBSYS_ERROR(op, "retry operation of %p (%s) failed",
                (void *) na_ofi_op_id, na_cb_type_to_string(cb_type));

            /* Force internal completion in error mode */
            hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_ERRORED);
            na_ofi_op_id->complete(na_ofi_op_id, true, ret);
        }
    } while (1);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_op_retry(struct na_ofi_context *na_ofi_context, unsigned int timeout_ms,
    struct na_ofi_op_id *na_ofi_op_id)
{
    struct na_ofi_op_queue *retry_op_queue = na_ofi_context->eq->retry_op_queue;

    NA_LOG_SUBSYS_DEBUG(op, "Pushing %p for retry (%s)", (void *) na_ofi_op_id,
        na_cb_type_to_string(na_ofi_op_id->type));

    /* Set retry deadline */
    hg_time_get_current_ms(&na_ofi_op_id->retry_last);
    na_ofi_op_id->retry_deadline =
        hg_time_add(na_ofi_op_id->retry_last, hg_time_from_ms(timeout_ms));

    /* Push op ID to retry queue */
    hg_thread_spin_lock(&retry_op_queue->lock);
    TAILQ_INSERT_TAIL(&retry_op_queue->queue, na_ofi_op_id, retry);
    hg_atomic_set32(&na_ofi_op_id->status, NA_OFI_OP_QUEUED);
    hg_thread_spin_unlock(&retry_op_queue->lock);
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_op_retry_abort_addr(
    struct na_ofi_context *na_ofi_context, fi_addr_t fi_addr, na_return_t ret)
{
    struct na_ofi_op_queue *op_queue = na_ofi_context->eq->retry_op_queue;
    struct na_ofi_op_id *na_ofi_op_id;

    NA_LOG_SUBSYS_DEBUG(op,
        "Aborting all operations in retry queue to FI addr %" PRIu64, fi_addr);

    hg_thread_spin_lock(&op_queue->lock);
    TAILQ_FOREACH (na_ofi_op_id, &op_queue->queue, retry) {
        if (!na_ofi_op_id->addr || na_ofi_op_id->addr->fi_addr != fi_addr)
            continue;

        TAILQ_REMOVE(&op_queue->queue, na_ofi_op_id, retry);
        NA_LOG_SUBSYS_DEBUG(op,
            "Aborting operation ID %p (%s) in retry queue to FI addr %" PRIu64,
            (void *) na_ofi_op_id, na_cb_type_to_string(na_ofi_op_id->type),
            fi_addr);
        hg_atomic_and32(&na_ofi_op_id->status, ~NA_OFI_OP_QUEUED);
        hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_ERRORED);
        na_ofi_op_id->complete(na_ofi_op_id, true, ret);
    }
    hg_thread_spin_unlock(&op_queue->lock);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_op_complete_single(struct na_ofi_op_id *na_ofi_op_id,
    NA_UNUSED bool complete, na_return_t cb_ret)
{
    struct na_cb_completion_data *completion_data =
        na_ofi_op_id->completion_data;

    /* Mark op id as completed (independent of cb_ret) */
    hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_COMPLETED);

    /* Set callback ret */
    completion_data->callback_info.arg = na_ofi_op_id->arg;
    completion_data->callback_info.type = na_ofi_op_id->type;
    completion_data->callback_info.ret = cb_ret;
    completion_data->callback = na_ofi_op_id->callback;

    completion_data->plugin_callback_args = na_ofi_op_id;
    completion_data->plugin_callback = na_ofi_op_release_single;

    NA_LOG_SUBSYS_DEBUG(op, "Adding completion data to queue");

    /* Add OP to NA completion queue */
    na_cb_completion_add(na_ofi_op_id->context, completion_data);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_op_release_single(void *arg)
{
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) arg;

    NA_CHECK_SUBSYS_WARNING(op,
        na_ofi_op_id &&
            (!(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED)),
        "Releasing resources from an uncompleted operation");

    if (na_ofi_op_id->addr) {
        na_ofi_addr_ref_decr(na_ofi_op_id->addr);
        na_ofi_op_id->addr = NULL;
    }
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_op_complete_multi(
    struct na_ofi_op_id *na_ofi_op_id, bool complete, na_return_t cb_ret)
{
    struct na_cb_completion_data *completion_data =
        na_ofi_op_id->completion_data;

    na_ofi_op_id->completion_data_storage.multi.completion_count++;

    if (complete) {
        struct na_ofi_op_queue *multi_op_queue =
            &NA_OFI_CONTEXT(na_ofi_op_id->context)->multi_op_queue;

        /* Mark op id as completed (independent of cb_ret) */
        hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_COMPLETED);
        NA_LOG_SUBSYS_DEBUG(op, "Completed %" PRIu32 " events for same buffer",
            na_ofi_op_id->completion_data_storage.multi.completion_count);

        hg_thread_spin_lock(&multi_op_queue->lock);
        TAILQ_REMOVE(&multi_op_queue->queue, na_ofi_op_id, multi);
        hg_atomic_decr32(
            &NA_OFI_CONTEXT(na_ofi_op_id->context)->multi_op_count);
        hg_thread_spin_unlock(&multi_op_queue->lock);
    }

    /* Set callback ret */
    completion_data->callback_info.arg = na_ofi_op_id->arg;
    completion_data->callback_info.type = na_ofi_op_id->type;
    completion_data->callback_info.ret = cb_ret;
    completion_data->callback = na_ofi_op_id->callback;

    completion_data->plugin_callback_args = na_ofi_op_id;
    completion_data->plugin_callback = na_ofi_op_release_multi;

    /* In the case of multi-event, set next completion data */
    na_ofi_op_id->completion_data = na_ofi_completion_multi_push(
        &na_ofi_op_id->completion_data_storage.multi);
    NA_CHECK_SUBSYS_ERROR_NORET(
        op, na_ofi_op_id->completion_data == NULL, error, "Queue is full");

    NA_LOG_SUBSYS_DEBUG(op, "Adding completion data to queue");
    /* Add OP to NA completion queue */
    na_cb_completion_add(na_ofi_op_id->context, completion_data);

error:
    return;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_op_release_multi(void *arg)
{
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) arg;

    na_ofi_completion_multi_pop(&na_ofi_op_id->completion_data_storage.multi);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_op_cancel(struct na_ofi_op_id *na_ofi_op_id)
{
    struct fid_ep *fi_ep = NULL;
    ssize_t rc;
    na_return_t ret;

    /* Let only one thread call fi_cancel() */
    if (hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_CANCELED) &
        NA_OFI_OP_CANCELED)
        return NA_SUCCESS;

    switch (na_ofi_op_id->type) {
        case NA_CB_RECV_UNEXPECTED:
        case NA_CB_MULTI_RECV_UNEXPECTED:
        case NA_CB_RECV_EXPECTED:
            fi_ep = NA_OFI_CONTEXT(na_ofi_op_id->context)->fi_rx;
            break;
        case NA_CB_SEND_UNEXPECTED:
        case NA_CB_SEND_EXPECTED:
        case NA_CB_PUT:
        case NA_CB_GET:
            fi_ep = NA_OFI_CONTEXT(na_ofi_op_id->context)->fi_tx;
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(op, error, ret, NA_INVALID_ARG,
                "Operation type %d not supported", na_ofi_op_id->type);
            break;
    }

    /* fi_cancel() is an asynchronous operation, either the operation
     * will be canceled and an FI_ECANCELED event will be generated
     * or it will show up in the regular completion queue.
     */
    rc = fi_cancel(&fi_ep->fid, &na_ofi_op_id->fi_ctx);
    NA_LOG_SUBSYS_DEBUG(
        op, "fi_cancel() rc: %d (%s)", (int) rc, fi_strerror((int) -rc));
    (void) rc;

    /* Work around segfault on fi_cq_signal() in some providers */
    if (na_ofi_prov_flags[na_ofi_op_id->na_ofi_class->fabric->prov_type] &
        NA_OFI_SIGNAL) {
        /* Signal CQ to wake up and no longer wait on FD */
        int rc_signal =
            fi_cq_signal(NA_OFI_CONTEXT(na_ofi_op_id->context)->eq->fi_cq);
        NA_CHECK_SUBSYS_ERROR(op, rc_signal != 0 && rc_signal != -ENOSYS, error,
            ret, na_ofi_errno_to_na(-rc_signal),
            "fi_cq_signal (op type %d) failed, rc: %d (%s)", na_ofi_op_id->type,
            rc_signal, fi_strerror(-rc_signal));
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_completion_multi_init(
    struct na_ofi_completion_multi *completion_multi, unsigned int count)
{
    na_return_t ret;

    completion_multi->data = (struct na_cb_completion_data *) calloc(
        count, sizeof(struct na_cb_completion_data));
    NA_CHECK_SUBSYS_ERROR(op, completion_multi->data == NULL, error, ret,
        NA_NOMEM, "Could not allocate %u completion data entries", count);
    completion_multi->size = count;
    completion_multi->mask = (int32_t) (count - 1);
    hg_atomic_init32(&completion_multi->head, 0);
    hg_atomic_init32(&completion_multi->tail, 0);
    completion_multi->completion_count = 0;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_completion_multi_destroy(
    struct na_ofi_completion_multi *completion_multi)
{
    free(completion_multi->data);
    completion_multi->data = NULL;
}

/*---------------------------------------------------------------------------*/
static struct na_cb_completion_data *
na_ofi_completion_multi_push(struct na_ofi_completion_multi *completion_multi)
{
    struct na_cb_completion_data *completion_data;
    int32_t head, next, tail;

    head = hg_atomic_get32(&completion_multi->head);
    next = (head + 1) & completion_multi->mask;
    tail = hg_atomic_get32(&completion_multi->tail);

    if (next == tail)
        /* Full */
        return NULL;

    completion_data = &completion_multi->data[head];

    hg_atomic_set32(&completion_multi->head, next);

    return completion_data;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_completion_multi_pop(struct na_ofi_completion_multi *completion_multi)
{
    int32_t head, next, tail;

    head = hg_atomic_get32(&completion_multi->head);
    tail = hg_atomic_get32(&completion_multi->tail);

    if (head == tail)
        /* Empty */
        return;

    next = (tail + 1) & completion_multi->mask;
    hg_atomic_set32(&completion_multi->tail, next);
}

/*---------------------------------------------------------------------------*/
/*
static NA_INLINE bool
na_ofi_completion_multi_empty(struct na_ofi_completion_multi *completion_multi)
{
    return hg_atomic_get32(&completion_multi->head) ==
           hg_atomic_get32(&completion_multi->tail);
}
*/

/*---------------------------------------------------------------------------*/
static NA_INLINE unsigned int
na_ofi_completion_multi_count(struct na_ofi_completion_multi *completion_multi)
{
    return (unsigned int) (((int32_t) completion_multi->size +
                               hg_atomic_get32(&completion_multi->head) -
                               hg_atomic_get32(&completion_multi->tail)) &
                           completion_multi->mask);
}

/********************/
/* Plugin callbacks */
/********************/

static na_return_t
na_ofi_get_protocol_info(
    const struct na_info *na_info, struct na_protocol_info **na_protocol_info_p)
{
    struct fi_info *prov, *providers = NULL;
    struct na_ofi_info info = NA_OFI_INFO_INITIALIZER;
    struct na_protocol_info *head = NULL, *prev = NULL;
    unsigned NA_DEBUG_LOG_USED count = 0;
    enum na_ofi_prov_type prov_type = NA_OFI_PROV_NULL;
    na_return_t ret;

    if (na_info != NULL) {
        const struct na_init_info *na_init_info = &na_info->na_init_info;

        if (na_info->protocol_name != NULL) {
            prov_type = na_ofi_prov_name_to_type(na_info->protocol_name);
            NA_CHECK_SUBSYS_ERROR(cls, prov_type == NA_OFI_PROV_NULL, error,
                ret, NA_PROTONOSUPPORT, "Protocol \"%s\" not supported",
                na_info->protocol_name);

            info.addr_format =
                na_ofi_prov_addr_format(prov_type, na_init_info->addr_format);
            NA_CHECK_SUBSYS_ERROR(cls, info.addr_format <= FI_FORMAT_UNSPEC,
                error, ret, NA_PROTONOSUPPORT, "Unsupported address format");
        }
    }

    ret = na_ofi_getinfo(prov_type, &info, &providers);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not get provider info");

    for (prov = providers; prov != NULL; prov = prov->next) {
        struct na_ofi_verify_info verify_info = {
            .loc_info = NULL,
            .domain_name = NULL,
            .addr_format = FI_FORMAT_UNSPEC,
            .prov_type =
                (prov_type != NA_OFI_PROV_NULL)
                    ? prov_type
                    : na_ofi_prov_name_to_type(prov->fabric_attr->prov_name),
        };

        if (verify_info.prov_type == NA_OFI_PROV_NULL)
            continue; /* Unsupported provider */

        verify_info.addr_format =
            (info.addr_format != FI_FORMAT_UNSPEC)
                ? info.addr_format
                : na_ofi_prov_addr_format(
                      verify_info.prov_type, NA_ADDR_UNSPEC);

        if (na_ofi_match_provider(&verify_info, prov)) {
            struct na_protocol_info *entry;

            /* Do not keep duplicates generated by OFI */
            for (entry = head; entry != NULL; entry = entry->next)
                if (!strcmp(entry->device_name, prov->domain_attr->name) &&
                    !strcmp(entry->protocol_name, prov->fabric_attr->prov_name))
                    break;

            if (entry != NULL)
                continue; /* duplicate found */

            entry = na_protocol_info_alloc(NA_OFI_CLASS_NAME,
                prov->fabric_attr->prov_name, prov->domain_attr->name);
            NA_CHECK_SUBSYS_ERROR(cls, entry == NULL, error, ret, NA_NOMEM,
                "Could not allocate protocol info entry");

            entry->next = head;
            head = entry;

            NA_LOG_SUBSYS_DEBUG(cls, "(#%u) Prov is %s, %s", count++,
                head->protocol_name, head->device_name);
        }
    }

    *na_protocol_info_p = head;

    fi_freeinfo(providers);

    return NA_SUCCESS;

error:
    if (providers != NULL)
        fi_freeinfo(providers);

    while (head != NULL) {
        prev = head;
        head = head->next;
        na_protocol_info_free(prev);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static bool
na_ofi_check_protocol(const char *protocol_name)
{
    struct fi_info *prov, *providers = NULL;
    enum na_ofi_prov_type type;
    bool accept;
    uint32_t runtime_version = fi_version();
    na_return_t na_ret;

    NA_LOG_SUBSYS_DEBUG(cls, "Querying info on libfabric v%d.%d",
        FI_MAJOR(runtime_version), FI_MINOR(runtime_version));
    NA_CHECK_SUBSYS_ERROR(cls, FI_VERSION_LT(runtime_version, NA_OFI_VERSION),
        out, accept, false,
        "runtime libfabric version (v%d.%d) is lower than required version "
        "(v%d.%d)",
        FI_MAJOR(runtime_version), FI_MINOR(runtime_version),
        FI_MAJOR(NA_OFI_VERSION), FI_MINOR(NA_OFI_VERSION));

    type = na_ofi_prov_name_to_type(protocol_name);
    NA_CHECK_SUBSYS_ERROR(cls, type == NA_OFI_PROV_NULL, out, accept, false,
        "Protocol %s not supported", protocol_name);

    /* Prevent < 1.20 builds to run with >= 1.20 runtimes */
    NA_CHECK_SUBSYS_ERROR(fatal,
        (type == NA_OFI_PROV_CXI) &&
            FI_VERSION_GE(runtime_version, FI_VERSION(1, 20)) &&
            FI_VERSION_LT(FI_COMPILE_VERSION, FI_VERSION(1, 20)),
        out, accept, false,
        "runtime libfabric version (v%d.%d) is not compatible with compiled "
        "version (v%d.%d) to use \"cxi\" provider",
        FI_MAJOR(runtime_version), FI_MINOR(runtime_version),
        FI_MAJOR(FI_COMPILE_VERSION), FI_MINOR(FI_COMPILE_VERSION));

/* Only the sockets provider is currently supported on macOS */
#ifdef __APPLE__
    NA_CHECK_SUBSYS_ERROR(fatal,
        FI_VERSION_LT(runtime_version, FI_VERSION(1, 18)) &&
            (type != NA_OFI_PROV_SOCKETS),
        out, accept, false,
        "\"sockets\" is the only supported provider on macOS");
#endif

    /* Get info from provider (no node info) */
    na_ret = na_ofi_getinfo(type, NULL, &providers);
    if (na_ret != NA_SUCCESS) {
        /* getinfo failed.  This could be because Mercury was
         * linked against a libfabric library that was not compiled with
         * support for the desired provider.  Attempt to detect this case
         * and display a user-friendly error message.
         */
        na_ofi_provider_check(type, protocol_name);
        NA_GOTO_SUBSYS_ERROR(
            cls, out, accept, false, "na_ofi_getinfo() failed");
    }

    for (prov = providers; prov != NULL; prov = prov->next) {
        if (strcmp(na_ofi_prov_name[type], prov->fabric_attr->prov_name) == 0) {
            NA_LOG_SUBSYS_DEBUG(
                cls, "Matched provider: %s", prov->fabric_attr->prov_name);
            break;
        }
    }
    accept = (bool) (prov != NULL);

    fi_freeinfo(providers);

out:
    return accept;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_initialize(
    na_class_t *na_class, const struct na_info *na_info, bool NA_UNUSED listen)
{
    const struct na_init_info *na_init_info = &na_info->na_init_info;
    struct na_ofi_class *na_ofi_class = NULL;
    enum na_ofi_prov_type prov_type;
    bool no_wait;
    char *domain_name = NULL;
    struct na_ofi_info info = NA_OFI_INFO_INITIALIZER;
    union na_ofi_auth_key base_auth_key;
    struct na_loc_info *loc_info = NULL;
    na_return_t ret;
#ifdef NA_OFI_HAS_MEM_POOL
    size_t pool_chunk_size;
#endif
#ifdef NA_OFI_HAS_ADDR_POOL
    unsigned int i;
#endif

    NA_LOG_SUBSYS_DEBUG(cls,
        "Entering na_ofi_initialize() protocol_name \"%s\", host_name \"%s\"",
        na_info->protocol_name, na_info->host_name);

    /* Get provider type */
    prov_type = na_ofi_prov_name_to_type(na_info->protocol_name);
    NA_CHECK_SUBSYS_ERROR(fatal, prov_type == NA_OFI_PROV_NULL, error, ret,
        NA_INVALID_ARG, "Protocol %s not supported", na_info->protocol_name);

#if defined(NA_OFI_HAS_EXT_GNI_H) && defined(NA_OFI_GNI_HAS_UDREG)
    /* In case of GNI using udreg, we check to see whether
     * MPICH_GNI_NDREG_ENTRIES environment variable is set or not.  If not, this
     * code is not likely to work if Cray MPI is also used. Print error msg
     * suggesting workaround.
     */
    NA_CHECK_SUBSYS_ERROR(fatal,
        prov_type == NA_OFI_PROV_GNI && !getenv("MPICH_GNI_NDREG_ENTRIES"),
        error, ret, NA_INVALID_ARG,
        "ofi+gni provider requested, but the MPICH_GNI_NDREG_ENTRIES "
        "environment variable is not set.\n"
        "Please run this executable with "
        "\"export MPICH_GNI_NDREG_ENTRIES=1024\" to ensure compatibility.");
#endif

    /* Get addr format */
    info.addr_format =
        na_ofi_prov_addr_format(prov_type, na_init_info->addr_format);
    NA_CHECK_SUBSYS_ERROR(cls, info.addr_format <= FI_FORMAT_UNSPEC, error, ret,
        NA_PROTONOSUPPORT, "Unsupported address format");

    /* Use HMEM */
    if (na_init_info->request_mem_device) {
        NA_LOG_SUBSYS_DEBUG(cls, "Requesting use of memory devices");
        info.use_hmem = na_init_info->request_mem_device;
    }

    /* Thread mode */
    info.thread_mode = (na_init_info->thread_mode & NA_THREAD_MODE_SINGLE)
                           ? FI_THREAD_DOMAIN
                           : FI_THREAD_SAFE;

    /* Parse hostname info and get domain name etc */
    if (na_info->host_name != NULL) {
        ret = na_ofi_parse_hostname_info(prov_type, na_info->host_name,
            info.addr_format, &domain_name, &info.node, &info.service,
            &info.src_addr, &info.src_addrlen);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "na_ofi_parse_hostname_info() failed");
    }

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    /* Parse auth key range info */
    if ((na_ofi_prov_flags[prov_type] & NA_OFI_AV_AUTH_KEY) &&
        na_init_info->auth_key && na_init_info->auth_key[0] != '\0') {
        ret = na_ofi_parse_auth_key_range(na_init_info->auth_key, prov_type,
            &base_auth_key, &info.num_auth_keys);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret,
            "Could not parse auth key range (%s)", na_init_info->auth_key);

        NA_LOG_SUBSYS_DEBUG(
            cls, "Configuring with %zu auth key(s)", info.num_auth_keys);
    }
#endif

    /* Create new OFI class */
    na_ofi_class = na_ofi_class_alloc();
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_class == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA OFI class");

    /* Check env config */
    ret = na_ofi_class_env_config(na_ofi_class);
    NA_CHECK_SUBSYS_NA_ERROR(
        cls, error, ret, "na_ofi_class_env_config() failed");

#ifdef NA_HAS_HWLOC
    /* Use autodetect if we can't guess which domain to use */
    if ((na_ofi_prov_flags[prov_type] & NA_OFI_LOC_INFO) && !domain_name &&
        !info.src_addr && !info.node) {
        ret = na_loc_info_init(&loc_info);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could init loc info");
    }
#endif

    /* Verify info */
    ret = na_ofi_verify_info(
        prov_type, &info, domain_name, loc_info, &na_ofi_class->fi_info);
#ifdef NA_HAS_HWLOC
    if (loc_info)
        na_loc_info_destroy(loc_info);
#endif
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not verify info for %s",
        na_ofi_prov_name[prov_type]);

    /* Set/check optional features */
    if ((na_ofi_prov_extra_caps[prov_type] & FI_MULTI_RECV) &&
        (na_ofi_class->msg_recv_unexpected == na_ofi_msg_recv)) {
        NA_CHECK_SUBSYS_ERROR(cls,
            !(na_ofi_class->fi_info->caps & FI_MULTI_RECV), error, ret,
            NA_PROTONOSUPPORT, "FI_MULTI_RECV is not supported by provider");
        na_ofi_class->opt_features |= NA_OPT_MULTI_RECV;
    }
    if (na_ofi_class->fi_info->caps & FI_SOURCE_ERR) {
        na_ofi_class->cq_poll = na_ofi_cq_poll_fi_source;
    } else
        na_ofi_class->cq_poll = na_ofi_cq_poll_no_source;

    /* Open fabric */
    ret = na_ofi_fabric_open(
        prov_type, na_ofi_class->fi_info->fabric_attr, &na_ofi_class->fabric);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not open fabric for %s",
        na_ofi_prov_name[prov_type]);

    /* Open domain */
    no_wait = na_init_info->progress_mode & NA_NO_BLOCK;
    ret = na_ofi_domain_open(na_ofi_class->fabric,
        (info.num_auth_keys > 1) ? (const void *) &base_auth_key
                                 : (const void *) na_init_info->auth_key,
        info.num_auth_keys, na_init_info->traffic_class, no_wait,
        na_ofi_class->fi_info, &na_ofi_class->domain);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret,
        "Could not open domain for %s, %s", na_ofi_prov_name[prov_type],
        na_ofi_class->fi_info->domain_attr->name);

    /* Make sure that domain is configured as no_wait */
    NA_CHECK_SUBSYS_WARNING(cls, no_wait != na_ofi_class->domain->no_wait,
        "Requested no_wait=%d, domain no_wait=%d", no_wait,
        na_ofi_class->domain->no_wait);
    na_ofi_class->no_wait = na_ofi_class->domain->no_wait || no_wait;

    /* Set context limits */
    NA_CHECK_SUBSYS_ERROR(fatal,
        na_init_info->max_contexts > na_ofi_class->domain->context_max, error,
        ret, NA_INVALID_ARG,
        "Maximum number of requested contexts (%" PRIu8 ") exceeds provider "
        "limitation(%zu)",
        na_init_info->max_contexts, na_ofi_class->domain->context_max);
    na_ofi_class->context_max = na_init_info->max_contexts;

    /* Use SEP */
    na_ofi_class->use_sep = (na_ofi_prov_flags[prov_type] & NA_OFI_SEP) &&
                            (na_ofi_class->context_max > 1);

    /* Create endpoint */
    ret = na_ofi_endpoint_open(na_ofi_class->fabric, na_ofi_class->domain,
        na_ofi_class->no_wait, na_ofi_class->use_sep, na_ofi_class->context_max,
        na_init_info->max_unexpected_size, na_init_info->max_expected_size,
        na_ofi_class->fi_info, &na_ofi_class->endpoint);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not create endpoint");

#ifdef NA_OFI_HAS_MEM_POOL
    pool_chunk_size = MAX(na_ofi_class->endpoint->unexpected_msg_size_max,
        na_ofi_class->endpoint->expected_msg_size_max);

    /* Register initial mempool */
    na_ofi_class->send_pool = hg_mem_pool_create(pool_chunk_size,
        NA_OFI_MEM_CHUNK_COUNT, NA_OFI_MEM_BLOCK_COUNT, na_ofi_mem_buf_register,
        NA_SEND, na_ofi_mem_buf_deregister, (void *) na_ofi_class);
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_class->send_pool == NULL, error, ret,
        NA_NOMEM,
        "Could not create send pool with %d blocks of size %d x %zu bytes",
        NA_OFI_MEM_BLOCK_COUNT, NA_OFI_MEM_CHUNK_COUNT, pool_chunk_size);

    /* Register initial mempool */
    na_ofi_class->recv_pool = hg_mem_pool_create(pool_chunk_size,
        NA_OFI_MEM_CHUNK_COUNT, NA_OFI_MEM_BLOCK_COUNT, na_ofi_mem_buf_register,
        NA_RECV, na_ofi_mem_buf_deregister, (void *) na_ofi_class);
    NA_CHECK_SUBSYS_ERROR(cls, na_ofi_class->recv_pool == NULL, error, ret,
        NA_NOMEM,
        "Could not create memory pool with %d blocks of size %d x %zu bytes",
        NA_OFI_MEM_BLOCK_COUNT, NA_OFI_MEM_CHUNK_COUNT, pool_chunk_size);
#endif

#ifdef NA_OFI_HAS_ADDR_POOL
    /* Create pool of addresses */
    for (i = 0; i < NA_OFI_ADDR_POOL_COUNT; i++) {
        struct na_ofi_addr *na_ofi_addr = na_ofi_addr_alloc(na_ofi_class);
        NA_CHECK_SUBSYS_ERROR(cls, na_ofi_addr == NULL, error, ret, NA_NOMEM,
            "Could not create address");
        STAILQ_INSERT_TAIL(&na_ofi_class->addr_pool.queue, na_ofi_addr, entry);
    }
#endif

    /* Get address from endpoint */
    ret = na_ofi_endpoint_get_src_addr(na_ofi_class);
    NA_CHECK_SUBSYS_NA_ERROR(
        cls, error, ret, "Could not get endpoint src address");

    na_class->plugin_class = (void *) na_ofi_class;

    na_ofi_free_hostname_info(
        domain_name, info.node, info.service, info.src_addr);

    return NA_SUCCESS;

error:
    na_ofi_free_hostname_info(
        domain_name, info.node, info.service, info.src_addr);

    if (na_ofi_class)
        na_ofi_class_free(na_ofi_class);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_finalize(na_class_t *na_class)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    hg_hash_table_iter_t addr_table_iter;
    na_return_t ret = NA_SUCCESS;

    if (na_ofi_class == NULL)
        return ret;

    /* Class is now finalizing */
    na_ofi_class->finalizing = true;

    /* Iterate over remaining addresses and free them */
    hg_hash_table_iterate(
        na_ofi_class->domain->addr_map.key_map, &addr_table_iter);
    while (hg_hash_table_iter_has_more(&addr_table_iter)) {
        struct na_ofi_addr *na_ofi_addr =
            (struct na_ofi_addr *) hg_hash_table_iter_next(&addr_table_iter);
        if (na_ofi_addr->class == na_ofi_class)
            na_ofi_addr_ref_decr(na_ofi_addr);
    }

    /* Free class */
    ret = na_ofi_class_free(na_ofi_class);
    NA_CHECK_SUBSYS_NA_ERROR(cls, out, ret, "Coult not free NA OFI class");

    na_class->plugin_class = NULL;

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static bool
na_ofi_has_opt_feature(na_class_t *na_class, unsigned long flags)
{
    return flags & NA_OFI_CLASS(na_class)->opt_features;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_context_create(na_class_t *na_class, void **context_p, uint8_t id)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NULL;
    na_return_t ret;
    int rc;

    na_ofi_context =
        (struct na_ofi_context *) calloc(1, sizeof(*na_ofi_context));
    NA_CHECK_SUBSYS_ERROR(ctx, na_ofi_context == NULL, error, ret, NA_NOMEM,
        "Could not allocate na_ofi_context");
    na_ofi_context->idx = id;

    /* If not using SEP, just point to class' endpoint */
    if (!na_ofi_class->use_sep) {
        na_ofi_context->fi_tx = na_ofi_class->endpoint->fi_ep;
        na_ofi_context->fi_rx = na_ofi_class->endpoint->fi_ep;
        na_ofi_context->eq = na_ofi_class->endpoint->eq;
    } else {
        int32_t n_contexts = hg_atomic_get32(&na_ofi_class->n_contexts);
        NA_CHECK_SUBSYS_ERROR(fatal,
            n_contexts >= (int32_t) na_ofi_class->context_max ||
                id >= na_ofi_class->context_max,
            error, ret, NA_OPNOTSUPPORTED,
            "n_contexts %" PRId32 ", context id %" PRIu8
            ", max_contexts %" PRIu8,
            n_contexts, id, na_ofi_class->context_max);

        /* Create Tx / Rx contexts */
        rc = fi_tx_context(na_ofi_class->endpoint->fi_ep, id, NULL,
            &na_ofi_context->fi_tx, NULL);
        NA_CHECK_SUBSYS_ERROR(ctx, rc < 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_tx_context() failed, rc: %d (%s)", rc, fi_strerror(-rc));

        rc = fi_rx_context(na_ofi_class->endpoint->fi_ep, id, NULL,
            &na_ofi_context->fi_rx, NULL);
        NA_CHECK_SUBSYS_ERROR(ctx, rc < 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_rx_context() failed, rc: %d (%s)", rc, fi_strerror(-rc));

        /* Create event queues (CQ, wait sets) */
        ret = na_ofi_eq_open(na_ofi_class->fabric, na_ofi_class->domain,
            na_ofi_class->no_wait, &na_ofi_context->eq);
        NA_CHECK_SUBSYS_NA_ERROR(
            ctx, error, ret, "Could not open event queues");

        rc = fi_ep_bind(na_ofi_context->fi_tx, &na_ofi_context->eq->fi_cq->fid,
            FI_TRANSMIT);
        NA_CHECK_SUBSYS_ERROR(ctx, rc < 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_ep_bind() noc_tx failed, rc: %d (%s)", rc, fi_strerror(-rc));

        rc = fi_ep_bind(
            na_ofi_context->fi_rx, &na_ofi_context->eq->fi_cq->fid, FI_RECV);
        NA_CHECK_SUBSYS_ERROR(ctx, rc < 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_ep_bind() noc_rx failed, rc: %d (%s)", rc, fi_strerror(-rc));

        rc = fi_enable(na_ofi_context->fi_tx);
        NA_CHECK_SUBSYS_ERROR(ctx, rc < 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_enable() noc_tx failed, rc: %d (%s)", rc, fi_strerror(-rc));

        rc = fi_enable(na_ofi_context->fi_rx);
        NA_CHECK_SUBSYS_ERROR(ctx, rc < 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_enable() noc_rx failed, rc: %d (%s)", rc, fi_strerror(-rc));
    }

    /* Create queue for tracking multi-event ops */
    rc = hg_thread_spin_init(&na_ofi_context->multi_op_queue.lock);
    NA_CHECK_SUBSYS_ERROR_NORET(
        ctx, rc != HG_UTIL_SUCCESS, error, "hg_thread_spin_init() failed");
    TAILQ_INIT(&na_ofi_context->multi_op_queue.queue);

    hg_atomic_incr32(&na_ofi_class->n_contexts);

    *context_p = (void *) na_ofi_context;

    return NA_SUCCESS;

error:
    if (na_ofi_context) {
        if (na_ofi_class->use_sep) {
            if (na_ofi_context->fi_tx)
                (void) fi_close(&na_ofi_context->fi_tx->fid);
            if (na_ofi_context->fi_rx)
                (void) fi_close(&na_ofi_context->fi_rx->fid);
            if (na_ofi_context->eq)
                (void) na_ofi_eq_close(na_ofi_context->eq);
        }
        free(na_ofi_context);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_context_destroy(na_class_t *na_class, void *context)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = (struct na_ofi_context *) context;
    na_return_t ret = NA_SUCCESS;
    int rc;

    if (na_ofi_class->use_sep) {
        bool empty;

        /* Check that retry op queue is empty */
        empty = TAILQ_EMPTY(&na_ofi_context->eq->retry_op_queue->queue);
        NA_CHECK_SUBSYS_ERROR(ctx, empty == false, out, ret, NA_BUSY,
            "Retry op queue should be empty");

        if (na_ofi_context->fi_tx) {
            rc = fi_close(&na_ofi_context->fi_tx->fid);
            NA_CHECK_SUBSYS_ERROR(ctx, rc != 0, out, ret,
                na_ofi_errno_to_na(-rc),
                "fi_close() noc_tx failed, rc: %d (%s)", rc, fi_strerror(-rc));
            na_ofi_context->fi_tx = NULL;
        }

        if (na_ofi_context->fi_rx) {
            rc = fi_close(&na_ofi_context->fi_rx->fid);
            NA_CHECK_SUBSYS_ERROR(ctx, rc != 0, out, ret,
                na_ofi_errno_to_na(-rc),
                "fi_close() noc_rx failed, rc: %d (%s)", rc, fi_strerror(-rc));
            na_ofi_context->fi_rx = NULL;
        }

        /* Close wait set */
        if (na_ofi_context->eq) {
            ret = na_ofi_eq_close(na_ofi_context->eq);
            NA_CHECK_SUBSYS_NA_ERROR(
                ctx, out, ret, "Could not close event queues");
            na_ofi_context->eq = NULL;
        }
    }

    (void) hg_thread_spin_destroy(&na_ofi_context->multi_op_queue.lock);
    free(na_ofi_context);
    hg_atomic_decr32(&na_ofi_class->n_contexts);

out:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_op_id_t *
na_ofi_op_create(na_class_t *na_class, unsigned long flags)
{
    struct na_ofi_op_id *na_ofi_op_id = NULL;

    na_ofi_op_id = (struct na_ofi_op_id *) calloc(1, sizeof(*na_ofi_op_id));
    NA_CHECK_SUBSYS_ERROR_NORET(op, na_ofi_op_id == NULL, error,
        "Could not allocate NA OFI operation ID");
    na_ofi_op_id->na_ofi_class = NA_OFI_CLASS(na_class);

    if (flags & NA_OP_MULTI) {
        na_return_t ret;

        ret = na_ofi_completion_multi_init(
            &na_ofi_op_id->completion_data_storage.multi,
            NA_OFI_OP_MULTI_CQ_SIZE);
        NA_CHECK_SUBSYS_NA_ERROR(
            op, error, ret, "Could not allocate multi-operation queue");
        na_ofi_op_id->multi_event = true;

        na_ofi_op_id->complete = na_ofi_op_complete_multi;
        na_ofi_op_id->completion_data = na_ofi_completion_multi_push(
            &na_ofi_op_id->completion_data_storage.multi);
        NA_CHECK_SUBSYS_ERROR_NORET(op, na_ofi_op_id->completion_data == NULL,
            error, "Could not reserve completion data");
    } else {
        na_ofi_op_id->complete = na_ofi_op_complete_single;
        na_ofi_op_id->completion_data =
            &na_ofi_op_id->completion_data_storage.single;
    }

    /* Completed by default */
    hg_atomic_init32(&na_ofi_op_id->status, NA_OFI_OP_COMPLETED);

    return (na_op_id_t *) na_ofi_op_id;

error:
    if (na_ofi_op_id) {
        if (na_ofi_op_id->multi_event)
            na_ofi_completion_multi_destroy(
                &na_ofi_op_id->completion_data_storage.multi);
        free(na_ofi_op_id);
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_op_destroy(na_class_t NA_UNUSED *na_class, na_op_id_t *op_id)
{
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) op_id;

    if (na_ofi_op_id->multi_event) {
        if (!(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED)) {
            struct na_ofi_op_queue *multi_op_queue =
                &NA_OFI_CONTEXT(na_ofi_op_id->context)->multi_op_queue;

            hg_thread_spin_lock(&multi_op_queue->lock);
            TAILQ_REMOVE(&multi_op_queue->queue, na_ofi_op_id, multi);
            hg_atomic_decr32(
                &NA_OFI_CONTEXT(na_ofi_op_id->context)->multi_op_count);
            hg_thread_spin_unlock(&multi_op_queue->lock);
        }
        na_ofi_completion_multi_destroy(
            &na_ofi_op_id->completion_data_storage.multi);
    } else {
        /* Multi-events may not be fully completed when they are destroyed */
        NA_CHECK_SUBSYS_WARNING(op,
            !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED),
            "Attempting to free OP ID that was not completed");
    }

    free(na_ofi_op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr_p)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_addr_key addr_key;
    int addr_format = (int) na_ofi_class->fi_info->addr_format;
    struct na_ofi_addr *na_ofi_addr = NULL;
    na_return_t ret;

    /* Check provider from name */
    NA_CHECK_SUBSYS_ERROR(fatal,
        NA_OFI_CLASS(na_class)->fabric->prov_type != NA_OFI_PROV_TCP &&
            na_ofi_addr_prov(name) != NA_OFI_CLASS(na_class)->fabric->prov_type,
        error, ret, NA_INVALID_ARG, "Unrecognized provider type found from: %s",
        name);

    /* Convert name to raw address */
    ret = na_ofi_str_to_raw_addr(name, addr_format, &addr_key.addr);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not convert string to address");

    /* Create key from addr for faster lookups */
    addr_key.val = na_ofi_raw_addr_to_key(addr_format, &addr_key.addr);
    NA_CHECK_SUBSYS_ERROR(addr, addr_key.val == 0, error, ret,
        NA_PROTONOSUPPORT, "Could not generate key from addr");

    /* Lookup key and create new addr if it does not exist.
     * When using auth keys, peers must either share the same global key or use
     * the same base key when using FI_AV_AUTH_KEY to be able to communicate. */
    ret = na_ofi_addr_key_lookup(
        na_ofi_class, &addr_key, FI_ADDR_NOTAVAIL, &na_ofi_addr);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not lookup address key for %s", name);

    *addr_p = (na_addr_t *) na_ofi_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE void
na_ofi_addr_free(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    na_ofi_addr_ref_decr((struct na_ofi_addr *) addr);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ofi_addr_set_remove(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{
    na_ofi_addr_ref_decr((struct na_ofi_addr *) addr);

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ofi_addr_self(na_class_t *na_class, na_addr_t **addr_p)
{
    struct na_ofi_endpoint *ep = NA_OFI_CLASS(na_class)->endpoint;

    na_ofi_addr_ref_incr(ep->src_addr); /* decref in na_ofi_addr_free() */
    *addr_p = (na_addr_t *) ep->src_addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_return_t
na_ofi_addr_dup(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr, na_addr_t **new_addr_p)
{
    struct na_ofi_addr *na_ofi_addr = (struct na_ofi_addr *) addr;

    na_ofi_addr_ref_incr(na_ofi_addr); /* decref in na_ofi_addr_free() */
    *new_addr_p = addr;

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static bool
na_ofi_addr_cmp(
    na_class_t NA_UNUSED *na_class, na_addr_t *addr1, na_addr_t *addr2)
{
    return addr1 == addr2;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_ofi_addr_is_self(na_class_t *na_class, na_addr_t *addr)
{
    return NA_OFI_CLASS(na_class)->endpoint->src_addr ==
           (struct na_ofi_addr *) addr;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size_p, na_addr_t *addr)
{
    return na_ofi_get_uri(NA_OFI_CLASS(na_class)->fabric,
        NA_OFI_CLASS(na_class)->domain, buf, buf_size_p,
        &((struct na_ofi_addr *) addr)->addr_key);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_addr_get_serialize_size(na_class_t *na_class, na_addr_t NA_UNUSED *addr)
{
#ifdef NA_OFI_ADDR_OPT
    return na_ofi_raw_addr_serialize_size(
        (int) NA_OFI_CLASS(na_class)->fi_info->addr_format);
#else
    return na_ofi_raw_addr_serialize_size(
               (int) NA_OFI_CLASS(na_class)->fi_info->addr_format) +
           sizeof(uint64_t);
#endif
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_serialize(
    na_class_t *na_class, void *buf, size_t buf_size, na_addr_t *addr)
{
    const struct na_ofi_addr_key *addr_key =
        &((struct na_ofi_addr *) addr)->addr_key;

#ifdef NA_OFI_ADDR_OPT
    return na_ofi_raw_addr_serialize(
        (int) NA_OFI_CLASS(na_class)->fi_info->addr_format, buf, buf_size,
        &addr_key->addr);
#else
    char *buf_ptr = (char *) buf;
    size_t buf_size_left = buf_size;
    int addr_format = (int) NA_OFI_CLASS(na_class)->fi_info->addr_format;
    uint64_t len = (uint64_t) na_ofi_raw_addr_serialize_size(addr_format);
    na_return_t ret;

    NA_ENCODE(error, ret, buf_ptr, buf_size_left, &len, uint64_t);

    return na_ofi_raw_addr_serialize(
        addr_format, buf_ptr, buf_size_left, &addr_key->addr);

error:
    return ret;
#endif
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_addr_deserialize(
    na_class_t *na_class, na_addr_t **addr_p, const void *buf, size_t buf_size)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_addr_key addr_key;
    union na_ofi_auth_key auth_key;
    fi_addr_t fi_auth_key = FI_ADDR_NOTAVAIL;
    int addr_format = (int) na_ofi_class->fi_info->addr_format;
    struct na_ofi_addr *na_ofi_addr = NULL;
    na_return_t ret;

#ifdef NA_OFI_ADDR_OPT
    /* Deserialize raw address */
    ret = na_ofi_raw_addr_deserialize(
        addr_format, &addr_key.addr, &auth_key, buf, buf_size);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not deserialize address key");
#else
    const char *buf_ptr = (const char *) buf;
    size_t buf_size_left = buf_size;
    uint64_t len;

    NA_DECODE(error, ret, buf_ptr, buf_size_left, &len, uint64_t);
    NA_CHECK_SUBSYS_ERROR(addr,
        len != (uint64_t) na_ofi_raw_addr_serialize_size(addr_format), error,
        ret, NA_PROTOCOL_ERROR,
        "Address size mismatch (got %" PRIu64 ", expected %zu)", len,
        na_ofi_raw_addr_serialize_size(addr_format));

    /* Deserialize raw address */
    ret = na_ofi_raw_addr_deserialize(
        addr_format, &addr_key.addr, &auth_key, buf_ptr, buf_size_left);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not deserialize address key");
#endif

#if FI_VERSION_GE(FI_COMPILE_VERSION, FI_VERSION(1, 20))
    if (na_ofi_class->domain->av_auth_key) {
        fi_auth_key = na_ofi_auth_key_lookup(
            na_ofi_class->domain->auth_key_map, &auth_key);
        NA_CHECK_SUBSYS_ERROR(addr, fi_auth_key == FI_ADDR_NOTAVAIL, error, ret,
            NA_NOENTRY, "Could not find auth key");
    }
#endif

    /* Create key from addr for faster lookups */
    addr_key.val = na_ofi_raw_addr_to_key(addr_format, &addr_key.addr);
    NA_CHECK_SUBSYS_ERROR(addr, addr_key.val == 0, error, ret,
        NA_PROTONOSUPPORT, "Could not generate key from addr");

    /* Lookup key and create new addr if it does not exist */
    ret = na_ofi_addr_key_lookup(
        na_ofi_class, &addr_key, fi_auth_key, &na_ofi_addr);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret, "Could not lookup address key");

    *addr_p = (na_addr_t *) na_ofi_addr;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_msg_get_max_unexpected_size(const na_class_t *na_class)
{
    return NA_OFI_CLASS(na_class)->endpoint->unexpected_msg_size_max;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_msg_get_max_expected_size(const na_class_t *na_class)
{
    return NA_OFI_CLASS(na_class)->endpoint->expected_msg_size_max;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_msg_get_unexpected_header_size(const na_class_t *na_class)
{
    if (!(NA_OFI_CLASS(na_class)->fi_info->caps & FI_SOURCE_ERR))
        return na_ofi_raw_addr_serialize_size(
            (int) NA_OFI_CLASS(na_class)->fi_info->addr_format);

    return 0;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE na_tag_t
na_ofi_msg_get_max_tag(const na_class_t *na_class)
{
    return (na_tag_t) NA_OFI_CLASS(na_class)->domain->max_tag;
}

/*---------------------------------------------------------------------------*/
static void *
na_ofi_msg_buf_alloc(
    na_class_t *na_class, size_t size, unsigned long flags, void **plugin_data)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_msg_buf_handle *msg_buf_handle = NULL;
    void *mem_ptr = NULL;

    msg_buf_handle =
        (struct na_ofi_msg_buf_handle *) calloc(1, sizeof(*msg_buf_handle));
    NA_CHECK_SUBSYS_ERROR_NORET(mem, msg_buf_handle == NULL, error,
        "Could not allocate msg_buf_handle");
    msg_buf_handle->flags = flags;

    /* Multi-recv buffers do not need to use the memory pool */
    if (flags & NA_MULTI_RECV) {
        mem_ptr = na_ofi_mem_alloc(na_ofi_class, size, &msg_buf_handle->flags,
            &msg_buf_handle->alloc_size, &msg_buf_handle->fi_mr);
        NA_CHECK_SUBSYS_ERROR_NORET(mem, mem_ptr == NULL, error,
            "Could not allocate %d bytes", (int) size);
    } else {
#ifdef NA_OFI_HAS_MEM_POOL
        struct hg_mem_pool *mem_pool = (flags & NA_SEND)
                                           ? na_ofi_class->send_pool
                                           : na_ofi_class->recv_pool;

        mem_ptr =
            hg_mem_pool_alloc(mem_pool, size, (void **) &msg_buf_handle->fi_mr);
        NA_CHECK_SUBSYS_ERROR_NORET(
            mem, mem_ptr == NULL, error, "Could not allocate buffer from pool");
        msg_buf_handle->alloc_size = size;
#else
        mem_ptr = na_ofi_mem_alloc(na_ofi_class, size, &msg_buf_handle->flags,
            &msg_buf_handle->alloc_size, &msg_buf_handle->fi_mr);
        NA_CHECK_SUBSYS_ERROR_NORET(mem, mem_ptr == NULL, error,
            "Could not allocate %d bytes", (int) size);
#endif
    }
    *plugin_data = (void *) msg_buf_handle;

    return mem_ptr;

error:
    free(msg_buf_handle);
    return NULL;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_msg_buf_free(na_class_t *na_class, void *buf, void *plugin_data)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_msg_buf_handle *msg_buf_handle =
        (struct na_ofi_msg_buf_handle *) plugin_data;

    if (msg_buf_handle->flags & NA_MULTI_RECV) {
        na_ofi_mem_free(na_ofi_class, buf, msg_buf_handle->alloc_size,
            msg_buf_handle->flags, msg_buf_handle->fi_mr);
    } else {
#ifdef NA_OFI_HAS_MEM_POOL
        struct hg_mem_pool *mem_pool = (msg_buf_handle->flags & NA_SEND)
                                           ? na_ofi_class->send_pool
                                           : na_ofi_class->recv_pool;

        hg_mem_pool_free(mem_pool, buf, (void *) msg_buf_handle->fi_mr);
#else
        na_ofi_mem_free(na_ofi_class, buf, msg_buf_handle->alloc_size,
            msg_buf_handle->flags, msg_buf_handle->fi_mr);
#endif
    }
    free(msg_buf_handle);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_init_unexpected(na_class_t *na_class, void *buf, size_t buf_size)
{
    /* For providers that don't support FI_SOURCE_ERR, insert the msg header
     * to piggyback the source address for unexpected message. */
    if (!(NA_OFI_CLASS(na_class)->fi_info->caps & FI_SOURCE_ERR))
        return na_ofi_raw_addr_serialize(
            (int) NA_OFI_CLASS(na_class)->fi_info->addr_format, buf, buf_size,
            &NA_OFI_CLASS(na_class)->endpoint->src_addr->addr_key.addr);

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_send_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    struct na_ofi_addr *na_ofi_addr = (struct na_ofi_addr *) dest_addr;
    struct fid_mr *fi_mr =
        (plugin_data) ? ((struct na_ofi_msg_buf_handle *) plugin_data)->fi_mr
                      : NULL;
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ofi_op_id->type));
    NA_CHECK_SUBSYS_ERROR(msg,
        buf_size > na_ofi_class->endpoint->unexpected_msg_size_max, error, ret,
        NA_INVALID_ARG, "Invalid msg size (%zu > %zu)", buf_size,
        na_ofi_class->endpoint->unexpected_msg_size_max);

    NA_OFI_OP_RESET(na_ofi_op_id, context, FI_SEND, NA_CB_SEND_UNEXPECTED,
        callback, arg, na_ofi_addr);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ofi_op_id->info.msg = (struct na_ofi_msg_info){.buf.const_ptr = buf,
        .buf_size = buf_size,
        .fi_addr = na_ofi_class->use_sep ? fi_rx_addr(na_ofi_addr->fi_addr,
                                               dest_id, NA_OFI_SEP_RX_CTX_BITS)
                                         : na_ofi_addr->fi_addr,
        .desc = (fi_mr) ? fi_mr_desc(fi_mr) : NULL,
        .tag = (uint64_t) tag | NA_OFI_UNEXPECTED_TAG};

    /* OPX requires context2 to pass persistent address down to provider */
    if ((int) na_ofi_class->fi_info->addr_format == FI_ADDR_OPX)
        na_ofi_op_id->fi_ctx[0].internal[0] = &na_ofi_addr->addr_key.addr.opx;

    ret = na_ofi_class->msg_send_unexpected(
        na_ofi_context->fi_tx, &na_ofi_op_id->info.msg, &na_ofi_op_id->fi_ctx);
    if (ret != NA_SUCCESS) {
        if (ret == NA_AGAIN) {
            na_ofi_op_id->retry_op.msg = na_ofi_class->msg_send_unexpected;
            na_ofi_op_retry(
                na_ofi_context, na_ofi_class->op_retry_timeout, na_ofi_op_id);
        } else
            NA_GOTO_SUBSYS_ERROR_NORET(msg, release, "Could not post msg send");
    }

    return NA_SUCCESS;

release:
    NA_OFI_OP_RELEASE(na_ofi_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    struct fid_mr *fi_mr =
        (plugin_data) ? ((struct na_ofi_msg_buf_handle *) plugin_data)->fi_mr
                      : NULL;
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ofi_op_id->type));

    NA_OFI_OP_RESET(na_ofi_op_id, context, FI_RECV, NA_CB_RECV_UNEXPECTED,
        callback, arg, NULL);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ofi_op_id->info.msg = (struct na_ofi_msg_info){.buf.ptr = buf,
        .buf_size = buf_size,
        .fi_addr = FI_ADDR_UNSPEC,
        .desc = (fi_mr) ? fi_mr_desc(fi_mr) : NULL,
        .tag = NA_OFI_UNEXPECTED_TAG,
        .tag_mask = NA_OFI_TAG_MASK};

    ret = na_ofi_class->msg_recv_unexpected(
        na_ofi_context->fi_rx, &na_ofi_op_id->info.msg, &na_ofi_op_id->fi_ctx);
    if (ret != NA_SUCCESS) {
        if (ret == NA_AGAIN) {
            na_ofi_op_id->retry_op.msg = na_ofi_class->msg_recv_unexpected;
            na_ofi_op_retry(
                na_ofi_context, na_ofi_class->op_retry_timeout, na_ofi_op_id);
        } else
            NA_GOTO_SUBSYS_ERROR_NORET(msg, release, "Could not post msg recv");
    }

    return NA_SUCCESS;

release:
    NA_OFI_OP_RELEASE(na_ofi_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_multi_recv_unexpected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_op_id_t *op_id)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    struct fid_mr *fi_mr =
        (plugin_data) ? ((struct na_ofi_msg_buf_handle *) plugin_data)->fi_mr
                      : NULL;
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ofi_op_id->type));

    NA_OFI_OP_RESET(na_ofi_op_id, context, FI_RECV, NA_CB_MULTI_RECV_UNEXPECTED,
        callback, arg, NULL);
    na_ofi_op_id->completion_data_storage.multi.completion_count = 0;

    /* Add operation ID to context multi-op queue for tracking */
    hg_thread_spin_lock(&na_ofi_context->multi_op_queue.lock);
    TAILQ_INSERT_TAIL(
        &na_ofi_context->multi_op_queue.queue, na_ofi_op_id, multi);
    hg_atomic_incr32(&na_ofi_context->multi_op_count);
    hg_thread_spin_unlock(&na_ofi_context->multi_op_queue.lock);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ofi_op_id->info.msg = (struct na_ofi_msg_info){.buf.ptr = buf,
        .buf_size = buf_size,
        .fi_addr = FI_ADDR_UNSPEC,
        .desc = (fi_mr) ? fi_mr_desc(fi_mr) : NULL,
        .tag = 0};

    ret = na_ofi_msg_multi_recv(
        na_ofi_context->fi_rx, &na_ofi_op_id->info.msg, &na_ofi_op_id->fi_ctx);
    if (ret != NA_SUCCESS) {
        if (ret == NA_AGAIN) {
            na_ofi_op_id->retry_op.msg = na_ofi_msg_multi_recv;
            na_ofi_op_retry(
                na_ofi_context, na_ofi_class->op_retry_timeout, na_ofi_op_id);
        } else
            NA_GOTO_SUBSYS_ERROR_NORET(
                msg, release, "Could not post msg multi recv");
    }

    return NA_SUCCESS;

release:
    hg_thread_spin_lock(&na_ofi_context->multi_op_queue.lock);
    TAILQ_REMOVE(&na_ofi_context->multi_op_queue.queue, na_ofi_op_id, multi);
    hg_atomic_decr32(&na_ofi_context->multi_op_count);
    hg_thread_spin_unlock(&na_ofi_context->multi_op_queue.lock);
    NA_OFI_OP_RELEASE(na_ofi_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_send_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, const void *buf, size_t buf_size,
    void *plugin_data, na_addr_t *dest_addr, uint8_t dest_id, na_tag_t tag,
    na_op_id_t *op_id)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    struct na_ofi_addr *na_ofi_addr = (struct na_ofi_addr *) dest_addr;
    struct fid_mr *fi_mr =
        (plugin_data) ? ((struct na_ofi_msg_buf_handle *) plugin_data)->fi_mr
                      : NULL;
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ofi_op_id->type));
    NA_CHECK_SUBSYS_ERROR(msg,
        buf_size > na_ofi_class->endpoint->expected_msg_size_max, error, ret,
        NA_INVALID_ARG, "Invalid msg size (%zu > %zu)", buf_size,
        na_ofi_class->endpoint->expected_msg_size_max);

    NA_OFI_OP_RESET(na_ofi_op_id, context, FI_SEND, NA_CB_SEND_EXPECTED,
        callback, arg, na_ofi_addr);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ofi_op_id->info.msg = (struct na_ofi_msg_info){.buf.const_ptr = buf,
        .buf_size = buf_size,
        .fi_addr = na_ofi_class->use_sep ? fi_rx_addr(na_ofi_addr->fi_addr,
                                               dest_id, NA_OFI_SEP_RX_CTX_BITS)
                                         : na_ofi_addr->fi_addr,
        .desc = (fi_mr) ? fi_mr_desc(fi_mr) : NULL,
        .tag = tag};

    /* OPX requires context2 to pass persistent address down to provider */
    if ((int) na_ofi_class->fi_info->addr_format == FI_ADDR_OPX)
        na_ofi_op_id->fi_ctx[0].internal[0] = &na_ofi_addr->addr_key.addr.opx;

    ret = na_ofi_tag_send(
        na_ofi_context->fi_tx, &na_ofi_op_id->info.msg, &na_ofi_op_id->fi_ctx);
    if (ret != NA_SUCCESS) {
        if (ret == NA_AGAIN) {
            na_ofi_op_id->retry_op.msg = na_ofi_tag_send;
            na_ofi_op_retry(
                na_ofi_context, na_ofi_class->op_retry_timeout, na_ofi_op_id);
        } else
            NA_GOTO_SUBSYS_ERROR_NORET(msg, release, "Could not post tag send");
    }

    return NA_SUCCESS;

release:
    NA_OFI_OP_RELEASE(na_ofi_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_msg_recv_expected(na_class_t *na_class, na_context_t *context,
    na_cb_t callback, void *arg, void *buf, size_t buf_size, void *plugin_data,
    na_addr_t *source_addr, uint8_t source_id, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    struct na_ofi_addr *na_ofi_addr = (struct na_ofi_addr *) source_addr;
    struct fid_mr *fi_mr =
        (plugin_data) ? ((struct na_ofi_msg_buf_handle *) plugin_data)->fi_mr
                      : NULL;
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) op_id;
    na_return_t ret;

    /* Check op_id */
    NA_CHECK_SUBSYS_ERROR(op, na_ofi_op_id == NULL, error, ret, NA_INVALID_ARG,
        "Invalid operation ID");
    NA_CHECK_SUBSYS_ERROR(op,
        !(hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_COMPLETED), error,
        ret, NA_BUSY, "Attempting to use OP ID that was not completed (%s)",
        na_cb_type_to_string(na_ofi_op_id->type));

    NA_OFI_OP_RESET(na_ofi_op_id, context, FI_RECV, NA_CB_RECV_EXPECTED,
        callback, arg, na_ofi_addr);

    /* We assume buf remains valid (safe because we pre-allocate buffers) */
    na_ofi_op_id->info.msg = (struct na_ofi_msg_info){.buf.ptr = buf,
        .buf_size = buf_size,
        .fi_addr = na_ofi_class->use_sep
                       ? fi_rx_addr(na_ofi_addr->fi_addr, source_id,
                             NA_OFI_SEP_RX_CTX_BITS)
                       : na_ofi_addr->fi_addr,
        .desc = (fi_mr) ? fi_mr_desc(fi_mr) : NULL,
        .tag = tag,
        .tag_mask = 0};

    ret = na_ofi_tag_recv(
        na_ofi_context->fi_rx, &na_ofi_op_id->info.msg, &na_ofi_op_id->fi_ctx);
    if (ret != NA_SUCCESS) {
        if (ret == NA_AGAIN) {
            na_ofi_op_id->retry_op.msg = na_ofi_tag_recv;
            na_ofi_op_retry(
                na_ofi_context, na_ofi_class->op_retry_timeout, na_ofi_op_id);
        } else
            NA_GOTO_SUBSYS_ERROR_NORET(msg, release, "Could not post tag recv");
    }

    return NA_SUCCESS;

release:
    NA_OFI_OP_RELEASE(na_ofi_op_id);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_mem_handle_create(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, unsigned long flags, na_mem_handle_t **mem_handle_p)
{
    struct na_ofi_mem_handle *na_ofi_mem_handle = NULL;
    na_return_t ret;

    /* Allocate memory handle */
    na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) calloc(1, sizeof(*na_ofi_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_ofi_mem_handle == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA OFI memory handle");

    na_ofi_mem_handle->desc.iov.s[0].iov_base = buf;
    na_ofi_mem_handle->desc.iov.s[0].iov_len = buf_size;
    na_ofi_mem_handle->desc.info.iovcnt = 1;
    na_ofi_mem_handle->desc.info.flags = flags & 0xff;
    na_ofi_mem_handle->desc.info.len = buf_size;

    *mem_handle_p = (na_mem_handle_t *) na_ofi_mem_handle;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_mem_handle_create_segments(na_class_t *na_class,
    struct na_segment *segments, size_t segment_count, unsigned long flags,
    na_mem_handle_t **mem_handle_p)
{
    struct na_ofi_mem_handle *na_ofi_mem_handle = NULL;
    struct iovec *iov = NULL;
    na_return_t ret;
    size_t i;

    NA_CHECK_SUBSYS_WARNING(mem, segment_count == 1, "Segment count is 1");

    /* Check that we do not exceed IOV limit */
    NA_CHECK_SUBSYS_ERROR(fatal,
        segment_count >
            NA_OFI_CLASS(na_class)->fi_info->domain_attr->mr_iov_limit,
        error, ret, NA_INVALID_ARG,
        "Segment count exceeds provider limit (%zu)",
        NA_OFI_CLASS(na_class)->fi_info->domain_attr->mr_iov_limit);

    /* Allocate memory handle */
    na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) calloc(1, sizeof(*na_ofi_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_ofi_mem_handle == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA OFI memory handle");

    if (segment_count > NA_OFI_IOV_STATIC_MAX) {
        /* Allocate IOVs */
        na_ofi_mem_handle->desc.iov.d =
            (struct iovec *) calloc(segment_count, sizeof(struct iovec));
        NA_CHECK_SUBSYS_ERROR(mem, na_ofi_mem_handle->desc.iov.d == NULL, error,
            ret, NA_NOMEM, "Could not allocate IOV array");

        iov = na_ofi_mem_handle->desc.iov.d;
    } else
        iov = na_ofi_mem_handle->desc.iov.s;

    na_ofi_mem_handle->desc.info.len = 0;
    for (i = 0; i < segment_count; i++) {
        iov[i].iov_base = (void *) segments[i].base;
        iov[i].iov_len = segments[i].len;
        na_ofi_mem_handle->desc.info.len += iov[i].iov_len;
    }
    na_ofi_mem_handle->desc.info.iovcnt = (uint64_t) segment_count;
    na_ofi_mem_handle->desc.info.flags = flags & 0xff;

    *mem_handle_p = (na_mem_handle_t *) na_ofi_mem_handle;

    return NA_SUCCESS;

error:
    if (na_ofi_mem_handle) {
        if (segment_count > NA_OFI_IOV_STATIC_MAX)
            free(na_ofi_mem_handle->desc.iov.d);
        free(na_ofi_mem_handle);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_ofi_mem_handle_free(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    struct na_ofi_mem_handle *na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) mem_handle;

    if (na_ofi_mem_handle->desc.info.iovcnt > NA_OFI_IOV_STATIC_MAX)
        free(na_ofi_mem_handle->desc.iov.d);
    free(na_ofi_mem_handle);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_mem_handle_get_max_segments(const na_class_t *na_class)
{
#ifdef NA_OFI_USE_REGV
    return NA_OFI_CLASS(na_class)->fi_info->domain_attr->mr_iov_limit;
#else
    (void) na_class;
    return 1;
#endif
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_mem_register(na_class_t *na_class, na_mem_handle_t *mem_handle,
    enum na_mem_type mem_type, uint64_t device)
{
    struct na_ofi_mem_handle *na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) mem_handle;
    struct na_ofi_domain *domain = NA_OFI_CLASS(na_class)->domain;
    const struct fi_info *fi_info = NA_OFI_CLASS(na_class)->fi_info;
    int32_t mr_cnt = hg_atomic_get32(domain->mr_reg_count);
    struct fi_mr_attr fi_mr_attr = {
        .mr_iov = NA_OFI_IOV(
            na_ofi_mem_handle->desc.iov, na_ofi_mem_handle->desc.info.iovcnt),
        .iov_count = (size_t) na_ofi_mem_handle->desc.info.iovcnt,
        .context = NULL,
        .auth_key_size = 0,
        .auth_key = NULL,
        .iface = FI_HMEM_SYSTEM,
        .device.reserved = 0};
    na_return_t ret;
    int rc;

    /* Set access mode */
    switch (na_ofi_mem_handle->desc.info.flags) {
        case NA_MEM_READ_ONLY:
            fi_mr_attr.access = FI_REMOTE_READ | FI_WRITE;
            break;
        case NA_MEM_WRITE_ONLY:
            fi_mr_attr.access = FI_REMOTE_WRITE | FI_READ;
            break;
        case NA_MEM_READWRITE:
            fi_mr_attr.access =
                FI_READ | FI_WRITE | FI_REMOTE_READ | FI_REMOTE_WRITE;
            break;
        default:
            NA_GOTO_SUBSYS_ERROR(
                mem, error, ret, NA_INVALID_ARG, "Invalid memory access flag");
            break;
    }

    /* Set memory type */
    switch (mem_type) {
        case NA_MEM_TYPE_CUDA:
            fi_mr_attr.iface = FI_HMEM_CUDA;
            fi_mr_attr.device.cuda = (int) device;
            break;
        case NA_MEM_TYPE_ROCM:
            fi_mr_attr.iface = FI_HMEM_ROCR;
            break;
        case NA_MEM_TYPE_ZE:
            fi_mr_attr.iface = FI_HMEM_ZE;
            fi_mr_attr.device.ze = (int) device;
            break;
        case NA_MEM_TYPE_HOST:
        case NA_MEM_TYPE_UNKNOWN:
        default:
            break;
    }
    NA_CHECK_SUBSYS_ERROR(mem,
        !(fi_info->caps & FI_HMEM) && (fi_mr_attr.iface != FI_HMEM_SYSTEM),
        error, ret, NA_OPNOTSUPPORTED,
        "selected provider does not support device registration");

    /* Let the provider provide its own key otherwise generate our own */
    fi_mr_attr.requested_key = (fi_info->domain_attr->mr_mode & FI_MR_PROV_KEY)
                                   ? 0
                                   : na_ofi_mem_key_gen(domain);

    /* Register region */
    rc = fi_mr_regattr(domain->fi_domain, &fi_mr_attr, 0 /* flags */,
        &na_ofi_mem_handle->fi_mr);
    NA_CHECK_SUBSYS_ERROR(mem, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
        "fi_mr_regattr() failed, rc: %d (%s), mr_reg_count: %d", rc,
        fi_strerror(-rc), mr_cnt);
    hg_atomic_incr32(domain->mr_reg_count);

    /* Attach MR to endpoint when provider requests it */
    if (fi_info->domain_attr->mr_mode & FI_MR_ENDPOINT) {
        struct na_ofi_endpoint *endpoint = NA_OFI_CLASS(na_class)->endpoint;

        rc = fi_mr_bind(na_ofi_mem_handle->fi_mr, &endpoint->fi_ep->fid, 0);
        NA_CHECK_SUBSYS_ERROR(mem, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_mr_bind() failed, rc: %d (%s)", rc, fi_strerror(-rc));

        rc = fi_mr_enable(na_ofi_mem_handle->fi_mr);
        NA_CHECK_SUBSYS_ERROR(mem, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_mr_enable() failed, rc: %d (%s), mr_reg_count: %d", rc,
            fi_strerror(-rc), mr_cnt);
    }

    /* Retrieve key */
    na_ofi_mem_handle->desc.info.fi_mr_key =
        fi_mr_key(na_ofi_mem_handle->fi_mr);

    return NA_SUCCESS;

error:
    if (na_ofi_mem_handle->fi_mr) {
        (void) fi_close(&na_ofi_mem_handle->fi_mr->fid);
        hg_atomic_decr32(domain->mr_reg_count);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_mem_deregister(na_class_t *na_class, na_mem_handle_t *mem_handle)
{
    struct na_ofi_domain *domain = NA_OFI_CLASS(na_class)->domain;
    struct na_ofi_mem_handle *na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) mem_handle;
    na_return_t ret;
    int rc;

    /* close MR handle */
    if (na_ofi_mem_handle->fi_mr != NULL) {
        rc = fi_close(&na_ofi_mem_handle->fi_mr->fid);
        NA_CHECK_SUBSYS_ERROR(mem, rc != 0, error, ret, na_ofi_errno_to_na(-rc),
            "fi_close() mr_hdl failed, rc: %d (%s)", rc, fi_strerror(-rc));
        hg_atomic_decr32(domain->mr_reg_count);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE size_t
na_ofi_mem_handle_get_serialize_size(
    na_class_t NA_UNUSED *na_class, na_mem_handle_t *mem_handle)
{
    struct na_ofi_mem_handle *na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) mem_handle;

    return sizeof(na_ofi_mem_handle->desc.info) +
           na_ofi_mem_handle->desc.info.iovcnt * sizeof(struct iovec);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_mem_handle_serialize(na_class_t NA_UNUSED *na_class, void *buf,
    size_t buf_size, na_mem_handle_t *mem_handle)
{
    struct na_ofi_mem_handle *na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) mem_handle;
    const struct iovec *iov = NA_OFI_IOV(
        na_ofi_mem_handle->desc.iov, na_ofi_mem_handle->desc.info.iovcnt);
    char *buf_ptr = (char *) buf;
    size_t buf_size_left = buf_size;
    na_return_t ret;

    /* Descriptor info */
    NA_ENCODE(error, ret, buf_ptr, buf_size_left, &na_ofi_mem_handle->desc.info,
        struct na_ofi_mem_desc_info);

    /* IOV */
    NA_ENCODE_ARRAY(error, ret, buf_ptr, buf_size_left, iov, const struct iovec,
        na_ofi_mem_handle->desc.info.iovcnt);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_mem_handle_deserialize(na_class_t NA_UNUSED *na_class,
    na_mem_handle_t **mem_handle_p, const void *buf, size_t buf_size)
{
    struct na_ofi_mem_handle *na_ofi_mem_handle = NULL;
    const char *buf_ptr = (const char *) buf;
    size_t buf_size_left = buf_size;
    struct iovec *iov = NULL;
    na_return_t ret;

    na_ofi_mem_handle =
        (struct na_ofi_mem_handle *) malloc(sizeof(*na_ofi_mem_handle));
    NA_CHECK_SUBSYS_ERROR(mem, na_ofi_mem_handle == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA OFI memory handle");
    na_ofi_mem_handle->desc.iov.d = NULL;
    na_ofi_mem_handle->fi_mr = NULL;
    na_ofi_mem_handle->desc.info.iovcnt = 0;

    /* Descriptor info */
    NA_DECODE(error, ret, buf_ptr, buf_size_left, &na_ofi_mem_handle->desc.info,
        struct na_ofi_mem_desc_info);

    /* IOV */
    if (na_ofi_mem_handle->desc.info.iovcnt > NA_OFI_IOV_STATIC_MAX) {
        /* Allocate IOV */
        na_ofi_mem_handle->desc.iov.d = (struct iovec *) malloc(
            na_ofi_mem_handle->desc.info.iovcnt * sizeof(struct iovec));
        NA_CHECK_SUBSYS_ERROR(mem, na_ofi_mem_handle->desc.iov.d == NULL, error,
            ret, NA_NOMEM, "Could not allocate segment array");

        iov = na_ofi_mem_handle->desc.iov.d;
    } else
        iov = na_ofi_mem_handle->desc.iov.s;

    NA_DECODE_ARRAY(error, ret, buf_ptr, buf_size_left, iov, struct iovec,
        na_ofi_mem_handle->desc.info.iovcnt);

    *mem_handle_p = (na_mem_handle_t *) na_ofi_mem_handle;

    return NA_SUCCESS;

error:
    if (na_ofi_mem_handle) {
        if (na_ofi_mem_handle->desc.info.iovcnt > NA_OFI_IOV_STATIC_MAX)
            free(na_ofi_mem_handle->desc.iov.d);
        free(na_ofi_mem_handle);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id, na_op_id_t *op_id)
{
    return na_ofi_rma_common(NA_OFI_CLASS(na_class), context, NA_CB_PUT,
        callback, arg, fi_writemsg, "fi_writemsg",
        FI_COMPLETION | FI_DELIVERY_COMPLETE,
        (struct na_ofi_mem_handle *) local_mem_handle, local_offset,
        (struct na_ofi_mem_handle *) remote_mem_handle, remote_offset, length,
        (struct na_ofi_addr *) remote_addr, remote_id,
        (struct na_ofi_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t *local_mem_handle, na_offset_t local_offset,
    na_mem_handle_t *remote_mem_handle, na_offset_t remote_offset,
    size_t length, na_addr_t *remote_addr, uint8_t remote_id, na_op_id_t *op_id)
{
    return na_ofi_rma_common(NA_OFI_CLASS(na_class), context, NA_CB_GET,
        callback, arg, fi_readmsg, "fi_readmsg", FI_COMPLETION,
        (struct na_ofi_mem_handle *) local_mem_handle, local_offset,
        (struct na_ofi_mem_handle *) remote_mem_handle, remote_offset, length,
        (struct na_ofi_addr *) remote_addr, remote_id,
        (struct na_ofi_op_id *) op_id);
}

/*---------------------------------------------------------------------------*/
static NA_INLINE int
na_ofi_poll_get_fd(na_class_t *na_class, na_context_t *context)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    int fd = -1, rc;

    if (na_ofi_class->no_wait ||
        (na_ofi_prov_flags[na_ofi_class->fabric->prov_type] & NA_OFI_WAIT_SET))
        goto out;

    rc = fi_control(&na_ofi_context->eq->fi_cq->fid, FI_GETWAIT, &fd);
    NA_CHECK_SUBSYS_ERROR_NORET(poll, rc != 0 && rc != -FI_ENOSYS, out,
        "fi_control() failed, rc: %d (%s)", rc, fi_strerror((int) -rc));
    NA_CHECK_SUBSYS_ERROR_NORET(
        poll, fd < 0, out, "Returned fd is not valid (%d), will not block", fd);

out:
    return fd;
}

/*---------------------------------------------------------------------------*/
static NA_INLINE bool
na_ofi_poll_try_wait(na_class_t *na_class, na_context_t *context)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    struct fid *fids[1];
    bool retry_queue_empty;
    int rc;

    if (na_ofi_class->no_wait)
        return false;

    /* Keep making progress if retry queue is not empty */
    hg_thread_spin_lock(&na_ofi_context->eq->retry_op_queue->lock);
    retry_queue_empty = TAILQ_EMPTY(&na_ofi_context->eq->retry_op_queue->queue);
    hg_thread_spin_unlock(&na_ofi_context->eq->retry_op_queue->lock);
    if (!retry_queue_empty)
        return false;

    /* Assume it is safe to block if provider is using wait set */
    if ((na_ofi_prov_flags[na_ofi_class->fabric->prov_type] & NA_OFI_WAIT_SET)
        /* PSM2 shows very slow performance with fi_trywait() */
        || na_ofi_class->fabric->prov_type == NA_OFI_PROV_PSM2)
        return true;

    fids[0] = &na_ofi_context->eq->fi_cq->fid;
    /* Check whether it is safe to block on that fd */
    rc = fi_trywait(na_ofi_class->fabric->fi_fabric, fids, 1);
    if (rc == FI_SUCCESS)
        return true;
    else if (rc == -FI_EAGAIN)
        return false;
    else {
        NA_LOG_SUBSYS_ERROR(poll, "fi_trywait() failed, rc: %d (%s)", rc,
            fi_strerror((int) -rc));
        return false;
    }
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_poll(na_class_t *na_class, na_context_t *context, unsigned int *count_p)
{
    struct na_ofi_class *na_ofi_class = NA_OFI_CLASS(na_class);
    struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
    unsigned int count = 0;
    na_return_t ret;

    /* If we can't hold more than NA_OFI_CQ_EVENT_NUM entries do not attempt
     * to read from CQ until NA_Trigger() has been called */
    if ((hg_atomic_get32(&na_ofi_context->multi_op_count) > 0) &&
        !na_ofi_cq_can_poll_multi(&na_ofi_context->multi_op_queue, count_p))
        return NA_SUCCESS;

    /* Read from CQ and process events */
    ret = na_ofi_class->cq_poll(na_ofi_class, na_ofi_context, &count);
    NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret, "Could not poll context CQ");

    /* Attempt to process retries */
    ret = na_ofi_cq_process_retries(
        na_ofi_context, na_ofi_class->op_retry_period);
    NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret, "Could not process retries");

    /* PSM2 is a user-level interface, to prevent busy-spin and allow
     * other threads to be scheduled, we need to yield here. */
    if ((na_ofi_class->fabric->prov_type == NA_OFI_PROV_PSM2) && (count == 0))
        hg_thread_yield();

    if (count_p != NULL)
        *count_p = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_poll_wait(na_class_t *na_class, na_context_t *context,
    unsigned int timeout_ms, unsigned int *count_p)
{
    hg_time_t deadline, now = hg_time_from_ms(0);
    na_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    do {
        struct na_ofi_context *na_ofi_context = NA_OFI_CONTEXT(context);
        unsigned int count = 0;

        if (timeout_ms != 0 && na_ofi_context->eq->fi_wait != NULL) {
            /* Wait in wait set if provider does not support wait on FDs */
            int rc = fi_wait(na_ofi_context->eq->fi_wait,
                (int) hg_time_to_ms(hg_time_subtract(deadline, now)));

            if (rc == -FI_EINTR) {
                hg_time_get_current_ms(&now);
                continue;
            }
            if (rc == -FI_ETIMEDOUT)
                break;

            NA_CHECK_SUBSYS_ERROR(poll, rc != 0, error, ret,
                na_ofi_errno_to_na(-rc), "fi_wait() failed, rc: %d (%s)", rc,
                fi_strerror(-rc));
        }

        ret = na_ofi_poll(na_class, context, &count);
        NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret, "Could not poll");

        if (count > 0) {
            if (count_p != NULL)
                *count_p = count;
            return NA_SUCCESS;
        }

        if (timeout_ms != 0)
            hg_time_get_current_ms(&now);
    } while (hg_time_less(now, deadline));

    return NA_TIMEOUT;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_ofi_cancel(
    na_class_t NA_UNUSED *na_class, na_context_t *context, na_op_id_t *op_id)
{
    struct na_ofi_op_id *na_ofi_op_id = (struct na_ofi_op_id *) op_id;
    int32_t status;
    na_return_t ret;

    /* Exit if op has already completed */
    status = hg_atomic_get32(&na_ofi_op_id->status);
    if ((status & NA_OFI_OP_COMPLETED) || (status & NA_OFI_OP_ERRORED) ||
        (status & NA_OFI_OP_CANCELED) || (status & NA_OFI_OP_CANCELING))
        return NA_SUCCESS;

    NA_LOG_SUBSYS_DEBUG(op, "Canceling operation ID %p (%s)",
        (void *) na_ofi_op_id, na_cb_type_to_string(na_ofi_op_id->type));

    /* Must set canceling before we check for the retry queue */
    hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_CANCELING);

    /* Check if op_id is in retry queue */
    if (hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_QUEUED) {
        struct na_ofi_op_queue *op_queue =
            NA_OFI_CONTEXT(context)->eq->retry_op_queue;
        bool canceled = false;

        /* If dequeued by process_retries() in the meantime, we'll just let it
         * cancel there */

        hg_thread_spin_lock(&op_queue->lock);
        if (hg_atomic_get32(&na_ofi_op_id->status) & NA_OFI_OP_QUEUED) {
            TAILQ_REMOVE(&op_queue->queue, na_ofi_op_id, retry);
            hg_atomic_and32(&na_ofi_op_id->status, ~NA_OFI_OP_QUEUED);
            hg_atomic_or32(&na_ofi_op_id->status, NA_OFI_OP_CANCELED);
            canceled = true;
        }
        hg_thread_spin_unlock(&op_queue->lock);

        if (canceled)
            na_ofi_op_id->complete(na_ofi_op_id, true, NA_CANCELED);
    } else {
        ret = na_ofi_op_cancel(na_ofi_op_id);
        NA_CHECK_SUBSYS_NA_ERROR(op, error, ret, "Could not cancel operation");
    }

    return NA_SUCCESS;

error:
    return ret;
}
